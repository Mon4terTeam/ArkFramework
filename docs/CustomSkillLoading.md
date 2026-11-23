# 自定义技能加载与运行时

本文是自定义技能的唯一入口文档，覆盖：

- 原生 `SkillData` 替换；
- `custom-complex` 的 JSON 技能；
- BuffData / ActionNode 图的结构；
- 宿主 prefab 与 ActionNode 的职责边界；
- AMMO、攻击范围等宿主侧例外；
- 调试、验证和常见故障。

实现文件：

- `custom/runtime/complex_skill.cpp`
- `custom/runtime/custom_graph.cpp`
- `custom/runtime/custom_module.cpp`
- `custom/runtime/custom_commands.cpp`
- `custom/plugin/custom_plugin.cpp`
- `custom/runtime/custom_complex_arc.json`

构建时 `custom_complex_arc.json` 会被复制到
`build-cmake/dist/custom_complex_arc.json`；插件运行时从 dist 根目录读取。
运行时模块是独立的 `plugins/CustomSkills.dll`，命令入口为
`build-cmake/dist/CustomSkills.request`，状态为
`build-cmake/dist/CustomSkills.status`。

---

## 1. 两条加载路径

### 1.1 直接替换原生技能数据

适合“只换技能 ID / 技能类型 / 持续时间 / 范围”的场景。

| 命令 | 作用 |
| --- | --- |
| `custom-load-skill <skillId> <skillAllLvl> <skillSpecialLvl>` | 从当前客户端数据库读取完整原生 `SkillData`，不要求源干员在编队或场上 |
| `custom-card-skill <cardUid> <skillId> [skillAllLvl] [skillSpecialLvl]` | 把原生 `SkillData` 复制到待部署卡牌的 `BattleCharacterData.mainSkill` |
| `custom-card-duration <cardUid> NONE\|AMMO <nativeSkillId> [skillAllLvl] [skillSpecialLvl]` | 带 `durationType` 校验地选择真实原生 prefab |
| `custom-skill-type <unitUid\|cardUid> PASSIVE\|MANUAL\|AUTO` | 修改目标自己的 `SkillData.skillType`，只改触发资格，不换 prefab |
| `custom-range <characterUid>` | 读取目标当前模式范围 |
| `custom-range-id <characterUid> <rangeId> [modeIndex]` | 直接改目标自己的 `UnitMode.m_rangeId` 并刷新范围 |
| `custom-skill-range <characterUid> <rangeId> [modeIndex]` | 只在技能 `isAffecting=true` 时应用范围，结束后恢复 |

关键边界：

- `durationType` 必须与真实 prefab 一致。`BattleController.CreateSkill` 只把
  `SkillData.prefabKey` 交给 `BattleFactory.CreateSkill`，不会按枚举换运行时类。
- `MANUAL/AUTO` prefab 只改标签不能变成真正的 `PASSIVE`；真实被动要用
  `custom-card-skill` 选择带原生 PASSIVE prefab 的技能 ID。
- 部署前传卡牌 UID，部署后传单位 UID；两条命令的 UID 含义不同。

### 1.2 `custom-complex`：原生 prefab + JSON Buff/ActionNode

适合“技能生命周期和攻击方式沿用原生 prefab，但效果、数值、阶段和 Buff 图全部自定义”的场景。

```text
custom-complex <cardUid> [jsonFile]
```

- 默认 JSON：`custom_complex_arc.json`
- 只能在卡牌 `state=1`（待部署）时安装
- 同一条命令可在待部署卡牌上重复安装不同 JSON
- 已部署单位要换配置，必须重开战斗后重新安装
- 修改 JSON 不需要重新编译 DLL；C++ 只保留 ABI 胶水

默认情况下只有两个 ABI 级 hook：

1. `BattleFactory.CreateSkill`：把卡牌技能替换成 JSON 指定的宿主 prefab。
2. `Buff._LoadActions`：在原生 Buff 装载 ActionNode 时注入 JSON 图。

`Buff._LoadActions` 在 `ON_BUFF_START` 之前执行，所以宿主 Buff 和后续 ActionNode
创建的 Buff 都能拿到 JSON 图。只有把 `runtime.ammoConsume` 显式设为 `hook` 时，
才会额外安装 `Entity.OnOutputAttackOrHeal` 作为耗弹兜底；默认 `actionNode` 不装。

### 1.3 prefab 选型的影响

`custom-complex` 的“基底”是**技能 prefab**（`skchr_*`），不是干员 prefab。
`nativePrefabKey` 会直接影响下面这些行为，JSON 并不能全部覆盖：

1. **能不能装载**：prefab 必须有 `BasicSkill` 和带 `_buffs` 的
   `ActiveBuffAbility`。例如 `skchr_angel_1` 只有 `ProjectileAbility + BasicSkill`，
   当前 loader 会因找不到 `ActiveBuffAbility` 而失败。
2. **运行时类和生命周期**：AMMO、普通限时/无限、过载、充能等语义由 prefab 组件
   决定；JSON 只是在创建出来的实例上覆盖字段。
3. **从原生 `SkillData` 继承的字段**：loader 用
   `DataConvertUtil.GetSkillData(nativePrefabKey)` 复制原生数据，只覆盖
   `skillId`、`prefabKey`、`name`、`description`、`rangeId`、`duration`、
   `durationType`、`skillType`、`spCost`、`initSp`、`maxChargeTime`、
   `increment` 和黑板。`spType`、图标、隐藏标记等仍来自宿主；要改回费类型，
   部署后用 `custom-sp-type`，或者直接选 `spType` 正确的宿主。
4. **实例上会被强制改写的字段**：
   `_attackBlackboardModeIndex=-1`、`_modeIndex=-1`、
   `_checkBulletProgressValid=false`、`_finishSkillWithProgress=true`、
   `_showSpAsBulletMode=true`、`_hideProgressOnAffect=true`、
   `_canDiscardRemainingCount=true`；关闭开始动画和语音；清空
   `_buffs`、`_passiveBuffs`、`_activeBuffs`、`m_buffInsts`。
5. **不会被清理、会继续影响行为的字段和组件**：
   `_isOverloadSkill`、`_extraAbilities`、`_extraModeIndex`、
   `_fetchFromMainAttack`、`_fromMainRawAttack`、`_useExtraModeProgress`、
   `_burstOnOverload`，以及技能 prefab 上额外的 `ProjectileAbility` /
   `ActionAbility` 组件；`BuffAbility` 的 `_preDelay`、`_waitForCasting`、
   `_interruptAbilityOnDetach` 和 selector 也不会被重写。比如
   `skchr_gnosis_2`、`skchr_oxi_2` 的 `_extraAbilities` 非空，复用前要先确认
   这些能力会不会自己生效。
