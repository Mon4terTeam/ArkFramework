# 自定义关卡加载与编辑

本文说明 `BetterBattle` 的 `start-json` 如何绕过 `LevelUtils.ParseLevelData`
的签名/加密校验，以及如何编辑敌人、路线、地块、装置和地图机制。

## 1. ParseLevelData 的校验逻辑

官方关卡资源不是明文 JSON。`LevelUtils.ParseLevelData(UnityEngine.TextAsset)`
大致执行以下流程：

1. 根据 `LevelUtils.LEVEL_CONVERTER_TYPE` 选择 converter。
2. 从 `LevelUtils.m_decrypter` 取签名/解密 converter。
3. 对 TextAsset 做签名校验和解密。
4. 再把解密后的内容反序列化为 `Torappu.LevelData`。

直接把明文 JSON 塞进 `ParseLevelData` 会在第 3 步抛出
`Torappu.SecurityException`；这不是 JSON 字段写错，而是资源没有签名。

`start-json` 完全不调用 `ParseLevelData`，改走游戏自身暴露的明文 converter：

```text
LevelUtils.get_plainTextConverter()
  -> IConverter (JsonNetConverter)
  -> ConverterInput.Create(TextAsset)
  -> JsonNetConverter.DeserializeWithType(input, typeof(Torappu.LevelData))
```

关键点：

- 不修改全局 converter，也不伪造签名。
- `ConverterInput.Create` 接收运行时构造的 `UnityEngine.TextAsset`。
- 必须调用 `DeserializeWithType(..., typeof(Torappu.LevelData))`；无类型的
  `Deserialize` 会返回字段全空的 `object`。
- 解析完成后，插件会把自定义对象中仍为 `null` 的引用字段从基础关卡补齐；
  `levelId`/`mapId` 只在自定义 JSON 中为 `null` 时继承基础关卡。

实现位置：`plugins/BetterBattle/battle.cpp` 的 `parse_level_json_plain` 与
`start_battle`。

## 2. 命令

```text
start-json <jsonFile> <stageId> <roster>
level-dump <stageId>
map-dump
tiles <cardUid>
```

- `start-json`：加载自定义 JSON。`stageId` 是基础关卡，用于提供关卡元数据、
  场景和默认字段，不是 JSON 文件名。
- `level-dump`：把官方关卡反序列化成明文 JSON，写到
  `dist/level_<stageId>.json`，适合作为模板。
- `map-dump`：把当前战斗的运行时地图写到 `dist/battle_map.txt`，包含地块索引、
  枚举值和逐行布局。
- `tiles`：用某张卡牌的 `Card.CheckBuildable` 探测可放置格，只用于快速排查，
  不等同于完整的地块合法性检查。

编队格式：

```text
干员ID:等级:精英阶段:潜能:技能序号(1起):技能等级:模组ID:模组等级:信赖
```

技能序号可以用 `0` 表示不选技能。示例：

```text
char_002_amiya:80:2:6:1:10,char_1045_svash2:90:2:6:1:10
```

### 基础关卡的尺寸约束

自定义地图的宽高必须与基础关卡地图一致。尺寸不一致时，战斗可能停在
`state=0`，既没有报错也不会进入 `state=2`。已验证：

- `wk_armor_5`：11 列 x 8 行。
- `main_04-04`：13 列 x 9 行。
- `act25side_08`（CW-8）：12 列 x 8 行。

因此正确做法是先用 `level-dump <stageId>` 导出同尺寸模板，再改 `map` 和
`tiles`。基础关卡同时决定场景背景和主题；换基础关卡就能换地图外观。

## 3. LevelData 结构

`Torappu.LevelData` 的主要字段：

