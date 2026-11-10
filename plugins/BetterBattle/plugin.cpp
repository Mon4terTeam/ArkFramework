#include "BetterBattle.h"
#include "battle_helpers.h"

#include <windows.h>

#include <ark/external_bridge.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "../common/il2cpp_exports.h"

namespace ark::better_battle {
namespace {

constexpr std::uint64_t kReconnectDelayMs = 2000;

std::atomic<LocalBattle*> active;

struct TaskPacket {
    LocalBattle* owner = nullptr;
    Command command;
};

void on_frame(void* user, const ArkFrameV1*) {
    static_cast<LocalBattle*>(user)->frame();
}

} // namespace

bool host_is_compatible(const ArkPluginApiV1* host) {
    return host && host->size >= sizeof(ArkPluginApiV1) && host->abi_version == ARK_ABI_VERSION &&
           host->imgui_version == IMGUI_VERSION_NUM && host->queue_game_task &&
           host->pump_game_tasks && host->on_frame && host->on_draw && host->log &&
           host->root_directory && host->queue_game_task && host->require_service &&
           host->create_native_hook && host->enable_native_hook && host->remove_native_hook;
}

LocalBattle::LocalBattle(const ArkPluginApiV1& host)
    : host_(host), battle_capture_(runtime_, host) {}

LocalBattle::~LocalBattle() {
    close_trace();
    close_probe();
}

void LocalBattle::set_bridge(const ArkExternalBridgeApiV1* bridge) {
    bridge_ = bridge;
}

void LocalBattle::log(std::uint32_t level, const std::string& message) const {
    host_.log(host_.owner, level, message.c_str());
}

void LocalBattle::frame() {
    if (stopping_.load()) {
        return;
    }
    connect_if_ready();
    if (pump_ready_ && !preprocess_global_buff_hook_) {
        const std::uint64_t now = GetTickCount64();
        if (now >= next_preprocess_retry_tick_) {
            next_preprocess_retry_tick_ = now + 5000;
            install_preprocess_global_buff_hook();
        }
    }
    process_request_file();

    const std::uint64_t now = GetTickCount64();
    if (now >= next_record_flush_tick_) {
        next_record_flush_tick_ = now + 500;
        flush_recording();
    }
    if (pump_ready_ && now >= next_catalog_tick_) {
        std::lock_guard lock(mutex_);
        const bool already_queued =
            std::any_of(pending_.begin(), pending_.end(), [](const Command& command) {
                return command.name == "catalog";
            });
        if (operators_.empty() && !already_queued) {
            next_catalog_tick_ = now + 5000;
            pending_.push_back(Command{"catalog", {}});
        }
    }
    if (host_.test_host) {
        host_.pump_game_tasks(host_.owner);
    }
    schedule_next_work();
}

void LocalBattle::process_request_file() {
    if (!pump_ready_ || host_.test_host) {
        return;
    }
    const std::uint64_t now = GetTickCount64();
    if (now < next_request_poll_tick_) {
        return;
    }
    next_request_poll_tick_ = now + 50;
    const std::filesystem::path request_path =
        std::filesystem::path(host_.root_directory) / "BetterBattle.request";
    std::ifstream input(request_path);
    if (!input) {
        return;
    }
    std::string line;
    std::getline(input, line);
    input.close();
    std::error_code ignored;
    std::filesystem::remove(request_path, ignored);
    if (line.empty()) {
        return;
    }

    while (!line.empty() &&
           std::isspace(static_cast<unsigned char>(line.back()))) {
        line.pop_back();
    }
    Command command;
    const std::size_t separator = line.find_first_of(" \t");
    command.name = line.substr(0, separator);
    if (separator != std::string::npos) {
        const std::size_t first = line.find_first_not_of(" \t", separator);
        if (first != std::string::npos) {
            command.args = line.substr(first);
        }
    }
    std::lock_guard lock(mutex_);
    pending_.push_back(std::move(command));

}

void LocalBattle::write_status_file(bool publish_web) {
    if (!host_.root_directory) {
        return;
    }
    const std::uint64_t status_started = perf_now_us();
    std::string result;
    bool ok = false;
    BattleSnapshot snap;
    {
        const std::uint64_t copy_started = perf_now_us();
        std::lock_guard lock(mutex_);
        result = last_result_;
        ok = last_ok_;
        snap = snapshot_;
        perf_.snapshot_copy.add(perf_now_us() - copy_started);
    }
    std::ofstream output(std::filesystem::path(host_.root_directory) / "BetterBattle.status");
    output << (ok ? "ok=1\n" : "ok=0\n") << result << '\n';
    output << "battle=" << (snap.available ? "1" : "0") << " playing=" << snap.playing
           << " paused=" << snap.paused << " state=" << snap.state << " cost=" << snap.cost
           << " frame=" << snap.frame << " stage=" << snap.stage_id << '\n';
    output << "trace active=" << (trace_active_ ? 1 : 0)
           << " changes=" << trace_changes_
           << " dropped=" << trace_dropped_lines_
           << " frame=" << trace_last_frame_ << '\n';
    const auto [record_operations, record_frames] = active_operation_stats();
    output << "record active=" << (record_active_.load() ? 1 : 0)
           << " operations=" << record_operations
           << " frames=" << record_frames
           << " file=battle_operations.json\n";
    {
        std::lock_guard lock(record_mutex_);
        output << "timeline active=" << (!timeline_nodes_.empty() ? 1 : 0)
               << " nodes=" << timeline_nodes_.size()
               << " current=" << timeline_current_id_
               << " root=" << timeline_root_id_
               << " source=" << timeline_source_
               << " revision=" << timeline_revision_
               << " jumping=" << (pending_timeline_jump_ ? 1 : 0)
               << " file=battle_timeline.json\n";
        output << "playback segment="
               << (playback_path_.empty()
                       ? "-"
                       : playback_path_[std::min(
                             playback_segment_, playback_path_.size() - 1)])
               << " index=" << playback_segment_
               << " path=";
        for (std::size_t index = 0; index < playback_path_.size(); ++index) {
            output << (index ? ">" : "") << playback_path_[index];
        }
        if (playback_path_.empty()) {
            output << '-';
        }
        output << '\n';
        output << "world sampling=" << (world_sampling_valid_ ? 1 : 0)
               << " wave=" << world_sampling_wave_ << '/'
               << world_sampling_total_waves_
               << " cost=" << world_sampling_cost_
               << " enemies=" << world_sampling_enemies_
               << " start=" << world_sampling_wave_start_ << '\n';
    }
    output << "replay active=" << (replay_active_.load() ? 1 : 0)
           << " operations=" << replay_dispatched_
           << " next=" << replay_next_step_
           << " total=" << replay_steps_.size()
           << " frame=" << replay_frame_
           << " file=" << replay_file_ << '\n';
    output << "stage_input=" << snap.stage_input << " stage_code=" << snap.stage_code
           << " stage_name=" << snap.stage_name << '\n';
    output << "damage available=" << (snap.damage.available ? 1 : 0)
           << " started=" << (snap.damage.started ? 1 : 0)
           << " ended=" << (snap.damage.ended ? 1 : 0)
           << " partial=" << (snap.damage.partial ? 1 : 0)
           << " overrun=" << (snap.damage.overrun ? 1 : 0)
           << " battle=" << snap.damage.battle
           << " sequence=" << snap.damage.sequence
           << " events=" << snap.damage.event_count
           << " revision=" << snap.damage.revision
           << " series_revision=" << snap.damage.series_revision
           << " series_points=" << snap.damage.series_points
           << " frames=" << snap.damage.start_frame << ".."
           << snap.damage.end_frame
           << " duration_frames=" << snap.damage.duration_frames
           << " duration=" << snap.damage.duration
           << " actual=" << snap.damage.total_actual
           << " calculated=" << snap.damage.total_calculated
           << " healing=" << snap.damage.total_healing
           << " regen=" << snap.damage.total_regen
           << " taken=" << snap.damage.total_taken << '\n';
    for (const DamageActorInfo& actor : snap.damage.actors) {
        output << "damage-actor " << actor.id << " actual=" << actor.actual
               << " calculated=" << actor.calculated
               << " healing=" << actor.healing
               << " regen=" << actor.regen
               << " taken=" << actor.taken;
        for (std::size_t type = 0; type < kDamageTypeCount; ++type) {
            output << ' ' << kDamageTypeKeys[type] << '='
                   << actor.types[type].actual;
        }
        output << " curve_points=" << actor.curve_points << '\n';
        if (actor.hp_tracked) {
            const double hp_delta = actor.hp_current - actor.hp_start;
            const double hp_effective_delta =
                actor.hp_effective_current - actor.hp_effective_start;
            const double event_delta =
                actor.healing_received + actor.regen - actor.taken;
            output << "ledger " << actor.id << " hp_start=" << actor.hp_start
                   << " hp_current=" << actor.hp_current
                   << " minus_hp=" << actor.minus_hp
                   << " max_minus_hp=" << actor.max_minus_hp
                   << " hp_delta=" << hp_delta
                   << " hp_effective_delta=" << hp_effective_delta
                   << " event_delta=" << event_delta
                   << " hp_event_delta=" << actor.hp_event_delta
                   << " hp_events=" << actor.hp_delta_events
                   << " hp_fallbacks=" << actor.hp_delta_fallbacks
                   << " balance=" << (event_delta - hp_effective_delta)
                   << '\n';
        }
    }
    if (!snap.error.empty()) {
        output << "error=" << snap.error << '\n';
    }
    for (const CardInfo& card : snap.cards) {
        output << "card " << card.id << ' ' << card.uid << " state=" << card.state
               << " ready=" << card.ready << " cost=" << card.cost
               << " remaining=" << card.remaining
               << " respawn=" << card.respawn_remaining
               << " dontOccupy=" << (card.dont_occupy_deploy_cnt ? 1 : 0)
               << " level=" << card.level
               << " evolve=" << card.evolve << " potential=" << card.potential
               << " skill=" << (card.skill_index + 1) << '\n';
    }
    for (const UnitInfo& unit : snap.units) {
        output << "unit " << unit.id << ' ' << unit.uid << " card=" << unit.card_uid
               << " owner=" << unit.owner_uid
               << " hp=" << unit.hp << '/' << unit.max_hp
               << " shield=" << unit.shield
               << " shield_layers=" << unit.shield_layers
               << " atk=" << unit.atk << " def=" << unit.def
               << " res=" << unit.magic_resistance
               << " aspd=" << unit.attack_speed << " cost=" << unit.unit_cost
               << " alive=" << unit.alive
               << " sp=" << unit.sp << '/' << unit.max_sp
               << " sp_enough=" << unit.sp_enough
               << " op_triggerable=" << unit.skill_op_triggerable
               << " skill=" << unit.has_skill << " in_skill=" << unit.in_skill
               << " affect=" << unit.skill_affecting
               << " begin_anim=" << unit.skill_begin_anim
               << " remaining=" << unit.skill_remaining
               << " trigger_cnt=" << unit.skill_trigger_count
               << " ammo=" << unit.skill_ammo_remaining << '/' << unit.skill_ammo_max
               << " overload_skill=" << unit.skill_overload_skill
               << " overloaded=" << unit.skill_overloaded
               << " retriggerable=" << unit.skill_retriggerable
               << " modules=";
        for (std::size_t module_index = 0; module_index < unit.modules.size();
             ++module_index) {
            if (module_index) {
                output << ',';
            }
            output << unit.modules[module_index].id << ':'
                   << unit.modules[module_index].level;
        }
        output
               << " state_id=" << unit.current_state_id
               << " activatable=" << unit.skill_activatable
               << " withdrawable=" << unit.withdrawable
               << " manual=" << unit.manually_withdrawable
               << " buffs=" << unit.buffs.size() << '\n';
    }
    output << "enemy-summary count=" << snap.enemies.size()
           << " waves=" << snap.enemy_wave_current << '/'
           << snap.enemy_wave_total
           << " focus=" << snap.enemy_focus_uid
           << " profiles=" << snap.enemy_profiles.size() << '\n';
    for (const EnemyProfile& profile : snap.enemy_profiles) {
        output << "enemy-profile " << profile.id << " name=" << profile.name
               << " level=" << profile.level_type
               << " stats=" << profile.max_hp << '/' << profile.atk << '/'
               << profile.def << '/' << profile.magic_resistance
               << " tags=" << profile.tags.size()
               << " abilities=" << profile.abilities.size()
               << " desc=" << profile.description.size()
               << " source=" << profile.source << '\n';
    }
    for (const EnemyInfo& enemy : snap.enemies) {
        output << "enemy #" << enemy.serial << ' ' << enemy.id << ' '
               << enemy.uid << " hp=" << enemy.hp << '/' << enemy.max_hp
               << " atk=" << enemy.atk << " def=" << enemy.def
               << " res=" << enemy.magic_resistance
               << " aspd=" << enemy.attack_speed
               << " move=" << enemy.move_speed
               << " alive=" << enemy.alive
               << " boss=" << enemy.boss
               << " giant_boss=" << enemy.giant_boss
               << " block=" << enemy.block_count
               << " pos=" << enemy.row << ',' << enemy.col
               << " skills=" << enemy.skills.size()
               << " buffs=" << enemy.buffs.size() << '\n';
        for (const EnemySkillInfo& skill : enemy.skills) {
            output << "enemy-skill #" << enemy.serial << ' ' << skill.key
                   << " enabled=" << skill.enabled
                   << " used=" << skill.used_up
                   << " trigger=" << skill.trigger_count << '/'
                   << skill.max_trigger_count
                   << " cd=" << skill.cooldown << '/'
                   << skill.cooldown_total
                   << " casting=" << skill.casting
                   << " affecting=" << skill.affecting << '\n';
        }
    }
    for (const EnemyWave& wave : snap.enemy_waves) {
        output << "enemy-wave " << wave.index << " total=" << wave.total
               << " pre=" << wave.pre_delay << '\n';
        for (const EnemyWaveAction& action : wave.actions) {
            output << "enemy-wave-action " << wave.index << ' '
                   << action.key << " count=" << action.count
                   << " route=" << action.route
                   << " pre=" << action.pre_delay
                   << " interval=" << action.interval
                   << " first=" << action.first_time << '\n';
        }
    }
    const auto write_perf = [&output](const char* name,
                                      const LocalBattle::PerfCounter& counter) {
        const auto percentiles = counter.percentiles_us();
        output << "perf " << name << " n=" << counter.count
               << " last_us=" << counter.last_us
               << " avg_us=" << counter.average_us()
               << " p50_us=" << percentiles.p50
               << " p95_us=" << percentiles.p95
               << " p99_us=" << percentiles.p99
               << " max_us=" << counter.max_us << '\n';
    };
    write_perf("refresh", perf_.refresh);
    write_perf("discover", perf_.discover);
    write_perf("cards", perf_.cards);
    write_perf("units", perf_.units);
    write_perf("buffs", perf_.buffs);
    write_perf("damage", perf_.damage);
    write_perf("copy", perf_.snapshot_copy);
    write_perf("status", perf_.status);
    write_perf("web_json", perf_.web_json);
    write_perf("web_publish", perf_.web_publish);
    write_perf("timeline_json", perf_.timeline_json);
    write_perf("record_write", perf_.record_write);
    write_perf("spine_auto", perf_.spine_auto);
    write_perf("frame_tick", perf_.frame_tick);
    write_perf("fixed_tick", perf_.fixed_tick);
    output << "perf cache card=" << perf_.card_hits << '/' << perf_.card_misses
           << " unit=" << perf_.unit_hits << '/' << perf_.unit_misses
           << " talent=" << perf_.talent_hits << '/' << perf_.talent_misses
           << " buff=" << perf_.buff_hits << '/' << perf_.buff_misses
           << " buff_live=" << buff_static_cache_.size()
           << " web=each_frame"
           << " interval_ms="
           << refresh_interval_ms_.load(std::memory_order_relaxed)
           << " frame_interval="
           << refresh_frame_interval_.load(std::memory_order_relaxed) << '\n';
    output << "perf web_payload last_bytes=" << perf_.web_bytes_last
           << " total_bytes=" << perf_.web_bytes_total
           << " identical_skipped=" << perf_.web_publish_skipped
           << " timeline_last_bytes=" << perf_.timeline_bytes_last
           << " timeline_skipped=" << perf_.timeline_publish_skipped << '\n';
    output << "perf sample frame_delta_last=" << perf_.last_frame_delta
           << " frame_delta_avg="
           << (perf_.frame_delta_samples
                   ? static_cast<double>(perf_.frame_delta_total) /
                         static_cast<double>(perf_.frame_delta_samples)
                   : 0.0)
           << " samples=" << perf_.frame_delta_samples << '\n';
    const std::uint64_t status_body_finished = perf_now_us();
    perf_.status.add(status_body_finished - status_started);
    if (publish_web) {
        publish_web_snapshot(snap);
    }
    if (profiles_dirty_) {
        publish_profiles();
    }
}

void LocalBattle::export_web(const std::string& prefix_text) {
    BattleSnapshot snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = snapshot_;
    }
    snapshot.damage = damage_snapshot(true);

