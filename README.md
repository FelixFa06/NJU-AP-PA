# UNO 终端卡牌游戏

本仓库为 2026 年秋季《高级程序设计》的课程大作业: 终端下运行的 UNO 卡牌游戏。

## 编译

```bash
cmake -S . -B build
cmake --build build -j
```

## 运行

```bash
# 终端 1：主机开房间
./build/uno --room=demo --name=Host --seed=42

# 终端 2、终端 3：玩家加入
./build/uno --join --room=demo --name=Alice
./build/uno --join --room=demo --name=Bob
```

在主机终端输入 `start` 开局；`help` 列出当前阶段能敲的命令，`exit` 退出。
`./build/uno --help` 列出全部命令行选项。

## 测试与评分

```bash
./run-tests.sh        # 全部
./run-tests.sh 1      # 阶段一：会话与大厅
./run-tests.sh 2      # 阶段二：基础规则
./run-tests.sh 3      # 阶段三：拓展规则
```

提交方式、验收安排与评分构成见 `docs/manual.pdf`。

## 文档

| 文件 | 内容 |
|---|---|
| `docs/manual.pdf` | 实验指导手册：三个阶段的任务、注意事项、运行与测试 |
| `docs/rules.pdf` | UNO 规则、拓展规则、确定性洗牌契约 |
| `docs/protocol.pdf` | 通信协议字段表与可观察语义 |

