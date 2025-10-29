#include "co_async/debug.hpp"
#include "co_async/epoll_loop.hpp"
#include "co_async/limit_timeout.hpp"

#include <cstring>
#include <deque>
#include <random>
#include <termios.h>
#include <thread>

// [[gnu::constructor]] 属性让这个函数在 main 函数执行之前被调用
// 用于设置终端为非规范模式（ICANON）和关闭回显（ECHO）
[[gnu::constructor]] static void disable_canon() {
    struct termios tc;
    tcgetattr(STDIN_FILENO, &tc);
    tc.c_lflag &= ~ICANON;
    tc.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tc);
}

using namespace std::chrono_literals;

co_async::EpollLoop epollLoop;
co_async::TimerLoop timerLoop;

// 游戏配置
const int MAP_WIDTH = 20;
const int MAP_HEIGHT = 15;
const int INIT_SNAKE_LENGTH = 3;

// 游戏状态
struct GameState {
    struct Position {
        int x, y;
        bool operator==(const Position& other) const { return x == other.x && y == other.y; }
    };
    std::deque<Position> snake;
    Position food;
    Position lastTail;
    int dx, dy;
    int score;
    bool running;
    bool gameOver;
};

GameState game;

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<> distX(1, MAP_WIDTH - 2);
std::uniform_int_distribution<> distY(1, MAP_HEIGHT - 2);

void generateFood();
void on_draw_init();

// 初始化游戏
void initGame() {
    std::cout << "\x1b[2J\x1b[H\x1b[?25l" << std::flush;
    game.snake.clear();
    game.snake.push_back({MAP_WIDTH / 2, MAP_HEIGHT / 2});
    for (int i = 1; i < INIT_SNAKE_LENGTH; ++i) {
        game.snake.push_back({MAP_WIDTH / 2 - i, MAP_HEIGHT / 2});
    }
    game.lastTail = {0, 0};
    game.dx = 1;
    game.dy = 0;
    game.score = 0;
    game.running = true;
    game.gameOver = false;
    generateFood();
    on_draw_init();
}

// 生成食物
void generateFood() {
    do {
        game.food = {distX(gen), distY(gen)};
    } while (std::find(game.snake.begin(), game.snake.end(), game.food) != game.snake.end());
}

// 检查碰撞
bool checkCollision() {
    const auto& head = game.snake.front();
    // 检查墙壁碰撞
    if (head.x <= 0 || head.x >= MAP_WIDTH - 1 || head.y <= 0 || head.y >= MAP_HEIGHT - 1) {
        return true;
    }
    // 检查自身碰撞（跳过蛇头）
    for (size_t i = 1; i < game.snake.size(); ++i) {
        if (head == game.snake[i]) {
            return true;
        }
    }
    return false;
}

// 输入处理
void on_key(char c) {
    if (c == 'w' && game.dy != 1) { // 防止 180 度掉头
        game.dx = 0;
        game.dy = -1;
    } else if (c == 'a' && game.dx != 1) {
        game.dx = -1;
        game.dy = 0;
    } else if (c == 's' && game.dy != -1) {
        game.dx = 0;
        game.dy = 1;
    } else if (c == 'd' && game.dx != -1) {
        game.dx = 1;
        game.dy = 0;
    } else if (c == 'r' && game.gameOver) {
        initGame();
    } else if (c == 'q') {
        game.running = false;
    }
}

// 更新游戏逻辑
void on_time() {
    if (game.gameOver) {
        return;
    }
    // 计算新蛇头的位置
    GameState::Position newHead = {game.snake.front().x + game.dx, game.snake.front().y + game.dy};
    // 检查碰撞
    if (checkCollision()) {
        game.gameOver = true;
        return;
    }
    // 在头部添加新位置
    game.snake.push_front(newHead);
    // 检查是否吃到食物
    if (newHead == game.food) {
        game.score += 1;
        generateFood();
    } else {
        // 没吃到食物，移除尾部
        game.lastTail = game.snake.back();
        game.snake.pop_back();
    }
}

