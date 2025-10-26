/*
 * 1. 掌握 协程、协程句柄 的基本概念，协程是可以暂停和恢复执行的函数
 * 2. 理解 Task, promise_type, Promise, Awaitable 怎么写
 * 3. 普通函数变为协程函数的三个语法糖：co_return, co_yield, co_await
 * EX:
 * - 修改 await_suspend() { return noop_coroutine(); } 实现 yield
 */

#include "debug.hpp"

#include <coroutine>

using namespace std;

// 一个简单的 Awaiter ，只要没执行完(co_return)，就一直返回自己
struct RepeatAwaiter {
    bool await_ready() const noexcept { return false; }

    coroutine_handle<> await_suspend(coroutine_handle<> coroutine) const noexcept {
        // if (coroutine.done())
        return noop_coroutine();
        // else
        //     return coroutine;
    }
    // 协程恢复处理
    void await_resume() const noexcept {}
};
// Awaiter 一定是 Awaitable, Awaitable 不一定是 Awaiter
// Awaitable: 可以对 co_await() 返回一个 Awaiter
struct RepeatAwaitable {
    RepeatAwaiter operator co_await() { return RepeatAwaiter(); }
};

struct Promise {
    int mValue;
    // 返回包含句柄的任务对象
    coroutine_handle<Promise> get_return_object() { return coroutine_handle<Promise>::from_promise(*this); }
    // 初始挂起，让调用者获得控制权控制何时开始
    auto initial_suspend() noexcept { return suspend_always{}; }
    // 完成其函数体执行后、等待调用者销毁之前的最终行为
    auto final_suspend() noexcept { return suspend_always{}; }
    // yield
    auto yield_value(int ret) {
        mValue = ret;
        return RepeatAwaiter();
    }
    // co_return 处理
    void return_void() { mValue = 0; }
    // 异常处理
    void unhandled_exception() {
        debug(), "unhandled_exception()";
        throw; // <==> std::rethrow_exception(std::current_exception());
    }
};

// 协程返回类型
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

    ~Task() { // RAII
        if (handle) {
            handle.destroy();
        }
    }
};

// 协程函数
Task hello() {
    debug(), "hello 42";
    co_yield 42;
    debug(), "hello 12";
    co_yield 12;
    debug(), "hello 6";
    co_yield 6;
    debug(), "hello() 结束";
    co_return;
}

int main() {
    debug(), "main() 即将调用 协程函数 hello()";
    Task t = hello();
    debug(), "main() 调用完了 协程函数 hello()";
    while (!t.handle.done()) {
        t.handle.resume();
        debug(), "main() 得到返回值为", t.handle.promise().mValue;
    }
    return 0;
}
