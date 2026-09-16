#pragma once

#include <memory>
#include <string>
#include <vector>

namespace uno {

// ---------------------------------------------------------------------------
// Endpoint -- 所有可以注册到 select()/poll() 循环中的对象的公共生命周期接口。
// Connection（已建立的连接）和 Server（监听工厂）都是拥有文件描述符且必须
// 可关闭的可选资源，所以 fd()/valid()/close() 这三个方法统一放在这里，避免
// 在两边重复实现。Connection 和 Server 并不互为子类型（Server 通过 accept()
// 产生 Connection），因此这个公共基类被有意保持得尽可能小。
// ---------------------------------------------------------------------------
class Endpoint {
public:
    virtual ~Endpoint() = default;

    // fd() 的存在是为了让调用方可以把该端点与 stdin 一起注册到
    // select()/poll() 循环中。
    virtual int  fd() const = 0;
    virtual bool valid() const = 0;
    virtual void close() = 0;
};

// ---------------------------------------------------------------------------
// Connection -- 统一的行式传输接口。
//
// 本地的 AF_UNIX 传输（默认）和 TCP 传输（彩蛋）都实现这个接口，因此
// host/client 逻辑只需要编写一次，就能同时适用于两种传输方式。每次 send()
// 恰好写入一条协议行（完整的 Message 序列化结果）；poll() 返回自上次调用
// 以来到达的所有完整行。
//
// fd() 的存在是为了让调用方可以把该连接与 stdin 一起注册到
// select()/poll() 循环中。
// ---------------------------------------------------------------------------
class Connection : public Endpoint {
public:
    virtual bool send(const std::string& line) = 0;
    virtual std::vector<std::string> poll() = 0; // 非阻塞，返回完整行
    virtual bool eof() const = 0;
};

// ---------------------------------------------------------------------------
// Server -- 产生客户端 Connection 的监听端点。
// ---------------------------------------------------------------------------
class Server : public Endpoint {
public:
    // 绑定一个 TCP 监听端口（网络传输）。
    virtual bool listen(int port, std::string& err) = 0;
    // 绑定一个 AF_UNIX 路径（本地传输）。不支持路径的传输返回 false。
    virtual bool listen_path(const std::string& path, std::string& err) = 0;
    // 返回下一个待处理的客户端连接；如果没有就绪连接则返回 nullptr。
    virtual std::unique_ptr<Connection> accept() = 0;
};

// 便于使用的别名，让 select() 循环可以统一把 Connection 和 Server 视为
// Endpoint（基于 fd 的就绪轮询只需要这些公共能力）。
using Selectable = Endpoint;

} // 命名空间 uno
