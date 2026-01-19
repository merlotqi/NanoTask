#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include "any_task.hpp"
#include "state_storage.hpp"
#include "task_traits.hpp"
#include "threadpool.hpp"

namespace taskflow {

class TaskManager {
 public:
  static TaskManager& getInstance() {
    static TaskManager instance;
    return instance;
  }

  ~TaskManager() {
    stop_cleanup_ = true;
    cleanup_cv_.notify_all();  // Wake up cleanup thread
    if (cleanup_thread_.joinable()) {
      cleanup_thread_.join();
    }
  }

  TaskManager(const TaskManager&) = delete;
  TaskManager& operator=(const TaskManager&) = delete;

  // Submit task for execution
  template <typename Task>
  TaskID submit_task(Task task, TaskLifecycle lifecycle = TaskLifecycle::disposable) {
    TaskID id = generate_task_id();

    // Initialize state
    states_.set_state(id, TaskState::created);

    // Store persistent tasks for later reawakening
    if (lifecycle == TaskLifecycle::persistent) {
      std::unique_lock lock(persistent_tasks_mutex_);
      persistent_tasks_[id] = std::make_unique<AnyTask>(make_any_task(id, std::move(task), lifecycle));
    }

    // Submit to thread pool for execution
    if (thread_pool_) {
      thread_pool_->execute([this, id, task = std::move(task), lifecycle]() mutable {
        AnyTask any_task = make_any_task(id, std::move(task), lifecycle);
        if (any_task.valid()) {
          TaskRuntimeCtx rctx{id, &states_, [this](TaskID tid) { return is_task_cancelled(tid); }};
          any_task.execute_task(rctx, result_storage_.get());
        }
      });
    }

    return id;
  }

  // Reawaken a persistent task with new parameters
  template <typename Task>
  bool reawaken_task(TaskID id, Task new_task) {
    std::unique_lock lock(persistent_tasks_mutex_);
    auto it = persistent_tasks_.find(id);
    if (it == persistent_tasks_.end()) {
      return false;  // Task not found or not persistent
    }

    // Reset task state for reawakening
    states_.set_state(id, TaskState::created);

    // Update the stored task with new parameters
    *it->second = make_any_task(id, std::move(new_task), TaskLifecycle::persistent);

    // Submit for re-execution
    if (thread_pool_) {
      thread_pool_->execute([this, id]() {
        std::unique_lock lock(persistent_tasks_mutex_);
        auto it = persistent_tasks_.find(id);
        if (it != persistent_tasks_.end() && it->second->valid()) {
          TaskRuntimeCtx rctx{id, &states_, [this](TaskID tid) { return is_task_cancelled(tid); }};
          it->second->execute_task(rctx, result_storage_.get());
        }
      });
    }

    return true;
  }

  // Check if a task is persistent
  [[nodiscard]] bool is_persistent_task(TaskID id) const {
    std::shared_lock lock(persistent_tasks_mutex_);
    auto it = persistent_tasks_.find(id);
    return it != persistent_tasks_.end();
  }

  // Query task state
  [[nodiscard]] std::optional<TaskState> query_state(TaskID id) const { return states_.get_state(id); }

  // Get task progress - template version for custom types
  template <typename ProgressType>
  [[nodiscard]] std::optional<ProgressType> get_progress(TaskID id) const {
    return states_.get_progress<ProgressType>(id);
  }

  // Backward compatibility
  [[nodiscard]] std::optional<std::pair<float, std::string>> get_progress(TaskID id) const {
    return states_.get_progress<std::pair<float, std::string>>(id);
  }

  // Get task error message
  [[nodiscard]] std::optional<std::string> get_error(TaskID id) const { return states_.get_error(id); }

  // Get task result
  [[nodiscard]] std::optional<ResultPayload> get_result(TaskID id) const {
    if (auto locator = states_.get_result_locator(id)) {
      return result_storage_->get_result(*locator);
    }
    return std::nullopt;
  }

  // Cancel task - sets cancellation flag, task handles it internally
  bool cancel_task(TaskID id) {
    // Store cancellation flag in a thread-safe way
    std::unique_lock lock(cancellation_mutex_);
    cancelled_tasks_[id] = true;
    return states_.has_task(id);
  }

  // Check if task is cancelled
  [[nodiscard]] bool is_task_cancelled(TaskID id) const {
    std::shared_lock lock(cancellation_mutex_);
    auto it = cancelled_tasks_.find(id);
    return it != cancelled_tasks_.end() && it->second;
  }

  // Get statistics
  [[nodiscard]] StateStorage::Statistics get_statistics() const { return states_.get_statistics(); }

  // Cleanup completed tasks
  void cleanup_completed_tasks(std::chrono::hours max_age = std::chrono::hours(24)) {
    states_.cleanup_old_tasks(max_age);
  }

  // Start task processing (typically called once)
  void start_processing(size_t num_threads = std::thread::hardware_concurrency()) {
    if (processing_started_) return;

    processing_started_ = true;
    thread_pool_ = std::make_unique<ThreadPool>(num_threads);
  }

  // Stop task processing
  void stop_processing() {
    thread_pool_.reset();
    processing_started_ = false;
  }

