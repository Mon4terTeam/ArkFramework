# web_bridge

ArkFramework 的通用外部 WebView 桥接插件。它只负责把插件发布的 `topic + JSON` 通过命名管道送到外部宿主，不包含战斗、Buff 或 Lua 业务字段。

## 服务

- 服务名：`ark.external.bridge`，ABI 1。
- 接口：`ArkExternalBridgeApiV1::publish(topic, json, size, flags)`。
- 管道：`\\.\pipe\ArkFramework.WebView`，仅本机连接。
- 帧格式：4 字节小端长度 + UTF-8 JSON；外层为 `{"topic":"...","seq":123,"payload":{...}}`。
- 每个 topic 只保留最新负载；客户端连接后会先收到所有 topic 的最新值。

## 构建

```powershell
cmake --build build-cmake --target web_bridge
```

## 文档

协议细节和宿主行为见 [ExternalBridge](../../docs/ExternalBridge.md)。
