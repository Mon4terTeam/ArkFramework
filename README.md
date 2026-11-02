# ArkFramework

面向 Windows x64 / Unity IL2CPP 游戏的原生插件框架，以及基于它实现的游戏内伤害统计插件。加载器与伤害统计插件当前适配《明日方舟》PC 客户端（`Arknights.exe`）。

- **通用核心** `ArkFramework.dll`：插件动态加载、卸载与热重载，依赖与命名服务，游戏线程任务队列，共享 Dear ImGui 浮层，原生函数钩子及其安全排空。
- **加载器** `ArkFrameworkLoader.exe`：等待游戏启动后用标准 `LoadLibraryExW` 载入核心，不修改游戏目录。
- **伤害统计** `plugins/damage_meter.dll`：采集战斗伤害事件，按干员和伤害类型统计实际扣血与计算伤害，并导出 CSV。

> 本项目仅供学习与研究。使用第三方工具可能违反游戏的用户协议，由此产生的一切后果由使用者自行承担。

## 功能

**核心**

- `Insert` 统一显示/隐藏插件管理器及全部插件页面；隐藏时保留页面状态，后台采集与任务继续运行。管理器可查看状态，加载、卸载、重新加载插件，扫描插件目录。
- 可开启“文件变化时热重载”；插件以影子副本加载，可直接覆盖重编的 DLL。
- 依赖链整体预检、按依赖顺序停止与恢复；提供方仍有消费者时拒绝单独卸载。
- 卸载前排空回调、任务、当前 ImGui 帧与原生钩子调用；超时或清理失败时保留 DLL，不强制释放仍在执行的代码。
- 统一 UI 缩放（50%–200%），只影响插件界面。
- 日志、缓存与导出均位于核心所在目录下的 `logs`、`cache`、`exports`。

**伤害统计**

- 观察原生 `BattleLogger.LogModifier` / `LogEpModifier`，分别记录计算伤害（`finalDelta`）与实际扣血（`realDelta`）。
- 按干员分组，展开显示物理、法术、真实、元素四类 HP 伤害；元素损伤累积单独记录，不计入 HP 伤害。
- 固定按实际扣血降序排列；默认显示 DPS（实际扣血 ÷ 本次已统计的游戏时间），可用「显示 DPS」开关隐藏该列。
- 显示关卡名、游戏时间与原生逻辑帧；角色、召唤物和关卡名称从游戏数据解析，失败时保留原始 ID。
- 战斗结束自动导出 `exports\damage-<pid>-<battle>.csv`，也可手动导出。
- 关闭窗口不停止采集；卸载插件会结束采集并丢弃本次历史，中途接入的战斗会标记为不完整。

## 目录结构

```text
include/ark/            插件 SDK 头文件（plugin.h、battle.h、imgui_config.h）
src/core/               核心：插件管理、原生钩子、D3D11 浮层与入口
src/loader/             外部加载器 ArkFrameworkLoader.exe
src/bootstrap/          窗口消息钩子引导 ArkBootstrap.dll（--method window-hook）
plugins/common/         插件共用的 IL2CPP 运行时、Unity 输入适配与窗口布局
plugins/damage_meter/   伤害统计插件
tests/                  单元测试、生命周期与原生钩子测试、测试夹具、D3D11 测试宿主
third_party/            Dear ImGui 1.91.9b、MinHook 1.3.4
cmake/                  CMake 的 Zig 工具链文件
docs/plugin-sdk.md      插件开发文档
```

## 构建

需要 [Zig 0.15.2](https://ziglang.org/download/)（作为 C/C++ 编译器），放在 `PATH` 中或 `.tools\zig\ziglang\zig.exe`。

```powershell
.\build.ps1                 # 运行测试并构建全部组件到 dist\
.\build.ps1 -Target Tests   # 仅构建并运行自动测试
.\build.ps1 -Target Plugin  # 仅重编插件（需先完成一次完整构建）
.\build.ps1 -Release        # 无调试信息的发布构建，中间文件位于 build\release
.\package.ps1               # 发布构建 + 全部测试，生成 package\ArkFramework
```

核心运行中时 `ArkFramework.dll` 与 `ArkImGui.dll` 无法覆盖，可用 `-DistDirectory <目录>` 构建到其他目录；插件可直接覆盖后热重载。

也可使用 CMake 3.22+ 与 Ninja。`cmake/zig-toolchain.cmake` 使用 Zig 自带的 Clang（`x86_64-windows-gnu`），该配置已验证可构建并通过全部 CTest；MSVC 尚未验证。

```powershell
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release --toolchain cmake/zig-toolchain.cmake
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

## 使用

发布目录结构如下，保持文件夹完整即可放在任意位置：

```text
ArkFrameworkLoader.exe
ArkFramework.dll
ArkImGui.dll
ArkBootstrap.dll
plugins\damage_meter.dll
LICENSE
licenses\
```

双击 `ArkFrameworkLoader.exe`：

1. 游戏以管理员权限运行时，加载器会请求一次 UAC 提权。
2. 若游戏已在运行，需先完全退出；加载器只接受开始等待后新启动的 `Arknights.exe`。
3. 检测到游戏后载入核心，加载与启动共限时 120 秒；失败时显示阶段与 Windows 错误码，不对同一进程重试。
4. 成功后实时显示 `logs\ArkFramework.log`。游戏退出后继续等待下一次启动。

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

- `damage_meter_tests`：事件历史、统计模型与关卡名索引。
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

见 [插件 SDK](docs/plugin-sdk.md)。

## 第三方组件

- [Dear ImGui](https://github.com/ocornut/imgui) v1.91.9b — MIT。DX11 后端改为运行时从 System32 加载 `D3DCompile`，并支持 `FramebufferScale` 缩放；Win32 后端按同一比例换算坐标。
- [MinHook](https://github.com/TsudaKageyu/minhook) v1.3.4 — BSD 2-Clause。

第三方许可证全文位于各自目录，并随发布包复制到 `licenses\`。插件生命周期与绘制回调的设计参考了 [REFramework](https://github.com/praydog/REFramework)。

## 许可证

ArkFramework 以 [GNU 通用公共许可证 v3.0 或更高版本](LICENSE)（GPL-3.0-or-later）发布。第三方组件保留各自的 MIT / BSD 2-Clause 许可证，二者均与 GPL-3.0 兼容。
