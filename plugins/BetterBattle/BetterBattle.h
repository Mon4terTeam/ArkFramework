#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ark/plugin.h>

#include "battle_event_capture.h"
#include "class_ids.h"
#include "../common/il2cpp_runtime.h"

struct ArkExternalBridgeApiV1;

namespace ark::better_battle {

inline constexpr std::uint32_t kLogInfo = 1;
inline constexpr std::uint32_t kLogWarning = 2;
inline constexpr std::uint32_t kLogError = 3;

struct OperatorEntry {
    std::string id;
    std::string name;
    int max_level = 90;
    int max_evolve = 2;
    int max_potential = 5;
    int skill_count = 3;
};

struct SquadSlot {
    std::string id;
    int level = 90;
    int evolve = 2;
    int potential = 0;
    int trust = 100;
    int skill_index = 0;
    int skill_level = 10;
    std::string module_id;
    int module_level = 3;
};

struct TalentInfo {
    std::string name;
    std::string description;
    int potential = 0;
    bool hidden = false;
};

struct ModuleInfo {
    std::string id;
    int level = 0;
};

struct BuffBlackboardEntry {
    std::string key;
    std::string value_str;
    double value = 0;
};

enum class BuffModifierKind : int {
    Addition = 0,
    Multiplier = 1,
    FinalAddition = 2,
    FinalScaler = 3,
};

struct BuffModifierInfo {
    BuffModifierKind kind = BuffModifierKind::Addition;
    std::string attribute;
    double value = 0;
};

struct GlobalBuffEntryInfo {
    std::string key;
    std::string template_key;
    std::string override_key;
    std::string blackboard;
    std::vector<BuffBlackboardEntry> blackboard_entries;
    int max_stack = 0;
    int priority = 0;
    double life_time = 0;
};

struct GlobalBuffGroupInfo {
    std::string key;
    std::string source;
    std::uint32_t uid = 0;
    int layer_mask = 0;
    std::string blackboard;
    std::vector<BuffBlackboardEntry> blackboard_entries;
    std::vector<GlobalBuffEntryInfo> buffs;
};

struct RogueRelicInfo {
    std::string id;
    int layer = 0;
    std::vector<std::string> buff_keys;
};

struct RogueContextInfo {
    bool valid = false;
    std::string topic_id;
    std::vector<RogueRelicInfo> relics;
};

struct BuffStaticInfo {
    std::string key;
    std::string template_key;
    std::string override_key;
    std::string effect_key;
    std::string source_id;
    std::string modifiers;
    std::string blackboard;
    std::vector<BuffModifierInfo> modifier_entries;
    std::vector<BuffBlackboardEntry> blackboard_entries;
    std::string abnormal_flags;
    std::string abnormal_immune_flags;
    std::string abnormal_anti_flags;
    std::string abnormal_combo_flags;
    std::string abnormal_combo_immune_flags;
    std::int64_t abnormal_flag_mask = 0;
    std::int64_t abnormal_immune_mask = 0;
    std::int64_t abnormal_anti_mask = 0;
    std::int64_t abnormal_combo_mask = 0;
    std::int64_t abnormal_combo_immune_mask = 0;
    int priority = 0;
    int event_priority = 0;
    int data_max_stack_count = 0;
    std::int64_t modifiers_remaining_bucket =
        std::numeric_limits<std::int64_t>::lowest();
    std::int64_t modifiers_existing_bucket =
        std::numeric_limits<std::int64_t>::lowest();
    int modifiers_stack_count = std::numeric_limits<int>::lowest();
    int modifiers_trigger_count = std::numeric_limits<int>::lowest();
    bool modifiers_initialized = false;
    std::uint64_t seen_generation = 0;
};

struct CardInfo {
    std::string id;
    std::uint32_t uid = 0;
    int level = 0;
    int evolve = 0;
    int potential = 0;
    int skill_index = 0;
    int skill_level = 10;
    int cost = -1;
    int remaining = -1;
    double respawn_remaining = 0;
    double respawn_progress = 0;
    int state = -1;
    bool ready = false;
    bool hidden = false;
    bool deployed = false;
    bool dont_occupy_deploy_cnt = false;
    std::vector<TalentInfo> talents;
    std::vector<ModuleInfo> modules;
};

struct UnitInfo {
    std::string id;
    std::string tmpl_id;
    std::uint32_t uid = 0;
    std::uint32_t card_uid = 0;
    std::uint32_t owner_uid = 0;
    double hp = 0;
    double max_hp = 0;
    double minus_hp = 0;
    double max_minus_hp = 0;
    double hp_effective = 0;
    double shield = 0;
    int shield_layers = 0;
    double atk = 0;
    double def = 0;
    double magic_resistance = 0;
    double attack_speed = 0;
    double move_speed = 0;
    double sp = 0;
    int max_sp = 0;
    int block_count = 0;
    int unit_cost = -1;
    bool mine = false;
    bool alive = false;
    bool has_skill = false;
    bool in_skill = false;
    bool skill_affecting = false;
    bool skill_begin_anim = false;
    double skill_remaining = 0;
    int skill_trigger_count = 0;
    int skill_ammo_remaining = -1;
    int skill_ammo_max = -1;
    int current_state_id = -1;
    bool skill_activatable = false;
    bool sp_enough = false;
    bool skill_op_triggerable = false;
    bool skill_overload_skill = false;
    bool skill_overloaded = false;
    bool skill_retriggerable = false;
    bool withdrawable = false;
    bool manually_withdrawable = false;
    std::vector<TalentInfo> talents;
    std::vector<ModuleInfo> modules;
    struct BuffInfo {
        std::string key;
        std::string template_key;
        std::string override_key;
        std::string effect_key;
        std::string source_id;
        std::string modifiers;
        std::vector<BuffModifierInfo> modifier_entries;
        std::string abnormal_flags;
        std::string abnormal_immune_flags;
        std::string abnormal_anti_flags;
        std::string abnormal_combo_flags;
        std::string abnormal_combo_immune_flags;
        std::string blackboard;
        std::vector<BuffBlackboardEntry> blackboard_entries;
        std::uint32_t uid = 0;
        std::int64_t abnormal_flag_mask = 0;
        std::int64_t abnormal_immune_mask = 0;
        std::int64_t abnormal_anti_mask = 0;
        std::int64_t abnormal_combo_mask = 0;
        std::int64_t abnormal_combo_immune_mask = 0;
        int priority = 0;
        int event_priority = 0;
        int stack_count = 0;
        int valid_stack_count = 0;
        int max_stack_count = 0;
        int trigger_count = 0;
        double life_time = 0;
        double remaining_time = 0;
        double existing_time = 0;
        double remaining_ratio = 0;
        double trigger_interval = 0;
        bool enabled = false;
        bool finished = false;
        bool valid = false;
        bool triggerable = false;
        bool status_resistable = false;
        bool has_shield = false;
    };
    std::vector<BuffInfo> buffs;
};

struct EnemySkillInfo {
    std::string key;
    std::string signal_id;
    std::string range_id;
    int priority = 0;
    int trigger_count = 0;
    int max_trigger_count = 0;
    int sp_cost = 0;
    double cooldown = 0;
    double cooldown_total = 0;
    bool enabled = false;
    bool used_up = false;
    bool casting = false;
    bool affecting = false;
};

struct EnemySkillDefinition {
    std::string key;
    int priority = 0;
    int sp_cost = 0;
    double cooldown = 0;
    double initial_cooldown = 0;
};

struct EnemyInfo {
    std::uint32_t serial = 0;
    std::string id;
    std::uint32_t uid = 0;
    std::uint32_t owner_uid = 0;
    double hp = 0;
    double max_hp = 0;
    double shield = 0;
    double atk = 0;
    double def = 0;
    double magic_resistance = 0;
    double attack_speed = 0;
    double move_speed = 0;
    double created_time = 0;
    int row = -1;
    int col = -1;
    int block_count = 0;
    int mass_level = 0;
    bool alive = false;
    bool boss = false;
    bool giant_boss = false;
    std::vector<EnemySkillInfo> skills;
    std::vector<UnitInfo::BuffInfo> buffs;
};

struct EnemyProfile {
    std::string id;
    std::string name;
    std::string index;
    std::string description;
    std::string race;
    std::string attack_type;
    std::string ability;
    std::vector<std::string> tags;
    std::vector<std::string> abilities;
    std::vector<std::string> link_enemies;
    std::vector<int> damage_types;
    std::vector<EnemySkillDefinition> skills;
    int level_type = 0;
    int sort_id = 0;
    int life_point_reduce = 0;
    int max_hp = 0;
    int atk = 0;
    int def = 0;
    int mass_level = 0;
    double magic_resistance = 0;
    double move_speed = 0;
    double base_attack_time = 0;
    double ep_damage_resistance = 0;
    double ep_resistance = 0;
    bool stun_immune = false;
    bool sleep_immune = false;
    bool frozen_immune = false;
    bool levitate_immune = false;
    bool disarmed_combat_immune = false;
    bool feared_immune = false;
    bool palsy_immune = false;
    bool attract_immune = false;
    bool teleport_immune = false;
    bool ground_bound_immune = false;
    std::string source;
};

struct EnemyWaveAction {
    std::string key;
    int count = 0;
    int route = -1;
    double pre_delay = 0;
    double interval = 0;
    double first_time = 0;
};

struct EnemyWave {
    int index = 0;
    int spawned = 0;
    int total = 0;
    double pre_delay = 0;
    std::vector<EnemyWaveAction> actions;
};

struct UnitProfile {
    std::uint32_t card_uid = 0;
    std::string operator_id;
    std::string name;
    std::string char_id;
    std::string skin_id;
    int level = 0;
    int evolve = 0;
    int potential = 0;
    std::string skill_id;
    std::string skill_name;
    std::string skill_description;
    std::string skill_icon_id;
    int sp_cost = 0;
    int max_sp = 0;
    double skill_duration = 0;
    int skill_type = -1;
    int skill_duration_type = -1;
    std::string avatar_id;
    std::string portrait_id;
    std::string illust_id;
    std::string dyn_illust_id;
    std::string art_file;
    std::string art_error;
    int art_x = 0;
    int art_y = 0;
    int art_w = 0;
    int art_h = 0;
    int art_tex_w = 0;
    int art_tex_h = 0;
};

inline constexpr std::size_t kDamageTypeCount = 4;

struct DamageTotals {
    double actual = 0;
    double calculated = 0;
};

struct DamageSeries {
    std::uint32_t step_frames = 1;
    std::vector<std::uint32_t> frames;
    std::vector<double> total;
    std::vector<double> healing;
    std::vector<double> taken;
    std::array<std::vector<double>, kDamageTypeCount> types;
};

struct DamageActorInfo {
    std::string id;
    std::string name;
    double actual = 0;
    double calculated = 0;
    double healing = 0;
    double healing_received = 0;
    double regen = 0;
    double taken = 0;
    double hp_start = 0;
    double hp_current = 0;
    double hp_max = 0;
    double minus_hp = 0;
    double max_minus_hp = 0;
    double hp_effective_start = 0;
    double hp_effective_current = 0;
    std::uint32_t hp_uid = 0;
    bool hp_tracked = false;
    bool hp_locked = false;
    double hp_event_delta = 0;
    std::uint32_t hp_delta_events = 0;
    std::uint32_t hp_delta_fallbacks = 0;
    double elemental_buildup = 0;
    std::array<DamageTotals, kDamageTypeCount> types{};
    std::vector<double> curve;
    std::vector<double> healing_curve;
    std::vector<double> taken_curve;
    std::size_t curve_points = 0;
};

struct DamageSnapshot {
    bool available = false;
    bool started = false;
    bool ended = false;
    bool partial = false;
    bool overrun = false;
    bool curves_included = false;
    std::uint64_t battle = 0;
    std::uint64_t sequence = 0;
    std::uint64_t event_count = 0;
    std::uint64_t revision = 0;
    std::uint64_t series_revision = 0;
    bool frame_valid = false;
    std::uint32_t start_frame = 0;
    std::uint32_t end_frame = 0;
    std::uint32_t duration_frames = 0;
    double duration = 0;
    double total_actual = 0;
    double total_calculated = 0;
    double total_healing = 0;
    double total_regen = 0;
    double total_taken = 0;
    double elemental_buildup = 0;
    std::array<DamageTotals, kDamageTypeCount> types{};
    DamageSeries series;
    std::size_t series_points = 0;
    std::vector<DamageActorInfo> actors;
};

struct DamageTracker {
    bool started = false;
    bool ended = false;
    bool partial = false;
    bool overrun = false;
    std::uint64_t battle = 0;
    std::uint64_t sequence = 0;
    std::uint64_t event_count = 0;
    std::uint64_t revision = 0;
    std::uint64_t series_revision = 0;
    bool frame_valid = false;
    std::uint32_t start_frame = 0;
    std::uint32_t current_frame = 0;
    double start_time = 0;
    double game_time = 0;
    double last_event_time = 0;
    double last_clock_time = 0;
    std::uint32_t last_event_kind = 0;
    double total_actual = 0;
    double total_calculated = 0;
    double total_healing = 0;
    double total_regen = 0;
    double total_taken = 0;
    double elemental_buildup = 0;
    std::array<DamageTotals, kDamageTypeCount> types{};
    DamageSeries series;
    std::unordered_map<std::string, DamageActorInfo> actors;
};

struct BattleSnapshot {
    bool available = false;
    bool playing = false;
    bool paused = false;
    int state = -1;
    int cost = -1;
    std::uint32_t frame = 0;
    std::uint32_t epoch = 0;
    std::string stage_id;
    std::string stage_input;
    std::string stage_code;
    std::string stage_name;
    std::string error;
    std::vector<CardInfo> cards;
    std::vector<UnitInfo> units;
    std::vector<EnemyInfo> enemies;
    std::vector<EnemyProfile> enemy_profiles;
    std::vector<EnemyWave> enemy_waves;
    std::vector<GlobalBuffGroupInfo> global_buffs;
    RogueContextInfo rogue;
    int enemy_wave_current = 0;
    int enemy_wave_total = 0;
    std::uint32_t enemy_focus_uid = 0;
    bool enemy_focus_active = false;
    DamageSnapshot damage;
};

struct StageInfo {
    std::string id;
    std::string code;
    std::string name;
    std::string level_id;
    bool valid = false;
};

struct BattleScene {
    std::uint64_t revision = 0;
    void* controller = nullptr;
    std::unique_ptr<il2cpp::Runtime::Root> controller_root;
    std::string stage_request;
    StageInfo stage;
    bool stage_pending = false;
    int player_side = 0;
    bool side_ready = false;
    bool launched_by_us = false;
};

struct GameModeConfig {
    int mode_type = 0;
    bool is_deterministic = false;
    bool allow_manual_tick = false;
    int player_side = 0;
    bool valid = false;
};

struct BattleStart {
    bool rogue = false;
    std::string stage;
    std::string path;
    std::vector<SquadSlot> squad;
    std::vector<std::pair<std::string, int>> relics;
    std::vector<std::pair<std::string, int>> buff_items;
    struct CharBuffSpec {
        std::string id;
        int layer = 1;
        std::vector<std::string> targets;
    };
    std::vector<CharBuffSpec> char_buffs;
    struct MasterSpec {
        std::string char_id;
        std::string master_id;
        int level = 1;
    };
    std::vector<MasterSpec> masters;
    std::vector<std::tuple<std::string, std::string, int>> extra_buffs;
    std::vector<std::string> tech_buffs;
    std::vector<std::tuple<std::string, std::string, double>> outer_buffs;
    struct OuterBuffField {
        std::string key;
        std::string value;
    };
    struct OuterBuffSpec {
        std::string key;
        std::vector<OuterBuffField> fields;
    };
    std::vector<OuterBuffSpec> outer_buff_specs;
    int mode = 0;
    int grade = 0;
    int growth = 0;
    int hp_bonus = 0;
    int max_hp_bonus = 0;
    int shield_bonus = 0;
    int gold_bonus = 0;
    int scrap_count = -1;
    int scrap_limit = -1;
    int rogue_seed = 12345;
    bool official_extra = false;
    GameModeConfig game_mode;
};

struct Command {
    std::string name;
    std::string args;
};

class Args {
public:
    explicit Args(std::string text) : text_(std::move(text)) {}

