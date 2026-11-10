# 肉鸽战斗机制证据台账

更新时间：2026-11-05

这份文档区分三类信息，后续不要把推测当成实测：

- **实测**：当前客户端实际进入本地战斗，通过 BetterBattle 运行时探针读到对象或属性。
- **静态证实**：只来自客户端资源、IL2CPP 类型或反汇编，未在真实战斗里跑过。
- **推测**：为了继续实现而采用的解释或外部描述，必须保留待验证标记。

本地战斗使用 `rogue-start`，只加载关卡资源，不推进真实肉鸽存档，也不产生正常战斗结算。

## 源石之城（RO6 `variation_7`）

### 结论

**静态证实**

- 主题表位置：`gamedata/excel/roguelike_topic_table.json`，RO6 `variationData.variation_7`。
- 名称：`“源石之城”`。
- 描述：`完成节点后获得源石锭，敌方属性随源石锭数量提升`。
- 对应全局 Buff prefab：`rogue_6_wooden_weather_7`，`targetSide=2`（敌方）。
- 全局 Buff 资产位于 `battle/prefabs/[uc]globalbuffs.ab`，条目名为
  `rogue_6_wooden_weather_7`，内部 Buff 模板也是
  `rogue_6_wooden_weather_7`。
- 模板 `gamedata/battle/buff_template_data.json` 的启动逻辑：
  1. `AssignGoldToBlackboard` 把 `BattleRoguelikeMeta.gold` 写入 `dynamic`。
  2. `cnt = dynamic / value`。
  3. `atk = atk * cnt`，上限键为 `atk_max`。
  4. `max_hp = max_hp * cnt`，上限键为 `max_hp_max`。
  5. 创建 `rogue_6_wooden_weather_7_attri`，属性类型为 `ATK` 和 `MAX_HP`，
     `formulaItem=MULTIPLIER`。
- 客户端 prefab 的 `blackboard` 是空数组，说明具体倍率并不是 prefab 写死的。
  真实服务器需要把 `value / atk / max_hp / atk_max / max_hp_max` 放进
  `unKeepBuff.blackboard`；这一点来自模板和资产结构，尚未捕获到真实服务器原始条目。
- `atk_max/max_hp_max` 是必需键。实测不传这两个键时，虽然
  `m_globalBuffs` 仍然生成，但敌人属性完全不变；因此不能只传
  `value/atk/max_hp`。

**已实测**

- 测试关卡：`rogue_6 / ro6_n_1_1`，
  `Obt/Roguelike/RO6/level_rogue6_1-1`。
- 测试编队：`char_4244_tocka`、`char_110_deepcl`。
- 注入参数：

```text
--gold=<N>
--outerbuff=global_buff_normal|rogue_6_wooden_weather_7|value=1;atk=0.004;max_hp=0.01;atk_max=999;max_hp_max=999
```

其中 `value=1;atk=0.004;max_hp=0.01` 是根据“每 1 源石锭攻击 +0.4%、生命 +1%”
构造的测试参数；第一轮 `atk_max/max_hp_max=999` 只是为了避免测试值被上限截断，
**不是**声称真实服务器配置就是 999。

- `gold=0` 时，运行时读取到 `meta.gold=0`。
- `gold=100` 时，运行时读取到 `meta.gold=100`。
- 两次战斗都得到 `m_globalBuffs count=1`，prefabKey 为
  `rogue_6_wooden_weather_7`，模板为
  `rogue_6_wooden_weather_7`。

同一关卡、同一敌人模板的实测属性：

| 敌人 ID | 对象类别 | gold=0 HP | gold=0 ATK | gold=100 HP | gold=100 ATK | 结论 |
|---|---|---:|---:|---:|---:|---|
| `enemy_2133_shdopl` | 受影响的敌方单位 | 5000 | 320 | 10000 | 448 | HP +100%，ATK +40% |
| `enemy_1034_laxe` | 受影响的敌方单位 | 9000 | 850 | 18000 | 1190 | HP +100%，ATK +40% |
| `enemy_3001_upeopl` | 未被影响的对象 | 3500 | 0 | 3500 | 0 | HP/ATK 均不变 |

同一批对照中：

