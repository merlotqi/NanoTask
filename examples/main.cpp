#include <iostream>
#include <velotask/taskmanager.hpp>

class ExampleTask : public velo::TaskBase {
public:
  using TaskBase::TaskBase;
  std::string getName() const override { return "ExampleTask"; }
  std::string getDescription() const override { return "An example task"; }

  void execute() override {
    std::cout << "Executing task: " << getTaskId() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    finishExecute({{"result", "success"}});
  }
  void cleanup() override {}
};

class FailingTask : public velo::TaskBase {
public:
  using TaskBase::TaskBase;
  std::string getName() const override { return "FailingTask"; }
  std::string getDescription() const override { return "A task that may fail"; }

  void execute() override {
    static int counter = 0;
    counter++;
    if (counter % 3 != 0) {  // Fail 2 out of 3 times
      std::cout << "Task " << getTaskId() << " failed, attempt " << counter << std::endl;
      failExecute("Simulated failure");
    } else {
      std::cout << "Task " << getTaskId() << " succeeded on attempt " << counter << std::endl;
      finishExecute({{"result", "success"}});
    }
  }
  void cleanup() override {}
};

int main() {
  std::cout << "VeloTask library examples" << std::endl;

  auto& manager = velo::TaskManager::getInstance();

  // Register task types
  manager.registerTaskType<ExampleTask>("example");

  // Example 1: Basic task submission
  std::cout << "\n1. Basic task submission:" << std::endl;
  auto id1 = manager.submitTask("example", {}, velo::TaskLifecycle::disposable, velo::TaskPriority::normal);
  std::cout << "Submitted task: " << id1 << std::endl;

  // Wait for completion
  auto info = manager.getTaskInfo(id1);
  while (info && info->state != velo::TaskState::success && info->state != velo::TaskState::failure) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    info = manager.getTaskInfo(id1);
  }
  std::cout << "Task completed with state: " << (int)info->state << std::endl;

  // Example 2: Batch submission
  std::cout << "\n2. Batch task submission:" << std::endl;
  std::vector<std::tuple<std::string, velo::json, velo::TaskLifecycle, velo::TaskPriority>> batchTasks = {
    {"example", {}, velo::TaskLifecycle::disposable, velo::TaskPriority::high},
    {"example", {}, velo::TaskLifecycle::disposable, velo::TaskPriority::low},
    {"example", {}, velo::TaskLifecycle::disposable, velo::TaskPriority::normal}
  };
  auto batchIds = manager.submitTasks(batchTasks);
  std::cout << "Submitted " << batchIds.size() << " tasks in batch" << std::endl;

  // Wait for all to complete
  bool allDone = false;
  while (!allDone) {
    allDone = true;
    for (const auto& id : batchIds) {
      auto taskInfo = manager.getTaskInfo(id);
      if (taskInfo && (taskInfo->state == velo::TaskState::pending || taskInfo->state == velo::TaskState::running)) {
        allDone = false;
        break;
      }
    }
    if (!allDone) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "All batch tasks completed" << std::endl;

  // Example 3: Retry mechanism
  std::cout << "\n3. Retry mechanism:" << std::endl;
  auto failingTask = std::make_shared<FailingTask>("failing_task", velo::TaskLifecycle::disposable, velo::TaskPriority::normal, 2);
  auto failId = manager.submitTask(failingTask, {});
  std::cout << "Submitted failing task: " << failId << std::endl;

  // Wait for completion
  info = manager.getTaskInfo(failId);
  while (info && info->state != velo::TaskState::success && info->state != velo::TaskState::failure) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    info = manager.getTaskInfo(failId);
  }
  std::cout << "Failing task final state: " << (int)info->state << std::endl;

  // Example 4: Task dependencies
  std::cout << "\n4. Task dependencies:" << std::endl;
  auto dep1 = manager.submitTask("example", {}, velo::TaskLifecycle::disposable, velo::TaskPriority::normal);
  auto dep2 = manager.submitTask("example", {}, velo::TaskLifecycle::disposable, velo::TaskPriority::normal);

  // Create dependent task
  auto dependentTask = std::make_shared<ExampleTask>("dependent_task");
  dependentTask->setDependencies({dep1, dep2});
  auto depId = manager.submitTask(dependentTask, {});

  std::cout << "Submitted dependent task: " << depId << " (depends on " << dep1 << " and " << dep2 << ")" << std::endl;

  // Wait for dependent task
  info = manager.getTaskInfo(depId);
  while (info && info->state != velo::TaskState::success && info->state != velo::TaskState::failure) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    info = manager.getTaskInfo(depId);
  }
  std::cout << "Dependent task completed" << std::endl;

  // Example 5: Custom priority
  std::cout << "\n5. Custom priority:" << std::endl;
  auto customTask = std::make_shared<ExampleTask>("custom_priority_task");
  customTask->setPriority(static_cast<velo::TaskPriority>(150));  // Higher than critical
  auto customId = manager.submitTask(customTask, {});
  std::cout << "Submitted task with custom priority 150: " << customId << std::endl;

  std::cout << "\nAll examples completed!" << std::endl;
  return 0;
}
