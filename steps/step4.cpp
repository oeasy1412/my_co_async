/*
 * 实现一个​​基于`单线程同步非阻塞事件循环`的协程调度器（Loop），能够自动管理协程的挂起、恢复，并集成了定时任务执行能力​​
 * 其核心在于通过Loop类统一调度就绪协程和定时协程，并利用SleepAwaiter将异步等待抽象为可被co_await的操作，从而用同步写法组织异步逻辑
 * EX: 
 * - More Schedule (TODO)
 */

#include "debug.hpp"

#include <coroutine>
#include <deque>
#include <queue>
#include <thread>

using namespace std;
using namespace chrono_literals;

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

// 对于高性能协程库， placement new 通常是更好的选择
template <class T>
struct Promise { // 不变
    union {
        T mResult;
    };
    // 根据C++标准，如果联合体包含非平凡类型的成员，该联合体的默认构造函数会被隐式删除​​
    coroutine_handle<> mPrevious{};
    exception_ptr mExceptionPtr{};

    // 状态机 用于追踪 Promise 的当前状态来构造和析构 union
    enum class State : uint8_t { Empty, Value, Exception };
    State mState = State::Empty;

    Promise() noexcept {}; // 为非平凡类型的成员指定空构造函数
    Promise(const Promise&) = delete;
    Promise(Promise&&) = delete;
    Promise& operator=(const Promise&) = delete;
    Promise& operator=(Promise&&) = delete;
    ~Promise() {
        if (mState == State::Value) {
            // mResult.~T();
            destroy_at(&mResult);
        }
    }

    coroutine_handle<Promise> get_return_object() { return coroutine_handle<Promise>::from_promise(*this); }
    auto initial_suspend() noexcept { return suspend_always{}; }
    auto final_suspend() noexcept { return PreviousAwaiter(mPrevious); } // 返回 PreviousAwaiter

    auto yield_value(T ret) noexcept {
        // new (&mResult) T(std::move(ret));
        construct_at(&mResult, std::move(ret));
        mState = State::Value;
        return suspend_always{};
    }

    template <typename U>
    void return_value(U&& ret) noexcept {
        construct_at(&mResult, std::forward<U>(ret));
        mState = State::Value;
    }

    void unhandled_exception() noexcept {
        debug(), "unhandled_exception()";
        mExceptionPtr = current_exception();
        mState = State::Exception;
    }

    T& result() & {
        if (mState == State::Exception) [[unlikely]] {
            rethrow_exception(mExceptionPtr);
        }
        return mResult;
    }
    T&& result() && {
        if (mState == State::Exception) [[unlikely]] {
            rethrow_exception(mExceptionPtr);
        }
        return std::move(mResult);
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
    operator coroutine_handle<>() const noexcept { return handle; } // 添加隐式转换函数

    Task(coroutine_handle<promise_type> coroutine) noexcept : handle(coroutine) {}
    Task(const Task&) = delete;
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
    ~Task() {
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

/* --- new thing begin --- */

// 类似于 Schedule, 单线程的同步非阻塞
struct Loop {
    deque<coroutine_handle<>> mReadyQueue;

    // 单例设计模式
    static Loop& GetLoop() {
        static Loop sLoop;
        return sLoop;
    }
    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;
    Loop(Loop&&) = delete; // 偷懒的话可以只写这一个
    Loop& operator=(Loop&&) = delete;
    // 根据C++规则，如果声明了移动构造函数（无论是=default、=delete、还是自定义实现）会阻止编译器​​自动生成​​移动赋值运算符。
    // 同时，声明了移动操作（构造函数或赋值运算符）也会阻止编译器自动生成拷贝操作（拷贝构造函数和拷贝赋值运算符）。但请注意，这是一个​​“弃置而非删除”​​的状态。
    // 也就是说，编译器不为你生成，但如果你尝试使用拷贝操作，编译器会尝试将左值转换为右值，进而匹配到你已删除的移动构造函数，最终呈现出“delete”的假象。

    struct TimerEntry {
        chrono::system_clock::time_point expireTime;
        coroutine_handle<> handle;

        bool operator<(TimerEntry const& other) const noexcept { return expireTime > other.expireTime; }
    };

    priority_queue<TimerEntry> mTimerHeap; // 定时器堆（小顶堆）

    void addTask(coroutine_handle<> handle) { mReadyQueue.push_front(handle); }

    void addTimer(chrono::system_clock::time_point expireTime, coroutine_handle<> handle) {
        mTimerHeap.push({expireTime, handle});
    }

    void runAll() {
        while (!mTimerHeap.empty() || !mReadyQueue.empty()) {
            // 执行所有就绪协程
            while (!mReadyQueue.empty()) {
                auto coroutine = mReadyQueue.front();
                mReadyQueue.pop_front();
                coroutine.resume();
            }
            if (mTimerHeap.empty()) {
                break;
            }
            // 处理定时器任务
            while (!mTimerHeap.empty()) {
                auto nowTime = chrono::system_clock::now();
                auto timer = mTimerHeap.top();
                if (timer.expireTime < nowTime) {
                    mTimerHeap.pop();
                    // timer.handle.resume(); // 避免在处理定时器的循环中执行过长的协程代码
                    mReadyQueue.push_back(timer.handle);
                } else {
                    break;
                }
            }
            if (!mReadyQueue.empty()) {
                continue;
            }
            auto nextExpireTime = mTimerHeap.top().expireTime;
            this_thread::sleep_until(nextExpireTime);
        }
    }

  private:
    Loop() = default;
    ~Loop() = default;
};

struct SleepAwaiter {
    chrono::system_clock::time_point mExpireTime;
    explicit SleepAwaiter(chrono::system_clock::time_point t) { mExpireTime = t; }

    bool await_ready() const noexcept { return false; }
    void await_suspend(coroutine_handle<> coroutine) const { Loop::GetLoop().addTimer(mExpireTime, coroutine); }

    void await_resume() const noexcept {}
};

Task<void> sleep_until(chrono::system_clock::time_point expireTime) {
    co_await SleepAwaiter(expireTime);
    co_return;
}

Task<void> sleep_for(chrono::system_clock::duration duration) {
    co_await SleepAwaiter(chrono::system_clock::now() + duration);
    co_return;
}

Task<int> hello1() {
    debug(), "hello1()开始睡1秒";
    co_await sleep_for(1s); // 1s 等价于 std::chrono::seconds(1)
    debug(), "hello1()睡醒了";
    co_return 1;
}

Task<int> hello2() {
    debug(), "hello2()开始睡2秒";
    co_await sleep_for(2s); // 2s 等价于 std::chrono::seconds(2)
    debug(), "hello2()睡醒了";
    co_return 2;
}

int main() {
    auto t1 = hello1();
    auto t2 = hello2();
    Loop::GetLoop().addTask(t1);
    Loop::GetLoop().addTask(t2);
    Loop::GetLoop().runAll();
    debug(), "main()中得到hello1()返回值:", t1.handle.promise().result();
    debug(), "main()中得到hello2()返回值:", t2.handle.promise().result();
    return 0;
}
