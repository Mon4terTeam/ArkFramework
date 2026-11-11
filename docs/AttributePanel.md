# 属性面板与 Buff 计入方式

本文回答两个问题：

1. 能不能从运行时 Buff 数据反推“Buff 如何计入面板”。
2. 如果能，应该读取哪些字段、按什么公式合成，以及与游戏当前值如何对账。

结论：可以。当前客户端的属性合成入口和四组 Buff 修饰数组都已确认；不需要猜测
Buff 的叠加顺序。需要额外注意客户端使用 Q32.32 定点数逐步舍入并按属性上下限
clamp，因此“分解值”用于展示，最终面板值仍应以游戏 `Attributes.GetValue` 为准。

## 1. 游戏侧的真实合成入口

当前客户端 `GameAssembly.dll` 中：

| 地址 | 方法 | 作用 |
|---|---|---|
| `0x180679970` | `Torappu.Battle.Attributes::_CalculateAttributeValue` | 计算单个属性的最终值 |
| `0x180678590` | `Torappu.Battle.Attributes::GetValue` | 命中缓存或重新计算，返回最终值 |
| `0x180678490` | `Torappu.Battle.Attributes::GetRawValue` | 返回未叠加 Buff 的原始值 |
| `0x180678520` | `Torappu.Battle.Attributes::GetValueRoundToInt` | 对最终值做整数面板取整 |
| `0x1821556e0` | `SharedUtils::ProcessFinalScaler` | 最终乘算值装载时的负值修正 |
| `0x1806cf750` | `Torappu.Battle.Buff::GetValue` | 返回当前 Buff 的四组有效修饰值 |
| `0x1806da630` | `Torappu.Battle.Buff::_LoadAttributesModifier` | 把 Buff 模板中的修饰项写入四组数组 |

`_CalculateAttributeValue` 的反编译逻辑可以归纳为：

```text
A  = m_rawData[attr]
Dp = sum(每个修饰器的 attributeAddition)
Dt = sum(每个修饰器的 attributeMultiplier)
Fp = sum(每个修饰器的 attributeFinalAddition)
Ft = product(每个修饰器的 attributeFinalScaler)

Af = Ft * ((A + Dp) * max(0, 1 + Dt) + Fp)
Af = clamp(Af, GetMinValue(attr), GetMaxValue(attr))
```

其中：

- `A` 是 `Attributes.GetRawValue(attr)`，不是直接查 `character_table.json` 得到的
  角色页基础值；它通常已经包含等级、精英化、潜能、信赖以及入场前的符文/藏品基础值
  处理。不要用角色表或敌人表替代它。
- `Dp`、`Fp` 是加法类，按属性分别求和。
- `Dt` 是直接乘算类，按属性分别求和；最终使用 `1 + Dt`，并且整体下限为 `0`。
- `Ft` 是最终乘算类，按属性分别相乘。
- `max(0, 1 + Dt)` 对应反编译中的 `TSMath::Max(0, multiplierAccumulator)`。
- `clamp` 使用 `m_dataRange`，如果 `m_overrideDataRange` 有覆盖则优先使用覆盖范围。
- 整数面板显示使用 `GetValueRoundToInt`，内部是 `MathUtil.RoundToInt(GetValue(attr))`。

### 1.1 四组数组的语义

`Buff` 上的四组 `Torappu.FP[]` 按 `AttributeType` 索引：

```text
m_attributeAdditions       直接加算       Dp
m_attributeMultipliers     直接乘算       Dt
m_attributeFinalAdditions  最终加算       Fp
m_attributeFinalScalers    最终乘算       Ft
```

约定如下：

- `m_attributeMultipliers` 中的 `0.30` 表示 `+30%`，不是 `1.30`。
- `m_attributeAdditions` / `m_attributeFinalAdditions` 是绝对值，例如 `DEF + 80`。
- `m_attributeFinalScalers` 中存放的是已经过负值修正的乘数：
  - `2.00` 表示 `+100%`，最终乘 `2.00`；
  - `0.70` 表示 `-30%`，最终乘 `0.70`；
  - `1.00` 是恒等值。
- `ProcessFinalScaler(x)` 的规则是 `x < 0 ? 1 + x : x`，但它在 Buff 装载阶段
  已经执行过一次。读取运行时数组时不要再对 `0.70` 做一次 `1 + x`，否则会变成
  `1.70`。