6. **攻击、目标和模式**：技能 prefab 自带的额外能力组件会保留；干员自己的攻击
   组件和 `_modes` 仍在干员 prefab 上。不要用治疗/辅助基底去期待它打伤害。
7. **事件**：事件枚举是全局的，不是某个 prefab 私有的；宿主只决定“哪些事件源
   存在”。没有攻击就不会有 `ON_AFTER_ATTACK`，没有伤害输出就不会有
   `ON_OUTPUT_DAMAGE`，没有技能就不会有 `ON_SKILL_*`。详见 4.2。
8. **表现**：技能图标、动画、音效和语音大多沿用宿主；loader 只关闭开始动画和
   语音。
9. **`durationType` 必须与宿主真实类型一致**（`AMMO` 对 `AMMO`，`NONE` 对
   `NONE`），否则数据里的持续类型和运行时类不匹配。

`skchr_angel_1` 的具体情况：

| prefab | 组件 | 技能的 Buff 载体 | 当前 `custom-complex` |
| --- | --- | --- | --- |
| `skchr_angel_1`（冲锋模式） | `ProjectileAbility` + `BasicSkill` | 无 `_buffs`；`ProjectileAbility._activeBuffs` 为空 | 找不到 `ActiveBuffAbility`，无法把 JSON host Buff 挂上去 |
| `skchr_angel_2`（扫射模式） | `BasicSkill` + `BuffAbility` | `BuffAbility._buffs=[angel_s_2]` | 可以替换 `_buffs` |
| `skchr_mcnist_2`（协防术式） | `BasicSkill` + `BuffAbility` | `BuffAbility._buffs=[mcnist_s_2]` | 可以替换 `_buffs`，当前 AMMO 基底 |

`skchr_angel_1` 不是“坏 prefab”，也不是游戏不允许自定义。它是一个 0 秒、
自动触发的“下一次攻击变成三连射”技能，效果由 `ProjectileAbility`
（`projectile_angel`、`_atkScaleKey=atk_scale`）和 `BasicSkill` 直接完成，
没有持续 Buff，所以没有 `BuffAbility`。

`ActiveBuffAbility` 的作用是“持有一组 `BuffData` 并把它们挂到 owner/目标上”。
当前 loader 的桥接方式是：

```text
CreateSkill 创建宿主 prefab
  -> GetComponent<ActiveBuffAbility>()
  -> 清空 _buffs/_passiveBuffs/_activeBuffs/m_buffInsts
  -> 把 _buffs 替换成 [JSON host Buff]
  -> 由 BuffAbility 的生命周期创建这个 Buff
  -> Buff._LoadActions 再把 JSON 图注入这个 Buff
```

所以“装不上”的准确含义是：**当前 loader 找不到放置 host Buff 的 `_buffs`
数组**，而不是这个技能不能自定义。换成“直接给 owner `AddBuff`”或“运行时补一个
`ActiveBuffAbility`”的桥接方式，`skchr_angel_1` 也可以承载同一套 JSON 图。
具体方案见 1.5。

结论：优先选结构干净、职责接近的宿主。当前 `skchr_mcnist_2` 只有
`BasicSkill + BuffAbility`，`_extraAbilities` 为空，是较安全的 AMMO 基底。
换宿主后必须重新核对上述遗留字段，不要假设 JSON 会覆盖一切。

### 1.4 天赋不是技能加载路径

- 技能只有 `BattleCharacterData.mainSkill` 一个 `SkillData`；天赋是
  `BattleCharacterData.talents` 列表，每个天赋还有多个候选（精英阶段/潜能），
  候选里才有 `prefabKey`、`blackboard`、`requiredPotentialRank`、
  `unlockCondition`。
- 天赋能力 prefab 在干员 prefab 的 `char_xxx/Talents/N` 下，常见
  `BuffAbility`、`PassiveBuffAbility`、`ToggleBuffAbility`；技能在 `skills`
  bundle 的 `skchr_*` prefab。两者装载点和生命周期不同。
- 当前 `CustomSkills` 没有 `custom-talent` 命令，也没有天赋注入 hook；
  `read_talents_from_data` 只读，用于面板显示。
- 可以用 `custom-graph <uid> <jsonFile>` + `custom-apply <uid> <jsonFile>
  <buffName>` 把自定义 Buff 挂到已部署单位，模拟“攻击力 +8%”这类简单天赋
  效果；但它不是真天赋：没有天赋图标/描述，不参与潜能/精英候选选择，不会在
  部署或再部署时自动挂载。
- 真要做天赋自定义，需要像 `custom-card-skill` 一样，在待部署时复制并改写
  `BattleCharacterData.talents` 的候选（`prefabKey`/`blackboard`/名称/描述），
  再让原生天赋装载链路创建对应 ability。这条路径目前尚未实现。

### 1.5 通用化：不要要求宿主自带 `ActiveBuffAbility`

当前 `custom-complex` 把“技能 shell”和“Buff 图”绑在一个 prefab 上：
`CreateSkill` 创建宿主，`ActiveBuffAbility._buffs` 负责创建 host Buff，
`Buff._LoadActions` 再注入 JSON 图。这个设计对 `skchr_mcnist_2` 够用，但对
`skchr_angel_1` 这种没有 BuffAbility 的 shell 就不成立。

通用系统应该把两件事拆开：

- **skill shell**：负责生命周期、技能进度、攻击/投射物、模式、弹药/过载计数；
- **effect graph**：负责 BuffData、ActionNode、事件槽位。

host Buff 的挂载至少可以有四种适配方式：

| 方式 | 做法 | 优点 | 代价 |
| --- | --- | --- | --- |
| `activeBuffAbility` | 替换宿主 `ActiveBuffAbility._buffs` | 最贴近原生，当前已实现 | 要求宿主自带该组件 |
| `ownerBuff` | 技能开始时 `Entity.AddBuff(hostBuffData, owner, skillAbility, ...)`，技能结束时 `FinishBuff` | 不要求宿主有 BuffAbility，`angel_1` 也能用 | 必须自己管生命周期和 source/ability 参数 |
| `addComponent` | 运行时 `AddComponent<ActiveBuffAbility>`，再注册到技能 ability 列表 | 保留原生 BuffAbility 生命周期 | 需要正确初始化和注册，容易漏字段 |
| `customAbility` | 写一个专用的技能 ability，自己持有图并派发事件 | 最通用、最可控 | 工程量最大 |

