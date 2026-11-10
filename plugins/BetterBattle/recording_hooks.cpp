#include "battle_helpers.h"

namespace ark::better_battle {

bool LocalBattle::on_record_player_spawn(void* self, std::uint32_t uid, int direction,
                                         void* tile, void* method) {
    LocalBattle* owner = recorder_owner.load(std::memory_order_acquire);
    if (!owner) {
        return false;
    }
    const bool recording =
        owner->record_active_.load(std::memory_order_acquire);
    const std::uint32_t frame =
        recording && owner->record_frame_get_
            ? owner->record_frame_get_(owner->record_frame_method_)
            : 0;
    std::string unit;
    if (owner->record_active_.load(std::memory_order_acquire) &&
        !owner->record_capture_suppressed_) {
        try {
            unit = owner->recorded_card_unit_id(self, uid);
        } catch (...) {
        }
    }
    PendingDeploy pending;
    pending.frame = frame;
    pending.uid = uid;
    pending.direction = direction;
    pending.tile = tile;
    if (owner->record_active_.load(std::memory_order_acquire) && tile) {
        try {
            owner->recorded_tile_position(self, tile, &pending.row, &pending.col);
        } catch (...) {
        }
    }
    bool queued = false;
    if (owner->record_active_.load(std::memory_order_acquire) &&
        !owner->record_capture_suppressed_ && uid && tile &&
        unit.empty()) {
        std::lock_guard lock(owner->record_mutex_);
        pending.serial = ++owner->recorded_pending_serial_;
        owner->recorded_pending_deploys_.push_back(pending);
        queued = true;
    }
    auto original = reinterpret_cast<RecordSpawnFn>(owner->record_spawn_original_);
    const bool accepted = original && original(self, uid, direction, tile, method);
    if (accepted && !unit.empty() && !owner->record_capture_suppressed_ &&
        owner->record_active_.load(std::memory_order_acquire)) {
        try {
            RecordedOperation operation;
            operation.kind = RecordedOperation::Kind::Deploy;
            operation.unit = std::move(unit);
            operation.direction = direction;
            operation.row = pending.row;
            operation.col = pending.col;
            owner->record_operation(operation, frame);
        } catch (...) {
        }
    }
    if (queued && !accepted) {
        std::lock_guard lock(owner->record_mutex_);
        const auto found = std::find_if(
            owner->recorded_pending_deploys_.begin(),
            owner->recorded_pending_deploys_.end(),
            [&](const PendingDeploy& candidate) {
                return candidate.serial == pending.serial;
            });
        if (found != owner->recorded_pending_deploys_.end()) {
            owner->recorded_pending_deploys_.erase(found);
        }
    }
    return accepted;
}

bool LocalBattle::on_record_player_skill(void* self, void* character, void* skill_id,
                                         void* method) {
    LocalBattle* owner = recorder_owner.load(std::memory_order_acquire);
    if (!owner) {
        return false;
    }
    const bool recording =
        owner->record_active_.load(std::memory_order_acquire);
    const std::uint32_t frame =
        recording && owner->record_frame_get_
            ? owner->record_frame_get_(owner->record_frame_method_)
            : 0;
    std::string unit;
    if (owner->record_active_.load(std::memory_order_acquire) &&
        !owner->record_capture_suppressed_) {
        try {
            unit = owner->recorded_character_id(character);
        } catch (...) {
        }
    }
    auto original = reinterpret_cast<RecordSkillFn>(owner->record_skill_original_);
    const bool accepted = original && original(self, character, skill_id, method);
    if (accepted && !unit.empty() && !owner->record_capture_suppressed_ &&
        owner->record_active_.load(std::memory_order_acquire)) {
        try {
            RecordedOperation operation;
            operation.kind = RecordedOperation::Kind::Skill;
            operation.unit = std::move(unit);
            owner->record_operation(operation, frame);
        } catch (...) {
        }
    }
    return accepted;
}

bool LocalBattle::on_record_player_withdraw(void* self, void* character, void* method) {
    LocalBattle* owner = recorder_owner.load(std::memory_order_acquire);
    if (!owner) {
        return false;
    }
    const bool recording =
        owner->record_active_.load(std::memory_order_acquire);
    const std::uint32_t frame =
        recording && owner->record_frame_get_
            ? owner->record_frame_get_(owner->record_frame_method_)
            : 0;
    std::string unit;
    if (owner->record_active_.load(std::memory_order_acquire) &&
        !owner->record_capture_suppressed_) {
        try {
            unit = owner->recorded_character_id(character);
        } catch (...) {
        }
    }
    auto original = reinterpret_cast<RecordWithdrawFn>(owner->record_withdraw_original_);
    const bool accepted = original && original(self, character, method);
    if (accepted && !unit.empty() && !owner->record_capture_suppressed_ &&
        owner->record_active_.load(std::memory_order_acquire)) {
        try {
            RecordedOperation operation;
            operation.kind = RecordedOperation::Kind::Withdraw;
            operation.unit = std::move(unit);
            owner->record_operation(operation, frame);
        } catch (...) {
        }
    }
    return accepted;
}

void LocalBattle::on_record_character_located(void* self, void* character, void* method) {
    LocalBattle* owner = recorder_owner.load(std::memory_order_acquire);
    if (!owner) {
        return;
    }
    auto original = reinterpret_cast<RecordLocateFn>(owner->record_located_original_);
    if (original) {
        original(self, character, method);
    }
    if (!owner->record_active_.load(std::memory_order_acquire)) {
        return;
    }
    const std::uint32_t frame =
        owner->record_frame_get_ ? owner->record_frame_get_(owner->record_frame_method_) : 0;
    try {
        owner->record_character_located(self, character, frame);
    } catch (...) {
    }
}

} // namespace ark::better_battle
