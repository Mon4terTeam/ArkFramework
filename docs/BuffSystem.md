# Buff 系统运行时说明

本文记录 `BetterBattle` 当前对游戏 Buff 系统的实际观测、插件接入方式和后续扩展约束。

重要前提：`E:\Games\ArkTest\机制类\仅供参考\` 下的 `buff-system.md`、`buff-container.md`
是静态分析资料，只能作为假设。本文中的“已验证”项均来自当前测试客户端
`E:\Games\ArkTest\Arknights\Arknights.exe` 的 IL2CPP 运行时读取，并使用
`recordings/sk5_9op_complete_20261103_010140.json` 在 SK-5 重放验证。

## 1. 已验证的对象关系

从已部署干员 `Character` 出发：

```text
Character (Entity)
  <buffContainer>k__BackingField : Torappu.Battle.Buff.BuffContainer
    <owner>k__BackingField       : Torappu.Battle.Entity
    m_buffs                      : Torappu.Battle.Buff.SortedDoubleBufferedBuffList
    m_buffOverrideMap            : Dictionary<string, Buff.OverrideGroup>
    m_buffUidMap                 : Dictionary<uint, Buff>
```

当前客户端运行时字段偏移：

| 类型 | 字段 | 偏移 | 运行时类型 |
|---|---|---:|---|
| `BuffContainer` | `<owner>k__BackingField` | 16 | `Torappu.Battle.Entity` |
| `BuffContainer` | `m_buffs` | 24 | `SortedDoubleBufferedBuffList` |
| `BuffContainer` | `m_buffOverrideMap` | 32 | `Dictionary<string, OverrideGroup>` |
| `BuffContainer` | `m_buffUidMap` | 40 | `Dictionary<uint, Buff>` |

`SortedDoubleBufferedBuffList` 的字段来自父类 `DoubleBufferedList<T>`：

| 字段 | 偏移 | 说明 |
|---|---:|---|
| `m_internalList` | 16 | `List<ObjectPtr<Buff>>`，当前有效元素列表 |
| `m_cachedBuffer` | 24 | 双缓冲快照缓存 |
| `m_eCounter` | 32 | 枚举代际计数 |
| `m_forwardEnumeratorPool` | 40 | 正向枚举器池 |
| `m_backwardEnumeratorPool` | 48 | 反向枚举器池 |

不要在代码里只扫描派生类字段。`m_internalList` 在父类
`Torappu.DoubleBufferedList<T>` 上，运行时反射必须继续遍历父类。

## 2. 已验证的 Buff 字段

当前客户端 `Torappu.Battle.Buff` 的关键字段：

| 字段 | 偏移 | 含义 |
|---|---:|---|
| `m_data` | 120 | `BuffData`，包含模板键、最大层数等配置 |
| `m_lifeTime` | 128 | 总生命周期，FP 秒；`-1` 表示永久 |
| `m_remainingTime` | 136 | 剩余时间，FP 秒 |
| `m_existingTime` | 144 | 已存在时间，FP 秒 |
| `m_triggerCnt` | 152 | 已触发次数 |
| `m_stackCnt` | 156 | 当前真实层数 |
| `m_maxValidStackCnt` | 160 | 属性计算允许使用的最大层数 |
| `m_started` | 505 | 是否已开始 |
| `m_isFinished` | 506 | 是否逻辑结束 |
| `m_isActuallyFinished` | 507 | 是否实际结束 |
| `m_needRemove` | 508 | 是否需要结构性移除 |
| `m_isActuallyEnabled` | 509 | 实际启用状态 |
| `m_isLateEnabled` | 510 | 是否延迟启用 |
| `m_isManuallyEnabled` | 511 | 是否由手动控制启用 |
| `m_isValid` | 512 | 当前是否有效 |
| `<key>k__BackingField` | 520 | 运行时 Buff 键 |
| `<overrideKey>k__BackingField` | 528 | 覆盖组键，可能带 `^N` 后缀 |
| `<instanceUid>k__BackingField` | 536 | Buff 实例 UID |
| `<priority>k__BackingField` | 540 | 覆盖优先级 |
| `<attributeMask>k__BackingField` | 544 | 属性修饰掩码 |
| `<abnormalFlagMask>k__BackingField` | 552 | 异常状态掩码 |
| `<effectKey>k__BackingField` | 592 | 特效键 |
| `<onEventPriority>k__BackingField` | 604 | 事件遍历优先级 |
| `<triggerable>k__BackingField` | 616 | 是否可触发 |
| `m_derivedBuffs` | 624 | 派生 Buff 列表 |

`BuffData` 中已验证可读的字段包括 `templateKey`、`maxStackCnt`。实际观察中
`templateKey` 可能是 `empty`，因此 UI 优先显示运行时 `key`，模板键只作为补充信息。

可用的无参 getter：

```text
get_remainingRatio
get_triggerInterval
get_triggerCnt
get_validStackCnt
get_isEnabled
get_isFinished
get_isValid
get_triggerable
get_isStatusResistable
get_source
```

## 3. 采集路径

### 3.1 主路径：`m_buffUidMap`

当前插件以 `m_buffUidMap` 为主采集路径。已验证的字典内部布局：

| 字段 | 偏移 |
|---|---:|
| `_buckets` | 16 |
| `_entries` | 24 |
| `_count` | 32 |
| `_freeList` | 36 |
| `_freeCount` | 40 |
| `_version` | 44 |

`_entries` 是 `Dictionary<uint, Buff>.Entry[]`。插件不硬编码 `Entry` 的偏移，而是：

1. 读取 `_entries` 数组；
2. 逐项取得 boxed `Entry`；
3. 反射查找 `Entry.value` 字段；
4. 用 `field_get_value_object` 取出 `Buff` 引用；
5. 过滤空值和重复项。

这条路径在 SK-5 重放中稳定得到实际 Buff 列表。例如：

```text
cardUid=36 character=char_1045_svash2 buffs=9
cardUid=42 character=char_1015_aglna2 buffs=14
```

### 3.2 辅助路径：`m_buffs.m_internalList`

`m_internalList` 的元素类型是 `Torappu.ObjectPtr<Buff>`，不是 `Buff` 本身。
当前实现按以下步骤读取：

- 从 `SortedDoubleBufferedBuffList` 读取父类字段 `m_internalList`；
- 不调用 `List<T>.get_Item`，而是读取 `List<T>` 的 `_items` 数组和 `_size`；
- 用数组 `GetValue` 取得 boxed `Torappu.ObjectPtr<Buff>`；
- 对 value type 的 `ObjectPtr` 使用 `object_unbox`，读取偏移 0 的 `obj`；
- 用对象实际类是否为 `Torappu.Battle.Buff` 做最终校验。

这条路径在 SK-5 上已经和 UID 字典交叉验证：同一干员 `buffs` 数量与
`list` 数量一致。600 帧附近 `buffs=6 list=6`；1228 帧附近三名场上干员分别为
`buffs=9 list=9`、`buffs=9 list=9`、`buffs=14 list=14`。更多客户端版本仍需回归。

实现时有一个容易踩的坑：`il2cpp_class_get_name` 只返回不带命名空间的短名，
`Runtime::klass_name` 才会拼上 namespace。判断 `Torappu.ObjectPtr` 和
`System.Collections.Generic.List` 必须使用后者；否则会看到 `ObjectPtr`1`/`List`1`
而匹配失败，表现为列表路径始终返回 0。

## 4. 修饰值、倍率和异常

`Buff` 持有四组 `FP[]`：

```text
m_attributeAdditions
m_attributeMultipliers
m_attributeFinalAdditions
m_attributeFinalScalers
```

每组按 `AttributeType` 索引。当前客户端观测到 38 个属性槽，名称从
`MAX_HP`、`ATK`、`DEF` 到 `SLOW_DOWN`、`BLOCK_RADIUS_SCALE`。

插件显示时做了归一化过滤：

- 加法数组跳过 `0`；
- 乘法数组跳过 `0`，非零值显示为百分比；
- 最终加法数组跳过 `0`；
- 最终乘数数组跳过 `1`，因为 `1` 是恒等值。

运行时验证到的实际文本示例：

```text
DEF+80.0000
HP_RECOVERY_PER_SEC_BY_MAX_HP_RATIO+0.0200
ATK112.00%
ATK88.00%
ATK30.00%
ATK18.00%
```

参考文档给出的合成式是：

```text
value = Ft * ((base + Dp) * max(0, 1 + Dt) + Fp)
Ft = product(normalize(each FINAL_SCALER))
normalize(x) = x < 0 ? 1 + x : x
```

插件目前只展示原始修饰项，不在浮点层重新合成最终属性。原因是客户端在 Q32.32 FP
边界进行逐步舍入和 clamp，插件不应假装自己能精确复刻；需要最终值时优先读取游戏
实体或技能/属性系统的结果。

修饰值不能跟随 Buff 的静态身份信息长期缓存。以 `tocka_s3[bonus]` 为例，同一个
实例 UID 会随 15 秒衰减过程把 `ATK` 修饰从 `120%` 逐步降到 `8%`，但 Buff 对象的
key、模板和来源不变。快照读取因此只缓存静态身份字段；已经显示修饰值或带触发间隔的
Buff 每次快照都会重新读取修饰数组，其余 Buff 在剩余时间/存在时间的 0.5 秒桶、层数
或触发计数变化时失效。当前阵容通常只有少量非空修饰 Buff，这样既能消除衰减边界上的
一帧滞后，也不会在每个逻辑帧刷新里无条件扫描全部 Buff 的四组属性数组。

异常状态由 `<abnormalFlagMask>` 表示。已实际观察到：

```text
aglna2_t_1[motion_target] -> MOTION_TARGET_FREE
```

## 5. 生命周期与显示语义

已用 SK-5 重放帧验证：

| 逻辑帧 | 观测 |
|---:|---|
| 1228 | `char_1045_svash2` 9 个 Buff，`char_1015_aglna2` 14 个 Buff |
| 1521 | `svash2` 11 个、`tocka` 11 个、`aglna2` 8 个、`haruka` 4 个、`ling` 2 个 |

临时 Buff 的时间字段确实随时间减少，例如：

```text
key=tocka_s3[bonus]
life=15.0000
remaining=13.6333 -> 10.4333
ratio=0.9089 -> 0.6956
```

永久 Buff 的生命周期和剩余时间观测为 `-1`，UI 显示“永久”。不要把 `-1` 当成
“已经结束”或“剩余 0 秒”。

UI 和状态文件使用以下显示规则：

- `enabled=0` 显示“禁用”；`valid=0` 显示“无效”；
- `life < 0` 或 `remaining < -1000` 显示“永久”；
- 层数显示 `stack/maxStack`，最大层数优先取 `BuffData.maxStackCnt`；
- 触发列显示 `triggerCnt / triggerInterval`；
- `overrideKey`、`effectKey`、`source` 放入悬停提示，避免主表过宽；
- `finished=1` 的 Buff 不进入实时快照列表。

当前客户端中 `get_validStackCnt` 对部分 `stack=1` 的 Buff 返回 `0`，而
`m_stackCnt` 和 `BuffData.maxStackCnt` 都正常。插件因此只把该 getter 当作辅助信息，
显示层以真实层数和配置上限为准。这个差异需要在后续客户端版本继续观察。

## 6. 插件接入点

### 6.1 实时快照

`refresh_battle()` 在每次刷新时为 `mine=true` 的 `Character` 读取
`<buffContainer>`，调用 `read_entity_buffs()`：

```text
BuffContainer
  -> m_buffUidMap（主路径）
  -> m_buffs.m_internalList（辅助路径）
  -> fill_buff_info()
  -> UnitInfo::buffs
