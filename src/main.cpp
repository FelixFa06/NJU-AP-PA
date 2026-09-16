#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <unistd.h>

#include <getopt.h>

#include "game/client_session.h"
#include "game/host_session.h"

namespace {

// 程序标识，用于诊断信息以及 --help / --version 输出。
constexpr const char *kProgram = "uno";
constexpr const char *kVersion = "2.0";

// 默认房间名。本地多人就是「同一台机器上的多个进程加入同一个房间」。
constexpr const char *kDefaultRoom = "uno";

struct Options {
	std::string name;               // 为空时使用默认值
	std::string room = kDefaultRoom; // 本地房间名
	bool join = false;              // 以客户端身份加入本地房间
	std::string hostAddr;           // 非空表示连接远程主机（彩蛋）
	bool hostGiven = false;         // 是否显式提供了 --host
	bool tcpHost = false;           // 以 TCP 方式监听（彩蛋）
	int port = 1145;
	std::uint32_t seed = 0;
	bool seedGiven = false;
	std::string deck;        // 非空表示注入固定牌堆（逗号分隔）
};

bool is_client_mode(const Options &opt) {
	return opt.join || opt.hostGiven;
}

// GNU 风格的用法摘要："Usage: prog [OPTION]..."，后接简短说明和对齐的选项列表。
void print_help(std::ostream &os) {
	os << "Usage: " << kProgram
	   << " [OPTION]...\n"
		  "\n"
		  "UNO - terminal card game.\n"
		  "\n"
		  "By default the program opens a room on this machine and waits for\n"
		  "other processes to join it; with --join it joins a room that is\n"
		  "already running.\n"
		  "\n"
		  "Options:\n"
		  "  -n, --name=NAME   display name (default: \"Host\" as host, the "
		  "local\n"
		  "                      hostname as a client)\n"
		  "      --room=NAME   local room name (default: \"uno\")\n"
		  "      --join        join the local room instead of hosting it\n"
		  "      --seed=SEED   deterministic deck-shuffle seed (for tests)\n"
		  "      --deck=LIST   fixed deck, comma separated (for tests)\n"
		  "  -h, --help        display this help and exit\n"
		  "  -V, --version     output version information and exit\n"
		  "\n"
		  "Options kept for the networked variant (see the manual):\n"
		  "      --host=HOST   join a host over TCP instead of a local room\n"
		  "      --port=PORT   TCP port to listen on or connect to (default: "
		  "1145)\n"
		  "      --tcp[=PORT]  host the game over TCP instead of a local room\n";
}

void print_hint() {
	std::cerr << "Try '" << kProgram << " --help' for more information.\n";
}

std::string default_client_name() {
	char buf[256] = {0};
	if (gethostname(buf, sizeof buf - 1) == 0 && buf[0] != '\0')
		return buf;
	return "Player";
}

// "--deck=R5,R7,GD2" -> {"R5", "R7", "GD2"}
std::vector<std::string> split_cards(const std::string& text) {
	std::vector<std::string> cards;
	std::string current;
	for (char c : text) {
		if (c == ',') {
			if (!current.empty()) cards.push_back(current);
			current.clear();
		} else if (c != ' ' && c != '\t') {
			current += c;
		}
	}
	if (!current.empty()) cards.push_back(current);
	return cards;
}

// 按 GNU 选项语法解析命令行：
//   * 长选项同时接受 "--opt=value" 和 "--opt value"；
//   * 接受无歧义的长选项前缀（例如用 "--po" 表示 "--port"）；
//   * 短选项可以组合，也可以紧跟取值（例如 "-nBob"）；
//   * "--" 终止选项列表。
// 成功时返回 true；如果处理了 --help 或 --version，则设置 `exiting`。
bool parse_args(int argc, char **argv, Options &opt, bool &exiting) {
	enum {
		OPT_HOST =
			0x100, // 仅长选项使用，避开短选项取值范围
		OPT_PORT,
		OPT_SEED,
		OPT_ROOM,
		OPT_JOIN,
		OPT_TCP,
		OPT_DECK,
	};
	static const struct option longOptions[] = {
		{"name", required_argument, nullptr, 'n'},
		{"room", required_argument, nullptr, OPT_ROOM},
		{"join", no_argument, nullptr, OPT_JOIN},
		{"host", required_argument, nullptr, OPT_HOST},
		{"port", required_argument, nullptr, OPT_PORT},
		{"tcp", optional_argument, nullptr, OPT_TCP},
		{"seed", required_argument, nullptr, OPT_SEED},
		{"deck", required_argument, nullptr, OPT_DECK},
		{"help", no_argument, nullptr, 'h'},
		{"version", no_argument, nullptr, 'V'},
		{nullptr, 0, nullptr, 0},
	};

	// getopt_long 会在诊断信息前加上 argv[0]；这里使用裸程序名，
	// 使所有消息无论调用路径如何都显示为 "uno: ..."。
	argv[0] = const_cast<char *>(kProgram);

	optind = 1; // 从第一个参数重新开始扫描
	int c;
	while ((c = getopt_long(argc, argv, "n:hV", longOptions, nullptr)) != -1) {
		switch (c) {
		case 'n':
			opt.name = optarg;
			break;
		case OPT_ROOM:
			opt.room = optarg;
			if (opt.room.empty()) {
				std::cerr << kProgram << ": --room requires a non-empty name\n";
				print_hint();
				return false;
			}
			break;
		case OPT_JOIN:
			opt.join = true;
			break;
		case OPT_HOST:
			opt.hostAddr = optarg;
			opt.hostGiven = true;
			break;
		case OPT_TCP:
			opt.tcpHost = true;
			if (optarg) {
				// "--tcp=PORT"：复用 --port 的校验逻辑。
				const std::string value = optarg;
				char *end = nullptr;
				const long port = std::strtol(value.c_str(), &end, 10);
				if (value.empty() || end == value.c_str() || *end != '\0' ||
					port <= 0 || port > 65535) {
					std::cerr << kProgram << ": invalid port: '" << value
							  << "'\n";
					print_hint();
					return false;
				}
				opt.port = static_cast<int>(port);
			}
			break;
		case OPT_PORT: {
			const std::string value = optarg;
			char *end = nullptr;
			const long port = std::strtol(value.c_str(), &end, 10);
			if (value.empty() || end == value.c_str() || *end != '\0' ||
				port <= 0 || port > 65535) {
				std::cerr << kProgram << ": invalid port: '" << value << "'\n";
				print_hint();
				return false;
			}
			opt.port = static_cast<int>(port);
			break;
		}
		case OPT_SEED: {
			const std::string value = optarg;
			char *end = nullptr;
			const unsigned long long seed =
				std::strtoull(value.c_str(), &end, 10);
			if (value.empty() || value[0] == '-' || end == value.c_str() ||
				*end != '\0' ||
				seed > std::numeric_limits<std::uint32_t>::max()) {
				std::cerr << kProgram << ": invalid seed: '" << value << "'\n";
				print_hint();
				return false;
			}
			opt.seed = static_cast<std::uint32_t>(seed);
			opt.seedGiven = true;
			break;
		}
		case OPT_DECK:
			opt.deck = optarg;
			break;
		case 'h':
			print_help(std::cout);
			exiting = true;
			return true;
		case 'V':
			std::cout << kProgram << " " << kVersion << "\n";
			exiting = true;
			return true;
		case '?':
		default:
			// getopt_long 已经在 stderr 上报告了错误选项。
			print_hint();
			return false;
		}
	}

	if (optind < argc) {
		std::cerr << kProgram << ": unexpected operand: '" << argv[optind]
				  << "'\n";
		print_hint();
		return false;
	}
	if (opt.hostGiven && opt.hostAddr.empty()) {
		std::cerr << kProgram << ": --host requires a non-empty host address "
				  << "(omit --host to run as the host)\n";
		print_hint();
		return false;
	}
	if (opt.hostGiven && opt.join) {
		std::cerr << kProgram << ": --host and --join are mutually exclusive\n";
		print_hint();
		return false;
	}
	if (opt.tcpHost && is_client_mode(opt)) {
		std::cerr << kProgram << ": --tcp hosts a game and cannot be combined "
				  << "with --join or --host\n";
		print_hint();
		return false;
	}
	return true;
}

} // 匿名命名空间

