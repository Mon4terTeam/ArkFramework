# BetterBattle 内嵌游戏视图技术方案

> 状态：基础实现完成，已通过构建和运行时窗口验证。
>
> 结论：可行，但实现方式不是把 Unity 游戏画面嵌进 WebView2，而是让
> `BetterBattleWeb` 仪表盘成为游戏窗口的 owner，在仪表盘上预留游戏矩形，
> 由游戏窗口覆盖该区域。全屏页面和 `游戏视图` 共用同一个宿主、同一个 WebView
> 和同一条数据通道。

## 1. 目标与边界

目标：在同一个屏幕内观察战斗，不必在游戏和 BetterBattle WebView 之间反复切换。
内嵌模式的布局为：

- 上方保留 BetterBattle 自己的导航。
- 左侧为干员/敌方列表，只显示头像和名称。
- 中间为游戏窗口。
- 右侧为选中单位详情。
- 下方为常驻时间轴。
- 编队、战斗配置、数据统计是局外页面；进入这些页面时隐藏游戏并全宽显示。
- 离开战斗/游戏视图时只发送一次 `pause`，返回游戏视图时不自动 `resume`。

非目标：

- 不修改现有全屏模式的页面结构、交互和视觉语言。
- 不把现有 `page-battle` 改造成内嵌模式的布局。
- 不通过屏幕截图或像素取样做验收，视觉检查由用户完成。
- 不在本阶段加入战斗事件列表。
- 不继续缩小游戏画面。已验证的游戏区下限是 `1600×900` client pixels。

## 2. 已确认的技术结论

### 2.1 窗口附着，不是 WebView 内嵌

Unity 游戏使用自己的顶层窗口和渲染表面。把游戏窗口改成 WebView2 的子窗口
（`SetParent`）会引入输入、焦点、D3D 表面、DPI 和销毁顺序问题，不适合作为
当前方案。

已验证可行的方式是：

1. 仪表盘窗口覆盖当前显示器的工作区。
2. 游戏窗口仍是独立顶层窗口。
3. 用 `GWLP_HWNDPARENT` 把仪表盘设置为游戏窗口的 owner。
4. 仪表盘不设置 `TopMost`。
5. 仪表盘在游戏矩形周围绘制 WebView UI，游戏窗口覆盖中间矩形。

效果上看起来像“游戏是仪表盘的一部分”，但技术上游戏没有被 WebView 拥有或
嵌入，WebView 也不需要透明或承载 Unity 渲染。

### 2.2 单宿主内的游戏视图 Tab

不再启动第二个宿主。启动时仍是现有全屏 WebView；导航新增 `游戏视图` Tab，
进入该 Tab 时才把游戏窗口设为 owner 并排版：

```text
BetterBattleWeb.exe
  MainForm
    WebView2
      index.html
        现有全屏页面保持不变
        + page-gameview（游戏视图）
    GameWindowAttachment
      查找游戏窗口 / owner / 布局 / 恢复
```

`游戏视图` 只是同一个 WebView 里的一个新页面，不替换 `page-battle`，也不改变
`page-enemies`、编队、配置、时间轴等现有页面。

这样做的原因：

- 只有一个管道客户端，不会出现两个 WebView 同时争抢数据。
- 现有数据、资源缓存和命令队列只保留一套。
- 现有页面不需要复制一份；新增的只是游戏视图布局和窗口附着。
- 游戏启动前后都可以进入 `游戏视图`；没有游戏窗口时显示等待状态。

### 2.3 单 WebView 客户端限制

`web_bridge` 的 `\\.\pipe\ArkFramework.WebView` 当前一次只接受一个 WebView
客户端。因此：

- 只运行一个 `BetterBattleWeb.exe`。
- `游戏视图` 与该宿主的其他页面共用 `BetterBattleSession` 和同一条 pipe。
- 不需要为了内嵌模式增加第二条数据连接或第二个快照解析器。

## 3. 现有架构复用

### 3.1 宿主

现有宿主是 .NET 8 WinForms + WebView2：