建议的 JSON 形态（尚未实现）：

```json
{
  "shell": {
    "prefabKey": "skchr_angel_1",
    "durationType": "NONE",
    "capabilities": ["attack", "projectile"],
    "hostMode": "ownerBuff"
  },
  "buffs": {},
  "actions": {}
}
```

loader 在安装前校验 `capabilities`：`attack` 需要攻击组件，`projectile` 需要
投射物，`ammo` 需要 `AbilityEventCounter`，`overload` 需要
`_isOverloadSkill=1`，`charge` 需要可读的 `availableCnt`。缺什么就在编辑器里
禁用对应事件/节点，而不是等到运行时静默不触发。

`ownerBuff` 方式要特别注意：

- `AddBuff` 的 `source`/`ability` 参数要传对。`BUFF_SOURCE`、
  `MODIFIER_SOURCE` 这些选择器依赖它；传错会让反击、转移伤害、来源过滤打到
  自己或友方。
- host Buff 必须在技能结束、撤退、单位死亡时结束；否则会跨技能残留。
- 一个单位可能同时有多个技能实例，Buff 句柄要按 owner + skill 实例保存，
  不要用全局单例。

---

## 2. `custom-complex` 的加载流程

```text
读取 JSON
  -> 安装 CreateSkill / Buff._LoadActions hook
  -> 构造 BuffData、ActionNode 图和事件槽位
  -> 用 DataConvertUtil.GetSkillData(nativePrefabKey) 读原生 SkillData
  -> Duplicate + 覆盖 skillId/prefabKey/name/description/range/duration/type/
     spCost/initSp/maxChargeTime/increment/blackboard（不覆盖 spType）
  -> 写入待部署卡牌的 m_data.mainSkill
  -> 部署时 CreateSkill hook 创建宿主 prefab
  -> 运行时替换宿主技能组件的 _buffs 为 JSON host Buff
  -> host Buff 的 ON_BUFF_START 创建 phase1
  -> phase1 的 ActionNode 负责停手、反击、冻结、阶段切换
  -> phase2 的 Buff 负责倍率、寒冷、吸血、护盾、攻击间隔
  -> complex_tick 每帧检查 rangeOverrides 并切换/恢复范围
```

`complex_skill.cpp` 在注入宿主技能时还会：

- 清空宿主 prefab 原来的 `_buffs`、`_passiveBuffs`、`_activeBuffs`、`m_buffInsts`；
- 写入 `_attackBlackboardModeIndex = -1`、`_modeIndex = -1`，避免宿主 prefab 自己的模式切换；
- 关闭技能开始动画和语音；
- 把 `ammoCount` 写入 prefab 内 `AbilityEventCounter` 的
  `_triggerTimeCount / m_maxCount / m_originalMaxCount`；
- 保留 `runtime.rangeOverrides` 的 C++ 范围覆盖。

这意味着复用他人 prefab 时，原始 Buff 效果会被整体替换；只保留 prefab 的
生命周期、攻击组件、计数器、投射物和模式能力。

---

## 3. JSON 结构

顶层：

```json
{
  "skill": {},
  "runtime": {},
  "buffs": {},
  "actions": {}
}
```

`hook` 字段只对通用的 `custom-graph` / `custom-apply` 命令有意义；
`custom-complex` 固定使用上面两个 hook。

### 3.1 `skill`

```json
{
  "virtualPrefabKey": "custom_complex_arc",
  "nativePrefabKey": "skchr_mcnist_2",
  "graphFile": "custom_complex_arc.json",
  "name": "复合测试技能",
  "description": "……",
  "rangeId": "3-3",
  "duration": -1.0,
  "durationType": "AMMO",
  "skillType": "MANUAL",
  "ammoCount": 16,
  "sp": {
    "cost": 0,
    "init": 0,
    "maxChargeTime": 1,
    "increment": 1.0
  },
  "blackboard": {
    "trigger_time": "$ammoCount",
    "charge_time": 10.0
  },
  "removeBlackboardKeys": ["atk", "atk_scale", "hp_ratio", "range_radius", "not_combat"]
}
```

字段说明：

- `nativePrefabKey`：真实原生技能 ID；决定运行时组件类、攻击方式、计数器来源。
- `virtualPrefabKey`：写回技能数据的虚拟 ID，避免看起来像原干员技能。
- `graphFile`：实际 Buff/ActionNode 图文件；默认就是命令传入的 JSON。
- `rangeId`：技能数据范围 ID；必须能被当前客户端加载。
- `durationType`：必须与宿主 prefab 的真实类型一致。
- `ammoCount`：整数，`1..9999`；写入原生弹药计数器，不是 Buff 层数。
- `blackboard`：写入技能黑板。值可以是数字，也可以是
  `$ammoCount` / `$duration` / `$spCost`。
- `removeBlackboardKeys`：先删除原生技能黑板的键，再写入自定义值。
  复用他人 prefab 时，这一步用来去掉原技能的 ATK、屏障、射程等效果。

### 3.2 `runtime`

```json
{
  "hostBuff": "host",
  "ammoConsume": "actionNode",
  "ammoRuleBuff": "ammo_rule",
  "rangeOverrides": [
    {
      "triggerBuff": "phase2",
      "rangeId": "3-3",
      "restoreOnExit": true
    }
  ]
}
```

- `hostBuff`：必须是 `buffs` 里的名称（不是 `key`）。
- `ammoConsume`：
  - `actionNode`：默认，由 JSON 里的耗弹节点驱动；
  - `hook`：额外安装 `Entity.OnOutputAttackOrHeal` 兜底 hook，必须提供 `ammoRuleBuff`；
  - `none`：不安装兜底，也不自动创建耗弹 Buff。
- `rangeOverrides`：`triggerBuff` 必须是 `buffs` 名称；`rangeId` 省略时用
  `skill.rangeId`；`restoreOnExit` 控制 Buff 消失后是否恢复原范围。

### 3.3 `buffs`

`buffs` 的键是“图名称”，`key` 是运行时 `buffKey`：

```json
{
  "phase1": {
    "key": "custom_complex_arc[phase1]",
    "lifeTime": 0.0,
    "lifeTimeType": "LIMITED",
    "durationKey": "charge_time",
    "maxStackCnt": 1,
    "disableOverride": true,
    "blackboard": {
      "retaliate_atk_scale": 3.5,
      "c2e_freeze": 2.0
    },
    "attributes": {
      "abnormalFlags": ["DISARMED"]
    }
  }
}
```

