#include "protocol/loop.h"

#include <sys/select.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

namespace uno {

namespace {

// select() 的等待上限。它只影响「无事可做」时循环醒来的频率。
constexpr int kPollTimeoutMs = 200;

} // 匿名命名空间

// --- LineReader -----------------------------------------------------------

std::vector<std::string> LineReader::poll_lines() {
    std::vector<std::string> out;
    if (m_fd < 0) return out;

    // 只读取一次：fd 可能是阻塞的（例如标准输入），所以这里不能循环读取。
    // 调用方只会在 select() 报告 fd 可读时调用本函数。
    char buf[1024];
    const ssize_t n = ::read(m_fd, buf, sizeof buf);
    if (n > 0) {
        m_buffer.append(buf, static_cast<std::size_t>(n));
    } else if (n < 0 && errno == EINTR) {
        return out;
    } else {
        m_eof = true; // n == 0（EOF）或读取错误
    }

    std::size_t pos = 0;
    while ((pos = m_buffer.find('\n')) != std::string::npos) {
        std::string line = m_buffer.substr(0, pos);
        m_buffer.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
    }
    return out;
}

// --- EventLoop ------------------------------------------------------------

void EventLoop::watch_stdin(int fd) {
    m_stdin_fd = fd;
    m_stdin = fd >= 0 ? std::make_unique<LineReader>(fd) : nullptr;
    m_stdin_eof = false;
}

PeerId EventLoop::adopt(std::unique_ptr<Connection> connection) {
    const PeerId id(m_next_peer++);
    if (connection) m_peers.emplace(id, std::move(connection));
    return id;
}

bool EventLoop::has(PeerId id) const {
    return m_peers.find(id) != m_peers.end();
}

std::vector<PeerId> EventLoop::peers() const {
    std::vector<PeerId> out;
    out.reserve(m_peers.size());
    for (const auto &entry : m_peers) out.push_back(entry.first);
    return out;
}

bool EventLoop::send(PeerId id, const std::string &line) {
    if (!has(id)) return false;
    m_outbox[id].push_back(line);
    return true;
}

void EventLoop::send_all(const std::string &line) {
    for (const auto &entry : m_peers) m_outbox[entry.first].push_back(line);
}

void EventLoop::close_peer(PeerId id) { remove_peer(id); }

void EventLoop::remove_peer(PeerId id) {
    auto it = m_peers.find(id);
    if (it == m_peers.end()) return;

    it->second->close();
    m_peers.erase(it);
    m_outbox.erase(id);

    // 注意：回调发生时该 PeerId 已经不在循环里了，此时再 close_peer() 是空操作。
    if (m_on_leave) m_on_leave(id);
}

void EventLoop::read_peer(PeerId id) {
    auto it = m_peers.find(id);
    if (it == m_peers.end()) return;

    const std::vector<std::string> lines = it->second->poll();
    for (const std::string &line : lines) {
        // 上一个回调可能已经把这个对端关掉了。
        if (!has(id)) return;
        if (m_on_line) m_on_line(id, line);
        if (m_stop) return;
    }

    if (has(id) && m_peers.at(id)->eof()) remove_peer(id);
}

void EventLoop::flush_outbox() {
    if (m_outbox.empty()) return;

    std::map<PeerId, std::vector<std::string>> pending;
    pending.swap(m_outbox);

    std::vector<PeerId> failed;
    for (auto &entry : pending) {
        auto it = m_peers.find(entry.first);
        if (it == m_peers.end()) continue;
        for (const std::string &line : entry.second) {
            if (!it->second->send(line)) {
                failed.push_back(entry.first);
                break;
            }
        }
    }
    for (PeerId id : failed) remove_peer(id);
}

bool EventLoop::pump(int timeout_ms) {
    if (m_stop) return false;

    fd_set readable;
    FD_ZERO(&readable);
    int max_fd = -1;
    const auto watch = [&](int fd) {
        if (fd < 0) return;
        FD_SET(fd, &readable);
        if (fd > max_fd) max_fd = fd;
    };

    if (m_server && m_server->valid()) watch(m_server->fd());
    for (const auto &entry : m_peers) watch(entry.second->fd());
    if (m_stdin && !m_stdin_eof) watch(m_stdin_fd);

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    const int ready = ::select(max_fd + 1, &readable, nullptr, nullptr, &timeout);
    if (ready < 0) {
        if (errno == EINTR) return true;
        return false;
    }

    if (ready > 0) {
        // 1) 新的客户端连接。
        if (m_server && m_server->valid() && FD_ISSET(m_server->fd(), &readable)) {
            while (auto incoming = m_server->accept()) {
                const PeerId id = adopt(std::move(incoming));
                if (m_on_join) m_on_join(id);
                if (m_stop) break;
            }
        }

        // 2) 已有连接。先收集就绪列表再派发：回调里可能新增或关闭对端。
        std::vector<PeerId> ready_peers;
        for (const auto &entry : m_peers) {
            if (entry.second->valid() && FD_ISSET(entry.second->fd(), &readable))
                ready_peers.push_back(entry.first);
        }
        for (PeerId id : ready_peers) {
            if (!has(id)) continue;
            read_peer(id);
            if (m_stop) break;
        }

        // 3) 本地键盘输入。
        if (!m_stop && m_stdin && !m_stdin_eof &&
            FD_ISSET(m_stdin_fd, &readable)) {
            const std::vector<std::string> lines = m_stdin->poll_lines();
            for (const std::string &line : lines) {
                if (m_on_stdin) m_on_stdin(line);
                if (m_stop) break;
            }
            if (m_stdin->eof()) {
                m_stdin_eof = true;
                m_stop = true; // 标准输入结束 == 用户要离开
            }
        }
    }

    flush_outbox();
    return !m_stop;
}

int EventLoop::run() {
    while (!m_stop) {
        if (m_on_idle) m_on_idle();
        if (!pump(kPollTimeoutMs)) break;
    }
    return 0;
}

} // namespace uno
