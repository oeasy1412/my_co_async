#include "debug.hpp"

#include <coroutine>
#include <deque>
#include <queue>
#include <span>
#include <thread>
#include <variant>

using namespace std;
using namespace chrono_literals;

template <class T = void>
struct NonVoidHelper {
    using Type = T;
};

template <>
struct NonVoidHelper<void> {
    using Type = NonVoidHelper; // 对于void类型，Type为NonVoidHelper自身，作为占位符
    explicit NonVoidHelper() = default;
};

template <class T>
struct Uninitialized {
    union {
        T mValue;
    };
    enum class State : uint8_t { Empty, Value, Exception };
    State mState = State::Empty;

    Uninitialized() noexcept {} // 为非平凡类型的成员指定空构造函数
    Uninitialized(Uninitialized&&) = delete;
    ~Uninitialized() {
        if (mState == State::Value) {
            // mValue.~T();
            destroy_at(addressof(mValue));
        }
    }

    T moveValue() & {
        if (mState != State::Value) [[unlikely]] {
            throw std::runtime_error("No value to move from Uninitialized object");
        }
        T ret = std::move(mValue);
        // mValue.~T();
        destroy_at(addressof(mValue)); // 移动后立即销毁源对象
        mState = State::Empty;
        return ret;
    }
    T&& moveValue() && {
        if (mState != State::Value) [[unlikely]] {
            throw std::runtime_error("No value to move from Uninitialized object");
        }
        mState = State::Empty; // 直接转移所有权
        return std::move(mValue);
    }

    template <class... Ts>
    void putValue(Ts&&... args) {
        if (mState == State::Value) [[unlikely]] {
            destroy_at(addressof(mValue));
        }
        // 绕过类型 T 可能重载的 operator& （取址运算符），直接获取对象在内存中的真实地址
        // new (&mValue) T(std::forward<Ts>(args)...);
        // construct_at(addressof(mValue), std::forward<Ts>(args)...);
        try {
            construct_at(addressof(mValue), std::forward<Ts>(args)...);
            mState = State::Value;
        } catch (...) {
            mState = State::Empty;
            throw; // 重新抛出异常
        }
    }
};
template <>
struct Uninitialized<void> {
    auto moveValue() { return NonVoidHelper<>{}; }
    void putValue(NonVoidHelper<>) {}
};

template <class T>
struct Uninitialized<T const> : Uninitialized<T> {};

template <class T>
struct Uninitialized<T&> : Uninitialized<reference_wrapper<T>> {};

template <class T>
struct Uninitialized<T&&> : Uninitialized<T> {};

// 使用概念约束
template <class A>
concept Awaiter = requires(A a, coroutine_handle<> h) {
                      { a.await_ready() };
                      { a.await_suspend(h) };
                      { a.await_resume() };
                  };

// Awaitable 是返回 Awaiter 的 “工厂”
template <class A>
concept Awaitable = Awaiter<A> || requires(A a) {
                                      { a.operator co_await() } -> Awaiter;
                                  };

template <class A>
struct AwaitableTraits;
// 概念约束的偏特化版本
template <Awaiter A>
struct AwaitableTraits<A> {
    using RetType = decltype(declval<A>().await_resume());
    using NonVoidRetType = typename NonVoidHelper<RetType>::Type;
};
// 萃取转发 直接继承获得另一个特征类的类型特征提取
template <class A>
    requires(!Awaiter<A> && Awaitable<A>)
struct AwaitableTraits<A> : AwaitableTraits<decltype(declval<A>().operator co_await())> {};

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

template <class T>
struct Promise {
    Uninitialized<T> mResult; // 使用 Uninitialized 类
    coroutine_handle<> mPrevious{};
    exception_ptr mExceptionPtr{};

    Promise& operator=(Promise&&) = delete;
    auto get_return_object() { return coroutine_handle<Promise>::from_promise(*this); }
    auto initial_suspend() noexcept { return suspend_always(); }
    auto final_suspend() noexcept { return PreviousAwaiter(mPrevious); }

    template <typename U>
    void return_value(U&& ret) noexcept {
        mResult.putValue(ret);
    }

    void unhandled_exception() noexcept {
        debug(), "unhandled_exception()";
        mExceptionPtr = current_exception();
    }

    T result() {
        if (mExceptionPtr) [[unlikely]] {
            std::rethrow_exception(mExceptionPtr);
        }
        return mResult.moveValue();
    }
};
template <>
struct Promise<void> { // 不变
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

template <class T = void>
struct Task { // 不变
    using promise_type = Promise<T>;

    coroutine_handle<promise_type> handle;
    operator coroutine_handle<>() const noexcept { return handle; }

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

struct Loop { // 不变
    deque<coroutine_handle<>> mReadyQueue;

