#!/usr/bin/env bash
#
# 命令行入口测试：帮助文本、参数校验，以及「开一个房间然后退出」这条最小
# 路径。同样与作业实现无关，骨架状态就该全绿。
set -euo pipefail

uno=${1:?usage: cli_seed_test.sh /path/to/uno}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

help_output=$("$uno" --help)
for expected in '--seed=SEED' '--room=NAME' '--join' '--host=HOST'; do
    if [[ "$help_output" != *"$expected"* ]]; then
        printf 'missing %s in help output\n' "$expected" >&2
        exit 1
    fi
done

"$uno" --seed=42 --help >/dev/null
"$uno" --join --help >/dev/null

if "$uno" --seed=not-a-number --help >"$tmp/out" 2>&1; then
    printf 'invalid seed unexpectedly succeeded\n' >&2
    exit 1
fi
if ! grep -q "invalid seed" "$tmp/out"; then
    printf 'invalid seed did not report an error\n' >&2
    cat "$tmp/out" >&2
    exit 1
fi

if "$uno" --host= --room=x < /dev/null >"$tmp/out" 2>&1; then
    printf 'empty --host unexpectedly succeeded\n' >&2
    exit 1
fi

if "$uno" --tcp --join < /dev/null >"$tmp/out" 2>&1; then
    printf 'conflicting --tcp and --join unexpectedly succeeded\n' >&2
    exit 1
fi
if ! grep -q "cannot be combined" "$tmp/out"; then
    printf 'conflicting options did not report an error\n' >&2
    cat "$tmp/out" >&2
    exit 1
fi

# 最小的一条真实路径：开一个本地房间，然后立刻退出。
room="cli-test-$$"
host_output=$(printf 'quit\n' | "$uno" --room="$room" --name=Host)
if [[ "$host_output" != *"Listening on room"* ]]; then
    printf 'host did not report its room\n' >&2
    printf '%s\n' "$host_output" >&2
    exit 1
fi

printf 'cli seed tests passed\n'
