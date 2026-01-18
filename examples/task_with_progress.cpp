#include <iostream>
#include <taskflow/task_manager.hpp>

int main() {
  std::cout << "TaskFlow Example: Task with Progress" << std::endl;

  // Get the task manager instance
  auto& manager = taskflow::TaskManager::getInstance();

  // Start processing with 4 threads
  manager.start_processing(4);

  // Task with progress
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

  // Stop processing
  manager.stop_processing();

  std::cout << "Example completed!" << std::endl;
  return 0;
}
