#pragma once

#include "epoll_loop.hpp"

#include <termios.h>

namespace co_async {

inline AsyncFile asyncStdFile(int fileNo) {
    AsyncFile file(checkError(dup(fileNo)));
    file.setNonblock();
    return file;
}

inline AsyncFile async_stdin(bool noCanon = false, bool noEcho = false) {
    AsyncFile file = asyncStdFile(STDIN_FILENO);
    if ((noCanon || noEcho) && isatty(file.fileNo())) {
        struct termios tc;
        tcgetattr(file.fileNo(), &tc);
        if (noCanon)
            tc.c_lflag &= ~ICANON;
        if (noEcho)
            tc.c_lflag &= ~ECHO;
        tcsetattr(file.fileNo(), TCSANOW, &tc);
    }
    return file;
}

inline AsyncFile async_stdout() { return asyncStdFile(STDOUT_FILENO); }

inline AsyncFile async_stderr() { return asyncStdFile(STDERR_FILENO); }

inline AsyncFile async_fd(int fileNo) { return asyncStdFile(fileNo); }

} // namespace co_async