    bool empty() const;
    std::string next();
    std::string rest();
    int integer();
    std::uint32_t uid();

private:
    std::string text_;
    std::size_t position_ = 0;
};

struct ReplayMeta {
    int schema = 1;
    std::string source;
    std::string stage_id;
    std::string stage_code;
    std::string stage_name;
    std::string level_id;
    int seed = -1;
    int seed_observed = -1;
    int seed_applied = -1;
    bool rogue = false;
    GameModeConfig game_mode;
    BattleStart start;
    std::int64_t created_unix_ms = 0;
};

struct TraceUnit {
    TraceUnit() = default;
    TraceUnit(il2cpp::Runtime& runtime, void* object, void* skill, std::uint32_t uid,
              std::uint32_t card_uid)
        : root(runtime, object),
          skill(runtime, skill),
          uid(uid),
          card_uid(card_uid) {}

    il2cpp::Runtime::Root root;
    il2cpp::Runtime::Root skill;
    std::uint32_t uid = 0;
    std::uint32_t card_uid = 0;
};

struct TraceSkillMethods {
    void* affecting = nullptr;
    void* remaining = nullptr;
    void* trigger_count = nullptr;
    void* begin_anim = nullptr;
};

struct ProbeBuff {
    std::string key;
    std::string template_key;
    std::string override_key;
    std::string effect_key;
    std::string source_id;
    std::string blackboard;
    std::string abnormal_flags;
    std::uint32_t uid = 0;
    int stack_count = 0;
    int trigger_count = 0;
    double remaining_time = 0;
    bool enabled = false;
    bool finished = false;
};

struct ProbeEntity {
    std::string key;
    std::string class_name;
    std::string id;
    std::string tmpl_id;
    std::string tile_key;
    std::uint32_t uid = 0;
    std::uint32_t card_uid = 0;
    double hp = 0;
    double max_hp = 0;
    double atk = 0;
    double def = 0;
    double magic_resistance = 0;
    double attack_speed = 0;
    double move_speed = 0;
    double sp = 0;
    int max_sp = 0;
    int block_count = 0;
    int unit_cost = -1;
    int state_id = -1;
    bool mine = false;
    bool alive = false;
    std::map<std::string, ProbeBuff> buffs;
};

struct ProbeTile {
    std::string key;
    std::string tile_key;
    std::string effect;
    std::string blackboard;
    std::string shared_effects;
    int tile_types = 0;
    int tile_info = 0;
    int effect_specs = 0;
};

struct RecordedOperation {
    enum class Kind {
        Deploy,
        Skill,
        Withdraw,
    };

