# 自定义地图显示覆盖进展（2026-11-12）

本文记录 `start-json` 自定义地图“逻辑正确、画面不跟随”问题的最新突破、已验证命令、
运行时证据，以及暂停时的未完成状态。目标是让任意自定义地图在**模型层面**显示正确，
即高台、地面、地穴等类别一眼可辨，而不是只换 shader/颜色。

## 1. 结论摘要

- 地图美术不是逐格独立网格，而是基础场景烘焙出的两张合并网格：
  `Combined Mesh (root: scene) 4/5`。
- `MapGraphic._graphics` 有 84 个槽位，其中 76 个是有效的
  `Torappu.Battle.Tiles.MeshTileGraphic`，8 个为 `null`。
- 这 76 个 `MeshTileGraphic` 可以**逐格独立换材质**。已验证：
  - 整图换色后，全地图一眼变成纯洋红色。
  - 按 `TileData.tileKey` 分色后，地面/高台/起点/终点/重力按钮/孤星道路呈现不同颜色，
    且各类数量与自定义 JSON 完全一致。
  - 整图套用官方材质 `arts/maps/map_icewild/res/materials/mt_icewild` 后，
    画面整体变成冰原石材质，证明官方素材可以在运行时加载并应用。
- `Tile._data.tileKey` 保存的是自定义 JSON 的 `tileKey`，不是基础场景值；
  之前 dump 读 `m_data` 得到空值，是字段选错。
- 当前真正缺的是**几何模型**：材质已可换，但合并网格的高台/地面/地穴形状仍未改变。
  下一步应关闭原始 84 个 renderer，按 `heightType`/`tileKey` 运行时生成新的地图网格。
- 源码中已声明并注册 `map-rebuild` 命令，但 `rebuild_map_visual()` 尚未实现；
  因此当前工作区源码不能编译，最后一次成功构建的 DLL 仍可运行。

## 2. 运行时对象与关键数据

### 2.1 图形槽位

`graphic-probe` 实测（`dist/battle_graphic_probe.txt`）：

```text
graphics=84
g0  class=Torappu.Battle.Tiles.MeshTileGraphic
    name=S_Ground_out_14_029
    tileKey=tile_forbidden
    mesh=Combined Mesh (root: scene) 4 id=748764 verts=6769 submeshes=65
    renderer=UnityEngine.MeshRenderer enabled=1 staticBatch=1
    materials=1 [0=MT_wilds_A#325184]
g1  <null>
...
```

- 84 个槽位中 76 个有效，8 个 `null`。
- 所有有效图形只引用两张网格：
  - `Combined Mesh (root: scene) 4`：id `748764`，6769 顶点，65 子网格。
  - `Combined Mesh (root: scene) 5`：id `748760`，2466 顶点，67 子网格。
- 所有 renderer 的 `isPartOfStaticBatch=1`。
- `Renderer.staticBatchIndex` 返回的是**网格实例 ID**（748760/748764），
  不是子网格索引，不能用来定位单格几何。

### 2.2 材质字段

`MeshTileGraphic` 暴露了完整的材质入口：

```text
UnityEngine.Renderer m_renderer
UnityEngine.Material m_originMaterial
UnityEngine.Material m_replaceableMaterial
UnityEngine.Material m_customMaterial
UnityEngine.Material m_buildableGroundMaterial
UnityEngine.Material m_buildableHighlandMaterial
UnityEngine.Material m_focusedMaterial
UnityEngine.Color    m_customColor
System.Boolean       m_isCustomColorInitialized
```

实测有效做法：

1. 读取 `m_renderer`。
2. 用 `new Material(shader)` 或 `new Material(originMaterial)` 创建材质。
3. `Material.SetColor("_Color", color)`。
4. `Renderer.set_sharedMaterial(material)`。
5. 同步写入 `m_customMaterial`、`m_customColor`、
   `m_isCustomColorInitialized=true`，避免 `ResetTile()` 后回退。