常用 BuffData 字段：

- `key`：运行时 buffKey；`$buffKey` 引用它。
- `lifeTimeType`：`LIMITED` / `INFINITY` 等。
- `lifeTime`：默认寿命；有 `durationKey` 时由黑板覆盖。
- `durationKey`：从技能/父 Buff 黑板读取持续时间。
- `maxStackCnt` / `maxValidStackCnt`：叠层上限。
- `overrideType`：`STACK` 等覆盖方式。
- `disableOverride`：是否禁用覆盖。
- `blackboard`：写入 BuffData 的 `List<DataPair>`，值必须是数字。
- `attributes`：写入 `BuffData.attributes`，支持 `abnormalFlags`、
  `abnormalImmunes`、`attributeModifiers` 等。

`attributes.attributeModifiers` 的元素是原生 `AttributeModifierData`，
常用字段：

```json
{
  "attributeType": "ATK",
  "formulaItem": "ADDITION",
  "loadFromBlackboard": true,
  "value": 0.0
}
```

其他 BuffData 字段只要当前客户端类型里存在，也可以直接写在 Buff 对象里；
字段名必须与 IL2CPP 类型一致，写错会在安装时报错。

### 3.4 `actions`

键是 Buff 图名称，值与 `buffs` 名称对应：

```json
{
  "phase1": {
    "ON_TAKE_DAMAGE": [
      {
        "type": "IfDamageTargetSide",
        "fields": {
          "_sideMask": "ENEMY",
          "_sourceType": "MODIFIER_SOURCE",
          "_targetType": "MODIFIER_TARGET"
        }
      }
    ]
  }
}
```

节点结构：

```json
{
  "type": "CreateBuff",
  "fields": {
    "_buff": { "$buff": "phase2" },
    "_buffOwner": "BUFF_OWNER"
  }
}
```

- `type`：ActionNode 短类名，例如 `IfElse`、`CreateBuff`、`CreateBuffById`、
  `AdvancedApplyDamage`、`AtkScaleUp`、`HealViaDamage`、`FilterByBuffStackCount`。
- `fields`：字段名必须与节点类一致；枚举可以写字符串。
- 子节点：`IfElse` 使用 `_conditionNode`、`_succeedNodes`、`_failNodes`；
  其他节点按原生字段名嵌套。
- `$buff`：引用 `buffs` 图名称。
- `$buffKey`：引用运行时 buffKey。
- 节点数组按顺序执行；节点返回 `false` 会停止后续节点。

常用事件：

- `ON_BUFF_START` / `ON_BUFF_FINISH`
- `ON_TAKE_DAMAGE`
- `ON_OUTPUT_DAMAGE` / `ON_AFTER_OUTPUT_DAMAGE`
- `ON_CALCULATE_DAMAGE`
- `ON_OUTPUT_ATK_OR_HEAL`
- `ON_AFTER_ATTACK`
- `ON_APPLYING_MODIFIER`
- `ON_ES_OVER_ZERO`

ActionNode 词汇表和字段可从静态提取数据查询：

- `E:\Games\ArkTest\_analysis\action_graphs\actionNodes.jsonl`
- `E:\Games\ArkTest\_analysis\action_graphs\actionNodeTypes.jsonl`
- `E:\Games\ArkTest\_analysis\action_graphs\nodes.jsonl`
- `E:\Games\ArkTest\_tools\action_node_catalog_scan.py`

不要凭感觉写节点字段；先在原生 Buff 图里找到同效果节点，再复制字段和事件。

---

## 4. 宿主 prefab 与 ActionNode 的职责边界

宿主 prefab 负责：

- 技能生命周期、`isAffecting`、技能结束；
- 原生 AMMO 计数器 `AbilityEventCounter`；
- 攻击组件、投射物、模式；
- 技能数据里的范围 ID。

ActionNode 负责：

- 停手、受击过滤、反击；
- 冻结、寒冷、治疗、护盾；
- 阶段切换、Buff 生命周期；
- 伤害倍率、攻击间隔属性。

C++ 只负责：

- `BattleFactory.CreateSkill` 注入宿主 prefab；
- `Buff._LoadActions` 注入 JSON 图；
- AMMO 计数器和范围覆盖这两个用户明确允许的宿主侧例外。

不要为了一个效果新增 hook。先用原生 Buff 模板 + ActionNode 组合；只有游戏原本没有的
原子行为才考虑 hook。

### 4.1 蓄力、过载、层数与 prefab 的绑定关系

这些机制不是同一种东西，不能一概而论：

| 机制 | 数据/实现位置 | 是否需要专用 prefab | 原生参考 |
| --- | --- | --- | --- |
| 充能次数、蓄力次数 | `spData.maxChargeTime`；loader 会写入 | 不需要专用“蓄力 prefab”。次数由 `SkillData` 驱动，宿主运行时类需支持 `availableCnt` | 灵知 S2 为 2；重岳 S1、松果 S1 为 3 |
| 蓄力/满充能的额外效果 | ActionNode 按当前可用次数或 SP 分支 | 不需要专用 prefab | 重岳 S1 的 `chyue_s_1_enhance_judge`：`CompareCharSkillAvailableCnt(GE,3)` 后 `CreateBuff` + `ClearCharacterSp`；词汇表里也有 `FilterChargeTimes` |
| 过载 | prefab 上的 `_isOverloadSkill`、`m_isOverloaded`、过载进度源 | **需要**。当前 loader 不写 `_isOverloadSkill` | `skchr_horn_2`、`skchr_horn_3`、`skchr_oxi_3`、`skchr_rockr_2`、`skchr_skybx_2`、`skchr_tmslot_2`、`skchr_wisdel_2` 的原生 prefab 该字段为 1；`tmslot_s_2[mark]` 使用 `ForceEnterSkillOverloadProgress` |
| Buff 层数 | JSON BuffData 的 `maxStackCnt`、`maxValidStackCnt`、`overrideType` | 不需要。任意宿主都能挂 JSON Buff | `FilterByBuffStackCount` 原生节点；当前 `counter` Buff |
| 技能充能层数 | `spData.maxChargeTime`，即“可充能 X 次” | 不需要专用 prefab | 松果 S1 `maxChargeTime=3`；重岳 S1 |
| 弹药数 | prefab 内的 `AbilityEventCounter` / `_progressSource` | **需要**。普通宿主没有弹药计数器，写入会落空 | 机械师 S2 `skchr_mcnist_2`；灰烬 S2、号角 S2 |
| 过载进度 | prefab 的 progress source 和过载方法 | **需要** | 号角 S2/S3 |

