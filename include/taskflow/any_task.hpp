#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "task_ctx.hpp"
#include "task_traits.hpp"

namespace taskflow {

struct AnyTask {
  TaskID id;
  TaskLifecycle lifecycle{TaskLifecycle::disposable};
  std::unique_ptr<void, void (*)(void*)> storage;
  void (*invoke)(void*, TaskRuntimeCtx&, ResultStorage*);

  AnyTask() : id(0), storage(nullptr, nullptr), invoke(nullptr) {}
  template <typename Task>
  AnyTask(TaskID task_id, Task task, TaskLifecycle lifecycle_type = TaskLifecycle::disposable,
          typename std::enable_if<is_task_v<Task>, int>::type = 0)
      : id(task_id),
        lifecycle(lifecycle_type),
        storage(new Task(std::move(task)), [](void* ptr) { delete static_cast<Task*>(ptr); }),
        invoke([](void* ptr, TaskRuntimeCtx& rctx, ResultStorage* result_storage) {
          Task& task_ref = *static_cast<Task*>(ptr);
          execute(task_ref, rctx, result_storage);
        }) {}

  AnyTask(AnyTask&& other) noexcept : id(other.id), storage(std::move(other.storage)), invoke(other.invoke) {
    other.invoke = nullptr;
  }
  AnyTask& operator=(AnyTask&& other) noexcept {
    if (this != &other) {
      id = other.id;
      storage = std::move(other.storage);
      invoke = other.invoke;
      other.invoke = nullptr;
    }
    return *this;
  }

  void execute_task(TaskRuntimeCtx& rctx, ResultStorage* result_storage) {
    if (invoke && storage) {
      invoke(storage.get(), rctx, result_storage);
    }
  }

  [[nodiscard]] bool valid() const { return invoke != nullptr && storage != nullptr; }

  void reset() {
    id = 0;
    storage.reset();
    invoke = nullptr;
  }
};

template <typename Task>
typename std::enable_if<is_task_v<Task>, AnyTask>::type make_any_task(TaskID id, Task task, TaskLifecycle lifecycle = TaskLifecycle::disposable) {
  return AnyTask(id, std::move(task), lifecycle);
}

template <typename Task>
typename std::enable_if<is_task_v<Task>, void>::type execute(Task& task, TaskRuntimeCtx& rctx);

template <typename Task>
typename std::enable_if<is_task_v<Task>, void>::type execute(Task&& task, TaskRuntimeCtx& rctx);

}  // namespace taskflow