`Sprites/Default` 在当前客户端可用，`Unlit/Color` 找不到。

### 2.3 地图坐标系

图形 transform 实测：

```text
g0 pos=(0, 3.5, -0.379653)     local=(700, 39.9653, -500)
g3 pos=(-3.015, -3.5, -0.379653) local=(0, 39.9653, -198.5)
```

- 地图位于 **XY 平面**，Z 近似常量 `-0.379653`。
- 格子间距约 `1.0`。
- 做运行时网格重建时，应在 XY 平面生成四边形，Z 稍微向相机方向偏移。

### 2.4 tileKey 已验证正确

`graphic-keys` 对 `custom_combined_verify.json` 的分色统计：

```text
按地块分色完成，绘制 76 格
tile_end=4 tile_floor=62 tile_grvtybtn=2
tile_ristar_road=3 tile_start=4 tile_wall=13
```

同一 JSON 用 PowerShell 按 `mapData.map` 统计的结果完全一致：

```text
tile_end=4 tile_floor=62 tile_grvtybtn=2
tile_ristar_road=3 tile_start=4 tile_wall=13
```

说明：

- `Tile._data.tileKey` 是自定义数据。
- `Tile.m_data` 在当前客户端为空，读取时应优先 `_data`，再回退 `m_data`。
- `graphic-dump` 里显示 `data=<empty>` 是 dump 代码读错字段，不代表数据缺失。

## 3. 新增命令

实现位置：

- `plugins/BetterBattle/battle_state.cpp`
- `plugins/BetterBattle/commands.cpp`
- `plugins/BetterBattle/BetterBattle.h`

### 3.1 已实现并验证

```text
graphic-probe
graphic-tint <r> <g> <b> [a] [shader]
graphic-keys
graphic-material <assetPath> [row col]
graphic-restore
asset-list <filter>
asset-load <assetPath>
```

- `graphic-probe`：写 `dist/battle_graphic_probe.txt`，包含每个图形的
  GameObject 名、tileKey、transform、网格 ID/顶点数/子网格数、
  renderer 状态、静态批处理状态、材质实例 ID。
- `graphic-tint`：整图 76 个有效图形统一换色。实测 `graphic-tint 1 0 1`
  成功，结果 `整图换色 76/84 格`，画面全洋红。
- `graphic-keys`：遍历运行时 `Tile`，读取 `_data.tileKey`，按类别给整图上色：
  - `tile_floor` 绿色
  - `tile_road` 蓝色
  - `tile_wall` 橙色
  - `tile_forbidden` 深灰
  - `tile_start` 亮绿
  - `tile_end` 红色
  - `tile_hole` 近黑
  - `tile_icestr` 青色
  - `tile_grvtybtn` 紫色
  - `tile_ristar_road` 蓝色
- `graphic-material`：从 `ABResourceManager.Load(string)` 加载官方 `Material`，
  应用到全部 76 格，或指定 `row col` 只应用一格。
- `graphic-restore`：恢复 `m_originMaterial` 并清空自定义材质状态。
- `asset-list`：读取 `ABResourceManager.m_assetNameToBundleInfoMap`，
  写 `dist/battle_assets.txt`。实测可枚举，单次上限 200000 条。

### 3.2 已注册但未实现

```text
map-rebuild <wild|ice|snow>
```

`BetterBattle.h` 已声明 `rebuild_map_visual()` 和 `cmd_map_rebuild()`，
`commands.cpp` 已注册命令并调用，但 `battle_state.cpp` 中**没有函数实现**。
下一次编译会报未定义符号。恢复工作时应先实现该函数，或临时移除注册。

## 4. 实测证据

### 4.1 整图洋红

```text
graphic-tint 1 0 1
-> 整图换色 76/84 格，跳过 0，shader=Sprites/Default
```

截图：`dist/screen_tint.png`。整张地图所有地面/高台/道路全部变成洋红色。

### 4.2 按 tileKey 分色

```text
graphic-keys
-> 按地块分色完成，绘制 76 格 ...
```

