#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

#include "task_base.hpp"
#include "taskpool.hpp"

namespace nanotask {

struct task_info {
  std::string task_id;
  std::string name;
  std::string description;
  task_state state = task_state::pending;
  task_lifecycle lifecycle{task_lifecycle::disposable};

  json input;
  json result_data;
  json current_progress_info;
  std::string error_message;

  std::chrono::system_clock::time_point created_time;
  std::chrono::system_clock::time_point updated_time;
  std::chrono::system_clock::time_point start_time;
  std::chrono::system_clock::time_point end_time;
};

class task_manager : public task_observer {
 public:
  static task_manager& get_instance() {
    static task_manager instance;
    return instance;
  }

  ~task_manager() override {
    stop_cleanup_ = true;
    cleanup_cv_.notify_all();
    if (info_cleanup_thread_.joinable()) {
      info_cleanup_thread_.join();
    }
  }

  task_manager(const task_manager&) = delete;
  task_manager& operator=(const task_manager&) = delete;

  template <typename T>
  void register_task_type(const std::string& task_type_name) {
    std::lock_guard<std::mutex> lock(task_factories_mutex_);
    task_factories_[task_type_name] = create_adapter_factory<T>();
  }

  std::string submit_task(const std::string& task_type_name, const json& input = {},
                          task_lifecycle lifecycle = task_lifecycle::disposable,
                          task_priority priority = task_priority::normal) {
    std::shared_ptr<task_base> task;
    {
      std::lock_guard<std::mutex> lock(task_factories_mutex_);
      auto it = task_factories_.find(task_type_name);
      if (it == task_factories_.end()) {
        throw std::runtime_error("Unknown task type: " + task_type_name);
      }
      task = it->second(generate_task_id(), lifecycle, priority);
    }

    task->set_observer(this);

    std::string task_id = task_pool_.submit_task(task, input);
    {
      std::lock_guard<std::mutex> lock(task_infos_mutex_);
      task_info info;
      info.task_id = task_id;
      info.lifecycle = lifecycle;
      info.name = task->get_name();
      info.description = task->get_description();
      info.input = input;
      info.created_time = std::chrono::system_clock::now();
      info.updated_time = info.created_time;

      task_infos_[task_id] = info;
    }
    return task_id;
  }

  std::string submit_task(functional_task::task_fn func, const json& input = {},
                          task_lifecycle lifecycle = task_lifecycle::disposable,
                          task_priority priority = task_priority::normal) {
    std::string task_id = generate_task_id();
    auto task = std::make_shared<functional_task>(task_id, std::move(func));
    task->set_observer(this);
    (void)task_pool_.submit_task(task, input);
    {
      std::lock_guard<std::mutex> lock(task_infos_mutex_);
      task_info info;
      info.task_id = task_id;
      info.lifecycle = lifecycle;
      info.name = task->get_name();
      info.description = task->get_description();
      info.input = input;
      info.created_time = std::chrono::system_clock::now();
      info.updated_time = info.created_time;

      task_infos_[task_id] = info;
    }
    return task_id;
  }

  bool restart_task(const std::string& task_id, const json& new_input = {}) {
    if (!task_pool_.restart_task(task_id, new_input)) {
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(task_infos_mutex_);
      auto it = task_infos_.find(task_id);
      if (it != task_infos_.end()) {
        it->second.state = task_state::pending;
        it->second.result_data.clear();
        it->second.error_message.clear();
        if (!new_input.empty()) {
          it->second.input = new_input;
        }
        it->second.updated_time = std::chrono::system_clock::now();
      }
    }

    return true;
  }

  bool has_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(task_infos_mutex_);
    return task_infos_.find(task_id) != task_infos_.end();
  }

  bool cancel_task(const std::string& task_id) { return task_pool_.cancel_task(task_id); }

  void cleanup() {
    task_pool_.cleanup();
    {
      std::lock_guard<std::mutex> lock(task_infos_mutex_);
      task_infos_.clear();
    }
  }