- `webview/Program.cs`：入口、单实例和 fixture 参数。
- `webview/MainForm.cs`：WebView2 初始化、命名管道读取、命令队列、资源缓存。
- `webview/ui/index.html`：现有全屏页面。
- `webview/ui/js/app.js`：页面切换、快照渲染和命令发送。

`MainForm` 持有 `GameWindowAttachment`；窗口附着、布局计算、owner 生命周期
都在这个组件中。`游戏视图` 页面只负责测量预留矩形和发送布局消息，数据仍由现有
`web_bridge` 提供。

### 3.2 数据主题

现有页面已经消费：

- `betterbattle.snapshot`：当前战斗状态、卡牌、我方单位、敌人、Buff、编队、回放等。
- `betterbattle.catalog`：干员目录。
- `betterbattle.profiles`：干员档案和静态信息。
- `betterbattle.timeline`：时间轴节点。

内嵌模式继续使用 `BetterBattleSession` 的快照入口和现有命令队列：

- 不新增第二套游戏对象轮询。
- 不新增每帧全量扫描。
- 不直接按渲染帧读取 Unity 对象。
- 需要逐帧机制分析时仍使用 `trace start` / `trace stop` 和逻辑帧数据。

### 3.3 现有页面保留

全屏模式继续保留：

- 战斗
- 敌方
- 编队
- 战斗配置
- 时间轴
- 数据统计
- 诊断

内嵌视图只提供精简导航：

```text
游戏视图    编队    战斗配置    数据统计
   |
   +-- 左侧子 Tab：干员 / 敌方
   +-- 中央：游戏窗口
   +-- 右侧：选中单位详情
   +-- 下方：时间轴
```

内嵌模式不显示诊断页；诊断仍由全屏模式提供。

## 4. 游戏窗口附着

### 4.1 目标进程识别

测试客户端必须来自：

```text
E:\Games\ArkTest\Arknights\Arknights.exe
```

不能使用 `E:\Games\ArkTest\Arknights Game\Arknights.exe` 或其他旧路径。附着前应：

1. 枚举 `Arknights` 进程。
2. 按完整可执行文件路径进行大小写不敏感匹配。
3. 排除没有主窗口的进程。
4. 当前客户端可进一步校验窗口类 `UnityWndClass` 和标题 `明日方舟`。
5. 若有多个候选，优先选择可见且面积最大的窗口，并记录 PID/HWND。

不要只按进程名匹配，避免旧客户端或其他同名进程被误附着。

### 4.2 Owner 关系

已验证的调用为：

```text
SetWindowLongPtrW(gameHwnd, GWLP_HWNDPARENT, dashboardHwnd)
GetWindow(gameHwnd, GW_OWNER) == dashboardHwnd
```

注意：

- 这是设置 owner，不是设置 parent；不要调用 `SetParent`。
- 先保存原来的 owner，退出时恢复原值，不能假设原 owner 一定是 `NULL`。
- 仪表盘保持 `TopMost = false`。owner 关系只保证游戏位于仪表盘之上，不应把
  整个桌面置顶。
- 游戏窗口被移动、缩放、显示或隐藏时，仪表盘需要同步重新计算布局。
- owner 窗口先销毁会让 owned 游戏窗口面临被系统销毁的风险，退出流程必须先
  解除 owner。

### 4.3 附着与恢复顺序

推荐顺序：

1. 找到游戏 HWND，保存：
   - 原 owner；
   - `GWL_STYLE`；
   - 原窗口矩形 `GetWindowRect`；
   - 原 client 尺寸 `GetClientRect`；
   - 可选的 `WINDOWPLACEMENT`。
2. 将仪表盘移动到游戏所在显示器的工作区。
3. 设置 owner，并立即验证 `GetWindow(hwnd, GW_OWNER)`。
4. 按布局设置游戏窗口位置和大小。
5. 在所有布局操作完成后再显示/定位游戏，避免用户看到中间跳变。

退出或关闭内嵌模式：

1. 停止窗口监听和尺寸监听。
2. 恢复原 owner。
3. 恢复原窗口样式。
4. 恢复原窗口矩形和 client 尺寸。
5. 必要时调用 `SetWindowPos(..., SWP_FRAMECHANGED)` 让样式生效。
6. 最后销毁 WebView 和仪表盘。

