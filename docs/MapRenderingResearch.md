# 自定义地图显示问题研究记录

本文记录 `start-json` 自定义地图“逻辑正确但美术不跟着变”的排查过程、已排除的
路径和下一步方案。结论先行：当前问题不是 `tileKey` 写错，而是地图美术在运行时
是一张**合并后的静态网格**，`TileData.tileKey` 只进入逻辑数据，不会重建网格。

## 1. 现象

同一张自定义 JSON 中：

- `mapData.map` 的地块索引、可部署位置、可通行性、地穴/冰面/重力机关效果都正确。
- `tileKey` 改成 `tile_grvtybtn`、`tile_icestr`、`tile_hole` 后，逻辑和
  `blackboard` 生效。
- 画面仍然显示基础关卡（例如 SK-5）的原生地块外观。

## 2. 运行时对象关系

`Map` 的关键对象：

```text
BattleController
  -> Map
       -> m_data (MapData)
       -> _graphic (MapGraphic)
            -> _graphics (TileGraphic[])
       -> _tiles (Tiles2D)
```

每个 `Tile`：

```text
Tile
  _tileKey       运行时显示/预制体键，来自基础场景
  _graphic       TileGraphic 组件
  m_data         TileData，来自自定义 LevelData
```

`graphic-dump` 输出的实测结果（`dist/battle_graphics.txt`）：

```text
map_graphic=Torappu.Battle.MapGraphic graphics=84
0:4 runtime=tile_start data=<empty> graphic=81 mesh=Combined Mesh (root: scene) 5 mat=MT_wilds_A
4:1 runtime=tile_wall  data=<empty> graphic=48 mesh=Combined Mesh (root: scene) 5 mat=MT_wilds_A
5:2 runtime=tile_floor data=<empty> graphic=20 mesh=Combined Mesh (root: scene) 5 mat=MT_wilds_A
```

关键点：

- 84 个 `MeshTileGraphic` 全部指向同一张
  `Combined Mesh (root: scene) 5`，并使用同一材质 `MT_wilds_A`。
- `Tile._tileKey` 仍是基础关卡的值（例如 `tile_wall`），不是自定义 JSON 的
  `tile_grvtybtn`。
- 自定义地块的 `blackboard` 确实进入了运行时；`probe` 中能看到
  `source_direction=0;activate_block_cnt=1;environment_dmage_flag=1`，说明逻辑层已生效。

## 3. 已尝试路径

### 路径 A：只改 `TileData.tileKey`

结果：逻辑生效，画面不变。

原因：`Map` 初始化时用基础场景的合并网格建立 TileGraphic；`SetData` 更新的是
`Tile.m_data`，没有重新实例化 TileGraphic。

### 路径 B：改 `Tile._graphic` 指针

新增调试命令：

```text
graphic-assign <row> <col> <graphicIndex>
```

实测：

```text
graphic-assign 5 1 81
已把 graphic[81] 绑定到地块 5,1
5:1 ... graphic=81
```

结果：`Tile._graphic` 指针已改为目标 `MeshTileGraphic`，但画面没有可辨识变化。

原因：`_graphics` 是一个场景内的图形对象池，实际渲染仍由合并网格/材质决定；
单独换指针不会重建合并网格，也不会把目标地块的几何写进合并网格。

### 路径 C：复制 `MeshFilter.sharedMesh` + `MeshRenderer` 材质

新增调试命令：

```text
graphic-copy <srcRow> <srcCol> <dstRow> <dstCol>
```

实测：

```text
graphic-copy 0 4 5 2
已复制地块图形 0,4 -> 5,2
```

再次 `graphic-dump` 后，源/目标/普通地面的网格和材质仍都是：

```text
mesh=Combined Mesh (root: scene) 5
mat=MT_wilds_A
```

结果：复制成功但外观仍不变。

原因：源和目标引用的是同一个合并网格与同一材质；复制共享引用不会改变合并网格
内部对应地块的几何/子网格。

### 路径 D：换基础关卡场景

已验证：

- 11x8 自定义图 + `wk_armor_5` -> SK-5 场景。
- 13x9 自定义图 + `main_04-04` -> 主线 4-4 场景。
- 12x8 自定义图 + `act25side_08` -> CW-8 场景。

结果：换基础关卡可以换背景、主题和整体美术，但地图布局仍然受基础场景烘焙网格
限制；自定义 JSON 的 `tileKey` 仍不会逐格改美术。

## 4. 根因

当前客户端的地图显示不是“每个 Tile 一个独立 prefab”的运行时拼装，而是：

1. 基础关卡场景预制体里已经烘焙好一张
   `Combined Mesh (root: scene) 5`。
2. `MapGraphic` 持有一组 `MeshTileGraphic`，它们共享这张合并网格和材质。
3. `TileData`/`tileKey` 用于高度、可部署、可通行、机关 blackboard 等逻辑。
4. `Map.SetData`/`Tile.SetData` 更新逻辑数据，但没有重建合并网格。

因此，单纯改 JSON 的 `tileKey` 不可能改变已烘焙的美术。

## 5. 可行的解决方向

### 方案 1：基础场景匹配（低成本，已验证）

按目标美术选择基础关卡，并让自定义地图尺寸与该关卡一致。适合“借用官方主题”，
不适合在同一张图混用冰面、重力、风雪等不同主题素材。

### 方案 2：运行时生成/重建地图网格（中等成本）

