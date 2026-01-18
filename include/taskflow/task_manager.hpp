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

// TaskManager v2 - Passive aggregation and query interface
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
  TaskID submit_task(Task task) {
    TaskID id = generate_task_id();

    // Initialize state
    states_.set_state(id, TaskState::created);

    // Submit to thread pool for execution
    if (thread_pool_) {
      thread_pool_->execute([this, id, task = std::move(task)]() mutable {
        AnyTask any_task = make_any_task(id, std::move(task));
        if (any_task.valid()) {
          TaskRuntimeCtx rctx{id, &states_, [this](TaskID tid) { return is_task_cancelled(tid); }};
          any_task.execute_task(rctx);
        }
      });
    }

    return id;
  }

  // Query task state
  [[nodiscard]] std::optional<TaskState> query_state(TaskID id) const { return states_.get_state(id); }

  // Get task progress
  [[nodiscard]] std::optional<ProgressInfo> get_progress(TaskID id) const { return states_.get_progress(id); }

  // Get task error message
  [[nodiscard]] std::optional<std::string> get_error(TaskID id) const { return states_.get_error(id); }

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
  TaskManager() : cleanup_thread_([this] { cleanup_loop(); }) {}

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

  // Cleanup
  std::thread cleanup_thread_;
  std::mutex cleanup_mutex_;
  std::condition_variable cleanup_cv_;
  std::atomic<bool> stop_cleanup_{false};
};

// Execution algorithm - State machine for task lifecycle
template <typename Task>
typename std::enable_if<is_task_v<Task>, void>::type execute_task(Task& task, TaskRuntimeCtx& rctx) {
  TaskCtx ctx{rctx.id, rctx.states, rctx.cancellation_checker};

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
                                                                                          TaskRuntimeCtx& rctx) {
  TaskCtx ctx{rctx.id, rctx.states, rctx.cancellation_checker};

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
                                                                                        TaskRuntimeCtx& rctx) {
  TaskCtx ctx{rctx.id, rctx.states, rctx.cancellation_checker};

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
typename std::enable_if<is_task_v<Task>, void>::type execute(Task& task, TaskRuntimeCtx& rctx) {
  if constexpr (is_cancellable_task_v<Task>) {
    execute_cancellable_task(task, rctx);
  } else if constexpr (is_observable_task_v<Task>) {
    execute_observable_task(task, rctx);
  } else {
    execute_task(task, rctx);
  }
}

// Execute task by value (for rvalue tasks)
template <typename Task>
typename std::enable_if<is_task_v<Task>, void>::type execute(Task&& task, TaskRuntimeCtx& rctx) {
  Task task_copy = std::forward<Task>(task);
  execute(task_copy, rctx);
}

}  // namespace taskflow