| 字段 | 作用 |
| --- | --- |
| `options` | 生命点、初始费用、费用回复、人数上限等 |
| `mapData` | 地图尺寸、地块索引、地块定义、地图标签和效果 |
| `routes` | 敌人路线 |
| `extraRoutes` | 额外路线 |
| `enemies` | 内联敌人数据 |
| `enemyDbRefs` | 从 EnemyDB 引用的敌人 |
| `waves` | 波次、片段和动作 |
| `predefines` | 预定义干员、装置、卡牌 |
| `globalBuffs` | 全局 Buff |
| `runes` / `optionalRunes` | 关卡符文 |
| `branches` | 分支波次 |
| `randomSeed` | 随机种子 |

完整字段以 `level-dump` 导出的 JSON 为准。

### mapData

```json
{
  "map": [[0, 1], [2, 3]],
  "tiles": [
    {
      "tileKey": "tile_floor",
      "heightType": "LOWLAND",
      "buildableType": "MELEE",
      "passableMask": "ALL",
      "playerSideMask": "ALL",
      "blackboard": null,
      "effects": null
    }
  ],
  "tags": ["PLAIN"],
  "effects": null
}
```

- `map` 是 `height` 行、`width` 列的二维数组，值是 `tiles` 的下标。
- `routes[].startPosition.row/col` 与 `map` 使用同一套关卡坐标。
- `map-dump` 输出的 `layout` 行号与 JSON `map` 行号一致，可直接对照；
  不要只翻转 `map` 而不翻转 `routes`。

## 4. 敌人与路线

### 地面敌人

在 `enemyDbRefs` 中引用 EnemyDB，再在波次里 `SPAWN`：

```json
"enemyDbRefs": [
  { "useDb": true, "id": "enemy_1007_slime", "level": 0, "overwrittenData": null }
],
"waves": [
  {
    "preDelay": 0,
    "postDelay": 0,
    "maxTimeWaitingForNextWave": -1,
    "fragments": [
      {
        "preDelay": 1,
        "actions": [
          {
            "actionType": "SPAWN",
            "managedByScheduler": true,
            "key": "enemy_1007_slime",
            "count": 5,
            "preDelay": 3,
            "interval": 2,
            "routeIndex": 0,
            "randomType": "ALWAYS",
            "refreshType": "ALWAYS"
          }
        ]
      }
    ]
  }
]
```

`key` 必须等于 `enemyDbRefs[].id`。`routeIndex` 是 `routes` 的下标。

### 空中敌人

空中敌人本身不需要特殊字段，但路线必须是：

```json
{
  "motionMode": "FLY",
  "startPosition": { "row": 7, "col": 6 },
  "endPosition": { "row": 2, "col": 10 },
  "checkpoints": []
}
```

已验证 `enemy_1005_yokai` 在 `motionMode: "FLY"` 路线上正常刷出。
不要把 `motionMode` 写成 `E_NUM`；它是枚举哨兵，会导致
`SPFA.GenerateNextMap` 抛 `IndexOutOfRangeException`。可用值为 `WALK` 和 `FLY`。

### 大型/BOSS/特殊敌人

这些属性来自敌人数据，不是关卡 JSON 的独立字段：

- `levelType`：`NORMAL` / `ELITE` / `BOSS`。
- `enemyTags`：特殊标签。
- `attributes.blockCnt`、`massLevel`：阻挡数、重量等。

已验证 `enemy_1001_bigbo` 刷出后为 `hp=22000`、`boss=1`、`block=1`、
`move=0.25`，但它是普通 `Torappu.Battle.Enemy`，不是巨型单位。

真正的巨型单位使用 `Torappu.Battle.EnemyGiantBoss` 子类。已验证
`enemy_2054_smdeer`（“萨米的意志”，HP 240000）运行时对象类为
`Torappu.Battle.EnemyGiantBoss`，状态中 `giant_boss=1`。状态读取必须按运行时
实际类查找 `get_isGiantBoss`，只按基础 `Torappu.Battle.Enemy` 查找会误报为 0。

要改数值时，可以在 `enemyDbRefs[].overwrittenData` 中覆盖
`name`、`prefabKey`、`attributes`、`motion`、`levelType`、`skills`、`spData`；
参考官方 `levels/obt/guide/level_guide_01.json`。