- 受影响敌人的 `DEF` 不变（例如 `enemy_2133_shdopl` 始终为 100）。
- `gold=100` 与公式 `0.004 * 100 = 0.4`、`0.01 * 100 = 1.0` 完全吻合。
- `enemy_3001_upeopl` 未受影响。它可能是非 `targetSide=2` 的中立/特殊对象，
  但具体对象侧别尚未读取验证；这里只把它记录为“实测未受影响”，不把原因写成已证实。

### 上限验证

公开资料描述该效果最多 100 层，对应测试参数
`atk_max=0.4`、`max_hp_max=1.0`。这里把“100 层”标记为**外部资料 + 推导**，
把下面的结果标记为**本地实测**：

| 注入参数 | meta.gold | `enemy_2133_shdopl` HP / ATK | 结果 |
|---|---:|---:|---|
| `atk=0.004;max_hp=0.01`，不传 max 键 | 100 | 5000 / 320 | 无加成 |
| `atk_max=0.4;max_hp_max=1.0` | 50 | 7500 / 384 | +50% / +20% |
| `atk_max=0.4;max_hp_max=1.0` | 100 | 10000 / 448 | +100% / +40% |
| `atk_max=0.4;max_hp_max=1.0` | 150 | 10000 / 448 | 与 100 相同，已封顶 |

因此本地已经验证：

- 公式按 gold 线性计算。
- `atk_max=0.4`、`max_hp_max=1.0` 确实产生 100 层封顶行为。
- **仍未验证**真实服务器报文是否就是这两个具体值；目前没有捕获到真实
  `variation_7` 的 `unKeepBuff.blackboard`。

原始证据文件：

```text
_captures/rogue_source_city_20261105/gold0_dump.txt
_captures/rogue_source_city_20261105/gold100_dump.txt
_captures/rogue_source_city_20261105/gold50_cap100_dump.txt
_captures/rogue_source_city_20261105/gold150_cap100_dump.txt
_captures/rogue_source_city_20261105/gold100_no_caps_dump.txt
_captures/rogue_source_city_20261105/gold0_global_buffs.json
_captures/rogue_source_city_20261105/gold100_global_buffs.json
```

### 仍未验证

- 真实服务器给 `variation_7` 下发的 `atk_max`、`max_hp_max` 是否恰好为
  `0.4/1.0`（本地行为已与 100 层上限一致，但没有原始报文）。
- 真实局的源石锭是否按战斗开始时的值固定，还是会在战斗中途变化。
- 敌方单位身上的派生 Buff 层数；从模板和 `m_globalBuffs count=1` 看，机制是
  “读取 gold 计算一个派生修正”，不是把 100 个层逐个叠加，但本次没有直接读取
  敌方 `BuffContainer.stackLayer`。
- 是否只影响 `targetSide=2`，还是还包含某些特殊对象。中性对象未受影响是实测事实，
  侧别归因仍是推断。

## 孤立石林（RO6 `variation_4`）

**已实测**

- 通过 `char_ability_new` 注入：

```text
--outerbuff=char_ability_new|rogue_6_wooden_weather_4|rogue_6_wooden_weather_4[real].attack_speed=30;rogue_6_wooden_weather_4[down].attack_speed=-50
```

- 处理器：`Torappu.Battle.Roguelike.Internal.CDynamicAbilityNew`。
- 托卡单独部署时为 `aspd=50`（-50）。
- 深海色进入托卡攻击范围后，托卡为 `aspd=130`（+30）；深海色自身为
  `aspd=58`（108-50），说明条件按每个单位自己的攻击范围独立计算。
- 单位 Buff：`rogue_6_wooden_weather_4[real]`，modifier 为
  `ATTACK_SPEED+30`。

相关原始 dump：

```text
ArkFramework/build-cmake/dist/battle_rogue_dump.txt
ArkFramework/build-cmake/dist/battle_buffs.txt
```

## 倾斜沙丘（RO6 `weather_2`）

**已实测（`_b` / 1 级，真实局，`modeGrade=6`）**

- 真实关卡：`Obt/Roguelike/RO6/level_rogue6_t-11`，暂停时 `frame=489`。
- 玩家局数据：`mode=2`、`modeGrade=6`、`equivalentGrade=6`。
- 气象数据：

```text
weather main=rogue_6_weather_2_b sub=rogue_6_subweather_1 eye=703 step=2
effectArea count=17
```

