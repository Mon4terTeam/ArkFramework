# Buff 静态目录

`buff-catalog.tsv` 是 Web 控制台使用的可编辑名称表，UTF-8 编码、Tab 分隔。
修改后重新打开 Web 控制台或按 `F5` 即可生效；修改发布目录
`build-cmake/dist/BetterBattleWeb/ui/data/buff-catalog.tsv` 不会影响源文件。

## 列

| 列 | 说明 |
|---|---|
| `kind` | `buff` / `skill` / `relic` |
| `key` | 匹配用的键；`buff` 行已归一化为 `xxx_s_1` / `xxx_t_1` |
| `name` | 显示名称，可留空 |
| `description` | 游戏原文，换行写成 `\n`，Tab 写成 `\t` |
| `subtype` | `skill` / `talent` / `character_skill` 等 |
| `source` | 原始技能或天赋 ID，如 `absin_s1` |
| `owner` | 干员 ID，如 `char_405_absin` |
| `topic` | 藏品所属肉鸽主题，如 `rogue_6` |

`relic-effects.tsv` 保存 `anchor -> relic_id`，`anchor` 是藏品动作名或动作参数
里的字符串（例如 `modify_sp_recover[normal]`）。Web 端先按完整键、再按去掉
`[...]` 的基础键查找候选藏品。

## 键归一化

```text
skchr_absin_1   -> absin_s_1
absin_s1        -> absin_s_1
absin_t1        -> absin_t_1
svash2_s_1[deck] 先查完整键，再查 svash2_s_1
以 [g] 开头的键先去掉 [g]
```

目录未收录的 Buff 仍会显示原始 `key`、属性修饰、异常掩码和 blackboard，
不会伪造名称。

## 重新生成

原始导出脚本：`E:\Games\ArkTest\_analysis\buff_viewer_re\export_web_catalog.py`。
它从 `buff_viewer.dll` 和本地 `gamedata/excel` 读取数据并重写本目录的两张表。
