#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#if __cplusplus >= 202002L
#include <concepts>
#define TASKFLOW_V2_HAS_CXX20 1
#endif

namespace taskflow {

// Type name utilities - compatible with C++17
namespace detail {

// Extract type name from function signature
template <typename T>
constexpr std::string_view type_name_impl() {
#if defined(__clang__)
  constexpr std::string_view prefix = "std::string_view taskflow::detail::type_name_impl() [T = ";
  constexpr std::string_view suffix = "]";
#elif defined(__GNUC__)
  constexpr std::string_view prefix = "constexpr std::string_view taskflow::detail::type_name_impl() [with T = ";
  constexpr std::string_view suffix = "; std::string_view = std::basic_string_view<char>]";
#elif defined(_MSC_VER)
  constexpr std::string_view prefix = "taskflow::detail::type_name_impl<";
  constexpr std::string_view suffix = ">(void)";
#else
  constexpr std::string_view prefix = "";
  constexpr std::string_view suffix = "";
#endif

  constexpr std::string_view function = __PRETTY_FUNCTION__;
  const size_t start = function.find(prefix) + prefix.size();
  const size_t end = function.find(suffix, start);

  if (start == std::string_view::npos || end == std::string_view::npos || start >= end) {
    return "unknown_type";
  }

  return function.substr(start, end - start);
}

}  // namespace detail

// Get type name as string_view
template <typename T>
constexpr std::string_view type_name() {
  return detail::type_name_impl<T>();
}

// For C++23 compatibility
#if __cplusplus >= 202302L
using std::type_name;
#endif

// Forward declarations
struct TaskCtx;
class StateStorage;

// Task state enumeration
enum class TaskState : std::uint8_t {
  created,
  running,
  success,
  failure
};

// Task priority enumeration
enum class TaskPriority : std::uint8_t {
  lowest = 0,
  low = 25,
  normal = 50,
  high = 75,
  critical = 100,
};

// Task ID type
using TaskID = std::uint64_t;

// Default task traits
template <typename T>
struct task_traits {
  static constexpr std::string_view name = taskflow::type_name<T>();
  static constexpr std::string_view description = "";
  static constexpr bool cancellable = false;
  static constexpr bool observable = false;
  static constexpr TaskPriority priority = TaskPriority::normal;
  using state_type = TaskState;
};

// Task trait detection using SFINAE
template <typename T, typename = void>
struct is_task : std::false_type {};

template <typename T>
struct is_task<T, std::void_t<decltype(std::declval<T&>()(std::declval<TaskCtx&>()))>> : std::true_type {};

template <typename T>
constexpr bool is_task_v = is_task<T>::value;

// Cancellable task trait
template <typename T>
struct is_cancellable_task : std::conjunction<is_task<T>, std::bool_constant<task_traits<T>::cancellable>> {};

template <typename T>
constexpr bool is_cancellable_task_v = is_cancellable_task<T>::value;

// Observable task trait
template <typename T>
struct is_observable_task : std::conjunction<is_task<T>, std::bool_constant<task_traits<T>::observable>> {};

template <typename T>
constexpr bool is_observable_task_v = is_observable_task<T>::value;

// Priority validation traits
template <TaskPriority P>
struct priority_traits {
  static constexpr bool is_valid = true;
  static constexpr bool is_standard = P == TaskPriority::lowest || P == TaskPriority::low ||
                                      P == TaskPriority::normal || P == TaskPriority::high ||
                                      P == TaskPriority::critical;
  static constexpr bool is_custom = !is_standard;
  static constexpr int value = static_cast<int>(P);
  static constexpr bool is_in_range = value >= 0 && value <= 255;

  static_assert(is_in_range, "TaskPriority value must be between 0 and 255");
};

// State transition traits
template <TaskState From, TaskState To>
struct state_transition_traits {
  static constexpr bool is_valid = [] {
    switch (From) {
      case TaskState::created:
        return To == TaskState::running;
      case TaskState::running:
        return To == TaskState::success || To == TaskState::failure;
      case TaskState::success:
      case TaskState::failure:
        return false;  // Terminal states
      default:
        return false;
    }
  }();
};

// Priority validation function
template <TaskPriority P>
constexpr auto validate_priority() {
  return priority_traits<P>{};
}

// Type-safe task input validation using SFINAE
template <typename T>
struct is_valid_task_input : std::disjunction<std::is_arithmetic<T>, std::is_same<T, std::string>> {};

template <typename T>
constexpr bool is_valid_task_input_v = is_valid_task_input<T>::value;

// Task result type
template <typename T = void>
struct TaskResult {
  TaskState state{TaskState::created};
  T data{};
  std::string error_message;
  bool cancelled{false};  // Cancellation flag

  [[nodiscard]] bool is_success() const { return state == TaskState::success && !cancelled; }
  [[nodiscard]] bool is_failure() const { return state == TaskState::failure || cancelled; }
};

template <>
struct TaskResult<void> {
  TaskState state{TaskState::created};
  std::string error_message;
  bool cancelled{false};  // Cancellation flag

  [[nodiscard]] bool is_success() const { return state == TaskState::success && !cancelled; }
  [[nodiscard]] bool is_failure() const { return state == TaskState::failure || cancelled; }
};

}  // namespace taskflow
