// 写评分文件的小工具，给 shell 写的测试用（C++ 测试直接调 support/score.h）。
//
//   uno_score <测试名> <阶段> <断言总数> <失败数>
//
// 例子：
//   uno_score room_scenario 1 7 0     # 阶段一，7 项断言全过
//
// 文件格式见 tests/support/score.h。
#include <cstdlib>
#include <iostream>
#include <string>

#include "support/score.h"

int main(int argc, char **argv) {
    if (argc != 5) {
        std::cerr << "usage: uno_score <name> <stage> <checks> <failed>\n";
        return 2;
    }
    const std::string name = argv[1];
    const unsigned stage = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));
    const unsigned checks = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));
    const unsigned failed = static_cast<unsigned>(std::strtoul(argv[4], nullptr, 10));

    if (!uno::test::write_score(name, stage, checks, failed)) {
        std::cerr << "uno_score: could not write the score file\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
