#pragma once

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "task_base.hpp"
#include "threadpool.hpp"

namespace nanotask {

class task_pool {
  thread_pool thread_pool_;

  std::multimap<task_priority, std::shared_ptr<task_base>, std::greater<task_priority>> pending_queue_;
  std::unordered_map<std::string, std::shared_ptr<task_base>> running_tasks_;
  std::unordered_map<std::string, std::shared_ptr<task_base>> persistent_tasks_;
  std::list<std::shared_ptr<task_base>> completed_tasks_;

  mutable std::mutex tasks_mutex_;
  std::condition_variable scheduler_cv_;

  std::thread scheduler_thread_;
  std::thread clean_disposed_thread_;

  std::atomic<bool> stop_scheduler_{false};

 public:
  explicit task_pool(size_t thread_count = std::thread::hardware_concurrency()) : thread_pool_(thread_count) {
    scheduler_thread_ = std::thread([this] { scheduler_loop(); });
    clean_disposed_thread_ = std::thread([this] { cleanup_loop(); });
  }

  ~task_pool() {
    stop_scheduler_ = true;
    scheduler_cv_.notify_all();

    if (scheduler_thread_.joinable()) {
      scheduler_thread_.join();
    }
    if (clean_disposed_thread_.joinable()) {
      clean_disposed_thread_.join();
    }
  }

  std::string submit_task(const std::shared_ptr<task_base>& task, const json& input = {}) {
    std::string task_id = task->get_task_id();
    if (task_id.empty()) {
      task_id = generate_task_id();
      task->set_task_id(task_id);
    }
    task->set_input(input);
    {
      std::lock_guard<std::mutex> lock(tasks_mutex_);
      if (task->get_lifecycle() == task_lifecycle::persistent) {
        persistent_tasks_[task_id] = task;
      }
      pending_queue_.insert({task->get_priority(), task});
    }

    scheduler_cv_.notify_one();

    return task_id;
  }

  bool restart_task(const std::string& task_id, const json& new_input = {}) {
    std::shared_ptr<task_base> task;
    {
      std::lock_guard<std::mutex> lock(tasks_mutex_);
      auto it = persistent_tasks_.find(task_id);
      if (it != persistent_tasks_.end()) {
        task = it->second;
      }
    }

    if (task && task->restart(new_input)) {
      {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        pending_queue_.insert({task->get_priority(), task});
      }
      scheduler_cv_.notify_one();
      return true;
    }

    return false;
  }

  std::shared_ptr<task_base> get_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);

    if (auto it = running_tasks_.find(task_id); it != running_tasks_.end()) return it->second;
    if (auto it = persistent_tasks_.find(task_id); it != persistent_tasks_.end()) return it->second;

    for (const auto& [priority, task] : pending_queue_) {
      if (task->get_task_id() == task_id) return task;
    }

    for (const auto& completed_task : completed_tasks_) {
      if (completed_task->get_task_id() == task_id) return completed_task;
    }
    return nullptr;
  }

  bool cancel_task(const std::string& task_id) const {
    auto task = get_task(task_id);
    if (task && (task->get_state() == task_state::running || task->get_state() == task_state::pending)) {
      task->cancel();
      task->wait_for_completion();
      task->cleanup();
      return true;
    }
    return false;
  }

  void cleanup() {
    thread_pool_.cleanup_queue();

    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (auto& [id, task] : running_tasks_) {
      task->cancel();
    }

    pending_queue_.clear();
    running_tasks_.clear();
    persistent_tasks_.clear();
    completed_tasks_.clear();
  }

 private:
  void scheduler_loop() {
    while (!stop_scheduler_) {
      std::unique_lock<std::mutex> lock(tasks_mutex_);
      scheduler_cv_.wait_for(lock, std::chrono::milliseconds(100),
                             [this] { return stop_scheduler_ || !pending_queue_.empty(); });
      if (stop_scheduler_) return;

      auto it = pending_queue_.begin();
      while (it != pending_queue_.end()) {
        auto task = it->second;
        if (task->get_state() == task_state::pending) {
          running_tasks_[task->get_task_id()] = task;

          thread_pool_.execute([this, task]() { execute_task(task); });
        }
        it = pending_queue_.erase(it);
      }
    }
  }

  void execute_task(const std::shared_ptr<task_base> &task) {
    task->begin_execute();
    try {
      task->execute();
    } catch (const std::exception& e) {
      task->fail_execute(e.what());
    } catch (...) {
      task->fail_execute("Unknown error occurred during execution.");
    }

    task_completed(task);
  }

  void task_completed(const std::shared_ptr<task_base> &task) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    running_tasks_.erase(task->get_task_id());
    completed_tasks_.push_back(task);
  }

  void cleanup_loop() {
    while (!stop_scheduler_) {
      std::unique_lock<std::mutex> lock(tasks_mutex_);
      scheduler_cv_.wait_for(lock, std::chrono::seconds(60), [this] { return stop_scheduler_.load(); });

      if (stop_scheduler_) return;

      auto it = completed_tasks_.begin();
      while (it != completed_tasks_.end()) {
        if ((*it)->get_lifecycle() == task_lifecycle::disposable) {
          it = completed_tasks_.erase(it);
        } else {
          ++it;
        }
      }
    }
  }
};

}  // namespace nanotask
