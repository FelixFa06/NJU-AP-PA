// 规则场景测试（黑盒）。
//
// 它只看两样东西：你从命令行收到了什么、你往协议上发了什么。内部设计完全
// 自由，但外部行为必须符合 docs/protocol.typ 的「可观察语义」。
//
// 构造局面的办法是「换 seed 直到局面合适」：牌堆顺序是规格固定的（见
// docs/manual.typ 的「牌堆与确定性洗牌」一节），所以每个 seed 的开局都是确定的。测试先开局、读出
// 每个人的手牌与引牌，再挑一个能满足断言前提的 seed 出来。
//
// 需要 -DUNO_GRADING_TESTS=ON 才会构建。
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
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

// 条件不成立时给出可读的原因，而不是只打印表达式。
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
    std::string color; // R / Y / G / B ，万能牌为 W
    std::string face;  // 0-9 / SK / RV / D2 / W / W4
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
    if (out.color != "R" && out.color != "Y" && out.color != "G" &&
        out.color != "B")
        return false;
    if (out.face == "SK" || out.face == "RV" || out.face == "D2") return true;
    return out.face.size() == 1 &&
           std::isdigit(static_cast<unsigned char>(out.face[0]));
}

bool is_wild(const Card &card) { return card.color == "W"; }

bool is_number(const Card &card) {
    return !is_wild(card) && card.face.size() == 1 &&
           std::isdigit(static_cast<unsigned char>(card.face[0]));
}

