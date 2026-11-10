// Damage Meter plugin: native battle capture, the ark.battle.events service, per-actor damage
// statistics, an ImGui panel and CSV export.
#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>
#include <stdexcept>

#include <ark/battle.h>
#include <ark/plugin.h>
#include <imgui.h>

#include "meter.h"

namespace ark::damage_meter {
namespace {

constexpr std::size_t kMaxReadCapacity = 4096;

std::atomic<Backend*> active_backend{nullptr};
Meter meter;

// --- Battle services -------------------------------------------------------------------------

std::size_t read_events_v1(std::uint64_t after, ArkEventV1* out, std::size_t capacity,
                           ArkReadStatusV1* status) {
    Backend* backend = active_backend.load();
    if (!backend || backend->stopping || !out || !capacity || capacity > kMaxReadCapacity ||
        (status && status->size < sizeof(*status))) {
        return 0;
    }
    return backend->history.read_v1(after, out, capacity, status);
}

std::size_t read_events_v2(std::uint64_t after, ArkEventV2* out, std::size_t capacity,
                           ArkReadStatusV1* status) {
    Backend* backend = active_backend.load();
    if (!backend || backend->stopping || !out || !capacity || capacity > kMaxReadCapacity ||
        (status && status->size < sizeof(*status))) {
        return 0;
    }
    return backend->history.read(after, out, capacity, status);
}

bool read_frame(ArkBattleFrameV1* out) {
    Backend* backend = active_backend.load();
    return backend && out && out->size >= sizeof(*out) && backend->current_frame(*out);
}

bool publish_test_event_v1(const ArkEventV1* event) {
    Backend* backend = active_backend.load();
    if (!backend || !backend->host.test_host || backend->stopping || !event ||
        event->size < sizeof(*event)) {
        return false;
    }
    backend->history.publish(*event);
    return true;
}

bool publish_test_event_v2(const ArkEventV2* event) {
    Backend* backend = active_backend.load();
    if (!backend || !backend->host.test_host || backend->stopping || !event ||
        event->size < sizeof(*event)) {
        return false;
    }
    backend->history.publish(*event);
    return true;
}

const ArkBattleEventsApiV1 kEventsApiV1{
    sizeof(ArkBattleEventsApiV1), ARK_BATTLE_ABI_VERSION, read_events_v1, read_frame};
const ArkBattleEventsApiV2 kEventsApiV2{
    sizeof(ArkBattleEventsApiV2), ARK_BATTLE_ABI_VERSION_V2, read_events_v2, read_frame};
const ArkBattleTestApiV1 kTestApiV1{
    sizeof(ArkBattleTestApiV1), ARK_BATTLE_ABI_VERSION, publish_test_event_v1};
const ArkBattleTestApiV2 kTestApiV2{
    sizeof(ArkBattleTestApiV2), ARK_BATTLE_ABI_VERSION_V2, publish_test_event_v2};

// --- Input adapter ---------------------------------------------------------------------------

void set_input_capture(void* user, bool mouse, bool keyboard) {
    static_cast<Backend*>(user)->input.set_capture(mouse, keyboard);
}

bool route_input_message(void* user, std::uint32_t message, std::uintptr_t wparam,
                         std::intptr_t lparam) {
    return static_cast<Backend*>(user)->input.route_message(message, wparam, lparam);
}

bool input_protected(void* user) {
    return static_cast<Backend*>(user)->input.protected_input();
}

// --- SDK callbacks ---------------------------------------------------------------------------

void on_frame(void* user, const ArkFrameV1*) {
    meter.update(*static_cast<Backend*>(user));
}

void on_draw(void*, const ArkFrameV1* frame) {
    meter.draw(*frame, active_backend.load());
}

// --- Lifecycle -------------------------------------------------------------------------------

bool host_is_compatible(const ArkPluginApiV1* host) {
    return host && host->size >= sizeof(ArkPluginApiV1) && host->abi_version == ARK_ABI_VERSION &&
           host->imgui_version == IMGUI_VERSION_NUM && host->queue_game_task &&
           host->pump_game_tasks && host->on_frame && host->on_draw && host->provide_service &&
           host->set_input_adapter && host->create_native_hook && host->enable_native_hook &&
           host->remove_native_hook && host->log && host->root_directory;
}

// Throws on the first failed registration.
void register_with_host(const ArkPluginApiV1& host, Backend& backend) {
    // The test host has no game window, so there is no native input to adapt.
    if (!host.test_host) {
        ArkInputAdapterV1 adapter;
        adapter.user = &backend;
        adapter.set_capture = set_input_capture;
        adapter.route_message = route_input_message;
        adapter.protected_input = input_protected;
        if (!host.set_input_adapter(host.owner, &adapter)) {
            throw std::runtime_error("Could not register damage meter input adapter");
        }
    }
    if (!host.provide_service(host.owner, ARK_BATTLE_SERVICE_NAME, ARK_BATTLE_ABI_VERSION,
                              &kEventsApiV1, sizeof(kEventsApiV1))) {
        throw std::runtime_error("Could not provide legacy battle event service");
    }
    if (!host.provide_service(host.owner, ARK_BATTLE_SERVICE_NAME_V2, ARK_BATTLE_ABI_VERSION_V2,
                              &kEventsApiV2, sizeof(kEventsApiV2))) {
        throw std::runtime_error("Could not provide battle event service v2");
    }
    if (host.test_host &&
        !host.provide_service(host.owner, ARK_BATTLE_TEST_SERVICE_NAME, ARK_BATTLE_ABI_VERSION,
                              &kTestApiV1, sizeof(kTestApiV1))) {
        throw std::runtime_error("Could not provide legacy battle test service");
    }
    if (host.test_host &&
        !host.provide_service(host.owner, ARK_BATTLE_TEST_SERVICE_NAME_V2,
                              ARK_BATTLE_ABI_VERSION_V2, &kTestApiV2,
                              sizeof(kTestApiV2))) {
        throw std::runtime_error("Could not provide battle test service v2");
    }
    if (!host.on_frame(host.owner, on_frame, &backend) ||
        !host.on_draw(host.owner, on_draw, nullptr)) {
        throw std::runtime_error("Could not register damage meter callbacks");
    }
}

bool initialize(const ArkPluginApiV1* host) {
    if (!host_is_compatible(host)) {
        return false;
    }
    std::lock_guard lock(meter.mutex());
    if (meter.started() || active_backend.load()) {
        return false;
    }
    meter.start(*host);
    try {
        // On failure the backend stays installed; shutdown() releases it.
        auto* backend = new Backend(*host);
        active_backend.store(backend);
        register_with_host(*host, *backend);
        return true;
    } catch (const std::exception& error) {
        host->log(host->owner, kLogError, error.what());
        return false;
    }
}

bool can_unload(char* reason, std::size_t capacity) {
    Backend* backend = active_backend.load();
    if (!backend || backend->input.can_unload()) {
        return true;
    }
    if (reason && capacity) {
        std::snprintf(reason, capacity, "%s", "请松开界面捕获的鼠标/按键，稍后再重载。");
    }
    return false;
}

// SDK frame/draw callbacks and game tasks are drained by the host before this runs. Returning
// false keeps the module mapped with all native state intact so the host can retry.
bool shutdown() {
    if (Backend* backend = active_backend.load()) {
        if (!backend->input.can_unload()) {
            return false;
        }
        backend->stopping = true;
        if (!backend->input.shutdown() || !backend->capture.shutdown()) {
            return false;
        }
        if (!backend->host.test_host &&
            !backend->host.set_input_adapter(backend->host.owner, nullptr)) {
            return false;
        }
        active_backend.store(nullptr);
        delete backend;
    }
    meter.stop();
    return true;
}

} // namespace
} // namespace ark::damage_meter

ARK_EXPORT const ArkPluginInfoV1* ark_plugin_query() {
    static const ArkPluginInfoV1 info{
        .size = sizeof(ArkPluginInfoV1),
        .abi_version = ARK_ABI_VERSION,
        .imgui_version = IMGUI_VERSION_NUM,
        .name = "Damage Meter",
        .version = "1.0.0",
        .author = "ArkFramework",
        .flags = ARK_PLUGIN_DEFAULT,
    };
    return &info;
}

ARK_EXPORT bool ark_plugin_initialize(const ArkPluginApiV1* host) {
    return ark::damage_meter::initialize(host);
}

ARK_EXPORT bool ark_plugin_can_unload(char* reason, size_t capacity) {
    return ark::damage_meter::can_unload(reason, capacity);
}

ARK_EXPORT bool ark_plugin_shutdown() {
    return ark::damage_meter::shutdown();
}
