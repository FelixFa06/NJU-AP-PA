// 拓展规则场景测试（黑盒 + 确定性牌堆）。
//
// 上一份 rules_scenario_test 靠「换 seed 挑局面」，只能覆盖能用 seed 筛出来的
// 情况。这里改用框架提供的「确定性牌堆」接缝（`--deck` /
// `Session::set_deck_override()`，契约见 src/game/session.h）：直接把牌堆摆成想要
// 的样子，于是 +4 质疑、叠加、0-7 换牌、多张出牌、计分、牌堆耗尽、漏喊举报
// 这些需要特定手牌的场景都能确定地构造出来。
//
// 需要 -DUNO_GRADING_TESTS=ON 才会构建。
#include <cctype>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "support/local_room.h"
#include "support/score.h"

namespace {

int checks = 0;
int failures = 0;

#define CHECK(condition)                                                      \
    do {                                                                      \
        ++checks;                                                             \
        if (!(condition)) {                                                   \
            std::cerr << __FILE__ << ':' << __LINE__                          \
                      << ": check failed: " #condition << '\n';               \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

#define CHECK_OR(condition, reason)                                           \
    do {                                                                      \
        ++checks;                                                             \
        if (!(condition)) {                                                   \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << (reason)      \
                      << '\n';                                                \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

// ---------------------------------------------------------------------------
// 牌
// ---------------------------------------------------------------------------

struct Card {
    std::string color;
    std::string face;
};

bool parse_card(const std::string &text, Card &out) {
    if (text == "W") {
        out = {"W", "W"};
        return true;
    }
    if (text == "W4") {
        out = {"W", "W4"};
        return true;
    }
    if (text.size() < 2) return false;
    out.color = text.substr(0, 1);
    out.face = text.substr(1);
    return true;
}

int card_value(const std::string &text) {
    Card card;
    if (!parse_card(text, card)) return 0;
    if (card.color == "W") return 50;
    if (card.face == "SK" || card.face == "RV" || card.face == "D2") return 20;
    return card.face[0] - '0';
}

int hand_value(const std::vector<std::string> &hand) {
    int total = 0;
    for (const std::string &card : hand) total += card_value(card);
    return total;
}

std::vector<std::string> split_cards(const std::string &text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

// 注入用的牌堆：座位 0..n-1 各 7 张，再一张引牌，再是摸牌堆。
std::vector<std::string> build_deck(
    const std::vector<std::vector<std::string>> &hands, const std::string &top,
    const std::vector<std::string> &draw_pile) {
    std::vector<std::string> deck;
    for (const auto &hand : hands)
        for (const std::string &card : hand) deck.push_back(card);
    deck.push_back(top);
    for (const std::string &card : draw_pile) deck.push_back(card);
    return deck;
}

// ---------------------------------------------------------------------------
// 消息
// ---------------------------------------------------------------------------

std::vector<uno::Message> messages_of(const std::string &log,
                                      const std::string &type) {
    std::vector<uno::Message> out;
    std::istringstream in(log);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        uno::Message message = uno::Message::parse(line);
        if (message.type == type) out.push_back(std::move(message));
    }
    return out;
}

std::size_t count_of(const std::string &log, const std::string &type) {
    return messages_of(log, type).size();
}

// ---------------------------------------------------------------------------
// 牌桌
// ---------------------------------------------------------------------------

struct Fixture {
    std::unique_ptr<uno::test::LocalRoom> room;
    int seats = 0;

    std::string log(int seat) const {
        return seat == 0 ? room->host_output() : room->client_output(seat - 1);
    }

    // 广播一定会到达任意一个客户端。
    std::string broadcast_log() const { return log(1); }

    void line(int seat, const std::string &command) {
        if (seat == 0)
            room->host_line(command);
        else
            room->client_line(seat - 1, command);
    }

    // host 执行 cards 打印权威手牌；client 看自己最近收到的私有 HAND。
    std::vector<std::string> hand_of(int seat) {
        if (seat == 0) line(0, "cards");
        const auto hands = messages_of(log(seat), "HAND");
        if (hands.empty()) return {};
        return split_cards(hands.back().get("cards"));
    }

    int current() const {
        const auto turns = messages_of(broadcast_log(), "TURN");
        return turns.empty() ? -1 : turns.back().get_int("player", -1);
    }

    std::string top() const {
        const auto turns = messages_of(broadcast_log(), "TURN");
        return turns.empty() ? std::string() : turns.back().get("top");
    }

    int pending() const {
        const auto turns = messages_of(broadcast_log(), "TURN");
        return turns.empty() ? 0 : turns.back().get_int("pending", 0);
    }

    std::size_t turns() const { return count_of(broadcast_log(), "TURN"); }
};

// 摆好牌堆并开局。开局后校验每个人的手牌与给定的一致 —— 这是「确定性牌堆」
// 契约的基本要求。
bool open_fixture(Fixture &fx, int seats,
                  const std::vector<std::vector<std::string>> &hands,
                  const std::string &top,
                  const std::vector<std::string> &draw_pile = {}) {
    fx.seats = seats;
    fx.room = std::make_unique<uno::test::LocalRoom>(seats - 1, "deck");
    if (!fx.room->ok()) {
        std::cerr << "room setup failed: " << fx.room->error() << '\n';
        return false;
    }
    fx.room->host().set_deck_override(build_deck(hands, top, draw_pile));
    fx.room->host_line("start");

    for (int seat = 0; seat < seats; ++seat) {
        const std::vector<std::string> actual = fx.hand_of(seat);
        if (actual != hands[seat]) {
            std::string shown;
            for (const std::string &card : actual) shown += card + " ";
            std::cerr << "座位 " << seat << " 的手牌与注入的牌堆不符，实际是："
                      << shown << '\n';
            return false;
        }
    }
    return true;
}

// 座位 0 手里是 7 张同色数字牌（刻意避开 0 和 7，它们会触发换牌）。
// 逐个打出，另外两家每轮 draw + pass；
// 打完第 6 张后座位 0 只剩 1 张，此时轮到座位 1，举报窗口是开着的。
void play_down_to_one(Fixture &fx) {
    for (int i = 0; i < 5; ++i) {
        fx.line(0, "play R" + std::to_string(i + 1));
        fx.line(1, "draw");
        fx.line(1, "pass");
        fx.line(2, "draw");
        fx.line(2, "pass");
    }
    fx.line(0, "play R6");
}

// 陪打用的手牌：分值非零（便于核对结算），而且与红色引牌既不撞色也不撞图案。
// 刻意避开 0 和 7，它们会触发换牌规则。
const std::vector<std::string> kJunk = {"B9", "B9", "B9", "B9",
                                        "B9", "B9", "B9"};

// ---------------------------------------------------------------------------
// 场景
// ---------------------------------------------------------------------------

void test_game_over_and_points() {
    const std::vector<std::vector<std::string>> hands = {
        {"R1", "R2", "R3", "R4", "R5", "R6", "R8"},
        kJunk,
        {"G0", "G0", "G0", "G0", "G0", "G0", "G0"},
    };
    Fixture fx;
    if (!open_fixture(fx, 3, hands, "R9", std::vector<std::string>(40, "Y0"))) {
        CHECK_OR(false, "确定性牌堆没有生效：开局手牌与注入的牌堆不一致");
        return;
    }

    play_down_to_one(fx);
    fx.line(1, "draw");
    fx.line(1, "pass");
    fx.line(2, "draw");
    fx.line(2, "pass");
    fx.line(0, "play R8"); // 打出最后一张

    const auto over = messages_of(fx.broadcast_log(), "GAMEOVER");
    CHECK_OR(!over.empty(), "出完最后一张牌应当广播 GAMEOVER");
    if (over.empty()) return;
    CHECK_OR(over.back().get_int("winner", -1) == 0,
             "GAMEOVER 的 winner 应当是出完牌的座位 0");

    // 结算分值：其余玩家剩余手牌分值之和。
    const int expected = hand_value(fx.hand_of(1)) + hand_value(fx.hand_of(2));
    CHECK_OR(over.back().get_int("points", -1) == expected,
             "GAMEOVER 的 points 应当是其余玩家手牌分值之和（期望 " +
                 std::to_string(expected) + "，实际 " +
                 over.back().get("points") + "）");

    // 一局结束之后所有人回到大厅。
    CHECK_OR(fx.room->host().phase() == uno::Session::Phase::Lobby,
             "一局结束后 host 应当回到大厅");
    CHECK_OR(fx.room->client(0).phase() == uno::Session::Phase::Lobby,
             "一局结束后客户端应当回到大厅");
}

void test_deck_exhaustion_reshuffles() {
    Fixture fx;
    if (!open_fixture(fx, 3, {kJunk, kJunk, kJunk}, "B8", {"G1"})) {
        CHECK_OR(false, "确定性牌堆没有生效");
        return;
    }

    // 先打一张，让弃牌堆里除了引牌之外还有内容。
    fx.line(0, "play B9");
    // 座位 1 摸走摸牌堆里最后一张。
    fx.line(1, "draw");
    CHECK_OR(fx.hand_of(1).size() == 8, "座位 1 应当摸到 1 张");
    fx.line(1, "pass");

    // 座位 2 再摸时摸牌堆已空，必须把弃牌堆重新洗成新牌堆。
    fx.line(2, "draw");
    const auto drawn = messages_of(fx.broadcast_log(), "DRAWN");
    CHECK_OR(!drawn.empty() && drawn.back().get_int("player", -1) == 2,
             "牌堆摸空后应当仍能摸到牌");
    CHECK_OR(fx.hand_of(2).size() == 8,
             "牌堆摸空后重新洗牌，座位 2 应当摸到 1 张");
}

void test_uno_report_and_window() {
    const std::vector<std::vector<std::string>> hands = {
        {"R1", "R2", "R3", "R4", "R5", "R6", "R8"}, kJunk, kJunk};

    // 情形一：窗口之内举报成立。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, hands, "R9", std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        play_down_to_one(fx); // 座位 0 只剩 1 张且没喊 UNO
        CHECK_OR(fx.hand_of(0).size() == 1, "座位 0 应当只剩 1 张牌");

        fx.line(1, "uno 0");
        const auto penalties = messages_of(fx.broadcast_log(), "UNOPENALTY");
        CHECK_OR(!penalties.empty(), "漏喊 UNO 被举报应当广播 UNOPENALTY");
        if (!penalties.empty()) {
            CHECK_OR(penalties.back().get_int("player", -1) == 0,
                     "UNOPENALTY 指向的玩家不对");
            CHECK_OR(penalties.back().get_int("count", 0) == 2,
                     "漏喊 UNO 应当罚抽 2 张");
        }
        CHECK_OR(fx.hand_of(0).size() == 3, "被罚抽之后应当变成 3 张");
    }

    // 情形二：下家已经行动，窗口关闭，举报无效。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, hands, "R9", std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        play_down_to_one(fx);
        fx.line(1, "draw");
        fx.line(1, "pass"); // 下家行动完毕，举报窗口关闭

        const std::size_t before = count_of(fx.broadcast_log(), "UNOPENALTY");
        fx.line(2, "uno 0");
        CHECK_OR(count_of(fx.broadcast_log(), "UNOPENALTY") == before,
                 "举报窗口关闭之后不应当再罚抽");
        CHECK_OR(count_of(fx.log(2), "ERROR") > 0,
                 "窗口外举报应当收到 ERROR");
    }
}

void test_uno_call_strictness() {
    const std::vector<std::vector<std::string>> hands = {
        {"R1", "R2", "R3", "R4", "R5", "R6", "R8"}, kJunk, kJunk};

    // 手里正好 1 张：喊牌合法，广播 UNO，不罚抽。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, hands, "R9",
                          std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        play_down_to_one(fx);
        CHECK_OR(fx.hand_of(0).size() == 1, "座位 0 应当只剩 1 张牌");

        const std::size_t penalties_before =
            count_of(fx.broadcast_log(), "UNOPENALTY");
        fx.line(0, "uno");
        const auto calls = messages_of(fx.broadcast_log(), "UNO");
        CHECK_OR(!calls.empty() && calls.back().get_int("player", -1) == 0,
                 "手里只剩 1 张时喊 UNO 应当广播 UNO player=0");
        CHECK_OR(count_of(fx.broadcast_log(), "UNOPENALTY") == penalties_before,
                 "合法喊牌不应当被罚抽");
        CHECK_OR(fx.hand_of(0).size() == 1, "合法喊牌之后手牌数不该变");

        // 喊过之后再举报就不成立了。
        fx.line(1, "uno 0");
        CHECK_OR(count_of(fx.broadcast_log(), "UNOPENALTY") == penalties_before,
                 "喊过 UNO 之后再举报不应当罚抽");
    }

    // 手里还有 2 张：喊牌算错喊，罚抽 2 张。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, hands, "R9",
                          std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        for (int i = 0; i < 5; ++i) {
            fx.line(0, "play R" + std::to_string(i + 1));
            fx.line(1, "draw");
            fx.line(1, "pass");
            fx.line(2, "draw");
            fx.line(2, "pass");
        }
        CHECK_OR(fx.hand_of(0).size() == 2, "座位 0 应当还剩 2 张牌");

        fx.line(0, "uno");
        const auto penalties = messages_of(fx.broadcast_log(), "UNOPENALTY");
        CHECK_OR(!penalties.empty() &&
                     penalties.back().get_int("player", -1) == 0,
                 "手里还有 2 张时喊 UNO 应当被判错喊");
        CHECK_OR(!penalties.empty() && penalties.back().get_int("count", 0) == 2,
                 "错喊 UNO 应当罚抽 2 张");
        CHECK_OR(fx.hand_of(0).size() == 4, "错喊之后应当变成 4 张");
    }
}

void test_zero_swap() {
    const std::vector<std::vector<std::string>> hands = {
        {"R0", "R1", "R2", "R3", "R4", "R5", "R6"},
        {"Y1", "Y2", "Y3", "Y4", "Y5", "Y6", "Y7"},
        {"G1", "G2", "G3", "G4", "G5", "G6", "G7"},
    };
    Fixture fx;
    if (!open_fixture(fx, 3, hands, "R9")) {
        CHECK_OR(false, "确定性牌堆没有生效");
        return;
    }

    fx.line(0, "play R0");

    CHECK_OR(count_of(fx.broadcast_log(), "SWAP") > 0,
             "打出 0 应当广播 SWAP");
    const auto swaps = messages_of(fx.broadcast_log(), "SWAP");
    CHECK_OR(!swaps.empty() && swaps.back().get("all") == "yes",
             "打出 0 应当广播 SWAP all=yes");

    // 座位 0 打出 R0 后手里剩 6 张，其余两家各 7 张；按出牌方向交出：
    // 座位 0 拿座位 2 的牌，座位 1 拿座位 0 的牌，座位 2 拿座位 1 的牌。
    const std::vector<std::string> seat0 = fx.hand_of(0);
    const std::vector<std::string> seat1 = fx.hand_of(1);
    const std::vector<std::string> seat2 = fx.hand_of(2);
    CHECK_OR(seat0 == hands[2], "座位 0 应当拿到座位 2 原来的手牌");
    CHECK_OR(seat2 == hands[1], "座位 2 应当拿到座位 1 原来的手牌");
    std::vector<std::string> seat1_expected = hands[0];
    seat1_expected.erase(seat1_expected.begin()); // R0 已经打出去了
    CHECK_OR(seat1 == seat1_expected, "座位 1 应当拿到座位 0 剩下的手牌");
}

void test_seven_swap() {
    const std::vector<std::vector<std::string>> hands = {
        {"R7", "R1", "R2", "R3", "R4", "R5", "R6"},
        {"Y1", "Y2", "Y3", "Y4", "Y5", "Y6", "Y7"},
        {"G1", "G2", "G3", "G4", "G5", "G6", "G7"},
    };
    Fixture fx;
    if (!open_fixture(fx, 3, hands, "R9")) {
        CHECK_OR(false, "确定性牌堆没有生效");
        return;
    }

    const std::size_t turns_before = fx.turns();
    fx.line(0, "play R7");

    const auto swaps = messages_of(fx.broadcast_log(), "SWAP");
    CHECK_OR(!swaps.empty() && swaps.back().get("waiting") == "yes",
             "打出 7 应当广播 SWAP player=… waiting=yes");
    CHECK_OR(fx.turns() == turns_before,
             "等出牌者指定交换对象之前不应当广播新的 TURN");

    // 别人插队指定对象是不行的。
    fx.line(1, "swap 2");
    CHECK_OR(count_of(fx.log(1), "ERROR") > 0,
             "不是出牌者却发 SWAP 应当收到 ERROR");

    fx.line(0, "swap 2");
    const auto done = messages_of(fx.broadcast_log(), "SWAP");
    CHECK_OR(!done.empty() && done.back().get("with") == "2",
             "换牌完成应当广播 SWAP player=… with=2");
    CHECK_OR(fx.turns() == turns_before + 1, "换牌完成之后应当广播新的 TURN");

    std::vector<std::string> seat0_expected = hands[0];
    seat0_expected.erase(seat0_expected.begin()); // R7 已经打出去了
    CHECK_OR(fx.hand_of(0) == hands[2], "座位 0 应当拿到座位 2 的手牌");
    CHECK_OR(fx.hand_of(2) == seat0_expected, "座位 2 应当拿到座位 0 的手牌");
}

void test_multi_card_plays() {
    // 同色同数字两张。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, {{"R5", "R5", "R6", "R1", "R2", "R3", "R4"},
                                  kJunk, kJunk},
                          "R9", std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        fx.line(0, "play R5,R5");
        const auto played = messages_of(fx.broadcast_log(), "PLAYED");
        CHECK_OR(!played.empty() && played.back().get("cards") == "R5,R5",
                 "同色同数字两张应当能一起出");
        CHECK_OR(fx.hand_of(0).size() == 5, "一次出两张之后应当还剩 5 张");
    }

    // 同数字不同色三张，必须指定颜色。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, {{"R5", "Y5", "G5", "R1", "R2", "R3", "R4"},
                                  kJunk, kJunk},
                          "R9", std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        fx.line(0, "play R5,Y5,G5");
        CHECK_OR(count_of(fx.broadcast_log(), "PLAYED") == 0,
                 "同数不同色三张必须指定颜色");
        fx.line(0, "play R5,Y5,G5 blue");
        const auto played = messages_of(fx.broadcast_log(), "PLAYED");
        CHECK_OR(!played.empty() && played.back().get("color") == "blue",
                 "同数不同色三张带颜色应当被接受");
    }

    // 同色 SK 叠出：出 n 张跳过 n 个玩家。四人局出 2 张，应当轮到座位 3。
    {
        Fixture fx;
        if (!open_fixture(fx, 4,
                          {{"RSK", "RSK", "R1", "R2", "R3", "R4", "R5"},
                           kJunk, kJunk, kJunk},
                          "R9")) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        fx.line(0, "play RSK,RSK");
        CHECK_OR(fx.current() == 3, "两张 SK 应当跳过两个玩家，轮到座位 3");
    }

    // 非法组合：手里只剩两张却想一次出完。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, {{"R5", "R5", "R1", "R2", "R3", "R4", "R6"},
                                  kJunk, kJunk},
                          "R9", std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        // 先出掉 5 张单牌，只剩两张同色同数字，此时不允许一次出完。
        for (const char *card : {"R1", "R2", "R3", "R4", "R6"}) {
            fx.line(0, std::string("play ") + card);
            fx.line(1, "draw");
            fx.line(1, "pass");
            fx.line(2, "draw");
            fx.line(2, "pass");
        }
        const std::size_t played_before = count_of(fx.broadcast_log(), "PLAYED");
        fx.line(0, "play R5,R5");
        CHECK_OR(count_of(fx.broadcast_log(), "PLAYED") == played_before,
                 "最后两张不允许一次出完，应当被拒绝");
    }
}

