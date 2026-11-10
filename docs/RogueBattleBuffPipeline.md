# 肉鸽局内 Buff 下发链路

本文记录 BetterBattle 在本地肉鸽战斗中注入 Buff 的已验证链路，以及各主题特殊机制
（岁时、年代、坍缩、气象等）目前的接入状态。

## 1. `RoguelikeInput.extraBuffs`

```text
Torappu.Battle.Roguelike.RoguelikeInput
  List<RoguelikeTopicExtraBuffData> extraBuffs

Torappu.RoguelikeTopicExtraBuffData
  string id
  int layer
  List<RoguelikeBuff> buffs

Torappu.RoguelikeBuff
  string key
  Blackboard blackboard
```

真实局中 `RL05SquadStatePlugin.GetTopicExtraBuffs("rogue_5")` 返回的样例：

```text
id=rogue_5_copper_buff_B_08_a layer=1
  buffs[0] key=immediate_reward
           blackboard: trig_type=TAKE_OUT, id=rogue_5_population, count=1
  buffs[1] key=copper_exchange
           blackboard: trig_type=PUT_BACK, id=rogue_5_copper_B_01_a
```

即：额外 Buff 是「通用 buff 模板 + Blackboard 参数」，不是「一个 id 对应一个 buff」。

## 2. 可表驱动的来源：`details.<topic>.relics`

`roguelike_topic_table.json` 的 `details.<topic>.relics` 同时包含藏品和 Buff 物品：

```text
rogue_5_copper_buff_B_08_a
  buffs: [ { key, blackboard[] }, ... ]
```

因此只要拿到 buff 物品 id，就可以直接构造 `RoguelikeTopicExtraBuffData`，无需真实局。

### 钱币

`modules.rogue_5.copper.copperData.<copperId>.buffItemIdList` 给出该钱币对应的
buff 物品 id；再查 `details.rogue_5.relics` 即可拿到 buff 定义。

```text
铜钱 rogue_5_copper_B_08_a
  -> buffItemIdList: [rogue_5_copper_buff_B_08_a]
  -> details.relics[rogue_5_copper_buff_B_08_a].buffs
```

BetterBattle 的 `--buffitem=<id>@<layer>[,...]` 就是这条链路。

## 3. 各主题特殊机制现状

| 主题 | 机制 | 数据位置 | 结论 |
| --- | --- | --- | --- |
| 水月 | 排异反应 | `details.rogue_2.charBuffData` | 表驱动；`--charbuff` 可指定编队干员 |
| 水月 | 启示 | `details.rogue_2.squadBuffData` | 表驱动，`--extra=virtue:<id>:<layer>` |
| 黑流树海 | 新典训等干员 Buff | `details.rogue_6.charBuffData` | 表驱动；`--charbuff` 可指定编队干员 |
| 黑流树海 | 概念体 | `modules.rogue_6.scrap` | 白模鸟已实测走 `char_attribute_add attack_speed=50`；涂装黎博利同链路 +35，其余 7 个为非战斗效果 |
| 萨卡兹 | 思绪 | `modules.rogue_4.fragment.fragmentBuffData` | 文本可确定的全局数值走 `--outerbuff`，其余仅展示 |
| 探索者 | 密文版 | `modules.rogue_3.totemBuff.totemBuffDatas` + `details.items` | 文本可确定的全局数值走 `--outerbuff`，其余仅展示 |
| 界园 | 钱币 | `modules.rogue_5.copper` + `details.relics` | 表驱动，`--buffitem` 已接入 |
| 探索者 | 坍缩范式 | `modules.rogue_3.chaos.chaosDatas` | 表内无 buff 定义；真实局确认走 `outerBuffs`，非线性移动/行动已接入 |
| 萨卡兹 | 年代 | `modules.rogue_4.disaster.disasterData` | 表内无 buff 定义，未接入 |
| 界园 | 岁时 | `modules.rogue_5.wrath.wrathData` | 表内无 buff 定义，未接入 |
| 黑流树海 | 气象 | `modules.rogue_6.weather` | 表内无 buff 定义，未接入 |

## 4. 为什么 3/4/5 的特殊机制不能直接查表

- `RL05SquadStatePlugin.GetTopicExtraBuffs` 的 native 实现只调用
  `RoguelikeCopperUtil.GetPlayerCopperModuleData()` /
  `GetGameCopperModuleData(topic)`，产出的是钱币相关条目。