实际选择时：

- 只要“可充能 X 次”，把 `sp.maxChargeTime` 设成 X 即可；技能释放后按原生规则
  消耗一层。要区分“满层释放”的额外效果，在 Buff 图里用
  `CompareCharSkillAvailableCnt` 或 `FilterChargeTimes` 分支，不要把层数写死。
- 要“过载”，先选一个 `_isOverloadSkill=1` 的宿主；只靠 JSON 或
  `ForceEnterSkillOverloadProgress` 不能替代 prefab 的过载进度源和结束语义。
- 能天使 S3 的名字叫“过载模式”，但它的 prefab `_isOverloadSkill` 并不是 1，
  只是一个普通 15 秒技能；判断机制要看 prefab 字段，不要看技能名。
- 要“Buff 层数”，用 BuffData 的 `maxStackCnt`；要“弹药数”，用 AMMO 宿主 +
  `ammoCount`；两者不是同一种层数。
- `spType`（自动回复/攻击回复/受击回复）也不由 JSON 暴露，随宿主继承；需要改时
  用 `custom-sp-type`，或者选 `spType` 匹配的宿主。

### 4.2 事件模型：事件枚举、事件源与 Buff 槽位

先说结论：**事件枚举是全局的，事件源才是 prefab 相关的**。当前代码里没有
“某个 prefab 私有的事件表”，只有 `Torappu.Battle.Buff/Event` 这一份全局枚举。

`Buff._LoadActions` 接收的是 `BuffTemplate.EventToActionMap`；JSON 图按
`事件名 -> 节点数组` 写入。运行时 `Buff.m_actions` 是一个按 `Buff.Event` 索引的
数组，`build_event_graph` 负责把事件名解析成枚举下标；写不存在的事件名会直接
报错。所以能订阅哪些事件，取决于全局枚举；事件会不会真的发生，取决于事件源。

常见事件源：

| 事件源 | 需要什么 | 典型事件 |
| --- | --- | --- |
| Buff 自身生命周期 | host Buff 已创建并启用 | `ON_BUFF_START`、`ON_BUFF_TRIGGER`、`ON_BUFF_FINISH`、`ON_BUFF_ENABLE`、`ON_BUFF_DISABLE`、`ON_BUFF_LATE_ENABLE` |
| owner 生命周期 | 活着的单位 | `ON_OWNER_BORN`、`ON_OWNER_POST_BORN`、`ON_OWNER_KILLED`、`ON_OWNER_BEFORE_DEAD`、`ON_OWNER_REBORN`、`ON_OWNER_HP_FULL` |
| 受击/伤害管线 | 单位能受到伤害 | `ON_TAKE_DAMAGE`、`ON_BEFORE_TRY_SET_HP_ZERO`、`ON_CALCULATE_DAMAGE`、`ON_AFTER_CALCULATE_DAMAGE`、`ON_BEING_CALCULATE_DAMAGE`、`ON_EVADE_DAMAGE`、`ON_BLOCK_DAMAGE` |
| 攻击/投射物 | 有攻击组件/投射物 | `ON_BEFORE_ATTACK`、`ON_AFTER_ATTACK`、`ON_OUTPUT_DAMAGE`、`ON_AFTER_OUTPUT_DAMAGE`、`ON_OUTPUT_ATK_OR_HEAL`、`ON_PROJECTILE_BORN`、`ON_PROJECTILE_HIT_TARGET` |
| 技能 | 有技能实例 | `ON_SKILL_START`、`ON_SKILL_FINISH`、`ON_SKILL_CAST_SUCCEED`、`ON_SKILL_RETRIGGERED`、`ON_TOGGLE_SKILL_START` |
| 属性修饰 | 属性修饰管线 | `ON_APPLYING_MODIFIER`、`ON_APPLIED_MODIFIER`、`ON_OUTPUT_MODIFIER`、`ON_BEFORE_APPLYING_MODIFIER`、`ON_APPLYING_SKIPPED_MODIFIER` |
| 护盾/EP | 有 ES/EP | `ON_ES_OVER_ZERO`、`ON_EP_BREAK_START`、`ON_EP_BREAK_FINISH`、`ON_TAKE_EP_DAMAGE` |
| 模式/地图 | 有模式或地块系统 | `ON_UNIT_SWITCH_MODE`、`ON_MOTION_MODE_CHANGED`、`ON_OWNER_BLOCK_MODE_CHANGED`、`ON_OWNER_ROOT_TILE_CHANGED` |

因此：

- `ON_BUFF_START/FINISH`、`ON_TAKE_DAMAGE`、`ON_CALCULATE_DAMAGE` 这类事件
  **不要求**宿主是 `BuffAbility` 技能；只要 host Buff 挂在一个有效 owner 上，
  就能收到。`skchr_angel_1` 用 `ownerBuff` 方式挂 host Buff 后，仍然可以做
  受击反击、伤害修饰、技能开始/结束。
- `ON_AFTER_ATTACK`、`ON_OUTPUT_DAMAGE`、`ON_PROJECTILE_HIT_TARGET` 要求
  对应的事件源真的执行。没有攻击/投射物就不会触发，这不是 Buff 图写错了。
- `ON_SKILL_START/FINISH` 要求 owner 的技能实例存在；技能 prefab 只决定技能
  怎么运行，不决定事件名。
- 弹药/过载没有天然的“数值变化事件”。要监听变化，优先用 BuffData 的周期触发：
  `triggerInterval` + `ON_BUFF_TRIGGER`，在节点里读
  `AssignAmmoSkillRemainingCountToBB` / `AssignSkillProgressToSharedBlackboard`
  等黑板值，再做条件分支。不要为了轮询再写 C++ hook。

这也是蓝图编辑器应该做的校验：每个 shell 声明 `capabilities`，编辑器只展示
该 shell 能产生的事件；运行时遇到“订阅了但事件源不存在”的节点，至少打一条
警告，而不是静默失败。

如果确实需要游戏没有的事件，只能在 C++/C# 侧加一个自定义派发器或自定义
ability；或者用 `ON_BUFF_TRIGGER` 轮询状态。不要伪造
`ON_AFTER_ATTACK` 之类的原生事件名，`Buff.Event` 枚举里没有对应槽位。

