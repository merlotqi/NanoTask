#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace taskflow {

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

// Task Lifecycle enumeration
enum class TaskLifecycle : std::uint8_t {
  disposable,
  persistent,
};

// Task Observability enumeration
enum class TaskObservability : std::uint8_t {
  none,     // No observation capabilities
  basic,    // Basic observation (start/end states only)
  progress  // Full observation (states + progress reporting)
};

// Task ID type
using TaskID = std::uint64_t;

// Result ID type
using ResultID = std::uint64_t;

// Result kind enumeration
enum class ResultKind : std::uint8_t {
  none,    // No result
  json,    // JSON data
  file,    // File path
  text,    // Plain text
  binary,  // Binary data
  custom   // Custom result type
};

// Result locator for referencing stored results
struct ResultLocator {
  ResultID id{0};
  ResultKind kind{ResultKind::none};
  std::string metadata;  // Additional metadata (optional)

  ResultLocator() = default;
  ResultLocator(ResultID result_id, ResultKind result_kind, std::string meta = "")
      : id(result_id), kind(result_kind), metadata(std::move(meta)) {}

  [[nodiscard]] bool valid() const { return id != 0; }

  // Comparison operators for use in std::map
  bool operator<(const ResultLocator& other) const {
    if (id != other.id) return id < other.id;
    if (static_cast<int>(kind) != static_cast<int>(other.kind)) {
      return static_cast<int>(kind) < static_cast<int>(other.kind);
    }
    return metadata < other.metadata;
  }

  bool operator==(const ResultLocator& other) const {
    return id == other.id && kind == other.kind && metadata == other.metadata;
  }
};

// Result payload variants
struct ResultPayload {
  ResultKind kind{ResultKind::none};

  // Union-like storage for different result types
  struct {
    nlohmann::json json_data;
    std::string text_data;
    std::string file_path;
    std::vector<std::uint8_t> binary_data;
    std::string custom_data;
  } data;

  ResultPayload() = default;

  // JSON result
  static ResultPayload json(const nlohmann::json& j) {
    ResultPayload p;
    p.kind = ResultKind::json;
    p.data.json_data = j;
    return p;
  }

  // Text result
  static ResultPayload text(std::string text) {
    ResultPayload p;
    p.kind = ResultKind::text;
    p.data.text_data = std::move(text);
    return p;
  }

  // File result
  static ResultPayload file(std::string path) {
    ResultPayload p;
    p.kind = ResultKind::file;
    p.data.file_path = std::move(path);
    return p;
  }

  // Binary result
  static ResultPayload binary(std::vector<std::uint8_t> data) {
    ResultPayload p;
    p.kind = ResultKind::binary;
    p.data.binary_data = std::move(data);
    return p;
  }

  // Custom result
  static ResultPayload custom(std::string data) {
    ResultPayload p;
    p.kind = ResultKind::custom;
    p.data.custom_data = std::move(data);
    return p;
  }
};

// Result storage interface for tasks
class ResultStorage {
 public:
  virtual ~ResultStorage() = default;

  // Store result and return locator
  virtual ResultLocator store_result(ResultPayload payload) = 0;

  // Retrieve result by locator
  virtual std::optional<ResultPayload> get_result(const ResultLocator& locator) const = 0;

  // Remove result
  virtual bool remove_result(const ResultLocator& locator) = 0;

  // Generate next result ID
  virtual ResultID next_result_id() = 0;
};

// Simple in-memory result storage implementation
class SimpleResultStorage : public ResultStorage {
 public:
  SimpleResultStorage() = default;

  ResultLocator store_result(ResultPayload payload) override {
    std::unique_lock lock(mutex_);
    ResultID id = next_id_++;
    ResultLocator locator{id, payload.kind};
    results_[locator] = std::move(payload);
    return locator;
  }

  std::optional<ResultPayload> get_result(const ResultLocator& locator) const override {
    std::shared_lock lock(mutex_);
    auto it = results_.find(locator);
    return it != results_.end() ? std::optional<ResultPayload>(it->second) : std::nullopt;
  }

  bool remove_result(const ResultLocator& locator) override {
    std::unique_lock lock(mutex_);
    return results_.erase(locator) > 0;
  }

  ResultID next_result_id() override {
    std::unique_lock lock(mutex_);
    return next_id_++;
  }

 private:
  mutable std::shared_mutex mutex_;
  std::map<ResultLocator, ResultPayload> results_;
  ResultID next_id_{1};
};

// Default task traits
template <typename T>
struct task_traits {
  // metadata
  static constexpr std::string_view name = taskflow::type_name<T>();
  static constexpr std::string_view description = "";
  static constexpr TaskPriority priority = TaskPriority::normal;

  // capabilities
  static constexpr bool cancellable = false;
  static constexpr TaskObservability observability = TaskObservability::basic;
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

// Basic observable task trait (supports basic observation)
template <typename T>
struct is_basic_observable_task
    : std::conjunction<is_task<T>, std::bool_constant<task_traits<T>::observability == TaskObservability::basic ||
                                                      task_traits<T>::observability == TaskObservability::progress>> {};

template <typename T>
constexpr bool is_basic_observable_task_v = is_basic_observable_task<T>::value;

// Progress observable task trait (supports progress reporting)
template <typename T>
struct is_progress_observable_task
    : std::conjunction<is_task<T>, std::bool_constant<task_traits<T>::observability == TaskObservability::progress>> {};

template <typename T>
constexpr bool is_progress_observable_task_v = is_progress_observable_task<T>::value;

// Legacy observable task trait (for backward compatibility)
template <typename T>
struct is_observable_task
    : std::conjunction<is_task<T>, std::bool_constant<task_traits<T>::observability != TaskObservability::none>> {};

template <typename T>
constexpr bool is_observable_task_v = is_observable_task<T>::value;

template <typename T>
struct is_valid_progress_type {
  static constexpr bool value = []() {
    if constexpr (std::is_arithmetic_v<T>) return true;
    if constexpr (std::is_same_v<T, nlohmann::json> || std::is_same_v<T, std::string>) return true;
    if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= 512) return true;

    return false;
  }();
};

}  // namespace taskflow