- 该类的 XLua hotfix 字段 `__Hotfix0_GetTopicExtraBuffs` 在运行时为 `0`，
  说明实际执行的是 native 分支，而不是 Lua 补丁。
- native 分支内已解析出的调用只有：
  `RoguelikeCopperUtil.GetPlayerCopperModuleData()`、
  `RoguelikeCopperUtil.GetGameCopperModuleData(topic)`、
  `RoguelikeDungeonController.GetDetailData(topic)`。
- `details.rogue_3/4/5.charBuffData` 为空，岁时/年代/坍缩没有像水月那样
  的 charBuff 表定义。
- `details.rogue_2.charBuffData` 19 条、`squadBuffData` 14 条；
  `details.rogue_6.charBuffData` 15 条（概念体），这两类有表定义。

因此这些机制目前没有找到「选择 id -> 完整 Buff 数值定义」的可复用客户端入口。
需要继续定位战斗侧对 `RoguelikeGameItemType.WRATH/DISASTER/CHAOS/WEATHER` 的
处理代码，或者直接捕获服务器下发的 `unKeepBuff`。

### 探索者坍缩的真实结构

真实探索者局中，「非线性移动」并没有进入 `extraBuffs`，而是进入
`RoguelikeInput.outerBuffs`：

```text
key = global_buff_normal
blackboard:
  key = enemy_attr_final[tag]
  max_hp = 1.15
  move_speed = 1.15
  attack_speed = 0
```

「非线性行动」对应同一结构，参数为 `max_hp=1.2`、`move_speed=1.2`、
`attack_speed=20`。启动后 `RoguelikeBattleManager.m_relics` 中出现
`GBuffNormal`，说明注入项已经通过管理器构建，而不是只停留在输入列表。

注意：`max_hp/move_speed=1.15` 这一组来自真实局 dump；`1.2/1.2/20`
目前来自关卡描述/用户核对值，尚未用同等强度的真实局 dump 复核。

### 其他主题的路径验证

用同一组 `outerBuffs` 分别在 `rogue_1..rogue_6` 启动本地战斗，六个主题都得到：

```text
outerBuffs count=1
m_relics count=1
Torappu.Battle.Roguelike.Internal.GBuffNormal
```

结果保存在 `build-cmake/dist/rogue_outer_path_test.json`。因此六个主题共享同一条
`RoguelikeInput.outerBuffs -> RoguelikeBattleManager -> GBuffNormal` 下发路径。
这不代表其他主题的每个特殊条目都已经知道 key/blackboard 参数；目前只有探索者的
非线性移动/行动有可直接复用的单个机制参数。其他真实局可通过 `rogue-capture`
保存完整 `unKeepBuff` 快照，并在 Web 配置的“真实局捕获”分组中整体启用。

### `global_buff_*` 的运行时注册关系

`RoguelikeBattleManager.m_relicClasses` 是
`Dictionary<string, System.Type>`，由 `_RegisterRelicClasses()` 在战斗初始化时填充。
实机 `rogue-dump-battle` 已导出 57 个有效注册项，关键映射为：

```text
global_buff_normal              -> Torappu.Battle.Roguelike.Internal.GBuffNormal
global_buff_stack               -> Torappu.Battle.Roguelike.Internal.GBuffStack
global_buff_stack_base_one      -> ...GBuffStackBaseOne
global_buff_layer               -> ...GBuffWithLayer
global_buff_override_blackboard -> ...GBuffOverrideBlackboard
global_buff_with_condition      -> ...GBuffWithCondition
```

同一份 dump 还包含 `char_attribute_*`、`enemy_attribute_*`、`level_*`、
`deck_*`、`misc_*` 等完整类族。也就是说，`unKeepBuff.key` 不是直接战斗 Buff；
它先由该表映射为 `BasicRelic` 子类，再由子类 `Preprocess` 解释 `Blackboard`。

同一 `prefabKey` 的多条 `outerBuffs` 如何合并，取决于处理器类：

```text
global_buff_normal         -> 每个条目独立生成 GlobalBuffData
global_buff_stack          -> 同一 prefabKey 的数值直接相加
                              1.1 + 1.2 = 2.3；attack_speed 1 + 2 = 3
global_buff_stack_base_one -> 同一 prefabKey 用 1 + Σ(v - 1) 合并
                              1.5 + 1.7 -> 2.2；5 + 7 -> 11
global_buff_layer          -> 按 layer 选择/覆盖
```