    Kind kind = Kind::Deploy;
    std::string unit;
    int direction = 0;
    int row = 0;
    int col = 0;
};

struct ReplayEvent {
    std::uint32_t frame = 0;
    std::string actor;
    std::string target;
    std::uint8_t type = 0;
    double actual = 0;
};

inline constexpr std::uint8_t kReplayEventHeal = 4;
inline constexpr std::uint8_t kReplayEventTaken = 5;
inline constexpr std::uint8_t kReplayEventRegen = 6;
inline constexpr char kNoSourceActorId[] = "__no_source__";
inline constexpr std::uint8_t kWorldEventCost = 1;
inline constexpr std::uint8_t kWorldEventWave = 2;

struct ReplayWorldEvent {
    std::uint32_t frame = 0;
    std::uint8_t kind = 0;
    int value = 0;
    int total = 0;
};

struct ReplayTreeNode {
    std::string id;
    std::string parent_id;
    std::string name;
    std::uint32_t fork_frame = 0;
    std::uint32_t end_frame = 0;
    std::uint32_t branch_no = 1;
    std::map<std::uint32_t, std::vector<RecordedOperation>> operations;
    std::map<std::uint32_t, std::vector<ReplayEvent>> observed_events;
    std::map<std::uint32_t, std::vector<ReplayEvent>> expected_events;
    std::map<std::uint32_t, std::vector<ReplayWorldEvent>>
        observed_world_events;
    std::map<std::uint32_t, std::vector<ReplayWorldEvent>>
        expected_world_events;
};

struct ReplayTree {
    std::string root_id;
    std::string current_id;
    std::vector<std::string> active_path;
    std::vector<ReplayTreeNode> nodes;
};

enum class PendingTimelineMode {
    None,
    NewRoot,
    ImportedReplay,
    Jump,
};

struct SeedState {
    int requested = -1;
    int observed = -1;
    int applied = -1;
    bool from_replay = false;
    bool apply_done = false;

    bool captured() const { return observed >= 0; }
    int effective() const { return applied >= 0 ? applied : observed; }
};

struct TimelineSession {
    struct Pending {
        PendingTimelineMode mode = PendingTimelineMode::None;
        BattleStart start;
        StageInfo stage;
        int seed = -1;
        std::string source_file;
        bool tas_allowed = true;
        bool jump = false;
        bool fast = false;
        std::string parent_id;
        std::uint32_t fork_frame = 0;
        bool has_document = false;
        ReplayMeta meta;
        ReplayTree tree;
    };