void test_challenge_branches() {
    // 不质疑：罚抽 4 张并跳过。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, {{"W4", "B0", "B0", "B0", "B0", "B0", "B0"},
                                  kJunk, kJunk},
                          "R9", std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        fx.line(0, "play W4 blue");
        const auto asks = messages_of(fx.broadcast_log(), "CHALLENGE");
        CHECK_OR(!asks.empty() && asks.back().get("ask") == "w4",
                 "打出 W4 之后应当询问是否质疑");
        fx.line(1, "challenge no");
        const auto results = messages_of(fx.broadcast_log(), "CHALLENGE");
        CHECK_OR(!results.empty() && results.back().get("result") == "draw4",
                 "不质疑应当是 result=draw4");
        CHECK_OR(fx.hand_of(1).size() == 11, "不质疑应当罚抽 4 张");
        CHECK_OR(fx.current() == 2, "被罚的人应当被跳过");
    }

    // 质疑不成立（W4 合法）：罚抽 6 张并跳过。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, {{"W4", "B0", "B0", "B0", "B0", "B0", "B0"},
                                  kJunk, kJunk},
                          "R9", std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        fx.line(0, "play W4 blue");
        fx.line(1, "challenge yes");
        const auto results = messages_of(fx.broadcast_log(), "CHALLENGE");
        CHECK_OR(!results.empty() && results.back().get("result") == "draw6",
                 "W4 合法时质疑应当是 result=draw6");
        CHECK_OR(fx.hand_of(1).size() == 13, "质疑不成立应当罚抽 6 张");
        CHECK_OR(fx.current() == 2, "质疑者应当被跳过");
    }

    // 质疑成立（W4 违规）：出牌者罚抽 4 张，质疑者接着出牌。
    {
        Fixture fx;
        if (!open_fixture(fx, 3, {{"W4", "R1", "B0", "B0", "B0", "B0", "B0"},
                                  kJunk, kJunk},
                          "R9", std::vector<std::string>(20, "Y0"))) {
            CHECK_OR(false, "确定性牌堆没有生效");
            return;
        }
        fx.line(0, "play W4 blue"); // 手里还有 R1（红色，与引牌同色）→ 违规
        fx.line(1, "challenge yes");
        const auto results = messages_of(fx.broadcast_log(), "CHALLENGE");
        CHECK_OR(!results.empty() && results.back().get("result") == "illegal",
                 "违规的 W4 被质疑成立时应当是 result=illegal");
        CHECK_OR(fx.hand_of(0).size() == 10, "出牌者应当罚抽 4 张");
        CHECK_OR(fx.current() == 1, "质疑成立之后应当由质疑者出牌");

        // 窗口已经关闭，再质疑就是非法的。
        const std::size_t before = count_of(fx.broadcast_log(), "CHALLENGE");
        fx.line(2, "challenge yes");
        CHECK_OR(count_of(fx.broadcast_log(), "CHALLENGE") == before,
                 "质疑窗口关闭之后不应当再仲裁");
    }
}

