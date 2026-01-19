#pragma once

#include <functional>
#include <string>

#include "state_storage.hpp"
#include "task_traits.hpp"

namespace taskflow {

// Forward declarations
class StateStorage;

// TaskCtx - Friendly API for task lifecycle management
struct TaskCtx {
  TaskID id;
  StateStorage* states;
  ResultStorage* result_storage{nullptr};  // Optional result storage
  std::function<bool(TaskID)> cancellation_checker;  // For cancellation checking

  // Lifecycle management
  void begin();
  void update_progress(float progress, const std::string& message = "");
  void success();
  void success_with_result(ResultPayload result);
  void failure(const std::string& error_message = "");
  void failure_with_result(const std::string& error_message, ResultPayload result);

  // Cancellation support - check with callback if available
  [[nodiscard]] bool is_cancelled() const { return cancellation_checker ? cancellation_checker(id) : false; }

  // Progress reporting - accepts any type defined by traits
  template <typename ProgressType>
  void report_progress(const ProgressType& progress_info) {
    if (states) {
      states->set_progress(id, progress_info);
    }
  }

  // Backward compatibility
  void report_progress(float progress, const std::string& message = "") {
    if (states) {
      states->set_progress(id, progress, message);
    }
  }

  // State queries
  [[nodiscard]] TaskState current_state() const;
  [[nodiscard]] bool is_running() const { return current_state() == TaskState::running; }
  [[nodiscard]] bool is_completed() const {
    auto state = current_state();
    return state == TaskState::success || state == TaskState::failure;
  }
};

// Task execution context
struct TaskRuntimeCtx {
  TaskID id;
  StateStorage* states;
  std::function<bool(TaskID)> cancellation_checker;

  explicit TaskRuntimeCtx(TaskID task_id, StateStorage* storage, std::function<bool(TaskID)> checker = nullptr)
      : id(task_id), states(storage), cancellation_checker(checker) {}
};

}  // namespace taskflow

// TaskCtx implementation
inline void taskflow::TaskCtx::begin() {
  if (states) {
    states->set_state(id, taskflow::TaskState::running);
  }
}

inline void taskflow::TaskCtx::update_progress(float progress, const std::string& message) {
  if (states) {
    states->set_progress(id, progress, message);
  }
}

inline void taskflow::TaskCtx::success() {
  if (states) {
    states->set_state(id, taskflow::TaskState::success);
  }
}

inline void taskflow::TaskCtx::failure(const std::string& error_message) {
  if (states) {
    states->set_state(id, taskflow::TaskState::failure);
    states->set_error(id, error_message);
  }
}



inline taskflow::TaskState taskflow::TaskCtx::current_state() const {
  if (states) {
    auto state = states->get_state(id);
    return state.value_or(taskflow::TaskState::created);
  }
  return taskflow::TaskState::created;
}

inline void taskflow::TaskCtx::success_with_result(taskflow::ResultPayload result) {
  if (result_storage && states) {
    auto locator = result_storage->store_result(std::move(result));
    states->set_result_locator(id, locator);
    states->set_state(id, taskflow::TaskState::success);
  } else {
    success();
  }
}

inline void taskflow::TaskCtx::failure_with_result(const std::string& error_message, taskflow::ResultPayload result) {
  if (result_storage && states) {
    auto locator = result_storage->store_result(std::move(result));
    states->set_result_locator(id, locator);
    states->set_state(id, taskflow::TaskState::failure);
    states->set_error(id, error_message);
  } else {
    failure(error_message);
  }
}
