#pragma once

#include "non_void_helper.hpp"

#include <memory>

namespace co_async {

template <class T>
struct Uninitialized {
    union {
        T mValue;
    };
    enum class State : uint8_t { Empty, Value, Exception };
    State mState = State::Empty;

    Uninitialized() noexcept {} // 为非平凡类型的成员指定空构造函数
    Uninitialized& operator=(Uninitialized&&) = delete;
    ~Uninitialized() noexcept {
        if (mState == State::Value) {
            // mValue.~T();
            std::destroy_at(std::addressof(mValue));
        }
    }

    T moveValue() & {
        if (mState != State::Value) [[unlikely]] {
            throw std::runtime_error("No value to move from Uninitialized object");
        }
        T ret = std::move(mValue);
        // mValue.~T();
        std::destroy_at(std::addressof(mValue)); // 移动后立即销毁源对象
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
            std::destroy_at(std::addressof(mValue));
        }
        // 绕过类型 T 可能重载的 operator& （取址运算符），直接获取对象在内存中的真实地址
        // new (&mValue) T(std::forward<Ts>(args)...);
        // std::construct_at(std::addressof(mValue), std::forward<Ts>(args)...);
        try {
            std::construct_at(std::addressof(mValue), std::forward<Ts>(args)...);
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
struct Uninitialized<T&> : Uninitialized<std::reference_wrapper<T>> {};

template <class T>
struct Uninitialized<T&&> : Uninitialized<T> {};

} // namespace co_async
