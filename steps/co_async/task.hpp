#pragma once

#include "debug.hpp"
#include "previous_awaiter.hpp"
#include "uninitialized.hpp"

namespace co_async {

template <class T>
struct Promise {
    Uninitialized<T> mResult; // 使用 Uninitialized 类
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mExceptionPtr{};

    Promise& operator=(Promise&&) = delete;
    auto get_return_object() { return std::coroutine_handle<Promise>::from_promise(*this); }
    auto initial_suspend() noexcept { return std::suspend_always(); }
    auto final_suspend() noexcept { return PreviousAwaiter(mPrevious); }

    template <typename U>
    void return_value(U&& ret) noexcept {
        mResult.putValue(ret);
    }

    void unhandled_exception() noexcept {
        debug(), "unhandled_exception()";
        mExceptionPtr = std::current_exception();
    }

    T result() {
        if (mExceptionPtr) [[unlikely]] {
            std::rethrow_exception(mExceptionPtr);
        }
        return mResult.moveValue();
    }
};

template <>
struct Promise<void> {
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mExceptionPtr{};

    Promise() noexcept = default;
    Promise& operator=(Promise&&) = delete;
    ~Promise() = default;

    std::coroutine_handle<Promise> get_return_object() { return std::coroutine_handle<Promise>::from_promise(*this); }
    auto initial_suspend() noexcept { return std::suspend_always{}; }
    auto final_suspend() const noexcept { return PreviousAwaiter(mPrevious); } // 返回 PreviousAwaiter

    void return_void() noexcept {}

    void unhandled_exception() noexcept {
        debug(), "unhandled_exception()";
        mExceptionPtr = std::current_exception();
    }

    void result() const {
        if (mExceptionPtr) [[unlikely]] {
            rethrow_exception(mExceptionPtr);
        }
    }
};

template <class T = void, class P = Promise<T>>
struct [[nodiscard]] Task {
    using promise_type = P;
    std::coroutine_handle<promise_type> mHandle;

    Task(std::coroutine_handle<promise_type> coroutine = nullptr) noexcept : mHandle(coroutine) {}
    Task(Task&& that) noexcept : mHandle(that.mHandle) { that.mHandle = nullptr; }
    Task& operator=(Task&& that) noexcept { std::swap(mHandle, that.mHandle); }
    ~Task() {
        if (mHandle)
            mHandle.destroy();
    }

    auto operator co_await() const noexcept { return Awaiter(mHandle); }

    struct Awaiter {
        std::coroutine_handle<promise_type> mHandle;
        Awaiter(std::coroutine_handle<promise_type> h) : mHandle(h) {}
        bool await_ready() const noexcept { return false; }
        std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
            mHandle.promise().mPrevious = coroutine;
            return mHandle;
        }
        T await_resume() const { return mHandle.promise().result(); }
    };

    operator std::coroutine_handle<promise_type>() const noexcept { return mHandle; }
};

template <class Loop, class T, class P>
T run_task(Loop& loop, const Task<T, P>& t) {
    auto a = t.operator co_await();
    a.await_suspend(std::noop_coroutine()).resume();
    while (loop.run()) {
    }
    return a.await_resume();
}

template <class T, class P>
void spawn_task(const Task<T, P>& t) {
    auto a = t.operator co_await();
    a.await_suspend(std::noop_coroutine()).resume();
}

} // namespace co_async
