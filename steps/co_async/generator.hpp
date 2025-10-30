#pragma once

#include "previous_awaiter.hpp"
#include "uninitialized.hpp"

#include <optional>

namespace co_async {

template <class T>
struct GeneratorPromise {
    Uninitialized<T> mResult;
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mExceptionPtr{};
    bool mFinal = false;
    GeneratorPromise& operator=(GeneratorPromise&&) = delete;

    auto get_return_object() { return std::coroutine_handle<GeneratorPromise>::from_promise(*this); }
    auto initial_suspend() noexcept { return std::suspend_always(); }
    auto final_suspend() noexcept { return PreviousAwaiter(mPrevious); }

    auto yield_value(T&& ret) {
        mResult.putValue(std::move(ret));
        return PreviousAwaiter(mPrevious);
    }
    auto yield_value(const T& ret) {
        mResult.putValue(ret);
        return PreviousAwaiter(mPrevious);
    }

    void return_void() { mFinal = true; }

    void unhandled_exception() noexcept {
        mExceptionPtr = std::current_exception();
        mFinal = true;
    }

    T result() { return mResult.moveValue(); }

    bool final() {
        if (mFinal) {
            if (mExceptionPtr) [[unlikely]] {
                std::rethrow_exception(mExceptionPtr);
            }
        }
        return mFinal;
    }
};

template <class T>
struct GeneratorPromise<T&> {
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mExceptionPtr{};
    T* mResult;
    GeneratorPromise& operator=(GeneratorPromise&&) = delete;

    auto get_return_object() { return std::coroutine_handle<GeneratorPromise>::from_promise(*this); }
    auto initial_suspend() noexcept { return std::suspend_always(); }
    auto final_suspend() noexcept { return PreviousAwaiter(mPrevious); }

    auto yield_value(T& ret) {
        mResult = std::addressof(ret);
        return PreviousAwaiter(mPrevious);
    }

    void return_void() { mResult = nullptr; }

    void unhandled_exception() noexcept {
        mExceptionPtr = std::current_exception();
        mResult = nullptr;
    }

    T& result() { return *mResult; }

    bool final() {
        if (!mResult) {
            if (mExceptionPtr) [[unlikely]] {
                std::rethrow_exception(mExceptionPtr);
            }
            return true;
        }
        return false;
    }
};

template <class T, class P = GeneratorPromise<T>>
struct [[nodiscard]] Generator {
    using promise_type = P;

    Generator(std::coroutine_handle<promise_type> coroutine = nullptr) noexcept : mHandle(coroutine) {}
    Generator(Generator&& that) noexcept : mHandle(that.mHandle) { that.mHandle = nullptr; }
    Generator& operator=(Generator&& that) noexcept { std::swap(mHandle, that.mHandle); }
    ~Generator() {
        if (mHandle)
            mHandle.destroy();
    }

    auto operator co_await() const noexcept { return Awaiter(mHandle); }

    struct Awaiter {
        std::coroutine_handle<promise_type> mHandle;

        bool await_ready() const noexcept { return false; }
        std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
            mHandle.promise().mPrevious = coroutine;
            return mHandle;
        }

        std::optional<T> await_resume() const {
            if (mHandle.promise().final())
                return std::nullopt;
            return mHandle.promise().result();
        }
    };
    operator std::coroutine_handle<promise_type>() const noexcept { return mHandle; }

  private:
    std::coroutine_handle<promise_type> mHandle;
};

#if 0
template <class T, class A, class LoopRef>
struct GeneratorIterator {
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    explicit GeneratorIterator(A awaiter, LoopRef loop) noexcept : mAwaiter(awaiter), mLoop(loop) { ++*this; }

    bool operator!=(std::default_sentinel_t) const noexcept { return mResult.has_value(); }
    bool operator==(std::default_sentinel_t) const noexcept { return !(*this != std::default_sentinel); }
    friend bool operator==(std::default_sentinel_t, GeneratorIterator const& it) noexcept {
        return it == std::default_sentinel;
    }
    friend bool operator!=(std::default_sentinel_t, GeneratorIterator const& it) noexcept {
        return it == std::default_sentinel;
    }

    GeneratorIterator& operator++() {
        mAwaiter.mCoroutine.resume();
        mLoop.run();
        mResult = mAwaiter.await_resume();
        return *this;
    }
    GeneratorIterator operator++(int) {
        auto tmp = *this;
        ++*this;
        return tmp;
    }

    T& operator*() noexcept { return *mResult; }
    T* operator->() noexcept { return mResult.operator->(); }

  private:
    A mAwaiter;
    LoopRef mLoop;
    std::optional<T> mResult;
};

template <class Loop, class T, class P>
auto run_generator(Loop& loop, Generator<T, P> const& g) {
    using Awaiter = typename Generator<T, P>::Awaiter;

    struct GeneratorRange {
        explicit GeneratorRange(Awaiter awaiter, Loop& loop) : mAwaiter(awaiter), mLoop(loop) {
            mAwaiter.await_suspend(std::noop_coroutine());
        }
        auto begin() const noexcept { return GeneratorIterator<T, Awaiter, Loop&>(mAwaiter, mLoop); }
        std::default_sentinel_t end() const noexcept { return {}; }

      private:
        Awaiter mAwaiter;
        Loop& mLoop;
    };

    return GeneratorRange(g.operator co_await(), loop);
};
#endif

} // namespace co_async