这里的合并发生在 `GBuffStack` / `GBuffStackBaseOne` 内部，不是服务器预先算好的
单个数值；因此抓取 `unKeepBuff` 原始条目比只记录最终倍率更有复现价值。

`_assets/ArknightsGameResource/gamedata/battle/buff_template_data.json` 里可以找到
这些处理器最终创建的战斗 Buff 模板。例如探索者坍缩会落到：

```text
rogue_3_enemy_attr_final[tag]
  ON_BUFF_START:
    CheckFilterTag(tag == "collapsal")
    IfNot -> FinishBuff
```

模板只定义“怎么应用”，具体 `max_hp/move_speed/attack_speed` 数值仍只出现在
服务器下发的 `global_buff_normal.blackboard` 中。因此不可能仅靠客户端静态表还原
全部等级数值。

## 5. 已提供的启动参数

```text
--relics=<id>@<layer>,...     藏品（details.relics）
--buffitem=<id>@<layer>,...   Buff 物品（details.relics 中含 buffs 的条目）
--charbuff=<id>@<layer>[:<charId>|<charId>],...
                              干员 Buff；targets 省略时 charUniqueIds 为空
--extra=<kind>:<id>:<layer>   水月 mutation/virtue 等直接条目
--outer=<key>:<field>:<value> 局外成长数值
--outerbuff=<key>|<keyField>|<field=value;...>
                              直接构造 RoguelikeBuff + Blackboard
--tech=<id>,...               科技树节点（web 端换算为 --outer）
```

### 5.0 干员 Buff 的目标映射

`RoguelikeGameCharBuffBattleData` 的 `charUniqueIds` 不是石英/角色模板 ID，
而是本局 `BattlePlayerData.characters[].uniqueId`。`--charbuff` 的 targets 使用
模板 ID（`BattleCharacterData.prefabKey`，例如 `char_1050_chen3`），
`rogue-start` 在构造完 `playerDataList` 后按 `prefabKey -> uniqueId` 映射并写入
`charUniqueIds`。目标不在本局时只写警告日志，不会把模板 ID 误当 uniqueId。

示例：

```text
--charbuff=rogue_2_mutation_1@1:char_1050_chen3|char_1029_yato2
```

### 5.0.1 思绪 / 密文版

这两类不是 `extraBuffs`，当前可确认的战斗数值直接构造为 `RoguelikeBuff`
写入 `outerBuffs`：

```text
rogue_4_fragment_F_19（异族，攻速+15）
  --outerbuff=char_attribute_add||attack_speed=15

rogue_3_totem_R_E1（歌唱，攻速+50）
  --outerbuff=char_attribute_add||attack_speed=50
```

构建脚本按描述只接入能确定的全局数值（攻速、攻击/生命/防御百分比、
再部署时间倍率、费用、部署人数）。涉及随机目标、条件触发、召唤物、
地形或仅局外资源的条目保持“仅展示”。

`--outerbuff` 的值会按完整数字自动走 `Blackboard.Assign(string,float)`，否则走
`Assign(string,string)`。例如：

```text
--outerbuff=global_buff_normal|enemy_attr_final[tag]|max_hp=1.15;move_speed=1.15;attack_speed=0
```

## 5.1 实际数据不是从 Module 现算的（重要）

对 `GameAssembly.dll` 的 `RoguelikeDataUtil.GetOutBuffs()`（RVA `0x28AFF80`）
反汇编核对后，实际逻辑是：

```text
current.player.pending[0]
  -> content
  -> battle
  -> unKeepBuff
```

也就是说，战斗开始时看到的 `outerBuffs`，来源是**当前 pending 的 BATTLE 事件里
已经存好的 `unKeepBuff` 列表**，不是运行时再从 `CurrentData.Module` 计算。
`GetOutBuffs()` 不读取 `Module.Chaos/Disaster/Wrath/Weather`。

各主题的当前状态字段如下，这些字段决定服务器生成/替换 pending 战斗时的战斗参数：

