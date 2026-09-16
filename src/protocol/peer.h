#pragma once

#include <string>

namespace uno {

// ---------------------------------------------------------------------------
// PeerId -- 框架为每个成功建立的连接分配的身份。
//
// 它标识的是「一条连接」，不是「一名玩家」。玩家编号（座位）是你需要自己
// 设计的模型，两者之间的映射同样由你维护：收到消息时拿到的是 PeerId，广播
// 时你可以发给 PeerId，也可以发给你自己模型里的座位号对应的 PeerId。
//
// 连接断开后该 id 作废，框架不会复用已经用过的 id，因此可以安全地把它作为
// map 的键。
// ---------------------------------------------------------------------------
class PeerId {
public:
    PeerId() = default;
    explicit PeerId(int value) : m_value(value) {}

    int value() const { return m_value; }
    bool valid() const { return m_value > 0; }
    std::string str() const { return std::to_string(m_value); }

    friend bool operator==(PeerId a, PeerId b) { return a.m_value == b.m_value; }
    friend bool operator!=(PeerId a, PeerId b) { return a.m_value != b.m_value; }
    friend bool operator<(PeerId a, PeerId b) { return a.m_value < b.m_value; }

private:
    int m_value = 0;
};

} // namespace uno
