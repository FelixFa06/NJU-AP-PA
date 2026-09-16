#!/usr/bin/env bash
#
# 进程级驱动测试：只用键盘输入驱动一个 host 会话，检查大厅命令表与参数校验。
#
# 它不检查游戏规则，也不依赖 host 的开局策略（开局需要至少两名玩家，而这里
# 只有键盘输入），所以骨架状态和完成状态都该全绿。
set -euo pipefail

driver=${1:?usage: shell_commands_test.sh /path/to/uno_shell_driver}

actual=$(
    printf '%s\n' \
        'help' \
        'kick' \
        'rename' \
        'bogus' \
        'challenge maybe' \
        'start' \
        'exit' \
        'quit' |
        "$driver"
)

require() {
    if [[ "$actual" != *"$1"* ]]; then
        printf 'missing expected output: %s\n' "$1" >&2
        printf '%s\n' '--- actual output ---' >&2
        printf '%s\n' "$actual" >&2
        exit 1
    fi
}

# 大厅命令表
require 'Lobby commands:'
require 'start'
require 'kick <player_id|player_name>'
require 'rename <name>'

# 参数个数与取值由命令表统一把关
require 'ERROR msg="usage: kick <player_id|player_name>"'
require 'ERROR msg="usage: rename <name>"'
require 'ERROR msg="Unknown command: bogus'
require 'ERROR msg="Unknown command: challenge'

printf 'shell command script tests passed\n'
