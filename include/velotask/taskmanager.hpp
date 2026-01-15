#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

#include "taskbase.hpp"
#include "taskpool.hpp"

namespace velo {

struct TaskInfo {
  std::string taskId;
  std::string name;
  std::string description;
  TaskState state = TaskState::pending;
  TaskLifecycle lifecycle{TaskLifecycle::disposable};

  json input;
  json resultData;
  json currentProgressInfo;
  std::string errorMessage;

  std::chrono::system_clock::time_point createdTime;
  std::chrono::system_clock::time_point updatedTime;
  std::chrono::system_clock::time_point startTime;
  std::chrono::system_clock::time_point endTime;
};

class TaskManager : public TaskObserver {
 public:
  static TaskManager& getInstance() {
    static TaskManager instance;
    return instance;
  }

  ~TaskManager() override {
    stopCleanup_ = true;
    cleanupCv_.notify_all();
    if (infoCleanupThread_.joinable()) {
      infoCleanupThread_.join();
    }
  }

  TaskManager(const TaskManager&) = delete;
  TaskManager& operator=(const TaskManager&) = delete;

  template <typename T>
  void registerTaskType(const std::string& taskTypeName) {
    std::lock_guard<std::mutex> lock(taskFactoriesMutex_);
    taskFactories_[taskTypeName] = createAdapterFactory<T>();
  }

  std::string submitTask(const std::string& taskTypeName, const json& input = {},
                         TaskLifecycle lifecycle = TaskLifecycle::disposable,
                         TaskPriority priority = TaskPriority::normal) {
    std::shared_ptr<TaskBase> task;
    {
      std::lock_guard<std::mutex> lock(taskFactoriesMutex_);
      auto it = taskFactories_.find(taskTypeName);
      if (it == taskFactories_.end()) {
        throw std::runtime_error("Unknown task type: " + taskTypeName);
      }
      task = it->second(generateTaskId(), lifecycle, priority);
    }

    task->setObserver(this);

    std::string taskId = taskPool_.submitTask(task, input);
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex_);
      TaskInfo info;
      info.taskId = taskId;
      info.lifecycle = lifecycle;
      info.name = task->getName();
      info.description = task->getDescription();
      info.input = input;
      info.createdTime = std::chrono::system_clock::now();
      info.updatedTime = info.createdTime;

