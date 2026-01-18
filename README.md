# TaskFlow

**TaskFlow** is a lightweight, high-performance, and thread-safe C++17 asynchronous task management library. It provides a simple yet powerful API for submitting and managing asynchronous tasks with progress tracking, cancellation, and error handling.

## 🚀 Key Features

* **Simple API**: Submit tasks as lambda functions or callable objects
* **Progress Tracking**: Built-in progress reporting and monitoring
* **Cancellation Support**: Tasks can check for cancellation and handle it gracefully
* **Error Handling**: Comprehensive error reporting and state management
* **Thread-Safe**: Designed for concurrent access from multiple threads
* **C++17**: Modern C++ with concepts and constexpr where available
* **Cross-Platform**: Works on Windows, Linux, and macOS

---

## 🏗 Architecture

1. **TaskManager**: The main singleton that manages task submission and execution
2. **TaskCtx**: Context object passed to tasks for state management and progress reporting
3. **StateStorage**: Internal storage for task states, progress, and errors
4. **Thread Pool**: Manages worker threads for task execution

---

## 💻 Quick Start

### 1. Include the Header

```cpp
#include <taskflow/task_manager.hpp>
```

### 2. Submit a Task

```cpp
// Get the task manager instance
auto& manager = taskflow::TaskManager::getInstance();

// Start processing (specify number of threads)
manager.start_processing(4);

// Submit a simple task
auto task_id = manager.submit_task([](taskflow::TaskCtx& ctx) {
    std::cout << "Task " << ctx.id << " is running" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ctx.success();  // Mark as successful
});
```

### 3. Monitor Task State

```cpp
// Query task state
auto state = manager.query_state(task_id);
if (state) {
    std::cout << "Task state: " << static_cast<int>(*state) << std::endl;
}
```

### 4. Task with Progress

```cpp
auto progress_task = manager.submit_task([](taskflow::TaskCtx& ctx) {
    for (int i = 0; i <= 100; i += 25) {
        ctx.report_progress(static_cast<float>(i) / 100.0f, "Step " + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ctx.success();
});

// Monitor progress
if (auto progress = manager.get_progress(progress_task)) {
    std::cout << "Progress: " << progress->progress * 100.0f << "% - " << progress->message << std::endl;
}
```

### 5. Handle Errors

```cpp
auto failing_task = manager.submit_task([](taskflow::TaskCtx& ctx) {
    try {
        // Some work that might fail
        throw std::runtime_error("Something went wrong");
    } catch (const std::exception& e) {
        ctx.failure(e.what());
    }
});

// Check for errors
if (auto error = manager.get_error(failing_task)) {
    std::cout << "Error: " << *error << std::endl;
}
```

### 6. Cancellation

```cpp
auto cancellable_task = manager.submit_task([](taskflow::TaskCtx& ctx) {
    while (!ctx.is_cancelled()) {
        // Do work, check for cancellation periodically
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (ctx.is_cancelled()) {
        ctx.failure("Task was cancelled");
    } else {
        ctx.success();
    }
});

// Cancel the task
manager.cancel_task(cancellable_task);
```

---

## ⚙️ Configuration

The TaskManager automatically manages cleanup of completed tasks:

* **Cleanup Interval**: Runs every 30 minutes by default
* **Max Task Age**: Tasks older than 24 hours are automatically cleaned up
* **Thread Count**: Specify number of worker threads when calling `start_processing()`

## 🛠 Dependencies

* [nlohmann/json](https://github.com/nlohmann/json): For internal data handling
* **C++17** or higher
