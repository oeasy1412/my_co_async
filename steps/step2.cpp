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

// 在协程最终挂起时，如果有上一个协程就恢复调用上一个协程，反之则停止
// P.S. co_await
// 结束后的默认行为是​​返回到最原始的调用者（比如main函数）​​，而不是直接返回到上一个调用协程。
struct PreviousAwaiter {
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

struct Promise {
    optional<int> mValue;
    coroutine_handle<> mPrevious{};
    exception_ptr mExceptionPtr{};

    coroutine_handle<Promise> get_return_object() { return coroutine_handle<Promise>::from_promise(*this); }
    auto initial_suspend() noexcept { return suspend_always{}; }
    auto final_suspend() noexcept { return PreviousAwaiter(mPrevious); } // 返回 PreviousAwaiter

    auto yield_value(int ret) {
        mValue = ret;
        return RepeatAwaiter();
    }

    void return_value(int ret) { mValue = ret; }

    void unhandled_exception() noexcept {
        debug(), "unhandled_exception()";
        // throw; 不要throw，要向上传递
        mExceptionPtr = std::current_exception();
    }

    int& result() { // 返回引用，使得异常可以向上传递
        if (mExceptionPtr) [[unlikely]] {
            rethrow_exception(mExceptionPtr);
        }
        return mValue.value(); // 无论如何都不会返回未初始化的 mValue
    }
};

struct Task {
    using promise_type = Promise;

    coroutine_handle<promise_type> handle;

    Task(coroutine_handle<promise_type> coroutine) : handle(coroutine) {}
    Task(const Task&) = delete; // 禁用拷贝（协程句柄不可拷贝）
    Task(Task&& other) noexcept : handle(other.handle) {
        other.handle = nullptr; // 防止双重销毁
    }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle)
                handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    // 使 Task 可被 co_await
    auto operator co_await() const { return Awaiter(handle); }

    ~Task() { // RAII
        if (handle) {
            handle.destroy();
        }
    }

    struct Awaiter {
        coroutine_handle<promise_type> handle;

        explicit Awaiter(coroutine_handle<promise_type> h) { handle = h; }

        bool await_ready() const { return false; }
        coroutine_handle<> await_suspend(coroutine_handle<> coroutine) const {
            handle.promise().mPrevious = coroutine; // 设置前驱关系
            return handle;                          // 恢复被等待的协程
        }

        auto await_resume() const { return handle.promise().result(); } // 返回结果
    };
};

/*
 * 1. 第一次resume()时，hello协程从挂起状态恢复，开始执行 int i = co_await world();
 * 2. 由于Task定义了operator co_await，co_await world()创建一个 Task::Awaiter
 * 3. Awaiter::await_suspend() 将当前hello协程设置为world协程的前驱
 * 4. 控制权转移到world协程
 * 5. world协程恢复执行，执行co_return 41，将值41存入Promise的mValue
 * 6. 执行结束进入final_suspend()，返回PreviousAwaiter(hello协程句柄)
 * 7. 恢复前驱协程hello
 * EX:
 * - 完善异常处理：throw runtime_error()
 * - 不可能既返回正常值又抛出异常，所以可以考虑 mExcepptionPtr mValue 合并为 variant<exception_ptr, int> mResult;
 */
Task world() {
    debug(), "world()";
    // throw runtime_error("fail world!");
    co_return 41;
}

Task hello() {
    int i = co_await world();
    debug(), "hello()得到world()结果为", i;
    co_return i + 1;
}

int main() {
    debug(), "main() 即将调用 协程函数 hello()";
    Task t = hello(); // 其实只创建了task对象，协程状态已初始化，当并没有真正开始执行函数体
    debug(), "main() 调用完了 协程函数 hello()";
    while (!t.handle.done()) {
        t.handle.resume(); // 第一次resume()
        debug(), "main() 得到返回值为", t.handle.promise().result();
    }
    return 0;
}