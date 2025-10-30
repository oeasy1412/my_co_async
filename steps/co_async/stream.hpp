#pragma once

#include "epoll_loop.hpp"
#include "stdio.hpp"
#include "stream_base.hpp"

namespace co_async {

struct FileBuf {
    EpollLoop* mLoop;
    AsyncFile mFile;

    FileBuf() noexcept : mLoop(nullptr) {}
    FileBuf(EpollLoop& loop, AsyncFile&& file) : mLoop(&loop), mFile(std::move(file)) {}

    Task<std::size_t> read(std::span<char> buffer) { return read_file(*mLoop, mFile, buffer); }
    Task<std::size_t> write(std::span<const char> buffer) { return write_file(*mLoop, mFile, buffer); }
};

using FileIStream = IStream<FileBuf>;
using FileOStream = OStream<FileBuf>;
using FileStream = IOStream<FileBuf>;

struct StdioBuf {
    EpollLoop* mLoop;
    AsyncFile mFileIn;
    AsyncFile mFileOut;

    StdioBuf() noexcept : mLoop(nullptr) {}
    StdioBuf(EpollLoop& loop) : mLoop(&loop), mFileIn(async_stdin(true)), mFileOut(async_stdout()) {}
    StdioBuf(EpollLoop& loop, AsyncFile&& fileIn, AsyncFile&& fileOut)
        : mLoop(&loop),
          mFileIn(std::move(fileIn)),
          mFileOut(std::move(fileOut)) {}

    Task<std::size_t> read(std::span<char> buffer) { return read_file(*mLoop, mFileIn, buffer); }
    Task<std::size_t> write(std::span<const char> buffer) { return write_file(*mLoop, mFileOut, buffer); }
};

using StdioStream = IOStream<StdioBuf>;

struct StringReadBuf {
    std::string_view mStringView;
    std::size_t mPosition{};

    StringReadBuf() noexcept {}
    StringReadBuf(std::string_view strView) : mStringView(strView) {}

    Task<std::size_t> read(std::span<char> buffer) {
        std::size_t size = std::min(buffer.size(), mStringView.size() - mPosition);
        std::copy_n(mStringView.begin() + mPosition, size, buffer.begin());
        mPosition += size;
        co_return size;
    }
};

struct StringWriteBuf {
    std::string mString;

    StringWriteBuf() noexcept {}
    StringWriteBuf(std::string&& str) : mString(std::move(str)) {}

    Task<std::size_t> write(std::span<const char> buffer) {
        mString.append(buffer.data(), buffer.size());
        co_return buffer.size();
    }
};

using StringIStream = IStream<StringReadBuf>;
using StringOStream = OStream<StringWriteBuf>;

} // namespace co_async
