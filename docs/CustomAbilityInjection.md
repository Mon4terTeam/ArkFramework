# 自定义 Ability 注入方法

本文记录“把一个已有干员/prefab 上的 Ability 复制到另一个 prefab 实例”的运行时方法。
结论来自当前客户端的 IL2CPP 元数据、静态 prefab 提取结果和实机验证，不把静态结构直接当成运行时行为。

## 1. 先分清四类对象

| 对象 | 运行时形态 | 典型例子 | 注入方式 |
| --- | --- | --- | --- |
| Ability | `Torappu.Battle.Ability` / `AbilityStandard` 子类 | `RangedAttack`、`MeleeAttack`、`ProjectileToTileAbility`、`PassiveBuffAbility` | `AddComponent` 后复制字段，再挂到实体或技能 |
| Ability Behaviour | `Torappu.Battle.AbilityStandard.Behaviour` 子类 | `SimpleProjectileEmitter`、各类 EffectEmitter | `AddComponent` 后 `Init(ability)`，再追加到 Ability 的 `m_behaviours` |
| Ability Attachment | `Torappu.Battle.IAbilityAttachment` | 需要 `Apply(owner, target, ability, blackboard)` 的附件 | `AddComponent` 后调用 `Apply`，必要时注册到实体 |
| Buff / ActionNode 图 | 不是 Ability 组件 | Buff 模板、`ON_*` 事件图、`CreateBuffById` | 走 Buff 容器或自定义图加载器，不要伪装成 Ability |

关键点：静态 prefab 里的 `kind` 是提取器标签，不一定等于运行时类名。
例如赤刃明霄陈 S3 的 `ProjectileAbility` 在运行时是
`Torappu.Battle.Abilities.SimpleProjectileEmitter`，属于 Behaviour，不是 `Ability` 子类。

## 2. 判断组件是不是 Ability

运行时用 `class_parent` 链判断，不要只看名字：

- 是 `Torappu.Battle.Ability` 子类：可以走实体 Ability 挂载路径。
- 是 `Torappu.Battle.AbilityStandard.Behaviour` 子类：只能挂到某个 Ability 上，靠该 Ability 的事件流触发。
- 两者都不是：先查它实现的是 `IAbilityAttachment`、`IProjectileSource` 还是纯 MonoBehaviour，再选路径。

## 3. Ability 子类注入

适用：`RangedAttack`、`MeleeAttack`、`ProjectileToTileAbility`、`PassiveBuffAbility` 等。

步骤：

1. 从源单位/源 prefab 找到目标 Ability 实例。
2. 在目标 GameObject 上 `AddComponent(源运行时类型)`。
3. 复制序列化字段，但跳过 owner、施法状态、冷却、UID、PRD、Hotfix 等运行时字段。
4. 调用实体的 `_InternalOnly_AttachAbility(ability)`；没有该入口时退回 `ability.Attach(entity)`。
5. 把实体的 `m_abilitiesDirtyFlag` 置位，等待下一次 Ability 刷新。
6. 如果目标 Ability 需要黑榜/选择器/子物体，必须同时准备这些依赖，否则“挂上了但不会工作”。

注意：

- 复制 `RangedAttack` 时，`_selector` 等引用仍可能指向源 prefab 的组件，跨单位使用前要确认引用是否可共享。
- `PassiveBuffAbility` 通常在 Attach 时应用被动 Buff，可通过目标单位 Buff 列表验证。
- 主动 Ability 挂上后不一定自动施放；需要由技能状态、Buff 图、事件或手动 `CastToTarget` / `CastDirectly` 触发。

## 4. AbilityStandard.Behaviour 注入

适用：`SimpleProjectileEmitter`、动画/音效/特效发射器等。

步骤：

1. 找到目标 Ability（例如单位当前攻击 Ability）。
2. 在目标 Ability 的 GameObject 上 `AddComponent(Behaviour 类型)`。
3. 复制序列化字段。
4. 调用 `Init(targetAbility)` 写入 `<ability>k__BackingField`。
5. 把该 Behaviour 追加到 `AbilityStandard.m_behaviours` 数组。
6. 由目标 Ability 的 `OnEvent` 事件流触发 Behaviour。

攻击组要特别处理：

- `AbilityRandomGroup` 本身可能只负责选子 Ability，不会把 `ON_CAST_START` 直接转发给挂在组上的 Behaviour。
- 应递归到组内每个具体攻击 Ability（例如三个 `MeleeAttack`），分别挂 Behaviour。
- 只挂到组上通常表现为“注入成功、日志正常，但攻击不发射”。

## 5. 赤刃明霄陈 S3 投射物案例

静态 prefab：

- 路径：`skchr_chen3_3/EmitProjectile`
- 提取标签：`ProjectileAbility`
- 运行时类型：`Torappu.Battle.Abilities.SimpleProjectileEmitter`

关键字段：

```text
_projectileKey = projectile_chr_chen3_s3
_emitEvent = 2
_mountPointType = 7
_includeDeadTargets = 0
_manageProjectileByOwner = 0
_onlyToInputTarget = 0
```

