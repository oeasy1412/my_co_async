#include "co_async/debug.hpp"
#include "co_async/epoll_loop.hpp"

#include <cerrno>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

co_async::EpollLoop loop;
auto file = co_async::AsyncFile(0);

co_async::Task<std::string> reader() {
    co_await wait_file_event(loop, file, EPOLLIN);
    std::string s;
    while (true) {
        char c;
        ssize_t len = read(0, &c, 1);
        if (len == -1) {
            if (errno != EWOULDBLOCK) [[unlikely]] {
                throw std::system_error(errno, std::system_category());
            }
            break;
        }
        s.push_back(c);
    }
    co_return s;
}

co_async::Task<void> async_main() {
    while (true) {
        auto s = co_await reader();
        debug(), "读到了", s;
        if (s == "quit\n")
            break;
    }
}

int main() {
    int attr = 1;
    ioctl(0, FIONBIO, &attr);

    auto t = async_main();
    t.mHandle.resume();
    while (!t.mHandle.done()) {
        loop.run();
    }

    return 0;
}
