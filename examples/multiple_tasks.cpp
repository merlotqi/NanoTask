#include <iostream>
#include <vector>
#include <taskflow/task_manager.hpp>

int main() {
  std::cout << "TaskFlow Example: Multiple Tasks" << std::endl;

  // Get the task manager instance
  auto& manager = taskflow::TaskManager::getInstance();

  // Start processing with 4 threads
  manager.start_processing(4);

  // Multiple task submission
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

  // Stop processing
  manager.stop_processing();

  std::cout << "Example completed!" << std::endl;
  return 0;
}
