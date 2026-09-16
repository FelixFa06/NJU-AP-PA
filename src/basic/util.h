#pragma once

// 整个项目共享的小型字符串工具。它们位于 src/basic，因为这是最底层模块，
// 所有其他模块都可以依赖它。

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace uno {

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// 将命令行按空白字符拆分为单词。
inline std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= n) break;
        size_t j = i;
        while (j < n && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

} // 命名空间 uno