      taskInfos_[taskId] = info;
    }
    return taskId;
  }

  std::string submitTask(FunctionalTask::TaskFn func, const json& input = {},
                         TaskLifecycle lifecycle = TaskLifecycle::disposable,
                         TaskPriority priority = TaskPriority::normal) {
    std::string taskId = generateTaskId();
    auto task = std::make_shared<FunctionalTask>(taskId, std::move(func));
    task->setObserver(this);
    (void)taskPool_.submitTask(task, input);
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex_);
      TaskInfo info;
      info.taskId = taskId;
      info.lifecycle = lifecycle;
      info.name = task->getName();
      info.description = task->getDescription();
      info.input = input;
      info.createdTime = std::chrono::system_clock::now();
      info.updatedTime = info.createdTime;

      taskInfos_[taskId] = info;
    }
    return taskId;
  }

  bool restartTask(const std::string& taskId, const json& newInput = {}) {
    if (!taskPool_.restartTask(taskId, newInput)) {
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(taskInfosMutex_);
      auto it = taskInfos_.find(taskId);
      if (it != taskInfos_.end()) {
        it->second.state = TaskState::pending;
        it->second.resultData.clear();
        it->second.errorMessage.clear();
        if (!newInput.empty()) {
          it->second.input = newInput;
        }
        it->second.updatedTime = std::chrono::system_clock::now();
      }
    }

    return true;
  }

  bool hasTask(const std::string& taskId) const {
    std::lock_guard<std::mutex> lock(taskInfosMutex_);
    return taskInfos_.find(taskId) != taskInfos_.end();
  }

  bool cancelTask(const std::string& taskId) { return taskPool_.cancelTask(taskId); }

  void cleanup() {
    taskPool_.cleanup();
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex_);
      taskInfos_.clear();
    }
  }

  std::optional<TaskInfo> getTaskInfo(const std::string& taskId) const {
    std::lock_guard<std::mutex> lock(taskInfosMutex_);
    auto it = taskInfos_.find(taskId);
    if (it != taskInfos_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  TaskState getTaskState(const std::string& taskId) const {
    auto info = getTaskInfo(taskId);
    return info ? info->state : TaskState::pending;
  }

  json getTaskResult(const std::string& taskId) const {
    auto info = getTaskInfo(taskId);
    if (info && !info->resultData.empty()) {
      return info->resultData;
    }
    return json{};
  }

  std::string getTaskErrorMessage(const std::string& taskId) const {
    auto info = getTaskInfo(taskId);
    if (info && !info->errorMessage.empty()) {
      return info->errorMessage;
    }
    return {};
  }

  json getTaskProgress(const std::string& taskId) const {
    auto info = getTaskInfo(taskId);
    if (info && !info->currentProgressInfo.empty()) {
      return info->currentProgressInfo;
    }
    return json{};
  }

  void onTaskStarted(const std::string& taskId, const json& input) override {
    std::lock_guard<std::mutex> lock(taskInfosMutex_);
    auto it = taskInfos_.find(taskId);
    if (it != taskInfos_.end()) {
      it->second.state = TaskState::running;
      it->second.startTime = std::chrono::system_clock::now();
      it->second.updatedTime = it->second.startTime;
    }
  }

  void onTaskCompleted(const std::string& taskId, const TaskResult& result) override {
    std::lock_guard<std::mutex> lock(taskInfosMutex_);
    auto it = taskInfos_.find(taskId);
    if (it != taskInfos_.end()) {
      it->second.state = result.state;
      it->second.resultData = result.data;
      it->second.endTime = std::chrono::system_clock::now();
      it->second.updatedTime = it->second.endTime;
    }
  }

  void onTaskProgress(const std::string& taskId, const json& progress) override {
    std::lock_guard<std::mutex> lock(taskInfosMutex_);
    auto it = taskInfos_.find(taskId);
    if (it != taskInfos_.end()) {
      it->second.updatedTime = std::chrono::system_clock::now();
      it->second.currentProgressInfo = progress;
    }
  }

  void onTaskError(const std::string& taskId, const std::string& errorMsg) override {
    std::lock_guard<std::mutex> lock(taskInfosMutex_);
    auto it = taskInfos_.find(taskId);
    if (it != taskInfos_.end()) {
      it->second.errorMessage = errorMsg;
    }
  }

 private:
  explicit TaskManager(size_t threadCount = std::thread::hardware_concurrency()) : taskPool_(threadCount) {
    infoCleanupThread_ = std::thread([this] { cleanupInfoLoop(); });
  }

  TaskPool taskPool_;

  std::unordered_map<std::string, TaskInfo> taskInfos_;
  mutable std::mutex taskInfosMutex_;

  using TaskFactory = std::function<std::shared_ptr<TaskBase>(const std::string&, TaskLifecycle, TaskPriority)>;

  std::unordered_map<std::string, TaskFactory> taskFactories_;
  mutable std::mutex taskFactoriesMutex_;

  std::thread infoCleanupThread_;
  std::atomic<bool> stopCleanup_{false};
  const std::chrono::hours maxInfoAge{24};
  const size_t maxInfoCount{1000};
  std::condition_variable cleanupCv_;

  template <typename T>
  TaskFactory createAdapterFactory() {
    if constexpr (std::is_constructible_v<T, const std::string&, TaskLifecycle, TaskPriority>) {
      return [](const std::string& taskId, TaskLifecycle lifecycle, TaskPriority priority) {
        return std::make_shared<T>(taskId, lifecycle, priority);
      };
    } else if constexpr (std::is_constructible_v<T, const std::string&, TaskLifecycle>) {
      return [](const std::string& taskId, TaskLifecycle lifecycle, TaskPriority priority) {
        auto task = std::make_shared<T>(taskId, lifecycle);
        task->setPriority(priority);
        return task;
      };
    } else if constexpr (std::is_constructible_v<T, const std::string&>) {
      return [](const std::string& taskId, TaskLifecycle lifecycle, TaskPriority priority) {
        auto task = std::make_shared<T>(taskId);
        task->setPriority(priority);
        return task;
      };
    }
    return {};
  }

  void cleanupInfoLoop() {
    while (!stopCleanup_) {
      auto now = std::chrono::system_clock::now();

      std::unique_lock<std::mutex> lock(taskInfosMutex_);
      cleanupCv_.wait_for(lock, std::chrono::minutes(5), [this] { return stopCleanup_.load(); });

      if (stopCleanup_) break;

      auto it = taskInfos_.begin();
      while (it != taskInfos_.end()) {
        const auto& info = it->second;
        bool isTerminated = (info.state == TaskState::success || info.state == TaskState::failure);
        bool isExpired = (now - info.updatedTime) > maxInfoAge;

        if (isTerminated && isExpired) {
          it = taskInfos_.erase(it);
        } else {
          ++it;
        }
      }

      if (taskInfos_.size() > maxInfoCount) {
        forceTrimInfos();
      }
    }
  }

  void forceTrimInfos() {
    struct TrimCandidate {
      std::string id;
      std::chrono::system_clock::time_point updatedTime;
    };
    std::vector<TrimCandidate> candidates;

    for (const auto& [id, info] : taskInfos_) {
      if ((info.state == TaskState::success || info.state == TaskState::failure) &&
          info.lifecycle == TaskLifecycle::disposable) {
        candidates.push_back({id, info.updatedTime});
      }
    }

    if (candidates.empty()) return;

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.updatedTime < b.updatedTime; });

    size_t excess = taskInfos_.size() - maxInfoCount;
    size_t toRemove = std::min(excess, candidates.size());

    for (size_t i = 0; i < toRemove; ++i) {
      taskInfos_.erase(candidates[i].id);
    }
  }
};

}  // namespace velo