int main(int argc, char **argv) {
	std::signal(SIGPIPE, SIG_IGN);

	Options opt;
	bool exiting = false;
	if (!parse_args(argc, argv, opt, exiting))
		return 1;
	if (exiting)
		return 0;

	if (is_client_mode(opt)) {
		if (opt.name.empty())
			opt.name = default_client_name();
		uno::ClientSession session(opt.name);
		if (opt.seedGiven)
			session.set_seed(opt.seed);
		if (!opt.deck.empty())
			session.set_deck_override(split_cards(opt.deck));

		std::string err;
		const bool connected =
			opt.hostGiven ? session.connect_tcp(opt.hostAddr, opt.port, err)
						  : session.join_room(opt.room, err);
		if (!connected) {
			std::cerr << "Error: " << err << "\n";
			return 1;
		}
		if (opt.hostGiven)
			std::cout << "Connected to " << opt.hostAddr << ":" << opt.port
					  << " as \"" << opt.name << "\".\n";
		else
			std::cout << "Joined room \"" << opt.room << "\" as \"" << opt.name
					  << "\".\n";
		return session.run();
	}

	if (opt.name.empty())
		opt.name = "Host";
	uno::HostSession session(opt.name);
	if (opt.seedGiven)
		session.set_seed(opt.seed);
	if (!opt.deck.empty())
		session.set_deck_override(split_cards(opt.deck));

	std::string err;
	if (opt.tcpHost) {
		if (!session.listen_tcp(opt.port, err)) {
			std::cerr << "Error: " << err << "\n";
			return 1;
		}
		std::cout << "Listening on port " << opt.port << " as \"" << opt.name
				  << "\".\n";
		std::cout << "Clients can join with: uno --host=<ip> --port=" << opt.port
				  << " [--name=<name>]\n";
	} else {
		if (!session.listen_room(opt.room, err)) {
			std::cerr << "Error: " << err << "\n";
			return 1;
		}
		std::cout << "Listening on room \"" << opt.room << "\" as \""
				  << opt.name << "\".\n";
		std::cout << "Clients can join with: uno --join --room=" << opt.room
				  << " [--name=<name>]\n";
	}
	return session.run();
}
