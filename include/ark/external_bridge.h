#pragma once

#include <stddef.h>
#include <stdint.h>

#define ARK_EXTERNAL_BRIDGE_ABI_VERSION 1u
#define ARK_EXTERNAL_BRIDGE_SERVICE_NAME "ark.external.bridge"

// Generic one-way bridge from an in-process plugin to an external UI host.
// The bridge deliberately knows nothing about Battle, Buff or Lua data.
struct ArkExternalBridgeApiV1 {
    uint32_t size = sizeof(ArkExternalBridgeApiV1);
    uint32_t abi_version = ARK_EXTERNAL_BRIDGE_ABI_VERSION;
    // Publishes a UTF-8 JSON payload under a topic. The latest payload per topic is
    // retained until it is sent; a new topic value replaces the previous value.
    bool (*publish)(const char* topic, const char* json, size_t size, uint32_t flags);
};