    std::filesystem::path prefix =
        prefix_text.empty() ? std::filesystem::path("battle_web")
                            : std::filesystem::path(prefix_text);
    if (prefix.is_relative()) {
        prefix = std::filesystem::path(host_.root_directory) / prefix;
    }
    std::error_code error;
    if (!prefix.parent_path().empty()) {
        std::filesystem::create_directories(prefix.parent_path(), error);
    }
    if (error) {
        publish_result("创建 Web 导出目录失败：" + error.message(), false);
        return;
    }

    const std::string snapshot_json = build_web_json(snapshot);
    const std::string profiles_json = build_profiles_json();
    const std::filesystem::path snapshot_path(prefix.string() + "_snapshot.json");
    const std::filesystem::path profiles_path(prefix.string() + "_profiles.json");
    std::ofstream snapshot_output(snapshot_path, std::ios::binary);
    std::ofstream profiles_output(profiles_path, std::ios::binary);
    snapshot_output << snapshot_json;
    profiles_output << profiles_json;
    if (!snapshot_output || !profiles_output) {
        publish_result("写入 Web 导出文件失败：" + snapshot_path.string(), false);
        return;
    }
    snapshot_output.close();
    profiles_output.close();

    {
        std::lock_guard lock(mutex_);
        profiles_dirty_ = false;
    }
    publish_result("Web 数据已导出：" + snapshot_path.string());
}

