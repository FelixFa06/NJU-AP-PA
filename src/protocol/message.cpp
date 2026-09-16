#include "message.h"

#include <cctype>

namespace uno {

std::string Message::get(const std::string &k, const std::string &def) const {
    auto it = fields.find(k);
    return it == fields.end() ? def : it->second;
}

bool Message::has(const std::string &k) const {
    return fields.find(k) != fields.end();
}

int Message::get_int(const std::string &k, int def) const {
    auto it = fields.find(k);
    if (it == fields.end())
        return def;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return def;
    }
}

std::string quote_value(const std::string &v) {
    bool need = false;
    for (char c : v) {
        if (c == ' ' || c == '\t' || c == '=' || c == '"' || c == '\\' ||
            c == '\n' || c == '\r') {
            need = true;
            break;
        }
    }
    if (!need)
        return v;

    std::string out = "\"";
    for (char c : v) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::vector<std::string> tokenize_line(const std::string &line) {
    std::vector<std::string> tokens;
    size_t i = 0, n = line.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        if (i >= n)
            break;

        if (line[i] == '"') {
            ++i;
            std::string cur;
            while (i < n && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < n) {
                    ++i;
                    cur += line[i];
                } else {
                    cur += line[i];
                }
                ++i;
            }
            if (i < n)
                ++i; // 跳过闭合引号
            tokens.push_back(std::move(cur));
        } else {
            size_t j = i;
            while (j < n && !std::isspace(static_cast<unsigned char>(line[j])))
                ++j;
            tokens.push_back(line.substr(i, j - i));
            i = j;
        }
    }
    return tokens;
}

Message Message::parse(const std::string &line) {
    size_t i = 0, n = line.size();

    // 第一个 token 是消息类型。
    while (i < n && std::isspace(static_cast<unsigned char>(line[i])))
        ++i;
    size_t j = i;
    while (j < n && !std::isspace(static_cast<unsigned char>(line[j])))
        ++j;
    Message m(line.substr(i, j - i));
    i = j;

    // 其余 token 是 key=value 对；value 可以用双引号包裹。
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        if (i >= n)
            break;

        size_t k = i;
        while (k < n && line[k] != '=' &&
               !std::isspace(static_cast<unsigned char>(line[k])))
            ++k;
        if (k >= n || line[k] != '=')
            break; // 尾部格式错误，忽略
        std::string key = line.substr(i, k - i);
        i = k + 1; // 跳过 '='

        std::string value;
        if (i < n && line[i] == '"') {
            ++i;
            while (i < n && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < n) {
                    ++i;
                    value += line[i];
                } else {
                    value += line[i];
                }
                ++i;
            }
            if (i < n)
                ++i; // 跳过闭合引号
        } else {
            size_t v = i;
            while (v < n && !std::isspace(static_cast<unsigned char>(line[v])))
                ++v;
            value = line.substr(i, v - i);
            i = v;
        }

        if (!key.empty())
            m.fields[key] = value;
    }
    return m;
}

std::string Message::serialize() const {
    std::string s = type;
    for (const auto &[k, v] : fields) {
        s += ' ';
        s += k;
        s += '=';
        s += quote_value(v);
    }
    return s;
}

} // 命名空间 uno
