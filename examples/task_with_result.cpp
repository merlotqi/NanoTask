#include <iostream>
#include <taskflow/task_manager.hpp>

int main() {
  std::cout << "TaskFlow Example: Task with Custom Result Storage" << std::endl;

  // Get the task manager instance
  auto& manager = taskflow::TaskManager::getInstance();

  // Start processing with 4 threads
  manager.start_processing(4);

  // Task with custom result storage
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

  // Stop processing
  manager.stop_processing();

  std::cout << "Example completed!" << std::endl;
  return 0;
}
