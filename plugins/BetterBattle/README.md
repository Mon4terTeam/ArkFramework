# BetterBattle

ArkFramework 的本地战斗插件。直接构造并进入任意已加载关卡，内置逐事件伤害/承伤/治疗统计、录像回放和时间轴；控制界面由独立的 `BetterBattleWeb` WebView2 宿主提供。

## 功能

- 输入任意已加载关卡 `stageId`，从干员目录选择任意干员并配置等级、精英化、潜能、技能和技能等级。
- 直接构造游戏自己的战斗数据并进入关卡，不需要账号持有该干员，也不经过关卡选择 UI。
- 局内支持部署、手动开技能、撤退、暂停/继续和返回主界面。
- 技能开启前检查 `BasicSkill.get_spEnough` 与 `IsOpTriggerable(side)`；自动触发的技能不会误判为可手动开启。
- 直接 Hook `BattleLogger.LogModifier` / `LogEpModifier`，按事件序列累计伤害、承伤和治疗；同一逻辑帧内的多次事件不会合并成 HP 差值。
- 通过 `web_bridge.dll` 发布 Web 快照和干员目录，命令写入加载器目录下的 `BetterBattle.request`。
- 支持普通关卡和 `GameModeMeta.ROGUELIKE` 肉鸽入口，可注入藏品、Buff 物品和局外成长条目。

## 构建

```powershell
cmake --build build-cmake --target BetterBattle web_bridge
cmake --build build-cmake --target BetterBattleWeb
```

`BetterBattle` 依赖 `web_bridge.dll`；`BetterBattleWeb` 由 `webview/BetterBattleWeb.csproj` 发布。

## 运行文件

- `BetterBattle.request`：命令文件，插件每 250 ms 轮询一次。
- `BetterBattle.status`：状态快照，文本按 1 秒节流写入。
- `BetterBattleWeb\BetterBattleWeb.exe`：外部控制台，插件启动时自动拉起并保证单实例。
- `\\.\pipe\ArkFramework.WebView`：快照管道，topic 为 `betterbattle.snapshot` 和 `betterbattle.catalog`。

## 命令

```text
start <stageId> <id:level:evolve:potential:skill:skillLevel,...>
rogue-start <topicId> <stageId> <roster> [mode] [grade] [growth] [--zone=<1-99>]
rogue-catalog [outputFile]
rogue-net-capture start|stop
rogue-capture
rogue-dump-battle
rogue-global-buffs
game-mode
deploy <cardUid> <row> <col> [direction]
skill <cardUid>
skill-state <cardUid>
skill-methods <cardUid>
buff-fields <cardUid>
buffs <cardUid>
sp <cardUid> [value]
withdraw <cardUid>
record start
record stop
record write
timeline jump <frame> [nodeId]
timeline advance <frames>
timeline status
timeline rename <nodeId> <name>
timeline delete <nodeId>
replay start [jsonFile]
replay plan [@nodeId] <frame:ops;...> [stop=<frame>] [fast]
replay seek <frame> [nodeId] [fast]
replay simulate <frame> deploy|skill|withdraw <unit> [dir row col]
replay stop
trace start [fields]
trace stop
pause
resume
cost <value>
leave
tiles <cardUid>
refresh
catalog
withdraw-signature
export-web [prefix]
```

部署、技能和撤退仍走 `BattleController.PlayerOp_Spawn` / `PlayerOp_TrigSkill` / `PlayerOp_Withdraw` 玩家入口。

## 录制与回放

录像使用 schema 1，键为 30 FPS 逻辑帧：

```json
{
  "schema": 1,
  "meta": {
    "stage_id": "wk_armor_5",
    "squad": [{"id": "char_1045_svash2", "level": 90, "evolve": 2,
               "potential": 6, "skill_index": 2, "skill_level": 10}]
  },
  "operations": {
    "487": [["deploy", "char_1045_svash2", 0, [2, 5]]],
    "758": [["skill", "char_1045_svash2"]],
    "2243": [["withdraw", "char_1045_svash2"]]
  }
}
```

部署参数为干员 ID、朝向、`[row, col]`；技能和撤退参数均为干员 ID。回放按 `BattleController.FixedUpdate` 和 `fixedFrameCnt` 推进，部署解析到卡牌 UID 后调用玩家入口。

时间轴分叉会使用根节点保存的启动参数和随机种子从头重建战斗，重放到目标帧后暂停并创建新分支；`timeline advance` 只推进逻辑帧，不注入玩家操作。

## 机制资料

- Buff 字段、采集预算和快照边界：[Buff 系统运行时说明](../../docs/BuffSystem.md)
- 肉鸽 Buff 下发链路：[RogueBattleBuffPipeline](../../docs/RogueBattleBuffPipeline.md)
- 肉鸽机制证据台账：[RogueMechanicsEvidence](../../docs/RogueMechanicsEvidence.md)

## 兼容性

- 不依赖 `damage_meter.dll`；后者继续作为 `ark.battle.events`（ABI 1）和 `ark.battle.events.v2`（ABI 2）的兼容 provider 发布。
- 两个插件同时加载时都会 Hook `BattleLogger`，核心的原生钩子按链执行，不会互相覆盖，但会产生两份采集开销。
- 逻辑帧固定为 30 FPS；`Character.isInSkillState` 只表示状态机进入技能子状态，完整技能期需结合 `isAffecting`、`remainingTime`、`triggerCnt` 和 `hasPlayedBeginAnim`。

## 测试

本地完整测试脚本 `run-test.ps1` 和 `recordings/` 回放数据不纳入提交；默认回放文件需要本地准备。
