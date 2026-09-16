// 阶段一测试：会话骨架、命令表、房间与阶段搬运。
//
// 这里检查的都是框架承诺的行为，外加少数几条你在手册里被告知的契约，
// 因此默认就会跑。
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>

#include "game/client_session.h"
#include "game/host_session.h"
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

bool contains(const std::string &text, const std::string &needle) {
    return text.find(needle) != std::string::npos;
}

std::string unique_room(const std::string &suffix) {
    static int counter = 0;
    return "p1-" + std::to_string(::getpid()) + "-" +
           std::to_string(counter++) + "-" + suffix;
}

void test_command_tables() {
    std::istringstream host_in;
    std::ostringstream host_out;
    uno::HostSession host("Host", host_in, host_out);
    CHECK(host.role() == uno::Session::Role::Host);
    CHECK(host.has_lobby_command("start"));
    CHECK(host.has_lobby_command("kick"));
    CHECK(host.has_lobby_command("rename"));
    CHECK(host.has_lobby_command("help"));
    CHECK(host.has_lobby_command("exit"));
    CHECK(host.has_lobby_command("quit"));
    CHECK(!host.has_lobby_command("play"));

    std::istringstream client_in;
    std::ostringstream client_out;
    uno::ClientSession client("Alice", client_in, client_out);
    CHECK(client.role() == uno::Session::Role::Client);
    CHECK(!client.has_lobby_command("start"));
    CHECK(!client.has_lobby_command("kick"));
    CHECK(client.has_lobby_command("rename"));
    CHECK(client.has_lobby_command("help"));
    CHECK(client.has_lobby_command("exit"));
    CHECK(client.has_lobby_command("quit"));

    // host 和 client 共用同一张游戏命令表。
    const char *game_commands[] = {
        "play",      "cards", "draw", "pass", "uno",
        "challenge", "swap",  "help", "exit", "quit",
    };
    for (const char *name : game_commands) {
        CHECK(host.has_game_command(name));
        CHECK(client.has_game_command(name));
    }
}

void test_help_and_usage() {
    std::istringstream in;
    std::ostringstream out;
    uno::HostSession session("Host", in, out);
    session.set_prompt_enabled(false);

    CHECK(session.handle_line("bogus") == uno::Session::Result::Continue);
    CHECK(contains(out.str(), "ERROR msg=\"Unknown command: bogus"));

    out.str("");
    out.clear();
    CHECK(session.handle_line("help") == uno::Session::Result::Continue);
    CHECK(contains(out.str(), "Lobby commands:"));
    CHECK(contains(out.str(), "start"));
    CHECK(contains(out.str(), "kick <player_id|player_name>"));

    // 参数个数由命令表统一把关。
    out.str("");
    out.clear();
    CHECK(session.handle_line("kick") == uno::Session::Result::Continue);
    CHECK(contains(out.str(), "ERROR msg=\"usage: kick"));

    out.str("");
    out.clear();
    CHECK(session.handle_line("rename") == uno::Session::Result::Continue);
    CHECK(contains(out.str(), "ERROR msg=\"usage: rename <name>\""));

    CHECK(session.handle_line("quit") == uno::Session::Result::Quit);
}

void test_seed_contract() {
    std::istringstream in;
    std::ostringstream out;
    uno::HostSession session("Host", in, out);

    CHECK(!session.has_seed());
    CHECK(session.seed() == 0);

    session.set_seed(123456u);
    CHECK(session.has_seed());
    CHECK(session.seed() == 123456u);
}

void test_room_listening() {
    const std::string name = unique_room("room");
    std::string err;
    const std::string path = uno::room_socket_path(name, err);
    CHECK(!path.empty());

    {
        std::istringstream in;
        std::ostringstream out;
        uno::HostSession host("Host", in, out);
        CHECK(host.listen_room(name, err));
        CHECK(host.room_path() == path);

        // 房间重名要明确失败，而不是把正在运行的房间顶掉。
        std::istringstream in2;
        std::ostringstream out2;
        uno::HostSession second("Host2", in2, out2);
        std::string second_err;
        CHECK(!second.listen_room(name, second_err));
        CHECK(contains(second_err, "already in use"));
    }
}

