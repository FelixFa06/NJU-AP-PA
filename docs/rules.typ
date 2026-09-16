#set document(title: "UNO 规则与拓展规则", author: "课程组")
#set page(margin: (x: 1.7cm, y: 1.7cm), numbering: "1")
#set text(font: ("Noto Serif CJK SC", "Noto Serif"), size: 12pt)
#set par(justify: true, leading: 1em)
#set heading(numbering: "1.1")
#show heading: set block(above: 1.1em, below: 0.6em)
#show heading.where(level: 1): set block(above: 2em, below: 0.9em)
#show raw: set text(size: 10pt)
#show raw.where(block: true): set block(fill: luma(245), inset: 8pt, radius: 3pt, width: 100%)

#align(center)[
  #text(size: 20pt, weight: "bold")[UNO 规则与拓展规则]
]
#v(0.3cm)

本文档是阶段三的规则规格。阶段二只需实现「基础规则」部分；阶段三需实现
「全部规则」，包括文末的拓展规则。这里只讲游戏规则；任务、消息格式与评分
方式见 `docs/manual.typ` 与 `docs/protocol.typ`。

= 牌组

标准 UNO 牌组共 108 张：

#table(
  columns: (1fr, auto),
  inset: 6pt,
  align: (left, left),
  stroke: 0.4pt,
  table.header([*牌*], [*数量*]),
  [每色（红/黄/绿/蓝）数字 0], [1 张 × 4 色],
  [每色数字 1--9], [2 张 × 9 种 × 4 色],
  [每色 Skip / Reverse / Draw Two], [2 张 × 3 种 × 4 色],
  [Wild], [4 张],
  [Wild Draw Four], [4 张],
)

牌编码：颜色 `R` / `Y` / `G` / `B`，数字 `0-9`，`SK`（Skip）、`RV`（Reverse）、
`D2`（Draw Two）、`W`（Wild）、`W4`（Wild Draw Four）。

= 游戏流程（基础规则）

1. 玩家加入大厅，人数不少于 2 人后由 host 执行 `start`。
2. 洗牌，每人发 7 张；翻出牌堆顶第一张作为*参照牌*（引牌）。
   - 若引牌是数字牌或 Wild：由庄家（玩家 0）先出牌。
   - 若引牌是功能牌：按功能生效（Skip 跳庄家、Reverse 反转、Draw Two/W4
     罚抽并跳过，详见第四节）。Wild 若作引牌，出牌颜色由庄家决定。
3. 出牌需与参照牌（弃牌堆最上一张）*颜色相同*或*图案（数字/符号）相同*；
   Wild 可无视参照牌直接打出，并由出牌者指定下一张牌的颜色。
4. 无牌可出时：从牌堆抽 1 张。若抽到的牌可出，可立即打出或保留；否则跳过，
   轮到下家。（也可有牌不出，但必须抽 1 张；抽牌后本轮不能出手中原来的牌。）
5. 先出完手牌者获胜；结算后回到大厅。

= 计分与结算

- 数字牌 0--9 计 0--9 分；功能牌（Skip/Reverse/Draw Two）计 20 分；
  Wild / Wild Draw Four 计 50 分。
- 一局结束时，其余玩家剩余手牌分值之和记为该局胜者得分；各玩家剩余手牌
  分值记为自己的负分。多局累积负分最少者为最终赢家。

= 功能牌效果

#table(
  columns: (auto, 1fr),
  inset: 6pt,
  align: (left, left),
  stroke: 0.4pt,
  table.header([*牌*], [*效果*]),
  [Skip], [跳过下家],
  [Reverse], [反转出牌方向（两人局等效 Skip）],
  [Draw Two], [下家摸 2 张并跳过],
  [Wild], [任意时刻可出，指定颜色],
  [Wild Draw Four], [指定颜色，下家摸 4 张并跳过],
)

两人局：Reverse 等价于 Skip；打出 Skip/Reverse 后仍由同一玩家继续出牌。
打出 Draw Two / Wild Draw Four 后，对方必须罚抽相应张数并跳过。

= 拓展规则（阶段三）

== +4 质疑（Wild Draw Four challenge）

- +4 仅在出牌者手中*没有与参照牌同颜色*的牌（数字牌或功能牌，不考虑万能牌）
  时才算合法。
- 被罚抽 4 张的下家有权质疑出牌合法性，出牌者须亮出手牌：
  - *不质疑*：下家直接罚抽 4 张并跳过。
  - *质疑不成立*（合法）：质疑者罚抽 6 张并跳过。
  - *质疑成立*（违规）：出牌者罚抽 4 张；质疑者可正常出牌（按出 `+4` 时
    指定的颜色），无牌可出则抽 1 张后轮到再下家。
- 无论结果如何，`+4` 指定的颜色始终生效。

== 叠加（stacking）

- `+2` 可叠加在 `+2` 上；`+4` 可叠加在任意罚抽牌上。
- 上家打出 `+2` 后，你可以出 `+2` 或 `+4`；上家打出 `+4` 后，只能用 `+4`
  叠加。直到某玩家不能（或不想）叠加时，罚抽所有累计张数并跳过。
- 叠加的 `+4` 默认不可质疑；限制：`+4` 直接叠 `+4` 时，下家仍可质疑首个
  `+4` 的合法性。至少打出一个 `+2` 后才允许「`+4` 后 `+4`」的连续叠加。

== 0-7 换牌

- 任意玩家打出 `0` 时，所有玩家按出牌方向交换手牌。
- 打出 `7` 时，出牌者可选择与任意玩家交换手牌。
- 换到仅剩 1 张牌的玩家须喊 UNO；若最后一张是 0 或 7，打出后直接获胜，
  其余玩家结算负分。

== 同牌叠出

- 同色数字牌可多张一起出（如 2 张「红 4」）；同数不同色至少 3 张才可一起出，
  出牌者从中指定颜色。
- 简化起见只允许「Skip」叠出：出 n 张 Skip 即跳过 n 个玩家。
- 强制约定最后 1 张牌只能单出；出到倒数第 2 张必须喊 UNO。

= UNO 喊牌

- 打剩 1 张牌时须喊「UNO」。忘喊且在下家出牌前被举报，罚抽 2 张；
  下家已出牌后发现则免罚。错喊 UNO 同样罚抽 2 张。

= 牌堆耗尽

牌堆摸空时，把弃牌堆（保留最上一张作参照牌）重新洗成新牌堆继续游戏。

= 双人规则

- Reverse 等同 Skip；打出 Skip/Reverse 后同一玩家继续出牌。
- 罚抽牌按叠加规则处理（见 5.2）。