  std::optional<task_info> get_task_info(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(task_infos_mutex_);
    auto it = task_infos_.find(task_id);
    if (it != task_infos_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  task_state get_task_state(const std::string& task_id) const {
    auto info = get_task_info(task_id);
    return info ? info->state : task_state::pending;
  }

  json get_task_result(const std::string& task_id) const {
    auto info = get_task_info(task_id);
    if (info && !info->result_data.empty()) {
      return info->result_data;
    }
    return json{};
  }

  std::string get_task_error_message(const std::string& task_id) const {
    auto info = get_task_info(task_id);
    if (info && !info->result_data.empty()) {
      return info->error_message;
    }
    return {};
  }

  json get_task_progress(const std::string& task_id) const {
    auto info = get_task_info(task_id);
    if (info && !info->current_progress_info.empty()) {
      return info->current_progress_info;
    }
    return json{};
  }

  void on_task_started(const std::string& task_id, const json& input) override {
    std::lock_guard<std::mutex> lock(task_infos_mutex_);
    auto it = task_infos_.find(task_id);
    if (it != task_infos_.end()) {
      it->second.state = task_state::running;
      it->second.start_time = std::chrono::system_clock::now();
      it->second.updated_time = it->second.start_time;
    }
  }

  void on_task_completed(const std::string& task_id, const task_result& result) override {
    std::lock_guard<std::mutex> lock(task_infos_mutex_);
    auto it = task_infos_.find(task_id);
    if (it != task_infos_.end()) {
      it->second.state = result.state;
      it->second.result_data = result.data;
      it->second.end_time = std::chrono::system_clock::now();
      it->second.updated_time = it->second.end_time;
    }
  }

  void on_task_progress(const std::string& task_id, const json& progress) override {
    std::lock_guard<std::mutex> lock(task_infos_mutex_);
    auto it = task_infos_.find(task_id);
    if (it != task_infos_.end()) {
      it->second.updated_time = std::chrono::system_clock::now();
      it->second.current_progress_info = progress;
    }
  }

  void on_task_error(const std::string& task_id, const std::string& error_msg) override {
    std::lock_guard<std::mutex> lock(task_infos_mutex_);
    auto it = task_infos_.find(task_id);
    if (it != task_infos_.end()) {
      it->second.error_message = error_msg;
    }
  }

 private:
  explicit task_manager(size_t thread_count = std::thread::hardware_concurrency()) : task_pool_(thread_count) {
    info_cleanup_thread_ = std::thread([this] { cleanup_info_loop(); });
  }

  task_pool task_pool_;

  std::unordered_map<std::string, task_info> task_infos_;
  mutable std::mutex task_infos_mutex_;

  using task_factory = std::function<std::shared_ptr<task_base>(const std::string&, task_lifecycle, task_priority)>;

  std::unordered_map<std::string, task_factory> task_factories_;
  mutable std::mutex task_factories_mutex_;

  std::thread info_cleanup_thread_;
  std::atomic<bool> stop_cleanup_{false};
  const std::chrono::hours max_info_age{24};
  const size_t max_info_count{1000};
  std::condition_variable cleanup_cv_;

  template <typename T>
  task_factory create_adapter_factory() {
    if constexpr (std::is_constructible_v<T, const std::string&, task_lifecycle, task_priority>) {
      return [](const std::string& task_id, task_lifecycle lifecycle, task_priority priority) {
        return std::make_shared<T>(task_id, lifecycle, priority);
      };
    } else if constexpr (std::is_constructible_v<T, const std::string&, task_lifecycle>) {
      return [](const std::string& task_id, task_lifecycle lifecycle, task_priority priority) {
        auto task = std::make_shared<T>(task_id, lifecycle);
        task->set_priority(priority);
        return task;
      };
    } else if constexpr (std::is_constructible_v<T, const std::string&>) {
      return [](const std::string& task_id, task_lifecycle lifecycle, task_priority priority) {
        auto task = std::make_shared<T>(task_id);
        task->set_priority(priority);
        return task;
      };
    }
    return {};
  }

  void cleanup_info_loop() {
    while (!stop_cleanup_) {
      auto now = std::chrono::system_clock::now();

      std::unique_lock<std::mutex> lock(task_infos_mutex_);
      cleanup_cv_.wait_for(lock, std::chrono::minutes(5), [this] { return stop_cleanup_.load(); });

      if (stop_cleanup_) break;

      auto it = task_infos_.begin();
      while (it != task_infos_.end()) {
        const auto& info = it->second;
        bool is_terminated = (info.state == task_state::success || info.state == task_state::failure);
        bool is_expired = (now - info.updated_time) > max_info_age;

        if (is_terminated && is_expired) {
          it = task_infos_.erase(it);
        } else {
          ++it;
        }
      }

      if (task_infos_.size() > max_info_count) {
        force_trim_infos();
      }
    }
  }

  void force_trim_infos() {
    struct trim_candidate {
      std::string id;
      std::chrono::system_clock::time_point updated_time;
    };
    std::vector<trim_candidate> candidates;

    for (const auto& [id, info] : task_infos_) {
      if ((info.state == task_state::success || info.state == task_state::failure) &&
          info.lifecycle == task_lifecycle::disposable) {
        candidates.push_back({id, info.updated_time});
      }
    }

    if (candidates.empty()) return;

    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.updated_time < b.updated_time; });

    size_t excess = task_infos_.size() - max_info_count;
    size_t to_remove = std::min(excess, candidates.size());

    for (size_t i = 0; i < to_remove; ++i) {
      task_infos_.erase(candidates[i].id);
    }
  }
};

}  // namespace nanotask
