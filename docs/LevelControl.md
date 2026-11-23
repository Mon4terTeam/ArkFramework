# 关卡控制器与随机/隐藏敌人控制

本文说明明日方舟关卡数据如何进入 `BattleController`、权重敌人和隐藏组
分别在什么阶段生效，以及 `BetterBattle` 的 `level-control` 如何在不改客户端
资源的情况下覆盖这些行为。

实现位置：`plugins/BetterBattle/battle_level_control.cpp`。

## 1. 数据流

关卡数据的主要结构是：

```text
Torappu.LevelData
  waves: WaveData[]
    fragments: FragmentData[]
      actions: ActionData[]
  branches: ListDict<string, BranchData>
    phases: PhaseData[]
      actions: ActionData[]
```

`LevelUtils.LoadLevel(levelId)` 返回 `LevelData`。本地开战时，插件在
`BattleController` 创建前修改这个对象，随后 `BattleController` 把波次交给
`Torappu.Battle.Scheduler`。Scheduler 在运行时持有 `m_waves`，并按照
fragment、action 的类型和延迟逐步执行。

因此控制点分成两类：

| 控制对象 | 生效阶段 | 能否局内切换 |
| --- | --- | --- |
| 权重敌人 pack | 关卡数据进入 Scheduler 前 | 不能，必须重新开战 |
| 隐藏组 | Scheduler 执行动作时 | 可以，直接改启用集合 |
| 原动作字段 | 关卡数据进入 Scheduler 前 | 不能，必须重新开战 |
| 追加敌人 | 关卡数据进入 Scheduler 前 | 不能，必须重新开战 |

## 2. 权重敌人

`ActionData` 里和权重随机有关的字段：

| 字段 | 含义 |
| --- | --- |
| `randomSpawnGroupKey` | 权重选择组 |
| `randomSpawnGroupPackKey` | 组内候选 pack |
| `weight` | 该 pack/动作的权重 |

实际规则：

- 同一个 `randomSpawnGroupKey` 是一次选择。
- 同一个 `randomSpawnGroupPackKey` 的刷怪动作作为一个整体保留。
- 空 `randomSpawnGroupPackKey` 时，每个动作本身就是一个单项候选。
- 选择完成后，同一 group 中未选中的动作不会执行。

例如 `level_rogue6_4-7` 的 `t1` 有 5 个空 pack 动作：

```text
空动作         850
trap_321...    105
trap_322...     35
trap_320...     35
trap_323...     15
```

固定到 `trap_323_shsbox#1` 后，应用后的关卡数据中 `t1` 只剩这一个动作。

对非空 pack，保留的是 pack 内的动作集合，而不是单独保留某个动作。

## 3. 隐藏组

`ActionData.hiddenGroup` 不是预处理器删除条件。隐藏组动作会保留在关卡数据
和 Scheduler 波次里，Scheduler 执行动作时通过：

```text
Scheduler.m_enabledHiddenGroups: Torappu.ListSet<string>
```

过滤动作。组名不在集合里，动作就不执行。

这与权重敌人的区别很重要：

- 隐藏组可以开战后立即启用或禁用。
- 不需要重建关卡数据。
- 当前客户端中 Scheduler 会自行设置默认隐藏组。例如
  `level_rogue6_4-7` 默认启用了 `hard`。
- 插件按逻辑帧持续把配置同步到该集合，所以即使 Scheduler 后续重新加回默认
  组，也会再次被配置覆盖。

`remove_disabled_hidden: true` 是另一条路径：它在开战前直接把被禁用组的动作
从 `actions` 数组删除。这样动作不会再出现在 Scheduler 中，但本次战斗内也
无法再通过 `enable` 恢复。

## 4. 配置文件

默认读取：

```text
ArkFramework/build-cmake/dist/level_control.json
```

每次 `start` 或 `rogue-start` 前重新读取。文件不存在时不修改关卡。

完整示例：

```json
{
  "stage": "ro6_e_4_7",
  "level": "level_rogue6_4-7",
  "dump_catalog": true,
  "enable_hidden": ["shsdgo"],
  "disable_hidden": ["hard", "shsdgo2", "shsdgo3", "shsdgo4"],
  "remove_disabled_hidden": true,
  "random_groups": [
    { "group": "t1", "key": "trap_323_shsbox#1" },
    { "group": "t2", "pack": "a" },
    { "group": "s1", "index": 1 },
    { "group": "unused_group", "remove": true }
  ],
  "patch_actions": [
    {
      "wave": 0,
      "fragment": 2,
      "enemy": "enemy_10065_ftzlc",
      "count": 5,
      "interval": 1.5
    }
  ],
  "add_enemies": [
    {
      "wave": 0,
      "fragment": 2,
      "enemy": "enemy_1001_bigbo",
      "count": 1,
      "route": 14,
      "preDelay": 4.0,
      "interval": 1.0
    }
  ]
}
```

### stage / level

- 两项都为空：所有关卡都尝试应用。
- `stage` 匹配关卡输入 ID，例如 `ro6_e_4_7`。
- `level` 匹配最终关卡 ID，例如
  `Obt/Roguelike/RO6/level_rogue6_4-7`。
- `"*"` 表示任意。

配置不匹配当前关卡时不会修改数据。

### random_groups

选择器优先级为 `pack`、`key`、`index`：