### 路线检查点

```json
{
  "motionMode": "WALK",
  "startPosition": { "row": 7, "col": 6 },
  "endPosition": { "row": 2, "col": 10 },
  "spawnRandomRange": { "x": 0.2, "y": 0.2 },
  "spawnOffset": { "x": 0, "y": 0 },
  "checkpoints": [
    {
      "type": "MOVE",
      "time": 0,
      "position": { "row": 6, "col": 6 },
      "reachOffset": { "x": 0, "y": 0 },
      "randomizeReachOffset": false,
      "reachDistance": 0
    }
  ],
  "allowDiagonalMove": true,
  "visitEveryTileCenter": false,
  "visitEveryNodeCenter": false,
  "visitEveryCheckPoint": false
}
```

路径必须落在可通行地块上；地穴、禁部署块和墙的 `passableMask` 会影响寻路。

## 5. 地图地块

### 关键字段

| 字段 | 常用值 | 含义 |
| --- | --- | --- |
| `heightType` | `LOWLAND` / `HIGHLAND` | 地面 / 高台 |
| `buildableType` | `NONE` / `MELEE` / `RANGED` | 不可部署 / 近战位 / 远程位 |
| `passableMask` | `ALL` / `FLY_ONLY` / `NONE` | 可通行对象 |
| `playerSideMask` | `ALL` / `PLAYER` / `ENEMY` | 所属阵营 |
| `blackboard` | `key/value` 列表 | 地块机制参数 |
| `effects` | 效果数组 | 地块特效 |

`tile_floor` 的 `buildableType` 必须是 `MELEE` 或 `RANGED`，否则会出现“地图
看起来是地面但没有任何可部署格”。

### 常用 tileKey

| tileKey | 典型配置 | 用途 |
| --- | --- | --- |
| `tile_floor` | LOWLAND / MELEE / ALL | 普通地面 |
| `tile_wall` | HIGHLAND / RANGED / FLY_ONLY | 高台/墙 |
| `tile_start` | LOWLAND / NONE / ALL | 侵入点 |
| `tile_end` | LOWLAND / NONE / ALL | 目标点 |
| `tile_hole` | LOWLAND / NONE / ALL | 地穴，地面敌人进入即死 |
| `tile_forbidden` | HIGHLAND / NONE / FLY_ONLY | 禁部署/飞行专用 |
| `tile_icestr` | LOWLAND / MELEE / ALL | 冰面 |
| `tile_grvtybtn` | LOWLAND / MELEE / ALL | 重力按钮 |
| `tile_ristar_road` | LOWLAND / MELEE / ALL | 孤星重力道路 |
| `tile_ristar_road_forbidden` | LOWLAND / NONE / ALL | 孤星禁部署道路 |

`tileKey` 决定加载哪个地块 prefab，因此外观和地块行为都由 prefab + `blackboard`
共同决定。官方 prefab 可以自由拼接；全新美术需要额外 AssetBundle/prefab，
当前框架只复用客户端已有资源。

### 地穴、冰面、禁部署示例

```json
{
  "tileKey": "tile_hole",
  "heightType": "LOWLAND",
  "buildableType": "NONE",
  "passableMask": "ALL",
  "playerSideMask": "ALL",
  "blackboard": null,
  "effects": null
}
```

```json
{
  "tileKey": "tile_icestr",
  "heightType": "LOWLAND",
  "buildableType": "MELEE",
  "passableMask": "ALL",
  "playerSideMask": "ALL",
  "blackboard": null,
  "effects": null
}
```

```json
{
  "tileKey": "tile_forbidden",
  "heightType": "HIGHLAND",
  "buildableType": "NONE",
  "passableMask": "FLY_ONLY",
  "playerSideMask": "ALL",
  "blackboard": null,
  "effects": null
}
```

同一张地图可以放多个 `tile_start`；`routes` 的起点应落在对应的 `tile_start` 上。