std::string LocalBattle::build_web_json(const BattleSnapshot& snap) const {
    std::lock_guard lock(mutex_);
    using json = nlohmann::ordered_json;
    const auto number = [](double value) {
        return std::isfinite(value) ? value : 0.0;
    };
    const auto curve_json = [&](const std::vector<double>& values) {
        json out = json::array();
        for (const double value : values) {
            out.push_back(number(value));
        }
        return out;
    };
    const auto frame_json = [](const std::vector<std::uint32_t>& values) {
        json out = json::array();
        for (const std::uint32_t value : values) {
            out.push_back(value);
        }
        return out;
    };
    const auto types_json = [&](const auto& types) {
        json out = json::object();
        for (std::size_t index = 0; index < kDamageTypeCount; ++index) {
            out[kDamageTypeKeys[index]] = {
                {"actual", number(types[index].actual)},
                {"calculated", number(types[index].calculated)},
            };
        }
        return out;
    };
    const auto talents_json = [](const std::vector<TalentInfo>& talents) {
        json out = json::array();
        for (const TalentInfo& talent : talents) {
            out.push_back({
                {"name", talent.name},
                {"description", talent.description},
                {"potential", talent.potential},
                {"hidden", talent.hidden},
            });
        }
        return out;
    };
    const auto buff_json = [&](const UnitInfo::BuffInfo& buff) {
        json modifier_entries = json::array();
        for (const BuffModifierInfo& modifier : buff.modifier_entries) {
            modifier_entries.push_back({
                {"kind", static_cast<int>(modifier.kind)},
                {"attribute", modifier.attribute},
                {"value", number(modifier.value)},
            });
        }
        json blackboard_entries = json::array();
        for (const BuffBlackboardEntry& entry : buff.blackboard_entries) {
            json item = {
                {"key", entry.key},
                {"value", number(entry.value)},
            };
            if (!entry.value_str.empty()) {
                item["value_str"] = entry.value_str;
            }
            blackboard_entries.push_back(std::move(item));
        }
        return json{
            {"key", buff.key},
            {"template", buff.template_key},
            {"override", buff.override_key},
            {"effect", buff.effect_key},
            {"source", buff.source_id},
            {"modifiers", buff.modifiers},
            {"modifier_entries", std::move(modifier_entries)},
            {"abnormal", buff.abnormal_flags},
            {"abnormal_immune", buff.abnormal_immune_flags},
            {"abnormal_anti", buff.abnormal_anti_flags},
            {"abnormal_combo", buff.abnormal_combo_flags},
            {"abnormal_combo_immune", buff.abnormal_combo_immune_flags},
            {"abnormal_masks", {
                {"flag", buff.abnormal_flag_mask},
                {"immune", buff.abnormal_immune_mask},
                {"anti", buff.abnormal_anti_mask},
                {"combo", buff.abnormal_combo_mask},
                {"combo_immune", buff.abnormal_combo_immune_mask},
            }},
            {"blackboard", buff.blackboard},
            {"blackboard_entries", std::move(blackboard_entries)},
            {"uid", buff.uid},
            {"priority", buff.priority},
            {"event_priority", buff.event_priority},
            {"stack", buff.stack_count},
            {"valid_stack", buff.valid_stack_count},
            {"max_stack", buff.max_stack_count},
            {"trigger_count", buff.trigger_count},
            {"life", number(buff.life_time)},
            {"remaining", number(buff.remaining_time)},
            {"ratio", number(buff.remaining_ratio)},
            {"trigger_interval", number(buff.trigger_interval)},
            {"enabled", buff.enabled},
            {"valid", buff.valid},
            {"triggerable", buff.triggerable},
            {"has_shield", buff.has_shield},
            {"status_resistable", buff.status_resistable},
        };
    };

    json root;
    root["ok"] = last_ok_;
    root["result"] = last_result_;
    root["connected"] = pump_ready_;
    root["pending"] = pending_.size();
    root["stage"] = snap.stage_id;
    root["stage_input"] = snap.stage_input;
    root["stage_code"] = snap.stage_code;
    root["stage_name"] = snap.stage_name;
    root["state"] = snap.state;
    root["playing"] = snap.playing;
    root["paused"] = snap.paused;
    root["cost"] = snap.cost;
    root["frame"] = snap.frame;
    root["epoch"] = snap.epoch;
    root["catalog"] = {
        {"loaded", operators_.size()},
        {"error", catalog_error_},
    };

    json squad = json::array();
    std::vector<std::string> seen_operators;
    for (const CardInfo& card : snap.cards) {
        if (card.id.empty() || card.id.rfind("token_", 0) == 0 ||
            std::find(seen_operators.begin(), seen_operators.end(), card.id) !=
                seen_operators.end()) {
            continue;
        }
        seen_operators.push_back(card.id);
        const SquadSlot* requested = nullptr;
        for (const SquadSlot& slot : squad_) {
            if (slot.id == card.id) {
                requested = &slot;
                break;
            }
        }
        const ModuleInfo* module =
            card.modules.empty() ? nullptr : &card.modules.front();
        const std::string module_id =
            module ? module->id : (requested ? requested->module_id : std::string{});
        int module_level = module && module->level > 0 ? module->level : 3;
        if (!module && requested && requested->module_level > 0) {
            module_level = requested->module_level;
        }
        squad.push_back({
            {"id", card.id},
            {"name", operator_label(card.id)},
            {"level", card.level},
            {"evolve", card.evolve},
            {"potential", card.potential},
            {"trust", requested ? requested->trust : 100},
            {"skill", card.skill_index + 1},
            {"skill_level", requested ? requested->skill_level : card.skill_level},
            {"module", module_id},
            {"module_level", module_level},
        });
    }
    if (squad.empty()) {
        for (const SquadSlot& slot : squad_) {
            squad.push_back({
                {"id", slot.id},
                {"name", operator_label(slot.id)},
                {"level", slot.level},
                {"evolve", slot.evolve},
                {"potential", slot.potential},
                {"trust", slot.trust},
                {"skill", slot.skill_index + 1},
                {"skill_level", slot.skill_level},
                {"module", slot.module_id},
                {"module_level", slot.module_level},
            });
        }
    }
    root["squad"] = std::move(squad);

    std::size_t timeline_node_count = 0;
    std::string timeline_current_id;
    std::string timeline_root_id;
    std::string timeline_stage_id;
    std::string timeline_source;
    std::uint64_t timeline_revision = 0;
    bool timeline_jumping = false;
    const auto [record_operations, record_frames] = active_operation_stats();
    {
        std::lock_guard timeline_lock(record_mutex_);
        timeline_node_count = timeline_nodes_.size();
        timeline_current_id = timeline_current_id_;
        timeline_root_id = timeline_root_id_;
        timeline_stage_id = timeline_stage_id_;
        timeline_source = timeline_source_;
        timeline_revision = timeline_revision_;
        timeline_jumping = pending_timeline_jump_;
    }
    root["record"] = {
        {"active", record_active_.load()},
        {"operations", record_operations},
        {"frames", record_frames},
        {"file", "battle_operations.json"},
        {"timeline_file", "battle_timeline.json"},
        {"timeline", {
            {"active", !timeline_current_id.empty()},
            {"nodes", timeline_node_count},
            {"current_id", timeline_current_id},
            {"root_id", timeline_root_id},
            {"stage", timeline_stage_id},
            {"source", timeline_source},
            {"revision", timeline_revision},
            {"jumping", timeline_jumping},
        }},
    };
    root["replay"] = {
        {"active", replay_active_.load()},
        {"operations", replay_dispatched_},
        {"next", replay_next_step_},
        {"total", replay_steps_.size()},
        {"frame", replay_frame_},
        {"file", replay_file_},
    };
    root["trace"] = {
        {"active", trace_active_},
        {"changes", trace_changes_},
        {"dropped", trace_dropped_lines_},
        {"frame", trace_last_frame_},
        {"fields", trace_fields_text_},
    };

    json damage;
    damage["available"] = snap.damage.available;
    damage["started"] = snap.damage.started;
    damage["ended"] = snap.damage.ended;
    damage["partial"] = snap.damage.partial;
    damage["overrun"] = snap.damage.overrun;
    damage["battle"] = snap.damage.battle;
    damage["sequence"] = snap.damage.sequence;
    damage["events"] = snap.damage.event_count;
    damage["revision"] = snap.damage.revision;
    damage["series_revision"] = snap.damage.series_revision;
    damage["series_points"] = snap.damage.series_points;
    damage["frame_valid"] = snap.damage.frame_valid;
    damage["start_frame"] = snap.damage.start_frame;
    damage["end_frame"] = snap.damage.end_frame;
    damage["duration_frames"] = snap.damage.duration_frames;
    damage["duration"] = number(snap.damage.duration);
    damage["total"] = {
        {"actual", number(snap.damage.total_actual)},
        {"calculated", number(snap.damage.total_calculated)},
        {"healing", number(snap.damage.total_healing)},
        {"regen", number(snap.damage.total_regen)},
        {"taken", number(snap.damage.total_taken)},
    };
    damage["elemental_buildup"] = number(snap.damage.elemental_buildup);
    damage["types"] = types_json(snap.damage.types);
    if (snap.damage.curves_included) {
        damage["series"] = {
            {"frame_step", snap.damage.series.step_frames},
            {"frames", frame_json(snap.damage.series.frames)},
            {"total", curve_json(snap.damage.series.total)},
            {"healing", curve_json(snap.damage.series.healing)},
            {"taken", curve_json(snap.damage.series.taken)},
        };
        for (std::size_t index = 0; index < kDamageTypeCount; ++index) {
            damage["series"][kDamageTypeKeys[index]] =
                curve_json(snap.damage.series.types[index]);
        }
    }
    json operators = json::array();
    for (const DamageActorInfo& actor : snap.damage.actors) {
        const double hp_delta =
            actor.hp_tracked ? actor.hp_effective_current -
                                   actor.hp_effective_start
                             : 0.0;
        const double event_delta =
            actor.healing_received + actor.regen - actor.taken;
        json item = {
            {"id", actor.id},
            {"name", actor.name},
            {"actual", number(actor.actual)},
            {"calculated", number(actor.calculated)},
            {"healing", number(actor.healing)},
            {"healing_received", number(actor.healing_received)},
            {"regen", number(actor.regen)},
            {"taken", number(actor.taken)},
            {"hp_tracked", actor.hp_tracked},
            {"hp_locked", actor.hp_locked},
            {"hp_start", number(actor.hp_start)},
            {"hp_current", number(actor.hp_current)},
            {"hp_max", number(actor.hp_max)},
            {"minus_hp", number(actor.minus_hp)},
            {"max_minus_hp", number(actor.max_minus_hp)},
            {"hp_effective_start", number(actor.hp_effective_start)},
            {"hp_effective_current", number(actor.hp_effective_current)},
            {"hp_delta", number(hp_delta)},
            {"event_delta", number(event_delta)},
            {"balance", number(event_delta - hp_delta)},
            {"hp_event_delta", number(actor.hp_event_delta)},
            {"hp_delta_events", actor.hp_delta_events},
            {"hp_delta_fallbacks", actor.hp_delta_fallbacks},
            {"elemental_buildup", number(actor.elemental_buildup)},
            {"curve_points", actor.curve_points},
            {"types", types_json(actor.types)},
        };
        if (snap.damage.curves_included) {
            item["curve"] = curve_json(actor.curve);
            item["healing_curve"] = curve_json(actor.healing_curve);
            item["taken_curve"] = curve_json(actor.taken_curve);
        }
        operators.push_back(std::move(item));
    }
    damage["operators"] = std::move(operators);
    root["damage"] = std::move(damage);

    json cards = json::array();
    for (const CardInfo& card : snap.cards) {
        cards.push_back({
            {"id", card.id},
            {"name", operator_name(card.id)},
            {"uid", card.uid},
            {"level", card.level},
            {"evolve", card.evolve},
            {"potential", card.potential},
            {"skill", card.skill_index + 1},
            {"cost", card.cost},
            {"remaining", card.remaining},
            {"respawn_remaining", number(card.respawn_remaining)},
            {"respawn_progress", number(card.respawn_progress)},
            {"state", card.state},
            {"ready", card.ready},
            {"hidden", card.hidden},
            {"deployed", card.deployed},
            {"dont_occupy_deploy_cnt", card.dont_occupy_deploy_cnt},
            {"talents", talents_json(card.talents)},
        });
    }
    root["cards"] = std::move(cards);

    json units = json::array();
    for (const UnitInfo& unit : snap.units) {
        json modules = json::array();
        for (std::size_t module_index = 0; module_index < unit.modules.size();
             ++module_index) {
            const ModuleInfo& module = unit.modules[module_index];
            modules.push_back({
                {"id", module.id},
                {"level", module.level},
            });
        }
        json buffs = json::array();
        for (const UnitInfo::BuffInfo& buff : unit.buffs) {
            buffs.push_back(buff_json(buff));
        }
        units.push_back({
            {"id", unit.id},
            {"name", operator_name(unit.id)},
            {"tmpl", unit.tmpl_id},
            {"uid", unit.uid},
            {"card_uid", unit.card_uid},
            {"owner_uid", unit.owner_uid},
            {"hp", number(unit.hp)},
            {"max_hp", number(unit.max_hp)},
            {"minus_hp", number(unit.minus_hp)},
            {"max_minus_hp", number(unit.max_minus_hp)},
            {"hp_effective", number(unit.hp_effective)},
            {"shield", number(unit.shield)},
            {"shield_layers", unit.shield_layers},
            {"atk", number(unit.atk)},
            {"def", number(unit.def)},
            {"magic_resistance", number(unit.magic_resistance)},
            {"attack_speed", number(unit.attack_speed)},
            {"move_speed", number(unit.move_speed)},
            {"block_count", unit.block_count},
            {"unit_cost", unit.unit_cost},
            {"sp", number(unit.sp)},
            {"max_sp", unit.max_sp},
            {"sp_enough", unit.sp_enough},
            {"mine", unit.mine},
            {"alive", unit.alive},
            {"in_skill", unit.in_skill},
            {"skill_affecting", unit.skill_affecting},
            {"skill_begin_anim", unit.skill_begin_anim},
            {"skill_remaining", number(unit.skill_remaining)},
            {"skill_trigger_count", unit.skill_trigger_count},
            {"skill_ammo_remaining", unit.skill_ammo_remaining},
            {"skill_ammo_max", unit.skill_ammo_max},
            {"state_id", unit.current_state_id},
            {"skill_activatable", unit.skill_activatable},
            {"skill_op_triggerable", unit.skill_op_triggerable},
            {"skill_overload_skill", unit.skill_overload_skill},
            {"skill_overloaded", unit.skill_overloaded},
            {"skill_retriggerable", unit.skill_retriggerable},
            {"talents", talents_json(unit.talents)},
            {"modules", std::move(modules)},
            {"withdrawable", unit.withdrawable},
            {"manually_withdrawable", unit.manually_withdrawable},
            {"buffs", std::move(buffs)},
        });
    }
    root["units"] = std::move(units);

    json profiles = json::array();
    for (const EnemyProfile& profile : snap.enemy_profiles) {
        json profile_skills = json::array();
        for (const EnemySkillDefinition& skill : profile.skills) {
            profile_skills.push_back({
                {"key", skill.key},
                {"priority", skill.priority},
                {"sp_cost", skill.sp_cost},
                {"cooldown", number(skill.cooldown)},
                {"initial_cooldown", number(skill.initial_cooldown)},
            });
        }
        profiles.push_back({
            {"id", profile.id},
            {"name", profile.name},
            {"index", profile.index},
            {"description", profile.description},
            {"race", profile.race},
            {"attack_type", profile.attack_type},
            {"ability", profile.ability},
            {"tags", profile.tags},
            {"abilities", profile.abilities},
            {"damage_types", profile.damage_types},
            {"skills", std::move(profile_skills)},
            {"level_type", profile.level_type},
            {"source", profile.source},
        });
    }
    root["enemy_profiles"] = std::move(profiles);

    json enemies = json::array();
    for (const EnemyInfo& enemy : snap.enemies) {
        json skills = json::array();
        for (const EnemySkillInfo& skill : enemy.skills) {
            skills.push_back({
                {"key", skill.key},
                {"signal", skill.signal_id},
                {"range", skill.range_id},
                {"priority", skill.priority},
                {"trigger_count", skill.trigger_count},
                {"max_trigger_count", skill.max_trigger_count},
                {"sp_cost", skill.sp_cost},
                {"cooldown", number(skill.cooldown)},
                {"cooldown_total", number(skill.cooldown_total)},
                {"enabled", skill.enabled},
                {"used_up", skill.used_up},
                {"casting", skill.casting},
                {"affecting", skill.affecting},
            });
        }
        json buffs = json::array();
        for (const UnitInfo::BuffInfo& buff : enemy.buffs) {
            buffs.push_back(buff_json(buff));
        }
        enemies.push_back({
            {"serial", enemy.serial},
            {"id", enemy.id},
            {"uid", enemy.uid},
            {"owner_uid", enemy.owner_uid},
            {"hp", number(enemy.hp)},
            {"max_hp", number(enemy.max_hp)},
            {"shield", number(enemy.shield)},
            {"atk", number(enemy.atk)},
            {"def", number(enemy.def)},
            {"magic_resistance", number(enemy.magic_resistance)},
            {"attack_speed", number(enemy.attack_speed)},
            {"move_speed", number(enemy.move_speed)},
            {"created_time", number(enemy.created_time)},
            {"row", enemy.row},
            {"col", enemy.col},
            {"block_count", enemy.block_count},
            {"mass_level", enemy.mass_level},
            {"alive", enemy.alive},
            {"boss", enemy.boss},
            {"giant_boss", enemy.giant_boss},
            {"skills", std::move(skills)},
            {"buffs", std::move(buffs)},
        });
    }
    root["enemies"] = std::move(enemies);

    json waves = json::array();
    for (const EnemyWave& wave : snap.enemy_waves) {
        json actions = json::array();
        for (const EnemyWaveAction& action : wave.actions) {
            actions.push_back({
                {"key", action.key},
                {"count", action.count},
                {"route", action.route},
                {"pre_delay", number(action.pre_delay)},
                {"interval", number(action.interval)},
                {"first_time", number(action.first_time)},
            });
        }
        waves.push_back({
            {"index", wave.index},
            {"spawned", wave.spawned},
            {"total", wave.total},
            {"pre_delay", number(wave.pre_delay)},
            {"actions", std::move(actions)},
        });
    }
    root["enemy_waves"] = {
        {"current", snap.enemy_wave_current},
        {"total", snap.enemy_wave_total},
        {"waves", std::move(waves)},
    };
    root["enemy_focus"] = {
        {"uid", snap.enemy_focus_uid},
        {"active", snap.enemy_focus_active},
    };

    const auto entries_json =
        [&](const std::vector<BuffBlackboardEntry>& entries) {
            json out = json::array();
            for (const BuffBlackboardEntry& entry : entries) {
                json item = {
                    {"key", entry.key},
                    {"value", number(entry.value)},
                };
                if (!entry.value_str.empty()) {
                    item["value_str"] = entry.value_str;
                }
                out.push_back(std::move(item));
            }
            return out;
        };
    json global_buffs = json::array();
    for (const GlobalBuffGroupInfo& group : snap.global_buffs) {
        json group_buffs = json::array();
        for (const GlobalBuffEntryInfo& buff : group.buffs) {
            group_buffs.push_back({
                {"key", buff.key},
                {"template", buff.template_key},
                {"override", buff.override_key},
                {"max_stack", buff.max_stack},
                {"priority", buff.priority},
                {"life", number(buff.life_time)},
                {"blackboard", buff.blackboard},
                {"blackboard_entries", entries_json(buff.blackboard_entries)},
            });
        }
        global_buffs.push_back({
            {"key", group.key},
            {"source", group.source},
            {"uid", group.uid},
            {"layer_mask", group.layer_mask},
            {"blackboard", group.blackboard},
            {"blackboard_entries", entries_json(group.blackboard_entries)},
            {"buffs", std::move(group_buffs)},
        });
    }
    root["global_buffs"] = std::move(global_buffs);

    json relics = json::array();
    for (const RogueRelicInfo& relic : snap.rogue.relics) {
        relics.push_back({
            {"id", relic.id},
            {"layer", relic.layer},
            {"buff_keys", relic.buff_keys},
        });
    }
    root["rogue"] = {
        {"valid", snap.rogue.valid},
        {"topic", snap.rogue.topic_id},
        {"relics", std::move(relics)},
    };
    return root.dump();
}

