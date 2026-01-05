# NanoTask

**NanoTask** is a lightweight, high-performance, and thread-safe C++17 asynchronous task management library. It provides a multi-level scheduling architecture including a priority-based task pool, a robust thread pool, and a centralized management singleton.

## 🚀 Key Features

* **Priority Scheduling**: Supports 4 levels of priority (`low`, `normal`, `high`, `critical`) using a sorted `multimap` queue.
* **Flexible Task Types**:
* **Class-based**: Inherit from `task_base` for complex logic.
* **Lambda-based**: Use `functional_task` for quick, "fire-and-forget" or progress-monitored functions.


* **Life-cycle Management**: Distinguish between `disposable` (clean up after finish) and `persistent` (reusable/restartable) tasks.
* **Observability**: Integrated `task_observer` pattern to track progress, completion, and errors in real-time.
* **Smart Cleanup**: Background threads automatically prune historical task info based on time (TTL) and capacity (LRU) to prevent memory bloating.
* **Cross-Platform**: Thread naming support for both Windows (`SetThreadDescription`) and Linux (`pthread_setname_np`).

---

## 🏗 Architecture

1. **TaskManager**: The primary singleton interface. Handles task registration, submission, and global status tracking.
2. **TaskPool**: Manages the life cycle of tasks, handles scheduling logic, and maintains the priority queue.
3. **ThreadPool**: A low-level execution engine that manages a fixed set of worker threads.
4. **TaskBase**: The abstract base class providing common functionality like cancellation, waiting, and state management.

---

## 💻 Quick Start

### 1. Define a Custom Task

```cpp
class MyDownloadTask : public nanotask::task_base {
public:
    using task_base::task_base;
    std::string get_name() const override { return "Downloader"; }
    std::string get_description() const override { return "Downloads files via HTTP"; }

    void execute() override {
        // Business logic here
        for(int i = 0; i <= 100; i += 10) {
            if (is_cancel_requested()) return;
            update_progress({{"percent", i}}); 
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        finish_execute({{"status", "complete"}});
    }
    void cleanup() override {}
};

```

### 2. Register and Submit

```cpp
auto& manager = nanotask::task_manager::get_instance();

// Register the type
manager.register_task_type<MyDownloadTask>("download_service");

// Submit via Type Name
std::string id = manager.submit_task("download_service", {{"url", "https://example.com"}}, 
                                     nanotask::task_lifecycle::disposable,
                                     nanotask::task_priority::high);

// OR Submit via Lambda
manager.submit_task([](const json& input, auto progress, auto is_cancelled) {
    progress({{"step", "starting"}});
    return json({{"result", "ok"}});
});

```

### 3. Monitor Progress

```cpp
auto info = manager.get_task_info(id);
if (info) {
    std::cout << "State: " << (int)info->state << std::endl;
    std::cout << "Progress: " << info->current_progress_info.dump() << std::endl;
}

```

---

## ⚙️ Configuration

Within `task_manager.hpp`, you can adjust the following constants to fit your memory constraints:

* `max_info_age_`: How long finished task records stay in memory (default: **24 hours**).
* `max_info_count_`: Maximum number of records before the LRU trimmer kicks in (default: **1000 records**).
* `cleanup_loop`: Runs every **5 minutes** to keep the manager healthy.

## 🛠 Dependencies

* [nlohmann/json](https://github.com/nlohmann/json): For input/output data and progress reporting.
* **C++17** or higher.
