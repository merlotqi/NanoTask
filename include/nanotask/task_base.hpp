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

namespace nanotask {

using json = nlohmann::json;

enum class task_lifecycle : std::uint8_t {
  disposable,
  persistent,
};

enum class task_state : std::uint8_t {
  pending,
  running,
  success,
  failure,
};

enum class task_priority : std::uint8_t {
  low,
  normal,
  high,
  critical,
};

static inline std::string generate_task_id() {
  // XXXXXXXX-XXXX-4XXX-YXXX-XXXXXXXXXXXX
  static thread_local std::random_device rnd;
  static thread_local std::mt19937_64 gen(rnd());
  std::uniform_int_distribution<uint64_t> dis;

  constexpr uint64_t uuid_v4_version_mask = 0xffffffffffff0fffULL;
  constexpr uint64_t uuid_v4_version_bits = 0x0000000000004000ULL;
  constexpr uint64_t uuid_v4_variant_mask = 0x3fffffffffffffffULL;
  constexpr uint64_t uuid_v4_variant_bits = 0x8000000000000000ULL;

  constexpr uint64_t mask_16_bit = 0xffffU;
  constexpr uint64_t mask_48_bit = 0xffffffffffffULL;

  uint64_t part1 = dis(gen);
  uint64_t part2 = dis(gen);

  part1 = (part1 & uuid_v4_version_mask) | uuid_v4_version_bits;
  part2 = (part2 & uuid_v4_variant_mask) | uuid_v4_variant_bits;

  std::stringstream oss;
  oss << std::hex << std::setfill('0') << std::uppercase;

  oss << std::setw(8) << (part1 >> 32);
  oss << "-";
  oss << std::setw(4) << ((part1 >> 16) & mask_16_bit);
  oss << "-";
  oss << std::setw(4) << (part1 & 0xffff);
  oss << "-";
  oss << std::setw(4) << (part2 >> 48);
  oss << "-";
  oss << std::setw(12) << (part2 & mask_48_bit);

  return oss.str();
}

struct task_result {
  task_state state{task_state::pending};
  json data;
  std::string error_message;

  [[nodiscard]] bool is_success() const { return state == task_state::success; }
  [[nodiscard]] bool has_error() const { return state == task_state::failure; }
};

class task_observer {
 public:
  virtual ~task_observer() = default;
  virtual void on_task_started(const std::string& task_id, const json& input) = 0;
  virtual void on_task_completed(const std::string& task_id, const task_result& result) = 0;
  virtual void on_task_progress(const std::string& task_id, const json& progress_info) = 0;
  virtual void on_task_error(const std::string& task_id, const std::string& error_message) = 0;
};

class task_base {
 public:
  explicit task_base(std::string_view task_id, task_lifecycle lifecycle = task_lifecycle::disposable,
                     task_priority priority = task_priority::normal)
      : task_id_(task_id), lifecycle_(lifecycle), priority_(priority) {}

  virtual ~task_base() = default;

  virtual std::string get_name() const = 0;
  virtual std::string get_description() const = 0;
  virtual void execute() = 0;
  virtual void cleanup() = 0;

  std::string get_task_id() const { return task_id_; }
  task_state get_state() const { return state_.load(std::memory_order_acquire); }
  task_lifecycle get_lifecycle() const { return lifecycle_; }
  json get_input() const { return input_; }
  std::chrono::system_clock::time_point get_start_time() const { return start_time_; }
  std::chrono::system_clock::time_point get_end_time() const { return end_time_; }
  task_priority get_priority() const { return priority_; }
  const task_result& get_result() const { return result_; }

  void set_observer(task_observer* observer) { observer_ = observer; }
  void set_input(const json& input) { input_ = input; }
  void set_task_id(std::string_view tid) { task_id_ = tid; }
  void set_priority(task_priority priority) { priority_ = priority; }