- 战斗内两条关键 `outerBuffs`：

```text
[87] key=misc_add_env_system
     key=env_047_rogue6_storm
     trig_type=TAKE_OUT
     atk=-0.35
     level=1

[88] key=enemy_attribute_mul
     move_speed=-0.15
```

- 敌方 `enemy_1074_dbskar_2` 实测 `moveSpeed=0.17`；基础 `0.2 × 0.85 = 0.17`，
  确认敌方移速 -15%。
- 结论：倾斜沙丘的真实载体是**环境系统** `env_047_rogue6_storm`，不是
  `char_attribute_mul`。`_b` 的攻击力惩罚为 `atk=-0.35`，方向/挡风判定由该
  环境系统处理；`atk=-0.2/-0.5` 为 0/2 级按描述推导。
- 本局同时确认 grade 6 的难度数值：`m_globalBuffs` 里有 4 组
  `enemy_atk_down atk=1.02` / `enemy_max_hp_down max_hp=1.02`，对应
  zone_1..zone_4 每层一次，叠乘为 `1.02^4`。另外 grade 5 的
  `max_hp=1.3` 对 NORMAL/ELITE/BOSS 各有一条。用本局
  `enemy_1074_dbskar_2` 对账：

```text
基础 HP 22000 / ATK 850
HP  = 22000 × 1.30 × 1.02^4 = 30957.56  （实测 30957.6）
ATK = 850   × 1.02^4       = 920.067    （实测 920.067）
```

- 因此难度不是“没有数值”，而是数值写在 `preprocessGlobalBuff` /
  `m_globalBuffs`，不是 `outerBuffs`。本地 `rogue-start` 目前没有生成这些
  GlobalBuff，需要按 grade + 当前 zone 数补齐。

原始证据：

```text
ArkFramework/build-cmake/dist/battle_rogue_dump.txt
ArkFramework/build-cmake/dist/rogue_player_dump.txt
ArkFramework/build-cmake/dist/rogue_player_data.json
```

注入命令：

```text
--outerbuff=misc_add_env_system||key=env_047_rogue6_storm;trig_type=TAKE_OUT;atk=-0.35;level=1
--outerbuff=enemy_attribute_mul||move_speed=-0.15
```

## 黑流地脉（RO6 `weather_1`）

**已实测（早期 `_a`，真实局）**

- 实际加载关卡：`Obt/Roguelike/RO6/level_rogue6_4-7`
  （基础关卡，不是 `dlc1` 变体）。
- 运行到 frame 2553 时观测到第二只 `enemy_2137_shsdgo`（猎犬proto），
  UID `403`。
- `outerBuffs[70]` 是：

```text
key = level_hidden_group_enable
blackboard:
  key = shsdgo
```

- 关卡文件 `level_rogue6_4-7.json` 有 20 条隐藏组猎犬 proto 刷新动作：
  `shsdgo` 4 条、`shsdgo4` 8 条、`shsdgo3` 4 条、`shsdgo2` 4 条。
  天气 `_a` 打开的是 `shsdgo` 组；因此额外猎犬不是凭空生成，而是关卡预置的
  hidden group 被天气激活。
- 黑流地脉的属性部分走两条普通全局 Buff：

```text
global_buff_normal
  key = enemy_max_hp_down
  max_hp = 1.1

global_buff_normal
  key = enemy_atk_down
  atk = 1.1
```

- 实测猎犬 proto：
  `HP 21384 / ATK 332.64 / DEF 300 / moveSpeed 0.65`。
- 基础敌人数据为 `HP 18000 / ATK 280 / DEF 300`。
  当前局还有其它全局倍率 `×0.9` 和猎犬层数藏品 `×1.2`，因此：

```text
18000 × 1.1 × 0.9 × 1.2 = 21384
280   × 1.1 × 0.9 × 1.2 = 332.64
```

- 这同时确认 `rogue_6_enemy_layer_multi` 的 `+20%` 会作用到
  `enemy_2137_shsdgo`，但它来自本局的稀有藏品链，不是黑流地脉本身。
- 当前已确认不是关卡变体差异：真实局 `stage=level_rogue6_4-7`，
  `dlc1` 版本也拥有同样的 20 条隐藏猎犬动作，并未被加载。

原始证据：

