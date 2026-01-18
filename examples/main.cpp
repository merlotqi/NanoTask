#include <iostream>
#include <taskflow/task_manager.hpp>

// Example task types with different observability levels
struct NoObservationTask {
    static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::none;
    static constexpr bool cancellable = false;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "No observation task executing" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx.success();
    }
};

struct BasicObservationTask {
    static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::basic;
    static constexpr bool cancellable = false;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "Basic observation task executing" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx.success();
    }
};

struct ProgressObservationTask {
    static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::progress;
    static constexpr bool cancellable = false;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "Progress observation task executing" << std::endl;
        ctx.report_progress(0.5f, "Halfway done");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx.success();
    }
};

struct CancellableTask {
    static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::basic;
    static constexpr bool cancellable = true;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "Cancellable task executing" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx.success();
    }
};

// Custom task with progress_info trait detection
struct CustomProgressTask {
    // Indicate that this task has custom progress info
    static constexpr bool progress_info = true;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "Custom progress task executing" << std::endl;

        // Report progress with custom data structure
        struct CustomProgress {
            int current_step;
            int total_steps;
            std::string phase;
            double percentage;
        };

        ctx.report_progress(CustomProgress{10, 100, "starting", 10.0});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        ctx.report_progress(CustomProgress{50, 100, "processing", 50.0});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        ctx.report_progress(CustomProgress{100, 100, "completed", 100.0});
        ctx.success();
    }
};

