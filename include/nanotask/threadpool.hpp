#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <processthreadsapi.h>
#else
#include <pthread.h>
#endif

class thread_pool {
 public:
  explicit thread_pool(size_t threads = std::thread::hardware_concurrency()) : stop_(false) {
    workers_.reserve(threads);
    for (size_t i = 0; i < threads; ++i) {
      workers_.emplace_back([this, i] {
        set_current_thread_name("task_w_" + std::to_string(i));
        while (true) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return stop_.load(std::memory_order_relaxed) || !tasks_.empty(); });

            if (stop_.load(std::memory_order_relaxed) && tasks_.empty()) {
              return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
          }
          if (task) {
            try {
              task();
            } catch (...) {
            }
          }
        }
      });
    }
  }

  ~thread_pool() {
    stop_.store(true, std::memory_order_release);
    condition_.notify_all();
    for (auto& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

  thread_pool(const thread_pool&) = delete;
  thread_pool& operator=(const thread_pool&) = delete;

  template <typename F, typename... Args>
  [[nodiscard]] auto enqueue(F&& fun, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        [func = std::forward<F>(fun), args_tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
          return std::apply(std::move(func), std::move(args_tuple));
        });

    std::future<return_type> res = task->get_future();
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (stop_.load(std::memory_order_relaxed)) {
        throw std::runtime_error("thread_pool: enqueue on stopped pool");
      }
      tasks_.emplace([task] { (*task)(); });
    }
    condition_.notify_one();
    return res;
  }

  template <typename F, typename... Args>
  void execute(F&& fun, Args&&... args) {
    auto task = [func = std::forward<F>(fun), args_tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
      std::apply(std::move(func), std::move(args_tuple));
    };

    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (stop_.load(std::memory_order_relaxed)) return;
      tasks_.emplace(std::move(task));
    }
    condition_.notify_one();
  }

  void cleanup_queue() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    std::queue<std::function<void()>> empty;
    std::swap(tasks_, empty);
  }

  size_t get_queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
  }

  size_t get_worker_count() const { return workers_.size(); }

 private:
  static void set_current_thread_name(const std::string& name) {
#ifdef _WIN32
    std::wstring wname(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wname.c_str());
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
#endif
  }

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  mutable std::mutex queue_mutex_;
  std::condition_variable condition_;
  std::atomic<bool> stop_;
};