```text
_captures/rogue_blackflow_20261106/battle_rogue_dump.txt
_captures/rogue_blackflow_20261106/battle_rogue_dump_hound2.txt
_captures/rogue_blackflow_20261106/global_buffs.json
_captures/rogue_blackflow_20261106/status_hound2.txt
```

**本地注入候选**

```text
--outer=level_hidden_group_enable:key:shsdgo
--outerbuff=global_buff_normal|enemy_max_hp_down|max_hp=1.1
--outerbuff=global_buff_normal|enemy_atk_down|atk=1.1
```

隐藏组对应关系：

| 天气等级 | 属性倍率 | 打开的隐藏组 | 关卡内猎犬动作数 | 证据 |
| --- | --- | --- | ---: | --- |
| `_a` 早期 | `atk=1.1, max_hp=1.1` | `shsdgo` | 4 | 本局真实样本实测 |
| `_b` 中期 | `atk=1.2, max_hp=1.2` | `shsdgo2` 或 `shsdgo3`（随机补全） | 4 | 真实规则未知；按用户要求随机二选一 |
| `_c` 晚期 | `atk=1.3, max_hp=1.3` | `shsdgo4` | 8 | 用户实机观测 |

`_c` 的本地注入候选：

```text
--outer=level_hidden_group_enable:key:shsdgo4
--outerbuff=global_buff_normal|enemy_max_hp_down|max_hp=1.3
--outerbuff=global_buff_normal|enemy_atk_down|atk=1.3
```

`_b` 的真实规则仍未确认。当前配置生成器为了让 UI 可用，在
`shsdgo2` 和 `shsdgo3` 中随机选择一个，每次生成本地战斗参数时独立随机；
这不是真实服务器规则，不能反过来当成已验证结论。

### 黑流树海测试范围排除

- `variation_6`「未亡者遗怨」：生成更多“居民”的恶意，属于战斗外/地图生成条件，
  不作为战斗内 Buff 注入项测试，也不要求本地战斗复现。

## 悲伤的红 / 佣兵的饰物（叠层藏品）

**已实测**

- `rogue_6_relic_cargo_2`（悲伤的红）9 层、零件数 9 时，
  `rogue_6_relic_inventory[real]` 显示 `MAX_HP 72.00%, ATK 72.00%`。
- 模板 `rogue_6_relic_inventory` 在 `ON_BUFF_START` 读取
  `BattleRoguelikeMeta.scrapInventoryCount`，再乘 `0.08`。
- `rogue_6_relic_legacy_136`（佣兵的饰物）4 层实测：
  托卡 `ATK 841 -> 1009`、`HP 1687 -> 2024`、`DEF 225 -> 270`。
- `rogue_6_relic_cargo_3`（空床）已增加
  `scrapInventoryLimit=10` 的代码处理，但本轮没有重新构建并实测，不能写成已通过。

### 界园兵锋（按衡钱叠层）

**已捕获真实战斗数据（界园 DLC2，暂停于第 73 帧，4 层）**

- 藏品：`rogue_5_relic_legacy_167`「兵锋」。
- `BattleRoguelikeRelicBuff` 中存在两条 buff：
  - `layer_copper_owned`，blackboard：`src=rogue_5_relic_legacy_152`、`max=15`、
    `lucky_level=MID`。
  - `layer_char_attribute_mul`，blackboard：`atk=0.1`、
    `selector.profession=warrior|sniper|tank|medic|support|caster|special|pioneer`、
    `divine_type=DIVINE`。
- 实例化后的 `CAttributeMulByLayer` 为 `stackLayer=4`、`data.key=layer_char_attribute_mul`，
  即 4 层时全体我方攻击力 +40%。
- 该数据与 `roguelike_topic_table.json` 中
  `details.rogue_5.relics.rogue_5_relic_legacy_167` 的静态定义一致。

注入路径沿用通用叠层藏品：UI 选中「兵锋」后把层数设为 4，生成
`--relics=rogue_5_relic_legacy_167@4`；插件会把 `layer` 写到
`BattleRoguelikeRelicBuff.layer`，由 `CAttributeMulByLayer` 读取。
同族的 `rogue_5_relic_legacy_152`「惊堂木」是生命值版本，走同一路径。

**本地注入实测（2026-11-06，`ro5_n_4_2`）**