截图：`dist/screen_keys.png`。

- 大面积绿色 = `tile_floor`
- 红色 = `tile_end`
- 紫色 = `tile_grvtybtn`
- 蓝色 = `tile_ristar_road`
- 橙色块 = `tile_wall`（高台/墙）
- 亮绿 = `tile_start`

这证明每格 renderer 可以独立控制，且 `tileKey` 与画面一致。

### 4.3 官方冰原材质整图替换

```text
graphic-material arts/maps/map_icewild/res/materials/mt_icewild
-> 官方材质 ... 已应用到 76 格，类型 UnityEngine.Material
```

截图：`dist/screen_ice_mat.png`。

- 原 SK-5 棕色地面整体变成冰原深蓝石材质。
- 高台/墙体也使用同一官方材质，画面与基础关卡明显不同。
- 说明资源路径、材质加载、renderer 赋值链路全部可用。

### 4.4 对照截图

- `dist/screen_before.png`：未修改的 SK-5 画面。
- `dist/screen_tint.png`：全图洋红。
- `dist/screen_keys.png`：按 tileKey 分色。
- `dist/screen_ice_mat.png`：官方冰原材质。

## 5. 官方资源路径

已确认可用：

```text
arts/maps/map_wild/res/materials/mt_wilds_a
arts/maps/map_wild/res/materials/mt_wilds_a_hg
arts/maps/map_wild/res/materials/mt_wilds_alpha
arts/maps/map_icewild/res/materials/mt_icewild
arts/maps/map_icewild2/res/materials/mt_icewild2
arts/maps/map_snowvillage/res/material/mt_snowvillage
arts/maps/map_snowvillage/res/material/mt_snowvillageboss
battle/prefabs/[uc]tiles/<tileKey>
battle/prefabs/effects/tile_start
battle/prefabs/effects/tile_end
battle/prefabs/effects/tile_forbidden
```

说明：

- `battle/prefabs/[uc]tiles/<tileKey>` 只有逻辑组件，没有 `MeshRenderer`，
  不能直接当可视地块实例化。
- `battle/prefabs/effects/tile_start/end/forbidden` 是官方标记特效 prefab，
  可用于在地块位置补充起点/终点/禁部署标记。
- `asset-load scenes/.../level_weekly_armor_5` 返回 `null`，
  场景资源不能走普通 `Load`。
- `ABResourceManager` 另有 `LoadScene(string, LoadSceneMode)` /
  `LoadSceneAsync(...)`，尚未验证；可作为加载其他主题场景、提取官方网格的入口。

## 6. 已排除或失败的路径

前一轮已排除：

- 只改 `TileData.tileKey`：逻辑生效，画面不变。
- 改 `Tile._graphic` 指针：指针变化，画面不变（所有 renderer 共享合并网格）。
- 复制 `MeshFilter.sharedMesh` + `Material`：源/目标本来就共享同一网格，画面不变。
- `tile-instantiate` / `tile-fill`：实例化的 tile prefab 没有渲染组件，画面不变。
- 场景资源普通 `Load`：返回 `null`。
- `Unlit/Color` shader：当前客户端不存在；`Sprites/Default` 可用。

仍需注意：

- `Renderer.staticBatchIndex` 不是子网格索引，不能据此直接提取单格网格。
- `Mesh.GetSubMesh(int)` 未暴露；但 `Mesh.GetTriangles(List<int>, int)`、
  `Mesh.GetVertices/GetNormals/GetUVs`、`Mesh.SetTriangles` 可用，
  理论上可提取合并网格的某个子网格。
- `graphic-material` 只作用于 `MapGraphic._graphics` 中的 76 个有效图形；
  场景中不在这 84 个槽位里的装饰物不会跟随。

## 7. 下一步：模型级重建（用户明确优先级）

用户明确表示：

> 相比于 shader，更重要的是改变模型，就是高台、地面、地穴一类的。

因此下一步不是继续调材质，而是重建可视几何。建议方案：

### 7.1 关闭原始地图渲染