`Buff.GetValue` 在 Buff 未启用时返回 `false`，四个输出值是 `0 / 0 / 0 / 1`；
启用时直接返回四组数组中该属性槽的当前有效值。层数、blackboard、来源实体重写等
影响已经反映在数组里，展示时不要再自行乘层数或重算 blackboard。

`m_rawData` 和 `m_cachedData` 是 `ObscuredFP[]`，不建议在插件里自行解密；直接调用
`GetRawValue` / `GetValue` 更稳，也能避免漏掉客户端的缓存和 dirty 逻辑。

## 2. 用户示例的对应关系

以“基础 100，Buff A +30% 攻击，Buff B +20% 攻击，Buff C +100% 攻击”为例：

```text
A  = 100
Buff A: m_attributeMultipliers[ATK] = 0.30
Buff B: m_attributeMultipliers[ATK] = 0.20
Buff C: m_attributeFinalScalers[ATK] = 2.00

Dp = 0
Dt = 0.30 + 0.20 = 0.50
Fp = 0
Ft = 2.00

Af = 2.00 * ((100 + 0) * (1 + 0.50) + 0)
   = 2.00 * 150
   = 300
```

如果 C 实际是“最终加算 +150”而不是“最终乘算 +100%”，则：

```text
Ft = 1.00
Fp = 150
Af = 1.00 * (100 * 1.50 + 150) = 300
```

两种写法都能得到 300，但字段完全不同。必须看 C 落在
`m_attributeFinalAdditions` 还是 `m_attributeFinalScalers`，不能只看描述里的
“+100%”。

如果 C 仍然是直接乘算 `m_attributeMultipliers[ATK] = 1.00`，则：

```text
Dt = 0.30 + 0.20 + 1.00 = 1.50
Af = 100 * (1 + 1.50) = 250
```

所以“C 在之后”在实现上意味着 C 属于最终加算或最终乘算，而不是继续进入直接乘算区。

## 3. 运行时对账示例

现有快照
`ArkFramework/build-cmake/dist/realdelta_check_snapshot.json` 中有一组可直接核对的数据：

```text
敌人 enemy_10087_hlchgr_2
基础攻击力（enemy_database.json）：500
基础移动速度（enemy_database.json）：1.2
Buff hlchgr_t[attribute_storage]：
  ATK125.00%
  MOVE_SPEED*2.2500(final)
```

快照观测：

```text
atk       = 1125
move_speed = 1.35
```

核对：

```text
ATK: 500 * (1 + 1.25) = 1125
MOVE_SPEED 属性: 1.2 * 2.25 = 2.7
Entity.get_moveSpeed 再乘全局显示系数 0.5: 2.7 * 0.5 = 1.35
```

这与 `_CalculateAttributeValue` 和 `Entity.get_moveSpeed` 的反编译结果一致。也说明
`m_attributeFinalScalers` 中的 `2.25` 是最终乘数本身，不是“再加 1”的增量。

## 4. 如何枚举实际参与计算的修饰器

不要只遍历 `Entity.<buffContainer>` 中的 Buff。当前客户端
`Attributes.m_attributeModifiers` 是：

```text
HashSet<IAttributesModifier>[]  // 按 AttributeType 索引
```

向这个集合注册的不仅是 `Buff`，还包括：

```text
Torappu.Battle.Buff
Torappu.Battle.SimpleAttributeModifier
Torappu.Battle.BasicSkill
Torappu.Battle.EnemySkill
Torappu.Battle.FeverBehaviour
Torappu.Battle.SandboxV3RecipeHolder
```

因此：

- 只加 Buff 的四组数组，通常对不上 `GetValue`，因为技能、天赋、藏品等也可能是独立
  的 `IAttributesModifier`。
- 要精确对账，应遍历 `m_attributeModifiers[attr]`，对每个对象调用其
  `GetValue(attr, out addition, out multiplier, out finalAddition, out finalScaler)`。
- 该方法的返回值是 `bool`；返回 `false` 表示本次不生效，按 `0 / 0 / 0 / 1` 处理。
- 如果对象是 `Buff`，再读取 `key`、`instanceUid`、`source` 作为展示名；其他类型至少
  显示实际类名。

`Attributes.AddModifier` 会根据修饰器的 `attributeMask` 把对象加入对应属性的
HashSet；`_CalculateAttributeValue` 只遍历该属性的 HashSet。所以按属性枚举是精确
等价于游戏计算的。

### 4.1 HashSet 读取建议

当前客户端 `System.Collections.Generic.HashSet<T>` 的字段是：