| 主题 | 当前状态 | 等级来源 |
| --- | --- | --- |
| 探索者 | `Module.Chaos.chaosList` | 条目 id（`_a` 为 1 级）；`Module.Chaos.level` 是坍缩整体效果等级，不是单个范式等级 |
| 萨卡兹 | `Module.Disaster.curDisasterId` | id 的 `_a/_b/_c` 后缀（表内 `level`） |
| 界园 | `Module.Wrath.wraths[]` | 条目 id（表内 `level`/`levelName`） |
| 黑流树海 | `Module.Weather.currentMain/currentSub` | 主气象 id 的 `_a/_b/_c` 后缀（表内 `level`） |

因此不能仅凭静态表枚举所有特殊机制的 Blackboard：

- `roguelike_topic_table.json` 只有名称、描述和等级，没有实际数值；
- `RoguelikeStartBattleRequest` 也没有 Buff 参数；
- pending BATTLE 的 `unKeepBuff` 是客户端从服务器同步下来的当局数据。

可以在真实局 pending 变为 BATTLE 时自动捕获：

```text
rogue-capture
```

BetterBattle 在检测到**非本地注入**的肉鸽战斗启动时，也会自动排队执行一次
`rogue-capture`。另外，`rogue-net-capture start` 安装的双 Hook 会在服务器增量
落地后检查 `current.player.pending[0]`；一旦 pending 变成 BATTLE，就按事件
`index` 去重并自动排队一次 `rogue-capture`。本地 `rogue-start` 不会自动覆盖捕获
目录，避免把 TAS 测试配置误当成官方局数据。

输出：

```text
build-cmake/dist/rogue_captures/capture-<tick>.json
build-cmake/dist/rogue_capture_latest.json
```

内容包含当前主题、pending 类型、Chaos/Disaster/Wrath/Weather 状态，以及
`unKeepBuff` 的完整 key/Blackboard。Web 配置构建脚本会读取 `rogue_captures`，
当一次捕获只包含一个特殊机制时，就把该机制的数值自动挂到对应条目上。

具体规则：

- 捕获文件里的 `active.chaos.ids`、`active.disaster.id`、`active.wrath.ids`、
  `active.weather.main/sub` 会匹配到主题表里的具体条目；
- 匹配到的条目直接写入 `mechanic.buffs`，UI 中该条目从“仅展示”变为可勾选；
- 同时仍保留一个“真实局捕获”分组，用于一键载入整组 Buff；
- `local:true` 的本地捕获不会参与自动挂载，避免 TAS 测试污染真实条目。

因此接入一个新年代/岁时/气象不需要改客户端硬编码：抓到一次真实战斗，
重新生成 `battle-config.js` 即可。

另外可使用 `rogue-player-dump` 查看当前局状态，使用 `rogue-outbuffs` 查看
当前 `GetOutBuffs()` 的原始返回。

### pending detail 与服务端数据

`PlayerRoguelikePendingEvent.Content.detailStr` 是 Base64 + zlib 压缩的 JSON。
BetterBattle 现在提供：

```text
rogue-player-json
rogue-pending-detail
rogue-net-capture start
rogue-net-capture stop
```

- `rogue-player-json` 导出完整 `PlayerDataModel.Serialize()` 到
  `build-cmake/dist/rogue_player_data.json`，可直接查看 `rlv2.current` 的
  pending、map、module、inventory。
- `rogue-pending-detail` 把每个 pending 的 `detailStr` 写入
  `build-cmake/dist/rogue_pending/`，并用客户端 `ZipUtils.DecompressStrFromBase64`
  解出 `pending-*.json`。当前真实 rogue_3 样本已解出完整的 run snapshot JSON。
- `rogue-net-capture start` 同时 Hook `PlayerData.NotifyDataChanged` 和
  `PlayerData.OnPlayerDataChanged`，把服务器数据增量中的 `modified/deleted`
  JObject 写进 `build-cmake/dist/rogue_net/`。`NotifyDataChanged` 更靠近网络层入口，
  `OnPlayerDataChanged` 覆盖模型回调；两个 Hook 用 delta 指针和 100 ms 窗口去重。
  这条路径抓到的是客户端已经反序列化后的服务端数据，不依赖 TLS 解密。
- 当前真实样本 `pending[0].type=15` 的 `content.result` 非空且 `content.battle=null`，
  因此该样本的 `15` 是结束结算事件，不是商店。所有类型判断都应继续使用运行时
  `PlayerRoguelikePlayerEventType` 的 `BATTLE` 枚举值，不能按枚举顺序硬编码。

从当前证据可以确定：