```

内部刷新周期由 `refresh_frame_interval_ = 1` 驱动，随 `fixedFrameCnt` 每个逻辑帧
执行一次，即约 33.3 ms；`100 ms` 墙钟条件只是帧时钟不可用时的兜底，当前默认值也已
收紧到 33 ms。Web 快照与内部快照都在每次刷新时发布；只有 `BetterBattle.status`
文本为了减少磁盘写入而按 500 ms 落盘。当前实现每次内部刷新会读取全部友方 Buff，
但只对少量动态修饰值重新扫描四组属性数组。这样做的原因：

- Buff 列表读取使用句柄缓存和状态差分，逐帧成本保持在微秒级；
- 动态修饰值只在非空修饰、触发型或层数/时间状态改变时重读；
- Web 快照沿用逻辑帧节奏，使用独立宿主消费，不阻塞战斗主线程。

### 6.2 文件命令

```text
buff-fields <cardUid>
buffs <cardUid>
```

`buff-fields` 写入 `battle_buff_fields.txt`，用于确认当前客户端的
`BuffContainer`、`m_buffs`、`m_buffUidMap` 和 `Buff` 字段布局。

`buffs` 写入 `battle_buffs.txt`，包含每个 Buff 的 key、模板键、覆盖键、UID、
优先级、事件优先级、生命周期、层数、触发信息、修饰项和异常标记。

这两个命令都通过 `BetterBattle.request` 提交，输出目录是加载器目录
`build-cmake\dist`。

### 6.3 面板

`panel.cpp` 的“场上干员”表新增 `Buff` 数量和“Buff 状态”折叠区。展开后可查看：

- Buff UID、key；
- 启用/禁用/无效状态；
- 剩余时间或永久标记；
- 当前层数/上限；
- 触发次数和触发间隔；
- 属性修饰；
- 异常标记；
- 悬停查看 override、effect、source。

### 6.4 候选 Hook 点

以下入口来自当前客户端的运行时方法表，适合后续做事件级验证；目前插件只使用
读取快照，没有挂这些 Buff 生命周期钩子：

| 层级 | 候选入口 | 用途 |
|---|---|---|
| 容器 | `BuffContainer.CreateBuff` / `NewBuff` | Buff 创建瞬间 |
| 容器 | `BuffContainer.RemoveBuff` / `RemoveBuffs` | Buff 移除 |
| 容器 | `BuffContainer.GetBuffByUid` / `GetAllUnfinishedBuffsByKey` | 精确查询 |
| 容器 | `BuffContainer.TriggerBuffByKeys` | 外部触发 |
| Buff | `Buff.OnStart` / `OnEnable` / `OnDisable` | 开始和启用状态变化 |
| Buff | `Buff.OnTick` / `OnLateTick` | 生命周期推进 |
| Buff | `Buff.OnFinish` / `MarkFinish` | 结束和逻辑移除 |
| Buff | `Buff.Trigger` | 触发事件 |
| 帧驱动 | `BattleController.FixedUpdate` | 30 FPS 逻辑帧边界 |
| 线程泵 | `GlobalInitializerAndUpdater.Update` | 当前插件队列任务的游戏线程入口 |

挂接前必须确认目标方法的原生入口、调用线程和 xLua/hotfix 覆盖情况；不要假设
元数据里存在的方法一定走同一条原生路径。

### 6.5 单个干员的排查流程

需要分析“某个干员身上到底有什么 Buff”时，按下面顺序做：

1. 在 `BetterBattle.status` 找到该干员的 `unit` 行，记录单位 UID 和 `card=` 的卡牌 UID；
2. 写入 `buffs <cardUid>`，读取 `battle_buffs.txt` 的 `buffs=N list=M`；
3. 如果 `buffs` 和 `list` 不一致，先判断是 UID 字典和列表的生命周期时点差异，
   再用 `buff-fields <cardUid>` 检查当前客户端的字段布局；
4. 对每个 Buff 看 `key`、`override`、`life/remaining`、`stack/validStack`、
   `enabled/valid`、`modifiers` 和 `abnormal`；
5. 需要追事件时，再按 6.4 的候选入口验证 `CreateBuff/RemoveBuff/OnStart/OnFinish`，
   不要只凭一次 UI 快照推断事件顺序。

例如重放中 `cardUid=36` 的单位在 600 帧附近输出 `buffs=6 list=6`，对应
`char_1045_svash2` 的 6 个实际 Buff；到 1200 帧附近数量会随部署、技能和天赋变化。

## 7. 后续逐帧采集接口

Buff 显示目前是逻辑帧快照，不应直接改成“每逻辑帧扫描所有对象”。如果后续需要
逐帧追踪，建议先做以下基础设施：

1. **订阅式采集**：只对显式加入采集集合的干员/Buff UID 做逐帧读取；
2. **句柄缓存**：缓存 `BuffContainer`、`Buff` 的 `Root` 或稳定 UID，避免每帧重新
   遍历对象管理器和字典；
3. **状态差分**：保存上一帧的 `BuffInfo`，只在 key、层数、剩余时间、启用状态或修饰项
   变化时写事件；
4. **预分配缓冲**：复用 `vector`、`unordered_set` 和字符串缓冲，避免每帧分配；
5. **原始数组快路径**：确认当前客户端 `FP[]` 的数组数据地址布局后，直接读取连续
   FP 数据，而不是逐元素 `get_Item`；
6. **可配置预算**：限制每帧最多扫描的 Buff/字段数量，超限写入 dropped 计数；
7. **生命周期钩子**：需要严格捕获开始/结束瞬间时，再评估
   `BuffContainer.CreateBuff/RemoveBuff`、`Buff.OnStart/OnFinish` 等原生入口，
   不要用 UI 快照冒充逐帧事件流。

推荐的采集记录形式：

```text
frame, unitUid, buffUid, key, event
event = add | remove | enable | disable | stack | time | modifier | abnormal
```

## 8. 已验证与未验证边界

### 已验证

- `Entity.<buffContainer>` 可读；
- `BuffContainer` 四个关键字段和偏移可读；
- `m_buffUidMap._entries` 可枚举出实际 Buff；
- `m_buffs.m_internalList._items/_size` 可枚举同一批 Buff，且与 UID 字典数量一致；
- `Buff` 的 key、overrideKey、effectKey、UID、时间、层数、触发、启用、有效、
  可触发、可抵抗和异常掩码可读；
- 四组属性修饰数组可读；
- SK-5 重放中 Buff 数量和临时 Buff 剩余时间随逻辑帧变化；
- 多干员同时读取不会导致崩溃。

### 未验证或需要继续确认

- `List<T>` 的 `_items`/`_size` 字段偏移和 `ObjectPtr` 的 boxed 布局需要按客户端版本
  继续回归；当前版本已验证，但不能假设所有版本相同；
- `Dictionary.Entry` 的内部偏移未硬编码，依赖运行时反射 `value` 字段；
- `get_validStackCnt` 的返回值与真实层数存在不一致，原因未确认；
- 手动禁用 Buff 的实际样本尚未在录像中稳定覆盖；
- `Buff` 对象池复用、UID 重用和 `Root` 生命周期在跨战斗场景切换时需要继续验证；
- xLua/hotfix 是否覆盖 `Buff.OnStart/OnFinish` 等生命周期方法尚未做动态断点验证；
- 参考文档中的 ActionNode、事件重入、覆盖组和派生 Buff 细节尚未全部做运行时对照；
- `BattleController.state=3` 不能单独作为战斗失败判据。

## 9. 验证记录

本次验证使用：

```text
客户端：E:\Games\ArkTest\Arknights\Arknights.exe
关卡：wk_armor_5（SK-5）
录像：ArkFramework\recordings\sk5_9op_complete_20261103_010140.json
逻辑帧率：30 FPS
```

重放到约 1200 帧后暂停，读取 `BetterBattle.status` 和 `battle_buffs.txt`；
再继续到约 1500 帧后暂停，确认 Buff 数量和时间字段发生变化；继续到约 2700 帧时
场上 7 名干员都能返回 Buff 数量（3 到 10 个不等），进程保持响应。未使用截图作为判据。
在 600 帧附近的单干员检查中，`buffs 36` 同时得到 `buffs=6 list=6`，确认列表路径
和 UID 字典路径返回同一批 Buff。
最终回放执行完全部 `50/50` 个操作，最后帧 `6841`。战斗结束后
`state=3`、`playing=0`；按项目约定这不单独表示战斗失败。本地战斗现在会跳过
FinishBattle 服务同步并直接返回 HOME，不再出现 `400` 上传记录失败。

## 10. 静态名称目录与效果展示

Web 控制台通过 `ui/data/buff-catalog.tsv` 补全 Buff 名称、描述和来源。
文件是 UTF-8、Tab 分隔，可直接用文本编辑器或表格软件修改；列定义见
`webview/ui/data/buff-catalog.README.md`。

- `buff` 行来自 `buff_viewer.dll` 内置的技能/天赋目录，按键归一化后匹配运行时
  `Buff.key`。
- `skill` 行保留 `skill_table.json` 的完整技能 ID，用于处理 `skchr_*` 形式的键。
- `relic` 行是 6 个肉鸽主题的 2341 件藏品；`relic-effects.tsv` 保存
  “效果锚点 -> 候选藏品”的静态索引。
- 运行时快照中的 `blackboard_entries` 和 `modifier_entries` 是结构化数据；
  `blackboard` / `modifiers` 字符串仍保留给文本输出。
- 效果按四组属性数组分类：基础加值、倍率、最终加值、最终倍率；再按数值符号
  标记为“加成 / 减益 / 混合 / 未分类”。
- 单位异常显示是当前有效 Buff 掩码的汇总。实体层本身只有计数，没有统一的
  剩余时间；剩余时间、层数和触发次数来自具体 Buff。
- 全局效果来自 `BattleController.m_globalBuffs`；肉鸽主题和藏品来自
  `m_gameMode.m_input.meta.topicId` 与 `relicBuffs`，藏品 Buff 的真实键还会从
  其 `blackboard.key` 补充。