### 4.3 四层模型：Skill prefab、Ability、Buff、ActionNode

这四层经常被混在一起，实际职责是：

```text
Skill prefab（资产/组合）
  ├─ BasicSkill / CastSkillWithLimitTimes（技能控制器，本身是 Ability）
  ├─ BuffAbility（Ability：把 BuffData 变成 Buff 实例）
  │    └─ BuffData（模板）
  │         └─ Buff（运行时实例）
  │              └─ m_actions[Buff.Event] -> ActionNode 图
  ├─ ProjectileAbility（Ability：攻击/投射物）
  └─ ActionAbility（Ability：在 ability 事件上运行 ActionNode 图）
```

| 层 | 是什么 | 生命周期 | 典型职责 |
| --- | --- | --- | --- |
| Skill prefab | Unity prefab，技能资产 | 由 `BattleFactory.CreateSkill` 实例化 | 组合 `BasicSkill`、BuffAbility、ProjectileAbility、ActionAbility 和序列化字段 |
| Ability | GameObject 上的行为组件 | 随组件 attach/detach/enable/disable | 技能控制、攻击、投射物、应用 Buff、运行 ActionAbility |
| Buff | `Entity.BuffContainer` 里的状态实例 | 由 BuffData 的持续时间/层数/来源决定 | 承载状态、属性修饰、派生 Buff、事件图 |
| ActionNode | 行为图里的一个节点 | 由宿主图和事件触发 | 条件、伤害、治疗、创建/结束 Buff、修改 Ability、切模式 |

几个容易混淆的点：

- `BuffAbility` 是 **Ability**，不是 Buff。它的 `_buffs` 是 `BuffData` 列表，
  运行时才变成 Buff 实例。
- `Buff` 不是 Ability。它是实体身上的状态实例，有自己的 `m_data` 和
  `m_actions`；它可以被 Ability 创建，也可以被 ActionNode 创建。
- `ActionNode` 不是 Buff。它是逻辑节点；只是在当前自定义技能里，我们把它挂在
  Buff 的 `m_actions` 上，所以看起来像“Buff 的行为”。
- `Skill prefab` 不是 `SkillData`。`SkillData` 是技能数值/黑板/类型数据；
  prefab 是按 `prefabKey` 实例化的运行时组件树。

原生里 ActionNode 也不只属于 Buff：`ActionAbility._actions` +
`_runActionOnEvent` 直接在 ability 事件上运行图；`ProjectileAbility` 和
`EntityRoot` 也可能带 `actionNodes`。当前 `custom-complex` 选择 Buff 作为图
宿主，是因为 Buff 的生命周期、层数和事件槽位最适合表达持续效果，不是因为
ActionNode 只能挂在 Buff 上。

---

## 5. 当前复合技能：逐组件原生对照

当前 JSON：`custom/runtime/custom_complex_arc.json`。

宿主：`skchr_mcnist_2`（机械师/Mechanist S2「协防术式」）。该 prefab 只有
`BasicSkill + BuffAbility`，`_extraAbilities` 为空，适合作为 AMMO 基底。

| 组件 | 实现 | 原生参考 |
| --- | --- | --- |
| 10 秒窗口 | `phase1`：`LIMITED + durationKey: charge_time=10` | 塔露拉 S2「受到攻击后触发，可充能{max_stack_cnt}次」的 `oxi_s_2_stack` 同款 `LIMITED + durationKey`；M7 `stack_duration=5`，10 秒是本技能数据。机械师 S3 原文是 40 秒限时技能，只能参考限时阶段，不能提供弹药 |
| 停止攻击 | `phase1.attributes.abnormalFlags: ["DISARMED"]` | 极光天赋「未开启技能且技力小于或等于最大值一半时，停止攻击并每秒恢复 3.5% 最大生命值」 |
| 非 DoT 受击 | `IsDamage` + `IfDamageTargetSide(ENEMY, MODIFIER_SOURCE→MODIFIER_TARGET)` + `CheckIfDamageHasSharedFlags(IS_CONTINUOUS, _isUnset:true)` + 环境伤害过滤 | 塔露拉 S2 `oxi_s_2_guard_manager` 的受击过滤；泡泡 S2 的敌方来源过滤 |
| 350% 物理反击 | `AdvancedApplyDamage(_atkScaleVar: retaliate_atk_scale, PHYSICAL, _targetType: MODIFIER_SOURCE)` | 斩业星熊 S1「每次受到攻击时对目标造成相当于斩业星熊攻击力{atk_scale}的法术伤害」的 `hsgma2_s_1` 完整节点链；键名参考塔露拉 S2「对攻击来源造成塔露拉攻击力{retaliate_atk_scale}」 |
| 反击冻结 2 秒 | `CreateBuffById("c2e_freeze[double_cold][freeze_key]", MODIFIER_SOURCE)` | 灵知 S2「对范围内所有敌人造成{cold}秒寒冷……蓄力：额外造成一层寒冷」的 `gnosis_s_2` 直接创建 `c2e_freeze[double_cold]`；2 秒别名来自原生 token 技能 `supctl_s1[range]` / `supctla_t[delay_frozen]` |
| 三次反击或 10 秒切换 | `counter` 叠层 + `FilterByBuffStackCount(GE,3)` + `FinishBuff`；`LIMITED` 自然结束也走 `ON_BUFF_FINISH` | 克洛丝 S2「击中目标{max_stack_count}次后技能剩余时间内攻击变为 4 连射」的 `kroos2_s_2[warming_counter]` 同款阈值切换 |
| 16 发弹药 | `ammoCount=16`；宿主 `CastSkillWithLimitTimes` 写 `_triggerTimeCount / m_maxCount / m_originalMaxCount` | 机械师 S2「攻击装有{trigger_time}发弹药，打完后结束」；Ash S2「攻击装有31发子弹」证明弹药来自原生 AMMO 计数器 |
| 每次攻击扣 1 发 | `CharSkillManualTriggerCountEvent(BUFF_SOURCE)` 挂在 `ON_AFTER_ATTACK` | 机械师 S2 `mcnist_s_2_shield` 的护盾破裂分支使用同一节点；Ash/号角 S2 的原生攻击计数是 `AbilityEventCounter`，但依赖目标攻击组 |
| 3-3 范围 | `rangeId=3-3` + `runtime.rangeOverrides(triggerBuff=phase2)` | 能天使 `char_103_angel` 精英二 `rangeId=3-3` |
| 200% 物理伤害 | `phase2.ON_CALCULATE_DAMAGE` 的 `AtkScaleUp(_atkScaleKey: atk_scale, _defaultValue: 2.0)`；`phase2.blackboard.atk_scale=2.0` | 极光 S2「若目标已经处于冻结状态，则改为使本次攻击的攻击力提高至{atk_scale}」的 `aurora_s_2[freezeattack2]` 同款 `ON_CALCULATE_DAMAGE → AtkScaleUp` |
| 0.8 秒攻击间隔 | `phase2_haste` 的 `AttributeModifierWithBB(BASE_ATTACK_TIME, ADDITION, base_attack_time=-0.2)` | 能天使 S3「攻击间隔少量缩短」的 `angel_s_3`：`BASE_ATTACK_TIME, ADDITION`（M7=-0.08） |
| 20% 回血 | `phase2_leech.ON_AFTER_OUTPUT_DAMAGE → HealViaDamage(DAMAGE_SCALE)`，`heal_scale=0.2` | 达格达天赋「每次攻击治疗自身相当于造成伤害 20% 的生命值」的 `dagda_t_2` |
| 溢出 33% 护盾 | `HEAL_CAN_GENERAL_SHIELD` + `SetCharacterMaxEs` + `ON_ES_OVER_ZERO` + `BlockDamage`；`shield_ratio=0.33` | 医生天赋「超过目标生命上限的治疗量可转化为不断衰减的屏障」的 `rdoc_t2`；摆渡人 S2「超出自身生命上限的生命值可转化为屏障」的 `varkis_s2[shield*]` |
| 攻击附加寒冷 | `phase2_cold.ON_OUTPUT_DAMAGE → CreateBuffById("c2e_cold", MODIFIER_TARGET)` | 极光 S2 `aurora_s_2[freezeattack]`、灵知天赋的 `c2e_cold` 路径 |
| 两层寒冷→冻结 | 只创建 `c2e_cold`；第二次寒冷由 `c2e_cold` 自己创建 `c2e_freeze[holder]` / `c2e_freeze` | `c2e_cold` 原生模板；不要自己判断层数或自己创建 `c2e_cold[debuff]` |