- `legacy_167` 兵锋 `@4` → `CAttributeMulByLayer.stackLayer=4`。
- `legacy_152` 惊堂木 `@4` → `stackLayer=4`。
- `speg_4` 酿山河 `@2` → `stackLayer=2`。
- `speg_6` 铿金征鼓 `@3` → `stackLayer=3`。
- `legacy_162` 得胜琴 `@5` → `stackLayer=5`（同时有静态 `char_attribute_mul`）。
- `explore_3` 家常小炒 `@6` → `stackLayer=6`（同时有静态 `char_attribute_mul`）。

对照实验：把兵锋/惊堂木/酿山河都写成 `@1`，同时注入 4 枚衡钱 + 2 枚花钱的
钱盒条目，三者仍是 1 层。因此 `layer_copper_owned` / `layer_copper_draw` 不会
在本地从钱盒重新计算层数，**层数完全以 `--relics=id@N` 为准**；这一类藏品
只需要在 UI 里填一个层数，和佣兵的饰物/圆石祭坛相同。
试验性的 `--boxcoin` 注入已撤回，不保留。

同一轮还验证了其余 `layer_*` 类型：

- `return_17` 圆石祭坛 `@4` → `layer_char_attribute_mul` `stackLayer=4`。
- `return_7` 生还者合约 `@3` → `layer_char_random_target_attribute` `stackLayer=3`。
- `farmer_3` 母鼎 `@4` → `global_buff_layer` `stackLayer=4`。
- `speg_1` 契心聆铃 `@3` → 三组 `global_buff_layer` 均为 `stackLayer=3`。
- `explore_7` 子武剑 `@3` → `layer_char_attribute_final_scaler` `stackLayer=3`。
- `legacy_157` 登天斧 `@2` → `layer_char_attribute_mul` 与静态 `char_attribute_mul` 均为 `stackLayer=2`。

结论：界园这些 `layer_*` 藏品都走通用的 `--relics=id@N` 显式层数路径；
不需要按钱盒/战斗事件在本地反推层数，也不需要额外的专用 Buff 配方。

### 特勤干员战斗精通（静态证实，待实机验证）

“战斗精通”不是藏品，也不在肉鸽藏品表里，而是角色 master 系统：

- 静态表：`char_meta_table.json` 的 `charMasterDataMap` 中 `masterType=2`；
  战斗数值在 `char_master_table.json` 的 `candidates[].talentData`。
- 运行时：`AdvancedCharacterInst.masterInfos` 是
  `Torappu.CharacterData.MasterInfo[]`；账号数据里的
  `chars[].master` 是 `masterId -> level` 字典。
- 游戏自带转换：
  `Torappu.CharacterUtil.CreateMasterInfosFromMasterDict(Dictionary<string,int>)`
  把字典转成 `MasterInfo[]`。
- 目前已确认的 combat mastery：
  - `master_radian_6`（电弧，界园）：1 级生命 +20%；2 级生命/防御 +25%；
    3 级生命/防御 +30%、法抗 +15。
  - `master_mcnist_2`（机械师，黑流树海）：生命 +20% / +25%+防御 / +30%+防御+法抗。
  - `master_mcnist_3`（机械师，黑流树海）：每秒对阻挡敌人造成攻击力
    25% / 35% / 50% 的法术伤害。

本地注入已加入 `--master=charId:masterId@level`：
构造 `Dictionary<string,int>` 后调用上述转换，写入
`AdvancedCharacterInst.masterInfos`。代码路径尚未实机验证。

**实测（2026-11-06）**

- 电弧 `master_radian_6@1`：基础 HP 1369 → 1642.8（+20%）。
- 电弧 `master_radian_6@3`：HP 1779.7、DEF 211.9、MR 35
  （基础 1369 / 163 / 20，对应 +30% / +30% / +15）。
- 机械师 `master_mcnist_2@1`：基础 HP 3631 → 4357.2（+20%）。

### 电弧的机制类“战斗精通 I”（master_radian_5）

这条不是 `masterInfos` 战斗天赋，而是局内成长链：

