# 伤害公式采集与 Buff 归因

本文记录 BetterBattle 当前客户端的伤害公式探针、分目标统计和归因约束。

## 目标

伤害统计必须区分三件事：

1. 游戏实际提交的伤害与治疗；
2. `BattleFormula.CalculateDamageInternal` 计算出的公式值；
3. 公式之后由 Buff 伤判乘区改写出的最终值。

本地计算器只负责第 2 步的可控复算。第 3 步由当前伤害事务中的真实
`Modifier` 变化提供来源，不能用一个比例反推成“某个 Buff”。

## 当前客户端 Hook

验证客户端：

```text
E:\Games\ArkTest\Arknights\Arknights.exe
```

当前使用以下观察入口。参数名来自运行时 `method_get_param_name`，不是从
反编译猜测：

| 入口 | 原生参数 | 用途 |
|---|---:|---|
| `BattleFormula.CalculateDamage(target, source, triggerTargetEvent, triggerSourceEvent, atkInfo&)` | 5 | 建立本次伤害事务的 source/target 上下文 |
| `BattleFormula.CalculateDamageNoEvent(target, source, atkInfo)` | 3 | 无事件路径的 source/target 补全 |
| `BattleFormula.CalculateDamageInternal(atkInfo, penetrateRatio, penetrateFixed, target)` | 4 | 获取公式结果和目标实体 |
| `Entity.OnCalculateDamage(target, atkInfo&)` | `this` + 2 | 攻击方实体级 `AttackInfo` 改写 |
| `Buff.OnCalculateDamage(AttackInfo&)` / `OnBeingCalculateDamage(AttackInfo&)` | `this` + 1 | 攻击/受击侧属性改写 |
| `Buff.OnTakeDamage(Modifier&)` / `OnOutputDamage(Modifier&)` | `this` + 1 | 伤判乘区前后值 |
| `BattleLogger.LogModifier` | 事件服务 | 最终伤害/治疗事件 |
| `CalculateDamage(atk, target, damageType, atkScale, atkAddition)` | 5 | 值入口的 target 上下文 |
| `CalculateElementDamage(source, target, atkScale)` | 3 | 元素伤害的 source/target 上下文 |
| `CalculateElementDamage(atk, target, atkScale)` / `(atk, target)` | 3 / 2 | 元素值入口，只补 target |
| `CalculateHeal(source, target, healScale)` | 3 | 治疗的 source/target 上下文 |
| `CalculateHeal(atk, target, healScale)` | 3 | 治疗值入口，只补 target |

`CalculateDamage` 的第 5 个参数在栈上，v1 gate 只搬运 4 个寄存器参数，会把
`AttackInfo&` 丢失并破坏原函数 ABI。该入口改用
`create_native_hook_v2(..., 5)`；v2 gate 把 4 个寄存器参数和 4 个栈参数一起
复制到 `NativeHookCall`，再按同一 ABI 调用 detour 和 trampoline。

`CalculateDamage` 的 `target` 是第一个参数、`source` 是第二个参数；这与
`Entity.OnCalculateDamage` 的“`this` 是攻击方、参数 `target` 是受击方”相反。
`CalculateDamageInternal` 本身只给出 `target`，来源实体来自外层
`CalculateDamage`/`CalculateDamageNoEvent`/`CalculateHeal`/
`CalculateElementDamage` 压入的线程内上下文。带 source 的命中进入主队列；
只有 target 的命中（值入口、预览、命运共享真伤）进入 fallback 队列，只有
在主队列没有同目标/类型/精确值命中时才被消费。

目标 DEF、法抗和元素抗性必须在 `CalculateDamageInternal` 被调用的当下
从目标 `Entity.m_attributes` 读取。不要使用事件处理时的末帧快照：目标抗性
可能在伤害计算和事件消费之间变化，会造成公式值无法复现。

## 精确关联

公式记录与伤害事件不使用帧窗口，也不计算匹配分数。

关联键为：

```text
目标 id
目标 instanceUid
伤害类型
Q32.32 精确伤害值
```

伤判乘区通过 `Modifier` 的精确 before/after 值链关联：

```text
公式值
  -> Buff A 的 before/after
  -> Buff B 的 before/after
  -> LogModifier 的最终值
```

每个 `before/after` 都按 Q32.32 原始值比较。只有完整值链能唯一闭合时才
归因；无法唯一闭合的命中保留为未匹配，不猜一个来源。

这也是不使用帧窗口的原因：探针记录的逻辑帧是快照时钟，公式调用、弹道命中和
`LogModifier` 不一定在同一帧写入；帧差既不是稳定常量，也不能证明两次记录属于
同一笔伤害。

## 本地计算器

`damage_calculator` 复刻当前客户端的四区属性公式和基础伤害公式，内部使用
Q32.32 `int64` 而不是 binary64：

- 物理：`max(fpMul(A, fp(0.05)), A - DEF)`；
- 法术/元素：`max(min, fpMul(fpMul(A, 100 - RES), fp(float32(0.01))))`；
- 真实与治疗：直接使用基本伤害；
- 四区属性：直接加算、直接乘算、最终加算、最终乘算，全部按 Q32.32
  `fixed_mul`/`fixed_add` 折叠。

