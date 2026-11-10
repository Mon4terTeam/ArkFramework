#include "battle_helpers.h"

namespace ark::better_battle {

bool LocalBattle::install_replay_hook() {
    if (replay_hook_) {
        return true;
    }
    void* controller_class = classes_.battle_controller;
    replay_frame_method_ =
        find_method_optional(runtime_, controller_class, "get_fixedFrameCnt", 0);
    void* fixed_update =
        find_method_optional(runtime_, controller_class, "FixedUpdate", 0);
    if (!replay_frame_method_ || !fixed_update) {
        throw std::runtime_error("回放所需的 FixedUpdate/get_fixedFrameCnt 缺失");
    }
    const auto entry = [](void* method) {
        return *static_cast<void**>(method);
    };
    if (!il2cpp::is_executable(entry(replay_frame_method_)) ||
        !il2cpp::is_executable(entry(fixed_update))) {
        throw std::runtime_error("回放方法没有可执行入口");
    }
    replay_frame_get_ = reinterpret_cast<StaticFrameGetter>(entry(replay_frame_method_));
    const std::uint64_t hook = host_.create_native_hook(
        host_.owner, entry(fixed_update),
        reinterpret_cast<void*>(&LocalBattle::on_replay_fixed_update),
        &replay_fixed_original_);
    if (!hook || !replay_fixed_original_) {
        throw std::runtime_error("创建回放 FixedUpdate Hook 失败");
    }
    replay_hook_ = hook;
    replay_owner.store(this, std::memory_order_release);
    if (!host_.enable_native_hook(host_.owner, replay_hook_, true)) {
        remove_replay_hook();
        throw std::runtime_error("启用回放 FixedUpdate Hook 失败");
    }
    return true;
}

bool LocalBattle::remove_replay_hook() {
    replay_active_.store(false, std::memory_order_release);
    replay_owner.store(nullptr, std::memory_order_release);
    if (!replay_hook_) {
        replay_fixed_original_ = nullptr;
        return true;
    }
    const std::uint64_t hook = replay_hook_;
    host_.enable_native_hook(host_.owner, hook, false);
    if (!host_.remove_native_hook(host_.owner, hook)) {
        return false;
    }
    replay_hook_ = 0;
    replay_fixed_original_ = nullptr;
    return true;
}

void LocalBattle::stop_replay() {
    cancel_timeline_fast_forward();
    const bool was_active = replay_active_.exchange(false, std::memory_order_acq_rel);
    replay_branch_armed_.store(false, std::memory_order_release);
    const std::uint32_t dispatched = replay_dispatched_;
    replay_stop_frame_.store(0, std::memory_order_release);
    replay_stop_enabled_.store(false, std::memory_order_release);
    pending_replay_seek_fast_.store(false, std::memory_order_release);
    pending_replay_seek_target_.store(0, std::memory_order_release);
    pending_timeline_jump_ = false;
    pending_timeline_fast_ = false;
    pending_timeline_parent_id_.clear();
    remove_replay_hook();
    if (was_active) {
        publish_result("录像回放已停止，已执行 " + std::to_string(dispatched) + " 个操作");
    } else {
        publish_result("录像回放未运行");
    }
}

} // namespace ark::better_battle