// 绘制游戏
void on_draw_init() {
    std::string frame;
    frame.reserve((MAP_WIDTH + 3) * (MAP_HEIGHT + 2));
    // 绘制上边界
    frame.append(MAP_WIDTH + 2, '#');
    frame += '\n';
    // 绘制中间区域（墙和空白）
    for (int i = 0; i < MAP_HEIGHT; ++i) {
        frame += '#';
        frame.append(MAP_WIDTH, ' ');
        frame += "#\n";
    }
    // 绘制下边界
    frame.append(MAP_WIDTH + 2, '#');
    frame += '\n';
    if (game.gameOver) {
        frame += "游戏结束! 得分: " + std::to_string(game.score) + " 按 R 重新开始\n";
    } else {
        frame += "得分: " + std::to_string(game.score) + " 控制: WASD, 退出: Q\n";
    }
    std::cout << "\x1b[H\x1b[2J" << frame << std::flush;
}

void set_cursor_pos(int x, int y) { std::cout << "\x1b[" << y + 1 << ";" << x + 1 << "H" << std::flush; }

// 增量draw
void on_draw() {
    // 擦除上一帧的蛇尾
    if (game.lastTail.x != 0) { // 避免游戏开始时擦除 (0,0)
        set_cursor_pos(game.lastTail.x + 1, game.lastTail.y + 1);
        std::cout << ' ' << std::flush; // 在原位置打印空格
    }
    // 绘制食物
    set_cursor_pos(game.food.x + 1, game.food.y + 1);
    std::cout << '$' << std::flush;
    // 绘制新的蛇头
    const auto& head = game.snake.front();
    set_cursor_pos(head.x + 1, head.y + 1);
    std::cout << (game.gameOver ? 'X' : '@') << std::flush;
    // 更新旧的蛇头位置为蛇身
    if (game.snake.size() > 1) {
        const auto& old_head = game.snake[1];
        set_cursor_pos(old_head.x + 1, old_head.y + 1);
        std::cout << '*' << std::flush;
    }
    // 更新底部的分数和状态信息
    set_cursor_pos(0, MAP_HEIGHT + 2);
    if (game.gameOver) {
        std::cout << "游戏结束! 得分: " << game.score << " 按 R 重新开始" << std::flush;
    } else {
        std::cout << "得分: " << game.score << " | 控制: WASD, 退出: Q" << std::flush;
    }
}

co_async::Task<std::string> read_string(co_async::EpollLoop& loop, co_async::AsyncFile& file) {
    co_await wait_file_event(loop, file, EPOLLIN);
    std::string s;
    size_t chunk = 8;
    while (true) {
        std::size_t exist = s.size();
        s.resize(exist + chunk);
        std::span<char> buffer(s.data() + exist, chunk);
        auto len = co_await read_file(loop, file, buffer);
        if (len != chunk) {
            s.resize(exist + len);
            break;
        }
        if (chunk < 65536)
            chunk *= 4;
    }
    co_return s;
}

co_async::Task<> async_main() {
    co_async::AsyncFile file(STDIN_FILENO);
    auto nextTp = std::chrono::system_clock::now();
    initGame();
    while (game.running) {
        auto res = co_await limit_timeout(timerLoop, read_string(epollLoop, file), nextTp);
        if (res) {
            // 如果有输入，处理按键
            for (char c : *res) {
                on_key(c);
            }
            on_draw();
        } else {
            // 超时，更新游戏逻辑
            on_time();
            on_draw();
            nextTp = std::chrono::system_clock::now() + 102ms; // 游戏速度
        }
    }
}

int main() {
    initGame();
    auto t = async_main();
    t.mHandle.resume();
    while (game.running) {
        auto timeout = timerLoop.run();
        auto hasEvent = epollLoop.run(timeout);
        if (!timeout && !hasEvent) {
            break;
        }
    }
    // 游戏结束，恢复终端的设置
    struct termios tc;
    tcgetattr(STDIN_FILENO, &tc);
    tc.c_lflag |= ICANON;
    tc.c_lflag |= ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tc);
    std::cout << "\x1b[?25h" << std::flush;
    return 0;
}