    BattleStart start;
    StageInfo stage;
    SeedState seed;
    std::string source;
    std::string source_file;
    std::int64_t created_unix_ms = 0;
    bool tas_allowed = true;
    bool metadata_captured = false;
    bool waiting_for_controller = false;
    bool controller_absent = false;
    std::uint64_t scene_revision = 0;
    std::map<std::string, ReplayTreeNode> nodes;
    std::string root_id;
    std::string current_id;
    std::vector<std::string> playback_path;
    std::size_t playback_segment = 0;
    std::string pending_playback_branch;
    ReplayMeta replay_meta;
    bool replay_meta_valid = false;
    std::string replay_source_path;
    std::vector<std::pair<std::uint32_t,
                          std::vector<RecordedOperation>>>
        replay_plan;
    bool replay_verify_events = true;
    std::uint64_t stale_scene_revision = 0;
    std::uint32_t next_node = 1;
    std::uint32_t next_branch = 2;
    std::uint64_t revision = 0;
    std::atomic<std::uint32_t> advance_target{0};
    std::atomic<std::uint32_t> advance_frames{0};
    Pending pending;
};

class LocalBattle {
public:
    using InstanceFixedGetter = std::int64_t (*)(void*, void*);
    using InstanceBoolGetter = bool (*)(void*, void*);
    using InstanceIntGetter = int (*)(void*, void*);
    using StaticFrameGetter = std::uint32_t (*)(void*);

    explicit LocalBattle(const ArkPluginApiV1& host);
    ~LocalBattle();

    LocalBattle(const LocalBattle&) = delete;
    LocalBattle& operator=(const LocalBattle&) = delete;

    void frame();
    bool can_unload() const;
    bool shutdown();
    void set_bridge(const ArkExternalBridgeApiV1* bridge);

private:
    static void game_task(void* user);
    static void on_global_update(void* self, void* method);
    static void on_battle_fixed_update(void* self, void* method);
    static void on_battle_finish_game(void* self, int result, bool show,
                                      void* callback, void* method);
    static void* on_player_data_get(void* self, void* method);
    static void on_player_data_changed(void* self, void* model, void* delta,
                                       void* method);
    static void on_player_data_notify(void* self, void* model, void* delta,
                                      void* method);
    static void* on_preprocess_global_buff(void* self, void* method);
    static bool on_record_player_spawn(void* self, std::uint32_t uid, int direction,
                                       void* tile, void* method);
    static bool on_record_player_skill(void* self, void* character, void* skill_id,
                                       void* method);
    static bool on_record_player_withdraw(void* self, void* character, void* method);
    static void on_record_character_located(void* self, void* character, void* method);
    static void on_replay_fixed_update(void* self, void* method);
    static void on_spine_manual_update(void* self, bool enabled, void* method);

    void log(std::uint32_t level, const std::string& message) const;
    void connect_if_ready();
    void process_request_file();
    void write_status_file(bool publish_web = true);
    void schedule_next_work();
    void run_command(const Command& command);
    void maybe_schedule_refresh();
    bool ensure_frame_clock();
    bool adopt_scene(void* controller);
    void clear_scene();
    std::uint32_t scene_frame();
    void clear_static_cache();
    void poll_battle_events();
    void update_unit_health_ledger(const std::vector<UnitInfo>& units,
                                   std::uint32_t frame);
    void sync_frame_track(std::uint32_t frame);
    DamageSnapshot damage_snapshot(bool include_curves) const;

