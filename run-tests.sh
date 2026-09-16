#!/usr/bin/env bash
#
# 自动化测试入口。你不需要读任何测试代码，运行下面这条命令就够了：
#
#   ./run-tests.sh        跑全部（框架回归 + 三个阶段）
#   ./run-tests.sh 1      只跑阶段一：会话与大厅
#   ./run-tests.sh 2      只跑阶段二：基础规则
#   ./run-tests.sh 3      只跑阶段三：拓展规则
#   ./run-tests.sh framework   只跑框架自身的回归
#
# 它会自动配置并编译到 build-tests/，然后跑对应阶段的测试。
#
# 注意：阶段是累进的 —— 阶段二、阶段三的测试也要先能开局，所以阶段一的
# 功能没写完时，它们同样会失败。
set -euo pipefail

target=${1:-all}
build_dir=${BUILD_DIR:-build-tests}

case "$target" in
    all | 1 | 2 | 3 | framework) ;;
    *)
        echo "用法: $0 [all|1|2|3|framework]" >&2
        exit 2
        ;;
esac

cd "$(dirname "$0")"
mkdir -p "$build_dir"

echo "== 配置与编译（$build_dir/）=="
cmake -S . -B "$build_dir" -DUNO_GRADING_TESTS=ON >"$build_dir/configure.log" 2>&1 || {
    echo "配置失败，日志见 $build_dir/configure.log" >&2
    tail -n 30 "$build_dir/configure.log" >&2
    exit 1
}
if ! cmake --build "$build_dir" -j >"$build_dir/build.log" 2>&1; then
    echo "编译失败，日志见 $build_dir/build.log" >&2
    tail -n 40 "$build_dir/build.log" >&2
    exit 1
fi

cd "$build_dir"

# 评分文件写到这里，跑完由本脚本汇总打印（格式见 tests/support/score.h）。
score_dir="$PWD/scores"
rm -rf "$score_dir"
mkdir -p "$score_dir"
export UNO_SCORE_DIR="$score_dir"

uint_at() { od -An -tu4 -j"$2" -N4 "$1" | tr -d ' \n'; }
double_at() { od -An -tf8 -j"$2" -N8 "$1" | tr -d ' \n'; }

print_scores() {
    local found=0
    for file in "$score_dir"/*.bin; do
        [[ -e "$file" ]] || continue
        found=1
        printf '  %-24s %6.1f 分（%s 项断言，%s 项失败）\n' \
            "$(basename "$file" .bin)" \
            "$(double_at "$file" 24)" \
            "$(uint_at "$file" 12)" \
            "$(uint_at "$file" 16)"
    done
    [[ "$found" == 1 ]] || echo "  （没有评分文件：测试可能没跑起来）"
}

set +e
if [[ "$target" == "all" ]]; then
    echo "== 跑全部测试 =="
    ctest --output-on-failure
elif [[ "$target" == "framework" ]]; then
    echo "== 框架回归 =="
    ctest -L framework --output-on-failure
else
    echo "== 阶段 $target（含框架回归）=="
    ctest -L "framework|stage$target" --output-on-failure
fi
status=$?
set -e

echo
if [[ "$target" == "framework" ]]; then
    echo "== 框架回归不计分 =="
else
    echo "== 分数（$score_dir）=="
    print_scores
fi
exit "$status"
