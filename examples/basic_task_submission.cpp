#include <iostream>
#include <taskflow/task_manager.hpp>

int main() {
  std::cout << "TaskFlow Example: Basic Task Submission" << std::endl;

  // Get the task manager instance
  auto& manager = taskflow::TaskManager::getInstance();

  // Start processing with 4 threads
  manager.start_processing(4);

  // Basic task submission
  auto task = [] (taskflow::TaskCtx& ctx) {
    std::cout << "Executing task: " << ctx.id << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ctx.success();
  };

  auto id = manager.submit_task(task);
  std::cout << "Submitted task: " << id << std::endl;

  // Wait for completion
  while (true) {
    auto state = manager.query_state(id);
    if (state && *state != taskflow::TaskState::running && *state != taskflow::TaskState::created) {
      std::cout << "Task completed with state: " << static_cast<int>(*state) << std::endl;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Stop processing
  manager.stop_processing();

  std::cout << "Example completed!" << std::endl;
  return 0;
}
