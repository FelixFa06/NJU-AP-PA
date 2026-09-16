#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "protocol/connection.h"
#include "protocol/peer.h"

namespace uno {

// ---------------------------------------------------------------------------
// LineReader -- 面向「可能阻塞的文件描述符」（典型例子是标准输入）的行缓冲
// 读取器。它用 read() 而不是 iostream，因此可以和 select() 安全地配合使用，
// 不会因为 iostream 的预读缓冲而互相拖死。
//
// 只在 select() 报告该 fd 可读时调用 poll_lines()。
// ---------------------------------------------------------------------------
class LineReader {
public:
    explicit LineReader(int fd) : m_fd(fd) {}

    std::vector<std::string> poll_lines();
    bool eof() const { return m_eof; }

private:
    int m_fd;
    std::string m_buffer;
    bool m_eof = false;
};

// ---------------------------------------------------------------------------
// EventLoop -- 多路复用事件循环。
//
// 这个类由框架完整实现，你不需要阅读或修改它。它的职责只有三件事：
//
//   1. 同时盯着标准输入、监听端点（host）和所有已建立的连接，谁就绪就处理谁；
//   2. 把到达的字节切成完整的一行，再回调对应的处理函数；
//   3. 检测连接断开，给出 on_leave 回调，并保证此刻没有其它回调正在遍历连接
//      容器。
//
// 它刻意不关心任何游戏含义：什么叫「加入」、收到 PLAY 之后该做什么，全都在
// 你的代码里。
//
// 出站消息是排队的：send()/send_all() 只是入队，真正的写入由循环在安全时机
// 统一完成。因此你可以在处理某条消息的过程中随意广播，不会破坏正在被遍历的
// 容器。
// ---------------------------------------------------------------------------
class EventLoop {
public:
    using JoinHandler = std::function<void(PeerId)>;
    using LineHandler = std::function<void(PeerId, const std::string &)>;
    using LeaveHandler = std::function<void(PeerId)>;
    using StdinHandler = std::function<void(const std::string &)>;
    using IdleHandler = std::function<void()>;

    EventLoop() = default;
    EventLoop(const EventLoop &) = delete;
    EventLoop &operator=(const EventLoop &) = delete;

    void on_join(JoinHandler handler) { m_on_join = std::move(handler); }
    void on_line(LineHandler handler) { m_on_line = std::move(handler); }
    void on_leave(LeaveHandler handler) { m_on_leave = std::move(handler); }
    void on_stdin(StdinHandler handler) { m_on_stdin = std::move(handler); }
    void on_idle(IdleHandler handler) { m_on_idle = std::move(handler); }

    // host：把监听端点交给循环，之后新连接会自动被 accept 并分配 PeerId。
    void set_server(Server *server) { m_server = server; }
    // fd < 0 表示不监听标准输入（测试用内存流驱动时应传 -1）。
    void watch_stdin(int fd);

    // 把一个已建立的连接交给循环托管。返回框架分配的身份。
    PeerId adopt(std::unique_ptr<Connection> connection);

    bool has(PeerId id) const;
    std::vector<PeerId> peers() const;

    // 入队一条出站消息。目标已经离开时返回 false。
    bool send(PeerId id, const std::string &line);
    void send_all(const std::string &line);

    // 主动断开某个对端；会触发 on_leave。
    void close_peer(PeerId id);

    void request_stop() { m_stop = true; }
    bool stopped() const { return m_stop; }

    // 推进一次循环。timeout_ms = 0 表示不等待；返回 false 表示应当结束。
    bool pump(int timeout_ms);
    // 阻塞式主循环，直到收到停止请求或标准输入结束。
    int run();

private:
    void flush_outbox();
    void read_peer(PeerId id);
    void remove_peer(PeerId id);

    std::map<PeerId, std::unique_ptr<Connection>> m_peers;
    std::map<PeerId, std::vector<std::string>> m_outbox;
    Server *m_server = nullptr;
    std::unique_ptr<LineReader> m_stdin;
    int m_stdin_fd = -1;
    int m_next_peer = 1;
    bool m_stop = false;
    bool m_stdin_eof = false;

    JoinHandler m_on_join;
    LineHandler m_on_line;
    LeaveHandler m_on_leave;
    StdinHandler m_on_stdin;
    IdleHandler m_on_idle;
};

} // namespace uno