    void load_catalog();
    void resolve_stage(const std::string& stage_id);
    void start_battle(const BattleStart& start);
    void refresh_battle();
    void refresh_enemies(void* controller, BattleSnapshot& snapshot);
    void refresh_global_buffs(void* controller, BattleSnapshot& snapshot);
    void refresh_rogue_context(void* controller, BattleSnapshot& snapshot);
    const EnemyProfile& enemy_profile(const std::string& id);
    void enemy_profile_probe(const std::string& id);
    void read_enemy_waves(void* controller, BattleSnapshot& snapshot);
    std::string enemy_name(const std::string& id);
    void focus_enemy(std::uint32_t uid, int duration_ms);
    void test_enemy_outline(std::uint32_t uid);
    void update_enemy_focus(void* controller);
    void clear_enemy_focus();
    void enemy_wave_probe();
    void read_talents_cached(void* data, void* data_class, int potential,
                             std::vector<TalentInfo>& output);
    void read_character_talents_cached(void* character, void* character_class,
                                       std::vector<TalentInfo>& output);
    void prune_buff_static_cache();
    void list_tiles(std::uint32_t uid);
    void deploy(std::uint32_t uid, int row, int col, int direction);
    void withdraw(std::uint32_t uid);
    void trigger_skill(std::uint32_t uid);
    void inspect_skill(std::uint32_t uid);
    void list_skill_methods(std::uint32_t uid);
    void inspect_type(const std::string& text, const std::string& path);
    void search_types(const std::string& text);
    void scan_objects(const std::string& text, const std::string& path);
    void static_scan(const std::string& text, const std::string& path);
    void ref_scan(const std::string& text, const std::string& path);
    void root_bfs(const std::string& text, const std::string& path, int value);
    void method_at(const std::string& text);
    void singleton_scan(const std::string& text, const std::string& path);
    void probe_rogue_extra(const std::string& text);
    void rogue_synth(
        const std::string& text,
        const std::vector<std::tuple<std::string, std::string, int>>& extra_buffs);
    void* player_data_model();
    void rogue_out_buffs(const std::string& text);
    void rogue_player_dump();
    void rogue_player_json();
    void rogue_pending_detail();
    void rogue_net_capture(const std::string& text);
    void capture_player_delta(void* delta);
    void maybe_queue_pending_capture();
    void rogue_capture(const std::string& text);
    void rogue_dump_battle();
    void rogue_dump_global_buffs();
    void queue_global_buff_capture(void* list);
    void capture_queued_global_buffs();
    void debug_buff(const std::string& text, int value);
    void probe_rogue_data(const std::string& text);
    void inspect_field_type(const std::string& text, const std::string& path,
                            const std::string& name);
    void rogue_catalog(const std::string& path);
    void rogue_start(const BattleStart& start);
    void game_mode_info();
    void inspect_buff_fields(std::uint32_t uid);
    void dump_buffs(std::uint32_t uid);
    void dispatch_buff_post_born(std::uint32_t uid, const std::string& path);
    void start_recording();
    void stop_recording();
    void queue_timeline_start(PendingTimelineMode mode, BattleStart start,
                              const std::string& stage_id, int seed,
                              const std::string& source_file,
                              bool tas_allowed = true);
    std::pair<std::uint32_t, std::uint32_t> active_operation_stats() const;
    void write_recording();
    void flush_recording();
    void timeline_action(const std::string& action, const std::string& name,
                         const std::string& label, int value);
    void timeline_jump(std::uint32_t target, const std::string& node_id,
                       bool fast);
    bool build_timeline_jump_plan(std::uint32_t target, const std::string& node_id,
                                  std::string* parent_id);
    void activate_pending_timeline();
    void begin_timeline_session(bool imported);
    void begin_timeline_session_from(
        const std::string& stage_id, std::vector<SquadSlot> squad,
        BattleStart start, bool rogue, bool tas_allowed,
        const std::string& source, const std::string& source_file,
        bool imported, int seed_to_apply, int seed_observed,
        int seed_applied, bool seed_info_locked);
    bool ensure_timeline_for_controller(void* controller);
    bool prepare_timeline_for_controller(void* controller);
    void remember_previous_controller_for_restart();
    void prepare_timeline_seed(void* controller);
    void capture_timeline_metadata(void* controller);
    ReplayMeta replay_meta_locked() const;
    void complete_timeline_jump(std::uint32_t frame, bool from_replay = false);
    void timeline_fast_forward(std::uint32_t frames);
    void complete_timeline_fast_forward_if_due(void* controller);
    void cancel_timeline_fast_forward();
    bool run_fast_timeline_jump(void* controller);
    bool ensure_spine_manual_hook();
    void note_timeline_frame(std::uint32_t frame);
    std::string timeline_node_for_frame_locked(std::uint32_t frame) const;
    void record_timeline_event_locked(std::uint32_t frame,
                                      const std::string& actor,
                                      const std::string& target, std::uint8_t type,
                                      double actual);
    void record_timeline_world_event_locked(std::uint32_t frame,
                                            std::uint8_t kind, int value,
                                            int total);
    void sample_world_events(void* controller, std::uint32_t frame, int cost);
    void reset_world_sampling();
    void verify_replay_events();
    void publish_timeline();
    std::string build_timeline_json();
    std::string build_timeline_json_locked();
    std::string build_replay_document_json(bool include_events = true);
    std::string build_replay_document_json_locked(bool include_events);
    void save_replay_file(const std::string& path, bool include_events);
    void start_replay(const std::string& path, std::uint32_t seek_target = 0,
                      bool seek_fast = false, const std::string& branch_id = {});
    void start_replay_plan(
        const ReplayMeta& meta, const std::string& label,
        const std::string& source_path,
        std::vector<std::pair<std::uint32_t, std::vector<RecordedOperation>>> plan,
        std::uint32_t seek_target, bool seek_fast,
        const std::string& origin_node, ReplayTree tree = {},
        bool verify_events = true);
    void stop_replay();
    void replay_seek(std::uint32_t target, bool fast,
                     const std::string& node_id = {});
    bool run_fast_replay_seek(void* controller, std::uint32_t target);
    void fork_replay_for_manual_operation(std::uint32_t frame);
    std::vector<std::string> timeline_path_to_root_locked(
        const std::string& node_id) const;
    void set_playback_path_locked(const std::string& node_id);
    void advance_playback_segment_locked(std::uint32_t frame);
    std::string playback_node_for_frame_locked(std::uint32_t frame);
    std::string playback_fork_parent_locked(std::uint32_t frame);
    bool same_recorded_operation(const RecordedOperation& left,
                                 const RecordedOperation& right) const;
    bool consume_matching_replay_operation(const RecordedOperation& operation,
                                           std::uint32_t frame);
    void reset_replay_consumed();
    bool replay_operation_consumed(std::size_t step,
                                   std::size_t index) const;
    bool load_replay_plan(const std::string& path);
    bool install_replay_hook();
    bool remove_replay_hook();
    void replay_fixed_before();
    void dispatch_replay_operation(const RecordedOperation& operation);
    void* replay_card(void* controller, const std::string& unit, std::uint32_t* uid);
    void* replay_character(void* controller, const std::string& unit);
    bool install_record_hooks();
    bool remove_record_hooks();
    bool install_player_data_hook();
    bool install_player_data_delta_hook();
    bool install_player_data_notify_hook();
    bool install_preprocess_global_buff_hook();
    void capture_global_buff_list(void* list);
    std::string last_global_buffs_json() const;
    void record_operation(const RecordedOperation& operation, std::uint32_t frame);
    void record_character_located(void* controller, void* character, std::uint32_t frame);
    std::string recorded_card_unit_id(void* controller, std::uint32_t uid);
    std::string recorded_character_id(void* character);
    bool recorded_tile_position(void* controller, void* tile, int* row, int* col);
    void start_trace(const std::string& fields);
    void stop_trace();
    void close_trace();
    void trace_frame();
    void trace_writer_loop(std::ofstream output);
    void queue_trace_line(std::string line);
    void start_probe(int interval_frames = 10);
    void stop_probe();
    void close_probe();
    void probe_frame(void* controller);
    void probe_battle_start(void* controller);
    void probe_snapshot(void* controller, bool force);
    void probe_write(std::string line);
    void probe_writer_loop(std::ofstream output);
    std::string probe_entity_json(const ProbeEntity& entity) const;
    std::string probe_tile_json(const ProbeTile& tile) const;
    ProbeEntity probe_read_entity(void* object);
    ProbeTile probe_read_tile(void* tile, int row, int col);
    void set_paused(bool paused);
    void leave_battle();
    bool route_home_from_finish();
    void describe_withdraw();
    void tas_bench(const std::string& text, const std::string& path, int value);
    void enforce_spine_auto(void* controller = nullptr);

    void cmd_start(Args& args);
    void cmd_deploy(Args& args);
    void cmd_tiles(Args& args);
    void cmd_skill(Args& args);
    void cmd_skill_state(Args& args);
    void cmd_skill_methods(Args& args);
    void cmd_type_inspect(Args& args);
    void cmd_type_search(Args& args);
    void cmd_object_scan(Args& args);
    void cmd_static_scan(Args& args);
    void cmd_ref_scan(Args& args);
    void cmd_root_bfs(Args& args);
    void cmd_method_at(Args& args);
    void cmd_singleton_scan(Args& args);
    void cmd_rogue_probe_extra(Args& args);
    void cmd_rogue_synth(Args& args);
    void cmd_rogue_data(Args& args);
    void cmd_rogue_out_buffs(Args& args);
    void cmd_rogue_player_dump(Args& args);
    void cmd_rogue_player_json(Args& args);
    void cmd_rogue_pending_detail(Args& args);
    void cmd_rogue_net_capture(Args& args);
    void cmd_rogue_capture(Args& args);
    void cmd_rogue_dump_battle(Args& args);
    void cmd_rogue_dump_global_buffs(Args& args);
    void cmd_rogue_global_capture(Args& args);
    void cmd_debug_buff(Args& args);
    void cmd_field_type(Args& args);
    void cmd_rogue_catalog(Args& args);
    void cmd_rogue_start(Args& args);
    void cmd_game_mode(Args& args);
    void cmd_buff_fields(Args& args);
    void cmd_buffs(Args& args);
    void cmd_buff_post_born(Args& args);
    void cmd_record(Args& args);
    void cmd_timeline(Args& args);
    void cmd_replay(Args& args);
    void cmd_trace(Args& args);
    void cmd_probe(Args& args);
    void cmd_withdraw(Args& args);
    void cmd_pause(Args& args);
    void cmd_resume(Args& args);
    void cmd_leave(Args& args);
    void cmd_enemy_focus(Args& args);
    void cmd_enemy_outline_test(Args& args);
    void cmd_enemy_profile(Args& args);
    void cmd_enemy_waves(Args& args);
    void cmd_tas_bench(Args& args);
    void cmd_refresh(Args& args);
    void cmd_interval(Args& args);
    void cmd_catalog(Args& args);
    void cmd_asset(Args& args);
    void cmd_withdraw_signature(Args& args);
    void cmd_export_web(Args& args);