void LocalBattle::publish_web_snapshot(const BattleSnapshot& snapshot) {
    if (!bridge_ || !bridge_->publish) {
        return;
    }
    const std::uint64_t json_started = perf_now_us();
    std::string json = build_web_json(snapshot);
    perf_.web_json.add(perf_now_us() - json_started);
    perf_.web_bytes_last = json.size();
    perf_.web_bytes_total += json.size();
    std::lock_guard lock(web_payload_mutex_);
    if (json == last_web_json_) {
        ++perf_.web_publish_skipped;
        return;
    }
    const std::uint64_t publish_started = perf_now_us();
    bridge_->publish("betterbattle.snapshot", json.data(), json.size(), 0);
    perf_.web_publish.add(perf_now_us() - publish_started);
    last_web_json_ = std::move(json);
}

void LocalBattle::publish_catalog() {
    if (!bridge_ || !bridge_->publish) {
        return;
    }
    nlohmann::ordered_json payload;
    {
        std::lock_guard lock(mutex_);
        payload["count"] = operators_.size();
        payload["error"] = catalog_error_;
        payload["operators"] = nlohmann::ordered_json::array();
        for (const OperatorEntry& entry : operators_) {
            payload["operators"].push_back({
                {"id", entry.id},
                {"name", entry.name},
                {"max_potential", entry.max_potential},
                {"skill_count", entry.skill_count},
            });
        }
    }
    const std::string json = payload.dump();
    bridge_->publish("betterbattle.catalog", json.data(), json.size(), 0);
}