```text
服务端 PlayerDataDelta
  -> rlv2.current.player.pending[0].content.battle.unKeepBuff
  -> RoguelikeDataUtil.GetOutBuffs()
  -> RoguelikeInput.outerBuffs
  -> RoguelikeBattleManager.m_relics
  -> GBuffNormal / GBuffStack / GBuffWithLayer / ...
```

各主题只是 `CurrentData.Module` 的状态字段不同；一旦进入 BATTLE pending，战斗参数都走
同一条 `unKeepBuff` 链。客户端静态表无法替代服务端下发的原因也在这里。

### 5.2 等级数值到底写在哪

以探索者“非线性移动”为例，客户端表里能查到的是：

```text
roguelike_topic_table.json
  modules.rogue_3.chaos.chaosDatas.<id>
    id / name / description / level / levelName
```

但这些条目**没有** `max_hp`、`move_speed`、`attack_speed` 的实际倍率。实际数值只在
服务端生成的 BATTLE pending 里：

```text
pending[0].content.battle.unKeepBuff[]
  key = global_buff_normal
  blackboard:
    key = enemy_attr_final[tag]
    max_hp = ...
    move_speed = ...
    attack_speed = ...
```

因此“0 级 +15% / 1 级 +20%”这类等级差异不能靠客户端表枚举，必须抓真实局；
同一个范式在不同难度、不同成长层数下也可能给出不同 Blackboard。

### 5.3 当前能自动获取与不能自动获取

能自动获取：

- 当前 pending/模块状态（`chaos/disaster/wrath/weather` 的 id、等级、列表）；
- BATTLE pending 的 `unKeepBuff` 原始 key/Blackboard；
- `PreprocessGlobalBuff` 返回的最终 `GlobalBuffData` 列表（prefabKey/blackboard/source）；
- 六个主题共用的 `outerBuffs -> m_relics -> GBuff*` 路径。

不能自动获取：

- 没有真实 BATTLE pending 时，凭空生成某个岁时/年代/坍缩的完整数值；
- `passProfessionMaskFlag / professionMask / playerSideMask`：
  在当前客户端从该 Hook 结果读取会触发 `GameAssembly.dll+0x433b8d` 访问冲突；
- 服务端才会决定的难度成长、遗物替换和特殊机制组合。

## 6. 真实局抓取

进入真实肉鸽战斗后执行：

```text
rogue-dump-battle
```

输出 `build-cmake/dist/battle_rogue_dump.txt`，包含：

```text
RoguelikeInput.extraBuffs   每个条目的 id / layer / buffs[].key
RoguelikeInput.relicBuffs   藏品 buff 条目
RoguelikeInput.outerBuffs   局外成长条目
RoguelikeBattleManager.m_relicClasses
                            global_buff_* / char_* / enemy_* 的运行时 Type 映射
metaField                   BattleRoguelikeMeta 的字段类型/偏移（仅类型，不读结构体）
RoguelikeBattleManager.m_relics / m_charBuffs
preprocessGlobalBuff        Hook 捕获到的最终 List<LevelData.GlobalBuffData>
```

用途：确认服务器下发的岁时/年代/坍缩等机制最终落在哪个集合里。若落在
`extraBuffs` 或 `m_relics`，即可按 §2 的表驱动方式在本地重建。

### 6.1 最终 GlobalBuffData 的安全捕获

`RoguelikeBattleManager.m_levelData.globalBuffs` 在实际客户端中为 `null`，
不能作为最终列表来源。此前尝试主动调用 `GatherGlobalBuffs()` 在非法/伪造
`global_buff_layer` 输入下会导致崩溃，已移除，禁止恢复这条调用。

现在的做法是在战斗初始化时 Hook：

```text
Torappu.Battle.Roguelike.RoguelikeBattleManager.PreprocessGlobalBuff()
  -> List<Torappu.LevelData.GlobalBuffData>
```

只调用原函数并读取其返回值，不额外触发任何 Gather/Preprocess：

- Hook 在连接主界面时安装；若当时类尚未可用，每 5 秒重试一次。
- Hook 内只创建 GC Root 并入队，序列化在后续安全游戏线程任务中执行，
  避免在 `PreprocessGlobalBuff` 返回点递归读取托管对象。