    void publish_result(const std::string& result, bool ok = true);
    void publish_snapshot(BattleSnapshot snapshot);
    void publish_web_snapshot(const BattleSnapshot& snapshot);
    void publish_profiles();
    void export_web(const std::string& prefix);
    void ensure_unit_profile(void* character, const UnitInfo& unit);
    void ensure_card_profile(void* data, const CardInfo& card);
    void ensure_profile_from_data(void* data, std::uint32_t key,
                                  const std::string& operator_id);
    void export_asset(const std::string& kind, const std::string& id);
    std::string build_web_json(const BattleSnapshot& snapshot) const;
    std::string build_profiles_json() const;
    void publish_catalog();
    std::string operator_name(const std::string& id) const;
    std::string operator_label(const std::string& id) const;

    const ArkPluginApiV1& host_;
    const ArkExternalBridgeApiV1* bridge_ = nullptr;
    il2cpp::Runtime runtime_;
    ClassIds classes_;
    BattleEventCapture battle_capture_;

    mutable std::mutex mutex_;
    std::deque<Command> pending_;
    std::atomic<bool> work_running_{false};
    std::atomic<bool> stopping_{false};
    bool pump_ready_ = false;
    bool catalog_requested_ = false;
    std::uint64_t pump_hook_ = 0;
    void* original_global_update_ = nullptr;
    std::uint64_t battle_observer_hook_ = 0;
    void* battle_observer_original_ = nullptr;
    std::uint64_t finish_game_hook_ = 0;
    void* finish_game_original_ = nullptr;
    std::uint64_t player_data_hook_ = 0;
    void* player_data_original_ = nullptr;
    std::atomic<void*> player_data_instance_{nullptr};
    std::uint64_t player_data_delta_hook_ = 0;
    void* player_data_delta_original_ = nullptr;
    std::uint64_t player_data_notify_hook_ = 0;
    void* player_data_notify_original_ = nullptr;
    std::uint64_t preprocess_global_buff_hook_ = 0;
    void* preprocess_global_buff_original_ = nullptr;
    std::atomic<bool> rogue_global_capture_{false};
    mutable std::mutex global_buff_mutex_;
    std::string last_global_buffs_json_;
    int last_global_buffs_count_ = 0;
    std::uint64_t last_global_buffs_tick_ = 0;
    std::mutex pending_global_mutex_;
    std::unique_ptr<il2cpp::Runtime::Root> pending_global_list_root_;
    std::atomic<bool> pending_global_capture_{false};
    std::atomic<bool> player_data_delta_active_{false};
    std::mutex player_data_delta_mutex_;
    void* last_captured_delta_ = nullptr;
    std::uint64_t last_captured_delta_tick_ = 0;
    std::string last_pending_capture_index_;
    std::atomic<bool> finish_home_pending_{false};
    std::atomic<bool> local_battle_active_{false};
    bool finish_bypassed_ = false;
    std::uint64_t next_connect_tick_ = 0;
    std::uint64_t next_refresh_tick_ = 0;
    std::uint64_t next_catalog_tick_ = 0;
    std::uint64_t next_frame_clock_retry_tick_ = 0;
    std::uint64_t next_preprocess_retry_tick_ = 0;
    std::atomic<std::uint64_t> refresh_interval_ms_{33};
    std::atomic<std::uint32_t> refresh_frame_interval_{1};
    std::atomic<std::uint64_t> status_interval_ms_{1000};
    std::uint64_t next_status_tick_ = 0;
    void* frame_clock_method_ = nullptr;
    StaticFrameGetter frame_clock_get_ = nullptr;
    bool last_refresh_frame_valid_ = false;
    std::uint32_t last_refresh_frame_ = 0;
    bool perf_last_frame_valid_ = false;
    std::uint32_t perf_last_frame_ = 0;
    std::uint64_t next_request_poll_tick_ = 0;
    std::uint64_t next_record_flush_tick_ = 0;
    DamageTracker damage_tracker_;
    std::uint64_t next_damage_series_tick_ = 0;
    std::uint64_t damage_series_published_revision_ = 0;

    struct PerfCounter {
        static constexpr std::size_t kSampleCapacity = 512;
        struct Percentiles {
            double p50 = 0;
            double p95 = 0;
            double p99 = 0;
        };

        std::uint64_t count = 0;
        std::uint64_t total_us = 0;
        std::uint64_t last_us = 0;
        std::uint64_t max_us = 0;
        std::array<std::uint32_t, kSampleCapacity> samples{};
        std::uint32_t sample_count = 0;
        std::uint32_t sample_cursor = 0;

        void add(std::uint64_t value) {
            ++count;
            total_us += value;
            last_us = value;
            if (value > max_us) {
                max_us = value;
            }
            samples[sample_cursor] = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(value, std::numeric_limits<std::uint32_t>::max()));
            sample_cursor = (sample_cursor + 1) % kSampleCapacity;
            if (sample_count < kSampleCapacity) {
                ++sample_count;
            }
        }

        double average_us() const {
            return count ? static_cast<double>(total_us) / static_cast<double>(count) : 0.0;
        }

        Percentiles percentiles_us() const {
            if (!sample_count) {
                return {};
            }
            std::array<std::uint32_t, kSampleCapacity> sorted = samples;
            const auto end = sorted.begin() + sample_count;
            std::sort(sorted.begin(), end);
            const auto at = [&](double percentile) {
                const std::size_t index = static_cast<std::size_t>(
                    std::clamp(percentile, 0.0, 1.0) *
                    static_cast<double>(sample_count - 1));
                return static_cast<double>(sorted[index]);
            };
            return {at(0.50), at(0.95), at(0.99)};
        }
    };

    struct PerfStats {
        PerfCounter refresh;
        PerfCounter discover;
        PerfCounter cards;
        PerfCounter units;
        PerfCounter buffs;
        PerfCounter snapshot_copy;
        PerfCounter status;
        PerfCounter web_json;
        PerfCounter web_publish;
        PerfCounter timeline_json;
        PerfCounter record_write;
        PerfCounter spine_auto;
        PerfCounter frame_tick;
        PerfCounter fixed_tick;
        PerfCounter damage;
        std::uint64_t card_hits = 0;
        std::uint64_t card_misses = 0;
        std::uint64_t unit_hits = 0;
        std::uint64_t unit_misses = 0;
        std::uint64_t talent_hits = 0;
        std::uint64_t talent_misses = 0;
        std::uint64_t buff_hits = 0;
        std::uint64_t buff_misses = 0;
        std::uint64_t frame_delta_total = 0;
        std::uint32_t frame_delta_samples = 0;
        std::uint32_t last_frame_delta = 0;
        std::uint64_t web_bytes_last = 0;
        std::uint64_t web_bytes_total = 0;
        std::uint64_t web_publish_skipped = 0;
        std::uint64_t timeline_bytes_last = 0;
        std::uint64_t timeline_publish_skipped = 0;
    };

