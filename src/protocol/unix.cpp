#include "protocol/unix.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace uno {

namespace {

bool make_directory(const std::string& path) {
    if (::mkdir(path.c_str(), 0700) == 0) return true;
    return errno == EEXIST;
}

} // 匿名命名空间

std::string room_socket_path(const std::string& room, std::string& err) {
    std::string safe;
    for (char c : room) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (ok) safe += c;
    }
    if (safe.empty()) {
        err = "invalid room name: '" + room + "'";
        return {};
    }

    // 首选每个用户私有的运行时目录；它不存在或不可写时退回 /tmp。
    std::string dir;
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime) {
        const std::string candidate = std::string(runtime) + "/uno";
        if (make_directory(candidate)) dir = candidate;
    }
    if (dir.empty()) {
        dir = "/tmp/uno-" + std::to_string(::getuid());
        if (!make_directory(dir)) {
            err = "could not create " + dir;
            return {};
        }
    }
    return dir + "/" + safe + ".sock";
}

UnixServer::~UnixServer() { close(); }

bool UnixServer::listen(int, std::string& err) {
    err = "local sockets are addressed by room name, not by port";
    return false;
}

bool UnixServer::listen_path(const std::string& path, std::string& err) {
    if (path.empty()) {
        err = "empty socket path";
        return false;
    }

    // 如果已经有进程在同一路径上监听，就不要抢占它。
    {
        UnixConnection probe;
        if (probe.connect(path)) {
            err = "room already in use: " + path;
            return false;
        }
    }

    // 上一次运行异常退出时可能留下同名文件；它只属于当前用户。
    ::unlink(path.c_str());

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) {
        err = "socket path too long: " + path;
        return false;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size());

    m_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_fd < 0) {
        err = "could not create local socket";
        return false;
    }

    if (::bind(m_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr) < 0) {
        err = "could not bind " + path;
        close();
        return false;
    }
    ::chmod(path.c_str(), 0600);

    if (::listen(m_fd, 16) < 0) {
        err = "could not listen on " + path;
        close();
        return false;
    }

    const int flags = ::fcntl(m_fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

    m_path = path;
    return true;
}

std::unique_ptr<Connection> UnixServer::accept() {
    if (m_fd < 0) return nullptr;

    const int client = ::accept(m_fd, nullptr, nullptr);
    if (client < 0) return nullptr;

    const int flags = ::fcntl(client, F_GETFL, 0);
    if (flags >= 0) ::fcntl(client, F_SETFL, flags | O_NONBLOCK);
    return std::make_unique<UnixConnection>(client);
}

void UnixServer::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (!m_path.empty()) {
        ::unlink(m_path.c_str());
        m_path.clear();
    }
}

} // 命名空间 uno
