# BetterBattleWeb

BetterBattle 的外部控制台宿主：.NET 8 WinForms + WebView2。它连接 ArkFramework 的命名管道，把插件快照转发给 Web 页面，并把页面命令串行写入 `BetterBattle.request`。

## 功能

- 页面：战斗、游戏视图、敌方、编队、战斗配置、时间轴、数据统计、诊断。
- `游戏视图` 会把游戏窗口设为宿主 owner 并吸附到预留区域；切出时暂停并恢复原窗口。
- 单实例运行；由 BetterBattle 插件启动，也可手动启动用于 fixture 调试。
- 命令队列一次只写一条请求，等待插件消费后再写下一行，避免快速点击覆盖请求。
- 支持 `--fixture <dir>`、`--fixture-index <n>`、`--fixture-frame <n>` 离线回放测试。

## 构建

```powershell
cmake --build build-cmake --target BetterBattleWeb
```

或直接发布：

```powershell
dotnet publish webview/BetterBattleWeb.csproj -c Release -r win-x64 --self-contained false `
  -o build-cmake/dist/BetterBattleWeb
```

## 目录

```text
Program.cs              入口、单实例、fixture 参数
MainForm.cs             管道客户端、命令队列、资源请求、fixture 播放
ui/                      Web 页面、样式、数据、fixture 和美术资源
```

协议和命令格式见 [BetterBattle README](../plugins/BetterBattle/README.md) 与 [ExternalBridge](../docs/ExternalBridge.md)。
