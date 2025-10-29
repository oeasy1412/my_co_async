## my_co_async
本项目旨在基于 ​​C++20 协程​​ 构建一个高性能的异步I/O协程库，将传统的回调式异步编程转化为直观的同步式代码风格，同时保持极高的性能。该库提供了基于 `epoll` 的事件循环机制、定时器管理以及协程原语，简化了异步编程的复杂性。

### 核心特性
- 基于 C++20 无栈协程(stackless coroutines)，零开销抽象
- 同步式异步编程，将异步回调代码转化为同步式编写风格，避免"回调地狱"
- 协程切换开销在纳秒级别，远低于线程切换
- 基于 Linux epoll 的高效 I/O 多路复用
- 集成定时器循环，支持精确的时间控制
- 自动批量处理就绪事件，提高吞吐量

### 前置知识
- step 1-5:
    - C++ OOP, RAII, 模板编程, 移动语义和完美转发, 异常处理 
    - C++ 常见数据结构和类型包装器（如：union, optional, variant）
    - C++20 概念约束 
    - C++20 协程 [coroutine教程](https://blog.alinche.dpdns.org/posts/os/coroutine/)
- step 7-9
    - Linux epoll [epoll教程](https://blog.alinche.dpdns.org/posts/os/io/epoll/)
- step 10+
    - socket

### 环境需求
- 任意 Linux 发行版，需要支持 g++ -std=c++20，如果使用 clangd 其版本至少需满足 >= 15 