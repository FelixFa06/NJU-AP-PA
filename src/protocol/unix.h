#pragma once

#include <string>

#include "protocol/connection.h"
#include "protocol/net.h"

namespace uno {

// ---------------------------------------------------------------------------
// 本地传输 -- 基于 AF_UNIX 套接字的一条连接。
//
// 它就是「同一台机器上的两个进程之间的一条双向管道」，用法、分帧和断开
// 语义与 TCP 完全一致，所以同一份 host/client 代码可以直接换到 TCP 上运行
// （见项目里的网络彩蛋）。
// ---------------------------------------------------------------------------
class UnixConnection : public Connection {
public:
    UnixConnection() = default;
    explicit UnixConnection(int fd) { m_sock.adopt(fd); }

    // 连接到某个房间的 socket 路径。失败返回 false。
    bool connect(const std::string& path) { return m_sock.connect_unix(path); }

    bool send(const std::string& line) override { return m_sock.send_line(line); }
    std::vector<std::string> poll() override { return m_sock.poll_lines(); }
    bool eof() const override { return m_sock.eof(); }
    int  fd() const override { return m_sock.fd(); }
    bool valid() const override { return m_sock.valid(); }
    void close() override { m_sock.close(); }

private:
    Socket m_sock;
};

// ---------------------------------------------------------------------------
// 本地监听端点。bind 一个文件系统路径，客户端 connect 该路径即可加入。
//
// 房间名到路径的推导、残留 socket 文件的清理、权限设置和退出时的清理都由
// 这里负责，你不需要关心。
// ---------------------------------------------------------------------------
class UnixServer : public Server {
public:
    UnixServer() = default;
    ~UnixServer() override;
    UnixServer(const UnixServer&) = delete;
    UnixServer& operator=(const UnixServer&) = delete;

    bool listen(int port, std::string& err) override; // 本地传输不支持端口
    bool listen_path(const std::string& path, std::string& err) override;
    std::unique_ptr<Connection> accept() override; // 无就绪连接时返回 nullptr
    int  fd() const override { return m_fd; }
    bool valid() const override { return m_fd >= 0; }
    void close() override;

    const std::string& path() const { return m_path; }

private:
    int m_fd = -1;
    std::string m_path;
};

// 过滤房间名并推导出 socket 路径：
//   $XDG_RUNTIME_DIR/uno/<room>.sock，退化到 /tmp/uno-<uid>/<room>.sock。
// 名字只保留字母、数字、'-'、'_'，为空则返回空字符串并填写 err。
std::string room_socket_path(const std::string& room, std::string& err);

} // 命名空间 uno
