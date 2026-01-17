#pragma once

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "taskbase.hpp"
#include "threadpool.hpp"

namespace velo {

class TaskPool {
  ThreadPool threadPool_;
  std::multimap<TaskPriority, std::shared_ptr<TaskBase>, std::greater<>> pendingQueue_;
  std::unordered_map<std::string, std::shared_ptr<TaskBase>> runningTasks_;
  std::unordered_map<std::string, std::shared_ptr<TaskBase>> persistentTasks_;
  std::list<std::shared_ptr<TaskBase>> completedTasks_;
  std::unordered_set<std::string> completedTaskIds_;

  mutable std::mutex tasksMutex_;
  std::condition_variable schedulerCv_;

  std::thread schedulerThread_;
  std::thread cleanDisposedThread_;

  std::atomic<bool> stopScheduler_{false};

 public:
  explicit TaskPool(size_t threadCount = std::thread::hardware_concurrency()) : threadPool_(threadCount) {
    schedulerThread_ = std::thread([this] { schedulerLoop(); });
    cleanDisposedThread_ = std::thread([this] { cleanupLoop(); });
  }

  ~TaskPool() {
    stopScheduler_ = true;
    schedulerCv_.notify_all();

    if (schedulerThread_.joinable()) {
      schedulerThread_.join();
    }
    if (cleanDisposedThread_.joinable()) {
      cleanDisposedThread_.join();
    }
  }

  std::string submitTask(const std::shared_ptr<TaskBase>& task, const json& input = {}) {
    std::string taskId = task->getTaskId();
    if (taskId.empty()) {
      taskId = generateTaskId();
      task->setTaskId(taskId);
    }
    task->setInput(input);
    {
      std::lock_guard<std::mutex> lock(tasksMutex_);
      if (task->getLifecycle() == TaskLifecycle::persistent) {
        persistentTasks_[taskId] = task;
      }
      pendingQueue_.insert({task->getPriority(), task});
    }

    schedulerCv_.notify_one();

    return taskId;
  }

  bool restartTask(const std::string& taskId, const json& newInput = {}) {
    std::shared_ptr<TaskBase> task;
    {
      std::lock_guard<std::mutex> lock(tasksMutex_);
      auto it = persistentTasks_.find(taskId);
      if (it != persistentTasks_.end()) {
        task = it->second;
      }
    }

    if (task && task->restart(newInput)) {
      {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        pendingQueue_.insert({task->getPriority(), task});
      }
      schedulerCv_.notify_one();
      return true;
    }

    return false;
  }

  std::shared_ptr<TaskBase> getTask(const std::string& taskId) const {
    std::lock_guard<std::mutex> lock(tasksMutex_);

    if (auto it = runningTasks_.find(taskId); it != runningTasks_.end()) return it->second;
    if (auto it = persistentTasks_.find(taskId); it != persistentTasks_.end()) return it->second;

    for (const auto& [priority, task] : pendingQueue_) {
      if (task->getTaskId() == taskId) return task;
    }

    for (const auto& completedTask : completedTasks_) {
      if (completedTask->getTaskId() == taskId) return completedTask;
    }
    return nullptr;
  }

  bool cancelTask(const std::string& taskId) const {
    auto task = getTask(taskId);
    if (task && (task->getState() == TaskState::running || task->getState() == TaskState::pending)) {
      task->cancel();
      task->waitForCompletion();
      task->cleanup();
      return true;
    }
    return false;
  }

  void cleanup() {
    threadPool_.cleanup_queue();

    std::lock_guard<std::mutex> lock(tasksMutex_);
    for (auto& [id, task] : runningTasks_) {
      task->cancel();
    }

    pendingQueue_.clear();
    runningTasks_.clear();
    persistentTasks_.clear();
    completedTasks_.clear();
    completedTaskIds_.clear();
  }

 private:
  void schedulerLoop() {
    while (!stopScheduler_) {
      std::vector<std::shared_ptr<TaskBase>> tasksToExecute;
      {
        std::unique_lock<std::mutex> lock(tasksMutex_);
        schedulerCv_.wait_for(lock, std::chrono::milliseconds(100),
                              [this] { return stopScheduler_ || !pendingQueue_.empty(); });
        if (stopScheduler_) return;

        auto it = pendingQueue_.begin();
        while (it != pendingQueue_.end()) {
          auto task = it->second;
          if (task->getState() == TaskState::pending) {
            // Check dependencies
            bool depsSatisfied = task->dependencies_.empty();
            if (!depsSatisfied) {
              depsSatisfied = true;
              for (const auto& dep : task->dependencies_) {
                if (completedTaskIds_.find(dep) == completedTaskIds_.end()) {
                  depsSatisfied = false;
                  break;
                }
              }
            }
            if (depsSatisfied) {
              runningTasks_[task->getTaskId()] = task;
              tasksToExecute.push_back(task);
              it = pendingQueue_.erase(it);
            } else {
              ++it;
            }
          } else {
            it = pendingQueue_.erase(it);
          }
        }
      }

      // Execute tasks outside the lock
      for (auto& task : tasksToExecute) {
        threadPool_.execute([this, task]() { executeTask(task); });
      }
    }
  }

  void executeTask(const std::shared_ptr<TaskBase>& task) {
    task->beginExecute();
    try {
      task->execute();
    } catch (const std::exception& e) {
      task->failExecute(e.what());
    } catch (...) {
      task->failExecute("Unknown error occurred during execution.");
    }

    // Handle retries
    if (task->getState() == TaskState::failure && task->currentRetries_ < task->maxRetries_) {
      task->currentRetries_++;
      // Reset state for retry
      task->state_.store(TaskState::pending, std::memory_order_release);
      task->result_ = TaskResult{};
      task->cancelRequested_.store(false, std::memory_order_release);
      // Resubmit to queue
      {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        pendingQueue_.insert({task->getPriority(), task});
      }
      schedulerCv_.notify_one();
      return;
    }

    taskCompleted(task);
  }

  void taskCompleted(const std::shared_ptr<TaskBase>& task) {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    runningTasks_.erase(task->getTaskId());
    completedTasks_.push_back(task);
    completedTaskIds_.insert(task->getTaskId());
  }

  void cleanupLoop() {
    while (!stopScheduler_) {
      std::unique_lock<std::mutex> lock(tasksMutex_);
      schedulerCv_.wait_for(lock, std::chrono::seconds(60), [this] { return stopScheduler_.load(); });

      if (stopScheduler_) return;

      auto it = completedTasks_.begin();
      while (it != completedTasks_.end()) {
        if ((*it)->getLifecycle() == TaskLifecycle::disposable) {
          completedTaskIds_.erase((*it)->getTaskId());
          it = completedTasks_.erase(it);
        } else {
          ++it;
        }
      }
    }
  }
};

}  // namespace velo