| 字段 | 作用 |
| --- | --- |
| `group` | 必填，`randomSpawnGroupKey` |
| `pack` | 固定到指定 `randomSpawnGroupPackKey` |
| `key` | 固定到指定动作 key，再按该动作的 pack 保留 |
| `index` | 固定到该组遍历顺序中的第 N 个候选，从 0 开始 |
| `remove` | 删除该组的全部动作 |

`index` 指的是组内候选序号，不是 fragment 的原始动作下标。要选择 `EMPTY`
这类空 key 动作时使用 `index`。

如果指定了 `pack`、`key` 或 `index` 但找不到对应候选，插件会中止本次开战
并报告错误，避免误把整组删空。配置的组在当前关卡完全不存在时只记录日志。

### patch_actions

用以下字段定位动作：

| 字段 | 作用 |
| --- | --- |
| `wave` | 波次下标 |
| `fragment` | 波次内 fragment 下标 |
| `phase` | 分支 phase 下标 |
| `index` | 当前 fragment/phase 的动作下标 |
| `enemy` | 匹配 `ActionData.key` |
| `hidden_group` | 匹配 `hiddenGroup` |
| `random_pack` | 匹配 `randomSpawnGroupPackKey` |

对每个 fragment/phase，最多修改第一个匹配动作。其余字段会写到
`ActionData`。支持字符串、布尔、整数、单精度、双精度和枚举名。

常用修改：

```json
{
  "wave": 0,
  "fragment": 2,
  "enemy": "enemy_10065_ftzlc",
  "count": 5,
  "preDelay": 2.0,
  "interval": 1.5,
  "routeIndex": 14,
  "randomType": "ALWAYS",
  "refreshType": "ALWAYS",
  "isValid": true
}
```

`extraMeta` 和 `actionId` 不支持直接写。

### add_enemies

追加动作会自动完成：

1. 检查 `route` 是否在当前 `LevelData.routes` 范围内。
2. 如果 `enemyDbRefs` 中没有该敌人，创建
   `{ useDb: true, id: enemy, level: 0, overwrittenData: null }`。
3. 如果目标 fragment 不存在，按需创建空的 `FragmentData`。
4. 创建 `SPAWN` 类型的 `ActionData`，追加到目标 fragment。

| 字段 | 默认值 | 作用 |
| --- | --- | --- |
| `wave` | 0 | 目标波次，必须已存在 |
| `fragment` | 0 | 目标 fragment，不存在时创建 |
| `enemy` | 必填 | EnemyDB 敌人 ID |
| `count` | 1 | 生成数量 |
| `route` | 0 | `LevelData.routes` 下标 |
| `preDelay` | 0 | 首次生成延迟，秒 |
| `interval` | 1 | 同动作多次生成的间隔，秒 |

当前只修改 EnemyDB 引用，不覆盖 `EnemyData` 数值。需要改血量、攻击、技能等
属性时，仍应使用自定义关卡的 `enemyDbRefs[].overwrittenData`。

## 5. 目录与验证

`dump_catalog: true` 会写两份文件：

```text
dist/level_control_catalog.json          修改前
dist/level_control_catalog_applied.json  修改后
```

每份文件包含：

- `actions`：完整动作列表，包括普通、隐藏组和权重组动作。
- `hidden_groups`：按 `hiddenGroup` 分类。
- `random_groups`：按 `group|pack` 分类。

检查随机组是否真的固定、动作字段是否修改、额外动作是否追加，应以
`level_control_catalog_applied.json` 为准。

## 6. 运行态命令

```text
level-control dump
level-control groups
level-control status
level-control hidden list
level-control hidden enable <group>
level-control hidden disable <group>
level-control hidden clear
```

`dump/groups/status` 都会写：

```text
dist/level_control_live.json
```

它读取当前 Scheduler 的 `m_waves` 和 `m_enabledHiddenGroups`，适合确认战斗中
还剩哪些动作、当前启用了哪些隐藏组。

命令通过 `BetterBattle.request` 发送，例如：

```powershell
Set-Content -LiteralPath 'ArkFramework\build-cmake\dist\BetterBattle.request' `
  -Value 'level-control hidden enable shsdgo' -NoNewline
```

## 7. 已验证行为

在当前测试客户端和 `Obt/Roguelike/RO6/level_rogue6_4-7` 上已验证：

- `enable_hidden: ["shsdgo"]` 后 Scheduler 只保留 `shsdgo`，
  `enemy_2137_shsdgo` 实际生成。
- `disable_hidden: ["hard"]` 会移除 Scheduler 默认启用的 `hard`。
- `random_groups` 固定 `t1/t2/s1` 后，应用后的关卡数据每组只剩目标动作。
- `patch_actions` 将 `enemy_10065_ftzlc` 改为
  `count=5, interval=1.5`，应用后数据正确。
- `add_enemies` 追加 `enemy_1001_bigbo` 后，敌人实际生成，状态为
  `boss=1, hp=26400`。
- `remove_disabled_hidden: true` 与权重固定、字段修改、追加敌人同时使用时，
  被禁用隐藏组从应用后数据中物理消失，Scheduler 隐藏组集合与配置一致。

## 8. 限制

- 权重、字段修改和追加敌人只在开战前生效；修改配置后需要重新进入关卡。
- `add_enemies` 只支持普通波次 fragment，不直接追加到分支 phase。
- 分支动作可以读取、参与隐藏组和权重处理，也可以通过 `phase` 修改，但当前
  目录中的 `branch:#N` 是遍历序号，不是原始分支 key。
- 不修改 EnemyDB 全局数据，也不改变敌人原型；只改关卡动作和引用。