### 运行时检查

```text
map-dump
```

输出中的 `H=`、`B=`、`P=`、`S=` 是枚举原始值：

- `H`：0=`LOWLAND`，1=`HIGHLAND`。
- `B`：0=`NONE`，1=`MELEE`，2=`RANGED`。
- `P`：2=`FLY_ONLY`，3=`ALL`。
- `S`：0=`ALL`。

## 6. 特殊装置

装置放在 `predefines.tokenInsts`：

```json
{
  "position": { "row": 3, "col": 6 },
  "direction": "UP",
  "hidden": false,
  "alias": "trap_014_tower#1",
  "inst": {
    "characterKey": "trap_014_tower",
    "level": 1,
    "phase": "PHASE_0",
    "favorPoint": 0,
    "potentialRank": 0
  },
  "skillIndex": 0,
  "mainSkillLvl": 1,
  "overrideSkillBlackboard": null
}
```

已验证的装置：

| characterKey | 说明 | 运行时结果 |
| --- | --- | --- |
| `trap_014_tower` | L-44 留声机 | HP 100，正常生成 |
| `trap_057_wpnsts` | 冰淇淋机 | HP 25000，`skillIndex=1` |
| `trap_121_gractrl` | 孤星重力控制器 | HP 100，CW-8 中作为单位生成 |
| `trap_050_blizzard` | 暴风雪 | HP 100，可被 `ACTIVATE_PREDEFINED` 激活 |

需要主动激活的装置，要给它唯一的 `alias`，然后在波次动作里引用：

```json
{
  "actionType": "ACTIVATE_PREDEFINED",
  "managedByScheduler": true,
  "key": "trap_050_blizzard#1",
  "count": 1,
  "preDelay": 1,
  "interval": 1,
  "routeIndex": 0
}
```

`key` 必须等于 `tokenInsts[].alias`。

## 7. 地图特殊机制

### 风雪过境：暴风雪

完整模板：`levels/activities/act14side/level_act14side_05.json`。

做法：

1. 在 `predefines.tokenInsts` 放一个 `trap_050_blizzard`，设置 `hidden: true`。
2. 给唯一 `alias`，例如 `trap_050_blizzard#1`。
3. 用 `overrideSkillBlackboard` 覆盖 `duration`。
4. 在波次中用 `ACTIVATE_PREDEFINED` 激活。

```json
{
  "position": { "row": 0, "col": 0 },
  "direction": "DOWN",
  "hidden": true,
  "alias": "trap_050_blizzard#1",
  "inst": {
    "characterKey": "trap_050_blizzard",
    "level": 1,
    "phase": "PHASE_0",
    "favorPoint": 0,
    "potentialRank": 0
  },
  "skillIndex": 0,
  "mainSkillLvl": 1,
  "overrideSkillBlackboard": [
    { "key": "duration", "value": 30, "valueStr": null }
  ]
}
```

实测激活后 `trap_050_blizzard` 出现 `affect=1`、`remaining≈26.37`、
`trigger_cnt=1`，说明装置、技能和波次激活链路都已走通。

### 孤星：重力感应

完整模板：`levels/activities/act25side/level_act25side_08.json`。

两个组成部分：

1. 地块 `tile_grvtybtn`，用 `blackboard` 指定重力方向：

```json
{
  "tileKey": "tile_grvtybtn",
  "heightType": "LOWLAND",
  "buildableType": "MELEE",
  "passableMask": "ALL",
  "playerSideMask": "ALL",
  "blackboard": [
    { "key": "source_direction", "value": 2.0, "valueStr": null },
    { "key": "activate_block_cnt", "value": 1.0, "valueStr": null }
  ],
  "effects": null
}
```

官方 CW-8 同时使用了 `source_direction=2.0` 和 `source_direction=0.0` 两种
按钮；`activate_block_cnt` 为激活所需阻挡数。