void LocalBattle::connect_if_ready() {
    if (pump_ready_) {
        return;
    }
    const std::uint64_t now = GetTickCount64();
    if (now < next_connect_tick_) {
        return;
    }
    next_connect_tick_ = now + kReconnectDelayMs;

    try {
        runtime_.initialize();
        if (!classes_.battle_controller) {
            classes_.resolve(runtime_);
        }
        il2cpp::Runtime::Attachment attachment(runtime_);
        try {
            if (!battle_capture_.initialize()) {
                log(kLogWarning, "BetterBattle battle event capture is not ready");
            }
        } catch (const std::exception& error) {
            log(kLogWarning, std::string("Battle event capture unavailable: ") +
                                 error.what());
        }
        void* updater = runtime_.find("Torappu", "GlobalInitializerAndUpdater");
        void* update = runtime_.method(updater, "Update", 0);
        std::uint32_t flags = 0;
        if ((runtime_.method_flags(update, &flags) & kMethodAttributeStatic) != 0) {
            throw std::runtime_error("GlobalInitializerAndUpdater.Update is static");
        }
        void* entry = *static_cast<void**>(update);
        if (!il2cpp::is_executable(entry)) {
            throw std::runtime_error("GlobalInitializerAndUpdater.Update has no native entry");
        }

        void* original = nullptr;
        const std::uint64_t hook =
            host_.create_native_hook(host_.owner, entry, reinterpret_cast<void*>(&on_global_update),
                                     &original);
        if (!hook || !original) {
            throw std::runtime_error("Could not create the game-thread pump hook");
        }
        pump_hook_ = hook;
        original_global_update_ = original;
        active.store(this);
        if (!host_.enable_native_hook(host_.owner, pump_hook_, true)) {
            host_.remove_native_hook(host_.owner, pump_hook_);
            pump_hook_ = 0;
            original_global_update_ = nullptr;
            active.store(nullptr);
            throw std::runtime_error("Could not enable the game-thread pump hook");
        }

        pump_ready_ = true;
        install_player_data_hook();
        install_preprocess_global_buff_hook();
        try {
            void* controller_class = classes_.battle_controller;
            void* fixed_update = runtime_.method(controller_class, "FixedUpdate", 0);
            void* fixed_entry = *static_cast<void**>(fixed_update);
            if (il2cpp::is_executable(fixed_entry)) {
                void* original = nullptr;
                const std::uint64_t hook = host_.create_native_hook(
                    host_.owner, fixed_entry,
                    reinterpret_cast<void*>(&on_battle_fixed_update), &original);
                if (hook && original) {
                    battle_observer_hook_ = hook;
                    battle_observer_original_ = original;
                    if (host_.enable_native_hook(host_.owner, battle_observer_hook_, true)) {
                        fixed_observer_owner.store(this, std::memory_order_release);
                    } else {
                        host_.remove_native_hook(host_.owner, battle_observer_hook_);
                        battle_observer_hook_ = 0;
                        battle_observer_original_ = nullptr;
                    }
                }
            }
        } catch (const std::exception& error) {
            log(kLogWarning, std::string("Battle observer unavailable: ") + error.what());
        }
        try {
            void* finish_game =
                runtime_.method(classes_.battle_controller, "FinishGame", 3);
            if (finish_game) {
                void* finish_entry = *static_cast<void**>(finish_game);
                if (il2cpp::is_executable(finish_entry)) {
                    void* original = nullptr;
                    const std::uint64_t hook = host_.create_native_hook(
                        host_.owner, finish_entry,
                        reinterpret_cast<void*>(&on_battle_finish_game), &original);
                    if (hook && original) {
                        finish_game_hook_ = hook;
                        finish_game_original_ = original;
                        finish_game_owner.store(this, std::memory_order_release);
                        if (!host_.enable_native_hook(host_.owner, finish_game_hook_, true)) {
                            finish_game_owner.store(nullptr, std::memory_order_release);
                            host_.remove_native_hook(host_.owner, finish_game_hook_);
                            finish_game_hook_ = 0;
                            finish_game_original_ = nullptr;
                        }
                    }
                }
            }
        } catch (const std::exception& error) {
            log(kLogWarning, std::string("Battle finish hook unavailable: ") + error.what());
        }
        log(kLogInfo, "BetterBattle runtime connected; game-thread task pump is active");

        std::lock_guard lock(mutex_);
        if (!catalog_requested_) {
            catalog_requested_ = true;
            pending_.push_back(Command{"catalog", {}});
        }
        pending_.push_back(Command{"refresh", {}});
    } catch (const std::exception& error) {
        log(kLogWarning, std::string("BetterBattle is waiting for IL2CPP: ") + error.what());
    }
}

