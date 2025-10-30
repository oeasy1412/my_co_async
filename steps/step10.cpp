#include "co_async/async_loop.hpp"
#include "co_async/debug.hpp"
#include "co_async/socket.hpp"

#include <termios.h>

[[gnu::constructor]] static void disable_canon() {
    struct termios tc;
    tcgetattr(STDIN_FILENO, &tc);
    tc.c_lflag &= ~ICANON;
    tc.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tc);
}

using namespace std::literals;

co_async::AsyncLoop loop;

co_async::Task<> amain() {
    auto sock = co_await create_tcp_client(loop, co_async::socket_address(co_async::ip_address("httpbin.org"), 80));
    std::string http_request =
        "GET /get?param1=value1&param2=value2 HTTP/1.1\r\n"
        "Host: httpbin.org\r\n"
        "User-Agent: my_co_async-client/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";
    co_await write_file(loop, sock, http_request);
    char buf[4096];
    auto len = co_await read_file(loop, sock, buf);
    std::string_view response(buf, len);
    size_t header_end = response.find("\r\n\r\n");
    if (header_end != std::string_view::npos) {
        std::string_view headers = response.substr(0, header_end);
        std::string_view body = response.substr(header_end + 4);
        
        std::cout << "=== 响应头 ===\n" << headers << "\n\n";
        std::cout << "=== 响应体 ===\n" << body << std::endl;
    } else {
        std::cout << response;
    }
    // // 解析响应，检查是否为 301/302 重定向
    // if (response.find("HTTP/1.1 301") != std::string_view::npos ||
    //     response.find("HTTP/1.1 302") != std::string_view::npos) {
    //     // 提取 location 头中的新 URL
    //     size_t loc_start = response.find("location: ");
    //     if (loc_start != std::string_view::npos) {
    //         loc_start += 10; // "location: " 的长度
    //         size_t loc_end = response.find("\r\n", loc_start);
    //         std::string_view new_location = response.substr(loc_start, loc_end - loc_start);
    //         std::cout << "重定向到: " << new_location << std::endl;
    //     }
    // }
    co_return;
}

int main() {
    run_task(loop, amain());
    return 0;
}