---

## 6. AMMO 与攻击范围：宿主侧例外

### 6.1 AMMO

当前宿主 `skchr_mcnist_2` 的运行时类是 `CastSkillWithLimitTimes`。C++ 把
`ammoCount` 写入：

- `_triggerTimeCount`
- `m_maxCount`
- `m_originalMaxCount`
- 黑板的 `trigger_time`

所以 16、160 或任意上限走同一条路径，不需要找“刚好 16 发”的 prefab。

耗弹时机由 ActionNode 决定：

- 默认“每次攻击扣 1 发”：`ON_AFTER_ATTACK` + `CharSkillManualTriggerCountEvent`；
- “护盾耗尽才扣 1 发”：把节点移到护盾破裂条件分支；
- “解除控制才扣 1 发”：把节点移到对应控制结束事件。

不要挂在 `ON_OUTPUT_ATK_OR_HEAL` 上表达“每次攻击”：本技能同时有
`HealViaDamage`，治疗也会触发一次，一次攻击会扣 2 发。

### 6.2 攻击范围

范围不是 ActionNode 效果，使用 `runtime.rangeOverrides`：

```json
{
  "triggerBuff": "phase2",
  "rangeId": "3-3",
  "restoreOnExit": true
}
```

`complex_tick` 每帧只检查已注册目标，Buff 出现时切到 `3-3`，消失时恢复。
这是用户明确允许的宿主侧例外，不要为了范围再写额外 hook。

---

## 7. 调试与验证

### 7.1 安装顺序

1. 进入战斗并 `pause`；
2. 确认卡牌 `state=1`（待部署）；
3. `custom-complex <cardUid>`；
4. `deploy <cardUid> <row> <col> [direction]`；
5. `resume` 至少一帧，让部署/技能操作真正执行；
6. `skill <cardUid>` 后再 `resume`，技能才会启动；
7. 需要观察时立即 `pause`。

部署后再执行 `custom-complex` 不会替换已创建的单位；必须重开战斗。

### 7.2 常用命令

```text
custom-complex <cardUid> [jsonFile]
custom-graph <unitUid> <jsonFile>
custom-apply <unitUid> <jsonFile> <buffName>
custom-actions <unitUid>
buffs <unitUid>
buff-fields <unitUid>
custom-vitals <unitUid> <hp> <def>
custom-attr <unitUid> <ATTR> <value>
custom-spfill <unitUid> <value>
custom-sp-type <unitUid> <spType>
custom-abilities <unitUid>
custom-remove-ability <unitUid> <query>
custom-switch-mode <unitUid> <modeIndex>
custom-bb <unitUid> <target> <key> <value>
custom-field <unitUid> <target> <field> <value>
custom-skillinfo <unitUid>
custom-modeinfo <unitUid>
custom-make-stop-mode <unitUid>
skill <cardUid>
deploy <cardUid> <row> <col> [direction]
pause
resume
trace start [state,cost,hp,sp,skill,affect,remain,trigger,ammo,overload,charge,mode,alive,all]
trace stop
```

输出文件：

- `build-cmake/dist/custom_actions.txt`：自定义 Buff 的事件槽位和黑板；
- `build-cmake/dist/battle_buffs.txt`：目标 Buff 列表、剩余时间、层数、属性修饰；
- `build-cmake/dist/BetterBattle.status`：单位 `ammo`、`affect`、`remaining`、
  `trigger_cnt`、`buffs` 等；
- `build-cmake/dist/CustomSkills.status`：自定义技能命令的最后结果和连接状态；
- `build-cmake/dist/logs/ArkFramework.log`：安装、范围覆盖、hook、异常。

### 7.3 当前技能的验证基线

以下数值来自 2026-11-18 实机（SK-5，能天使，宿主机械师 S2）：

- 10 秒自然结束：`phase1 remaining=9.3`，11 秒后 `phase2` 全套 Buff 出现；
- 三次反击：三个敌人各被反击一次，`counter stack=3`，进入 phase2；
- 反击原始伤害：`709.56 × 3.5 = 2483.46`；DEF 3100 时按最低伤害 `124.17`；
- 反击冻结：`c2e_freeze[double_cold] life=2.0000 remaining=1.9000 abnormal: FROZEN`，
  派生 `c2e_freeze` 带 `MAGIC_RESISTANCE -15`；