bool LocalBattle::ensure_frame_clock() {
    if (frame_clock_get_) {
        return true;
    }
    const std::uint64_t now = GetTickCount64();
    if (now < next_frame_clock_retry_tick_) {
        return false;
    }
    next_frame_clock_retry_tick_ = now + 5000;
    try {
        il2cpp::Runtime::Attachment attachment(runtime_);
        void* controller_class = classes_.battle_controller;
        void* method = runtime_.method(controller_class, "get_fixedFrameCnt", 0);
        void* entry = *static_cast<void**>(method);
        if (method && il2cpp::is_executable(entry)) {
            frame_clock_method_ = method;
            frame_clock_get_ = reinterpret_cast<StaticFrameGetter>(entry);
            return true;
        }
    } catch (...) {
    }
    return false;
}

void LocalBattle::maybe_schedule_refresh() {
    if (!pump_ready_ || host_.test_host || stopping_.load()) {
        return;
    }
    const std::uint64_t now = GetTickCount64();
    std::uint32_t frame = 0;
    const bool has_frame_clock = ensure_frame_clock();
    if (has_frame_clock) {
        try {
            frame = frame_clock_get_(frame_clock_method_);
        } catch (...) {
            frame_clock_get_ = nullptr;
            frame_clock_method_ = nullptr;
        }
    }

    bool frame_due = false;
    if (has_frame_clock && last_refresh_frame_valid_) {
        const std::uint32_t minimum =
            refresh_frame_interval_.load(std::memory_order_relaxed);
        frame_due = frame >= last_refresh_frame_ &&
                    frame - last_refresh_frame_ >= minimum;
    }
    const bool wall_due = now >= next_refresh_tick_;
    if (!frame_due && !wall_due) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        const bool already_queued =
            std::any_of(pending_.begin(), pending_.end(), [](const Command& command) {
                return command.name == "refresh";
            });
        if (already_queued || work_running_.load()) {
            return;
        }
        pending_.push_back(Command{"refresh", {}});
    }
    last_refresh_frame_valid_ = has_frame_clock;
    last_refresh_frame_ = frame;
    next_refresh_tick_ =
        now + refresh_interval_ms_.load(std::memory_order_relaxed);
}

void LocalBattle::schedule_next_work() {
    if (!pump_ready_ || work_running_.load() || host_.test_host) {
        return;
    }
    std::unique_ptr<TaskPacket> packet;
    {
        std::lock_guard lock(mutex_);
        if (pending_.empty()) {
            return;
        }
        packet = std::make_unique<TaskPacket>(TaskPacket{this, std::move(pending_.front())});
        pending_.pop_front();
    }
    work_running_.store(true);
    if (!host_.queue_game_task(host_.owner, &LocalBattle::game_task, packet.get())) {
        work_running_.store(false);
        std::lock_guard lock(mutex_);
        pending_.push_front(std::move(packet->command));
        return;
    }
    (void)packet.release();
}

void LocalBattle::game_task(void* user) {
    auto* packet = static_cast<TaskPacket*>(user);
    std::unique_ptr<TaskPacket> owned(packet);
    LocalBattle* self = owned->owner;
    try {
        self->run_command(owned->command);
    } catch (const std::exception& error) {
        self->publish_result(std::string("局内任务异常: ") + error.what(), false);
        self->log(kLogError, std::string("BetterBattle task failed: ") + error.what());
    } catch (...) {
        self->publish_result("局内任务发生未知异常", false);
    }
    self->work_running_.store(false);
    self->schedule_next_work();
}