int main() {
  std::cout << "TaskFlow library examples" << std::endl;

  // Get the task manager instance
  auto& manager = taskflow::TaskManager::getInstance();

  // Start processing with 4 threads
  manager.start_processing(4);

  // Example 1: Basic task submission
  std::cout << "\n1. Basic task submission:" << std::endl;
  auto task1 = [] (taskflow::TaskCtx& ctx) {
    std::cout << "Executing task: " << ctx.id << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ctx.success();
  };
  auto id1 = manager.submit_task(task1);
  std::cout << "Submitted task: " << id1 << std::endl;

  // Wait for completion
  while (true) {
    auto state = manager.query_state(id1);
    if (state && *state != taskflow::TaskState::running && *state != taskflow::TaskState::created) {
      std::cout << "Task completed with state: " << static_cast<int>(*state) << std::endl;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Example 2: Multiple tasks
  std::cout << "\n2. Multiple task submission:" << std::endl;
  std::vector<taskflow::TaskID> task_ids;
  for (int i = 0; i < 3; ++i) {
    auto task = [i] (taskflow::TaskCtx& ctx) {
      std::cout << "Task " << i << " (ID: " << ctx.id << ") executing" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(50 + i * 25));
      ctx.success();
    };
    auto id = manager.submit_task(task);
    task_ids.push_back(id);
    std::cout << "Submitted task " << i << ": " << id << std::endl;
  }

  // Wait for all to complete
  bool all_done = false;
  while (!all_done) {
    all_done = true;
    for (auto id : task_ids) {
      auto state = manager.query_state(id);
      if (!state || *state == taskflow::TaskState::running || *state == taskflow::TaskState::created) {
        all_done = false;
        break;
      }
    }
    if (!all_done) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "All tasks completed" << std::endl;

  // Example 3: Task with failure
  std::cout << "\n3. Task with failure:" << std::endl;
  auto failing_task = [] (taskflow::TaskCtx& ctx) {
    std::cout << "Task " << ctx.id << " failing" << std::endl;
    ctx.failure("Simulated failure");
  };
  auto fail_id = manager.submit_task(failing_task);
  std::cout << "Submitted failing task: " << fail_id << std::endl;

  // Wait for completion
  while (true) {
    auto state = manager.query_state(fail_id);
    if (state && *state != taskflow::TaskState::running && *state != taskflow::TaskState::created) {
      std::cout << "Failing task completed with state: " << static_cast<int>(*state) << std::endl;
      if (auto error = manager.get_error(fail_id)) {
        std::cout << "Error message: " << *error << std::endl;
      }
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Example 4: Task with progress
  std::cout << "\n4. Task with progress:" << std::endl;
  auto progress_task = [] (taskflow::TaskCtx& ctx) {
    for (int i = 0; i <= 100; i += 25) {
      ctx.report_progress(static_cast<float>(i) / 100.0f, "Processing step " + std::to_string(i));
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ctx.success();
  };
  auto progress_id = manager.submit_task(progress_task);
  std::cout << "Submitted progress task: " << progress_id << std::endl;

  // Monitor progress
  while (true) {
    auto state = manager.query_state(progress_id);
    if (state && *state != taskflow::TaskState::running && *state != taskflow::TaskState::created) {
      std::cout << "Progress task completed with state: " << static_cast<int>(*state) << std::endl;
      break;
    }
    if (auto progress = manager.get_progress(progress_id)) {
      std::cout << "Progress: " << progress->first * 100.0f << "% - " << progress->second << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Example 5: Task with custom result storage
  std::cout << "\n5. Task with custom result storage:" << std::endl;

  auto result_task = [] (taskflow::TaskCtx& ctx) {
    std::cout << "Task " << ctx.id << " storing custom result" << std::endl;

    // Store different types of results
    nlohmann::json result = {
      {"task_id", ctx.id},
      {"status", "completed"},
      {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
      {"data", {
        {"processed_items", 42},
        {"success_rate", 0.95}
      }}
    };

    ctx.success_with_result(taskflow::ResultPayload::json(result));
  };

  auto result_id = manager.submit_task(result_task);
  std::cout << "Submitted result task: " << result_id << std::endl;

  // Wait for completion and get result
  while (true) {
    auto state = manager.query_state(result_id);
    if (state && *state != taskflow::TaskState::running && *state != taskflow::TaskState::created) {
      std::cout << "Result task completed with state: " << static_cast<int>(*state) << std::endl;

      // Retrieve and display the result
      if (auto result = manager.get_result(result_id)) {
        if (result->kind == taskflow::ResultKind::json) {
          std::cout << "Result JSON: " << result->data.json_data.dump(2) << std::endl;
        }
      } else {
        std::cout << "No result found" << std::endl;
      }
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Example 6: Persistent task that can be reawakened
  std::cout << "\n6. Persistent task reawakening:" << std::endl;

  // Counter to track reawakenings
  static int reawaken_count = 0;

  auto persistent_task = [] (taskflow::TaskCtx& ctx) {
    std::cout << "Persistent task " << ctx.id << " executing (count: " << ++reawaken_count << ")" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Store result for persistent task
    nlohmann::json result = {
      {"execution_count", reawaken_count},
      {"task_type", "persistent"}
    };
    ctx.success_with_result(taskflow::ResultPayload::json(result));
  };

  auto persistent_id = manager.submit_task(persistent_task, taskflow::TaskLifecycle::persistent);
  std::cout << "Submitted persistent task: " << persistent_id << std::endl;
  std::cout << "Is persistent: " << (manager.is_persistent_task(persistent_id) ? "Yes" : "No") << std::endl;

  // Wait for first execution
  while (true) {
    auto state = manager.query_state(persistent_id);
    if (state && *state != taskflow::TaskState::running && *state != taskflow::TaskState::created) {
      std::cout << "Persistent task first execution completed" << std::endl;

      // Get first result
      if (auto result = manager.get_result(persistent_id)) {
        std::cout << "First execution result: " << result->data.json_data.dump() << std::endl;
      }
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Reawaken the persistent task with new parameters
  std::cout << "Reawakening persistent task..." << std::endl;
  auto new_persistent_task = [] (taskflow::TaskCtx& ctx) {
    std::cout << "Reawakened persistent task " << ctx.id << " with new logic!" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Store updated result
    nlohmann::json result = {
      {"execution_count", 2},
      {"task_type", "persistent_reawakened"},
      {"message", "This is the reawakened execution"}
    };
    ctx.success_with_result(taskflow::ResultPayload::json(result));
  };

  bool reawaken_success = manager.reawaken_task(persistent_id, new_persistent_task);
  std::cout << "Reawaken request: " << (reawaken_success ? "Success" : "Failed") << std::endl;

  // Wait for reawakened execution
  while (true) {
    auto state = manager.query_state(persistent_id);
    if (state && *state != taskflow::TaskState::running && *state != taskflow::TaskState::created) {
      std::cout << "Persistent task reawakened execution completed" << std::endl;

      // Get updated result
      if (auto result = manager.get_result(persistent_id)) {
        std::cout << "Reawakened execution result: " << result->data.json_data.dump() << std::endl;
      }
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Stop processing
  manager.stop_processing();

  std::cout << "\nAll examples completed!" << std::endl;
  return 0;
}