- `master_radian_5` 是 `masterType=1` 的机制精通。
- 界园表里对应 `rogue_5_somaster_7/8/9`（等级 1/2/3），
  通过 `zone_into_reward` 在进入新层时依次发放：
  - zone_1 → `rogue_5_somaster_19`：召唤物部署费用 -9
  - zone_2 → `rogue_5_somaster_17`：召唤物同时部署数量 +2
  - zone_3 → `rogue_5_somaster_18`：召唤物攻击速度 +35
  - zone_4 → `rogue_5_somaster_13`：召唤物不消耗部署位
- 四个奖励条目本身是 `RoguelikeBuff`：
  - `deck_card_buff value=-9` / `dont_occupy_deploy_cnt=1`
  - `char_attribute_add max_deploy_count=2`
  - `char_attribute_add attack_speed=35`
  - `char_ability_new_at_root rogue_2_occupy_zero[token]`
  都通过 `selector.char` 限定 `token_10051/10052/10053_radian_tower*`。

因此本地 TAS 若要复现，不能只填 `--master=...`；需要直接注入这四条效果，
或者做一个等价的 zone 奖励模拟。

现在已接入：

- Web 配置的“特勤干员精通”里，`master_radian_5` 可选 1-3 级和到达层数；
  按 1..N 层累积生成 `--outerbuff`。
- 扩展了 `--outerbuff` 解析，支持 `key||field=...` 的空 keyField 写法，
  因此 `deck_card_buff` / `char_attribute_add` 可直接注入。
- 实测（`ro5_n_4_2`）：注入四层效果后，三号召唤物 `token_10053_radian_tower3`
  的费用从 16 降到 7（部署费用 -9），
  且 `char_attribute_add attack_speed=35`、`dont_occupy_deploy_cnt=1`、
  `char_ability_new_at_root` 均已被解析并注入。
  攻速实测 100 → 135（+35）；卡牌 `dont_occupy_deploy_cnt` 实测
  false → true；费用实测 16 → 7。`max_deploy_count +2` 还没有用
  “同时部署 4/5 个召唤物”的方式单独验证。

WebView 的 `units` 之前只收类名严格等于 `Torappu.Battle.Character` 的对象，
召唤物是子类所以一直不显示。现已改为按基类可赋值判断，并在卡牌快照增加
`dont_occupy_deploy_cnt`；现在 `token_*` 召唤物会出现在战斗页的 units 列表。

## 黑流树海概念体

**已实测（白模鸟）**

- 观测环境：官方黑流树海战斗，游戏 PID 27900，暂停于战斗内。
- 概念体是局外被动道具，进入满足条件的节点后转换为
  `RoguelikeInput.outerBuffs`，不是战斗内实体。
- 白模鸟 `rogue_6_scrap_P_01` 的实测 `outerBuffs` 条目：

```text
[6] key = char_attribute_add
    blackboard:
      attack_speed = 50

[7] key = battle_extra_drop
    blackboard:
      id = pool_treasure
      count = 1
```

- `char_attribute_add attack_speed=50` 是本次战斗的全体攻速 +50。
- `battle_extra_drop pool_treasure=1` 是战后随机收藏品，不是战斗内效果。
- `m_relics` 中出现
  `Torappu.Battle.Roguelike.Internal.CAttributeAdd`，确认走
  `outerBuffs -> CAttributeAdd`；战斗内没有白模鸟敌人、token 或地块。
- `PreprocessGlobalBuff` 只生成 6 个敌方全局 Buff，没有生成白模鸟。

原始证据：

```text
_analysis/rogue6_white_bird_rogue_dump.txt
_analysis/rogue6_white_bird_probe.jsonl
_analysis/rogue6_concepts.md
```

**静态证实 / 已本地复核**

- 黑流树海共有 9 个概念体，其中只有两个有战斗内效果：
  - `P_01` 白模鸟：我方全体攻速 +50（已实测）。
  - `P_04` 涂装黎博利：我方全体攻速 +35（本轮已本地实测）。
- `P_04` 与白模鸟走同一条 `char_attribute_add` 链路。
- 其余 7 个概念体是局外奖励、护盾、揭示地图或商店/加工品效果，不属于战斗内 Buff。

### 本轮本地复核（概念体 + 微型胶囊）

测试关卡：`ro6_n_1_1`，`level_rogue6_1-1`。

注入：

```text
--outer=char_attribute_add:attack_speed:35
--outerbuff=global_buff_normal|damage_scale[in][phy]|damage_scale=1.25
```

