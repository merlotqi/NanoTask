#pragma once

#include <iostream>
#include <taskflow/task_manager.hpp>

// Example task types with different observability levels
struct NoObservationTask {
    static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::none;
    static constexpr bool cancellable = false;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "No observation task executing" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx.success();
    }
};

struct BasicObservationTask {
    static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::basic;
    static constexpr bool cancellable = false;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "Basic observation task executing" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx.success();
    }
};

struct ProgressObservationTask {
    static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::progress;
    static constexpr bool cancellable = false;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "Progress observation task executing" << std::endl;
        ctx.report_progress(0.5f, "Halfway done");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx.success();
    }
};

struct CancellableTask {
    static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::basic;
    static constexpr bool cancellable = true;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "Cancellable task executing" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ctx.success();
    }
};

// Custom task with progress_info trait detection
struct CustomProgressTask {
    // Indicate that this task has custom progress info
    static constexpr bool progress_info = true;

    void operator()(taskflow::TaskCtx& ctx) const {
        std::cout << "Custom progress task executing" << std::endl;

        // Report progress with custom data structure
        struct CustomProgress {
            int current_step;
            int total_steps;
            char phase[32];
            double percentage;
        };

        ctx.report_progress(CustomProgress{10, 100, "starting", 10.0});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        ctx.report_progress(CustomProgress{50, 100, "processing", 50.0});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        ctx.report_progress(CustomProgress{100, 100, "completed", 100.0});
        ctx.success();
    }
};