遍历 `MapGraphic._graphics` 的 84 个槽位，对每个有效 renderer：

```text
Renderer.set_enabled(false)
```

保留 `Tile`、`TileData`、碰撞、寻路、可部署判定不变，只替换画面。

### 7.2 按类别生成新网格

遍历 `Map` 的所有 `Tile`：

- 读取 `Tile._data`（TileData）：
  - `tileKey`
  - `heightType`（0=LOWLAND，1=HIGHLAND）
  - `buildableType`、`passableMask`
- 读取 `Tile.transform.position` 得到格心 `(x, y, z)`。
- 按类别生成：
  - **LOWLAND / tile_floor / tile_road**：XY 平面四边形，贴近原 Z。
  - **HIGHLAND / tile_wall**：顶部四边形 + 向下延伸的正面/侧面，
    高度取 `Tile.get_height`；若为 0，用约 `0.45` 作为视觉高度。
  - **tile_hole**：暗色凹陷四边形，Z 向相机反方向偏移，或加一圈井壁。
  - **tile_start/tile_end**：在基础地块上叠加绿色/红色小四边形标记。
  - **tile_icestr/tile_grvtybtn/tile_ristar_road**：叠加青色/紫色/蓝色标记，
    或使用对应官方材质。
- 每个材质类别单独建一个 Mesh + GameObject，避免运行时构造 `Material[]`：
  - `BB_Map_Floor`
  - `BB_Map_Highland`
  - `BB_Map_Hole`
  - `BB_Map_Start`
  - `BB_Map_End`
  - `BB_Map_Special`

### 7.3 Mesh 构造 API

当前运行时已验证/可用：

```text
new UnityEngine.Mesh()
Mesh.SetVertices(List<Vector3>)
Mesh.SetNormals(List<Vector3>)
Mesh.SetUVs(int, List<Vector2>)
Mesh.SetTriangles(List<int>, int)
Mesh.RecalculateBounds()
```

建议用 `List<T>` 累积，调用 `Add` 写入；不要依赖 `Array.SetValue` 的装箱路径。
每个四边形建议正反两面都写入，避免相机方向导致背面剔除。

### 7.4 材质策略

- wild：`mt_wilds_a`（地面）+ `mt_wilds_a_hg`（高台）
- ice：`mt_icewild` 或 `mt_icewild2`
- snow：`mt_snowvillage` 或 `mt_snowvillageboss`
- hole：`Sprites/Default` + 近黑色
- start/end/special：`Sprites/Default` + 纯色，半透明或小尺寸叠加

### 7.5 风险点

- GameObject/MeshFilter/MeshRenderer 的运行时创建与 `AddComponent(Type)` 尚未实测。
- `Mesh.SetUVs` 的泛型重载需要用方法参数类型筛选。
- 网格绕序、Z 偏移方向、高台高度需要一次实机校准。
- 重建后要确认干员/敌人仍站在正确高度，不能只对齐画面。
- 关闭原始 renderer 后，地图高亮/可部署提示是否仍正常需要验证。

## 8. 三张验收地图的当前状态

现有文件均在 `build-cmake/dist/`：

```text
custom_showcase_1_bigbo_tower.json       11x8
custom_showcase_2_wpnsts_blizzard.json   11x8
custom_showcase_3_gravity_path.json      11x8
custom_combined_verify.json              11x8
```

当前统计：

| 文件 | 敌人 | 装置 | 地块特点 |
| --- | --- | --- | --- |
| showcase_1 | slime / slime_2 / bigbo | trap_014_tower | 13 高台，2 起点，2 终点 |
| showcase_2 | slime / slime_2 | trap_057_wpnsts、trap_050_blizzard | 2 冰面 |
| showcase_3 | slime / slime_2 | trap_121_gractrl | 2 重力按钮、5 孤星道路 |

与用户要求的差距：