2. 在 `predefines.tokenInsts` 放重力控制器：

```json
{
  "position": { "row": 0, "col": 0 },
  "direction": "DOWN",
  "hidden": false,
  "alias": null,
  "inst": {
    "characterKey": "trap_121_gractrl",
    "level": 1,
    "phase": "PHASE_0",
    "favorPoint": 0,
    "potentialRank": 0
  },
  "skillIndex": 0,
  "mainSkillLvl": 1
}
```

实测在 CW-8 原生尺寸的地图中，`trap_121_gractrl` 作为单位生成，
`tile_grvtybtn` 及其 `blackboard` 正确进入运行时地图。按钮被阻挡后的实际
重力推拉效果仍建议在 CW-8 原生布局中逐帧验证。

### 冰面与地穴

- `tile_icestr` 使用冰面 prefab，地块数据仍是 `LOWLAND / MELEE / ALL`。
- `tile_hole` 使用地穴 prefab，`buildableType` 为 `NONE`；地面敌人进入即死，
  飞行敌人不受影响。
- `tile_forbidden` 用 `passableMask: "FLY_ONLY"` 表示地面不可走、飞行可过。

### 其它全局效果

`globalBuffs`、`mapData.effects`、`runes` 和 `options.configBlackBoard` 也能
改变关卡机制。它们的字段随版本变化较大，建议从包含目标机制的官方关卡
`level-dump` 后整体复制，再改数值。

## 8. 地图外观

地图外观分三层：

1. **地块外观**：由 `tileKey` 选中的 prefab 决定。相同网格可以混用
   `tile_floor`、`tile_wall`、`tile_hole`、`tile_icestr` 等任意官方地块。
2. **场景背景/主题**：由基础关卡的 `levelId` / 场景决定。换 `stageId` 就会
   换背景；自定义地图尺寸必须与该基础关卡一致。
3. **全新素材**：需要新的 tile prefab、场景或 AssetBundle。当前框架没有
   资源打包/注入流程，只能复用客户端已有资源。

之前“地图布局变了但外观没变”的原因是：`start-json` 用了 `wk_armor_5`
（11x8）作为基础关卡，所以背景和地块主题仍然是 SK-5；`mapData` 只替换了
布局、敌人和机制。

补充（2026-11-12）：`tileKey` 本身**不会**触发展示 prefab 的重新实例化。
实测所有可视化地块共享基础场景的合并网格，改 `tileKey` 只改变逻辑。
但运行时可以逐格替换 `MeshTileGraphic.m_renderer` 的材质，并已用官方材质
`arts/maps/map_icewild/res/materials/mt_icewild` 完成整图换肤验证。
模型级重建（高台/地面/地穴几何）仍是待完成项，详见
[`MapVisualOverrideProgress.md`](MapVisualOverrideProgress.md)。

已验证：

- 11x8 自定义地图 + `wk_armor_5`：进入 SK-5 场景。
- 13x9 自定义地图 + `main_04-04`：进入主线 4-4 场景。
- 12x8 自定义地图 + `act25side_08`：进入 CW-8 场景，重力装置正常生成。

如果自定义 JSON 中显式写了非 `null` 的 `levelId`/`mapId`，插件不会再覆盖；
但这两个值必须指向客户端已有资源，否则场景加载可能失败。

## 9. 推荐工作流

1. 选一个与目标地图同尺寸、主题合适的基础关卡。
2. `level-dump <stageId>` 导出模板。
3. 修改 `mapData.map`、`mapData.tiles`、`routes`、`enemyDbRefs`、`waves`。
4. `start-json <json> <stageId> <roster>`。
5. `map-dump` 检查地块索引、枚举和布局。
6. 用状态文件确认 `state=2`、波次、敌人和装置。
7. 需要逐帧机制时先 `pause`，再用 `trace start`。

## 10. 同图多机制验证

`custom_combined_verify.json`（11x8，基础关卡 `wk_armor_5`）同时放入：

