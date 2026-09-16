#!/usr/bin/env bash
#
# 跨进程回归测试：一个 host 进程开房间，一个 client 进程加入同一个房间。
#
# 它只验证框架承诺的部分（房间建立、跨进程连接、标准输入结束后的干净退出），
# 不涉及任何作业实现，所以骨架状态就该全绿。
set -euo pipefail

uno=${1:?usage: room_cli_test.sh /path/to/uno}
room="roomcli-$$"
tmp=$(mktemp -d)

socket_dir="${XDG_RUNTIME_DIR:-/tmp/uno-$(id -u)}/uno"
socket="$socket_dir/$room.sock"

pids=()
cleanup() {
    exec 3>&- 4>&- 2>/dev/null || true
    for pid in "${pids[@]:-}"; do kill "$pid" 2>/dev/null || true; done
    wait 2>/dev/null || true
    rm -rf "$tmp"
}
trap cleanup EXIT

fail() {
    printf 'room cli failure: %s\n' "$1" >&2
    for log in host client; do
        if [[ -s "$tmp/$log.log" ]]; then
            printf '%s\n' "--- $log.log ---" >&2
            cat "$tmp/$log.log" >&2
        fi
    done
    exit 1
}

wall() {
    local file=$1 pattern=$2 attempts=${3:-250}
    for ((i = 0; i < attempts; ++i)); do
        if grep -q -- "$pattern" "$file" 2>/dev/null; then return 0; fi
        sleep 0.02
    done
    return 1
}

mkdir -p "$socket_dir"
mkfifo "$tmp/host.in" "$tmp/client.in"

"$uno" --room="$room" --name=Host <"$tmp/host.in" >"$tmp/host.log" 2>&1 &
host_pid=$!
pids+=("$host_pid")
exec 3>"$tmp/host.in"

for ((i = 0; i < 250; ++i)); do
    [[ -S "$socket" ]] && break
    sleep 0.02
done
[[ -S "$socket" ]] || fail "the host never opened room $room"
grep -q 'Listening on room' "$tmp/host.log" ||
    fail "the host did not announce its room"

"$uno" --join --room="$room" --name=Alice <"$tmp/client.in" \
    >"$tmp/client.log" 2>&1 &
client_pid=$!
pids+=("$client_pid")
exec 4>"$tmp/client.in"

wall "$tmp/client.log" 'Joined room' || fail "the client never joined"

# 关掉客户端标准输入：它应该自己退出，并且以 0 结束。
exec 4>&-
if ! wait "$client_pid"; then fail "the client exited with a failure"; fi

# 关掉 host 标准输入：同样应该干净退出。
exec 3>&-
if ! wait "$host_pid"; then fail "the host exited with a failure"; fi
pids=()

printf 'room cli tests passed\n'