void test_stacking() {
    Fixture fx;
    if (!open_fixture(fx, 3,
                      {{"RD2", "R1", "R2", "R3", "R4", "R5", "R6"},
                       {"GD2", "W4", "B0", "B0", "B0", "B0", "B0"},
                       kJunk},
                      "R9", std::vector<std::string>(20, "Y0"))) {
        CHECK_OR(false, "确定性牌堆没有生效");
        return;
    }

    // 座位 0 打出 D2：下家面对 2 张罚抽，并且可以接着叠。
    fx.line(0, "play RD2");
    CHECK_OR(fx.current() == 1, "D2 之后应当轮到下家");
    CHECK_OR(fx.pending() == 2, "D2 之后 TURN 应当带 pending=2");

    // 座位 1 叠一张 D2（叠加不检查颜色）：累计变成 4。
    fx.line(1, "play GD2");
    CHECK_OR(fx.current() == 2, "叠加之后应当轮到再下家");
    CHECK_OR(fx.pending() == 4, "两张 D2 之后 pending 应当变成 4");

    // 座位 2 吃下罚抽：抽 4 张并被跳过。
    fx.line(2, "draw");
    const auto drawn = messages_of(fx.broadcast_log(), "DRAWN");
    CHECK_OR(!drawn.empty() && drawn.back().get_int("count", 0) == 4,
             "吃下罚抽时应当一次抽 4 张");
    CHECK_OR(fx.hand_of(2).size() == 11, "座位 2 应当变成 11 张");
    CHECK_OR(fx.current() == 0, "吃下罚抽之后应当被跳过");
}

} // 匿名命名空间

int main() {
    test_game_over_and_points();
    test_deck_exhaustion_reshuffles();
    test_uno_report_and_window();
    test_uno_call_strictness();
    test_zero_swap();
    test_seven_swap();
    test_multi_card_plays();
    test_challenge_branches();
    test_stacking();

    uno::test::write_score("deck_scenario_test", 3,
                           static_cast<unsigned>(checks),
                           static_cast<unsigned>(failures));
    if (failures != 0) {
        std::cerr << failures << " of " << checks << " check(s) failed\n";
        return 1;
    }
    std::cout << "deck scenario tests passed (" << checks << " checks)\n";
    return 0;
}
