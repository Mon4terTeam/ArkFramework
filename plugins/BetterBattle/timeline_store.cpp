#include "battle_helpers.h"

namespace ark::better_battle {

void LocalBattle::note_timeline_frame(std::uint32_t frame) {
    std::lock_guard lock(record_mutex_);
    if (timeline_waiting_for_controller_) {
        return;
    }
    auto current = timeline_nodes_.find(timeline_current_id_);
    if (current == timeline_nodes_.end() ||
        frame <= current->second.end_frame) {
        return;
    }
    for (const auto& [id, child] : timeline_nodes_) {
        (void)id;
        if (child.parent_id == current->second.id) {
            return;
        }
    }
    current->second.end_frame = frame;
    ++timeline_revision_;
    timeline_dirty_.store(true, std::memory_order_release);
}

std::string LocalBattle::timeline_node_for_frame_locked(
    std::uint32_t frame) const {
    if (timeline_nodes_.empty() || timeline_current_id_.empty()) {
        return {};
    }
    const std::vector<std::string> path =
        active_path_for(timeline_nodes_, timeline_current_id_);
    std::string containing;
    for (const std::string& id : path) {
        const auto node = timeline_nodes_.find(id);
        if (node == timeline_nodes_.end()) {
            continue;
        }
        const std::uint32_t start =
            node->second.parent_id.empty() ? 0 : node->second.fork_frame + 1;
        if (frame >= start) {
            containing = id;
        } else {
            break;
        }
    }
    if (containing.empty() && !path.empty()) {
        containing = path.front();
    }
    return containing;
}

void LocalBattle::record_timeline_event_locked(std::uint32_t frame,
                                               const std::string& actor,
                                               const std::string& target,
                                               std::uint8_t type,
                                               double actual,
                                               std::uint32_t actor_uid,
                                               std::uint32_t target_uid) {
    const std::string node_id = timeline_node_for_frame_locked(frame);
    auto node = timeline_nodes_.find(node_id);
    if (node == timeline_nodes_.end()) {
        return;
    }
    auto& values = node->second.observed_events[frame];
    ReplayEvent event;
    event.frame = frame;
    event.actor = actor;
    event.target = target;
    event.actor_uid = actor_uid;
    event.target_uid = target_uid;
    event.type = type;
    event.actual = actual;
    values.push_back(std::move(event));
    if (values.size() > 2048) {
        values.erase(values.begin(),
                     values.begin() + static_cast<std::ptrdiff_t>(values.size() - 2048));
    }
    ++timeline_revision_;
    timeline_dirty_.store(true, std::memory_order_release);
}

void LocalBattle::record_timeline_world_event_locked(
    std::uint32_t frame, std::uint8_t kind, int value, int total) {
    const std::string node_id = timeline_node_for_frame_locked(frame);
    auto node = timeline_nodes_.find(node_id);
    if (node == timeline_nodes_.end()) {
        return;
    }
    auto& values = node->second.observed_world_events[frame];
    ReplayWorldEvent event;
    event.frame = frame;
    event.kind = kind;
    event.value = value;
    event.total = total;
    values.push_back(std::move(event));
    if (values.size() > 2048) {
        values.erase(
            values.begin(),
            values.begin() + static_cast<std::ptrdiff_t>(values.size() - 2048));
    }
    ++timeline_revision_;
    timeline_dirty_.store(true, std::memory_order_release);
}

} // namespace ark::better_battle