  void cancel() {
    cancel_requested_.store(true, std::memory_order_release);
    cv_.notify_all();
  }

  bool restart(const json& new_input = {}) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (state_ == task_state::success || state_ == task_state::failure) {
      reset_state();
      if (!new_input.empty()) input_ = new_input;
      return true;
    }
    return false;
  }

  bool wait_for_completion(int timeout_ms = -1) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto is_terminated = [this] {
      auto ste = state_.load(std::memory_order_relaxed);
      return ste == task_state::success || ste == task_state::failure;
    };

    if (is_terminated()) return true;

    if (timeout_ms < 0) {
      cv_.wait(lock, is_terminated);
      return true;
    }
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), is_terminated);
  }

  bool is_running() const { return state_.load(std::memory_order_acquire) == task_state::running; }
  bool is_cancel_requested() const { return cancel_requested_.load(std::memory_order_acquire); }

  void begin_execute() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (state_.load(std::memory_order_relaxed) != task_state::pending) {
        throw std::runtime_error("nanotask: task not in pending state");
      }
      state_.store(task_state::running, std::memory_order_release);
      start_time_ = std::chrono::system_clock::now();
    }
    if (observer_ != nullptr) {
      observer_->on_task_started(task_id_, input_);
    }
  }

  void update_progress(const json& progress_info) {
    if (observer_ != nullptr) {
      observer_->on_task_progress(task_id_, progress_info);
    }
  }

  void finish_execute(const json& result_data) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (state_.load(std::memory_order_relaxed) != task_state::running) return;
      state_.store(task_state::success, std::memory_order_release);
      end_time_ = std::chrono::system_clock::now();
      result_.state = task_state::success;
      result_.data = result_data;
      cv_.notify_all();
    }
    if (observer_ != nullptr) {
      observer_->on_task_completed(task_id_, result_);
    }
  }

  void fail_execute(const std::string& message) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto ste = state_.load(std::memory_order_relaxed);
      if (ste != task_state::running && ste != task_state::pending) return;
      state_.store(task_state::failure, std::memory_order_release);
      end_time_ = std::chrono::system_clock::now();
      result_.state = task_state::failure;
      result_.error_message = message;
      cv_.notify_all();
    }
    if (observer_ != nullptr) {
      observer_->on_task_completed(task_id_, result_);
      observer_->on_task_error(task_id_, message);
    }
  }

 private:
  void reset_state() {
    state_.store(task_state::pending, std::memory_order_release);
    result_ = task_result{};
    cancel_requested_.store(false, std::memory_order_release);
  }

  std::string task_id_;
  std::atomic<task_state> state_{task_state::pending};
  task_lifecycle lifecycle_{task_lifecycle::disposable};
  task_priority priority_{task_priority::normal};

  task_result result_;
  json input_;
  task_observer* observer_ = nullptr;

  std::chrono::system_clock::time_point start_time_;
  std::chrono::system_clock::time_point end_time_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> cancel_requested_{false};
};

class functional_task : public task_base {
 public:
  using progress_fn = std::function<void(const json&)>;
  using cancel_fn = std::function<bool()>;
  using task_fn = std::function<json(const json&, progress_fn, cancel_fn)>;

  functional_task(std::string_view task_id, task_fn func) : task_base(task_id), func_(std::move(func)) {}

  std::string get_name() const override { return "functional_task"; }
  std::string get_description() const override { return "Task defined by a lambda"; }

  void execute() override {
    try {
      json result = func_(
          get_input(), [this](const json& info) { this->update_progress(info); },
          [this] { return this->is_cancel_requested(); });

      if (!is_cancel_requested()) {
        finish_execute(result);
      } else {
        fail_execute("task_cancelled");
      }
    } catch (const std::exception& e) {
      fail_execute(e.what());
    } catch (...) {
      fail_execute("unknown_exception");
    }
  }

  void cleanup() override {}

 private:
  task_fn func_;
};

}  // namespace nanotask
