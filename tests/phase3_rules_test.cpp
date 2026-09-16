// 阶段 3 评分冒烟测试。
//
// 本测试只驱动固定的命令行与协议接口，确认拓展规则相关的命令都不再是占位
// 实现。更细的规则正确性由课程评分器在同样的接口上继续检查。
#include <iostream>
#include <string>

#include "support/local_room.h"
#include "support/score.h"

namespace {

int fail(const std::string &message) {
    std::cerr << "phase3 failure: " << message << '\n';
    return 1;
}

} // 匿名命名空间

int run_tests() {
    uno::test::LocalRoom room(2, "p3");
    if (!room.ok()) return fail("could not set up a local room: " + room.error());

    room.host().set_seed(7u);
    if (!room.host().has_seed() || room.host().seed() != 7u)
        return fail("session did not preserve the deterministic seed");

    room.host_line("start");
    if (room.host().phase() != uno::Session::Phase::Game)
        return fail("'start' must move the host into the game phase");
    if (uno::test::contains(room.host_output(), "TODO"))
        return fail("stage 3 still emits framework TODO guidance");

    const char *host_commands[] = {
        "uno",
        "challenge yes",
        "swap 1",
        "draw",
        "pass",
    };
    for (const char *command : host_commands) {
        room.host_line(command);
        if (uno::test::contains(room.host_output(), "TODO"))
            return fail(std::string("'") + command + "' is still a placeholder");
    }

    // 客户端侧同样不该再出现占位提示。
    for (std::size_t i = 0; i < 2; ++i) {
        if (uno::test::contains(room.client_output(i), "TODO"))
            return fail("the client side is still a placeholder");
    }

    std::cout << "phase 3 rule smoke tests passed\n";
    return 0;
}

int main() {
    const int result = run_tests();
    uno::test::write_score("phase3_rules_test", 3, 1, result == 0 ? 0 : 1);
    return result;
}