- 序列化包含每个条目的 `prefabKey`、`blackboard` 键值以及 `source` 类名。
  掩码/布尔字段（`passProfessionMaskFlag`、`professionMask`、`playerSideMask`）
  在实机读取时触发过 `GameAssembly.dll+0x433b8d` 访问冲突，当前不读取；
  这些字段不影响岁时/年代/坍缩的数值复现。
- `rogue-net-capture start` 同时打开该捕获的落盘：
  `rogue_captures/global-buffs-<tick>.json` 和 `rogue_global_buffs_latest.json`。
- `rogue-global-buffs` 可把最近一次内存快照单独写到
  `rogue_global_buffs_latest.json`，不要求当前仍在战斗中。
- `rogue-dump-battle` 会把最近一次快照写入 `battle_rogue_dump.txt`，
  并同步写 `rogue_global_buffs_latest.json`。

本地 `rogue-capture` 只写 `rogue_captures/capture-local-<tick>.json`，
不覆盖官方局的 `rogue_capture_latest.json`；因此 TAS/本地注入不会污染真实局样本。

### 6.2 抓一个真实岁时/年代/坍缩

```text
1. 主界面执行 rogue-net-capture start
2. 正常进入带目标机制的肉鸽战斗
3. pending 变成 BATTLE 时自动排队 rogue-capture
4. PreprocessGlobalBuff 被调用时自动写 global-buffs-*.json
5. 战斗结束后查看：
   build-cmake/dist/rogue_captures/capture-*.json
   build-cmake/dist/rogue_captures/global-buffs-*.json
   build-cmake/dist/rogue_capture_latest.json
   build-cmake/dist/rogue_global_buffs_latest.json
```

`capture-*.json` 给出服务器下发的原始 `unKeepBuff`（用于复现输入），
`global-buffs-*.json` 给出游戏实际创建的 `GlobalBuffData`（用于核对 prefab 和倍率）。
两者应成对保存；只保存后者会丢失 `global_buff_stack` 等合并前的条目。

## 7. 当前验证边界

已验证：

- `rogue-player-json` 可导出完整 `PlayerDataModel`；
- `rogue-pending-detail` 可解出真实 `detailStr`；
- `m_relicClasses` 的 57 个有效注册项可在实机战斗读取；
- 真实探索者样本的 `global_buff_normal` 能生成 `GBuffNormal`；
- `PreprocessGlobalBuff` Hook 已在本地 ROGUELIKE 战斗读取最终
  `GlobalBuffData` 列表；`global_buff_normal` 与 `global_buff_layer` 均捕获到
  `count=1`，战斗继续运行；
- `global_buff_stack` 与 `global_buff_stack_base_one` 的合并结果已在本地实机复核；
- Web 配置生成器已用临时 `rogue_4` 捕获验证：匹配条目会自动变为可注入；
- 六个主题共用同一条 `outerBuffs -> m_relics` 路径。

尚未完成端到端验证：

- 本次会话已抓到真实 `PlayerDataDelta` 回调（`rogue_net/delta-*.json`），
  但样本的 `modified/deleted` 为空；还没有抓到带 `unKeepBuff` 的 BATTLE delta。
  `NotifyDataChanged` / `OnPlayerDataChanged` 双 Hook 已成功安装并触发。
- 回 Home 时 `PlayerData` 重建导致的旧缓存悬空崩溃已修复并复测。
- 当前保存的真实机制参数只有探索者“非线性移动”一组；岁时、年代、气象、
  其它坍缩范式的实际 Blackboard 仍需在后续真实 BATTLE pending 中逐个捕获。
- `m_levelData.globalBuffs` 不可用，最终列表只能通过上述 Hook 获取。

## 8. 年代 / 坍缩类型详解

### 8.1 四类机制的静态结构

客户端 `roguelike_topic_table.json` 里能直接读到的字段如下：

| 机制 | 运行时类型 | 关键字段 | 等级表达 |
| --- | --- | --- | --- |
| 探索者·坍缩 | `Torappu.RoguelikeChaosData` | `chaosId`, `level`, `nextChaosId`, `prevChaosId` | `_a` 后缀为 1 级，`level` 为 0/1 |
| 萨卡兹·年代 | `Torappu.RoguelikeDisasterData` | `id`, `level`, `levelName`, `type` | `_a/_b/_c` 对应 1/2/3 级 |
| 界园·岁时 | `Torappu.RoguelikeWrathData` | `id`, `group`, `level`, `levelName`, `isPacified` | `_a/_b/_c` 对应 1/2/3 级 |
| 黑流树海·气象 | `Torappu.RoguelikeMainWeatherData` | `id`, `level`, `levelName`, `type`, `isPositive` | `_a/_b/_c` 对应 1/2/3 级 |
| 黑流树海·实托邦方针 | `Torappu.RoguelikeSubWeatherData` | `id`, `name`, `type`, `isPositive` | 无独立等级 |