实测结果：

- 棘刺基础 `ASPD=100`，战斗内 `ASPD=135`，涂装黎博利 +35 生效。
- 棘刺 `DEF=402`，敌人 `enemy_1034_laxe` 的 `ATK=850`。
- 无微型胶囊时理论物理伤害为 `850 - 402 = 448`。
- 微型胶囊 1.25 倍后理论伤害为 `448 × 1.25 = 560`。
- 实测棘刺 `HP 2612 → 2052`，正好损失 `560`，确认微型胶囊实际生效。
- `m_globalBuffs` 中对应条目为 `damage_scale[in][phy]`，
  blackboard 为 `damage_scale = 1.25`。

证据：

```text
_captures/rogue_microcapsule_local_20261106/status_after_hit.txt
_captures/rogue_microcapsule_local_20261106/battle_rogue_dump.txt
_captures/rogue_microcapsule_local_20261106/global_buffs.json
_captures/rogue_microcapsule_local_20261106/battle_buffs.txt
```

### 注入命令速查

```text
# 概念体 P_01 白模鸟：全体攻速 +50
--outer=char_attribute_add:attack_speed:50

# 概念体 P_04 涂装黎博利：全体攻速 +35
--outer=char_attribute_add:attack_speed:35

# 微型胶囊 a / b / c
--outerbuff=global_buff_normal|damage_scale[in][phy]|damage_scale=1.25
--outerbuff=global_buff_normal|damage_scale[in][phy]|damage_scale=1.4
--outerbuff=global_buff_normal|damage_scale[in][phy]|damage_scale=1.7

# 孤立石林
--outerbuff=char_ability_new|rogue_6_wooden_weather_4|rogue_6_wooden_weather_4[real].attack_speed=30;rogue_6_wooden_weather_4[down].attack_speed=-50

# 源石之城（gold=100 示例）
--gold=100
--outerbuff=global_buff_normal|rogue_6_wooden_weather_7|value=1;atk=0.004;max_hp=0.01;atk_max=0.4;max_hp_max=1.0
```

注意：当前 `--outerbuff` 解析器会丢弃空的中间段，
`--outerbuff=char_attribute_add||attack_speed=35` 会被错误解释成
`keyField=attack_speed=35`。带空 keyField 的直挂属性请使用
`--outer=char_attribute_add:attack_speed:35`；这是已经踩过的坑。

### 天气 / 年代 / 岁时的等级阈值

静态证实（`roguelike_topic_table.json`，各主题 `difficulties` 的 `ruleDesc`）：

- 萨卡兹（rogue_4）年代：难度 6 起进入扩张期，难度 12 起进入鼎盛期。
- 界园（rogue_5）岁时：难度 6 起由朦胧提升至真切，难度 12 起提升至入髓。
- 黑流树海（rogue_6）实托邦：难度 6 起提升至中期并追加方针，难度 12 起提升至晚期。

因此 Web 配置按 `grade < 6` / `6..11` / `>= 12` 自动选择 a / b / c 三档。
岁时的 `_d` 变体是“醒觉”（`isPacified=true`），不属于难度档次，当前未纳入自动选择。

戌绘（`rogue_5_wrath_1`）共有 15 个条目：a/b/c 三档各带若干职业组合子变体，
另有 `_d` 醒觉。子变体只改变受影响的职业，且便符是敌方单位、客户端无独立
Buff 注入路径，当前全部标记为“仅展示”。UI 按难度只显示当前档位的一张卡，
卡下用职业 chip 直接选择近卫/狙击/辅助等具体组合（不再提供“随机 N 个职业”条目）；
选择会保存，但因条目未通过注入校验，不会写入战斗参数。

醒觉（`_d`，`isPacified=true`）不是难度档位，每张岁时卡带独立的“醒觉”开关；
开启后按 `_d` 条目展示和保存，关闭后回到当前难度档的具体职业组合。
岁时允许多选：不同岁时可同时勾选，同一岁时只能处于普通或醒觉其中一种状态；
其他特殊机制（年代/实托邦）仍保持单选。

### 界园钱币的重复来源（静态证实）

`roguelike_topic_table.json` 的 `modules.rogue_5.copper.copperData` 共 2334 条：

