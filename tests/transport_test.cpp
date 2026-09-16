// 传输层测试：本地 AF_UNIX 与 TCP（彩蛋）都要能收发。
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

#include "protocol/net.h"
#include "protocol/unix.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            std::cerr << __FILE__ << ':' << __LINE__                          \
                      << ": check failed: " #condition << '\n';               \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

void test_local_transport() {
    std::string err;
    const std::string path = uno::room_socket_path(
        "transport-" + std::to_string(::getpid()), err);

    uno::UnixServer server;
    CHECK(server.listen_path(path, err));

    // 本地传输不接受端口，这一点必须明确失败而不是悄悄成功。
    std::string port_err;
    CHECK(!server.listen(1145, port_err));

    uno::UnixConnection client;
    CHECK(client.connect(path));

    const std::unique_ptr<uno::Connection> accepted = server.accept();
    CHECK(accepted != nullptr);
    if (!accepted) return;

    CHECK(client.send("HELLO"));
    CHECK(client.send("SECOND"));
    const std::vector<std::string> got = accepted->poll();
    CHECK(got.size() == 2);
    CHECK(got.size() > 0 && got[0] == "HELLO");
    CHECK(got.size() > 1 && got[1] == "SECOND");

    CHECK(accepted->send("BACK"));
    const std::vector<std::string> reply = client.poll();
    CHECK(reply.size() == 1);
    CHECK(!reply.empty() && reply[0] == "BACK");
}

void test_tcp_transport() {
    // 彩蛋通道：TCP 走的是同一套 Connection 接口。
    uno::TcpServer server;
    std::string err;
    CHECK(server.listen(0, err)); // 0 == 让内核挑一个空闲端口
    const int port = server.port();
    CHECK(port > 0);

    uno::TcpConnection client;
    CHECK(client.connect("127.0.0.1", port));

    const std::unique_ptr<uno::Connection> accepted = server.accept();
    CHECK(accepted != nullptr);
    if (!accepted) return;

    CHECK(client.send("PING"));
    const std::vector<std::string> got = accepted->poll();
    CHECK(got.size() == 1);
    CHECK(!got.empty() && got[0] == "PING");

    // TCP 不接受路径监听，同样要明确失败。
    std::string path_err;
    CHECK(!server.listen_path("/tmp/should-not-work.sock", path_err));
}

} // 匿名命名空间

int main() {
    test_local_transport();
    test_tcp_transport();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "transport tests passed\n";
    return 0;
}