// 这张牌能不能压在 top 上。top 是万能牌时按 active_color 判断。
bool playable_on(const Card &card, const Card &top,
                 const std::string &active_color) {
    if (is_wild(card)) return true;
    const std::string color = is_wild(top) ? active_color : top.color;
    if (card.color == color) return true;
    return card.face == top.face;
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

std::map<std::string, int> deck_limits() {
    std::map<std::string, int> limits;
    for (const char *color : {"R", "Y", "G", "B"}) {
        limits[std::string(color) + "0"] = 1;
        for (int i = 1; i <= 9; ++i)
            limits[std::string(color) + std::to_string(i)] = 2;
        for (const char *face : {"SK", "RV", "D2"})
            limits[std::string(color) + face] = 2;
    }
    limits["W"] = 4;
    limits["W4"] = 4;
    return limits;
}

// ---------------------------------------------------------------------------
// 日志解析
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
// 局面
// ---------------------------------------------------------------------------

struct Table {
    std::unique_ptr<uno::test::LocalRoom> room;
    int seed = 0;
    int seats = 0;
    std::vector<std::vector<std::string>> hands; // 每个座位的手牌
    std::string top;                             // 引牌
    int current = -1;                            // 当前轮到谁

    // 某个端自己的日志。host 只会打印本地信息与自己收到的消息，收不到自己
    // 发出的广播，所以「看广播」要用 broadcast_log()。
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

    // 读某个座位现在的手牌。
    //
    // host 没有「收到 HAND」这回事，所以要它执行 cards 打印权威手牌；client
    // 直接看它最近收到的那条私有 HAND 即可 —— 这样客户端的日志里只会留下
    // 真正收到的消息，隐私检查才能数得准。
    std::vector<std::string> hand_of(int seat) {
        if (seat == 0) line(0, "cards");
        const auto hands = messages_of(log(seat), "HAND");
        if (hands.empty()) return {};
        return split_cards(hands.back().get("cards"));
    }

    int next_seat(int from, int step = 1) const {
        return ((from + step) % seats + seats) % seats;
    }
};

// 读出开局后的完整局面。失败说明实现没有按协议发出必需的消息。
bool read_table(Table &table) {
    table.hands.assign(static_cast<std::size_t>(table.seats), {});
    for (int seat = 0; seat < table.seats; ++seat) {
        table.hands[seat] = table.hand_of(seat);
        if (table.hands[seat].empty()) return false;
    }

    const auto turns = messages_of(table.broadcast_log(), "TURN");
    if (turns.empty()) return false;
    table.current = turns.back().get_int("player", -1);
    table.top = turns.back().get("top");
    if (table.current < 0 || table.current >= table.seats) return false;
    if (table.top.empty()) return false;
    return true;
}

using Predicate = std::function<bool(const Table &)>;

// 依次尝试 seed_from..seed_to，返回第一个满足 ok 的局面。
bool open_table(Table &table, int seats, const Predicate &ok, int seed_from = 1,
                int seed_to = 40) {
    for (int seed = seed_from; seed <= seed_to; ++seed) {
        auto room = std::make_unique<uno::test::LocalRoom>(seats - 1, "scn");
        if (!room->ok()) {
            std::cerr << "room setup failed: " << room->error() << '\n';
            return false;
        }
        room->host().set_seed(static_cast<std::uint32_t>(seed));
        room->host_line("start");

        Table candidate;
        candidate.room = std::move(room);
        candidate.seed = seed;
        candidate.seats = seats;
        if (!read_table(candidate)) continue;
        if (ok(candidate)) {
            table = std::move(candidate);
            return true;
        }
    }
    return false;
}

std::string find_card(const std::vector<std::string> &hand,
                      const std::function<bool(const Card &)> &pick) {
    for (const std::string &name : hand) {
        Card card;
        if (parse_card(name, card) && pick(card)) return name;
    }
    return {};
}

bool top_is_plain_number(const Table &table) {
    Card top;
    return parse_card(table.top, top) && is_number(top);
}

// 需要一个「引牌是数字牌，且当前玩家手上有一张能出的功能牌」的局面。
Predicate needs_playable(const std::string &face) {
    return [face](const Table &table) {
        if (!top_is_plain_number(table)) return false;
        Card top;
        parse_card(table.top, top);
        return !find_card(table.hands[table.current], [&](const Card &card) {
                    return card.face == face && playable_on(card, top, "");
                })
                    .empty();
    };
}

std::string playable_card(const Table &table, const std::string &face) {
    Card top;
    parse_card(table.top, top);
    return find_card(table.hands[table.current], [&](const Card &card) {
        return card.face == face && playable_on(card, top, "");
    });
}

// ---------------------------------------------------------------------------
// 场景
// ---------------------------------------------------------------------------

void test_deal() {
    Table table;
    if (!open_table(table, 3, [](const Table &) { return true; })) {
        CHECK_OR(false, "开局失败：请确认 start 会发出 HAND / START / TURN，"
                        "并且 TURN 带 player= 与 top=");
        return;
    }

    std::cout << "  [发牌] seed=" << table.seed << " 引牌=" << table.top
              << " 首出=" << table.current << '\n';

    for (int seat = 0; seat < table.seats; ++seat)
        CHECK_OR(table.hands[seat].size() == 7,
                 "座位 " + std::to_string(seat) + " 开局应有 7 张牌，实际 " +
                     std::to_string(table.hands[seat].size()) + " 张");

    // 手牌是私有的：每个客户端开局只应收到一条 HAND（自己那条）。
    for (int seat = 1; seat < table.seats; ++seat) {
        const std::size_t received =
            count_of(table.room->client_output(seat - 1), "HAND");
        CHECK_OR(received == 1,
                 "座位 " + std::to_string(seat) + " 开局收到 " +
                     std::to_string(received) +
                     " 条 HAND；手牌是私有信息，每人只该收到自己那一条");
    }

    // 两个人的手牌不该一模一样（把同一副牌广播给所有人的典型症状）。
    CHECK_OR(table.hands[1] != table.hands[2],
             "两个客户端拿到了完全相同的手牌");

    // 所有手牌加起来不能超出牌组本身的数量。
    const std::map<std::string, int> limits = deck_limits();
    std::map<std::string, int> used;
    for (const auto &hand : table.hands) {
        for (const std::string &name : hand) {
            Card card;
            CHECK_OR(parse_card(name, card), "无法识别的牌名 '" + name + "'");
            used[name] += 1;
        }
    }
    for (const auto &entry : used) {
        const auto limit = limits.find(entry.first);
        CHECK_OR(limit != limits.end(), "牌组里没有 '" + entry.first + "'");
        if (limit != limits.end())
            CHECK_OR(entry.second <= limit->second,
                     "'" + entry.first + "' 在发出去的手牌里出现 " +
                         std::to_string(entry.second) + " 次，超过牌组上限 " +
                         std::to_string(limit->second));
    }
}

void test_turn_ownership() {
    Table table;
    // 关键：让没轮到的玩家手里有一张「本来就能出」的牌。否则他会因为颜色
    // 不匹配而被拒，测试就测不到回合归属这条规则了。
    const Predicate ready = [](const Table &t) {
        if (!top_is_plain_number(t)) return false;
        Card top;
        parse_card(t.top, top);
        const int other = t.next_seat(t.current);
        return !find_card(t.hands[other], [&](const Card &card) {
                    return !is_wild(card) && playable_on(card, top, "");
                })
                    .empty();
    };
    if (!open_table(table, 3, ready)) {
        CHECK_OR(false, "找不到「没轮到的玩家手里有能出的牌」的开局");
        return;
    }

    Card top;
    parse_card(table.top, top);
    const int other = table.next_seat(table.current);
    const std::string card = find_card(table.hands[other], [&](const Card &c) {
        return !is_wild(c) && playable_on(c, top, "");
    });
    const std::size_t played_before = count_of(table.broadcast_log(), "PLAYED");

    std::cout << "  [回合归属] seed=" << table.seed << " 轮到=" << table.current
              << " 抢跑道=" << other << " 他手上的合法牌=" << card << '\n';
    table.line(other, "play " + card);

    CHECK_OR(count_of(table.log(other), "ERROR") > 0,
             "没轮到的玩家出牌时应当收到 ERROR");
    CHECK_OR(count_of(table.broadcast_log(), "PLAYED") == played_before,
             "没轮到的玩家出牌竟然被接受了");
    CHECK_OR(table.top.empty() == false && table.hand_of(table.current).size() == 7,
             "非法出牌之后当前玩家的手牌数变了");
}

void test_matching_rules() {
    Table table;
    const Predicate ready = [](const Table &t) {
        if (!top_is_plain_number(t)) return false;
        Card top;
        parse_card(t.top, top);
        const std::string bad = find_card(
            t.hands[t.current],
            [&](const Card &c) { return !playable_on(c, top, ""); });
        const std::string good = find_card(t.hands[t.current], [&](const Card &c) {
            return is_number(c) && playable_on(c, top, "");
        });
        return !bad.empty() && !good.empty();
    };
    if (!open_table(table, 3, ready)) {
        CHECK_OR(false, "找不到「既有合法数字牌又有非法牌」的开局");
        return;
    }

    Card top;
    parse_card(table.top, top);
    const std::string bad = find_card(
        table.hands[table.current],
        [&](const Card &c) { return !playable_on(c, top, ""); });
    const std::string good = find_card(table.hands[table.current], [&](const Card &c) {
        return is_number(c) && playable_on(c, top, "");
    });

    std::cout << "  [匹配] seed=" << table.seed << " 引牌=" << table.top
              << " 非法=" << bad << " 合法=" << good << '\n';

    const int me = table.current;
    table.line(me, "play " + bad);
    CHECK_OR(count_of(table.log(me), "ERROR") > 0,
             "颜色和图案都不匹配的牌应当被拒绝");
    CHECK_OR(count_of(table.broadcast_log(), "PLAYED") == 0,
             "不合法的牌被打出去了");

    table.line(me, "play " + good);
    const auto played = messages_of(table.broadcast_log(), "PLAYED");
    CHECK_OR(played.size() == 1, "合法的牌没有产生 PLAYED");
    if (!played.empty()) {
        CHECK_OR(played.back().get_int("player", -1) == me,
                 "PLAYED 里的 player 不是出牌者");
        CHECK_OR(played.back().get("cards") == good,
                 "PLAYED 里的 cards 与实际出的牌不一致");
    }

    const auto turns = messages_of(table.broadcast_log(), "TURN");
    CHECK_OR(!turns.empty() &&
                 turns.back().get_int("player", -1) == table.next_seat(me),
             "出牌后应当轮到下家");
    CHECK_OR(!turns.empty() && turns.back().get("top") == good,
             "出牌后 TURN 的 top 应当是刚打出的牌");
}

void test_draw_and_pass() {
    Table table;
    if (!open_table(table, 3, top_is_plain_number)) {
        CHECK_OR(false, "找不到引牌是数字牌的开局");
        return;
    }

    const int me = table.current;
    const std::string old_card = table.hands[me].front();

    table.line(me, "draw");

    const auto drawn = messages_of(table.broadcast_log(), "DRAWN");
    CHECK_OR(!drawn.empty(), "draw 应当广播 DRAWN");
    if (!drawn.empty()) {
        CHECK_OR(drawn.back().get_int("player", -1) == me,
                 "DRAWN 里的 player 不是抽牌者");
        CHECK_OR(drawn.back().get_int("count", 0) == 1,
                 "一次 draw 应当只抽 1 张");
    }
    CHECK_OR(table.hand_of(me).size() == 8, "抽牌后该玩家应当有 8 张牌");

    // 抽牌之后不能再出抽牌前就握着的牌。
    table.line(me, "play " + old_card);
    CHECK_OR(count_of(table.broadcast_log(), "PLAYED") == 0,
             "抽牌之后又把原来的牌打出去了");

    table.line(me, "pass");
    const auto turns = messages_of(table.broadcast_log(), "TURN");
    CHECK_OR(!turns.empty() &&
                 turns.back().get_int("player", -1) == table.next_seat(me),
             "pass 之后应当轮到下家");
}

void test_skip() {
    Table table;
    if (!open_table(table, 3, needs_playable("SK"), 1, 200)) {
        CHECK_OR(false, "找不到「当前玩家手上有一张能出的 SK」的开局");
        return;
    }

    const int me = table.current;
    std::cout << "  [Skip] seed=" << table.seed << " 座位=" << me << '\n';
    table.line(me, "play " + playable_card(table, "SK"));

    const auto turns = messages_of(table.broadcast_log(), "TURN");
    CHECK_OR(!turns.empty() &&
                 turns.back().get_int("player", -1) == table.next_seat(me, 2),
             "打出 SK 之后应当跳过一个玩家");
}

void test_reverse_three_players() {
    Table table;
    if (!open_table(table, 3, needs_playable("RV"), 1, 200)) {
        CHECK_OR(false, "找不到「当前玩家手上有一张能出的 RV」的开局");
        return;
    }

    const int me = table.current;
    std::cout << "  [Reverse] seed=" << table.seed << " 座位=" << me << '\n';
    table.line(me, "play " + playable_card(table, "RV"));

    const auto turns = messages_of(table.broadcast_log(), "TURN");
    CHECK_OR(!turns.empty() &&
                 turns.back().get_int("player", -1) == table.next_seat(me, -1),
             "三人局打出 RV 之后方向应当反转，轮到上家");
}

void test_draw_two() {
    Table table;
    if (!open_table(table, 3, needs_playable("D2"), 1, 200)) {
        CHECK_OR(false, "找不到「当前玩家手上有一张能出的 D2」的开局");
        return;
    }

    const int me = table.current;
    const int victim = table.next_seat(me);
    std::cout << "  [D2] seed=" << table.seed << " 出牌者=" << me
              << " 受害者=" << victim << '\n';

    table.line(me, "play " + playable_card(table, "D2"));

    // 叠加语义：下家先面对 2 张罚抽，可以选择继续叠，也可以吃下。
    auto turns = messages_of(table.broadcast_log(), "TURN");
    CHECK_OR(!turns.empty() && turns.back().get_int("player", -1) == victim,
             "D2 之后应当轮到受害者");
    CHECK_OR(!turns.empty() && turns.back().get_int("pending", 0) == 2,
             "D2 之后 TURN 应当带 pending=2");

    // 受害者吃下罚抽：一次抽 2 张并被跳过。
    table.line(victim, "draw");
    CHECK_OR(table.hand_of(victim).size() == 9,
             "吃下 D2 罚抽之后应当变成 9 张");
    turns = messages_of(table.broadcast_log(), "TURN");
    CHECK_OR(!turns.empty() &&
                 turns.back().get_int("player", -1) == table.next_seat(me, 2),
             "吃下罚抽之后受害者应当被跳过");
}

void test_two_player_reverse_is_skip() {
    Table table;
    if (!open_table(table, 2, needs_playable("RV"), 1, 200)) {
        CHECK_OR(false, "两人局里找不到「当前玩家手上有一张能出的 RV」的开局");
        return;
    }

    const int me = table.current;
    std::cout << "  [两人局 RV] seed=" << table.seed << " 座位=" << me << '\n';
    table.line(me, "play " + playable_card(table, "RV"));

    const auto turns = messages_of(table.broadcast_log(), "TURN");
    CHECK_OR(!turns.empty() && turns.back().get_int("player", -1) == me,
             "两人局打出 RV 之后应当仍由同一玩家出牌");
}

void test_wild_needs_color() {
    Table table;
    const Predicate ready = [](const Table &t) {
        return !find_card(t.hands[t.current],
                          [](const Card &c) { return c.face == "W"; })
                    .empty();
    };
    if (!open_table(table, 3, ready, 1, 200)) {
        CHECK_OR(false, "找不到「当前玩家手上有一张 W」的开局");
        return;
    }

    const int me = table.current;
    const std::string wild = find_card(
        table.hands[me], [](const Card &c) { return c.face == "W"; });

    std::cout << "  [Wild] seed=" << table.seed << " 座位=" << me << '\n';
    table.line(me, "play " + wild);
    CHECK_OR(count_of(table.broadcast_log(), "PLAYED") == 0,
             "打 Wild 时必须指定颜色，缺 color 应当被拒绝");

    table.line(me, "play " + wild + " blue");
    const auto played = messages_of(table.broadcast_log(), "PLAYED");
    CHECK_OR(played.size() == 1, "带颜色的 Wild 应当被接受");
    if (!played.empty()) {
        CHECK_OR(played.back().get("color") == "blue",
                 "PLAYED 应当带上指定的颜色");
        CHECK_OR(played.back().get("cards") == wild,
                 "PLAYED 的 cards 与实际出的牌不一致");
    }
}

void test_wrong_uno_call_gets_penalty() {
    Table table;
    if (!open_table(table, 3, top_is_plain_number)) {
        CHECK_OR(false, "找不到引牌是数字牌的开局");
        return;
    }

    const int me = table.current;
    std::cout << "  [错喊 UNO] seed=" << table.seed << " 座位=" << me << '\n';
    table.line(me, "uno");

    const auto penalties = messages_of(table.broadcast_log(), "UNOPENALTY");
    CHECK_OR(!penalties.empty(), "手里不止一张还喊 UNO，应当被判错喊并罚抽");
    if (!penalties.empty()) {
        CHECK_OR(penalties.back().get_int("player", -1) == me,
                 "UNOPENALTY 指向的玩家不对");
        CHECK_OR(penalties.back().get_int("count", 0) == 2,
                 "错喊 UNO 应当罚抽 2 张");
    }
    CHECK_OR(table.hand_of(me).size() == 9, "错喊 UNO 之后应当变成 9 张");
}

void test_seed_is_deterministic() {
    const Predicate any = [](const Table &) { return true; };
    Table first;
    Table second;
    if (!open_table(first, 3, any, 7, 7) || !open_table(second, 3, any, 7, 7)) {
        CHECK_OR(false, "同一个 seed 连续两次开局都失败");
        return;
    }
    std::cout << "  [确定性] seed=7\n";
    CHECK_OR(first.top == second.top, "同一个 seed 两次开局翻出的引牌不同");
    CHECK_OR(first.hands == second.hands,
             "同一个 seed 两次开局发出的手牌不同");
}

void test_garbage_message_is_ignored() {
    Table table;
    if (!open_table(table, 3, top_is_plain_number)) {
        CHECK_OR(false, "找不到引牌是数字牌的开局");
        return;
    }

    // 用一个裸连接冒充客户端，直接往协议里塞垃圾。
    uno::UnixConnection raw;
    if (!raw.connect(table.room->host().room_path())) {
        CHECK_OR(false, "无法建立裸连接");
        return;
    }
    table.room->pump_all(2);

    const std::size_t played_before = count_of(table.broadcast_log(), "PLAYED");
    CHECK(raw.send("BOGUS x=1"));
    CHECK(raw.send("PLAY"));
    table.room->pump_all(2);

    CHECK_OR(count_of(table.broadcast_log(), "PLAYED") == played_before,
             "垃圾消息不应当改变牌面");

    // 主机必须还活着：换回正常玩家继续操作。
    CHECK_OR(table.hand_of(table.current).size() == 7,
             "处理垃圾消息之后主机状态被破坏");
}

} // 匿名命名空间

int main() {
    test_deal();
    test_turn_ownership();
    test_matching_rules();
    test_draw_and_pass();
    test_skip();
    test_reverse_three_players();
    test_draw_two();
    test_two_player_reverse_is_skip();
    test_wild_needs_color();
    test_wrong_uno_call_gets_penalty();
    test_seed_is_deterministic();
    test_garbage_message_is_ignored();

    uno::test::write_score("rules_scenario_test", 2,
                           static_cast<unsigned>(checks),
                           static_cast<unsigned>(failures));
    if (failures != 0) {
        std::cerr << failures << " of " << checks << " check(s) failed\n";
        return 1;
    }
    std::cout << "rules scenario tests passed (" << checks << " checks)\n";
    return 0;
}