`fixed_mul` 使用 `(a * b) >> 32` 的算术右移；`fp(0.05f)` 和 `fp(0.01f)` 从
float32 位模式转换，避免 0.05/0.01 的十进制双精度误差。这样
`CalculateDamageInternal` 的公式值与本机计算器按 raw 值精确相等。

公式输入使用 `AttackInfo` 的实时攻击力和倍率。来源属性修饰列表用于 Shapley
分解；如果列表与 `AttackInfo` 的实际攻击力不一致，会明确生成
`system.unmodeled-hit-atk` 残差。

目标减伤同理：DEF/RES 的最终值来自 `CalculateDamageInternal` 调用时的实体，
来源分解来自当前 Buff 修饰快照；两者不一致时生成
`system.unmodeled-target-mitigation` 残差。

## 伤判增量

Buff 的实际 `before/after.final` 是精确 Q32.32 值。归因使用它在这一步产生的
增量，而不是事后相除得到的比例：

```text
delta_i = after_final_i - before_final_i
final   = formula + sum(delta_i)
```

链闭合时 `before_final_0 == formula`、`after_final_n == final`，所以增量之和
天然精确等于 `final - formula`，不会因为 `after/before` 的定点除法产生
几 ulp 的漂移。仍未识别的部分写入：

```text
system.post-defense-delta
```

该项表示“仍有一个真实存在但当前探针没识别出来的防御后增量”，不是具体
Buff。九人录像中的 `oxi_t_1_arcane_stack` 仍然按它自己的
`after - before` 归因，不再使用比例。

## 统计语义

- 分目标以 `目标 entityUid` 区分；同名敌人不会被合并成一个目标。
- `damage.attribution.totals` 中每个条目同时保留 `actor`、`target`、
  `target_uid`、`zone`、`source_kind`、`source_key` 和 `buff_uid`。
- Shapley 值可加，所有 zone 的 Shapley 之和应与公式模型值守恒。
- 反事实值是“去掉该来源后差多少”，不能直接相加。
- `unallocated` 是最终事件值与完整模型值之差；正常精确命中必须为 0。
- `observed_events`、`hits`、`unmatched_events` 表示事件覆盖，不能用
  `hits` 冒充全部伤害。
- `types.physical/magical/true/healing/elemental` 是已观察事件的分类计数；
  `entries` 是各原生入口的调用计数。

## 已知边界

- 护盾、无敌、闪避、HP 上限和过量治疗属于伤判后的提交阶段，当前分别统计为
  承伤、治疗或取消；它们不能被算成攻击 Buff。
- `__no_source__` 是本客户端合法的“无来源伤害”类别，不是采集失败。命运
  共享/环境真伤由 `CalculateDamageNoEvent(target, null, atkInfo)` 直接进入
  公式，按 target/type/Q32.32 值精确配对；`totals` 会为它们生成
  `zone=base`、`source_kind=base`、`source_key=base.no-source` 的基础伤害行，
  面板选择“无来源”即可看到。当前只统计到无来源事件本身，不强行把它归到
  某个干员；如果要追到触发它的 Buff（如命运共享遗物），需要单独 hook 发射
  入口。
- 只带 target 的值入口没有 source；如果外层没有 Heal/Element 作用域，
  它只能进入 fallback 队列，不会被当成有来源命中。
- 预览调用仍可能产生 target-only fallback 命中；sourced 命中优先消费，
  fallback 只在没有 sourced 命中时消费，因此不会重复归因。常量真伤的
  fallback 命中值与真实命中相同，但每条事件仍只消费一条命中。
- 来源 Buff 的属性贡献使用当前快照；快照与命中时攻击力不一致时由
  `system.unmodeled-hit-atk` 显式暴露。
- 目标属性分解使用当前快照，命中时最终值来自公式调用时的实体；差异由
  `system.unmodeled-target-mitigation` 显式暴露。
- `system.unmodeled-hit-atk` 在少量命中上会留下 `-0.00` 级残差，来源是逐帧
  Buff 快照与命中时 `AttackInfo.atk` 的微小差异；这是已记录问题，当前不处理。
- 当前 Hook 签名按本测试客户端验证；换客户端后必须重新检查入口和字段偏移。
- 事件配对和计算器都使用 Q32.32 原始值；Shapley 分摊在 Q32.32 值上计算后
  量化回定点，并把余数补给权重最大的来源，保证分摊和精确守恒。

## 实机验证

使用默认录像 `recordings/tas_ISW-NO_20261106-151217.json`：

```text
damage-formula start
damage-formula status
export-web <name>
```

最近一次完整回放结果：

```text
观察事件 279
精确配对 279
未匹配 0
未归因 0
exact_zones true
类型 physical/magical/true/healing/elemental = 2/69/162/36/10
无来源事件（事件服务的 `__no_source__`）= 178
入口 damage/noevent/internal/value/element(value-only)/heal =
  745/799/799/0/33/27+27
totals 69，全部 actor/target/target_uid 非空
```
