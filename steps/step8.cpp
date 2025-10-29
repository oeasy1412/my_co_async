#include "co_async/debug.hpp"
#include "co_async/epoll_loop.hpp"
#include "co_async/timer_loop.hpp"
#include "co_async/when_all.hpp"
#include "co_async/when_any.hpp"

#include <cerrno>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <system_error>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

co_async::EpollLoop epollLoop;
co_async::TimerLoop timerLoop;

auto file = co_async::AsyncFile(0);

co_async::Task<std::string> reader() {
    auto which = co_await when_any(wait_file_event(epollLoop, file, EPOLLIN), sleep_for(timerLoop, 1s));
    if (which.index() != 0) {
        co_return "timeout: 1秒内没有收到任何输入";
    }
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
        if (auto delay = timerLoop.run()) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(*delay);
            epollLoop.run(ms);
        } else {
            epollLoop.run();
        }
    }

    return 0;
}
