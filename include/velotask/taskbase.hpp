#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iomanip>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace velo {

using json = nlohmann::json;

enum class TaskLifecycle : std::uint8_t {
  disposable,
  persistent,
};

enum class TaskState : std::uint8_t {
  pending,
  running,
  success,
  failure,
};

enum class TaskPriority : std::uint8_t {
  lowest = 0,
  low = 25,
  normal = 50,
  high = 75,
  critical = 100,
  // Allow custom values up to 255
};

static inline std::string generateTaskId() {
  // XXXXXXXX-XXXX-4XXX-YXXX-XXXXXXXXXXXX
  static thread_local std::random_device rnd;
  static thread_local std::mt19937_64 gen(rnd());
  std::uniform_int_distribution<uint64_t> dis;

  constexpr uint64_t uuidV4VersionMask = 0xffffffffffff0fffULL;
  constexpr uint64_t uuidV4VersionBits = 0x0000000000004000ULL;
  constexpr uint64_t uuidV4VariantMask = 0x3fffffffffffffffULL;
  constexpr uint64_t uuidV4VariantBits = 0x8000000000000000ULL;

  constexpr uint64_t mask16Bit = 0xffffU;
  constexpr uint64_t mask48Bit = 0xffffffffffffULL;

  uint64_t part1 = dis(gen);
  uint64_t part2 = dis(gen);

  part1 = (part1 & uuidV4VersionMask) | uuidV4VersionBits;
  part2 = (part2 & uuidV4VariantMask) | uuidV4VariantBits;

  std::stringstream oss;
  oss << std::hex << std::setfill('0') << std::uppercase;

  oss << std::setw(8) << (part1 >> 32);
  oss << "-";
  oss << std::setw(4) << ((part1 >> 16) & mask16Bit);
  oss << "-";
  oss << std::setw(4) << (part1 & 0xffff);
  oss << "-";
  oss << std::setw(4) << (part2 >> 48);
  oss << "-";
  oss << std::setw(12) << (part2 & mask48Bit);

  return oss.str();
}

struct TaskResult {
  TaskState state{TaskState::pending};
  json data;
  std::string errorMessage;

  [[nodiscard]] bool isSuccess() const { return state == TaskState::success; }
  [[nodiscard]] bool hasError() const { return state == TaskState::failure; }
};

class TaskObserver {
 public:
  virtual ~TaskObserver() = default;
  virtual void onTaskStarted(const std::string& taskId, const json& input) = 0;
  virtual void onTaskCompleted(const std::string& taskId, const TaskResult& result) = 0;
  virtual void onTaskProgress(const std::string& taskId, const json& progressInfo) = 0;
  virtual void onTaskError(const std::string& taskId, const std::string& errorMessage) = 0;
};

class TaskBase {
 public:
  explicit TaskBase(std::string_view taskId, TaskLifecycle lifecycle = TaskLifecycle::disposable,
                    TaskPriority priority = TaskPriority::normal, int maxRetries = 0)
      : taskId_(taskId), lifecycle_(lifecycle), priority_(priority), maxRetries_(maxRetries) {}

  virtual ~TaskBase() = default;

  virtual std::string getName() const = 0;
  virtual std::string getDescription() const = 0;
  virtual void execute() = 0;
  virtual void cleanup() = 0;

  std::string getTaskId() const { return taskId_; }
  TaskState getState() const { return state_.load(std::memory_order_acquire); }
  TaskLifecycle getLifecycle() const { return lifecycle_; }
  json getInput() const { return input_; }
  std::chrono::system_clock::time_point getStartTime() const { return startTime_; }
  std::chrono::system_clock::time_point getEndTime() const { return endTime_; }
  TaskPriority getPriority() const { return priority_; }
  const TaskResult& getResult() const { return result_; }

  void setObserver(TaskObserver* observer) { observer_ = observer; }
  void setInput(const json& input) { input_ = input; }
  void setTaskId(std::string_view tid) { taskId_ = tid; }
  void setPriority(TaskPriority priority) { priority_ = priority; }
  void setDependencies(const std::vector<std::string>& deps) { dependencies_ = deps; }
  const std::vector<std::string>& getDependencies() const { return dependencies_; }

