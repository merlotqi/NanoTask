#pragma once

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "taskbase.hpp"
#include "threadpool.hpp"

namespace velo {

class TaskPool {
  ThreadPool threadPool_;
  std::multimap<TaskPriority, std::shared_ptr<TaskBase>, std::greater<>> pendingQueue_;
  std::unordered_map<std::string, std::shared_ptr<TaskBase>> runningTasks_;
  std::unordered_map<std::string, std::shared_ptr<TaskBase>> persistentTasks_;
  std::list<std::shared_ptr<TaskBase>> completedTasks_;

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
  }

 private:
  void schedulerLoop() {
    while (!stopScheduler_) {
      std::unique_lock<std::mutex> lock(tasksMutex_);
      schedulerCv_.wait_for(lock, std::chrono::milliseconds(100),
                            [this] { return stopScheduler_ || !pendingQueue_.empty(); });
      if (stopScheduler_) return;

      auto it = pendingQueue_.begin();
      while (it != pendingQueue_.end()) {
        auto task = it->second;
        if (task->getState() == TaskState::pending) {
          runningTasks_[task->getTaskId()] = task;

          threadPool_.execute([this, task]() { executeTask(task); });
        }
        it = pendingQueue_.erase(it);
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

    taskCompleted(task);
  }

  void taskCompleted(const std::shared_ptr<TaskBase>& task) {
    std::lock_guard<std::mutex> lock(tasksMutex_);
    runningTasks_.erase(task->getTaskId());
    completedTasks_.push_back(task);
  }

  void cleanupLoop() {
    while (!stopScheduler_) {
      std::unique_lock<std::mutex> lock(tasksMutex_);
      schedulerCv_.wait_for(lock, std::chrono::seconds(60), [this] { return stopScheduler_.load(); });

      if (stopScheduler_) return;

      auto it = completedTasks_.begin();
      while (it != completedTasks_.end()) {
        if ((*it)->getLifecycle() == TaskLifecycle::disposable) {
          it = completedTasks_.erase(it);
        } else {
          ++it;
        }
      }
    }
  }
};

}  // namespace velo