1. showcase_1 的“大型敌人”仍是 `enemy_1001_bigbo`。
   用户明确要求类似“萨米的意志”的**巨型单位标签**敌人，
   应改为 `enemy_2054_smdeer`（运行时 `EnemyGiantBoss`，`giant_boss=1`）。
2. 三张图底排目前只有两端是 `tile_wall`，不是“底部一整排高台”。
   需要把 JSON 最后一行 11 格全部改为 `tile_wall`
   （`HIGHLAND / RANGED / FLY_ONLY`），并同步调整路线起点，
   避免敌人从高台/不可通行格出生。
3. 需要九人编队进入并暂停验收；编队使用 `run-test.ps1` 默认九人：

```text
char_4244_tocka:90:2:6:3:10,char_4247_oxi:90:2:6:3:10,
char_1050_chen3:90:2:6:3:10,char_4202_haruka:90:2:6:2:10,
char_1015_aglna2:90:2:6:3:10,char_377_gdglow:90:2:6:2:10,
char_2023_ling:90:2:6:3:10,char_1045_svash2:90:2:6:2:10,
char_1029_yato2:90:2:6:3:10
```

## 9. 复现步骤

```powershell
cd E:\Games\ArkTest\ArkFramework
.\run-test.ps1 -SkipBuild -NoBattle -TimeoutSeconds 90
```

到达主界面后：

```text
start-json E:\Games\ArkTest\ArkFramework\build-cmake\dist\custom_combined_verify.json wk_armor_5 <九人编队>
pause
graphic-probe
graphic-keys
graphic-material arts/maps/map_icewild/res/materials/mt_icewild
```

命令通过写 `build-cmake\dist\BetterBattle.request` 发送，插件约 250 ms 轮询一次。
连续发送时每条之间至少等待 1.5 秒，避免覆盖。

## 10. 暂停时的源码/运行状态

- 最后成功构建：`build-cmake/dist/plugins/BetterBattle.dll`
  `2026-11-12 22:09:24`，2,938,942 字节。
- 该 DLL 已包含并验证：
  `graphic-probe`、`graphic-tint`、`graphic-keys`、`graphic-material`、
  `graphic-restore`。
- 游戏仍在运行：PID `32516`，已进入 `custom_combined_verify.json` 并暂停；
  当前画面是官方冰原材质整图替换后的状态。
- 源码在最后一次构建后新增了 `map-rebuild` 声明与命令注册，
  但 `rebuild_map_visual()` 未实现，**当前源码不可编译**。
- 恢复工作第一步：实现 `rebuild_map_visual()`，或先移除
  `map-rebuild` 注册以恢复可编译状态。

## 11. 本次修改文件

```text
plugins/BetterBattle/BetterBattle.h
plugins/BetterBattle/commands.cpp
plugins/BetterBattle/battle_state.cpp
docs/MapRenderingResearch.md
docs/CustomLevelAuthoring.md
docs/MapVisualOverrideProgress.md   （本文）
```

运行时产物：

```text
build-cmake/dist/battle_graphic_probe.txt
build-cmake/dist/battle_assets.txt
build-cmake/dist/assets_wilds2.txt
build-cmake/dist/assets_mtwilds.txt
build-cmake/dist/assets_icestr.txt
build-cmake/dist/assets_grvty.txt
build-cmake/dist/assets_mtsnow.txt
build-cmake/dist/assets_mtice.txt
build-cmake/dist/assets_mapsnow.txt
build-cmake/dist/assets_mapice.txt
build-cmake/dist/screen_before.png
build-cmake/dist/screen_tint.png
build-cmake/dist/screen_keys.png
build-cmake/dist/screen_ice_mat.png
```

## 12. 代码注意点

- C++ 中 `f(args.next(), args.next())` 的参数求值顺序未指定；
  当前 MinGW 实际为从右到左，曾导致 `object-scan` 参数颠倒。
  新命令应逐个 `args.next()` 存入局部变量，避免依赖求值顺序。
- `read_field_deep` 读不到字段时返回 `false`，`read_string_field`
  可能抛异常；读取 `_data`/`m_data` 时要加回退和 `try/catch`。
