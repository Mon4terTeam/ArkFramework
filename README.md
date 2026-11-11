# ArkFramework

面向 Windows x64 / Unity IL2CPP 游戏的原生插件框架。加载器与核心当前适配《明日方舟》PC 客户端（`Arknights.exe`）。本仓库是 [`wuyu8512/ArkFramework`](https://github.com/wuyu8512/ArkFramework) 的 fork。

- **通用核心** `ArkFramework.dll`：插件动态加载、卸载与热重载，依赖与命名服务，游戏线程任务队列，共享 Dear ImGui 浮层，原生函数钩子及其安全排空。
- **加载器** `ArkFrameworkLoader.exe`：等待游戏启动后用标准 `LoadLibraryExW` 载入核心，不修改游戏目录。
- **插件 SDK**：`include/ark/plugin.h`、`battle.h`、`external_bridge.h`。具体插件位于 `plugins/<name>/`，每个插件目录维护自己的 README。

> 本项目仅供学习与研究。使用第三方工具可能违反游戏的用户协议，由此产生的一切后果由使用者自行承担。

## 核心功能

- `Insert` 统一显示/隐藏插件管理器及全部插件页面；隐藏时保留页面状态，后台采集与任务继续运行。管理器可查看状态，加载、卸载、重新加载插件，扫描插件目录。
- 可开启“文件变化时热重载”；插件以影子副本加载，可直接覆盖重编的 DLL。
- 依赖链整体预检、按依赖顺序停止与恢复；提供方仍有消费者时拒绝单独卸载。
- 卸载前排空回调、任务、当前 ImGui 帧与原生钩子调用；超时或清理失败时保留 DLL，不强制释放仍在执行的代码。
- 统一 UI 缩放（50%–200%），只影响插件界面。
- 日志、缓存与导出均位于核心所在目录下的 `logs`、`cache`、`exports`。

## 兼容性

- 宿主 ABI 不变：`include/ark/plugin.h` 的 `ARK_ABI_VERSION` 仍为 1，`ArkPluginApiV1`、依赖声明和 `provide_service` / `require_service` 规则没有改动。
- 战斗事件服务保留 ABI 1：`ArkBattleEventsApiV1` 和 `ArkEventV1` 的字段、布局、服务名都不变，旧消费者不需要重新编译或修改代码。
- 新增 `ark.battle.events.v2`（ABI 2）：`ArkEventV2` 增加逻辑帧、帧内序号、目标、原始来源和召唤物宿主。新消费者必须显式请求 `ARK_BATTLE_SERVICE_NAME_V2`，不要把 ABI 2 的事件写进 ABI 1 的缓冲区。
- 旧的事件 provider 继续随包构建和发布，同时注册 ABI 1 与 ABI 2 两套服务和测试服务；旧的 `ark.battle.test` 仍可用于旧测试宿主。
- 框架不修改上游插件的入口约定；新增能力通过独立插件目录和新增服务名提供。
- 不再兼容的部分只有新增能力本身：旧消费者拿不到 ABI 2 的帧内事件，需要改用 V2 服务名。其他旧接口保持原样。
- 本地测试数据（`recordings/`、临时脚本、构建输出）不纳入提交。

## 与上游的关系

本仓库是 [`wuyu8512/ArkFramework`](https://github.com/wuyu8512/ArkFramework) 的 fork，分叉基点为 `26c5bc7`；上游当前 HEAD `94c616f` 只修改了 README 的许可证说明。

- **ABI 兼容**：宿主 `ARK_ABI_VERSION` 保持 1，`plugin.h`、插件清单、依赖声明和 `provide_service` / `require_service` 规则与上游一致；上游的 ABI 1 插件可以直接加载。
- **事件服务兼容**：ABI 1 的表、事件布局和服务名不变；V2 只新增 `ark.battle.events.v2`，不改旧服务。
- **新增能力隔离**：本 fork 的功能插件位于独立目录，不替换上游组件的入口约定，也不要求上游插件改代码。
- **构建差异**：以 CMake 为准；`build.ps1` / `package.ps1` 已扩展为包含本 fork 的组件。`run-test.ps1` 和 `webview/tools` 下的脚本属于本机测试/开发工具，不纳入提交。
- **未验证项**：ABI 1 兼容是代码层面的保证；本 fork 的新插件在上游 core 上的实机行为尚未逐项验证，上游 core 也不会主动消费 V2 服务。同步上游后应重新跑事件模型测试和 `ArkFrameworkHost --self-test`。

## 目录结构

```text
include/ark/            插件 SDK 头文件（plugin.h、battle.h、external_bridge.h、imgui_config.h）
src/core/               核心：插件管理、原生钩子、D3D11 浮层与入口
src/loader/             外部加载器 ArkFrameworkLoader.exe
src/bootstrap/          窗口消息钩子引导 ArkBootstrap.dll（--method window-hook）
plugins/common/         插件共用的 IL2CPP 运行时、Unity 输入适配与窗口布局
plugins/<name>/         具体插件目录；每个目录自带 README
tests/                  单元测试、生命周期与原生钩子测试、测试夹具、D3D11 测试宿主
third_party/            Dear ImGui 1.91.9b、MinHook 1.3.4、nlohmann/json
cmake/                  CMake 的 Zig 工具链文件
docs/plugin-sdk.md      插件开发文档
```

## 构建

当前发布构建使用 CMake + MinGW Makefiles，不依赖仓库内的 Zig 工具链。

`build.ps1` 仍保留给 Zig 构建流程；`package.ps1` 现在独立使用 `build-package`，
不会覆盖正在运行的游戏已映射的 DLL。`run-test.ps1` 是本机测试脚本，不纳入提交。

```powershell
.\package.ps1               # 构建并生成 package\ArkFramework
.\package.ps1 -SkipBuild    # 复用 build-package\dist，仅重新组装发布目录
```

发布包只包含 `damage_meter`、`BetterBattle`、`web_bridge` 三个插件；
`auto_login` 与 `BattleCheat` 明确排除。核心运行中时不要直接用
`build-cmake` 重新链接核心，`package.ps1` 默认使用独立的 `build-package`。

也可直接使用 CMake 3.22+ 与 Ninja：

```powershell
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

具体插件的构建目标见对应插件目录的 README。

## 使用

发布目录结构如下，保持文件夹完整即可放在任意位置：

```text
ArkFrameworkLoader.exe
ArkFramework.dll
ArkImGui.dll
ArkBootstrap.dll
plugins\
BetterBattleWeb\
LICENSE
licenses\
```

插件可能在 `plugins\` 下附带自己的运行时目录；以各插件 README 为准。

双击 `ArkFrameworkLoader.exe`：

1. 游戏以管理员权限运行时，加载器会请求一次 UAC 提权。
2. 若游戏已在运行，需先完全退出；加载器只接受开始等待后新启动的 `Arknights.exe`。
3. 检测到游戏后载入核心，加载与启动共限时 120 秒；失败时显示阶段与 Windows 错误码，不对同一进程重试。
4. 加载成功后会从 `BetterBattleWeb\BetterBattleWeb.exe` 自动启动 WebView 控制台。
5. 成功后实时显示 `logs\ArkFramework.log`。游戏退出后继续等待下一次启动。

命令行：

```text
ArkFrameworkLoader.exe --watch [--method remote-thread|window-hook] [--timeout-seconds N] [--report file.json]
ArkFrameworkLoader.exe --pid <PID> [...]          # 已运行的进程，主要用于诊断
ArkFrameworkLoader.exe --pid <PID> --diagnose     # 只报告加载器与目标进程的完整性级别
ArkFrameworkLoader.exe --self-test [--method ...] # 在测试宿主中验证加载流程
```

开发时可使用 `load-game.ps1` 包装上述命令，并在 `.tools\` 下生成 JSON 报告：

```powershell
.\load-game.ps1 -Watch -Method remote-thread -TimeoutSeconds 120 -ElevateIfRequired
```

## 测试

`build.ps1` 会构建并运行：

- 事件模型测试：事件历史、统计模型与关卡名索引。
- `lifecycle_tests`：使用真实 DLL 的加载、依赖、服务租约、热重载、失败恢复与回调排空。
- `native_hook_tests`：共享钩子链、启用/禁用、跨线程卸载排空与原生返回。

`dist\ArkFrameworkHost.exe` 是独立的 D3D11 测试宿主，用回放数据验证浮层绘制与插件生命周期；以下命令覆盖三种加载方式，均检查实际绘制与正常退出：

```powershell
.\dist\ArkFrameworkHost.exe --self-test
.\dist\ArkFrameworkLoader.exe --self-test --method remote-thread --timeout-seconds 30
.\dist\ArkFrameworkLoader.exe --self-test --method window-hook --timeout-seconds 30
.\dist\ArkFrameworkLoader.exe --watch --self-test --timeout-seconds 30
```

## 插件开发

见 [插件 SDK](docs/plugin-sdk.md)。插件应放在 `plugins/<name>/`，并在该目录维护自己的 README。

## 第三方组件

- [Dear ImGui](https://github.com/ocornut/imgui) v1.91.9b — MIT。DX11 后端改为运行时从 System32 加载 `D3DCompile`，并支持 `FramebufferScale` 缩放；Win32 后端按同一比例换算坐标。
- [MinHook](https://github.com/TsudaKageyu/minhook) v1.3.4 — BSD 2-Clause。
- [nlohmann/json](https://github.com/nlohmann/json) — MIT，单头文件版本。

第三方许可证全文位于各自目录，并随发布包复制到 `licenses\`。插件生命周期与绘制回调的设计参考了 [REFramework](https://github.com/praydog/REFramework)。

## 许可证

- ArkFramework 框架本身 以 [GNU 通用公共许可证 v3.0 或更高版本](LICENSE) 发布，对框架二改需要遵循该协议。
- 插件本身不受上述协议影响。
- 第三方组件保留各自的 MIT / BSD 2-Clause 许可证，二者均与 GPL-3.0 兼容。
