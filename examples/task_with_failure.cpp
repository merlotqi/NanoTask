#include <iostream>
#include <taskflow/task_manager.hpp>

int main() {
  std::cout << "TaskFlow Example: Task with Failure" << std::endl;

  // Get the task manager instance
  auto& manager = taskflow::TaskManager::getInstance();

  // Start processing with 4 threads
  manager.start_processing(4);

  // Task with failure
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

  // Stop processing
  manager.stop_processing();

  std::cout << "Example completed!" << std::endl;
  return 0;
}
