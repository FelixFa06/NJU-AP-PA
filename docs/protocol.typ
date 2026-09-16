#set document(title: "UNO 通信协议", author: "课程组")
#set page(margin: (x: 1.7cm, y: 1.7cm), numbering: "1")
#set text(font: ("Noto Serif CJK SC", "Noto Serif"), size: 12pt)
#set par(justify: true, leading: 1em)
#set heading(numbering: "1.1")
#show heading: set block(above: 1.1em, below: 0.6em)
#show heading.where(level: 1): set block(above: 2em, below: 0.9em)
#show raw: set text(size: 10pt)
#show raw.where(block: true): set block(fill: luma(245), inset: 8pt, radius: 3pt, width: 100%)

#align(center)[
  #text(size: 20pt, weight: "bold")[UNO 通信协议]
]
#v(0.3cm)

端点之间传的都是文本行，一行一条消息：

```text
TYPE key1=value1 key2=value2 ...
```

- 含空白或 `=`、`"`、`\` 的值会被双引号包裹并转义。编码解码由框架的
  `Message` 完成，你不需要自己拼字符串。
- 每个端点只把消息发给它该发的人。框架负责分帧、排队与投递，你负责决定
  「发什么」和「发给谁」。
- 表里没列的字段可以自行扩展，但不要改动已有字段名与语义：评分测试按它们
  观察你的输出。

= 消息一览

== 客户端发给主机

#table(
  columns: (auto, 1fr),
  inset: 6pt,
  align: (left, left),
  stroke: 0.4pt,
  table.header([*消息*], [*字段与含义*]),
  [`JOIN`], [字段 `name`。连上之后发的第一条消息。],
  [`RENAME`], [字段 `name`。改名。],
  [`PLAY`], [字段 `cards`（逗号分隔，可以多张），可选 `color`。出牌。],
  [`DRAW`], [无字段。抽一张牌。],
  [`PASS`], [无字段。抽牌之后结束本回合。],
  [`UNO`], [可选字段 `player`。喊 UNO；带 `player` 时表示举报该玩家漏喊。],
  [`CHALLENGE`], [字段 `accept`，取值 `yes` 或 `no`。回答 `+4` 质疑。],
  [`SWAP`], [字段 `player`。打出 `7` 之后指定交换对象。],
)

== 主机发给客户端

#table(
  columns: (auto, 1fr),
  inset: 6pt,
  align: (left, left),
  stroke: 0.4pt,
  table.header([*消息*], [*字段与含义*]),
  [`WELCOME`],
  [字段 `id`、`name`。只发给新玩家：告诉他坐在哪个座位。],
  [`PLAYERS`],
  [字段 `count`，以及每个座位的 `0=`、`1=`…（座位号等于名字）。广播大厅名单。],
  [`START`], [无字段。广播开局；客户端收到后进入游戏阶段。],
  [`HAND`], [字段 `cards`（逗号分隔）。只发给本人：他自己的手牌。],
  [`TURN`],
  [字段 `player`、`top`，可选 `direction`、`pending`。广播轮到谁、当前牌面、
   待罚抽张数。],
  [`PLAYED`],
  [字段 `player`、`cards`，指定了颜色时还要带 `color`。广播有人出了牌。],
  [`DRAWN`], [字段 `player`、`count`，可选 `card`。广播有人抽了牌。],
  [`UNO`], [字段 `player`。广播有人喊了 UNO。],
  [`UNOPENALTY`], [字段 `player`、`count`。广播漏喊或错喊的罚抽。],
  [`CHALLENGE`],
  [询问时字段是 `player`、`ask`、`cards`、`color`；结果时字段是 `player`、
   `accept`、`result`。广播 `+4` 质疑的询问与结果。],
  [`SWAP`],
  [全体换牌时字段是 `all`；等指定对象时是 `player`、`waiting`；完成时是
   `player`、`with`。广播 0-7 换牌。],
  [`GAMEOVER`], [字段 `winner`、`points`。广播本局结束，所有人回到大厅。],
  [`ABORTED`], [无字段。可选：宿主中止本局时广播，骨架不会发它。],
  [`ERROR`], [字段 `msg`。只发给操作者本人：刚才那个操作为什么非法。],
)

= 可观察语义（评分依据）

上面是字段表；这一节规定「收到什么命令就必须产生什么消息」。评分测试只按这一节
断言，不看你的内部实现。实现内部怎么设计不受限制。

通用约定：

- 每个端点的日志里应当能看到它*收到*的所有消息，评分测试就是从这些消息判断
  行为的。
- `HAND` 是私有消息：任何时候都只能发给手牌的主人，牌桌上其他人绝不能收到。
- 被判定的非法操作一律以 `ERROR` 回复给发起者，并且*不产生任何其它消息*、
  不改变任何状态。
- 出牌、抽牌、换位之类的动作完成后，必须广播新的 `TURN`。

各命令的必需应答：

+ `JOIN`：回复 `WELCOME`（带座位号与名字），然后向所有人广播 `PLAYERS`。
+ `start`（最少 2 人）：人数不足时 `ERROR` 并留在大厅；成功时先给每位玩家
  发送私有的 `HAND`（7 张），然后广播 `START`，最后广播 `TURN`（带首出玩家
  与引牌）。洗牌与发牌遵守 `docs/manual.typ` 的「牌堆与确定性洗牌」一节：
  同一个 `--seed` 必须得到同一副手牌。
+ `PLAY`：合法时广播 `PLAYED`（带出牌者、所出的牌），然后广播新的 `TURN`
  （`top` 是新牌面）；非法时只回 `ERROR`。只要这次出牌指定了颜色（万能牌，
  或同数不同色的多张），`PLAYED` 就必须带 `color`，并且该颜色成为接下来的
  有效颜色。
+ `DRAW`：广播 `DRAWN`（`count=1`，可带抽到的牌），并给该玩家发送新的私有
  `HAND`。抽牌之后本轮*不能*再出抽牌前就握在手里的牌。
+ `PASS`：只有在本回合已经抽过牌时才合法；合法时广播新的 `TURN`，否则
  `ERROR`。
+ 功能牌：效果在 `PLAY` 之后紧接着体现为新的 `TURN`（必要时还给受影响玩家
  发送私有 `HAND`）。`SK` 跳过下家；`RV` 在三人及以上时反转方向、两人局
  等同 `SK`；`D2` 与 `W4` 进入罚抽链（细节见规则文档）；`W` 与 `W4` 必须带
  `color`，该颜色一直生效到下一次打出非万能牌。
+ `UNO`：只有手里正好 1 张时喊牌才算合法，广播 `UNO`；其它张数都算错喊，
  广播 `UNOPENALTY` 并给他发送私有 `HAND`（+2 张）。喊牌不受轮次限制：任何
  时候都可以喊，判定只看当时的手牌张数。带 `player` 时表示举报漏喊。
+ `CHALLENGE`：只在刚被打出 `W4`、正在等被罚者回答的窗口内合法；结果广播
  `CHALLENGE`，并给受罚者发送新的私有 `HAND`。窗口之外一律 `ERROR`。
+ `SWAP`：只在刚打出 `7`、正在等出牌者指定对象的窗口内合法；合法时交换双方
  手牌，两人各收到新的私有 `HAND`，然后广播新的 `TURN`。
+ 有人出完最后一张牌：广播 `GAMEOVER`（带赢家与结算分值），所有人回到大厅。
+ 无法识别的消息类型或字段缺失：`ERROR` 给发送者，不改变状态，不崩溃。

`cards` 不是协议消息而是本地命令：执行后必须在*本端*打印一行
`HAND cards=<该玩家的手牌>`（host 打印权威手牌，client 打印缓存视图）。

= 与框架的对应关系

- 客户端把命令翻译成消息：`ClientSession::play/draw/pass/...`（框架已给出）。
- 主机收到消息走 `HostSession::on_peer_message(PeerId from, const Message&)`，
  `from` 是*连接*的身份；座位号是你自己模型里的概念，两者的映射由你维护。
- 客户端收到消息走 `ClientSession::on_peer_message(...)`，在这里维护本地视图。
- `START`、`GAMEOVER`、`ABORTED` 在客户端一侧的*阶段切换*由框架完成，你只需
  维护视图内容。

规则细节见 `docs/rules.typ`，任务与评分见 `docs/manual.typ`。
