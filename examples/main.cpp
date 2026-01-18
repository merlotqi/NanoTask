#include <iostream>
#include <taskflow/task_manager.hpp>

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
      std::cout << "Progress: " << progress->progress * 100.0f << "% - " << progress->message << std::endl;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Stop processing
  manager.stop_processing();

  std::cout << "\nAll examples completed!" << std::endl;
  return 0;
}