建议把原 owner、样式和矩形写入一个小的恢复记录文件。如果宿主异常退出，下次启动
时先检查游戏 PID/HWND 是否仍存在，再恢复窗口，避免游戏长期停留在被改过的状态。

### 4.4 去除游戏标题栏

目标是让游戏画面不带 Windows caption，视觉上更像仪表盘的一部分。

已尝试的思路是移除以下窗口样式：

```text
WS_CAPTION
WS_SYSMENU
WS_MINIMIZEBOX
WS_MAXIMIZEBOX
WS_THICKFRAME
```

然后调用：

```text
SetWindowPos(..., SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED)
```

该路径在独立 demo 中可以工作，但仍标记为实验项，原因是：

- Unity 可能在运行时重新设置窗口样式。
- 去除边框会改变 frame/client 的差值，之前计算的窗口尺寸不能直接复用。
- 游戏若被切换成全屏或重建窗口，样式可能再次变化。
- 部分游戏版本可能不接受无 caption 的窗口样式。

因此实现时应：

- 保存原始 `GWL_STYLE`。
- 样式变更后重新测量 `GetWindowRect` 和 `GetClientRect`。
- 用新的 border 差值重新计算 `1600×900` client 区对应的 frame 尺寸。
- 在 owner 存续期间监听样式变化，必要时只重应用一次，避免和 Unity 反复抢样式。
- 退出时恢复原始样式和原始矩形。

如果 Unity 强制恢复 caption，优先接受标题栏并调整游戏矩形，不要为了隐藏标题栏
引入一个长期 `TopMost` 的遮挡窗口。确实需要视觉遮挡时，遮罩也必须是短生命周期的
独立顶层窗口，并在退出时完整销毁。

### 4.5 DPI 与坐标

宿主使用 Per-Monitor V2 DPI。Win32 的窗口坐标和尺寸是物理像素，CSS 的布局单位
是 DIP（可以近似理解为 96 DPI 下的像素）。两者不能混用。

```text
scale = GetDpiForWindow(hwnd) / 96
physical = round(css * scale)
css = physical / scale
```

当前测试环境为 150% DPI：

- 屏幕物理尺寸约 `2560×1600`。
- CSS viewport 约为 `1707×1067`。
- 游戏验收目标是 client 区 `1600×900` 物理像素。
- 因而游戏在 WebView 坐标中约占 `1067×600` CSS 像素。

`1600×900` 是 **client** 区目标，不是 frame 区。frame 尺寸必须加上当前窗口边框：

```text
targetFrameWidth  = targetClientWidth  + (frameWidth  - clientWidth)
targetFrameHeight = targetClientHeight + (frameHeight - clientHeight)
```

去除 caption 之后必须重新取一次边框差值。

## 5. 布局

### 5.1 推荐布局

仪表盘覆盖游戏所在显示器的工作区，使用自定义无边框窗口；游戏区位于仪表盘
中央偏左。以 CSS 像素计算的面板建议为：

- 顶部导航：约 `50` CSS px，沿用现有 `base.css` 的 `.topbar`。
- 左侧干员/敌方列表：约 `220` CSS px。
- 右侧详情：约 `390` CSS px。
- 底部时间轴：约 `250` CSS px。
- 面板间距：约 `6` CSS px。
- 游戏区外边距：约 `12` CSS px。

游戏区建议位置：

```text
gameX = work.left + leftPanel + margin
gameY = work.top + topbar + margin
gameW = 1600 physical client pixels
gameH = 900 physical client pixels
```

左侧面板从 topbar 下方开始，底部时间轴覆盖左侧和游戏区下方；右侧详情从 topbar
下方一直延伸到工作区底部。顶部导航横跨整个仪表盘。

### 5.2 150% DPI 下的空间检查

在 `2560×1600`、150% DPI 下：

```text
left panel   = 220 * 1.5 = 330 physical
right panel  = 390 * 1.5 = 585 physical
topbar       =  50 * 1.5 =  75 physical
bottom panel = 250 * 1.5 = 375 physical
game         = 1600 × 900 physical
```

