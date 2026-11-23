#include "../runtime/custom_module.h"

#include <ark/plugin.h>

#include <imgui.h>

#include <atomic>
#include <cstdio>
#include <memory>

namespace {

std::atomic<ark::custom::CustomModule*> module{nullptr};
std::uint64_t frame_callback = 0;
ArkPluginApiV1 host_api{};

void on_frame(void* user, const ArkFrameV1*) {
    auto* instance = static_cast<ark::custom::CustomModule*>(user);
    if (instance) {
        instance->frame();
    }
}

bool host_is_compatible(const ArkPluginApiV1* host) {
    return host && host->size >= sizeof(ArkPluginApiV1) &&
           host->abi_version == ARK_ABI_VERSION &&
           host->imgui_version == IMGUI_VERSION_NUM && host->log &&
           host->on_frame && host->remove_callback && host->queue_game_task &&
           host->pump_game_tasks && host->root_directory &&
           host->create_native_hook && host->enable_native_hook &&
           host->remove_native_hook;
}

} // namespace

ARK_EXPORT const ArkPluginInfoV1* ark_plugin_query() {
    static const ArkPluginInfoV1 info{
        .size = sizeof(ArkPluginInfoV1),
        .abi_version = ARK_ABI_VERSION,
        .imgui_version = IMGUI_VERSION_NUM,
        .name = "CustomSkills",
        .version = "1.0.0",
        .author = "ArkFramework",
        .flags = ARK_PLUGIN_PROCESS_LIFETIME,
        .dependencies = nullptr,
        .dependency_count = 0,
    };
    return &info;
}

ARK_EXPORT bool ark_plugin_initialize(const ArkPluginApiV1* host) {
    if (!host_is_compatible(host)) {
        return false;
    }
    host_api = *host;
    auto backend = std::make_unique<ark::custom::CustomModule>(*host);
    frame_callback = host->on_frame(host->owner, &on_frame, backend.get());
    if (!frame_callback) {
        return false;
    }
    module.store(backend.release(), std::memory_order_release);
    host->log(host->owner, ark::custom::kLogInfo,
              "CustomSkills initialized; request file: CustomSkills.request");
    return true;
}

ARK_EXPORT bool ark_plugin_can_unload(char* reason, size_t capacity) {
    auto* backend = module.load(std::memory_order_acquire);
    if (!backend || backend->can_unload()) {
        return true;
    }
    if (reason && capacity) {
        std::snprintf(reason, capacity, "%s", "CustomSkills is still busy.");
    }
    return false;
}

ARK_EXPORT bool ark_plugin_shutdown() {
    auto* backend = module.exchange(nullptr, std::memory_order_acq_rel);
    if (!backend) {
        return true;
    }
    if (!backend->shutdown()) {
        module.store(backend, std::memory_order_release);
        return false;
    }
    if (frame_callback) {
        host_api.remove_callback(host_api.owner, frame_callback);
        frame_callback = 0;
    }
    delete backend;
    return true;
}
