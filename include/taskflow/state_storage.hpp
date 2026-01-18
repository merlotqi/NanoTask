#pragma once

#include <any>
#include <chrono>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "task_traits.hpp"

namespace taskflow {

// Template for progress info storage - uses task traits
template <typename TaskType>
struct TaskProgressInfo {
  using ProgressInfoType = typename task_traits<TaskType>::progress_info_type;
  ProgressInfoType data;
  std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};

  TaskProgressInfo() = default;
  TaskProgressInfo(ProgressInfoType info, std::chrono::system_clock::time_point ts = std::chrono::system_clock::now())
      : data(std::move(info)), timestamp(ts) {}
};

class StateStorage {
 public:
  void set_state(TaskID id, TaskState state) {
    std::unique_lock lock(mutex_);
    states_[id] = state;
    timestamps_[id] = std::chrono::system_clock::now();
  }

  [[nodiscard]] std::optional<TaskState> get_state(TaskID id) const {
    std::shared_lock lock(mutex_);
    auto it = states_.find(id);
    return it != states_.end() ? std::optional<TaskState>(it->second) : std::nullopt;
  }

  [[nodiscard]] bool has_task(TaskID id) const {
    std::shared_lock lock(mutex_);
    return states_.find(id) != states_.end();
  }

  void remove_task(TaskID id) {
    std::unique_lock lock(mutex_);
    states_.erase(id);
    timestamps_.erase(id);
    progress_info_.erase(id);
    error_messages_.erase(id);
    result_locators_.erase(id);
  }

  // Template method to set progress with custom type
  template <typename ProgressType>
  void set_progress(TaskID id, ProgressType progress_info) {
    std::unique_lock lock(mutex_);
    progress_info_[id] = std::make_any<ProgressType>(std::move(progress_info));
    timestamps_[id] = std::chrono::system_clock::now();
  }

  // Template method to get progress with custom type
  template <typename ProgressType>
  [[nodiscard]] std::optional<ProgressType> get_progress(TaskID id) const {
    std::shared_lock lock(mutex_);
    auto it = progress_info_.find(id);
    if (it != progress_info_.end()) {
      try {
        return std::any_cast<ProgressType>(it->second);
      } catch (const std::bad_any_cast&) {
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  // Backward compatibility method
  void set_progress(TaskID id, float progress, const std::string& message = "") {
    std::unique_lock lock(mutex_);
    progress_info_[id] = std::make_any<std::pair<float, std::string>>(progress, message);
    timestamps_[id] = std::chrono::system_clock::now();
  }

  void set_error(TaskID id, const std::string& message) {
    std::unique_lock lock(mutex_);
    error_messages_[id] = message;
  }

  [[nodiscard]] std::optional<std::string> get_error(TaskID id) const {
    std::shared_lock lock(mutex_);
    auto it = error_messages_.find(id);
    return it != error_messages_.end() ? std::optional<std::string>(it->second) : std::nullopt;
  }

  void set_result_locator(TaskID id, ResultLocator locator) {
    std::unique_lock lock(mutex_);
    result_locators_[id] = locator;
  }

  [[nodiscard]] std::optional<ResultLocator> get_result_locator(TaskID id) const {
    std::shared_lock lock(mutex_);
    auto it = result_locators_.find(id);
    return it != result_locators_.end() ? std::optional<ResultLocator>(it->second) : std::nullopt;
  }

  [[nodiscard]] std::optional<std::chrono::system_clock::time_point> get_timestamp(TaskID id) const {
    std::shared_lock lock(mutex_);
    auto it = timestamps_.find(id);
    return it != timestamps_.end() ? std::optional<std::chrono::system_clock::time_point>(it->second) : std::nullopt;
  }

  [[nodiscard]] std::vector<TaskID> get_all_task_ids() const {
    std::shared_lock lock(mutex_);
    std::vector<TaskID> ids;
    ids.reserve(states_.size());
    for (const auto& [id, _] : states_) {
      ids.push_back(id);
    }
    return ids;
  }

  struct Statistics {
    size_t total_tasks{0};
    size_t running_tasks{0};
    size_t completed_tasks{0};
    size_t failed_tasks{0};
  };

  [[nodiscard]] Statistics get_statistics() const {
    std::shared_lock lock(mutex_);
    Statistics stats;
    stats.total_tasks = states_.size();

    for (const auto& [_, state] : states_) {
      switch (state) {
        case TaskState::running:
          stats.running_tasks++;
          break;
        case TaskState::success:
          stats.completed_tasks++;
          break;
        case TaskState::failure:
          stats.failed_tasks++;
          break;
        default:
          break;
      }
    }

    return stats;
  }

  void cleanup_old_tasks(std::chrono::hours max_age) {
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - max_age;

    std::unique_lock lock(mutex_);
    for (auto it = timestamps_.begin(); it != timestamps_.end();) {
      if (it->second < cutoff) {
        auto id = it->first;
        states_.erase(id);
        progress_info_.erase(id);
        error_messages_.erase(id);
        it = timestamps_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  mutable std::shared_mutex mutex_;

  std::unordered_map<TaskID, TaskState> states_;
  std::unordered_map<TaskID, std::chrono::system_clock::time_point> timestamps_;
  std::unordered_map<TaskID, std::any> progress_info_;
  std::unordered_map<TaskID, std::string> error_messages_;
  std::unordered_map<TaskID, ResultLocator> result_locators_;
};

}  // namespace taskflow
