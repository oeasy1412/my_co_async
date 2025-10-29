#pragma once

#include "task.hpp"

namespace co_async {

struct ReturnPreviousPromise {
    std::coroutine_handle<> mPrevious{};
    ReturnPreviousPromise& operator=(ReturnPreviousPromise&&) = delete;
    auto get_return_object() { return std::coroutine_handle<ReturnPreviousPromise>::from_promise(*this); }
    auto initial_suspend() noexcept { return std::suspend_always(); }
    auto final_suspend() const noexcept { return PreviousAwaiter(mPrevious); }

    void unhandled_exception() { throw; }

    void return_value(std::coroutine_handle<> previous) noexcept { mPrevious = previous; }
};

struct [[nodiscard]] ReturnPreviousTask {
    using promise_type = ReturnPreviousPromise;

    std::coroutine_handle<promise_type> mHandle;

    ReturnPreviousTask(std::coroutine_handle<promise_type> coroutine) noexcept : mHandle(coroutine) {}
    ReturnPreviousTask& operator=(ReturnPreviousTask&&) = delete;
    ~ReturnPreviousTask() {
        if (mHandle)
            mHandle.destroy();
    }
};

} // namespace co_async
