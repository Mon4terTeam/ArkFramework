#include "battle_helpers.h"

namespace ark::better_battle {

void LocalBattle::queue_timeline_start(PendingTimelineMode mode,
                                       BattleStart start,
                                       const std::string& stage_id, int seed,
                                       const std::string& source_file,
                                       bool tas_allowed) {
    pending_timeline_mode_ = mode;
    pending_timeline_stage_id_ = stage_id;
    pending_timeline_squad_ = start.squad;
    pending_timeline_start_ = std::move(start);
    pending_timeline_rogue_ = pending_timeline_start_.rogue;
    pending_timeline_tas_allowed_ = tas_allowed;
    pending_timeline_seed_to_apply_ = seed;
    pending_timeline_source_file_ = source_file;
    pending_timeline_jump_ = false;
    pending_timeline_fast_ = false;
    pending_timeline_parent_id_.clear();
    pending_timeline_fork_frame_ = 0;
}

void LocalBattle::start_recording() {
    try {
        std::string stage;
        {
            std::lock_guard lock(mutex_);
            stage = scene_.stage.id;
            if (stage.empty()) {
                stage = snapshot_.stage_input;
            }
        }
        if (stage.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        BattleStart start;
        start.stage = stage;
        const int seed = tas_mode_.load(std::memory_order_acquire)
                             ? tas_seed_.load(std::memory_order_acquire)
                             : -1;
        queue_timeline_start(PendingTimelineMode::NewRoot, std::move(start),
                             stage, seed, {});
        begin_timeline_session(false);
        publish_result("时间轴录制已开始，根节点已建立");
    } catch (const std::exception& error) {
        publish_result(std::string("开始时间轴录制失败：") + error.what(), false);
    }
}

void LocalBattle::activate_pending_timeline() {
    const PendingTimelineMode mode = pending_timeline_mode_;
    pending_timeline_mode_ = PendingTimelineMode::None;
    if (mode == PendingTimelineMode::NewRoot) {
        begin_timeline_session(false);
    } else if (mode == PendingTimelineMode::ImportedReplay) {
        begin_timeline_session(true);
    } else if (mode == PendingTimelineMode::Jump) {
        record_active_.store(false, std::memory_order_release);
    }
}

void LocalBattle::stop_recording() {
    record_active_.store(false, std::memory_order_release);
    write_recording();
    publish_timeline();
    const auto [operations, frames] = active_operation_stats();
    publish_result("时间轴录制已暂停，记录 " +
                   std::to_string(operations) + " 个操作，覆盖 " +
                   std::to_string(frames) + " 帧");
}

} // namespace ark::better_battle