void LocalBattle::on_global_update(void* self, void* method) {
    LocalBattle* battle = active.load();
    if (!battle) {
        return;
    }
    using UpdateFn = void (*)(void*, void*);
    auto original = reinterpret_cast<UpdateFn>(battle->original_global_update_);
    if (original) {
        original(self, method);
    }
    const std::uint64_t tick_started = perf_now_us();
    if (battle->finish_home_pending_.exchange(false, std::memory_order_acq_rel)) {
        if (!battle->route_home_from_finish()) {
            battle->finish_home_pending_.store(true, std::memory_order_release);
        }
        return;
    }
    battle->trace_frame();
    battle->maybe_schedule_refresh();
    battle->host_.pump_game_tasks(battle->host_.owner);
    battle->perf_.frame_tick.add(perf_now_us() - tick_started);
}

void* LocalBattle::on_player_data_get(void* self, void* method) {
    LocalBattle* battle = player_data_owner.load(std::memory_order_acquire);
    using GetDataFn = void* (*)(void*, void*);
    auto original = battle
                        ? reinterpret_cast<GetDataFn>(battle->player_data_original_)
                        : nullptr;
    void* result = original ? original(self, method) : nullptr;
    if (battle && self) {
        battle->player_data_instance_.store(self, std::memory_order_release);
    }
    return result;
}

void LocalBattle::on_player_data_changed(void* self, void* model, void* delta,
                                          void* method) {
    LocalBattle* battle =
        player_data_delta_owner.load(std::memory_order_acquire);
    using ChangedFn = void (*)(void*, void*, void*, void*);
    auto original = battle
                        ? reinterpret_cast<ChangedFn>(
                              battle->player_data_delta_original_)
                        : nullptr;
    if (battle &&
        battle->player_data_delta_active_.load(std::memory_order_acquire) &&
        delta) {
        try {
            battle->capture_player_delta(delta);
        } catch (...) {
        }
    }
    if (original) {
        original(self, model, delta, method);
    }
    if (battle && delta) {
        battle->maybe_queue_pending_capture();
    }
}

void LocalBattle::on_player_data_notify(void* self, void* model, void* delta,
                                         void* method) {
    LocalBattle* battle =
        player_data_delta_owner.load(std::memory_order_acquire);
    using ChangedFn = void (*)(void*, void*, void*, void*);
    auto original = battle
                        ? reinterpret_cast<ChangedFn>(
                              battle->player_data_notify_original_)
                        : nullptr;
    if (battle &&
        battle->player_data_delta_active_.load(std::memory_order_acquire) &&
        delta) {
        try {
            battle->capture_player_delta(delta);
        } catch (...) {
        }
    }
    if (original) {
        original(self, model, delta, method);
    }
    if (battle && delta) {
        battle->maybe_queue_pending_capture();
    }
}

void* LocalBattle::on_preprocess_global_buff(void* self, void* method) {
    LocalBattle* battle =
        preprocess_global_buff_owner.load(std::memory_order_acquire);
    using PreprocessFn = void* (*)(void*, void*);
    auto original = battle
                        ? reinterpret_cast<PreprocessFn>(
                              battle->preprocess_global_buff_original_)
                        : nullptr;
    void* result = original ? original(self, method) : nullptr;
    if (battle && result) {
        try {
            battle->queue_global_buff_capture(result);
        } catch (...) {
        }
    }
    return result;
}

bool LocalBattle::install_player_data_hook() {
    if (player_data_hook_) {
        return true;
    }
    try {
        void* player_data_class = classes_.player_data;
        void* get_data = runtime_.method(player_data_class, "get_data", 0);
        void* entry = *static_cast<void**>(get_data);
        if (!il2cpp::is_executable(entry)) {
            throw std::runtime_error("PlayerData.get_data 没有可执行入口");
        }
        void* original = nullptr;
        const std::uint64_t hook = host_.create_native_hook(
            host_.owner, entry,
            reinterpret_cast<void*>(&LocalBattle::on_player_data_get), &original);
        if (!hook || !original) {
            throw std::runtime_error("创建 PlayerData Hook 失败");
        }
        player_data_hook_ = hook;
        player_data_original_ = original;
        player_data_owner.store(this, std::memory_order_release);
        if (!host_.enable_native_hook(host_.owner, player_data_hook_, true)) {
            player_data_owner.store(nullptr, std::memory_order_release);
            host_.remove_native_hook(host_.owner, player_data_hook_);
            player_data_hook_ = 0;
            player_data_original_ = nullptr;
            throw std::runtime_error("启用 PlayerData Hook 失败");
        }
        log(kLogInfo, "PlayerData.get_data hook active");
        return true;
    } catch (const std::exception& error) {
        log(kLogWarning, std::string("PlayerData hook unavailable: ") + error.what());
        return false;
    }
}

bool LocalBattle::install_player_data_delta_hook() {
    if (player_data_delta_hook_) {
        return true;
    }
    try {
        void* player_data_class = classes_.player_data;
        void* changed =
            runtime_.method(player_data_class, "OnPlayerDataChanged", 2);
        void* entry = *static_cast<void**>(changed);
        if (!il2cpp::is_executable(entry)) {
            throw std::runtime_error("PlayerData.OnPlayerDataChanged 没有可执行入口");
        }
        void* original = nullptr;
        const std::uint64_t hook = host_.create_native_hook(
            host_.owner, entry,
            reinterpret_cast<void*>(&LocalBattle::on_player_data_changed),
            &original);
        if (!hook || !original) {
            throw std::runtime_error("创建 PlayerDataDelta Hook 失败");
        }
        player_data_delta_hook_ = hook;
        player_data_delta_original_ = original;
        player_data_delta_owner.store(this, std::memory_order_release);
        if (!host_.enable_native_hook(host_.owner, player_data_delta_hook_,
                                      true)) {
            player_data_delta_owner.store(nullptr, std::memory_order_release);
            host_.remove_native_hook(host_.owner, player_data_delta_hook_);
            player_data_delta_hook_ = 0;
            player_data_delta_original_ = nullptr;
            throw std::runtime_error("启用 PlayerDataDelta Hook 失败");
        }
        log(kLogInfo, "PlayerData.OnPlayerDataChanged hook active");
        return true;
    } catch (const std::exception& error) {
        log(kLogWarning,
            std::string("PlayerDataDelta hook unavailable: ") + error.what());
        return false;
    }
}

bool LocalBattle::install_player_data_notify_hook() {
    if (player_data_notify_hook_) {
        return true;
    }
    try {
        void* player_data_class = classes_.player_data;
        void* notify = runtime_.method(player_data_class, "NotifyDataChanged", 2);
        void* entry = *static_cast<void**>(notify);
        if (!il2cpp::is_executable(entry)) {
            throw std::runtime_error(
                "PlayerData.NotifyDataChanged 没有可执行入口");
        }
        void* original = nullptr;
        const std::uint64_t hook = host_.create_native_hook(
            host_.owner, entry,
            reinterpret_cast<void*>(&LocalBattle::on_player_data_notify),
            &original);
        if (!hook || !original) {
            throw std::runtime_error("创建 PlayerDataNotify Hook 失败");
        }
        player_data_notify_hook_ = hook;
        player_data_notify_original_ = original;
        if (!host_.enable_native_hook(host_.owner, player_data_notify_hook_,
                                      true)) {
            host_.remove_native_hook(host_.owner, player_data_notify_hook_);
            player_data_notify_hook_ = 0;
            player_data_notify_original_ = nullptr;
            throw std::runtime_error("启用 PlayerDataNotify Hook 失败");
        }
        log(kLogInfo, "PlayerData.NotifyDataChanged hook active");
        return true;
    } catch (const std::exception& error) {
        log(kLogWarning,
            std::string("PlayerDataNotify hook unavailable: ") + error.what());
        return false;
    }
}

