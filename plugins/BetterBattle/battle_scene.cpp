#include "battle_helpers.h"

namespace ark::better_battle {

bool LocalBattle::adopt_scene(void* controller) {
    if (!controller) {
        return false;
    }
    if (scene_.controller == controller) {
        return true;
    }
    auto root = std::make_unique<il2cpp::Runtime::Root>(runtime_, controller);
    const bool replacing = scene_.controller != nullptr;
    const bool preserve_stage = scene_.stage_pending;
    scene_.controller = controller;
    scene_.controller_root = std::move(root);
    ++scene_.revision;
    scene_.player_side = 0;
    scene_.side_ready = false;
    if (!preserve_stage) {
        scene_.stage = StageInfo{};
        scene_.stage_request.clear();
    } else {
        scene_.stage_pending = false;
    }
    scene_.launched_by_us = local_battle_active_.load(std::memory_order_acquire);
    scene_.rogue = false;
    scene_.rogue_checked = false;
    rogue_auto_capture_revision_ =
        std::numeric_limits<std::uint64_t>::max();
    card_scene_revision_ = 0;
    card_objects_ready_ = false;
    card_roots_.clear();
    enemy_scene_revision_ = std::numeric_limits<std::uint64_t>::max();
    enemy_serials_.clear();
    enemy_identity_ids_.clear();
    next_enemy_serial_ = 1;
    enemy_wave_plan_source_ = nullptr;
    enemy_wave_plan_cached_ = false;
    enemy_wave_plan_total_ = 0;
    enemy_wave_plan_cache_.clear();
    spine_scene_revision_ = std::numeric_limits<std::uint64_t>::max();
    spine_scan_tick_ = 0;
    spine_manual_done_.clear();
    focused_animator_ = nullptr;
    focused_enemy_root_.reset();
    focused_animator_root_.reset();
    focused_outline_manager_root_.reset();
    focused_outline_manager_ = nullptr;
    focused_outline_ = false;
    focused_enemy_uid_ = 0;
    focused_enemy_until_ = 0;
    panel_target_uid_ = 0;
    focused_renderers_.clear();
    focused_materials_.clear();
    reset_world_sampling();
    if (replacing) {
        log(kLogInfo, "战斗场景切换 revision=" + std::to_string(scene_.revision));
    }
    return true;
}

void LocalBattle::clear_scene() {
    last_refresh_frame_valid_ = false;
    last_refresh_frame_ = 0;
    perf_last_frame_valid_ = false;
    perf_last_frame_ = 0;
    next_refresh_tick_ = 0;
    next_status_tick_ = 0;
    rogue_capture_queued_ = false;
    if (scene_.controller || scene_.controller_root) {
        ++scene_.revision;
    }
    scene_.controller = nullptr;
    scene_.controller_root.reset();
    if (!scene_.stage_pending) {
        scene_.stage = StageInfo{};
        scene_.stage_request.clear();
    }
    scene_.player_side = 0;
    scene_.side_ready = false;
    scene_.launched_by_us = false;
    scene_.rogue = false;
    scene_.rogue_checked = false;
    rogue_auto_capture_revision_ =
        std::numeric_limits<std::uint64_t>::max();
    card_scene_revision_ = 0;
    card_objects_ready_ = false;
    card_roots_.clear();
    spine_scene_revision_ = std::numeric_limits<std::uint64_t>::max();
    spine_scan_tick_ = 0;
    spine_manual_done_.clear();
    focused_animator_ = nullptr;
    focused_enemy_root_.reset();
    focused_animator_root_.reset();
    focused_outline_manager_root_.reset();
    focused_outline_manager_ = nullptr;
    focused_outline_ = false;
    focused_enemy_uid_ = 0;
    focused_enemy_until_ = 0;
    panel_target_uid_ = 0;
    focused_renderers_.clear();
    focused_materials_.clear();
    enemy_serials_.clear();
    enemy_identity_ids_.clear();
    next_enemy_serial_ = 1;
    enemy_scene_revision_ = std::numeric_limits<std::uint64_t>::max();
    enemy_wave_plan_source_ = nullptr;
    enemy_wave_plan_cached_ = false;
    enemy_wave_plan_total_ = 0;
    enemy_wave_plan_cache_.clear();
    reset_world_sampling();
}

std::uint32_t LocalBattle::scene_frame() {
    if (!scene_.controller) {
        return 0;
    }
    return method_int(runtime_, scene_.controller, classes_.battle_controller,
                      "get_fixedFrameCnt", 0);
}

} // namespace ark::better_battle
