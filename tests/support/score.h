#pragma once

// ---------------------------------------------------------------------------
// 评分文件
//
// 每个评分测试跑完之后会写出一个**定长 32 字节、小端**的二进制文件，方便脚本
// 直接读，不必去解析测试的文本输出。
//
//   offset  0   char[4]   magic     "UNOS"
//   offset  4   uint32    version   1
//   offset  8   uint32    stage     0 = 框架回归，1/2/3 = 对应阶段
//   offset 12   uint32    checks    断言总数
//   offset 16   uint32    failed    失败断言数
//   offset 20   uint32    reserved  0
//   offset 24   double    score     0.00 ~ 100.00，等于 (checks-failed)/checks×100
//
// 文件名是 <测试名>.bin，目录取环境变量 UNO_SCORE_DIR，没设置就写当前目录。
// 测试崩溃或没跑起来时不会留文件 —— 读不到文件就按 0 分处理。
//
// 用脚本读（示例）：
//
//   od -An -tu4 -j12 -N4 deck_scenario_test.bin   # 断言总数
//   od -An -tu4 -j16 -N4 deck_scenario_test.bin   # 失败数
//   od -An -tf8 -j24 -N8 deck_scenario_test.bin   # 分数
//
// 这里假定小端主机（x86-64 / AArch64 Linux 都是）。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace uno {
namespace test {

inline double score_of(unsigned checks, unsigned failed) {
    if (checks == 0) return 0.0;
    if (failed > checks) failed = checks;
    return 100.0 * static_cast<double>(checks - failed) /
           static_cast<double>(checks);
}

inline bool write_score(const std::string &name, unsigned stage,
                        unsigned checks, unsigned failed,
                        const std::string &directory = "") {
    std::string dir = directory;
    if (dir.empty()) {
        const char *env = std::getenv("UNO_SCORE_DIR");
        dir = (env && *env) ? env : ".";
    }
    const std::string path = dir + "/" + name + ".bin";

    unsigned char buffer[32] = {0};
    std::memcpy(buffer, "UNOS", 4);
    const auto put32 = [&buffer](std::size_t offset, std::uint32_t value) {
        std::memcpy(buffer + offset, &value, sizeof value);
    };
    put32(4, 1u); // version
    put32(8, stage);
    put32(12, checks);
    put32(16, failed);
    put32(20, 0u); // reserved
    const double score = score_of(checks, failed);
    std::memcpy(buffer + 24, &score, sizeof score);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char *>(buffer), sizeof buffer);
    return out.good();
}

} // namespace test
} // namespace uno
