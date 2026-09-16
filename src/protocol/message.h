#pragma once

#include <map>
#include <string>
#include <vector>

namespace uno {

// ---------------------------------------------------------------------------
// 统一消息接口。
//
// 每条消息都是一行文本：
//     TYPE key1=value1 key2=value2 ...
//
// 包含空白或特殊字符（'='、'"'、'\'）的值会被双引号包裹并转义。无论
// 端点位于同一进程（本地传输）还是不同机器（网络传输），使用的都是同一
// 种线格式，从而满足“统一信息传递”的要求。
// ---------------------------------------------------------------------------
class Message {
public:
    std::string type;
    std::map<std::string, std::string> fields;

    Message() = delete;
    explicit Message(std::string t) : type(std::move(t)) {}

    Message& set(const std::string& k, const std::string& v) { fields[k] = v; return *this; }
    Message& set(const std::string& k, int v) { fields[k] = std::to_string(v); return *this; }

    // 获取键 "k" 对应的值；如果不存在则返回 def。
    std::string get(const std::string& k, const std::string& def = "") const;
    bool has(const std::string& k) const;
    int  get_int(const std::string& k, int def = 0) const;

    std::string serialize() const;
    static Message parse(const std::string& line);
};

// 按双引号规则切分一行（返回已反转义的 token）。
std::vector<std::string> tokenize_line(const std::string& line);
std::string quote_value(const std::string& v);

} // 命名空间 uno
