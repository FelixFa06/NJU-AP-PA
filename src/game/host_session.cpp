#include "host_session.h"

#include <utility>

namespace uno {

HostSession::HostSession(std::string name, std::istream& in, std::ostream& out)
    : Session(Role::Host, std::move(name), in, out) {
    add_lobby_command("start", "start",
                      "start the game and deal the cards (host only)",
                      [this](const Args& args) { return start(args); });
    add_lobby_command("kick", "kick <player_id|player_name>",
                      "remove a player from the room (host only)",
                      [this](const Args& args) { return kick(args); }, 1, 1);
    initialize_players();
}

HostSession::~HostSession() = default;

// ---------------------------------------------------------------------------
// 传输初始化。这段是框架的一部分：房间路径推导、残留 socket 清理、权限、
// 非阻塞设置都已经处理好，你不需要改动。
// ---------------------------------------------------------------------------
bool HostSession::listen_room(const std::string& room, std::string& err) {
    const std::string path = room_socket_path(room, err);
    if (path.empty()) return false;

    auto server = std::make_unique<UnixServer>();
    if (!server->listen_path(path, err)) return false;

    m_room_path = path;
    m_server = std::move(server);
    loop().set_server(m_server.get());
    return true;
}

bool HostSession::listen_tcp(int port, std::string& err) {
    auto server = std::make_unique<TcpServer>();
    if (!server->listen(port, err)) return false;

    m_server = std::move(server);
    loop().set_server(m_server.get());
    return true;
}

// ---------------------------------------------------------------------------
// 事件钩子：这些是你要实现的部分。
// ---------------------------------------------------------------------------
void HostSession::on_peer_join(PeerId who) {
    // info("TODO(host): a client connected as peer " + who.str() +
    //      " -- greet it, ask for a name, and update the player list");
    (void)who;
}

void HostSession::on_peer_message(PeerId from, const Message& message) {
    // info("TODO(host): handle " + message.type + " from peer " + from.str());
    if (message.type == "JOIN")
    {
        int seat_id = append_player(from, message.get("name"));
        send_to(from, Message("WELCOME").set("id",seat_id).set("name",message.get("name")));
        broadcast(show_players());
    }
    if (message.type == "RENAME")
    {
        rename_player(from, message.get("name"));
        broadcast(show_players());
    }
}

void HostSession::on_peer_leave(PeerId who) {
    // info("TODO(host): peer " + who.str() +
    //      " disconnected -- drop it from the player list");
    drop_player(who);
    broadcast(show_players());
}

// ---------------------------------------------------------------------------
// Player List 相关操作
// ---------------------------------------------------------------------------
void HostSession::initialize_players() {
    m_players.clear();
    m_players.push_back(Player(PeerId(0),session_name()));
    m_seat_of[PeerId(0)] = 0;
}

Message HostSession::show_players() const {
    Message mes("PLAYERS");
    mes.set("count", (int)m_players.size());
    for (int i = 0; i < (int)m_players.size();i++)
        mes.set(std::to_string(i), m_players[i].name);
    return Message(mes);
}

int HostSession::append_player(PeerId who, std::string name) {
    Player player(who, name);
    int seat_id = (int)m_players.size();
    m_players.push_back(player);
    m_seat_of[who] = seat_id;
    return seat_id; // 返回座位号
}

void HostSession::rename_player(PeerId who, std::string name) {
    if (m_seat_of.find(who)==m_seat_of.end())
        return;
    int seat_id = m_seat_of[who];
    m_players[seat_id].name = name;
}

void HostSession::drop_player(PeerId who) {
    if (m_seat_of.find(who)==m_seat_of.end())
        return;
    int seat_id = m_seat_of[who];
    m_seat_of.erase(who);
    m_players.erase(m_players.begin() + seat_id);
    reindex_players();
}

void HostSession::reindex_players() {
    m_seat_of.clear();
    for (int i = 0; i < (int)m_players.size(); i++)
        m_seat_of[m_players[i].who] = i;
}

int HostSession::find_seat(const std::string& key) const { // key 可以为座位号或玩家名字
    bool numeric = !key.empty();
    for (char c : key) if (!std::isdigit((unsigned char)c)) { numeric = false; break; }
    if (numeric)
    {
        const int seat_id = std::stoi(key);
        return (seat_id >= 0 && seat_id < (int)m_players.size()) ? seat_id : -1;
    }
    for (int i = 0; i < (int)m_players.size(); i++)
        if (m_players[i].name == key) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// 本地命令行：host 玩家自己敲的命令。
// ---------------------------------------------------------------------------
Session::Result HostSession::start(const Args&) {
    // info("TODO(host): validate >=2 players, deal cards, send HAND to each "
    //      "player and START/TURN to everyone");
    // 骨架行为：即使游戏逻辑还没写，也让大厅 -> 游戏 -> 大厅的流程可走通。
    if ((int)m_players.size() < 2)
    {
        error("players not enough (less than 2)");
        return Result::Continue;
    }
    enter_game();
    return Result::StartGame;
}

Session::Result HostSession::kick(const Args& args) {
    // info("TODO(host): remove player " + args[1] + " and broadcast PLAYERS");
    int seat_id = find_seat(args[1]);
    if (seat_id < 0)
        error("no such player: " + args[1]); 
    else if (!m_players[seat_id].who.valid())
        error("cannot kick that player");
    else
        loop().close_peer(m_players[seat_id].who);
    return Result::Continue;
}

Session::Result HostSession::rename(const Args& args) {
    // info("TODO(host): rename player to " + args[1] + " and broadcast PLAYERS");
    rename_player(PeerId(0), args[1]);
    broadcast(show_players());
    return Result::Continue;
}

Session::Result HostSession::play(const Args& args) {
    info("TODO(host): validate and play " + args[1] +
         ", then send HAND/PLAYED/TURN");
    return Result::Continue;
}

Session::Result HostSession::cards(const Args&) {
    info("TODO(host): print the authoritative hand of this player");
    return Result::Continue;
}

Session::Result HostSession::draw(const Args&) {
    info("TODO(host): draw one card, decide playability, update the turn");
    return Result::Continue;
}

Session::Result HostSession::pass(const Args&) {
    info("TODO(host): finish the draw turn and advance the current player");
    return Result::Continue;
}

Session::Result HostSession::uno(const Args& args) {
    info(args.size() > 1 ? "TODO(host): resolve a forgotten-UNO report"
                         : "TODO(host): record this player's UNO call");
    return Result::Continue;
}

Session::Result HostSession::challenge(const Args& args) {
    if (!valid_challenge_answer(args)) return Result::Continue;
    info("TODO(host): resolve challenge answer=" + args[1]);
    return Result::Continue;
}

Session::Result HostSession::swap(const Args& args) {
    info("TODO(host): swap hands with player " + args[1]);
    return Result::Continue;
}

} // 命名空间 uno
