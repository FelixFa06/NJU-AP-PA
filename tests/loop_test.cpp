// 事件循环测试。全部使用框架自带的本地传输，不涉及任何作业实现。
//
// 它验证四件事：
//   * 新连接会被 accept 并触发 on_join；
//   * 到达的字节被切成完整的行（分帧），顺序保持不变；
//   * 处理一条消息的过程中广播/关闭连接是安全的；
//   * 对端断开时触发 on_leave，之后投递到它的消息被安全丢弃。
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "protocol/loop.h"
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

std::string room(const std::string &suffix) {
    std::string err;
    return uno::room_socket_path(
        "loop-" + std::to_string(::getpid()) + "-" + suffix, err);
}

void test_join_framing_and_leave() {
    const std::string path = room("a");
    uno::UnixServer server;
    std::string err;
    if (!server.listen_path(path, err)) {
        std::cerr << "listen failed: " << err << '\n';
        ++failures;
        return;
    }

    uno::EventLoop loop;
    loop.set_server(&server);

    std::vector<uno::PeerId> joins;
    std::vector<uno::PeerId> leaves;
    std::vector<std::string> incoming;

    loop.on_join([&](uno::PeerId who) {
        joins.push_back(who);
        // 在 on_join 里发送：出站是排队的，不会影响正在进行的 accept。
        loop.send(who, "WELCOME id=" + who.str());
    });
    loop.on_line([&](uno::PeerId from, const std::string &line) {
        incoming.push_back(line);
        // 在派发过程中广播：这是框架保证安全的场景。
        loop.send_all("ECHO from=" + from.str() + " line=" + line);
        if (line == "quit") loop.close_peer(from);
    });
    loop.on_leave([&](uno::PeerId who) { leaves.push_back(who); });

    uno::UnixConnection alice;
    uno::UnixConnection bob;
    CHECK(alice.connect(path));
    CHECK(bob.connect(path));

    loop.pump(0);
    CHECK(joins.size() == 2);
    if (joins.size() != 2) return;

    const uno::PeerId alice_id = joins[0];
    const uno::PeerId bob_id = joins[1];

    const std::vector<std::string> alice_welcome = alice.poll();
    CHECK(alice_welcome.size() == 1);
    CHECK(!alice_welcome.empty() &&
          alice_welcome[0] == "WELCOME id=" + alice_id.str());
    const std::vector<std::string> bob_welcome = bob.poll();
    CHECK(bob_welcome.size() == 1);
    CHECK(!bob_welcome.empty() &&
          bob_welcome[0] == "WELCOME id=" + bob_id.str());

    // 一次写入多行：必须被切成两行，并且顺序不变。
    CHECK(alice.send("HELLO one"));
    CHECK(alice.send("HELLO two"));
    loop.pump(0);

    CHECK(incoming.size() == 2);
    CHECK(incoming.size() > 0 && incoming[0] == "HELLO one");
    CHECK(incoming.size() > 1 && incoming[1] == "HELLO two");

    // 广播应该同时到达两个对端。
    const std::vector<std::string> alice_echo = alice.poll();
    const std::vector<std::string> bob_echo = bob.poll();
    CHECK(alice_echo.size() == 2);
    CHECK(bob_echo.size() == 2);

    // 主动关闭：on_leave 触发一次，且之后投递被安全丢弃。
    CHECK(bob.send("quit"));
    loop.pump(0);
    CHECK(leaves.size() == 1);
    CHECK(leaves.size() > 0 && leaves[0] == bob_id);
    CHECK(!loop.send(bob_id, "ANYONE THERE?"));
    CHECK(!loop.has(bob_id));
    CHECK(loop.has(alice_id));

    // 对端自然断开（EOF）同样触发 on_leave。
    alice.close();
    loop.pump(0);
    CHECK(leaves.size() == 2);
    CHECK(leaves.size() > 1 && leaves[1] == alice_id);
}

void test_broadcast_from_leave() {
    // on_leave 里广播也不能破坏容器：这里再开一轮，专门覆盖这条路径。
    const std::string path = room("b");
    uno::UnixServer server;
    std::string err;
    if (!server.listen_path(path, err)) {
        std::cerr << "listen failed: " << err << '\n';
        ++failures;
        return;
    }

    uno::EventLoop loop;
    loop.set_server(&server);

    std::vector<std::string> log;
    loop.on_join([&](uno::PeerId who) {
        loop.send_all("JOINED " + who.str());
    });
    loop.on_line([&](uno::PeerId from, const std::string &line) {
        if (line == "quit") loop.close_peer(from);
    });
    loop.on_leave([&](uno::PeerId who) {
        log.push_back("left " + who.str());
        loop.send_all("LEFT " + who.str());
    });

    uno::UnixConnection a;
    uno::UnixConnection b;
    CHECK(a.connect(path));
    CHECK(b.connect(path));
    loop.pump(0);

    a.poll();
    b.poll();
    CHECK(a.send("quit"));
    loop.pump(0);

    CHECK(log.size() == 1);
    const std::vector<std::string> b_lines = b.poll();
    CHECK(b_lines.size() == 1);
    CHECK(!b_lines.empty() && b_lines[0].rfind("LEFT ", 0) == 0);
}

} // 匿名命名空间

int main() {
    test_join_framing_and_leave();
    test_broadcast_from_leave();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "event loop tests passed\n";
    return 0;
}
