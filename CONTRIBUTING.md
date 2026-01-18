# Contributing to TaskFlow

Thank you for your interest in contributing to TaskFlow! We welcome contributions from everyone. This document provides guidelines and information for contributors.

## 📋 Table of Contents

- [Getting Started](#getting-started)
- [Development Environment](#development-environment)
- [Code Style](#code-style)
- [Building and Testing](#building-and-testing)
- [Submitting Changes](#submitting-changes)
- [Pull Request Process](#pull-request-process)
- [Reporting Issues](#reporting-issues)
- [License](#license)

## 🚀 Getting Started

### Prerequisites

- **C++17** compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- **CMake** 3.14 or higher
- **Git** for version control

### Fork and Clone

1. Fork the repository on GitHub
2. Clone your fork locally:
   ```bash
   git clone https://github.com/your-username/VeloTask.git
   cd VeloTask
   ```
3. Add the upstream repository:
   ```bash
   git remote add upstream https://github.com/merlotqi/VeloTask.git
   ```

### Dependencies

TaskFlow uses the following dependencies:

- **[nlohmann/json](https://github.com/nlohmann/json)**: For JSON data handling (automatically fetched via CMake if not found)
- **Threads**: System threading library

## 🛠 Development Environment

### Setting Up

1. Ensure you have CMake installed
2. Configure the project:
   ```bash
   mkdir build && cd build
   cmake -S .. -B .
   ```

3. Build the project:
   ```bash
   cmake --build .
   ```

### IDE Setup

- **Visual Studio Code**: Use the provided `.vscode/` settings and extensions
- **CLion**: Automatically detects CMake configuration
- **VS Code with CMake Tools**: Recommended for best CMake integration

## 💅 Code Style

TaskFlow follows specific coding standards to maintain code quality and consistency.

### Formatting

- Use **clang-format** with the provided `.clang-format` configuration
- Based on Google style with 120 character line limit
- Run clang-format before committing:
  ```bash
  find . -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i
  ```

### Naming Conventions

- **Classes/Structs**: PascalCase (e.g., `TaskManager`, `TaskCtx`)
- **Functions/Methods**: camelCase (e.g., `submit_task()`, `get_progress()`)
- **Variables**: snake_case (e.g., `task_id`, `progress_info`)
- **Constants**: SCREAMING_SNAKE_CASE (e.g., `MAX_THREADS`)
- **Namespaces**: lowercase (e.g., `taskflow`)

### Code Guidelines

- **C++17 Features**: Use modern C++17 features where appropriate
- **Error Handling**: Use exceptions for exceptional cases, return values for expected errors
- **Documentation**: Document public APIs with clear comments
- **Thread Safety**: Ensure thread safety for shared resources
- **Performance**: Consider performance implications of changes

### File Organization

```
include/taskflow/     # Public headers
├── task_manager.hpp  # Main task management
├── task_traits.hpp   # Task trait definitions
├── task_ctx.hpp      # Task execution context
├── state_storage.hpp # Internal state management
├── threadpool.hpp    # Thread pool implementation
└── any_task.hpp      # Type-erased task wrapper

examples/             # Example programs
├── task_types.hpp    # Shared task type definitions
├── basic_task_submission.cpp
├── multiple_tasks.cpp
└── ...
```

## 🏗 Building and Testing

### Build Process

```bash
# Configure
cmake -S . -B build

# Build
cmake --build build

# Build with specific configuration
cmake --build build --config Release
```

### Build Options

- `TASKFLOW_BUILD_EXAMPLES=ON` (default): Build example programs
- `CMAKE_BUILD_TYPE=Debug|Release`: Build configuration

### Running Examples

After building, you can run individual examples:

```bash
# Run basic task submission example
./build/basic_task_submission

# Run multiple tasks example
./build/multiple_tasks

# Run task with progress example
./build/task_with_progress
```

### Testing

Currently, TaskFlow uses example programs for testing. Each example demonstrates specific functionality:

- `basic_task_submission`: Basic task submission and execution
- `multiple_tasks`: Concurrent task execution
- `task_with_failure`: Error handling
- `task_with_progress`: Progress reporting
- `task_with_result`: Result storage
- `persistent_task`: Persistent task reawakening

## 📝 Submitting Changes

### Commit Guidelines

- Use clear, descriptive commit messages
- Start with a verb in imperative mood (e.g., "Add", "Fix", "Update")
- Reference issue numbers when applicable (e.g., "Fix #123: Handle edge case")
- Keep commits focused on single changes

### Example Commit Messages

```
Add support for custom progress types
Fix memory leak in task cancellation
Update documentation for persistent tasks
Refactor thread pool for better performance
```

### Branch Naming

- Use descriptive branch names
- Prefix with feature type: `feature/`, `bugfix/`, `docs/`, `refactor/`

```
feature/add-custom-result-types
bugfix/handle-cancellation-race-condition
docs/update-contribution-guide
refactor/simplify-task-traits
```

## 🔄 Pull Request Process

1. **Create a Branch**: Create a feature branch from `main`
2. **Make Changes**: Implement your changes with tests
3. **Test Locally**: Ensure all examples build and run correctly
4. **Format Code**: Run clang-format on your changes
5. **Commit**: Make focused commits with clear messages
6. **Push**: Push your branch to your fork
7. **Create PR**: Open a pull request against the main repository

### Pull Request Template

When creating a pull request, include:

- **Title**: Clear, descriptive title
- **Description**: Detailed explanation of changes
- **Related Issues**: Reference any related issues
- **Testing**: Describe how you tested the changes
- **Breaking Changes**: Note any breaking changes

### Code Review

- Address review comments promptly
- Be open to feedback and suggestions
- Update your PR based on review feedback
- Keep the PR focused and avoid scope creep

## 🐛 Reporting Issues

### Bug Reports

When reporting bugs, please include:

- **Description**: Clear description of the issue
- **Steps to Reproduce**: Step-by-step instructions
- **Expected Behavior**: What should happen
- **Actual Behavior**: What actually happens
- **Environment**: OS, compiler version, CMake version
- **Code Sample**: Minimal code to reproduce the issue

### Feature Requests

For feature requests, include:

- **Description**: What feature you'd like to see
- **Use Case**: Why this feature would be useful
- **Implementation Ideas**: Any thoughts on implementation
- **Alternatives**: Alternative approaches considered

## 📄 License

By contributing to TaskFlow, you agree that your contributions will be licensed under the same license as the project (see LICENSE file).

## 🙏 Recognition

Contributors will be acknowledged in the project documentation. We appreciate all contributions, from bug reports to major features!

---

Thank you for contributing to TaskFlow! Your help makes this project better for everyone. 🎉