挂到普通攻击后的效果：

```text
Timeline projectile[0] class=Torappu.Battle.SimpleProjectile
key=projectile_chr_chen3_s3
source=char_1045_svash2 uid=15
```

这说明“原本没有投射物的攻击 prefab”可以在运行时获得陈 S3 的投射物发射行为。

## 6. 当前可用命令

以下命令写入 `build-cmake/dist/CustomSkills.request`：

```text
custom-child-abilities <entityUid>
custom-components <entityUid>
custom-add-ability <targetUid> <sourceUid> <query> [emitEvent] [projectileKey]
custom-add-emitter <targetUid> <projectileKey> [emitEvent] [mountPointType]
custom-cast-ability <entityUid> <query> [targetEntityUid]
custom-ability-dump <entityUid> [query]
custom-create-ability <entityUid> <jsonFile> [castTargetEntityUid]
custom-projectiles
```

`custom-components` 是通用发现命令，会列出目标实体的 Ability、Behaviour 和
Attachment；`custom-child-abilities` 保留为兼容入口。

`custom-add-ability` 只负责从 live 源单位复制普通 Ability；
`custom-add-emitter` 现在只是兼容包装：它构造一份 JSON 规格并调用
`custom-create-ability` 的统一核心，不再包含独立的注入实现。

`custom-add-ability` 的 Attach 流程有一个容易漏掉的点：

- 实体的 `_InternalOnly_AttachAbility` 只负责注册，不能保证 Ability 的
  `<isAttached>` 变为 true。
- 注册后要调用 `get_isAttached` 检查；仍为 false 时必须再调用
  `ability.Attach(entity)`。
- 否则 Ability 会出现在对象表里，字段也对，但 `m_passiveBuffUids` 为空、被动 Buff 不生效。
- `Attach` 成功后不要再无条件调用 `AddPassiveBuffs`，否则被动 Buff 会加两遍。
  只有 `m_passiveBuffUids` 为空时才补调用。

`custom-create-ability` 不依赖源干员：它直接按 JSON 指定运行时类型、挂点和字段，
因此来源干员可以不在编队、也不在场上。它也是“完整自定义 Ability”的入口。

Behaviour 的 `anchor` 支持 `attack`、`skill`、`ability`；`kind=attachment`
会走 `Apply(owner,target,ability,blackboard)` 和实体注册路径。

## 7. 验证方法

1. 用 `custom-components <uid>` 看目标实际挂了哪些 Ability/Behaviour/Attachment。
2. 用 `export-web <prefix>` 读取 `units[].buffs`、`units[].uid` 和敌人位置。
3. 用 `tas-bench projectiles 0` 抓当前投射物列表；日志会写出 key、source、uid。
4. 用 `custom-projectiles` 生成 `custom_projectiles.txt` 作为结构检查。
5. 需要验证伤害时，再结合 damage series 或敌人 HP 变化，不能只看特效。

## 7.1 JSON 自定义 Ability

规格示例见 `custom/specs/chen3_emitter.json` 和
`custom/specs/custom_ranged.json`。

如果要从静态 prefab 生成规格，不需要该干员在编队或场上：

```powershell
python custom\ability_spec.py `
  --prefab skill:skchr_chen3_3 `
  --path skchr_chen3_3/EmitProjectile `
  --kind ProjectileAbility `
  --out custom\specs\chen3_emitter_generated.json
```

生成器会读取 `_analysis/action_graphs/prefabs.jsonl`，把标量字段转成 JSON；
对象引用和 ActionNode 列表会被跳过并在输出中提示。运行时类型无法可靠推断时，
用 `--type` 和 `--runtime-kind` 显式指定。

最小结构：

```json
{
  "type": "Torappu.Battle.Abilities.SimpleProjectileEmitter",
  "kind": "behaviour",
  "anchor": "attack",
  "fields": {
    "_projectileKey": "projectile_chr_chen3_s3",
    "_mountPointType": 7,
    "_emitEvent": 2,
    "_includeDeadTargets": false,
    "_manageProjectileByOwner": false,
    "_onlyToInputTarget": false
  }
}
```

字段说明：

- `type`：运行时完整类型名；也可以拆成 `namespace` + `class`。
- `kind`：`ability` 或 `behaviour`；省略时按运行时继承关系自动判断。
- `anchor`：
  - Ability 支持 `entity`（默认）或 `skill`；
  - Behaviour 目前固定为 `attack`，会递归挂到攻击组内每个具体攻击 Ability。
- `fields`：直接写入组件字段。
  - 支持 `bool`、整数/枚举值、浮点、字符串、`string[]`；
  - 暂不深拷贝 `TargetSelector`、ActionNode 列表等对象引用；
  - 复杂依赖应通过 Buff/ActionNode 图或自定义加载器提供。

