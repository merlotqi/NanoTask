#include <iostream>
#include <taskflow/task_manager.hpp>

int main() {
  std::cout << "TaskFlow Example: Persistent Task Reawakening" << std::endl;

  // Get the task manager instance
  auto& manager = taskflow::TaskManager::getInstance();

  // Start processing with 4 threads
  manager.start_processing(4);

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

  std::cout << "Example completed!" << std::endl;
  return 0;
}
