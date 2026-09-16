// 协议编解码测试。这一段是框架自带的能力，与作业实现无关，因此默认就会跑。
#include <iostream>
#include <string>

#include "protocol/message.h"

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

void test_round_trip() {
    uno::Message message("PLAY");
    message.set("cards", "R5,G5");
    message.set("color", "blue");
    const std::string line = message.serialize();

    const uno::Message parsed = uno::Message::parse(line);
    CHECK(parsed.type == "PLAY");
    CHECK(parsed.get("cards") == "R5,G5");
    CHECK(parsed.get("color") == "blue");
    CHECK(!parsed.has("missing"));
    CHECK(parsed.get("missing", "fallback") == "fallback");
}

void test_quoting() {
    uno::Message message("INFO");
    message.set("msg", "a b=c \"quoted\" back\\slash");
    const std::string line = message.serialize();

    // 含空白与特殊字符的值必须被引号包裹。
    CHECK(line.find('"') != std::string::npos);

    const uno::Message parsed = uno::Message::parse(line);
    CHECK(parsed.get("msg") == "a b=c \"quoted\" back\\slash");
}

void test_integer_fields() {
    const uno::Message parsed = uno::Message::parse("TURN player=3 top=R5");
    CHECK(parsed.get_int("player") == 3);
    CHECK(parsed.get_int("missing", -1) == -1);

    const uno::Message broken = uno::Message::parse("TURN player=abc");
    CHECK(broken.get_int("player", 7) == 7);
}

void test_malformed_lines() {
    // 尾部缺少 '=' 的字段直接忽略，前面的字段照常解析。
    const uno::Message parsed = uno::Message::parse("STATE top=R5 garbage");
    CHECK(parsed.type == "STATE");
    CHECK(parsed.get("top") == "R5");

    // 空行解析出空类型，调用方应当直接忽略。
    CHECK(uno::Message::parse("").type.empty());
}

} // 匿名命名空间

int main() {
    test_round_trip();
    test_quoting();
    test_integer_fields();
    test_malformed_lines();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "message tests passed\n";
    return 0;
}