宽度上仍能容纳 `1600` 的游戏区。实际布局必须使用工作区而不是整个屏幕，并预留
任务栏和窗口边框。

### 5.3 紧凑回退

如果显示器宽度不足，不能把游戏继续缩小来硬塞侧栏。建议的回退顺序：

1. 保留游戏 `1600×900`，折叠左侧列表或把右侧详情改为抽屉。
2. 仍不足时，使用游戏原始 `1921×1080` client 区，而不是缩小到 `1600×900` 以下。
3. 提示用户当前分辨率无法同时容纳游戏和侧栏。

回退布局只改变 UI 面板的呈现方式，不改变游戏的可读尺寸底线。

## 6. UI 内容

### 6.1 左侧列表

左侧只承担选择功能：

- 干员/敌方两个子 Tab。
- 每项只显示头像和名称。
- 不显示 HP、SP、费用、职业、冷却条或其他元数据。
- 选中项用现有设计语言的选中态表达。

这样可以显著压缩宽度，也避免和右侧详情重复。

### 6.2 右侧干员详情

只展示：

- 当前数值属性；
- 当前 Buff。

不展示：

- 立绘；
- 天赋；
- 模组；
- 技能描述；
- 技能动画/大段说明文字。

立绘和技能说明已经由游戏窗口本身提供，内嵌视图不应重复占用空间。数值属性和
Buff 仍从现有 snapshot 读取，沿用现有 `.stat-cell`、`.buff-list` 等组件。

### 6.3 右侧敌方详情

保留现有敌方属性展示。敌方列表本身只显示头像和名称；右侧仍可展示已有属性数据。

### 6.4 时间轴

时间轴常驻底部：

- 使用现有时间轴数据和组件。
- 不展示战斗事件列表。
- 事件详情留到后续单独设计，不阻塞当前布局。

### 6.5 视觉语言

内嵌页面必须复用现有全屏 UI 的设计语言：

- `base.css` 的 CSS 变量、字体、颜色和顶栏。
- `.panel`、`.panel-head`、`.badge`、`.btn`、`.stat-cell`、`.buff-list`、
  `.unit-tab`、`.enemy-row` 等现有类。
- 不新造一套绿色渐变、圆角卡片或另一套间距体系。
- 不在内嵌页面里复制定制 demo 的样式。

## 7. 页面行为与自动暂停

### 7.1 页面切换

内嵌模式页面分为两类：

- 战斗上下文：`游戏视图`。
- 局外上下文：`编队`、`战斗配置`、`数据统计`。

行为：

1. 进入 `游戏视图`：显示并定位游戏窗口，不自动恢复战斗。
2. 离开 `游戏视图` 到任一局外页面：先发送 `pause`，再隐藏游戏窗口。
3. 局外页面全宽显示，不保留游戏矩形。
4. 从编队/配置开始战斗时，自动切换到 `游戏视图` 并显示游戏。
5. 从局外页面返回 `游戏视图`，只显示游戏，不发送 `resume`。

自动暂停必须是幂等的：如果 snapshot 已经显示 `paused=true`，不要重复排队大量
`pause` 命令。只提供“离开时暂停”，不提供“回来时继续”；用户必须显式点击
手动继续入口。

### 7.2 数据统计

数据统计属于局外分析页：

- 全宽显示；
- 隐藏游戏；
- 离开战斗时触发暂停；
- 不提供战斗内常驻图表，也不占用时间轴下方区域。

### 7.3 游戏隐藏与恢复

离开游戏视图后，游戏可以 `ShowWindow(SW_HIDE)`；重新进入时再显示并重新定位。
隐藏和恢复期间：

- 不假设 Unity 会继续渲染或保持输入状态；
- 暂停命令应在隐藏前发出；
- 恢复显示后重新读取窗口矩形和 DPI；
- 不自动继续战斗。

如果 Unity 在 `SW_HIDE` 后出现渲染恢复问题，应在实现阶段改为“保留窗口但移出
工作区”或“保持显示并在 UI 层遮挡”，并把结果记录到实测文档；不能在未验证前
假定隐藏是安全的。

## 8. 数据与组件复用

### 8.1 复用原则

