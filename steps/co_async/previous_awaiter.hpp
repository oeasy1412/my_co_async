#pragma once

#include <coroutine>

namespace co_async {

struct PreviousAwaiter {
    std::coroutine_handle<> mPrevious;
    explicit PreviousAwaiter(std::coroutine_handle<> h) { mPrevious = h; }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept {
        if (mPrevious)
            return mPrevious;
        else
            return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

} // namespace co_async