    PerfStats perf_;
    std::mutex web_payload_mutex_;
    std::string last_web_json_;
    std::string last_timeline_json_;

    bool rogue_capture_queued_ = false;
    std::uint32_t rogue_context_revision_ = 0xFFFFFFFFu;
    bool rogue_context_ready_ = false;
    RogueContextInfo rogue_context_cache_;
    BattleScene scene_;
    std::uint64_t card_scene_revision_ = 0;
    bool card_objects_ready_ = false;
    std::vector<il2cpp::Runtime::Root> card_roots_;

    std::unordered_map<void*, CardInfo> card_static_cache_;
    std::unordered_map<std::uint32_t, UnitInfo> unit_static_cache_;
    std::unordered_map<std::uint32_t, std::uint32_t> enemy_serials_;
    std::uint32_t next_enemy_serial_ = 1;
    std::uint64_t enemy_scene_revision_ =
        std::numeric_limits<std::uint64_t>::max();
    mutable std::unordered_map<std::string, std::string> enemy_name_cache_;
    std::unordered_map<std::string, EnemyProfile> enemy_profile_cache_;
    void* enemy_tile_cache_source_ = nullptr;
    std::unordered_map<void*, std::pair<int, int>> enemy_tile_cache_;
    void* enemy_wave_plan_source_ = nullptr;
    bool enemy_wave_plan_cached_ = false;
    int enemy_wave_plan_total_ = 0;
    std::vector<EnemyWave> enemy_wave_plan_cache_;
    std::uint32_t focused_enemy_uid_ = 0;
    std::uint64_t focused_enemy_until_ = 0;
    std::unique_ptr<il2cpp::Runtime::Root> focused_enemy_root_;
    std::unique_ptr<il2cpp::Runtime::Root> focused_animator_root_;
    void* focused_animator_ = nullptr;
    std::unique_ptr<il2cpp::Runtime::Root> focused_outline_manager_root_;
    void* focused_outline_manager_ = nullptr;
    bool focused_outline_ = false;
    struct FocusedRendererState {
        std::unique_ptr<il2cpp::Runtime::Root> root;
        void* renderer = nullptr;
        int sorting_order = 0;
    };
    struct FocusedMaterialState {
        std::unique_ptr<il2cpp::Runtime::Root> root;
        void* material = nullptr;
        int render_queue = 0;
    };
    std::vector<FocusedRendererState> focused_renderers_;
    std::vector<FocusedMaterialState> focused_materials_;
    bool enemy_wave_probe_pending_ = false;
    std::map<std::pair<void*, int>, std::vector<TalentInfo>> talent_cache_;
    std::map<std::pair<void*, std::uint32_t>, BuffStaticInfo> buff_static_cache_;
    std::uint64_t buff_cache_generation_ = 0;

    std::atomic<bool> record_active_{false};
    std::atomic<bool> timeline_record_pending_{false};
    std::atomic<bool> record_dirty_{false};
    std::atomic<bool> timeline_dirty_{false};
    mutable std::mutex record_mutex_;
    std::mutex record_write_mutex_;
    using TimelineNode = ReplayTreeNode;
    TimelineSession timeline_;
    std::map<std::string, TimelineNode>& timeline_nodes_ = timeline_.nodes;
    std::string& timeline_root_id_ = timeline_.root_id;
    std::string& timeline_current_id_ = timeline_.current_id;
    std::string& timeline_stage_id_ = timeline_.stage.id;
    std::string& timeline_stage_code_ = timeline_.stage.code;
    std::string& timeline_stage_name_ = timeline_.stage.name;
    std::string& timeline_level_id_ = timeline_.stage.level_id;
    std::string& timeline_source_ = timeline_.source;
    std::string& timeline_source_file_ = timeline_.source_file;
    std::int64_t& timeline_created_unix_ms_ = timeline_.created_unix_ms;
    std::vector<SquadSlot>& timeline_squad_ = timeline_.start.squad;
    BattleStart& timeline_start_ = timeline_.start;
    bool& timeline_rogue_ = timeline_.start.rogue;
    bool& timeline_tas_allowed_ = timeline_.tas_allowed;
    int& timeline_seed_observed_ = timeline_.seed.observed;
    int& timeline_seed_applied_ = timeline_.seed.applied;
    int& timeline_seed_to_apply_ = timeline_.seed.requested;
    bool& timeline_seed_info_locked_ = timeline_.seed.from_replay;
    bool& timeline_seed_apply_done_ = timeline_.seed.apply_done;
    GameModeConfig& timeline_game_mode_ = timeline_.start.game_mode;
    bool& timeline_metadata_captured_ = timeline_.metadata_captured;
    bool& timeline_waiting_for_controller_ = timeline_.waiting_for_controller;
    bool& timeline_controller_absent_ = timeline_.controller_absent;
    void* timeline_stale_controller_ = nullptr;
    std::uint64_t& timeline_stale_scene_revision_ =
        timeline_.stale_scene_revision;
    PendingTimelineMode& pending_timeline_mode_ = timeline_.pending.mode;
    std::string& pending_timeline_stage_id_ = timeline_.pending.stage.id;
    std::string& pending_timeline_source_file_ = timeline_.pending.source_file;
    std::vector<SquadSlot>& pending_timeline_squad_ =
        timeline_.pending.start.squad;
    BattleStart& pending_timeline_start_ = timeline_.pending.start;
    bool& pending_timeline_rogue_ = timeline_.pending.start.rogue;
    bool& pending_timeline_tas_allowed_ = timeline_.pending.tas_allowed;
    int& pending_timeline_seed_to_apply_ = timeline_.pending.seed;
    bool& pending_timeline_jump_ = timeline_.pending.jump;
    bool& pending_timeline_fast_ = timeline_.pending.fast;
    std::string& pending_timeline_parent_id_ = timeline_.pending.parent_id;
    std::uint32_t& pending_timeline_fork_frame_ = timeline_.pending.fork_frame;
    std::atomic<std::uint32_t>& timeline_advance_target_ =
        timeline_.advance_target;
    std::atomic<std::uint32_t>& timeline_advance_frames_ =
        timeline_.advance_frames;
    std::uint32_t& timeline_next_node_ = timeline_.next_node;
    std::uint32_t& timeline_next_branch_ = timeline_.next_branch;
    std::uint64_t& timeline_revision_ = timeline_.revision;
    bool record_capture_suppressed_ = false;
    struct PendingDeploy {
        std::uint32_t frame = 0;
        std::uint32_t uid = 0;
        int row = 0;
        int col = 0;
        int direction = 0;
        void* tile = nullptr;
        std::uint64_t serial = 0;
    };
    std::vector<PendingDeploy> recorded_pending_deploys_;
    std::uint64_t recorded_pending_serial_ = 0;
    void* record_entity_class_ = nullptr;
    void* record_character_class_ = nullptr;
    void* record_frame_method_ = nullptr;
    StaticFrameGetter record_frame_get_ = nullptr;
    std::uint64_t record_hooks_[4]{};
    void* record_spawn_original_ = nullptr;
    void* record_skill_original_ = nullptr;
    void* record_withdraw_original_ = nullptr;
    void* record_located_original_ = nullptr;

