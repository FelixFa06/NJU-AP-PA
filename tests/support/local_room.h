#pragma once

#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "game/client_session.h"
#include "game/host_session.h"

namespace uno {
namespace test {

// ---------------------------------------------------------------------------
// LocalRoom -- 进程内的确定性多人测试台。
//
// 它搭起「一个 host + N 个 client」，走真实的 AF_UNIX 套接字和真实的行式
// 协议，只是由测试逐个推进事件循环。因此既不需要 sleep，也不会出现不确定的
// 交错顺序，测试可以放心断言输出。
//
// 它同时是「本地多进程」这一要求的进程内缩影：同一份代码换成真实的多个
// 进程也一样跑得通。
// ---------------------------------------------------------------------------
class LocalRoom {
public:
    LocalRoom(std::size_t client_count, const std::string &label) {
        static int counter = 0;
        m_room = "test-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter++) + "-" + label;

        m_host = std::make_unique<HostSession>("Host", m_host_in, m_host_out);
        m_host->set_prompt_enabled(false);

        std::string err;
        if (!m_host->listen_room(m_room, err)) m_error = err;

        for (std::size_t i = 0; i < client_count; ++i) {
            auto in = std::make_unique<std::istringstream>();
            auto out = std::make_unique<std::ostringstream>();
            auto client = std::make_unique<ClientSession>(
                "P" + std::to_string(i + 1), *in, *out);
            client->set_prompt_enabled(false);
            if (m_error.empty() && !client->join_room(m_room, err))
                m_error = err;
            m_client_in.push_back(std::move(in));
            m_client_out.push_back(std::move(out));
            m_clients.push_back(std::move(client));
        }
        settle();
    }

    bool ok() const { return m_error.empty(); }
    const std::string &error() const { return m_error; }

    HostSession &host() { return *m_host; }
    ClientSession &client(std::size_t i) { return *m_clients.at(i); }

    std::string host_output() const { return m_host_out.str(); }
    std::string client_output(std::size_t i) const {
        return m_client_out.at(i)->str();
    }

    void pump_all(int rounds = 1) {
        for (int i = 0; i < rounds; ++i) {
            m_host->pump(0);
            for (auto &client : m_clients) client->pump(0);
        }
    }

    // 反复推进，直到所有参与者连续若干轮都没有新的输出。
    void settle(int max_passes = 64) {
        std::size_t stable = 0;
        std::size_t last = total_output_size();
        for (int i = 0; i < max_passes; ++i) {
            pump_all(1);
            const std::size_t now = total_output_size();
            if (now == last) {
                if (++stable >= 3) break;
            } else {
                stable = 0;
                last = now;
            }
        }
    }

    // 注入一行本地输入，然后让整个房间静下来。
    void host_line(const std::string &line) {
        m_host->handle_line(line);
        settle();
    }

    void client_line(std::size_t i, const std::string &line) {
        m_clients.at(i)->handle_line(line);
        settle();
    }

private:
    std::size_t total_output_size() const {
        std::size_t total = m_host_out.str().size();
        for (const auto &out : m_client_out) total += out->str().size();
        return total;
    }

    std::istringstream m_host_in;
    std::ostringstream m_host_out;
    std::vector<std::unique_ptr<std::istringstream>> m_client_in;
    std::vector<std::unique_ptr<std::ostringstream>> m_client_out;
    std::unique_ptr<HostSession> m_host;
    std::vector<std::unique_ptr<ClientSession>> m_clients;
    std::string m_room;
    std::string m_error;
};

inline bool contains(const std::string &text, const std::string &needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace test
} // namespace uno