void test_client_cannot_join_missing_room() {
    std::istringstream in;
    std::ostringstream out;
    uno::ClientSession client("Alice", in, out);
    std::string err;
    CHECK(!client.join_room(unique_room("nowhere"), err));
    CHECK(!err.empty());
}

void test_framework_accepts_and_drops_peers() {
    // 会话层（而不只是事件循环）要能把连接变成可用的 PeerId，并在断开后回收。
    const std::string name = unique_room("peers");
    std::string err;
    const std::string path = uno::room_socket_path(name, err);

    std::istringstream in;
    std::ostringstream out;
    uno::HostSession host("Host", in, out);
    host.set_prompt_enabled(false);
    CHECK(host.listen_room(name, err));
    CHECK(host.peers().empty());

    uno::UnixConnection first;
    uno::UnixConnection second;
    CHECK(first.connect(path));
    CHECK(second.connect(path));

    host.pump(0);
    CHECK(host.peers().size() == 2);

    first.close();
    host.pump(0);
    CHECK(host.peers().size() == 1);
}

void test_client_phase_follows_host() {
    // 阶段搬运与游戏命令表都是框架行为：客户端从 START 进入游戏，然后命令表
    // 换成游戏那一张；GAMEOVER 或 exit 再回到大厅。这条路径不依赖 host 的
    // 开局策略，所以无论实现怎么写都必须成立。
    const std::string name = unique_room("phase");
    std::string err;
    const std::string path = uno::room_socket_path(name, err);

    uno::UnixServer fake_host;
    CHECK(fake_host.listen_path(path, err));

    std::istringstream in;
    std::ostringstream out;
    uno::ClientSession client("Alice", in, out);
    client.set_prompt_enabled(false);
    CHECK(client.join_room(name, err));

    std::unique_ptr<uno::Connection> link = fake_host.accept();
    CHECK(link != nullptr);
    if (!link) return;

    CHECK(client.phase() == uno::Session::Phase::Lobby);
    CHECK(link->send("START seed=7"));
    client.pump(0);
    CHECK(client.phase() == uno::Session::Phase::Game);
    CHECK(contains(out.str(), "START seed=7"));

    out.str("");
    out.clear();
    CHECK(client.handle_line("help") == uno::Session::Result::Continue);
    CHECK(contains(out.str(), "Game commands:"));
    CHECK(contains(out.str(), "play <card>[,<card>...] [color]"));
    CHECK(!contains(out.str(), "kick <player_id|player_name>"));

    // challenge 的取值由框架校验，任何实现都必须保留这条行为。
    out.str("");
    out.clear();
    CHECK(client.handle_line("challenge maybe") ==
          uno::Session::Result::Continue);
    CHECK(contains(out.str(), "ERROR msg=\"usage: challenge yes|no\""));

    CHECK(client.handle_line("exit") == uno::Session::Result::LeaveGame);
    CHECK(client.phase() == uno::Session::Phase::Lobby);

    CHECK(link->send("GAMEOVER winner=0"));
    // 回到大厅后，GAMEOVER 同样是幂等的。
    CHECK(link->send("START seed=7"));
    client.pump(0);
    CHECK(client.phase() == uno::Session::Phase::Game);
    CHECK(link->send("GAMEOVER winner=0"));
    client.pump(0);
    CHECK(client.phase() == uno::Session::Phase::Lobby);
}

} // 匿名命名空间

int main() {
    test_command_tables();
    test_help_and_usage();
    test_seed_contract();
    test_room_listening();
    test_client_cannot_join_missing_room();
    test_framework_accepts_and_drops_peers();
    test_client_phase_follows_host();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "phase 1 session tests passed\n";
    return 0;
}
