#include "debug.hpp"

#include <coroutine>
#include <optional>

using namespace std;

struct RepeatAwaiter { // 不变
    bool await_ready() const noexcept { return false; }

    coroutine_handle<> await_suspend(coroutine_handle<> coroutine) const noexcept {
        if (coroutine.done())
            return noop_coroutine();
        else
            return coroutine;
    }

    void await_resume() const noexcept {}
};

struct PreviousAwaiter { // 不变
    coroutine_handle<> mPrevious = nullptr;

    explicit PreviousAwaiter(coroutine_handle<> h) { mPrevious = h; }

    bool await_ready() const noexcept { return false; }
    coroutine_handle<> await_suspend(coroutine_handle<>) const noexcept {
        if (mPrevious)
            return mPrevious;
        else
            return noop_coroutine();
    }

    void await_resume() const noexcept {}
};

// 对于高性能协程库，placement new通常是更好的选择。// 但我不会写QAQ
template <class T>
struct Promise {
    // union {
    //     T mResult;
    // };
    // P.S.根据C++标准，如果联合体包含非平凡类型的成员，该联合体的默认构造函数会被隐式删除​​
    optional<T> mResult; // 开销比 variant 小
    coroutine_handle<> mPrevious{};
    exception_ptr mExceptionPtr{};

    Promise() noexcept = default;
    Promise(Promise&&) = delete;
    ~Promise() = default;

    coroutine_handle<Promise> get_return_object() { return coroutine_handle<Promise>::from_promise(*this); }
    auto initial_suspend() noexcept { return suspend_always{}; }
    auto final_suspend() noexcept { return PreviousAwaiter(mPrevious); } // 返回 PreviousAwaiter

    auto yield_value(T ret) noexcept {
        mResult = std::move(ret);
        return suspend_always{};
    }

    void return_value(T ret) noexcept { mResult = std::move(ret); }

    void unhandled_exception() noexcept {
        debug(), "unhandled_exception()";
        mExceptionPtr = std::current_exception();
    }

    T result() {
        if (mExceptionPtr) [[unlikely]] {
            rethrow_exception(mExceptionPtr);
        }
        return mResult.value();
    }
};

template <> // void 特化版本
struct Promise<void> {
    coroutine_handle<> mPrevious{};
    exception_ptr mExceptionPtr{};

    Promise() noexcept = default;
    Promise(Promise&&) = delete;
    ~Promise() = default;

    coroutine_handle<Promise> get_return_object() { return coroutine_handle<Promise>::from_promise(*this); }
    auto initial_suspend() noexcept { return suspend_always{}; }
    auto final_suspend() const noexcept { return PreviousAwaiter(mPrevious); } // 返回 PreviousAwaiter

    void return_void() noexcept {}

    void unhandled_exception() noexcept {
        debug(), "unhandled_exception()";
        mExceptionPtr = current_exception();
    }

    void result() const {
        if (mExceptionPtr) [[unlikely]] {
            rethrow_exception(mExceptionPtr);
        }
    }
};

template <class T>
struct Task {
    using promise_type = Promise<T>;

    coroutine_handle<promise_type> handle;

    Task(coroutine_handle<promise_type> coroutine) noexcept : handle(coroutine) {}
    Task(const Task&) = delete; // 禁用拷贝
    Task(Task&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle)
                handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~Task() { // RAII
        if (handle) {
            handle.destroy();
        }
    }

    auto operator co_await() const noexcept { return Awaiter(handle); }

    struct Awaiter {
        coroutine_handle<promise_type> handle;
        explicit Awaiter(coroutine_handle<promise_type> h) { handle = h; }
        bool await_ready() const noexcept { return false; }
        coroutine_handle<promise_type> await_suspend(coroutine_handle<> coroutine) const noexcept {
            handle.promise().mPrevious = coroutine;
            return handle;
        }
        T await_resume() const { return handle.promise().result(); }
    };
};

Task<string> haha() {
    debug(), "haha()";
    // throw runtime_error("fail haha!");
    co_return "aaa\n";
}

Task<double> world() {
    debug(), "world()";
    // throw runtime_error("fail world!");
    co_return 3.14;
}

Task<int> hello() {
    auto ret = co_await haha();
    debug(), "hello()得到haha()结果为", ret;
    int i = (int)co_await world();
    debug(), "hello()得到world()结果为", i;
    co_return i + 1;
}

int main() {
    debug(), "main() 即将调用 协程函数 hello()";
    Task t = hello(); // 其实只创建了task对象，协程状态已初始化，当并没有真正开始执行函数体
    debug(), "main() 调用完了 协程函数 hello()";
    while (!t.handle.done()) {
        t.handle.resume();
        debug(), "main() 得到返回值为", t.handle.promise().result();
    }
    return 0;
}