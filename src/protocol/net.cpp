#include "net.h"

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <utility>

namespace uno {

// --- Socket 实现 -----------------------------------------------------------

Socket::~Socket() { close(); }

Socket::Socket(Socket&& o) noexcept
    : m_fd(o.m_fd), m_buffer(std::move(o.m_buffer)), m_eof(o.m_eof) {
    o.m_fd = -1;
    o.m_eof = false;
}

Socket& Socket::operator=(Socket&& o) noexcept {
    if (this != &o) {
        close();
        m_fd = o.m_fd;
        m_buffer = std::move(o.m_buffer);
        m_eof = o.m_eof;
        o.m_fd = -1;
        o.m_eof = false;
    }
    return *this;
}

void Socket::set_non_blocking() {
    if (m_fd < 0) return;
    int fl = fcntl(m_fd, F_GETFL, 0);
    if (fl >= 0) fcntl(m_fd, F_SETFL, fl | O_NONBLOCK);
}

bool Socket::connect(const std::string& host, int port) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[16];
    std::snprintf(portstr, sizeof portstr, "%d", port);

    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0) return false;

    bool ok = false;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        int s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) continue;
        if (::connect(s, p->ai_addr, p->ai_addrlen) == 0) {
            m_fd = s;
            ok = true;
            break;
        }
        ::close(s);
    }
    freeaddrinfo(res);

    if (ok) set_non_blocking();
    return ok;
}

void Socket::adopt(int fd) {
    close();
    m_fd = fd;
    set_non_blocking();
}

bool Socket::connect_unix(const std::string& path) {
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof addr.sun_path) return false;
    std::memcpy(addr.sun_path, path.c_str(), path.size());

    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return false;
    if (::connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(s);
        return false;
    }
    m_fd = s;
    set_non_blocking();
    return true;
}

bool Socket::send_line(const std::string& line) {
    if (m_fd < 0) return false;
    std::string data = line + "\n";
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(m_fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fd_set wf;
                FD_ZERO(&wf);
                FD_SET(m_fd, &wf);
                struct timeval tv{1, 0};
                select(m_fd + 1, nullptr, &wf, nullptr, &tv);
                continue;
            }
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::vector<std::string> Socket::poll_lines() {
    std::vector<std::string> out;
    if (m_fd < 0) return out;

    char buf[4096];
    while (true) {
        ssize_t n = ::recv(m_fd, buf, sizeof buf, 0);
        if (n > 0) {
            m_buffer.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        // n == 0 是对端正常关闭；其它错误（例如对端带着未读数据关闭而触发的
        // ECONNRESET）同样意味着这条连接已经死掉。只有 EAGAIN/EWOULDBLOCK
        // 表示「这一轮读完了」，连接仍然活着。
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) m_eof = true;
        break;
    }

    size_t pos;
    while ((pos = m_buffer.find('\n')) != std::string::npos) {
        std::string line = m_buffer.substr(0, pos);
        m_buffer.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
    }
    return out;
}

void Socket::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_buffer.clear();
    m_eof = false;
}

// --- TcpServer 实现 --------------------------------------------------------

TcpServer::~TcpServer() { close(); }

bool TcpServer::listen(int port, std::string& err) {
    m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0) {
        err = "could not create socket";
        return false;
    }

    int yes = 1;
    setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(m_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr) < 0) {
        err = "could not bind port " + std::to_string(port);
        close();
        return false;
    }
    if (::listen(m_fd, 16) < 0) {
        err = "could not listen on port " + std::to_string(port);
        close();
        return false;
    }

    int fl = fcntl(m_fd, F_GETFL, 0);
    if (fl >= 0) fcntl(m_fd, F_SETFL, fl | O_NONBLOCK);
    return true;
}

std::unique_ptr<Connection> TcpServer::accept() {
    if (m_fd < 0) return nullptr;

    struct sockaddr_in addr{};
    socklen_t len = sizeof addr;
    int c = ::accept(m_fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    if (c < 0) return nullptr;

    int fl = fcntl(c, F_GETFL, 0);
    if (fl >= 0) fcntl(c, F_SETFL, fl | O_NONBLOCK);
    return std::make_unique<TcpConnection>(c);
}

bool TcpServer::listen_path(const std::string&, std::string& err) {
    err = "tcp sockets are addressed by host and port, not by path";
    return false;
}

void TcpServer::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

int TcpServer::port() const {
    if (m_fd < 0) return 0;
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    if (::getsockname(m_fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0)
        return 0;
    return ntohs(addr.sin_port);
}

} // 命名空间 uno
