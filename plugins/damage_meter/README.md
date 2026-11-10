# damage_meter

ArkFramework 的伤害统计插件，同时作为 `ark.battle.events` 的兼容 provider 随包发布。

## 功能

- 观察原生 `BattleLogger.LogModifier` / `LogEpModifier`，记录计算伤害和实际扣血。
- 按干员分组显示物理、法术、真实、元素四类 HP 伤害；元素损伤累积单独记录。
- 显示关卡名、游戏时间与原生逻辑帧；战斗结束自动导出 CSV，也可手动导出。
- 提供 ABI 1 的 `ark.battle.events` 和 ABI 2 的 `ark.battle.events.v2` 两套服务。
- 在测试宿主中提供 ABI 1 的 `ark.battle.test` 和 ABI 2 的 `ark.battle.test.v2` 两套测试服务。
- 保留旧版 ImGui 面板；关闭窗口不停止采集。

## 构建

```powershell
cmake --build build-cmake --target damage_meter
```

## 兼容性

- `ArkEventV1`、`ArkBattleEventsApiV1`、`ark.battle.events` 的字段、布局和服务名保持不变，旧消费者不需要修改。
- `ArkEventV2` 通过新服务名 `ark.battle.events.v2` 提供，增加 `frame`、`frame_event_index`、`owner_id`、`target_id` 和 `original_source_id`。
- `EventHistory` 内部存储 V2 事件；ABI 1 的 `read` 做 V1 投影，ABI 2 的 `read` 返回完整事件。
- 与 BetterBattle 同时加载时会各自 Hook `BattleLogger`，核心原生钩子按链执行。

## 测试

```powershell
cmake --build build-cmake --target damage_meter_tests
```

测试覆盖事件历史、ABI 1/2 读取、统计模型和关卡名索引。
