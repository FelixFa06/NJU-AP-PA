// 阶段 2 评分冒烟测试。
//
// 卡牌、牌堆和玩家模型的设计被有意保持自由，因此这里只观察固定的命令行与
// 协议接口：它要求阶段 2 的实现用真实的大厅与基础游戏流程消息替换掉框架的
// TODO 引导。
//
// 场景在一个进程内搭起真实的 host + 2 个 client（本地房间 + 真实协议），
// 由测试逐个推进事件循环，因此结果确定、不需要 sleep。
#include <iostream>
#include <string>

#include "support/local_room.h"
#include "support/score.h"

namespace {

int fail(const std::string &message) {
    std::cerr << "phase2 failure: " << message << '\n';
    return 1;
}

} // 匿名命名空间

int run_tests() {
    uno::test::LocalRoom room(2, "p2");
    if (!room.ok()) return fail("could not set up a local room: " + room.error());
    if (room.host().peers().size() != 2)
        return fail("the host did not accept both local clients");

    room.host().set_seed(20240914u);
    if (!room.host().has_seed() || room.host().seed() != 20240914u)
        return fail("session did not preserve the deterministic seed");

    // 两个客户端已经连上，host 应该已经认识了它们。
    if (uno::test::contains(room.host_output(), "TODO"))
        return fail("stage 2 still emits framework TODO guidance");

    room.host_line("start");
    if (room.host().phase() != uno::Session::Phase::Game)
        return fail("'start' must move the host into the game phase");
    if (uno::test::contains(room.host_output(), "TODO"))
        return fail("stage 2 still emits framework TODO guidance");

    // 每个客户端都必须拿到自己的手牌。
    if (!uno::test::contains(room.client_output(0), "HAND") ||
        !uno::test::contains(room.client_output(1), "HAND"))
        return fail("every client must receive its own HAND message");
    if (!uno::test::contains(room.client_output(0), "TURN"))
        return fail("clients must be told whose turn it is");

    // host 自己也是玩家，cards 应该打印权威手牌。
    room.host_line("cards");
    if (!uno::test::contains(room.host_output(), "HAND"))
        return fail("'cards' must show the host's own hand");

    room.host_line("draw");
    if (uno::test::contains(room.host_output(), "TODO"))
        return fail("'draw' is still a placeholder");

    std::cout << "phase 2 basic tests passed\n";
    return 0;
}

int main() {
    const int result = run_tests();
    uno::test::write_score("phase2_basic_test", 2, 1, result == 0 ? 0 : 1);
    return result;
}