读取 `MapData.map` 和 `TileData`，为每个格子创建独立 `MeshTileGraphic` 或
`MeshFilter`/`MeshRenderer`，用 `tileKey` 查表加载对应网格/材质，然后替换
`Map._graphic` 或关闭合并网格渲染。需要确认：

- `MeshTileGraphic` 的网格/材质资源键。
- `MapGraphic._graphics` 的初始化顺序。
- 地图层 `MapLayer`、高度偏移和寻路碰撞体的同步。

### 方案 3：自定义地图 prefab / AssetBundle（高成本，最彻底）

制作包含目标 `tileKey` prefab 的地图资源包，启动时加载并替换 `MapGraphic`。
这是真正支持任意官方/自制素材的路线，但需要资源打包和 AssetBundle 加载链。

### 方案 4：按主题加载不同 TileGraphic 池

把不同主题的 TileGraphic 池合并到一个 MapGraphic，使用 `tileKey` 映射到不同
网格/材质。它比方案 3 轻，但仍需要解决合并网格和不同主题资源的加载。

## 6. 调试命令

```text
graphic-dump
graphic-assign <row> <col> <graphicIndex>
graphic-copy <srcRow> <srcCol> <dstRow> <dstCol>
```

- `graphic-dump` 写 `dist/battle_graphics.txt`，包含 84 个 TileGraphic 的
  GameObject 名、Tile 的 `_tileKey`、`_graphic` 下标、mesh 名和材质名。
- `graphic-assign` 只改 Tile 的 `_graphic` 指针，用于验证引用关系。
- `graphic-copy` 复制 sharedMesh/sharedMaterials，用于验证是否是逐格网格。

当前实验表明后两者只能证明“不是逐格网格”，不能解决显示。

## 7. 当前结论

显示与地图不符的根因是**基础场景的烘焙合并网格**，不是 `tileKey` 字段错误。

已排除：

- `tileKey` 拼写/枚举错误。
- `TileData` 未进入运行时。
- `Tile._graphic` 指针未切换。
- `MeshFilter.sharedMesh`/材质未复制。

尚未实现：

- 逐格重建合并网格。
- 从 `[uc]tiles.ab` 或主题资源包加载任意 `tileKey` 网格。
- 自定义地图 prefab/AssetBundle。

在实现方案 2 或 3 前，`tileKey` 只能保证逻辑正确，不能保证画面逐格对应。

## 8. 2026-11-12 更新：逐格材质与官方素材已验证

本节记录阶段 7 之后的实测突破，详细命令、截图和未完成状态见
[`MapVisualOverrideProgress.md`](MapVisualOverrideProgress.md)。

### 8.1 逐格 renderer 可以独立控制

`MapGraphic._graphics` 的 84 个槽位中有 76 个有效
`Torappu.Battle.Tiles.MeshTileGraphic`。虽然它们只共享两张合并网格
（`Combined Mesh (root: scene) 4/5`），但每个图形有独立的 `m_renderer`，
可以逐格替换材质：

```text
graphic-tint 1 0 1
-> 整图换色 76/84 格，shader=Sprites/Default
```

结果：整张地图一眼变成洋红色（`dist/screen_tint.png`）。

### 8.2 按 tileKey 分色与 JSON 完全一致

`graphic-keys` 读取 `Tile._data.tileKey` 后分色，`custom_combined_verify.json`
的统计为：

```text
tile_end=4 tile_floor=62 tile_grvtybtn=2
tile_ristar_road=3 tile_start=4 tile_wall=13
```

与直接解析 JSON 的结果完全一致（`dist/screen_keys.png`）。
注意：`Tile._data` 才是自定义数据，`Tile.m_data` 在当前客户端为空；
旧 `graphic-dump` 的 `data=<empty>` 是字段读错。

### 8.3 官方材质可运行时加载并整图替换

```text
graphic-material arts/maps/map_icewild/res/materials/mt_icewild
-> 已应用到 76 格
```

结果：原 SK-5 画面整体变成冰原石材质（`dist/screen_ice_mat.png`）。
已验证的官方材质路径包括：

```text
arts/maps/map_wild/res/materials/mt_wilds_a
arts/maps/map_wild/res/materials/mt_wilds_a_hg
arts/maps/map_icewild/res/materials/mt_icewild
arts/maps/map_icewild2/res/materials/mt_icewild2
arts/maps/map_snowvillage/res/material/mt_snowvillage
```

`asset-list <filter>` 现在可以枚举 `ABResourceManager` 的资源字典，
单次上限 200000 条。

### 8.4 仍缺的是几何模型

`Renderer.staticBatchIndex` 返回的是网格实例 ID，不是子网格索引，
不能直接用来定位单格几何。当前已能换材质/颜色，但高台、地面、地穴的
**形状**仍来自基础场景合并网格。

用户已明确优先级：模型类别（高台/地面/地穴）比 shader 更重要。
下一步方案是：

1. 关闭 84 个原始 renderer。
2. 遍历运行时 `Tile`，按 `heightType`/`tileKey` 生成 XY 平面四边形；
   高台加高度和侧面，地穴做暗色凹陷。
3. 每个材质类别单独建 Mesh + GameObject，使用已验证的官方材质。

具体 API、风险点和三张验收图的差距见
[`MapVisualOverrideProgress.md`](MapVisualOverrideProgress.md) 第 7、8 节。