 private:
  TaskManager() : cleanup_thread_([this] { cleanup_loop(); }), result_storage_(std::make_unique<SimpleResultStorage>()) {}

  TaskID generate_task_id() {
    static std::atomic<TaskID> next_id{1};
    return next_id.fetch_add(1, std::memory_order_relaxed);
  }

  void cleanup_loop() {
    while (!stop_cleanup_.load(std::memory_order_acquire)) {
      // Wait for either timeout or stop signal
      {
        std::unique_lock lock(cleanup_mutex_);
        cleanup_cv_.wait_for(lock, std::chrono::minutes(30),
                             [this] { return stop_cleanup_.load(std::memory_order_acquire); });
      }

      if (!stop_cleanup_.load(std::memory_order_acquire)) {
        cleanup_completed_tasks(std::chrono::hours(1));
      }
    }
  }

  // Task processing
  std::unique_ptr<ThreadPool> thread_pool_;
  std::atomic<bool> processing_started_{false};

  // State management
  StateStorage states_;

  // Cancellation management
  mutable std::shared_mutex cancellation_mutex_;
  std::unordered_map<TaskID, bool> cancelled_tasks_;

  // Persistent tasks storage
  mutable std::shared_mutex persistent_tasks_mutex_;
  std::unordered_map<TaskID, std::unique_ptr<AnyTask>> persistent_tasks_;

  // Result storage
  std::unique_ptr<ResultStorage> result_storage_;

  // Cleanup
  std::thread cleanup_thread_;
  std::mutex cleanup_mutex_;
  std::condition_variable cleanup_cv_;
  std::atomic<bool> stop_cleanup_{false};
};

// Execution algorithm - State machine for task lifecycle
template <typename Task>
typename std::enable_if<is_task_v<Task>, void>::type execute_task(Task& task, TaskRuntimeCtx& rctx, ResultStorage* result_storage) {
  TaskCtx ctx{rctx.id, rctx.states, result_storage, rctx.cancellation_checker};

  try {
    // Begin execution
    ctx.begin();

    // Execute the task
    task(ctx);

    // If not cancelled and not failed, mark as success
    if (!ctx.is_cancelled() && !ctx.is_completed()) {
      ctx.success();
    }
  } catch (const std::exception& e) {
    // Handle exceptions
    ctx.failure(e.what());
  } catch (...) {
    // Handle unknown exceptions
    ctx.failure("unknown_exception");
  }
}

// Execute task with cancellation support
template <typename Task>
typename std::enable_if<is_cancellable_task_v<Task>, void>::type execute_cancellable_task(Task& task,
                                                                                          TaskRuntimeCtx& rctx, ResultStorage* result_storage) {
  TaskCtx ctx{rctx.id, rctx.states, result_storage, rctx.cancellation_checker};

  try {
    ctx.begin();

    // For cancellable tasks, task itself handles cancellation
    task(ctx);

    // Task completed normally or handled cancellation internally
    if (!ctx.is_completed()) {
      ctx.success();
    }
  } catch (const std::exception& e) {
    ctx.failure(e.what());
  } catch (...) {
    ctx.failure("unknown_exception");
  }
}

// Execute task with observation
template <typename Task>
typename std::enable_if<is_observable_task_v<Task>, void>::type execute_observable_task(Task& task,
                                                                                        TaskRuntimeCtx& rctx, ResultStorage* result_storage) {
  TaskCtx ctx{rctx.id, rctx.states, result_storage, rctx.cancellation_checker};

  try {
    ctx.begin();

    task(ctx);

    if (!ctx.is_cancelled() && !ctx.is_completed()) {
      ctx.success();
    }
  } catch (const std::exception& e) {
    ctx.failure(e.what());
  } catch (...) {
    ctx.failure("unknown_exception");
  }
}

// Generic task execution dispatcher
template <typename Task>
typename std::enable_if<is_task_v<Task>, void>::type execute(Task& task, TaskRuntimeCtx& rctx, ResultStorage* result_storage) {
  if constexpr (is_cancellable_task_v<Task> && is_progress_observable_task_v<Task>) {
    execute_observable_task(task, rctx, result_storage);
  } else if constexpr (is_cancellable_task_v<Task> && is_basic_observable_task_v<Task>) {
    execute_cancellable_task(task, rctx, result_storage);
  } else if constexpr (is_progress_observable_task_v<Task>) {
    execute_observable_task(task, rctx, result_storage);
  } else if constexpr (is_basic_observable_task_v<Task>) {
    execute_task(task, rctx, result_storage);
  } else if constexpr (is_cancellable_task_v<Task>) {
    execute_cancellable_task(task, rctx, result_storage);
  } else {
    execute_task(task, rctx, result_storage);
  }
}

// Execute task by value (for rvalue tasks)
template <typename Task>
typename std::enable_if<is_task_v<Task>, void>::type execute(Task&& task, TaskRuntimeCtx& rctx, ResultStorage* result_storage) {
  Task task_copy = std::forward<Task>(task);
  execute(task_copy, rctx, result_storage);
}

}  // namespace taskflow
