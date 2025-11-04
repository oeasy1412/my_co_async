#include "co_async/async_loop.hpp"
#include "co_async/debug.hpp"
#include "co_async/filesystem.hpp"
#include "co_async/simple_map.hpp"
#include "co_async/socket.hpp"
#include "co_async/stdio.hpp"
#include "co_async/stream.hpp"

using namespace std::literals;
using namespace co_async;

AsyncLoop loop;

struct HTTPHeaders : SimpleMap<std::string, std::string> {
    using SimpleMap<std::string, std::string>::SimpleMap;
};

struct HTTPRequest {
    std::string method;
    std::string uri;
    HTTPHeaders headers;
    std::string body;

    // 若底层socket的发送缓冲区已满，当前协程会co_await挂起。网络IO等待期间不会阻塞线程。
    Task<> write_into(auto& sock) {
        co_await sock.puts(method);
        co_await sock.putchar(' ');
        co_await sock.puts(uri);
        co_await sock.puts(" HTTP/1.1\r\n"sv);
        for (const auto& [k, v] : headers) {
            co_await sock.puts(k);
            co_await sock.puts(": "sv);
            co_await sock.puts(v);
            co_await sock.puts("\r\n"sv);
        }
        if (body.empty()) {
            co_await sock.puts("\r\n"sv);
        } else {
            co_await sock.puts("content-length: "sv);
            co_await sock.puts(std::to_string(body.size()));
            co_await sock.puts("\r\n"sv);
            co_await sock.puts(body);
        }
    }

    auto repr() const { return std::make_tuple(method, uri, headers, body); }
};

struct HTTPResponse {
    int status;
    HTTPHeaders headers;
    std::string body;

    // 在等待网络数据到达时，协程会co_await挂起，不阻塞事件循环
    // getn()异步读取精确长度的消息体，可以处理TCP粘包/半包的问题
    Task<> read_from(auto& sock) {
        auto line = co_await sock.getline("\r\n"sv);
        if (line.size() <= 9 || line.substr(0, 9) != "HTTP/1.1 "sv) [[unlikely]] {
            throw std::invalid_argument("invalid http response");
        }
        status = std::stoi(line.substr(9));
        while (true) {
            auto line = co_await sock.getline("\r\n"sv);
            if (line.empty()) {
                break;
            }
            auto pos = line.find(':');
            if (pos == line.npos || pos == line.size() - 1 || line[pos + 1] != ' ') [[unlikely]] {
                throw std::invalid_argument("invalid http response");
            }
            auto key = line.substr(0, pos);
            for (auto& c : key) {
                if (c >= 'A' && c <= 'Z') {
                    c += 'a' - 'A';
                }
            }
            headers.insert_or_assign(std::move(key), line.substr(pos + 2));
        }
        if (auto p = headers.at("content-length"sv)) [[likely]] {
            auto len = std::stoi(*p);
            body = co_await sock.getn(len);
        }
    }

    auto repr() const { return std::make_tuple(status, headers, body); }
};

Task<> amain() {
    auto addr = socket_address(ip_address("www.baidu.com"), 80);
    FileStream sock(loop, co_await create_tcp_client(loop, addr));

    HTTPRequest request{
        .method = "GET",
        .uri = "/",
        .headers =
            {
                {"Host", "www.baidu.com"},
                {"User-Agent", "my_co_async-claient/1.0"},
                {"Accept", "*/*"},
                {"Connection", "close"},
            },
    };
    co_await request.write_into(sock);
    co_await sock.flush(); // 因为没法在析构函数执行协程，所以没法RAII flush()，只能手动

    HTTPResponse response;
    co_await response.read_from(sock);

    FileStream file(loop, co_await open_fs_file(loop, "./output.html", OpenMode::Write));
    co_await file.puts(response.body);
    co_await file.flush();
}

int main() {
    run_task(loop, amain());
    return 0;
}
