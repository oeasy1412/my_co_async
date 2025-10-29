#pragma once

#include <cerrno>
#include <system_error>
#if !defined(NDEBUG)
#include <source_location>
#endif

namespace co_async {

#if !defined(NDEBUG)
// 把C语言错误码转换为C++异常
auto checkError(auto res, const std::source_location& loc = std::source_location::current()) {
    if (res == -1) [[unlikely]] {
        throw std::system_error(
            errno, std::system_category(), (std::string)loc.file_name() + ":" + std::to_string(loc.line()));
    }
    return res;
}

auto checkErrorNonBlock(auto res,
                        int blockres = 0,
                        int blockerr = EWOULDBLOCK,
                        const std::source_location& loc = std::source_location::current()) {
    if (res == -1) {
        if (errno != blockerr) [[unlikely]] {
            throw std::system_error(
                errno, std::system_category(), (std::string)loc.file_name() + ":" + std::to_string(loc.line()));
        }
        res = blockres;
    }
    return res;
}
#else
auto checkError(auto res) {
    if (res == -1) [[unlikely]] {
        throw std::system_error(errno, std::system_category());
    }
    return res;
}

auto checkErrorNonBlock(auto res, int blockres = 0, int blockerr = EWOULDBLOCK) {
    if (res == -1) {
        if (errno != blockerr) [[unlikely]] {
            throw std::system_error(errno, std::system_category());
        }
        res = blockres;
    }
    return res;
}
#endif

} // namespace co_async