- 1166 条是 `changeCopperMap` 的 `rogue_5_change_copper_*` 镜像条目，
  与对应的 `rogue_5_copper_*` 名称、描述、Buff 完全相同，是“同名同描述”的主要来源。
- 每个基础钱币还有约 10 个 `gildTypeId` 变体（锈色/存护/入幻/引光/巡游/相合/
  易变/易花/易厉/受引），只影响源石锭、护盾、希望、烛火、票券或投币规则，
  不参与战斗。

Web 配置生成时同时排除镜像 ID 与 `gildTypeId` 变体，并按 `name|usage` 去重，
剩余 105 条。`花-鸭爵金币`、`花-圣诏封神` 的“必定投出”变体属于局外投币规则，
已合并回基础条目；效果文本里的“（下次投钱前变化为-大炎通宝）”局外提示也已去掉。
剩余同名条目只有 `衡-捕风` 朝左/右/上/下 4 条，是实际不同的方向效果。

### 岁时“巳农”实测

真实界园战斗（`level_rogue5_2-6`、`modeGrade=2`）中，玩家状态的岁时为
`rogue_5_wrath_2_a`（巳农·朦胧）。战斗输入与运行时实例对应如下：

```text
outerBuffs[23] key=char_cost_add
  cost=2
  selector.profession=warrior|sniper|tank|medic|support|caster|special|pioneer

m_relics[3] Torappu.Battle.Roguelike.Internal.CCostAdd stackLayer=1
```

因此 `rogue_5_wrath_2_a/_b/_c` 分别按同一 `CCostAdd` 链路注入
`cost=2/3/4`，UI 已通过 `VERIFIED_SPECIALS` 启用这三档。

醒觉 `_d` 另有一次真实样本，效果是“首位干员部署费用 -6”。它不是全局
`CCostAdd`，而是两条 Buff 配合：

```text
deck_card_buff
  value=-6, cost_scale=1, selector.profession=<all>

global_buff_normal
  key=rogue_5_first_character_create_card_buff_to_deck
  value=6, cost_scale=1, selector.profession=<all>
```

运行时分别实例化为 `DCardBuff` 和 `GBuffNormal`。Web 配置已按这两条原样生成，
不能简化成全局 `char_cost_add cost=-6`。

原始样本见 `_captures/rogue_wrath_sinong_20261106/` 和
`_captures/rogue_wrath_sinong_awakened_20261106/`。

申铸醒觉 `rogue_5_wrath_8_d` 可从普通档实测链路推导：普通档已确认
`enemy_attribute_mul` 使用 `selector.tag=animated`，醒觉只把同一化物目标改为
`atk=-0.05`，因此 Web 配置已启用。

寅诗普通档 `rogue_5_wrath_7_a` 的真实输入不是敌人攻击力倍率，而是：

```text
global_buff_normal
  key=rogue_5_enemy_speg_weather
  damage_scale=1.1
  prob=1
  selector.profession=<all>
```

运行时实例为 `GBuffNormal`，`stackLayer=1`。Web 配置按同一链路生成
`damage_scale=1.1/1.15/1.25` 三档并已启用。醒觉 `_d` 的“伺烛客”目标 selector
尚未确认，继续保持禁用。原始样本见 `_captures/rogue_wrath_yinshi_20261106/`。

## 待办标记

- [ ] 若要让源石之城在 Web 配置中可选，需要为 `variation` 增加 UI 分组，
  并允许输入/保存源石锭数值，生成 `--gold=<N>`。可直接使用已验证的
  `atk_max=0.4;max_hp_max=1.0`，但需在 UI 中标注“100 层上限来自外部资料推导”。
- [ ] 如需精确复现真实服务器上限，必须捕获真实 `unKeepBuff.blackboard`，
  不能从客户端 prefab 猜 `atk_max/max_hp_max`。
- [ ] 若要确认派生 Buff 的层数，应扩展敌方单位 Buff 容器读取，而不是只看属性结果。
- [ ] 概念体 UI 尚未生成 `concepts` 数据；白模鸟/涂装黎博利可以在 UI 中生成
  `--outer=char_attribute_add:attack_speed:50` 或
  `--outer=char_attribute_add:attack_speed:35`（当前解析器不要用空 keyField 的
  `--outerbuff` 写法），其余概念体只能展示局外效果。