bool LocalBattle::install_preprocess_global_buff_hook() {
    if (preprocess_global_buff_hook_) {
        return true;
    }
    try {
        void* manager_class =
            runtime_.find("Torappu.Battle.Roguelike", "RoguelikeBattleManager");
        void* preprocess =
            runtime_.method(manager_class, "PreprocessGlobalBuff", 0);
        if (!preprocess) {
            throw std::runtime_error(
                "RoguelikeBattleManager.PreprocessGlobalBuff 不存在");
        }
        void* entry = *static_cast<void**>(preprocess);
        if (!il2cpp::is_executable(entry)) {
            throw std::runtime_error(
                "RoguelikeBattleManager.PreprocessGlobalBuff 没有可执行入口");
        }
        void* original = nullptr;
        const std::uint64_t hook = host_.create_native_hook(
            host_.owner, entry,
            reinterpret_cast<void*>(&LocalBattle::on_preprocess_global_buff),
            &original);
        if (!hook || !original) {
            throw std::runtime_error("创建 PreprocessGlobalBuff Hook 失败");
        }
        preprocess_global_buff_hook_ = hook;
        preprocess_global_buff_original_ = original;
        preprocess_global_buff_owner.store(this, std::memory_order_release);
        if (!host_.enable_native_hook(host_.owner, preprocess_global_buff_hook_,
                                      true)) {
            preprocess_global_buff_owner.store(nullptr,
                                               std::memory_order_release);
            host_.remove_native_hook(host_.owner, preprocess_global_buff_hook_);
            preprocess_global_buff_hook_ = 0;
            preprocess_global_buff_original_ = nullptr;
            throw std::runtime_error("启用 PreprocessGlobalBuff Hook 失败");
        }
        log(kLogInfo, "RoguelikeBattleManager.PreprocessGlobalBuff hook active");
        return true;
    } catch (const std::exception& error) {
        log(kLogWarning, std::string("PreprocessGlobalBuff hook unavailable: ") +
                             error.what());
        return false;
    }
}

bool LocalBattle::can_unload() const {
    return !work_running_.load();
}

bool LocalBattle::shutdown() {
    stopping_.store(true);
    if (!battle_capture_.shutdown()) {
        return false;
    }
    close_probe();
    if (pump_hook_) {
        host_.enable_native_hook(host_.owner, pump_hook_, false);
        if (!host_.remove_native_hook(host_.owner, pump_hook_)) {
            return false;
        }
        pump_hook_ = 0;
    }
    if (battle_observer_hook_) {
        fixed_observer_owner.store(nullptr, std::memory_order_release);
        host_.enable_native_hook(host_.owner, battle_observer_hook_, false);
        if (!host_.remove_native_hook(host_.owner, battle_observer_hook_)) {
            return false;
        }
        battle_observer_hook_ = 0;
        battle_observer_original_ = nullptr;
    }
    if (finish_game_hook_) {
        finish_game_owner.store(nullptr, std::memory_order_release);
        host_.enable_native_hook(host_.owner, finish_game_hook_, false);
        if (!host_.remove_native_hook(host_.owner, finish_game_hook_)) {
            return false;
        }
        finish_game_hook_ = 0;
        finish_game_original_ = nullptr;
    }
    if (player_data_hook_) {
        player_data_owner.store(nullptr, std::memory_order_release);
        host_.enable_native_hook(host_.owner, player_data_hook_, false);
        if (!host_.remove_native_hook(host_.owner, player_data_hook_)) {
            return false;
        }
        player_data_hook_ = 0;
        player_data_original_ = nullptr;
        player_data_instance_.store(nullptr, std::memory_order_release);
    }
    if (player_data_delta_hook_) {
        player_data_delta_active_.store(false, std::memory_order_release);
        player_data_delta_owner.store(nullptr, std::memory_order_release);
        host_.enable_native_hook(host_.owner, player_data_delta_hook_, false);
        if (!host_.remove_native_hook(host_.owner, player_data_delta_hook_)) {
            return false;
        }
        player_data_delta_hook_ = 0;
        player_data_delta_original_ = nullptr;
    }
    if (player_data_notify_hook_) {
        host_.enable_native_hook(host_.owner, player_data_notify_hook_, false);
        if (!host_.remove_native_hook(host_.owner, player_data_notify_hook_)) {
            return false;
        }
        player_data_notify_hook_ = 0;
        player_data_notify_original_ = nullptr;
    }
    if (preprocess_global_buff_hook_) {
        preprocess_global_buff_owner.store(nullptr,
                                           std::memory_order_release);
        host_.enable_native_hook(host_.owner, preprocess_global_buff_hook_,
                                 false);
        if (!host_.remove_native_hook(host_.owner,
                                      preprocess_global_buff_hook_)) {
            return false;
        }
        preprocess_global_buff_hook_ = 0;
        preprocess_global_buff_original_ = nullptr;
    }
    pump_ready_ = false;
    active.store(nullptr);
    original_global_update_ = nullptr;
    record_active_.store(false, std::memory_order_release);
    write_recording();
    if (!remove_replay_hook()) {
        return false;
    }
    if (!remove_record_hooks()) {
        return false;
    }
    close_trace();

    {
        std::lock_guard lock(mutex_);
        pending_.clear();
    }
    return true;
}

} // namespace ark::better_battle

namespace {

std::atomic<ark::better_battle::LocalBattle*> plugin{nullptr};

void launch_webview(const char* root_directory) {
    if (!root_directory || !*root_directory) {
        return;
    }
    const std::filesystem::path directory =
        std::filesystem::path(root_directory) / "BetterBattleWeb";
    const std::filesystem::path executable = directory / "BetterBattleWeb.exe";
    if (!std::filesystem::is_regular_file(executable)) {
        return;
    }
    std::wstring command = L"\"" + executable.wstring() + L"\"";
    std::wstring working_directory = directory.wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNOACTIVATE;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, working_directory.c_str(), &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

bool initialize(const ArkPluginApiV1* host) {
    if (!ark::better_battle::host_is_compatible(host) || plugin.load()) {
        return false;
    }
    auto backend = std::make_unique<ark::better_battle::LocalBattle>(*host);
    const auto* bridge = static_cast<const ArkExternalBridgeApiV1*>(
        host->require_service(host->owner, ARK_EXTERNAL_BRIDGE_SERVICE_NAME,
                              ARK_EXTERNAL_BRIDGE_ABI_VERSION,
                              sizeof(ArkExternalBridgeApiV1)));
    if (bridge && bridge->size >= sizeof(ArkExternalBridgeApiV1) &&
        bridge->abi_version == ARK_EXTERNAL_BRIDGE_ABI_VERSION) {
        backend->set_bridge(bridge);
    }
    if (!host->on_frame(host->owner, &ark::better_battle::on_frame, backend.get())) {
        return false;
    }
    plugin.store(backend.release());
    launch_webview(host->root_directory);
    return true;
}

} // namespace

ARK_EXPORT const ArkPluginInfoV1* ark_plugin_query() {
    static const char* const dependencies[] = {"web_bridge.dll"};
    static const ArkPluginInfoV1 info{
        .size = sizeof(ArkPluginInfoV1),
        .abi_version = ARK_ABI_VERSION,
        .imgui_version = IMGUI_VERSION_NUM,
        .name = "BetterBattle",
        .version = "1.0.0",
        .author = "ArkFramework",
        .flags = ARK_PLUGIN_DEFAULT,
        .dependencies = dependencies,
        .dependency_count = 1,
    };
    return &info;
}

ARK_EXPORT bool ark_plugin_initialize(const ArkPluginApiV1* host) {
    return initialize(host);
}

ARK_EXPORT bool ark_plugin_can_unload(char* reason, size_t capacity) {
    auto* backend = plugin.load();
    if (!backend || backend->can_unload()) {
        return true;
    }
    if (reason && capacity) {
        std::snprintf(reason, capacity, "%s", "请等待局内任务结束。");
    }
    return false;
}

ARK_EXPORT bool ark_plugin_shutdown() {
    auto* backend = plugin.load();
    if (!backend) {
        return true;
    }
    if (!backend->shutdown()) {
        return false;
    }
    plugin.store(nullptr);
    delete backend;
    return true;
}
