#pragma once

#include "epoll_loop.hpp"
#include "error_handling.hpp"
#include "task.hpp"

#include <fcntl.h>
#include <filesystem>

namespace co_async {

enum class OpenMode : int {
    Read = O_RDONLY,
    Write = O_WRONLY | O_TRUNC | O_CREAT,
    ReadWrite = O_RDWR | O_CREAT,
    Append = O_WRONLY | O_APPEND | O_CREAT,
};

inline Task<AsyncFile> open_fs_file(EpollLoop& loop, std::filesystem::path path, OpenMode mode, mode_t access = 0644) {
    int oflags = (int)mode;
    /* oflags |= O_NONBLOCK; */ // TODO: nonblockfsfilesviaiouring!
    int res = checkError(open(path.c_str(), oflags, access));
    AsyncFile file(res);
    co_return file;
}

} // namespace co_async
