#pragma once

#include "concepts.hpp"
#include "return_previous.hpp"
#include "task.hpp"
#include "uninitialized.hpp"

#include <span>
#include <tuple>
#include <vector>

namespace co_async {

struct WhenAllCtlBlock {
    std::size_t mCount;
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};
};

struct WhenAllAwaiter {
    WhenAllCtlBlock& mControl;
    std::span<ReturnPreviousTask const> mTasks;
    explicit WhenAllAwaiter(WhenAllCtlBlock& ctl, std::span<ReturnPreviousTask const> ts) : mControl(ctl), mTasks(ts) {}
    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const {
        if (mTasks.empty())
            return coroutine;
        mControl.mPrevious = coroutine;
        for (const auto& t : mTasks.subspan(0, mTasks.size() - 1))
            t.mHandle.resume();
        return mTasks.back().mHandle;
    }

    void await_resume() const {
        if (mControl.mException) [[unlikely]] {
            std::rethrow_exception(mControl.mException);
        }
    }
};

template <class T>
ReturnPreviousTask whenAllHelper(auto&& t, WhenAllCtlBlock& ctl, Uninitialized<T>& result) {
    try {
        result.putValue(co_await std::forward<decltype(t)>(t));
    } catch (...) {
        ctl.mException = std::current_exception();
        co_return ctl.mPrevious;
    }
    --ctl.mCount;
    if (ctl.mCount == 0) {
        co_return ctl.mPrevious; // 返回 when_all()
    }
    co_return std::noop_coroutine();
}

template <class = void>
ReturnPreviousTask whenAllHelper(auto&& t, WhenAllCtlBlock& ctl, Uninitialized<void>&) {
    try {
        co_await std::forward<decltype(t)>(t);
    } catch (...) {
        ctl.mException = std::current_exception();
        co_return ctl.mPrevious;
    }
    --ctl.mCount;
    if (ctl.mCount == 0) {
        co_return ctl.mPrevious;
    }
    co_return std::noop_coroutine();
}

template <std::size_t... Is, class... Ts>
Task<std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>> whenAllImpl(std::index_sequence<Is...>, Ts&&... ts) {
    WhenAllCtlBlock ctl{sizeof...(Ts)};
    std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;
    ReturnPreviousTask taskArray[]{whenAllHelper(ts, ctl, std::get<Is>(result))...};
    co_await WhenAllAwaiter(ctl, taskArray);
    co_return std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>(
        std::get<Is>(result).moveValue()...); // 将所有异步任务的结果组装成一个std::tuple后返回
}

template <Awaitable... Ts>
    requires(sizeof...(Ts) != 0)
auto when_all(Ts&&... ts) {
    return whenAllImpl(std::make_index_sequence<sizeof...(Ts)>{}, std::forward<Ts>(ts)...);
}

template <Awaitable T, class Alloc = std::allocator<T>>
Task<std::conditional_t<std::same_as<void, typename AwaitableTraits<T>::RetType>,
                        std::vector<typename AwaitableTraits<T>::RetType, Alloc>,
                        void>>
when_all(const std::vector<T, Alloc>& tasks) {
    WhenAllCtlBlock ctl{tasks.size()};
    Alloc alloc = tasks.get_allocator();
    std::vector<Uninitialized<typename AwaitableTraits<T>::RetType>, Alloc> result(tasks.size(), alloc);
    {
        std::vector<ReturnPreviousTask, Alloc> taskArray(alloc);
        taskArray.reserve(tasks.size());
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            taskArray.push_back(whenAllHelper(tasks[i], ctl, result[i]));
        }
        co_await WhenAllAwaiter(ctl, taskArray);
    }
    if constexpr (!std::same_as<void, typename AwaitableTraits<T>::RetType>) {
        std::vector<typename AwaitableTraits<T>::RetType, Alloc> res(alloc);
        res.reserve(tasks.size());
        for (auto& r : result) {
            res.push_back(r.moveValue());
        }
        co_return res;
    }
}

} // namespace co_async