- 给 `MeshTileGraphic` 换材质后，`ResetTile()` 可能恢复原材质；
  必须同步写 `m_customMaterial` 和 `m_isCustomColorInitialized`。
- 场景资源与普通资源走不同加载链；不要再用 `Load` 加载 `scenes/...`。

## 13. 2026-11-13 更新：官方盒体拼接与高亮保留

### 13.1 资源结论

- 资源索引里没有 `S_Ground_out_*` 这类单格 Mesh 资产；地图主题目录下只有
  `background_mesh`、材质和贴图，没有逐地块网格。
- `arts/effects/[pack]map/meshes/box` 是可实例化的 `GameObject` prefab，
  运行时 `MeshFilter.sharedMesh = pPlane2`（100 顶点），
  `bounds = center(0,0,0), extents(50,50,50)`，即 100x100x100。
- 该 prefab 默认材质是 `end_01`，实例化后可以把 `Renderer.sharedMaterial`
  换成主题材质；已验证 `MT_wilds_A`、`MT_wilds_A_HG` 生效。

### 13.2 运行时 Z 平面

`renderer-dump` 统计当前 SK-5 场景的合并网格 renderer：

```text
z=-0.380 total=49 enabled=37
z=+0.020 total=35 enabled=34
```

两个主平面差值为 `0.4`，分别对应地面和高台。`map-rebuild` 现在对
`MapGraphic._graphics` 的 Z 做聚类，取最低的主要平面作地面、最高的主要平面
作高台，差值作为台高，再整体偏移 `-0.01` 避免与原始可部署高亮 Z-fighting。
11x8 条纹图结果：

```text
Z=-0.889813..0.0101869
地面顶面 -0.389813
高台顶面  0.0101869
地穴顶面 -0.489813
```

主线 4-4 的原始平面是 `-0.28 / +0.02`，台高 `0.3`；13x9 条纹图自动得到
`Z偏移=-0.28`、`台高=0.30`、`Z=-0.689794..0.0100153`。

### 13.3 盒体模式

`map-rebuild box` / `box-ice` / `box-snow` / `box-debug` 已实现：

- 逐格实例化官方 `box` prefab，按 bounds 自动缩放，顶面对齐到目标高度。
- 默认 `box_theme=auto`：直接复用当前基础关卡原生地块材质，SK-5 为
  `MT_wilds_A`，主线 4-4 为 `MT_Chernobog_Ground` /
  `MT_Chernobog_B_Outworld`；`box-ice` / `box-snow` / `box-debug` 可显式覆盖。
- 地穴深色；起终点/冰面/重力/道路和未知特殊 tileKey 使用薄标记盒。
- 11x8 条纹图：88 盒体；`custom_showcase_1`：88 盒体 + 4 标记。
- 13x9 条纹图 + `main_04-04`：117 盒体，自动材质与台高均正确。
- 自建网格模式保留为 `map-rebuild debug` / `map-rebuild wild`，用于对照。

### 13.4 可部署高亮

直接隐藏 76 个 `MeshTileGraphic` 会让拖动干员时的绿色可部署格消失。现在改为：

- 保留 76 个 renderer 启用，把材质换成全透明，并同步写
  `m_originMaterial` / `m_replaceableMaterial` / `m_customMaterial`。
- 拖动时游戏仍会切到 `m_buildableGroundMaterial`，绿色高亮保留。
- 额外 60 个装饰 renderer 禁用；`map-rebuild off` 会恢复原材质并重新启用。
- 原材质在 `saved_graphic_materials_` 中按对象缓存，避免 `off` 恢复成透明。

### 13.5 复现

```text
start-json ...\custom_ztest_rows.json wk_armor_5 <九人编队>
pause
map-rebuild box
renderer-dump
map-rebuild off
```

当前未完成项：`pPlane2` 缩放后的 UV/纹理密度、高台单位遮挡、地穴顶面处理，
以及 88 个独立 renderer 的合并优化。