- `enemy_2054_smdeer`：`Torappu.Battle.EnemyGiantBoss`，HP 240000。
- `trap_050_blizzard`：暴风雪，`ACTIVATE_PREDEFINED` 激活。
- `trap_121_gractrl`：重力控制器。
- `tile_grvtybtn`：路径上的重力按钮。
- `trap_014_tower`、`trap_057_wpnsts`：留声机和冰淇淋机。

`probe snapshot` 的实体记录证明同一批敌人同时带有重力与暴风雪 Buff：
原始摘录写在 `dist/mechanism_coexistence.txt`。

```text
enemy_2054_smdeer  class=Torappu.Battle.EnemyGiantBoss
  gractrl_s_enemy[equal]
  gractrl_s_enemy[opposite]
  gractrl_s_ristar
  gractrl_s_speed_down
  blizzard_s[mark]
  blizzard_s[enemy]
  c2e_cold
  c2e_cold[debuff]
```

同一局中 `enemy_1001_bigbo` 也出现过 `gractrl_s_speed_up`，移动速度由 `0.075`
变为 `0.66`，说明重力控制器不是只生成单位，而是实际改变了场上敌人属性。
暴风雪的 `blizzard_s*` / `c2e_cold` 与重力的 `gractrl_s*` 同时存在，因此
“暴风雪 + 重力感应”不依赖 CW-8 或风雪过境原地图，可以在同一张自定义图中共存。

状态里的 `trap_121_gractrl affect=0` 不能作为重力未生效的判据；它的效果通过
`gractrl_s*` 全局 Buff 施加到敌人，而不是自身技能实例的 `isAffecting`。

## 11. 已知限制

- 明文 converter 只用于本地练习战斗，不走官方签名链路，也不应依赖上传结算。
- 基础关卡尺寸必须与自定义地图一致，否则可能停在 `state=0`。
- 部分活动关卡还依赖活动/子场景元数据；优先选择普通主线、周常或已验证关卡。
- `stage-list` 当前返回 0 条，关卡模板以本地官方数据目录和 `level-dump` 为准。
- `map-dump` 的枚举值输出为整数，映射见第 5 节。
- `TileData.tileKey` 当前主要影响逻辑（高度、可部署、可通行、机关），场景显示
  仍复用基础关卡的 TileGraphic。运行时逐格换材质已验证可行，但真正的
  tileKey 模型换皮仍需要图形重建/资源注入。详细实验、运行时对象关系、
  已排除路径和最新进展见 `docs/MapRenderingResearch.md` 与
  `docs/MapVisualOverrideProgress.md`。

## 12. 自定义地图显示：盒体拼接（2026-11-13）

`map-rebuild box` 是当前推荐的自定义地图显示路线。它不再手写四边形，而是
逐格实例化客户端自带的官方盒体 prefab，并套用官方主题材质：

```text
arts/effects/[pack]map/meshes/box
  -> GameObject prefab
  -> MeshFilter.sharedMesh = pPlane2（100 顶点）
  -> bounds = center(0,0,0), extents(50,50,50)，即 100x100x100
```

命令：

```text
map-rebuild               # 默认等价于 map-rebuild box（自动主题）
map-rebuild box           # 自动复用基础关卡原生地块材质
map-rebuild box-ice       # 冰原主题
map-rebuild box-snow      # 雪村主题
map-rebuild box-debug     # 纯色调试：地面绿、高台洋红、地穴近黑
map-rebuild debug         # 自建网格 + 纯色调试
map-rebuild off           # 恢复原始地图渲染和材质
renderer-dump             # 写 dist/battle_renderers.txt
```

### 12.1 几何与 Z 锚定

- 重建时先对 `MapGraphic._graphics` 的 Z 做聚类，取“最低的主要平面”作为
  地面、“最高的主要平面”作为高台，二者差值作为该关卡的台高。
  SK-5 为 `-0.379653 / +0.020347`，台高 `0.4`；主线 4-4 为
  `-0.28 / +0.02`，台高 `0.3`。整体再偏移 `-0.01` 避免与原始高亮 Z-fighting。
