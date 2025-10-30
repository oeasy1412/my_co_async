#include "co_async/async_loop.hpp"
#include "co_async/debug.hpp"
#include "co_async/stream.hpp"

using namespace std::literals;

co_async::AsyncLoop loop;
auto ain = co_async::async_stdin();
auto aout = co_async::async_stdout();

co_async::Task<> amain() {
    co_async::FileIStream ain(loop, co_async::async_stdin(true));
    while (true) {
        auto s = co_await ain.getline(": ");
        debug(), s;
        // auto s = co_await ain.getline('\n');
        // debug(), s;
        if (s == "quit")
            break;
    }
    co_return;
}

int main() {
    run_task(loop, amain());
    return 0;
}
