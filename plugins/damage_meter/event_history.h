#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>

#include <ark/battle.h>

namespace ark::damage_meter {

// Bounded, thread-safe log of the current battle's events. Producers publish from the game
// thread; consumers read from any thread by sequence number.
class EventHistory {
public:
    static constexpr std::size_t kDefaultCapacity = 65536;

    explicit EventHistory(std::size_t capacity = kDefaultCapacity) : capacity_(capacity) {}

    // Legacy ABI-1 publishers have no frame field. Their events enter the shared
    // history at frame 0 and are projected back without the ABI 2 fields.
    void publish(const ArkEventV1& legacy) {
        ArkEventV2 event;
        event.kind = legacy.kind;
        event.sequence = legacy.sequence;
        event.battle = legacy.battle;
        event.game_time = legacy.game_time;
        event.calculated_damage = legacy.calculated_damage;
        event.actual_damage = legacy.actual_damage;
        event.damage_type = legacy.damage_type;
        event.partial = legacy.partial;
        std::memcpy(event.actor_id, legacy.actor_id, sizeof(event.actor_id));
        std::memcpy(event.stage_id, legacy.stage_id, sizeof(event.stage_id));
        publish(event);
    }

    // Assigns the sequence and battle numbers. A battle start discards the previous battle.
    void publish(ArkEventV2 event) {
        std::lock_guard lock(mutex_);
        if (event.kind == ARK_BATTLE_START) {
            events_.clear();
            ++battle_;
            partial_ = event.partial != 0;
            ended_ = false;
            frame_valid_ = false;
            frame_ = event.frame;
            frame_event_source_ = event.frame;
            frame_event_index_ = 0;
        }
        if (event.kind == ARK_BATTLE_END) {
            ended_ = true;
        }
        event.sequence = ++sequence_;
        event.battle = battle_;
        if (event.frame != frame_event_source_) {
            frame_event_source_ = event.frame;
            frame_event_index_ = 0;
        }
        event.frame_event_index = frame_event_index_++;
        events_.push_back(event);
        while (events_.size() > capacity_) {
            events_.pop_front();
            partial_ = true;
        }
    }

    // Copies up to `capacity` events newer than `after`. `status->overrun` reports that events
    // between `after` and the oldest retained event were dropped.
    // Legacy ABI-1 read. The caller's buffer is an array of ArkEventV1, so ABI 2
    // fields are dropped instead of being reinterpreted.
    std::size_t read_v1(std::uint64_t after, ArkEventV1* out, std::size_t capacity,
                        ArkReadStatusV1* status) {
        std::lock_guard lock(mutex_);
        fill_status_locked(after, status);
        std::size_t written = 0;
        for (const ArkEventV2& source : events_) {
            if (source.sequence <= after || written >= capacity) {
                continue;
            }
            ArkEventV1& event = out[written++];
            event = {};
            event.size = sizeof(ArkEventV1);
            event.kind = source.kind;
            event.sequence = source.sequence;
            event.battle = source.battle;
            event.game_time = source.game_time;
            event.calculated_damage = source.calculated_damage;
            event.actual_damage = source.actual_damage;
            event.damage_type = source.damage_type;
            event.partial = source.partial;
            std::memcpy(event.actor_id, source.actor_id, sizeof(event.actor_id));
            std::memcpy(event.stage_id, source.stage_id, sizeof(event.stage_id));
        }
        return written;
    }

    std::size_t read(std::uint64_t after, ArkEventV2* out, std::size_t capacity,
                     ArkReadStatusV1* status) {
        std::lock_guard lock(mutex_);
        fill_status_locked(after, status);
        std::size_t written = 0;
        for (const ArkEventV2& event : events_) {
            if (event.sequence > after && written < capacity) {
                out[written++] = event;
            }
        }
        return written;
    }

    // Caches the native logic frame of the running battle; ignored outside a battle.
    void set_frame(std::uint32_t frame) {
        std::lock_guard lock(mutex_);
        if (!battle_ || ended_) {
            return;
        }
        frame_ = frame;
        frame_valid_ = true;
    }

    bool frame(ArkBattleFrameV1& out) {
        std::lock_guard lock(mutex_);
        if (!frame_valid_) {
            return false;
        }
        out = {};
        out.battle = battle_;
        out.frame = frame_;
        return true;
    }

private:
    void fill_status_locked(std::uint64_t after, ArkReadStatusV1* status) const {
        if (!status) {
            return;
        }
        *status = {};
        status->partial = partial_;
        status->ended = ended_;
        if (!events_.empty()) {
            const ArkEventV2& oldest = events_.front();
            status->oldest_sequence = oldest.sequence;
            status->newest_sequence = events_.back().sequence;
            status->overrun =
                after && after + 1 < oldest.sequence && oldest.kind != ARK_BATTLE_START;
        }
    }

    std::mutex mutex_;
    std::deque<ArkEventV2> events_;
    std::size_t capacity_;
    std::uint64_t sequence_ = 0;
    std::uint64_t battle_ = 0;
    std::uint32_t frame_event_source_ = 0;
    std::uint32_t frame_event_index_ = 0;
    bool partial_ = false;
    bool ended_ = false;
    std::uint32_t frame_ = 0;
    bool frame_valid_ = false;
};

} // namespace ark::damage_meter