  void cancel() {
    cancelRequested_.store(true, std::memory_order_release);
    cv_.notify_all();
  }

  bool restart(const json& newInput = {}) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (state_ == TaskState::success || state_ == TaskState::failure) {
      resetState();
      if (!newInput.empty()) input_ = newInput;
      return true;
    }
    return false;
  }

  bool waitForCompletion(int timeoutMs = -1) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto isTerminated = [this] {
      auto ste = state_.load(std::memory_order_relaxed);
      return ste == TaskState::success || ste == TaskState::failure;
    };

    if (isTerminated()) return true;

    if (timeoutMs < 0) {
      cv_.wait(lock, isTerminated);
      return true;
    }
    return cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), isTerminated);
  }

  bool isRunning() const { return state_.load(std::memory_order_acquire) == TaskState::running; }
  bool isCancelRequested() const { return cancelRequested_.load(std::memory_order_acquire); }

  void beginExecute() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (state_.load(std::memory_order_relaxed) != TaskState::pending) {
        throw std::runtime_error("nanotask: task not in pending state");
      }
      state_.store(TaskState::running, std::memory_order_release);
      startTime_ = std::chrono::system_clock::now();
    }
    if (observer_ != nullptr) {
      observer_->onTaskStarted(taskId_, input_);
    }
  }

  void updateProgress(const json& progressInfo) {
    if (observer_ != nullptr) {
      observer_->onTaskProgress(taskId_, progressInfo);
    }
  }

  void finishExecute(const json& resultData) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (state_.load(std::memory_order_relaxed) != TaskState::running) return;
      state_.store(TaskState::success, std::memory_order_release);
      endTime_ = std::chrono::system_clock::now();
      result_.state = TaskState::success;
      result_.data = resultData;
      cv_.notify_all();
    }
    if (observer_ != nullptr) {
      observer_->onTaskCompleted(taskId_, result_);
    }
  }

  void failExecute(const std::string& message) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto ste = state_.load(std::memory_order_relaxed);
      if (ste != TaskState::running && ste != TaskState::pending) return;
      state_.store(TaskState::failure, std::memory_order_release);
      endTime_ = std::chrono::system_clock::now();
      result_.state = TaskState::failure;
      result_.errorMessage = message;
      cv_.notify_all();
    }
    if (observer_ != nullptr) {
      observer_->onTaskCompleted(taskId_, result_);
      observer_->onTaskError(taskId_, message);
    }
  }

 private:
  friend class TaskPool;

  void resetState() {
    state_.store(TaskState::pending, std::memory_order_release);
    result_ = TaskResult{};
    cancelRequested_.store(false, std::memory_order_release);
  }

  std::string taskId_;
  std::atomic<TaskState> state_{TaskState::pending};
  TaskLifecycle lifecycle_{TaskLifecycle::disposable};
  TaskPriority priority_{TaskPriority::normal};

  TaskResult result_;
  json input_;
  TaskObserver* observer_ = nullptr;

  std::chrono::system_clock::time_point startTime_;
  std::chrono::system_clock::time_point endTime_;

  int maxRetries_{0};
  int currentRetries_{0};
  std::vector<std::string> dependencies_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> cancelRequested_{false};
};

class FunctionalTask : public TaskBase {
 public:
  using ProgressFn = std::function<void(const json&)>;
  using CancelFn = std::function<bool()>;
  using TaskFn = std::function<json(const json&, ProgressFn, CancelFn)>;

  FunctionalTask(std::string_view taskId, TaskFn func) : TaskBase(taskId), func_(std::move(func)) {}

  std::string getName() const override { return "FunctionalTask"; }
  std::string getDescription() const override { return "Task defined by a lambda"; }

  void execute() override {
    try {
      json result = func_(
          getInput(), [this](const json& info) { this->updateProgress(info); },
          [this] { return this->isCancelRequested(); });

      if (!isCancelRequested()) {
        finishExecute(result);
      } else {
        failExecute("task_cancelled");
      }
    } catch (const std::exception& e) {
      failExecute(e.what());
    } catch (...) {
      failExecute("unknown_exception");
    }
  }

  void cleanup() override {}

 private:
  TaskFn func_;
};

}  // namespace velo