```text
_buckets
_slots       // Slot<T>[]
_count
_lastIndex
_freeList
_comparer
_version
```

建议按以下方式读取，避免硬编码 `Slot<T>` 内部偏移：

1. 从 `Attributes.m_attributeModifiers` 取 `HashSet` 对象。
2. 读取 `_slots` 数组和 `_lastIndex`。
3. 逐个取 boxed `Slot<T>`，通过反射查找 `value` 字段。
4. 跳过空值和重复对象。
5. 用对象实际类判断是否为 `Buff`，不要假设集合里只有 Buff。

这与现有 `m_buffUidMap` 通过反射读取 `Entry.value` 的做法一致。

## 5. 建议的 BetterBattle 输出

建议新增一个按需命令（例如 `panel <cardUid> [attr]`），不要放进每逻辑帧的全量快照：

```text
panel char_1045_svash2 ATK
```

输出结构建议：

```json
{
  "attribute": "ATK",
  "raw": 100.0,
  "dp": 0.0,
  "dt": 0.5,
  "fp": 0.0,
  "ft": 2.0,
  "predicted": 300.0,
  "actual": 300.0,
  "min": 0.0,
  "max": null,
  "modifiers": [
    {
      "kind": "buff",
      "key": "buff_a",
      "uid": 123,
      "source": "char_xxx",
      "addition": 0.0,
      "multiplier": 0.3,
      "final_addition": 0.0,
      "final_scaler": 1.0
    }
  ]
}
```

展示层建议同时给出：

- `A`：原始值；
- 每个修饰器的 `Dp / Dt / Fp / Ft`；
- 汇总公式和预测值；
- 游戏 `GetValue` 的实际值；
- 预测值与实际值的差，用于发现 clamp、定点舍入或未归类的修饰器。

当前 BetterBattle Web 的敌人详情把这部分直接渲染在对应属性值下面：
勾选“显示计算过程”后，只拼接实际生效的 `Dp / Dt / Fp / Ft` 项，`+0`、`×1`
和恒等项不显示；敌人 Tab 与游戏视图使用同一套面板数据。

如果要做“这个 Buff 对面板贡献多少”，不要只给一个加法差值，因为最终乘算不是线性的。
更准确的做法是：

1. 计算包含全部修饰器的 `final`。
2. 重新计算去掉目标 Buff 后的 `final_without`。
3. 展示 `final - final_without`，并同时列出该 Buff 的四个分组值。

## 6. 与 buff_viewer.dll 的关系

`buff_viewer.dll` 在 `0x18000843c` 和 `0x180008495` 分别引用了
`GetRawValue` 和 `GetValue`，并比较两者差异；它的“最终属性”来自游戏当前值，
没有自己重新合成 Buff。这样做最安全，但只能看到结果，不能解释每个 Buff 落在哪个
乘区。

本文的方案是在它之上再往下一层：仍然用 `GetValue` 作为最终值，但额外读取
`m_attributeModifiers[attr]` 和每个修饰器的 `GetValue`，因此可以同时得到
“游戏最终值”和“每个 Buff 的分组贡献”。

## 7. 注意事项

- 不要用角色表/敌人表直接当 `A`。`GetRawValue` 才是游戏当前属性管线的输入。
- 不要对 `m_attributeFinalScalers` 再做一次 `1 + x`；负值修正已在装载时完成。
- 不要自行乘 Buff 层数或重算 blackboard；以 `Buff.GetValue` 的当前返回值为准。
- 不要只统计 Buff；技能、敌人技能、Fever、RecipeHolder 等也可能在
  `m_attributeModifiers` 中。
- 属性最终值有上下限 clamp；例如 `MOVE_SPEED` 下限是 `0.1`，`ATTACK_SPEED`
  下限是 `20`。接近边界时，未 clamp 的公式值会和面板值不同。
- 客户端使用 Q32.32 定点数逐步运算，聚合顺序不同可能造成末位差异。展示预测值时
  应以 `GetValue` 为准，并把差异标出来。
- 部分实体 getter 有额外显示变换。例如 `Entity.get_moveSpeed` 会把
  `Attributes.GetValue(MOVE_SPEED)` 再乘 `0.5`。做通用分解时使用
  `Attributes.GetValue`，不要把 getter 的显示值直接当作属性管线值。
- Buff 的修饰数组会随衰减、层数、触发次数变化；按需读取，不要跨帧缓存。

