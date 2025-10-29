#pragma once

#include <utility>

namespace co_async {

template <class T = void>
struct NonVoidHelper {
    using Type = T;
};

template <>
struct NonVoidHelper<void> {
    using Type = NonVoidHelper; // 对于void类型，Type为NonVoidHelper自身，作为占位符

    explicit NonVoidHelper() = default;
    template <class T>
    constexpr friend T&& operator,(T&& t, NonVoidHelper) {
        return std::forward<T>(t);
    }

    char const* repr() const noexcept { return "NonVoidHelper"; }
};

} // namespace co_async