    std::atomic<bool> replay_active_{false};
    std::atomic<bool> replay_branch_armed_{false};
    std::vector<std::pair<std::uint32_t, std::vector<RecordedOperation>>> replay_steps_;
    std::vector<std::vector<bool>> replay_consumed_;
    std::size_t replay_next_step_ = 0;
    std::uint32_t replay_dispatched_ = 0;
    std::uint32_t replay_frame_ = 0;
    std::atomic<std::uint32_t> replay_stop_frame_{0};
    std::atomic<bool> replay_stop_enabled_{false};
    std::atomic<bool> pending_replay_seek_fast_{false};
    std::atomic<std::uint32_t> pending_replay_seek_target_{0};
    std::vector<std::string>& playback_path_ = timeline_.playback_path;
    std::size_t& playback_segment_ = timeline_.playback_segment;
    std::string& pending_playback_branch_id_ =
        timeline_.pending_playback_branch;
    ReplayMeta& replay_meta_ = timeline_.replay_meta;
    bool& replay_meta_valid_ = timeline_.replay_meta_valid;
    std::string& replay_source_path_ = timeline_.replay_source_path;
    ReplayTree current_tree_snapshot_locked(
        const std::string& current_id) const;
    bool build_plan_for_nodes_path_locked(
        const std::string& node_id,
        std::vector<std::pair<std::uint32_t,
                              std::vector<RecordedOperation>>>* plan) const;
    std::atomic<bool> timeline_fast_running_{false};
    std::string replay_file_;
    bool replay_has_event_baseline_ = false;
    bool replay_has_world_event_baseline_ = false;
    bool replay_event_check_valid_ = false;
    bool replay_event_mismatch_ = false;
    std::uint32_t replay_event_check_frame_ = 0;
    std::uint64_t replay_hook_ = 0;
    void* replay_fixed_original_ = nullptr;
    void* replay_frame_method_ = nullptr;
    StaticFrameGetter replay_frame_get_ = nullptr;
    std::uint64_t spine_hook_ = 0;
    void* spine_manual_original_ = nullptr;
    std::atomic<bool> tas_mode_{true};
    std::atomic<bool> tas_spine_auto_{true};
    std::uint64_t spine_scene_revision_ =
        std::numeric_limits<std::uint64_t>::max();
    std::uint32_t spine_scan_tick_ = 0;
    std::map<std::pair<void*, std::uint32_t>, bool> spine_manual_done_;
    std::atomic<int> tas_seed_{20261104};
    bool world_sampling_valid_ = false;
    int world_sampling_cost_ = -1;
    int world_sampling_wave_ = 0;
    int world_sampling_total_waves_ = 0;
    int world_sampling_enemies_ = 0;
    std::int64_t world_sampling_wave_start_ = 0;
    int world_sampling_wave_start_index_ = 0;
    bool trace_active_ = false;
    bool trace_has_frame_ = false;
    std::thread trace_writer_;
    std::mutex trace_queue_mutex_;
    std::condition_variable trace_queue_ready_;
    std::deque<std::string> trace_queue_;
    bool trace_writer_stop_ = false;
    std::uint64_t trace_dropped_lines_ = 0;
    std::uint32_t trace_fields_ = 0;
    std::uint64_t trace_scene_revision_ =
        std::numeric_limits<std::uint64_t>::max();
    void* trace_frame_method_ = nullptr;
    void* trace_state_method_ = nullptr;
    void* trace_paused_method_ = nullptr;
    void* trace_playing_method_ = nullptr;
    void* trace_cost_method_ = nullptr;
    void* trace_hp_method_ = nullptr;
    void* trace_sp_method_ = nullptr;
    void* trace_max_sp_method_ = nullptr;
    void* trace_in_skill_method_ = nullptr;
    void* trace_state_id_method_ = nullptr;
    void* trace_alive_method_ = nullptr;
    InstanceFixedGetter trace_hp_get_ = nullptr;
    InstanceFixedGetter trace_sp_get_ = nullptr;
    InstanceBoolGetter trace_in_skill_get_ = nullptr;
    InstanceIntGetter trace_state_id_get_ = nullptr;
    InstanceBoolGetter trace_alive_get_ = nullptr;
    InstanceBoolGetter trace_paused_get_ = nullptr;
    InstanceBoolGetter trace_playing_get_ = nullptr;
    InstanceIntGetter trace_state_get_ = nullptr;
    InstanceIntGetter trace_max_sp_get_ = nullptr;
    StaticFrameGetter trace_frame_get_ = nullptr;
    std::uint32_t trace_last_frame_ = 0;
    std::string trace_fields_text_;
    bool trace_last_paused_ = false;
    bool trace_last_playing_ = false;
    int trace_last_state_value_ = -1;
    int trace_last_cost_ = -1;
    int trace_side_ = 0;
    std::string trace_last_state_;
    std::string trace_last_units_;
    std::vector<TraceUnit> trace_units_;
    std::unordered_map<void*, TraceSkillMethods> trace_skill_methods_;
    unsigned int trace_changes_ = 0;

    std::atomic<bool> probe_enabled_{false};
    std::atomic<bool> probe_active_{false};
    std::atomic<int> probe_interval_frames_{10};
    std::uint32_t probe_last_frame_ = 0;
    std::uint32_t probe_last_tile_frame_ = 0;
    bool probe_has_frame_ = false;
    bool probe_has_tile_snapshot_ = false;
    std::uint64_t probe_scene_revision_ =
        std::numeric_limits<std::uint64_t>::max();
    std::uint64_t probe_battle_id_ = 0;
    std::map<std::string, ProbeEntity> probe_entities_;
    std::map<std::string, ProbeTile> probe_tiles_;
    std::deque<std::string> probe_queue_;
    std::mutex probe_queue_mutex_;
    std::condition_variable probe_queue_ready_;
    std::thread probe_writer_;
    bool probe_writer_stop_ = false;
    std::uint64_t probe_dropped_ = 0;

    std::vector<OperatorEntry> operators_;
    std::unordered_map<std::string, std::size_t> operator_index_;
    std::map<std::uint32_t, UnitProfile> profiles_;
    bool profiles_dirty_ = false;
    BattleSnapshot snapshot_;
    std::string last_result_;
    bool last_ok_ = true;
    std::string catalog_error_;

    std::vector<SquadSlot> squad_;
    char operator_filter_[128] = "";
    char manual_operator_[128] = "";
    int manual_level_ = 90;
    int manual_evolve_ = 2;
    int manual_potential_ = 0;
    int manual_skill_ = 1;
    int manual_skill_level_ = 10;
    int catalog_selection_ = -1;
};

inline std::atomic<LocalBattle*> fixed_observer_owner{nullptr};
inline std::atomic<LocalBattle*> finish_game_owner{nullptr};
inline std::atomic<LocalBattle*> player_data_owner{nullptr};
inline std::atomic<LocalBattle*> player_data_delta_owner{nullptr};
inline std::atomic<LocalBattle*> preprocess_global_buff_owner{nullptr};

} // namespace ark::better_battle
