#pragma once

#include <string>
#include <vector>

#include "connection.h"

namespace uno {

// 轻量的行式 POSIX TCP socket 封装（不依赖第三方库）。TcpConnection 和
// 本地传输都使用它（本地传输基于 socket pair，因此完全复用同一条代码路径）。
class Socket {
public:
    Socket() = default;
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& o) noexcept;
    Socket& operator=(Socket&& o) noexcept;

    bool connect(const std::string& host, int port);
    bool connect_unix(const std::string& path); // AF_UNIX 路径
    void adopt(int fd);           // 接管一个（客户端）fd
    bool valid() const { return m_fd >= 0; }
    int  fd() const { return m_fd; }

    bool send_line(const std::string& line);

    // 非阻塞读取：追加当前可用字节，并返回所有完整行。
    std::vector<std::string> poll_lines();
    bool eof() const { return m_eof; }

    void close();

private:
    void set_non_blocking();

    int m_fd = -1;
    std::string m_buffer;
    bool m_eof = false;
};

// Connection 的网络实现（TCP 客户端 socket）。
class TcpConnection : public Connection {
public:
    TcpConnection() = default;
    explicit TcpConnection(int fd) { m_sock.adopt(fd); }

    bool connect(const std::string& host, int port) { return m_sock.connect(host, port); }

    bool send(const std::string& line) override { return m_sock.send_line(line); }
    std::vector<std::string> poll() override { return m_sock.poll_lines(); }
    bool eof() const override { return m_sock.eof(); }
    int  fd() const override { return m_sock.fd(); }
    bool valid() const override { return m_sock.valid(); }
    void close() override { m_sock.close(); }

private:
    Socket m_sock;
};

// Server 的网络实现（TCP 监听器）。
class TcpServer : public Server {
public:
    TcpServer() = default;
    ~TcpServer() override;
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool listen(int port, std::string& err) override;
    bool listen_path(const std::string& path, std::string& err) override;
    std::unique_ptr<Connection> accept() override; // 无就绪连接时返回 nullptr
    int  fd() const override { return m_fd; }
    bool valid() const override { return m_fd >= 0; }
    // 实际绑定的端口。以 0 调用 listen() 时用于取回内核分配的端口。
    int  port() const;
    void close() override;

private:
    int m_fd = -1;
};

} // 命名空间 uno