因此目前的“完整自定义”覆盖组件字段和事件挂点；如果要把 `BuffData[]`、
ActionNode 图、TargetSelector 引用也全部写进同一份 JSON，还需要继续扩展
字段编码和引用解析。投射物/特效资产本身也必须已存在。

完整自定义一个主动 `RangedAttack` 的示例：

```json
{
  "type": "Torappu.Battle.Abilities.RangedAttack",
  "kind": "ability",
  "anchor": "entity",
  "fields": {
    "_projectileKey": "projectile_enemy_plkght",
    "_mountPointType": 7,
    "_damageType": 2,
    "_atkScale": 1.0,
    "_atkScaleKey": "atk_scale",
    "_attackType": 1,
    "_selectTargetSource": 2,
    "_alwaysIncludeTarget": true,
    "_animKey": "Attack",
    "_endAnimKey": "Idle",
    "_waitForAttackEvent": true
  }
}
```

命令中的 `castTargetEntityUid` 可选；提供后会在挂载完成后调用
`CastToTarget(target, null, false)`。攻击动画/投射物需要继续推进若干逻辑帧，
因此命令返回 `cast=true` 后还要 resume 一小段时间再查投射物列表。

## 8. 实机组合结果

| 目标 prefab | 来源 Ability | 类型 | 结果 |
| --- | --- | --- | --- |
| `char_1045_svash2` 普通攻击 | 陈 S3 `SimpleProjectileEmitter` | Behaviour | 成功生成 `projectile_chr_chen3_s3` |
| `char_1045_svash2` 实体 | 陈 `PassiveBuffAbility` `char_1050_chen3.T.1` | Ability | 成功获得 `chen3_t1`、`common_weak_damage` |
| `char_1045_svash2` 实体 | 敌人 `RangedAttack` | Ability | 手动施放后生成 `projectile_enemy_plkght` |
| `char_1050_chen3` 实体 | 敌人 `RangedAttack` | Ability | 手动施放后生成 `projectile_enemy_plkght` |
| `char_1050_chen3` 实体 | Svash2 `GlobalAuraAbility` | Ability | 能挂上，但目标 Buff 列表没有可见变化，说明还缺少光环条件/目标依赖 |
| `char_1045_svash2` 普通攻击 | JSON `SimpleProjectileEmitter`（无源干员） | Behaviour | 成功生成 `projectile_chr_chen3_s3` |
| `char_1045_svash2` 实体 | JSON `RangedAttack`（无源干员） | Ability | `cast=true`，生成 `projectile_enemy_plkght` |

关键证据：

```text
key=projectile_chr_chen3_s3 source=char_1045_svash2 uid=15

Svash2 buffs: chen3_t1, common_weak_damage

key=projectile_enemy_plkght source=char_1045_svash2 uid=22
key=projectile_enemy_plkght source=char_1050_chen3 uid=14

# 纯 JSON，陈不在编队/场上
key=projectile_chr_chen3_s3 source=char_1045_svash2 uid=13
key=projectile_enemy_plkght source=char_1045_svash2 uid=13
```

纯 JSON 测试时编队只有：

```text
cards = [(1, char_1045_svash2), (2, token_10057_svash2_eagle2)]
units = [(13, char_1045_svash2)]
```

陈既不在编队，也不在场上；能力字段完全来自 JSON。

这说明：

- 同一个 Ability 可以跨干员、跨阵营复制。
- 被动 Ability 和主动攻击 Ability 的生命周期不同，必须分别验证 Buff 与施放结果。
- “挂上组件”与“产生效果”之间还差事件源、施放调用和依赖组件。

## 9. 自定义 Ability 的推荐流程

1. 静态定位：从 prefab 找到目标组件的提取标签、路径、关键字段和引用。
2. 运行时定类：用 `type-search` / `class_parent` 确认它是 Ability、Behaviour 还是 Attachment。
3. 选挂点：
   - Ability 挂到实体或技能 GameObject；
   - Behaviour 挂到具体 Ability 的 GameObject；
   - Attachment 走 `Apply` / `RegisterAbilityAttachment`。
4. 复制字段：跳过 owner、施法状态、冷却、UID、PRD、Hotfix 等运行时字段。
5. 走生命周期：Ability 注册后检查 `isAttached`，必要时显式 `Attach`；Behaviour 调 `Init` 并追加 `m_behaviours`。
6. 提供触发：攻击事件、技能状态、Buff 图或手动 `CastToTarget` / `CastDirectly`。
7. 验证层级：对象存在 → `isAttached` → 字段正确 → Buff/投射物/伤害实际出现。
8. 不依赖源干员时：从静态 prefab/元数据确定运行时类型和字段，写 JSON 规格，
   用 `custom-create-ability` 创建。

## 10. 边界

- 这里修改的是战斗内实例化的 prefab 实例，不是磁盘上的 AssetBundle prefab 资产。
- Ability 挂载成功不等于 Ability 可用；事件源、黑榜、选择器、子物体、技能状态缺一不可。
- 需要持久化到所有关卡时，应在 bundle/资源层修改 prefab，而不是每次战斗运行时注入。