- 普攻：`709.56 × 2 = 1419.12`；
- 回血：`1419.12 × 0.2 = 283.824`/次；
- 弹药：4 次攻击 `16 → 12`，一次攻击扣 1 发；
- 护盾：生命 `1580/2260` 时继续攻击，回满并获得 `shield=150.248`，
  溢出 `455.3 × 0.33 = 150.25`；
- 寒冷：`c2e_cold` + `c2e_cold[debuff]`（`ATTACK_SPEED -30`）；
- 两层寒冷：`c2e_freeze[holder]` + `c2e_freeze`（FROZEN）；
- 范围：日志 `范围覆盖开始 buff=phase2 range=3-3 tiles=12`。

---

## 8. 常见问题

### 8.1 技能没有效果

- 是否在部署后才发现忘了 `custom-complex`？重开战斗，在待部署状态安装。
- 用的是卡牌 UID 还是单位 UID？`custom-complex`/`deploy`/`skill` 用卡牌 UID，
  `custom-actions`/`buffs`/`custom-vitals` 用单位 UID。
- 是否只改了源 JSON，没有同步到 `build-cmake/dist/custom_complex_arc.json`？
  重新构建，或手动复制；`-SkipBuild` 不会自动复制。

### 8.2 弹药一次扣 2 发

检查耗弹节点是否挂在 `ON_OUTPUT_ATK_OR_HEAL`。治疗也会触发该事件；
“每次攻击扣 1 发”应使用 `ON_AFTER_ATTACK`。

### 8.3 200% 变成 400%

检查 `atk_scale` 是否同时存在于技能黑板和 Buff 黑板，且还有 `AtkScaleUp`。
宿主 prefab 的攻击组件可能读取 `atk_scale`；复用宿主时应在
`removeBlackboardKeys` 去掉原值，只保留一个倍率来源。

### 8.4 寒冷/冻结只有特效

- 寒冷用 `c2e_cold`，冻结用 `c2e_freeze[double_cold]` / `c2e_freeze`；
  不要使用 `e2c_*`（enemy to char）。
- 两层寒冷→冻结交给 `c2e_cold` 自己处理；不要手写 `c2e_cold[debuff]` 层数。
- 某些 Buff 查看器只枚举主 `m_buffs`，不枚举派生 Buff；以 `buffs` 命令和
  异常标志 `FROZEN`/`COLD` 为准。

### 8.5 反击打到自己/友方

`ON_TAKE_DAMAGE` 先加 `IsDamage` 和
`IfDamageTargetSide(ENEMY, MODIFIER_SOURCE→MODIFIER_TARGET)`；
反击目标用 `_targetType: MODIFIER_SOURCE`。

### 8.6 范围没有恢复

`runtime.rangeOverrides.restoreOnExit` 必须为 `true`；触发 Buff 必须实际消失，
而不是被 `FinishBuff` 提前清掉。

### 8.7 自定义 Buff 面板没有中文描述

自定义 Buff 没有数据库条目，面板只能显示 `buffKey` 和黑板原值；
这不会影响 Buff 的实际效果。

### 8.8 换了 prefab 后出现没写的额外效果

检查宿主 prefab 的 `_extraAbilities`、`_extraModeIndex` 和额外的
`ProjectileAbility`/`ActionAbility` 组件。当前 loader 只清 Buff 数组，不清这些。
用 `custom-abilities` 查看，必要时换一个更干净的宿主；不要在本技能 JSON 里
找不存在的效果。

### 8.9 过载不触发

先确认宿主 prefab 的 `_isOverloadSkill=1`。当前 `custom-complex` 不写这个字段，
JSON 里也没有对应开关；选号角 S2/S3 这类原生过载宿主，再用 `trace overload`
看 `m_isOverloaded` 和 `remainingOverloadProgress`。`ammo` 技能进入过载的条件
是“剩余弹药降到初始弹药的一半”，不是固定时间。

### 8.10 天赋不能按 `custom-complex` 装载

`custom-complex` 只写 `mainSkill`。天赋在 `BattleCharacterData.talents` 里，
当前没有天赋注入命令；详见 1.4。先用 `custom-graph` + `custom-apply` 验证
Buff 效果，不要把“能挂 Buff”当成“已经替换天赋”。

---

## 9. 新增一个自定义技能的最小清单

1. 选一个原生 prefab，读它的技能原文、`durationType`、攻击组件和计数器来源；
   确认它有 `BasicSkill + ActiveBuffAbility`，并检查 `_isOverloadSkill`、
   `_extraAbilities`、`_extraModeIndex` 等不会被 loader 清理的字段。
2. 列出技能需要的 `capabilities`（attack/projectile/ammo/overload/charge/
   shield/mode）和事件；确认 shell 能提供这些事件源，再为每个效果找原生
   干员/模板参考，记录原文和关键节点。
3. 在 `buffs` 里按职责拆分 Buff，不要把所有效果塞进一个 Buff。
4. 在 `actions` 里按原生事件挂节点；字段名从原生图复制，不猜。
5. 只把 AMMO 和范围放进 `runtime`；其余效果用 ActionNode。
6. 在待部署状态 `custom-complex`，部署后 `resume` 一帧再 `skill`。
7. 用 `custom-actions`、`buffs`、`status`、`trace` 验证：
   Buff 生命周期、弹药、伤害、治疗、护盾、范围；涉及蓄力/过载时额外看
   `charge`、`availableCnt`、`overload`。
8. 把验证数值和原生参考写回本文，不要另开新文档。

---

## 10. 数据和工具位置

- 游戏技能表：`E:\Games\ArkTest\_assets\ArknightsGameResource\gamedata\excel\skill_table.json`
- 干员表：`E:\Games\ArkTest\_assets\ArknightsGameResource\gamedata\excel\character_table.json`
- 静态 ActionNode 图：`E:\Games\ArkTest\_analysis\action_graphs\`
  - `buffTemplates.jsonl`
  - `nodes.jsonl`
  - `actionNodes.jsonl`
  - `actionNodeTypes.jsonl`
  - `prefabs.jsonl`
- 全量 Buff 模板数据：`E:\Games\ArkTest\_analysis\git_zh_CN_gamedata_battle_buff_template_data.json`
- 图提取工具：`E:\Games\ArkTest\_tools\extract_action_graphs.py`
- ActionNode 目录扫描：`E:\Games\ArkTest\_tools\action_node_catalog_scan.py`
- Buff 字段与采集边界：`docs/BuffSystem.md`