内嵌页面不应再建立一条 pipe 客户端。建议：

- 继续使用现有 `BetterBattleSession` 的 snapshot/timeline 入口。
- 继续使用现有命令队列写入 `BetterBattle.request`。
- 继续使用现有资源缓存和 `asset-loader`。
- 只把需要的渲染逻辑提取成共享函数，或者在内嵌入口中直接调用现有函数。
- 不在内嵌页面新增定时器去扫描 snapshot。

### 8.2 共享渲染的边界

当前 `app.js` 同时承担页面状态、DOM 查找和多个 renderer 的调度。内嵌模式可以
先通过独立入口复用数据对象和 CSS，再逐步提取以下渲染能力：

- 干员列表项；
- 敌方列表项；
- 数值属性网格；
- Buff 列表；
- 时间轴。

提取时必须保证全屏页面输出不变。不能为了内嵌布局改动现有 renderer 的 DOM
结构、类名或选择器。

## 9. 主要风险

| 风险 | 影响 | 处理方向 |
|---|---|---|
| Unity 重置窗口样式 | 标题栏重新出现、frame 尺寸变化 | 保存并监听样式；重新测量；不强行循环争抢 |
| 关闭仪表盘未解除 owner | 游戏可能被销毁 | `FormClosing` 中先恢复 owner/窗口，再销毁 WebView |
| `SetParent` 子窗口方案 | 输入/渲染/DPI 异常 | 不使用，坚持 owner 方案 |
| DPI 混用 | 游戏区偏大/偏小，面板错位 | Win32 用物理像素，CSS 用 DIP，统一换算 |
| 游戏隐藏后恢复异常 | 黑屏、输入丢失 | 当前保留缓存 HWND；再次进入直接显示并重排，若 Unity 异常再改用移屏方案 |
| 单 WebView 客户端 | 第二个宿主没有数据 | 只启动一个宿主，`游戏视图` 共用现有 pipe |
| AutoLogin 加载链路 | 手工启动宿主可能没有插件环境 | 使用 `run-test.ps1` 启动完整测试链路 |
| 继续缩小游戏 | 文字和地图不可读 | `1600×900` client 作为硬下限 |
| 顶层窗口抢焦点 | 导航点击后游戏失焦 | owner + `SWP_NOACTIVATE` 测试，避免全局置顶 |

## 10. 验证计划

实现阶段的验收顺序：

1. 确认测试客户端从 `E:\Games\ArkTest\Arknights\Arknights.exe` 启动。
2. 使用 `run-test.ps1` 完成加载器、AutoLogin、游戏和 Web 宿主的启动；不要手工
   启动一个没有 AutoLogin 的 WebView 来代替完整流程。
3. 校验宿主日志中：
   - 游戏 PID/HWND；
   - `ownerOk=True`；
   - 游戏 client 区为 `1600×900`；
   - 关闭后 owner 解除、原矩形和原样式恢复。
4. 校验内嵌模式的 `游戏视图`、`编队`、`战斗配置`、`数据统计` 切换。
5. 校验离开游戏视图时只发送一次 `pause`，返回时不发送 `resume`。
6. 校验全屏模式的原有页面、导航和数据渲染没有变化。
7. 通过状态文件和宿主日志做行为验证；不做截图或像素检查，视觉验收由用户完成。

## 11. 当前状态

基础实现已经接入同一个 WebView：

- `MainForm` + `GameWindowAttachment` 负责游戏窗口 owner、尺寸和恢复。
- `index.html` 新增 `游戏视图` 页面，现有页面结构保留。
- `features-gameview.js` 复用 `operatorDetailView`、`enemyStatRows`、Buff 渲染和
  `BetterBattleTimeline` 的紧凑时间轴。
- 离开 `游戏视图` 会发送一次 `pause`，返回时不发送 `resume`。

独立窗口 demo 仍可作为 owner 附着和布局的验证参考，但不属于正式 UI：

```text
webview/demo/
```

当前已验证：`ownerOk=True`、游戏 client `1600×900`、切出后状态 `paused=1`、
再次进入保持暂停、关闭/切出后原窗口矩形和 caption 样式恢复。
