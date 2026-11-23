# 自定义技能实验记录

本文是数据采集与实验阶段的结果汇总。单个干员/技能只是验证探针，不是实现目标；
后续开发应以可组合的组件、挂点、生命周期和事件源为核心。

## 1. 当前框架层

| 层 | 文件 | 职责 |
| --- | --- | --- |
| 运行时基础 | `custom/runtime/custom_helpers.*` | 实体/Ability 列表、信号、继承判断、通用子组件遍历 |
| JSON 字段层 | `custom/runtime/custom_json.*` | 把 bool/整数/枚举/浮点/字符串/string[] 写入任意 IL2CPP 对象 |
| 组件注入层 | `custom/runtime/custom_ability_add.cpp` | 统一处理 Ability、AbilityStandard.Behaviour、IAbilityAttachment 的创建、挂载、施放和 dump |
| 组件规格生成 | `custom/ability_spec.py` | 从静态 prefab 生成 `custom-create-ability` JSON，不需要源干员在场 |
| 技能运行时 | `custom/runtime/complex_skill.cpp` | 把一个原生技能改造成数据驱动技能；graph/hostBuff/ammo/range 都是可选特性 |
| ActionNode 图 | `custom/runtime/custom_graph.cpp` | JSON Buff/ActionNode 图加载；与 Ability 注入层解耦 |

## 2. 实验矩阵

| 目标 | 来源 | 组件 | 结果 |
| --- | --- | --- | --- |
| `char_1045_svash2` 普攻组 | 陈 S3 prefab（源干员不在编队） | `SimpleProjectileEmitter` | 生成 `projectile_chr_chen3_s3` |
| `char_1045_svash2` 实体 | 陈 live Ability | `PassiveBuffAbility` | 获得 `chen3_t1`、`common_weak_damage` |
| `char_1045_svash2` 实体 | 敌人 live Ability | `RangedAttack` | 手动施放生成 `projectile_enemy_plkght` |
| `char_1050_chen3` 实体 | 敌人 live Ability | `RangedAttack` | 手动施放生成 `projectile_enemy_plkght` |
| `char_1050_chen3` 实体 | Svash2 live Ability | `GlobalAuraAbility` | 能挂载，但目标 Buff 无可见变化；缺少光环目标条件 |
| `char_1045_svash2` 实体 | 纯 JSON | `SimpleProjectileEmitter` | 生成 `projectile_chr_chen3_s3` |
| `char_1045_svash2` 实体 | 纯 JSON | `RangedAttack` | `cast=true`，生成 `projectile_enemy_plkght` |
| `char_4230_mcnist` 普攻 | JSON 兼容包装 | `SimpleProjectileEmitter` | 生成 `projectile_chr_chen3_s3` |
| `char_4230_mcnist` 卡牌 1 | `custom_complex_arc.json` | 数据驱动技能 | 安装成功；触发后 `skill_ammo_remaining=16`，host/phase/counter Buff 正常 |

证据字段：

```text
key=projectile_chr_chen3_s3 source=char_1045_svash2 uid=13
key=projectile_enemy_plkght source=char_4230_mcnist uid=24
key=projectile_chr_chen3_s3 source=char_4230_mcnist uid=24
skill_ammo_remaining=16 skill_ammo_max=16
component_categories=ability,behaviour
```

## 3. 特例审计与处理

| 原实现 | 问题 | 当前处理 |
| --- | --- | --- |
| `custom-add-emitter` 内直接 `AddComponent + Init + append` | 只支持一种 Behaviour | 改为构造 JSON 规格，调用 `custom-create-ability` 核心 |
| `collect_emitter_targets` | 名字和语义绑定发射器 | 改名为 `collect_concrete_ability_targets`，只表达“递归到具体攻击” |
| `custom-child-abilities` 全局扫描所有 Ability | 噪声大、只覆盖 Ability | 改为 `custom-components`，覆盖 Ability/Behaviour/Attachment，且只扫目标实体 |
| `custom_graph` 自己的 ability list/signal 实现 | 与注入层重复 | 下沉到 `custom_helpers`，两处共用 |
| `complex_skill` 强制 ammo/hostBuff/graph | 只能服务单一原型技能 | 改为可选特性；缺少字段时保留原生行为 |
| `complex_skill` 只写固定 SkillData 字段 | 新技能要改 C++ | 支持 `skill.fields` / `skill.overrides` 及未知字段自由覆盖 |

## 4. 仍然实验性的部分

- `complex_skill` 仍是单个全局配置，不是多技能注册表。
- ammo 消耗规则目前有 `actionNode`、`hook`、`none` 三种实验模式；更自由的做法应变成事件→条件→动作图。
- `rangeOverrides` 仍是内置逻辑，后续应抽成独立范围特性模块。
- JSON 字段层还不支持深层对象引用、`BuffData[]`、ActionNode 引用；这些应通过引用表或图加载器表达。
- 修改的是战斗内 prefab 实例；磁盘 AssetBundle prefab 资产修改尚未实现。

`complex_skill` 的最小配置现在只需要一个基础 prefab（`nativePrefabKey` 或
`virtualPrefabKey`），其余 `name/range/duration/sp/ammo/hostBuff/graph` 都可省略。
额外字段可放在 `skill.fields`、`skill.overrides`，或直接作为 `skill` 下的未知键，
由通用 JSON 字段层写入 SkillData。缺字段时保留原生技能值，不再强制抛错。

## 5. 给后续开发 agent 的建议

1. 先建“组件规格”数据结构，不要从单个技能写分支。
2. 所有能力按 `kind + anchor + fields + lifecycle + trigger` 描述。
3. Ability、Behaviour、Attachment 分开处理，但共享实体/组件发现层。
4. 复杂技能拆成可选特性：sp、ammo、range、hostBuff、graph、presentation。
5. 每个特例先记录成实验矩阵，再决定是否进入框架层。