这些类型的共同点是：**只有名称、描述、等级和展示字段，没有战斗 Buff 的 Blackboard**。
`functionDesc` 里的百分比只是给玩家看的文本，不能直接当作 `max_hp=1.15` 之类参数。

### 8.2 `Module.Chaos.level` 不是单个坍缩的等级

探索者最容易混淆的是两个“等级”：

```text
rlv2.current.module.chaos.level       全局坍缩值/层级（0..8）
rlv2.current.module.chaos.chaosList   当前实际范式条目（如 rogue_3_chaos_3）
chaosDatas.<id>.level                 该范式自己的等级（0 或 1）
```

`levelInfoDict` 把全局坍缩值区间映射到 `ChaosEffectRank`，用于局内视觉/难度表现；
它不决定某个范式的 `max_hp/move_speed/attack_speed`。

真实账号样本（`rogue-player-json` 导出）：

```text
theme = rogue_3
module.chaos.level = 1
module.chaos.chaosList = ["rogue_3_chaos_3"]
chaosDatas["rogue_3_chaos_3"].level = 0
chaosDatas["rogue_3_chaos_3"].functionDesc = 生命+15%，移速+15%
```

同一局抓到的战斗输入是：

```text
global_buff_normal
  key = enemy_attr_final[tag]
  max_hp = 1.15
  move_speed = 1.15
  attack_speed = 0
```

所以：**全局坍缩等级是 1，但条目是 0 级，实际用的是 0 级数值 1.15**。
`rogue_3_chaos_3_a`（1 级）的文本是 +20%/+20%/+20，
当前 UI 里的 `1.2/1.2/20` 是根据文本和用户核对值填入的，尚未用同强度真实局捕获复核。

### 8.3 年代 / 岁时 / 气象的等级

这三类没有 `chaosList` 这种二级结构，当前状态字段是：

```text
萨卡兹   module.disaster.curDisasterId
界园     module.wrath.wraths[]
黑流树海 module.weather.currentMain / currentSub
```

条目的 `level` / `levelName` 直接来自 `_a/_b/_c` 变体。战斗侧仍然只认
`unKeepBuff`；客户端不会因为 `curDisasterId=..._b` 就现算倍率。

年代也不都是简单的属性倍率。以“魔王年代”为例，
`buff_template_data.json` 里能看到：

```text
CheckGlobalBuffExistByKey("rogue_4_disaster_buff_8")
  -> CreateBuff(rogue_4_apoptosis[not_withdrawable])
```

“年代印痕内无法主动撤退”是脚本化全局 Buff，不是 `enemy_attr_final` 的一个
`atk=1.2` 能表达的。因此不能用一个通用百分比解析器覆盖年代/岁时/气象；
必须保留服务器下发的处理器 key 和 Blackboard。

另外，`pending.content.detailStr` 解出的 run snapshot 只有
`brief / troopChars / zones / seed` 等局内记录，不含 `unKeepBuff`。
可以从中拿到真实随机种子和最终模块状态，但不能反推战斗 Buff 数值。

### 8.4 运行时处理器

`unKeepBuff[].key` 是处理器键，不是战斗 Buff 模板：

```text
global_buff_normal
  -> GBuffNormal
  -> prefabKey = blackboard.key
  -> 克隆 Blackboard 生成 LevelData.GlobalBuffData

global_buff_stack
  -> GBuffStack
  -> 同一 prefabKey 的数值相加

global_buff_stack_base_one
  -> GBuffStackBaseOne
  -> 同一 prefabKey 使用 1 + Σ(v - 1)

global_buff_layer
  -> GBuffWithLayer
  -> 按 layer 选择/覆盖

global_buff_override_blackboard
  -> GBuffOverrideBlackboard
  -> LatePreprocess 覆盖目标 Blackboard

global_buff_with_condition
  -> GBuffWithCondition
  -> 满足条件才应用
```

