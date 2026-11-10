# BetterBattle 外部 Web 控制台

BetterBattle 不再注册游戏内 ImGui 页面。战斗控制、编队、干员目录、录制/回放和
Buff 查看统一由独立进程 `BetterBattleWeb.exe` 提供。游戏内插件只负责运行时操作和
数据投影。

## 1. 组件关系

```text
游戏进程
  BetterBattle.dll
    └─ ark.external.bridge（通用服务，由 web_bridge.dll 提供）
         └─ \\.\pipe\ArkFramework.WebView

外部进程
  BetterBattleWeb.exe（.NET 8 WinForms + WebView2）
    ├─ 读取命名管道，把 topic/payload 转发给 Web 页面
    └─ 接收 Web 页面命令，串行写入 BetterBattle.request
```

`web_bridge.dll` 只实现 `topic + JSON` 的单向发布，不包含 BetterBattle 业务字段。
Framework 核心和 `ArkPluginApiV1` 不参与这套 UI 协议。

## 2. 管道协议

- 管道名：`\\.\pipe\ArkFramework.WebView`
- 仅本机连接：服务器使用 `PIPE_REJECT_REMOTE_CLIENTS`
- 每帧：4 字节小端长度 + UTF-8 JSON
- 外层 JSON：`{"topic":"...","seq":123,"payload":{...}}`
- 每个 topic 只保留最新负载；客户端连接后会先收到所有已有 topic 的最新值
- 当前只允许一个 WebView 客户端

当前 topic：

- `betterbattle.snapshot`：战斗状态、卡牌、我方单位、Buff、当前编队、录制/回放/采集状态
- `betterbattle.catalog`：干员目录，仅在目录加载成功或失败后发布

干员显示名不使用 `CharacterData.name`（该字段保存内部键）。目录加载时按伤害统计插件
相同的顺序解析：

1. `Torappu.CharacterUtil.TryGetCharName(string, out string)`
2. `Torappu.TokenDB.TryGetName(string, out string)`（召唤物/token）
3. `Torappu.CharPatchDB.get_data().patchChars[id].name`（补丁/异格数据）

解析失败才回退到内部 ID。

## 3. 刷新频率

`betterbattle.snapshot` 每 500 ms 生成一次。它表示“当前时刻的一份快照”，不是
500 ms 内的 15 个逻辑帧历史。逐逻辑帧数据仍使用：

```text
trace start [fields]
trace stop
```

结果写入加载器目录下的 `battle_trace_frames.log`，帧号来自 `fixedFrameCnt`，
逻辑帧率固定为 30 FPS。

## 4. 命令通道

Web 页面通过 `chrome.webview.postMessage` 把命令交给宿主。宿主维护单消费者队列，
一次只向加载器目录下的 `BetterBattle.request` 写入一条命令，并等待插件消费完文件后
再写下一行，避免快速点击时覆盖请求。

命令文本与 `BetterBattle.request` 完全一致，例如：

```text
start wk_armor_5 char_1050_chen3:90:2:6:1:10,char_2023_ling:90:2:6:3:10
deploy 3 1 7 1
skill 56
withdraw 56
pause
resume
record start
replay start wk_armor_5 <roster> battle_operations.json
trace start state,cost,hp,sp,skill,affect,remain
```

插件每 250 ms 轮询一次请求文件。部署、技能和撤退仍走
`BattleController.PlayerOp_Spawn` / `PlayerOp_TrigSkill` / `PlayerOp_Withdraw`
玩家入口。

## 5. 启动与构建

```powershell
cmake --build build-cmake --target BetterBattle web_bridge -j 8
cmake --build build-cmake --target BetterBattleWeb
.\build-cmake\dist\BetterBattleWeb\BetterBattleWeb.exe
```

`BetterBattleWeb.exe` 假定自己在 `dist\BetterBattleWeb\` 下运行，请求文件位于其父目录
`dist\BetterBattle.request`。

## 6. 边界

- 当前命令回传复用 BetterBattle 请求文件，不是第二条管道；后续可在
  `ArkExternalBridgeApiV1` 增加通用双向命令能力。
- Web 端禁止直接按渲染帧轮询游戏对象；所有游戏内操作仍由 BetterBattle 的游戏线程任务泵执行。
- Buff 面板按 500 ms 快照刷新，不应被当作逐帧机制结论。需要精确技能阶段时使用
  `affect`、`remain`、`trigger`、`anim`、`stateid` 的逐帧 trace。
