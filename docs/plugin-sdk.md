# 插件 SDK

插件是放在核心目录 `plugins\` 下的 DLL，使用 `include/ark/plugin.h`、Dear ImGui v1.91.9b 与共享的 `ArkImGui.dll`。插件不得静态链接另一份 ImGui，也不得销毁共享上下文。

```cpp
ARK_EXPORT const ArkPluginInfoV1* ark_plugin_query();
ARK_EXPORT bool ark_plugin_initialize(const ArkPluginApiV1* api);
ARK_EXPORT bool ark_plugin_shutdown();
// 可选：无副作用的卸载预检；返回 false 时整条重载链保持运行。
ARK_EXPORT bool ark_plugin_can_unload(char* reason, size_t capacity);
```

`ArkPluginInfoV1` 声明结构大小、`ARK_ABI_VERSION`、`IMGUI_VERSION_NUM`、名称、版本、作者、标志以及直接依赖的 DLL 文件名。ABI 或 ImGui 版本不一致的插件会被拒绝加载。

最小示例：

```cpp
#include <ark/plugin.h>
#include <imgui.h>

namespace {
const ArkPluginApiV1* host = nullptr;

void draw(void*, const ArkFrameV1* frame) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(frame->imgui_context));
    ImGui::Begin("Hello");
    ImGui::TextUnformatted("Hello from a plugin");
    ImGui::End();
}
} // namespace

ARK_EXPORT const ArkPluginInfoV1* ark_plugin_query() {
    static const ArkPluginInfoV1 info{sizeof(ArkPluginInfoV1), ARK_ABI_VERSION, IMGUI_VERSION_NUM,
                                      "Hello", "1.0.0", "Example"};
    return &info;
}

ARK_EXPORT bool ark_plugin_initialize(const ArkPluginApiV1* api) {
    if (!api || api->size < sizeof(ArkPluginApiV1) || api->abi_version != ARK_ABI_VERSION) {
        return false;
    }
    host = api;
    return host->on_draw(host->owner, draw, nullptr) != 0;
}