- 盒体按 mesh bounds 自动缩放：X/Y 缩放到 1 格，Z 按目标厚度缩放；
  顶面对齐到目标高度，不再依赖 prefab 轴心假设。
- 地面顶面约 `-0.39`、高台顶面约 `+0.01`、地穴顶面约 `-0.49`。
  所有盒体最低到 `-0.89`，不会出现大面积 `z>=0` 遮挡小人。
- 盒体材质默认自动复用当前基础关卡的原生地块材质（`box_theme=auto`）：
  SK-5 为 `MT_wilds_A`，主线 4-4 为 `MT_Chernobog_Ground` /
  `MT_Chernobog_B_Outworld`。这样换基础关卡不需要改材质路径。

### 12.2 可部署高亮

不能直接 `Renderer.enabled=false`，否则拖动干员时的绿色可部署提示也会消失。
当前做法是保留 `MapGraphic._graphics` 里的 76 个地块 renderer，把它们换成
全透明材质并同步写入 `m_originMaterial` / `m_replaceableMaterial`；拖动时游戏
仍会把 renderer 切回 `m_buildableGroundMaterial`，绿色格子正常显示。
额外的 60 个装饰类 renderer 会被禁用，`map-rebuild off` 会重新启用并恢复原材质。

### 12.3 已验证

- 11x8 条纹图：88 个盒体、33 地面 / 33 高台 / 22 地穴，
  `Z=-0.889813..0.0101869`。
- 13x9 条纹图 + `main_04-04`：117 个盒体，`Z偏移=-0.28`、`台高=0.30`、
  `Z=-0.689794..0.0100153`，自动材质为 `MT_Chernobog_*`。
- `custom_showcase_1_bigbo_tower.json`：88 个盒体 + 4 个起终点标记。
- `renderer-dump` 确认盒体使用 `MT_wilds_A` / `MT_wilds_A_HG`，不是纯色占位。
- `map-rebuild off`：日志 `已还原 76/84 格原始材质`，盒体全部销毁，
  装饰 renderer 重新启用。

### 12.4 仍需实机确认

- `pPlane2` 的 UV 在缩放后是否与主题材质匹配；若纹理密度不对，需要调整
  UV 或按子网格拆分。
- 高台上的干员/敌人是否完全不被盒体遮挡。
- 地穴盒体是否需要只保留侧壁、去掉顶面。
- 体量大时 88 个独立 renderer 的 draw call 是否可接受；后续可合并同材质盒体。

### 12.5 基础关卡选择与自定义锚点

优先选择“干净基底”，不要把装饰很多的关卡当作自定义地图底板：

- SK-5（`wk_armor_5`，11x8）已验证较干净：盒体模式下启用的 `S_` 对象只有 76 个
  透明地块 renderer，额外隐藏 62 个装饰；未发现成片的源石地板/场景特效。
- 主线 4-4（`main_04-04`，13x9）有大量 `S_playground_*` 逐格装饰和源石地板
  特效；虽然 `S_`/`T_`/`FX_Mesh_` 前缀对象会被透明化，仍不建议作为基底。
- 换基底时只需保证自定义地图尺寸与基础关卡一致；台高和材质会自动从基础场景
  的两个主平面/原生材质推导。

侵入点和保护目标的位置完全由 JSON 决定：

- 侵入点：在 `mapData.map` 放置 `tile_start`，并让
  `routes[].startPosition` 指向同一格；可以有多个起点和多条路线。
- 保护目标：放置 `tile_end`，并让 `routes[].endPosition` 指向同一格；
  多个终点会生成多个保护目标。
- `map-rebuild box` 会在这些格子上生成绿色（起点）和红色（终点）标记盒；
  原场景烘焙的旧目标/装饰模型会被透明化，所以视觉位置以自定义 JSON 为准。