`RoguelikeBattleManager.PreprocessGlobalBuff()` 返回最终的
`List<LevelData.GlobalBuffData>`；这是核对“服务器原始条目”与“游戏最终结果”的
唯一可靠中间点。

本地实机（`rogue_3` + `--outerbuff`）已经验证：

```text
global_buff_stack
  max_hp 1.1 + 1.2 -> 2.3
  move_speed 1.1 + 1.2 -> 2.3

global_buff_stack_base_one
  max_hp 1.5 + 1.7 -> 2.2
  attack_speed 5 + 7 -> 11   (即 1 + (5-1) + (7-1))
```

两组最终 `GlobalBuffData` 的 `prefabKey` 分别是
`enemy_attr_final[stack]` 和 `enemy_attr_final[base]`，与输入一致。

### 8.5 从捕获到可注入

```text
真实 BATTLE pending
  -> rogue-capture 写 capture-*.json（原始 unKeepBuff）
  -> PreprocessGlobalBuff Hook 写 global-buffs-*.json（最终 GlobalBuffData）
  -> build-battle-config.js 按 active id 自动挂到对应年代/岁时/气象/坍缩条目
  -> Web 配置勾选后生成 --outerbuff=...
  -> rogue-start 本地复现
```

自动挂载规则在 `webview/tools/build-battle-config.js`：
`active.chaos.ids / active.disaster.id / active.wrath.ids / active.weather.main/sub`
匹配到表条目后，把 `captureMechanic()` 的结果写入该条目的 `mechanic`。
未捕获的条目仍显示“仅展示”，不会伪造数值。

## 9. BetterBattle 接入手段

### 9.1 动态调试

```text
rogue-player-json      导出完整 PlayerDataModel，查看 rlv2.current
rogue-pending-detail   解出 pending.content.detailStr
rogue-outbuffs         调用 GetOutBuffs() 看当前 outerBuffs
rogue-capture          读取 pending BATTLE 的 unKeepBuff
rogue-dump-battle      读取 RoguelikeInput / m_relics / m_relicClasses
rogue-global-buffs     导出最近一次 PreprocessGlobalBuff 结果
rogue-net-capture      监听 PlayerDataDelta，BATTLE pending 自动捕获
game-mode              核对 modeType/isDeterministic/extraData
```

`PreprocessGlobalBuff` Hook 的安装位置在
`plugins/BetterBattle/plugin.cpp:1703`，返回列表的序列化在
`plugins/BetterBattle/battle.cpp:6427`。

### 9.2 静态分析

```text
_tools/index_clean.json
  RoguelikeChaosData / RoguelikeDisasterData / RoguelikeWrathData
  RoguelikeMainWeatherData / RoguelikeSubWeatherData
  RoguelikeBattleManager / GBuffNormal / GBuffStack / ...

_assets/.../excel/roguelike_topic_table.json
  modules.rogue_3.chaos.chaosDatas
  modules.rogue_4.disaster.disasterData
  modules.rogue_5.wrath.wrathData
  modules.rogue_6.weather.mainWeatherData/subWeatherData

_assets/.../battle/buff_template_data.json
  rogue_3_enemy_attr_final[tag] 等实际战斗 Buff 模板
```

`RoguelikeDataUtil.GetOutBuffs()` 的反汇编已确认只读
`current.player.pending[0].content.battle.unKeepBuff`，不读
`Module.Chaos/Disaster/Wrath/Weather`。这一步是区分“客户端展示状态”和
“服务器战斗输入”的关键。

### 9.3 抓包 / 服务端增量

`rogue-net-capture start` 同时 Hook：

```text
Torappu.PlayerData.NotifyDataChanged
Torappu.PlayerData.OnPlayerDataChanged
```

抓到的是已经反序列化的 `PlayerDataDelta.modified/deleted`，写入
`build-cmake/dist/rogue_net/delta-*.json`。它不是 TLS 原文，但足以拿到
`pending[0].content.battle.unKeepBuff` 和 `Module.*` 的最终值，避免处理证书、
协议加密和版本兼容。只有需要验证“服务器原始报文格式”时，才需要外部代理抓 TLS。

场景切换时 `PlayerData` 可能被重建，旧缓存指针会失效。`player_data_model()`
现在优先重新读取 `PlayerPrefsWrapper.s_playerDataLink`，并在 `leave` /
结算回 Home 时清空缓存；否则 delta 回调在回主界面时会读悬空指针。