ARK_EXPORT bool ark_plugin_shutdown() {
    host = nullptr;
    return true;
}
```

## 核心职责

核心只提供加载、依赖与生命周期、命名服务、帧/绘制回调、游戏线程任务队列、输入适配、原生调用钩子及共享绘制；SDK 中不包含任何游戏业务类型。游戏相关逻辑（IL2CPP 反射、Unity 输入、战斗数据）都在插件内实现，可共用 `plugins/common` 中的辅助代码。

## 依赖与命名服务

- 依赖只能是插件目录中的叶文件名，最多 256 个；宿主先初始化依赖，再初始化消费者。缺失依赖、循环依赖和越界路径会被拒绝。
- 提供方用 `provide_service(owner, name, abi, table, table_size)` 注册同进程函数表；消费者用 `require_service(owner, name, abi, minimum_size)` 获取，并自行检查表的版本、大小与所需函数。
- 服务只能从声明的**直接依赖**获取，间接依赖不会自动授予访问。依赖关系保证提供方不会先于消费者卸载。
- 跨 DLL 只传递 POD、显式长度的缓冲区和函数指针，不传递 STL 容器或插件私有对象的所有权。

进程内的外部宿主可调用核心导出的 `ark_framework_with_service(name, abi, minimum_size, callback, user)`：回调期间持有提供方租约，表和函数指针不得逃逸出回调。返回 `false` 表示服务不可用、核心正在停止或回调抛出异常。

## 生命周期

- 每个 SDK 回调都归属于具体插件实例。卸载时先阻止新的进入、移除回调及尚未执行的任务，再等待正在执行的调用结束；绘制租约覆盖整个 ImGui 帧，包括 ImDrawList 自定义回调。
- 清理成功后才调用 `FreeLibrary` 并删除影子副本。超时或 `ark_plugin_shutdown` 返回 `false` 时保留模块及必要依赖，不强制释放仍可能执行的代码。`shutdown` 被调用时 SDK 回调已经移除，再次 `remove_callback` 返回 `false` 不代表失败。
- “卸载”拒绝仍有消费者的提供方；“重新加载”覆盖所有传递消费者：先整体预检，再按消费者到提供方的顺序停止，按依赖顺序加载新副本。
- `ark_plugin_can_unload` 必须无副作用；拒绝时写入有界、零结尾的原因。`shutdown` 仍需复查可能在预检之后发生变化的状态。
- `ARK_PLUGIN_PROCESS_LIFETIME` 让模块驻留到进程退出，仅供确有其他常驻义务的插件使用；通过宿主创建的原生钩子不需要该标志。
- 重载创建新实例，不迁移私有状态。插件必须在 `shutdown` 中停止并回收自己的线程与外部回调，清理成功后不得再调用 SDK。

## 绘制与游戏线程

- `on_frame` / `on_draw` 回调在渲染线程执行，通过 `ArkFrameV1.imgui_context` 使用共享 ImGui。插件使用 ImGui 逻辑坐标与 `ImGui::GetIO().DisplaySize`；核心通过 `DisplayFramebufferScale` 统一缩放整个界面，插件不要自行乘以缩放比例。
- `Insert` 控制整个浮层的显隐：`on_frame` 始终执行，用于后台更新；`on_draw` 仅在浮层可见时执行，用于绘制界面。隐藏不卸载插件，也不重置插件自身的页面状态；隐藏期间核心不捕获新的鼠标或键盘输入，已归属界面的按键释放仍由输入适配器排空。
- 游戏主线程 API 不得在渲染回调中直接调用，应使用 `queue_game_task` 排队。核心只持有队列，由插件的原生线程适配器在正确的线程调用 `pump_game_tasks`。

## 输入适配

`ArkInputAdapterV1` 把捕获状态、窗口消息和原生轮询保护交给应用插件。核心根据当前 Win32 鼠标事件坐标判断 ImGui 是否占用输入；适配器负责保持原生按下/抬起的配对，避免游戏丢失释放事件。

- 每个插件最多注册一个适配器；核心向所有适配器广播捕获状态，窗口消息与保护查询会合并全部适配器的结果。
- 插件停止期间仍会收到释放尾事件。注销时先完成按键释放与中性帧排空，再调用 `set_input_adapter(owner, nullptr)`；不得在持有消息路由所需锁的情况下等待排空。注销失败时须保留对象与模块。

## 原生调用钩子

`create_native_hook / enable_native_hook / remove_native_hook` 允许多个插件安全地共享同一原生目标。

- 仅支持 Windows x64：最多四个整数/指针寄存器参数，返回整数/指针/void；不支持按值浮点/向量参数或栈参数。目标必须是进程常驻代码，不能位于可卸载的 DLL 中。
- 钩子创建时处于禁用状态，返回 ID 与 `original` 网关；只有所属插件能操作该 ID。启用的回调按创建时间从新到旧执行，`original` 继续调用其下方的链，原生目标只执行一次。
- 禁用只跳过本插件的链节；最后一个启用的链节停用时恢复原入口。禁用不等待在途调用。
- 移除会等待已进入的调用结束；超时、在回调中移除自身或跨回调循环等待时返回 `false`，状态保留以便重试。移除成功前必须保留回调状态与 DLL；成功后不得再调用已交还的 `original`。
- 清理顺序：先停止会触发调用的异步来源，再移除钩子，最后销毁业务状态。

## 战斗事件服务

`include/ark/battle.h` 定义由 `damage_meter.dll` 提供的两套战斗事件服务。消费者声明对 `damage_meter.dll` 的依赖后，按需要选择服务名：

- `ark.battle.events`，ABI 1：`ArkBattleEventsApiV1` / `ArkEventV1`，与 1.0.0 完全兼容，字段和布局不变。
- `ark.battle.events.v2`，ABI 2：`ArkBattleEventsApiV2` / `ArkEventV2`，增加逻辑帧、帧内序号、目标、原始来源和召唤物宿主。

`damage_meter.dll` 同时注册两套服务。旧消费者继续请求 ABI 1 即可，不需要改代码；新消费者显式请求 V2 服务名。不要用 ABI 2 的表去填 ABI 1 的缓冲区，两者的事件布局不同。

- `read(after, buffer, capacity, status)` 返回序号大于 `after` 的事件，单次最多 4096 条。消费者从序号 0 开始重放当前历史，并保存最后序号以免重复累计。ABI 1 的 `read` 写入 `ArkEventV1`，ABI 2 的 `read` 写入 `ArkEventV2`。
- 事件种类：`BattleStart`、`Damage`、`Elemental`、`BattleEnd`、`Clock`。`status.partial` 表示中途接入或历史被截断，`status.overrun` 表示读取落后于有界历史（65,536 条）。
- `frame(out)` 返回当前战斗的原生逻辑帧（`BattleController.fixedFrameCnt`）及其所属 `battle`，可在渲染线程调用。调用者应核对 `battle` 与正在展示的数据一致；尚未采集时返回 `false`，不能当作帧号 0。结算后保留最终帧，下一次 `BattleStart` 作废旧值。
- `frame` 是 `ArkEventV2` 的第一时间戳；逻辑帧固定为 30/s。`frame_event_index` 是该事件在所属帧内的发布顺序。`sequence` 是跨帧的全局历史游标，只用于读取和去重。
- `ArkDamageType` 沿用原生编号：物理 1、法术 2、真实 3、治疗 4、元素 5。`ARK_DAMAGE` 且类型为 5 是元素 HP 伤害；`ARK_ELEMENTAL` 是元素损伤累积，不计入 HP 伤害。治疗、无类型与未知类型不计入四类伤害。
- `game_time` 保留原生战斗时间（`BattleController.fixedPlayTime`），只作为辅助显示值；统计和排序以 `frame` 为准。
- `actor_id` 是伤害来源角色；来源为召唤物/token 且能解析出宿主时，`owner_id` 为宿主干员 ID。`target_id` 是本次 Modifier 的目标实体，`original_source_id` 是原始来源实体。按干员汇总时使用 `owner_id`，逐实体和逐目标分析时使用 `actor_id`/`target_id`。

采集器、事件历史与统计界面都属于 `damage_meter.dll`：关闭统计窗口仍继续采集；卸载或重载会停止采集并销毁历史，不补造缺失数据。

`ark.battle.test`（ABI 1）和 `ark.battle.test.v2`（ABI 2）发布接口仅在 `ArkFrameworkHost.exe` 测试宿主中注册，游戏中不可用。旧测试宿主使用前者，新测试宿主使用后者。

## 共用辅助代码

`plugins/common` 中的代码按需编入插件，不属于核心：

- `il2cpp_exports.h`：解析 `GameAssembly.dll` 导出函数，检查可执行内存。
- `il2cpp_runtime.*`：IL2CPP 反射与托管调用辅助。只在已附加的线程上调用托管方法，持有所需 GC 根，并按**方法声明类型**决定是否拆箱接收者（值类型继承的 `System.Enum` / `System.Object` 方法仍需装箱接收者）。
- `unity_input.*`：Unity 原生输入轮询适配，配合 `ArkInputAdapterV1` 使用。
- `window_layout.h`：按当前显示区域约束窗口初始大小与位置。