    static Loop& GetLoop() {
        static Loop sLoop;
        return sLoop;
    }
    Loop(Loop&&) = delete;

    struct TimerEntry {
        chrono::system_clock::time_point expireTime;
        coroutine_handle<> handle;
        bool operator<(TimerEntry const& that) const noexcept { return expireTime > that.expireTime; }
    };

    priority_queue<TimerEntry> mTimerHeap;

    void addTask(coroutine_handle<> coroutine) { mReadyQueue.push_front(coroutine); }

    void addTimer(chrono::system_clock::time_point expireTime, coroutine_handle<> handle) {
        mTimerHeap.push({expireTime, handle});
    }

    void runAll() {
        while (!mTimerHeap.empty() || !mReadyQueue.empty()) {
            while (!mReadyQueue.empty()) {
                auto coroutine = mReadyQueue.front();
                debug(), "pop";
                mReadyQueue.pop_front();
                coroutine.resume();
            }
            if (mTimerHeap.empty()) {
                break;
            }
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

struct SleepAwaiter { // 不变
    chrono::system_clock::time_point mExpireTime;
    explicit SleepAwaiter(chrono::system_clock::time_point t) { mExpireTime = t; }

    bool await_ready() const noexcept { return false; }
    void await_suspend(coroutine_handle<> coroutine) const { Loop::GetLoop().addTimer(mExpireTime, coroutine); }

    void await_resume() const noexcept {}
};

Task<void> sleep_until(chrono::system_clock::time_point expireTime) { co_await SleepAwaiter(expireTime); }
Task<void> sleep_for(chrono::system_clock::duration duration) {
    co_await SleepAwaiter(chrono::system_clock::now() + duration);
}

/* --- new thing begin --- */

// 获取正在等待它的协程的句柄
// 即 "Who am I": 在 when_all 中，子任务需要知道完成之后应该恢复哪个父协程
struct CurrentCoroutineAwaiter {
    coroutine_handle<> mCurrent;
    bool await_ready() const noexcept { return false; }
    coroutine_handle<> await_suspend(coroutine_handle<> coroutine) noexcept {
        mCurrent = coroutine;
        return coroutine;
    }
    auto await_resume() const noexcept { return mCurrent; }
};

struct ReturnPreviousPromise {
    coroutine_handle<> mPrevious{};
    ReturnPreviousPromise& operator=(ReturnPreviousPromise&&) = delete;
    auto get_return_object() { return coroutine_handle<ReturnPreviousPromise>::from_promise(*this); }
    auto initial_suspend() noexcept { return suspend_always(); }
    auto final_suspend() const noexcept { return PreviousAwaiter(mPrevious); }

    void unhandled_exception() { throw; }

    void return_value(coroutine_handle<> previous) noexcept { mPrevious = previous; }
};

struct ReturnPreviousTask {
    using promise_type = ReturnPreviousPromise;

    coroutine_handle<promise_type> handle;

    ReturnPreviousTask(coroutine_handle<promise_type> coroutine) noexcept : handle(coroutine) {}
    ReturnPreviousTask(ReturnPreviousTask&&) = delete;
    ~ReturnPreviousTask() { handle.destroy(); }
};

/* --- 下面开始实现 when_all() --- */
struct WhenAllCtlBlock {
    size_t mCount;
    coroutine_handle<> mPrevious{};
    exception_ptr mExceptionPtr{};
};

struct WhenAllAwaiter {
    WhenAllCtlBlock& mControl;
    span<ReturnPreviousTask const> mTasks;
    explicit WhenAllAwaiter(WhenAllCtlBlock& ctl, span<ReturnPreviousTask const> ts) : mControl(ctl), mTasks(ts) {}

    bool await_ready() const noexcept { return false; }
    coroutine_handle<> await_suspend(coroutine_handle<> coroutine) const {
        if (mTasks.empty()) {
            return coroutine;
        }
        mControl.mPrevious = coroutine;
        // 为每个传入when_all的原始任务t创建了一个 ReturnPreviousTask协程
        for (const auto& t : mTasks.subspan(1)) {
            Loop::GetLoop().addTask(t.handle);
        }
        return mTasks.front().handle; // 第一个 handle 不用加入Loop了，直接返回更高效
    }

    void await_resume() const {
        if (mControl.mExceptionPtr) [[unlikely]] {
            rethrow_exception(mControl.mExceptionPtr);
        }
    }
};

// 为每个异步任务创建一个包装器协程
template <class T>
ReturnPreviousTask whenAllHelper(const auto& t, WhenAllCtlBlock& ctl, Uninitialized<T>& result) {
    try {
        result.putValue(co_await t);
    } catch (...) {
        ctl.mExceptionPtr = current_exception();
        co_return ctl.mPrevious;
    }
    --ctl.mCount;
    if (ctl.mCount == 0) {
        co_return ctl.mPrevious; // 返回 when_all
    }
    co_return nullptr;
}

template <size_t... Is, class... Ts>
Task<tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>> whenAllImpl(index_sequence<Is...>, Ts&&... ts) {
    WhenAllCtlBlock ctl{sizeof...(Ts)};
    tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;
    ReturnPreviousTask taskArray[]{whenAllHelper(ts, ctl, get<Is>(result))...};
    co_await WhenAllAwaiter(ctl, taskArray);
    co_return tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>(
        get<Is>(result).moveValue()...); // 将所有异步任务的结果组装成一个std::tuple后返回
}

template <Awaitable... Ts>
    requires(sizeof...(Ts) != 0)
auto when_all(Ts&&... ts) {
    return whenAllImpl(make_index_sequence<sizeof...(Ts)>{}, forward<Ts>(ts)...);
}

/* --- 下面开始实现 when_any() --- */
// 竞态条件处理​​：多个数据源查询，使用最先返回的数据
// 当然，when_any(一个请求, 3s)也可以实现类似超时
struct WhenAnyCtlBlock {
    static constexpr size_t kNullIndex = size_t(-1);
    size_t mIndex{kNullIndex};
    coroutine_handle<> mPrevious{};
    exception_ptr mExceptionPtr{};
};

struct WhenAnyAwaiter {
    WhenAnyCtlBlock& mControl;
    span<ReturnPreviousTask const> mTasks;
    explicit WhenAnyAwaiter(WhenAnyCtlBlock& ctl, span<ReturnPreviousTask const> ts) : mControl(ctl), mTasks(ts) {}

    bool await_ready() const noexcept { return false; }
    coroutine_handle<> await_suspend(coroutine_handle<> coroutine) const {
        if (mTasks.empty()) {
            return coroutine;
        }
        mControl.mPrevious = coroutine;
        for (auto const& t : mTasks.subspan(1)) {
            Loop::GetLoop().addTask(t.handle);
        }
        return mTasks.front().handle;
    }

    void await_resume() const {
        if (mControl.mExceptionPtr) [[unlikely]] {
            rethrow_exception(mControl.mExceptionPtr);
        }
    }
};

template <class T>
ReturnPreviousTask whenAnyHelper(const auto& t, WhenAnyCtlBlock& ctl, Uninitialized<T>& result, size_t index) {
    try {
        result.putValue(co_await t);
    } catch (...) {
        ctl.mExceptionPtr = current_exception();
        co_return ctl.mPrevious;
    }
    --ctl.mIndex = index;
    co_return ctl.mPrevious;
}

template <size_t... Is, class... Ts>
Task<variant<typename AwaitableTraits<Ts>::NonVoidRetType...>> whenAnyImpl(index_sequence<Is...>, Ts&&... ts) {
    WhenAnyCtlBlock ctl{};
    tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;
    ReturnPreviousTask taskArray[]{whenAnyHelper(ts, ctl, get<Is>(result), Is)...};
    co_await WhenAnyAwaiter(ctl, taskArray);
    Uninitialized<variant<typename AwaitableTraits<Ts>::NonVoidRetType...>> varResult;
    ((ctl.mIndex == Is && (varResult.putValue(in_place_index<Is>, get<Is>(result).moveValue()), 0)), ...);
    co_return varResult.moveValue();
}

template <Awaitable... Ts>
    requires(sizeof...(Ts) != 0)
auto when_any(Ts&&... ts) {
    return whenAnyImpl(make_index_sequence<sizeof...(Ts)>{}, std::forward<Ts>(ts)...);
}

Task<int> hello1() {
    debug(), "hello1()开始睡1秒";
    co_await sleep_for(1s);
    debug(), "hello1()睡醒了";
    co_return 1;
}

Task<int> hello2() {
    debug(), "hello2()开始睡2秒";
    co_await sleep_for(2s);
    debug(), "hello2()睡醒了";
    co_return 2;
}

Task<int> hello() {
    debug(), "hello()开始等";
    // 1. 同步地等
    /* co_await hello1(); */
    /* co_await hello2(); */
    // 2. when_all()
    // auto v = co_await when_all(hello1(), hello2(), hello2());
    // debug(), "hello()看到全部都睡醒了";
    // 3. when_any()
    auto v = co_await when_any(hello1(), hello2(), hello2());
    debug(), "hello()看到", (int)v.index() + 1, "睡醒了";
    co_return get<0>(v);
}

int main() {
    auto t1 = hello();
    Loop::GetLoop().addTask(t1);
    Loop::GetLoop().runAll();
    debug(), "主函数中得到hello结果:", t1.handle.promise().result();
    return 0;
}
