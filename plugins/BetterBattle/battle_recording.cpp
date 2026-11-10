#include "battle_helpers.h"
#include "replay_document.h"

#include <chrono>
#include <nlohmann/json.hpp>
#include <set>

namespace ark::better_battle {

namespace {

using RecordedFrameMap =
    std::map<std::uint32_t, std::vector<RecordedOperation>>;

ReplayDocument read_replay_document_file(const std::string& path,
                                         const std::string& root_directory) {
    std::filesystem::path input(path);
    if (input.is_relative()) {
        input = std::filesystem::path(root_directory) / input;
    }
    std::ifstream stream(input, std::ios::in | std::ios::binary);
    if (!stream) {
        throw std::runtime_error("无法打开回放文件: " + input.string());
    }
    std::string text((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
    ReplayDocument document;
    std::string error;
    if (!parse_replay_document(text, document, &error)) {
        throw std::runtime_error("回放 JSON 解析失败: " + error);
    }
    return document;
}

bool build_plan_for_tree_path(const ReplayTree& tree,
                              const std::string& node_id,
                              RecordedFrameMap* plan) {
    if (!plan || node_id.empty()) {
        return false;
    }
    std::map<std::string, const ReplayTreeNode*> nodes;
    for (const ReplayTreeNode& node : tree.nodes) {
        nodes.emplace(node.id, &node);
    }
    const auto selected = nodes.find(node_id);
    if (selected == nodes.end()) {
        return false;
    }
    std::vector<const ReplayTreeNode*> path;
    for (const ReplayTreeNode* cursor = selected->second;;) {
        path.push_back(cursor);
        if (cursor->parent_id.empty()) {
            break;
        }
        const auto parent = nodes.find(cursor->parent_id);
        if (parent == nodes.end()) {
            return false;
        }
        cursor = parent->second;
    }
    std::reverse(path.begin(), path.end());
    plan->clear();
    for (const ReplayTreeNode* node : path) {
        for (const auto& [frame, operations] : node->operations) {
            auto& merged = (*plan)[frame];
            merged.insert(merged.end(), operations.begin(), operations.end());
        }
    }
    return true;
}

bool set_internal_pause(il2cpp::Runtime& runtime, void* controller,
                        void* controller_class, bool paused) {
    try {
        void* pause_key = runtime.nested(
            runtime.find("Torappu.Battle", "Consts"), "BattlePauseKey");
        if (!pause_key) {
            return false;
        }
        const int key = runtime.static_value<int>(pause_key, "UI_CONTROLLER");
        void* method = runtime.method(controller_class, "SetPaused", 2);
        bool paused_value = paused;
        int key_value = key;
        runtime.invoke(method, controller, {&paused_value, &key_value});
        return true;
    } catch (...) {
        return false;
    }
}

std::uint32_t numeric_node_suffix(const std::string& id) {
    if (id.size() < 2 || id.front() != 'n') {
        return 0;
    }
    std::uint64_t value = 0;
    for (std::size_t index = 1; index < id.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(id[index]))) {
            return 0;
        }
        value = value * 10 + static_cast<unsigned>(id[index] - '0');
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            return 0;
        }
    }
    return static_cast<std::uint32_t>(value);
}

void normalize_branch_numbers(
    std::map<std::string, ReplayTreeNode>& nodes) {
    std::unordered_set<std::string> roots;
    for (const auto& [id, node] : nodes) {
        if (node.parent_id.empty()) {
            roots.insert(id);
        }
    }
    std::uint32_t next = 1;
    std::unordered_set<std::string> visited;
    const auto visit = [&](const auto& self, const std::string& id) -> void {
        if (id.empty() || !visited.insert(id).second) {
            return;
        }
        auto found = nodes.find(id);
        if (found == nodes.end()) {
            return;
        }
        ReplayTreeNode& node = found->second;
        const std::uint32_t previous = node.branch_no;
        node.branch_no = next++;
        const std::string previous_tl = "TL" + std::to_string(previous);
        const std::string previous_l = "L" + std::to_string(previous);
        const std::string current_tl = "TL" + std::to_string(node.branch_no);
        if (node.name == previous_tl || node.name == previous_l) {
            node.name = current_tl;
        } else if (node.name.rfind(previous_tl + " · ", 0) == 0) {
            node.name = current_tl + node.name.substr(previous_tl.size());
        }

        std::vector<std::pair<std::uint32_t, std::string>> children;
        for (const auto& [child_id, child] : nodes) {
            if (child.parent_id == id) {
                children.emplace_back(child.fork_frame, child_id);
            }
        }
        std::sort(children.begin(), children.end());
        for (const auto& [frame, child_id] : children) {
            (void)frame;
            self(self, child_id);
        }
    };

    std::vector<std::string> ordered_roots(roots.begin(), roots.end());
    std::sort(ordered_roots.begin(), ordered_roots.end(),
              [&](const std::string& left, const std::string& right) {
                  const std::uint32_t left_frame =
                      nodes[left].fork_frame;
                  const std::uint32_t right_frame =
                      nodes[right].fork_frame;
                  return left_frame != right_frame
                             ? left_frame < right_frame
                             : left < right;
              });
    for (const std::string& root : ordered_roots) {
        visit(visit, root);
    }
    for (const auto& [id, node] : nodes) {
        if (!visited.count(id)) {
            visit(visit, id);
        }
    }
}

void normalize_timeline_ranges(
    std::map<std::string, ReplayTreeNode>& nodes) {
    for (auto& [id, node] : nodes) {
        (void)id;
        bool has_children = false;
        std::uint32_t child_fork =
            std::numeric_limits<std::uint32_t>::max();
        for (const auto& [child_id, child] : nodes) {
            (void)child_id;
            if (child.parent_id == node.id) {
                has_children = true;
                child_fork = std::min(child_fork, child.fork_frame);
            }
        }
        if (has_children) {
            node.end_frame = child_fork;
        }
    }
}

std::int64_t unix_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

using Json = nlohmann::ordered_json;

bool read_value_field_copy(il2cpp::Runtime& runtime, void* object, void* klass,
                           const char* name, std::vector<unsigned char>& bytes,
                           void** value_class) {
    if (!object || !klass || !value_class) {
        return false;
    }
    void* field = find_field_deep(runtime, klass, name);
    if (!field) {
        return false;
    }
    void* field_class = runtime.class_from_type(runtime.field_type(field));
    if (!field_class || !runtime.class_is_value(field_class)) {
        return false;
    }
    std::uint32_t alignment = 0;
    const int size = runtime.class_value_size(field_class, &alignment);
    if (size <= 0) {
        return false;
    }
    bytes.assign(static_cast<std::size_t>(size), 0);
    const int offset = runtime.field_offset(field);
    if (offset < 0) {
        return false;
    }
    std::memcpy(bytes.data(),
                static_cast<const unsigned char*>(object) + offset,
                static_cast<std::size_t>(size));
    *value_class = field_class;
    return true;
}

bool read_fixed_field_raw(il2cpp::Runtime& runtime, void* object,
                          void* klass, const char* name,
                          std::int64_t* value) {
    if (!value) {
        return false;
    }
    std::vector<unsigned char> bytes;
    void* value_class = nullptr;
    if (!read_value_field_copy(runtime, object, klass, name, bytes,
                               &value_class)) {
        return false;
    }
    void* field = find_field_deep(runtime, value_class, "_serializedValue");
    if (!field) {
        return false;
    }
    const int offset = runtime.field_offset(field) - 16;
    if (offset < 0 ||
        static_cast<std::size_t>(offset) + sizeof(std::int64_t) >
            bytes.size()) {
        return false;
    }
    std::memcpy(value, bytes.data() + offset, sizeof(std::int64_t));
    return true;
}

std::string read_struct_string(il2cpp::Runtime& runtime, void* klass,
                               const std::vector<unsigned char>& bytes,
                               const char* name) {
    void* field = find_field_deep(runtime, klass, name);
    if (!field) {
        return {};
    }
    const int offset = runtime.field_offset(field) - 16;
    if (offset < 0 || static_cast<std::size_t>(offset) + sizeof(void*) >
                          bytes.size()) {
        return {};
    }
    void* value = nullptr;
    read_at(bytes.data() + offset, &value);
    return value ? runtime.string(value) : std::string{};
}

int read_struct_int(il2cpp::Runtime& runtime, void* klass,
                    const std::vector<unsigned char>& bytes, const char* name,
                    int fallback = 0) {
    void* field = find_field_deep(runtime, klass, name);
    if (!field) {
        return fallback;
    }
    const int offset = runtime.field_offset(field) - 16;
    if (offset < 0 ||
        static_cast<std::size_t>(offset) + sizeof(int) > bytes.size()) {
        return fallback;
    }
    int value = fallback;
    read_at(bytes.data() + offset, &value);
    return value;
}

struct CapturedStageMetadata {
    std::string stage_id;
    std::string stage_code;
    std::string stage_name;
    std::string level_id;
};

GameModeConfig capture_game_mode_metadata(il2cpp::Runtime& runtime) {
    GameModeConfig result;
    try {
        void* controller_class =
            runtime.find("Torappu.Battle", "BattleController");
        void* mode_class = runtime.find("Torappu.Battle", "GameModeMeta");
        void* cached_field =
            find_field_deep(runtime, controller_class, "s_cachedModeMeta");
        if (!controller_class || !mode_class || !cached_field) {
            return result;
        }
        std::uint32_t alignment = 0;
        const int size = runtime.class_value_size(mode_class, &alignment);
        if (size < 12) {
            return result;
        }
        std::vector<unsigned char> cached(static_cast<std::size_t>(size), 0);
        runtime.field_static_get(cached_field, cached.data());
        result.mode_type = static_cast<int>(cached[0]);
        result.is_deterministic = cached[1] != 0;
        result.allow_manual_tick = cached[2] != 0;
        std::memcpy(&result.player_side, cached.data() + 4, sizeof(int));
        result.valid = true;
    } catch (...) {
    }
    return result;
}

CapturedStageMetadata capture_stage_metadata(il2cpp::Runtime& runtime) {
    CapturedStageMetadata result;
    try {
        void* in_out_class = runtime.find("Torappu.Battle", "BattleInOut");
        void* in_params_class = runtime.nested(in_out_class, "InParams");
        void* stage_info_class = runtime.find("Torappu.Battle", "BattleStageInfo");
        if (!in_out_class || !in_params_class || !stage_info_class) {
            return result;
        }
        void* in_out = runtime.singleton(in_out_class);
        if (!in_out) {
            return result;
        }
        Root in_out_root(runtime, in_out);
        void* input = runtime.get_object(in_out, "<input>k__BackingField");
        if (!input) {
            return result;
        }
        Root input_root(runtime, input);
        std::vector<unsigned char> stage_bytes;
        void* value_class = nullptr;
        if (!read_value_field_copy(runtime, input, in_params_class, "stageInfo",
                                   stage_bytes, &value_class) ||
            value_class != stage_info_class) {
            return result;
        }
        result.stage_id =
            read_struct_string(runtime, stage_info_class, stage_bytes, "stageId");
        result.stage_code =
            read_struct_string(runtime, stage_info_class, stage_bytes, "code");
        result.stage_name =
            read_struct_string(runtime, stage_info_class, stage_bytes, "name");
        result.level_id =
            read_struct_string(runtime, stage_info_class, stage_bytes, "levelId");
    } catch (...) {
    }
    return result;
}

std::vector<SquadSlot> capture_squad_metadata(il2cpp::Runtime& runtime,
                                              void* controller) {
    std::vector<SquadSlot> result;
    if (!controller) {
        return result;
    }
    try {
        void* controller_class = runtime.find("Torappu.Battle", "BattleController");
        void* player_data_class =
            runtime.find("Torappu.Battle", "BattlePlayerData");
        void* character_data_class =
            runtime.find("Torappu.Battle", "BattleCharacterData");
        void* skill_data_class = runtime.find("Torappu", "SkillData");
        if (!controller_class || !player_data_class || !character_data_class) {
            return result;
        }
        void* players = nullptr;
        if (!read_field_deep(runtime, controller, controller_class,
                             "m_playerDataList", &players) ||
            !players) {
            return result;
        }
        Root players_root(runtime, players);
        std::unordered_set<std::string> seen;
        const int player_count = runtime.count(players);
        for (int player_index = 0; player_index < player_count; ++player_index) {
            void* player = nullptr;
            try {
                player = runtime.at(players, player_index);
            } catch (...) {
                continue;
            }
            if (!player) {
                continue;
            }
            Root player_root(runtime, player);
            void* characters = nullptr;
            if (!read_field_deep(runtime, player, player_data_class, "characters",
                                 &characters) ||
                !characters) {
                continue;
            }
            Root characters_root(runtime, characters);
            const int character_count = runtime.count(characters);
            for (int character_index = 0;
                 character_index < character_count &&
                 result.size() < 12;
                 ++character_index) {
                void* character = nullptr;
                try {
                    character = runtime.at(characters, character_index);
                } catch (...) {
                    continue;
                }
                if (!character) {
                    continue;
                }
                Root character_root(runtime, character);
                bool is_token = false;
                read_field_deep(runtime, character, character_data_class,
                                "isToken", &is_token);
                if (is_token) {
                    continue;
                }
                SquadSlot slot;
                slot.id = read_string_field(runtime, character,
                                             character_data_class, "prefabKey");
                if (slot.id.empty()) {
                    slot.id = method_string(runtime, character,
                                            character_data_class, "GetPrefabKey");
                }
                if (slot.id.empty() || !seen.insert(slot.id).second) {
                    continue;
                }
                read_field_deep(runtime, character, character_data_class, "level",
                                &slot.level);
                read_field_deep(runtime, character, character_data_class,
                                "evolvePhase", &slot.evolve);
                read_field_deep(runtime, character, character_data_class,
                                "potentialRank", &slot.potential);
                int favor_phase = 0;
                if (read_field_deep(runtime, character, character_data_class,
                                    "favorBattlePhase", &favor_phase)) {
                    slot.trust = std::clamp(favor_phase * 2, 0, 200);
                }
                read_field_deep(runtime, character, character_data_class,
                                "mainSkillIndex", &slot.skill_index);
                std::vector<unsigned char> skill_bytes;
                void* skill_value_class = nullptr;
                if (skill_data_class &&
                    read_value_field_copy(runtime, character, character_data_class,
                                          "mainSkill", skill_bytes,
                                          &skill_value_class) &&
                    skill_value_class == skill_data_class) {
                    slot.skill_level =
                        read_struct_int(runtime, skill_data_class, skill_bytes,
                                        "level", slot.skill_level);
                }
                std::vector<ModuleInfo> modules;
                read_modules_from_data(runtime, character, character_data_class,
                                       modules);
                if (!modules.empty()) {
                    slot.module_id = modules.front().id;
                    slot.module_level = modules.front().level;
                }
                result.push_back(std::move(slot));
            }
        }
    } catch (...) {
    }
    return result;
}

}  // namespace

std::string LocalBattle::recorded_character_id(void* character) {
    if (!character || !record_entity_class_) {
        return {};
    }
    std::string id = read_string_field(
        runtime_, character, record_entity_class_, "<id>k__BackingField");
    if (id.empty()) {
        id = read_string_field(
            runtime_, character, record_entity_class_, "<tmplId>k__BackingField");
    }
    return id;
}

std::string LocalBattle::recorded_card_unit_id(void* controller, std::uint32_t uid) {
    if (!controller || !uid) {
        return {};
    }
    void* controller_class = classes_.battle_controller;
    void* deck_class = classes_.deck;
    void* card_class = runtime_.nested(deck_class, "Card");
    if (!controller_class || !deck_class || !card_class) {
        return {};
    }
    void* deck = nullptr;
    void* card = find_card(runtime_, controller, controller_class, uid, &deck);
    if (!card) {
        return {};
    }
    Root card_root(runtime_, card);
    void* data = nullptr;
    if (!read_field_deep(runtime_, card, card_class, "m_data", &data) || !data) {
        return {};
    }
    Root data_root(runtime_, data);
    return read_string_field(runtime_, data, runtime_.object_class(data), "prefabKey");
}

bool LocalBattle::recorded_tile_position(void* controller, void* tile, int* row, int* col) {
    if (!controller || !tile || !row || !col) {
        return false;
    }
    void* controller_class = classes_.battle_controller;
    Root controller_root(runtime_, controller);
    void* map = nullptr;
    void* get_map = find_method_optional(runtime_, controller_class, "get_map", 0);
    if (get_map) {
        map = runtime_.invoke(get_map, controller);
    }
    if (!map) {
        read_field_deep(runtime_, controller, controller_class, "_map", &map);
    }
    if (!map) {
        return false;
    }
    Root map_root(runtime_, map);
    void* map_class = runtime_.object_class(map);
    void* tiles_2d = nullptr;
    if (!read_field_deep(runtime_, map, map_class, "_tiles", &tiles_2d) || !tiles_2d) {
        return false;
    }
    Root tiles_root(runtime_, tiles_2d);
    void* tiles_class = runtime_.object_class(tiles_2d);
    int width = 0;
    int height = 0;
    read_field_deep(runtime_, tiles_2d, tiles_class, "_width", &width);
    read_field_deep(runtime_, tiles_2d, tiles_class, "_height", &height);
    void* get_tile = find_method_optional(runtime_, map_class, "GetTile", 2);
    if (width <= 0 || height <= 0 || width > 64 || height > 64 || !get_tile) {
        return false;
    }
    for (int current_row = 0; current_row < height; ++current_row) {
        for (int current_col = 0; current_col < width; ++current_col) {
            int row_value = current_row;
            int col_value = current_col;
            if (runtime_.invoke(get_tile, map, {&row_value, &col_value}) == tile) {
                *row = current_row;
                *col = current_col;
                return true;
            }
        }
    }
    return false;
}

std::pair<std::uint32_t, std::uint32_t>
LocalBattle::active_operation_stats() const {
    std::lock_guard lock(record_mutex_);
    std::uint32_t operations = 0;
    std::set<std::uint32_t> frames;
    for (const std::string& id :
         active_path_for(timeline_nodes_, timeline_current_id_)) {
        const auto node = timeline_nodes_.find(id);
        if (node == timeline_nodes_.end()) {
            continue;
        }
        for (const auto& [frame, values] : node->second.operations) {
            operations += static_cast<std::uint32_t>(values.size());
            frames.insert(frame);
        }
    }
    return {operations, static_cast<std::uint32_t>(frames.size())};
}

bool LocalBattle::same_recorded_operation(
    const RecordedOperation& left, const RecordedOperation& right) const {
    if (left.kind != right.kind || left.unit != right.unit) {
        return false;
    }
    if (left.kind == RecordedOperation::Kind::Deploy) {
        return left.direction == right.direction && left.row == right.row &&
               left.col == right.col;
    }
    return true;
}

void LocalBattle::reset_replay_consumed() {
    replay_consumed_.clear();
    replay_consumed_.resize(replay_steps_.size());
    for (std::size_t index = 0; index < replay_steps_.size(); ++index) {
        replay_consumed_[index].assign(replay_steps_[index].second.size(),
                                       false);
    }
}

bool LocalBattle::replay_operation_consumed(std::size_t step,
                                            std::size_t index) const {
    return step < replay_consumed_.size() &&
           index < replay_consumed_[step].size() &&
           replay_consumed_[step][index];
}

bool LocalBattle::consume_matching_replay_operation(
    const RecordedOperation& operation, std::uint32_t frame) {
    if (replay_steps_.empty()) {
        return false;
    }
    if (replay_consumed_.size() != replay_steps_.size()) {
        reset_replay_consumed();
    }
    for (std::size_t step = replay_next_step_; step < replay_steps_.size();
         ++step) {
        const std::uint32_t step_frame = replay_steps_[step].first;
        if (step_frame < frame) {
            continue;
        }
        if (step_frame > frame) {
            break;
        }
        auto& consumed = replay_consumed_[step];
        for (std::size_t index = 0; index < replay_steps_[step].second.size();
             ++index) {
            if (consumed[index]) {
                continue;
            }
            if (same_recorded_operation(replay_steps_[step].second[index],
                                        operation)) {
                consumed[index] = true;
                return true;
            }
        }
    }
    return false;
}

void LocalBattle::record_operation(const RecordedOperation& operation,
                                   std::uint32_t frame) {
    if (record_capture_suppressed_) {
        return;
    }
    std::lock_guard lock(record_mutex_);
    if (!record_active_.load(std::memory_order_acquire)) {
        return;
    }
    frame_actual_operations_[frame].push_back(operation);
}

std::vector<RecordedOperation> LocalBattle::expected_operations_locked(
    std::uint32_t frame) const {
    std::vector<RecordedOperation> expected;
    for (const std::string& id : playback_path_) {
        const auto node = timeline_nodes_.find(id);
        if (node == timeline_nodes_.end()) {
            continue;
        }
        const auto operations = node->second.operations.find(frame);
        if (operations != node->second.operations.end()) {
            expected.insert(expected.end(), operations->second.begin(),
                            operations->second.end());
        }
    }
    return expected;
}

bool LocalBattle::operations_equal(
    std::vector<RecordedOperation> left,
    std::vector<RecordedOperation> right) const {
    if (left.size() != right.size()) {
        return false;
    }
    for (const RecordedOperation& operation : right) {
        const auto match = std::find_if(
            left.begin(), left.end(),
            [&](const RecordedOperation& candidate) {
                return same_recorded_operation(candidate, operation);
            });
        if (match == left.end()) {
            return false;
        }
        left.erase(match);
    }
    return true;
}

void LocalBattle::timeline_frame_enter(void* controller,
                                       std::uint32_t frame) {
    if (!controller) {
        return;
    }
    if (frame_prepared_ == frame) {
        return;
    }
    frame_prepared_ = frame;

    std::vector<RecordedOperation> expected;
    {
        std::lock_guard lock(record_mutex_);
        if (frame_policy_ != TimelineFramePolicy::CompareInject) {
            return;
        }
        expected = expected_operations_locked(frame);
    }
    if (expected.empty()) {
        return;
    }

    std::vector<RecordedOperation> remaining;
    {
        std::lock_guard lock(record_mutex_);
        remaining = frame_actual_operations_[frame];
    }
    std::vector<RecordedOperation> injected;
    try {
        for (const RecordedOperation& operation : expected) {
            const auto match = std::find_if(
                remaining.begin(), remaining.end(),
                [&](const RecordedOperation& candidate) {
                    return same_recorded_operation(candidate, operation);
                });
            if (match != remaining.end()) {
                remaining.erase(match);
                continue;
            }
            dispatch_replay_operation(operation);
            injected.push_back(operation);
        }
    } catch (const std::exception& error) {
        publish_result("参考操作执行失败 F" + std::to_string(frame) +
                           "：" + error.what(),
                       false);
    }
    if (!injected.empty()) {
        std::lock_guard lock(record_mutex_);
        auto& actual = frame_actual_operations_[frame];
        actual.insert(actual.end(), injected.begin(), injected.end());
    }
}

void LocalBattle::timeline_frame_commit(std::uint32_t frame) {
    if (frame_prepared_ != frame) {
        return;
    }
    frame_prepared_ = std::numeric_limits<std::uint32_t>::max();

    std::vector<RecordedOperation> actual;
    std::vector<RecordedOperation> expected;
    TimelineFramePolicy policy = TimelineFramePolicy::Record;
    {
        std::lock_guard lock(record_mutex_);
        const auto actual_it = frame_actual_operations_.find(frame);
        if (actual_it != frame_actual_operations_.end()) {
            actual = actual_it->second;
        }
        policy = frame_policy_;
        if (policy != TimelineFramePolicy::Record) {
            expected = expected_operations_locked(frame);
        }
    }

    if (policy == TimelineFramePolicy::Record) {
        std::lock_guard lock(record_mutex_);
        const auto current = timeline_nodes_.find(timeline_current_id_);
        if (current != timeline_nodes_.end()) {
            bool has_children = false;
            for (const auto& [id, child] : timeline_nodes_) {
                (void)id;
                if (child.parent_id == current->second.id) {
                    has_children = true;
                    break;
                }
            }
            if (!has_children) {
                if (!actual.empty()) {
                    auto& operations =
                        current->second.operations[frame];
                    operations.insert(operations.end(), actual.begin(),
                                      actual.end());
                }
                current->second.end_frame =
                    std::max(current->second.end_frame, frame);
                ++timeline_revision_;
                record_dirty_.store(true, std::memory_order_release);
                timeline_dirty_.store(true, std::memory_order_release);
            }
        }
        frame_actual_operations_.erase(frame);
        return;
    }

    if (!operations_equal(actual, expected)) {
        {
            std::lock_guard lock(record_mutex_);
            frame_actual_operations_[frame] = std::move(actual);
        }
        fork_replay_for_manual_operation(frame);
        return;
    }
    std::lock_guard lock(record_mutex_);
    replay_frame_ = std::max(replay_frame_, frame);
    replay_dispatched_ +=
        static_cast<std::uint32_t>(actual.size());
    replay_next_step_ = 0;
    while (replay_next_step_ < replay_steps_.size() &&
           replay_steps_[replay_next_step_].first <= frame) {
        ++replay_next_step_;
    }
    frame_actual_operations_.erase(frame);
}

void LocalBattle::fork_replay_for_manual_operation(std::uint32_t frame) {
    replay_active_.store(false, std::memory_order_release);
    replay_stop_frame_.store(0, std::memory_order_release);
    replay_stop_enabled_.store(false, std::memory_order_release);
    pending_replay_seek_fast_.store(false, std::memory_order_release);
    pending_replay_seek_target_.store(0, std::memory_order_release);
    cancel_timeline_fast_forward();
    set_paused(true);
    tas_mode_.store(true, std::memory_order_release);
    tas_spine_auto_.store(true, std::memory_order_release);

    std::string parent_id;
    std::string segment_id;
    std::string playback_path;
    std::size_t segment_index = 0;
    std::uint32_t parent_fork = 0;
    std::uint32_t parent_end = 0;
    std::vector<RecordedOperation> actual;
    {
        std::lock_guard lock(record_mutex_);
        const auto actual_it = frame_actual_operations_.find(frame);
        if (actual_it != frame_actual_operations_.end()) {
            actual = actual_it->second;
        }
        frame_actual_operations_.clear();
        segment_id = playback_node_for_frame_locked(frame);
        parent_id = playback_fork_parent_locked(frame);
        segment_index = playback_segment_;
        for (std::size_t index = 0; index < playback_path_.size(); ++index) {
            if (index) {
                playback_path += " > ";
            }
            playback_path += playback_path_[index];
        }
        const auto parent = timeline_nodes_.find(parent_id);
        if (parent != timeline_nodes_.end()) {
            parent_fork = parent->second.fork_frame;
            parent_end = parent->second.end_frame;
        }
    }
    if (parent_id.empty()) {
        publish_result("录像分叉失败：播放路径没有可用节点", false);
        return;
    }

    const std::uint32_t fork_frame = frame > 0 ? frame - 1 : 0;
    log(kLogInfo, "录像分叉：frame=" + std::to_string(frame) +
                      " segment=" + segment_id + "@" +
                      std::to_string(segment_index) + " path=[" +
                      playback_path + "] parent=" + parent_id + " [" +
                      std::to_string(parent_fork) + "," +
                      std::to_string(parent_end) + "] fork=" +
                      std::to_string(fork_frame));
    pending_timeline_parent_id_ = parent_id;
    pending_timeline_jump_ = true;
    pending_timeline_fast_ = false;
    pending_timeline_play_ = false;
    pending_timeline_fork_frame_ = fork_frame;
        frame_policy_ = TimelineFramePolicy::Record;
    complete_timeline_jump(fork_frame, true);
    if (!actual.empty()) {
        std::lock_guard lock(record_mutex_);
        const auto current = timeline_nodes_.find(timeline_current_id_);
        if (current != timeline_nodes_.end()) {
            auto& operations = current->second.operations[frame];
            operations.insert(operations.end(), actual.begin(), actual.end());
            current->second.end_frame =
                std::max(current->second.end_frame, frame);
        }
    }
    replay_file_.clear();
    replay_steps_.clear();
    reset_replay_consumed();
    replay_next_step_ = 0;
    replay_dispatched_ = 0;
    replay_frame_ = 0;
}

void LocalBattle::record_character_located(void* controller, void* character,
                                           std::uint32_t frame) {
    void* tile = nullptr;
    if (void* get_root_tile =
            find_method_optional(runtime_, record_character_class_, "get_rootTile", 0)) {
        tile = runtime_.invoke(get_root_tile, character);
    }
    if (!tile) {
        read_field_deep(runtime_, character, record_entity_class_,
                        "<originTile>k__BackingField", &tile);
    }
    if (!tile) {
        return;
    }
    int row = 0;
    int col = 0;
    if (!recorded_tile_position(controller, tile, &row, &col)) {
        return;
    }

    PendingDeploy pending;
    bool matched = false;
    {
        std::lock_guard lock(record_mutex_);
        std::uint32_t card_uid = 0;
        read_field_deep(runtime_, character, record_character_class_,
                        "<cardUid>k__BackingField", &card_uid);
        const auto found = std::find_if(
            recorded_pending_deploys_.begin(), recorded_pending_deploys_.end(),
            [&](const PendingDeploy& candidate) {
                const bool same_tile = candidate.tile && candidate.tile == tile;
                const bool same_uid = candidate.uid && candidate.uid == card_uid;
                return (same_tile || same_uid) && frame >= candidate.frame &&
                       frame - candidate.frame <= 120;
            });
        if (found != recorded_pending_deploys_.end()) {
            pending = *found;
            recorded_pending_deploys_.erase(found);
            matched = true;
        }
    }
    if (!matched) {
        return;
    }

    RecordedOperation operation;
    operation.kind = RecordedOperation::Kind::Deploy;
    operation.unit = recorded_character_id(character);
    operation.direction = pending.direction;
    operation.row = row;
    operation.col = col;
    if (!operation.unit.empty()) {
        record_operation(operation, pending.frame);
    }
}

bool LocalBattle::install_record_hooks() {
    if (record_hooks_[0] && record_hooks_[1] && record_hooks_[2] && record_hooks_[3]) {
        return true;
    }
    void* controller_class = classes_.battle_controller;
    record_character_class_ = classes_.character;
    record_entity_class_ = classes_.entity;
    record_frame_method_ =
        find_method_optional(runtime_, controller_class, "get_fixedFrameCnt", 0);
    void* spawn_method =
        find_method_optional(runtime_, controller_class, "PlayerOp_Spawn", 3);
    void* skill_method =
        find_method_optional(runtime_, controller_class, "PlayerOp_TrigSkill", 2);
    void* withdraw_method =
        find_method_optional(runtime_, controller_class, "PlayerOp_Withdraw", 1);
    void* locate_method =
        find_method_optional(runtime_, controller_class, "OnCharacterLocate", 1);
    if (!record_frame_method_ || !spawn_method || !skill_method || !withdraw_method ||
        !locate_method) {
        throw std::runtime_error("录制所需的 IL2CPP 方法缺失");
    }
    const auto entry = [](void* method) {
        return *static_cast<void**>(method);
    };
    if (!il2cpp::is_executable(entry(record_frame_method_)) ||
        !il2cpp::is_executable(entry(spawn_method)) ||
        !il2cpp::is_executable(entry(skill_method)) ||
        !il2cpp::is_executable(entry(withdraw_method)) ||
        !il2cpp::is_executable(entry(locate_method))) {
        throw std::runtime_error("录制方法没有可执行入口");
    }
    record_frame_get_ = reinterpret_cast<StaticFrameGetter>(entry(record_frame_method_));

    const auto create = [&](void* method, void* detour, void** original,
                            std::uint64_t* hook) {
        *hook = host_.create_native_hook(host_.owner, entry(method), detour, original);
        if (!*hook || !*original) {
            throw std::runtime_error("创建录制 Hook 失败");
        }
    };
    try {
        create(spawn_method, reinterpret_cast<void*>(&LocalBattle::on_record_player_spawn),
               &record_spawn_original_, &record_hooks_[0]);
        create(skill_method, reinterpret_cast<void*>(&LocalBattle::on_record_player_skill),
               &record_skill_original_, &record_hooks_[1]);
        create(withdraw_method,
               reinterpret_cast<void*>(&LocalBattle::on_record_player_withdraw),
               &record_withdraw_original_, &record_hooks_[2]);
        create(locate_method,
               reinterpret_cast<void*>(&LocalBattle::on_record_character_located),
               &record_located_original_, &record_hooks_[3]);
        recorder_owner.store(this, std::memory_order_release);
        for (const std::uint64_t hook : record_hooks_) {
            if (!host_.enable_native_hook(host_.owner, hook, true)) {
                throw std::runtime_error("启用录制 Hook 失败");
            }
        }
    } catch (...) {
        remove_record_hooks();
        throw;
    }
    return true;
}

bool LocalBattle::remove_record_hooks() {
    record_active_.store(false, std::memory_order_release);
    bool all_removed = true;
    for (std::uint64_t& hook : record_hooks_) {
        if (!hook) {
            continue;
        }
        if (host_.remove_native_hook(host_.owner, hook)) {
            hook = 0;
        } else {
            all_removed = false;
        }
    }
    if (all_removed) {
        recorder_owner.store(nullptr, std::memory_order_release);
        record_spawn_original_ = nullptr;
        record_skill_original_ = nullptr;
        record_withdraw_original_ = nullptr;
        record_located_original_ = nullptr;
    }
    return all_removed;
}

void LocalBattle::begin_timeline_session(bool imported) {
    begin_timeline_session_from(
        pending_timeline_stage_id_, pending_timeline_squad_,
        pending_timeline_start_, pending_timeline_rogue_,
        pending_timeline_tas_allowed_,
        imported ? "replay" : "live", pending_timeline_source_file_,
        imported, pending_timeline_seed_to_apply_,
        imported && timeline_.pending.has_document
            ? timeline_.pending.meta.seed_observed
            : -1,
        imported && timeline_.pending.has_document
            ? timeline_.pending.meta.seed_applied
            : -1,
        imported && timeline_.pending.has_document);
}

void LocalBattle::begin_timeline_session_from(
    const std::string& stage_id, std::vector<SquadSlot> squad,
    BattleStart start, bool rogue, bool tas_allowed, const std::string& source,
    const std::string& source_file, bool imported, int seed_to_apply,
    int seed_observed, int seed_applied, bool seed_info_locked) {
    try {
        cancel_timeline_fast_forward();
        std::lock_guard lock(record_mutex_);
        timeline_nodes_.clear();
        recorded_pending_deploys_.clear();
        record_capture_suppressed_ = false;
        timeline_stage_id_ = stage_id;
        timeline_squad_ = std::move(squad);
        start.squad = timeline_squad_;
        if (start.stage.empty()) {
            start.stage = stage_id;
        }
        timeline_game_mode_ = start.game_mode;
        timeline_start_ = std::move(start);
        timeline_start_.game_mode = timeline_game_mode_;
        timeline_rogue_ = rogue;
        timeline_tas_allowed_ = tas_allowed;
        timeline_seed_observed_ = seed_observed;
        timeline_seed_applied_ = seed_applied;
        timeline_seed_to_apply_ = seed_to_apply;
        timeline_seed_info_locked_ = seed_info_locked;
        timeline_seed_apply_done_ = seed_to_apply < 0 && seed_info_locked;
        timeline_metadata_captured_ = false;
        timeline_stage_code_.clear();
        timeline_stage_name_.clear();
        timeline_level_id_.clear();
        timeline_created_unix_ms_ = unix_time_ms();
        const bool has_document = imported && timeline_.pending.has_document;
        if (has_document) {
            timeline_stage_code_ = timeline_.pending.meta.stage_code;
            timeline_stage_name_ = timeline_.pending.meta.stage_name;
            timeline_level_id_ = timeline_.pending.meta.level_id;
            timeline_created_unix_ms_ = timeline_.pending.meta.created_unix_ms;
        }
        timeline_waiting_for_controller_ = true;
        timeline_controller_absent_ = false;
        timeline_.scene_revision = 0;
        timeline_source_ = source;
        timeline_source_file_ = source_file;
        timeline_next_branch_ = 2;
        timeline_revision_ = 1;
        tas_mode_.store(true, std::memory_order_release);
        tas_spine_auto_.store(true, std::memory_order_release);
        if (has_document && !timeline_.pending.tree.nodes.empty()) {
            bool event_baseline = false;
            bool world_event_baseline = false;
            for (const ReplayTreeNode& source_node :
                 timeline_.pending.tree.nodes) {
                if (!source_node.observed_events.empty()) {
                    event_baseline = true;
                }
                if (!source_node.observed_world_events.empty()) {
                    world_event_baseline = true;
                }
                timeline_nodes_.emplace(source_node.id, source_node);
            }
            for (auto& [id, node] : timeline_nodes_) {
                (void)id;
                node.expected_events = std::move(node.observed_events);
                node.observed_events.clear();
                node.expected_world_events =
                    std::move(node.observed_world_events);
                node.observed_world_events.clear();
            }
            replay_has_event_baseline_ = event_baseline;
            replay_has_world_event_baseline_ =
                world_event_baseline;
            normalize_branch_numbers(timeline_nodes_);
            normalize_timeline_ranges(timeline_nodes_);
            timeline_root_id_ = timeline_.pending.tree.root_id;
            timeline_current_id_ = timeline_.pending.tree.current_id;
            std::uint32_t next_node = timeline_next_node_;
            std::uint32_t next_branch = 2;
            for (const auto& [id, node] : timeline_nodes_) {
                next_node = std::max(next_node, numeric_node_suffix(id) + 1);
                next_branch = std::max(next_branch, node.branch_no + 1);
            }
            timeline_next_node_ = std::max(timeline_next_node_, next_node);
            timeline_next_branch_ = next_branch;
        } else {
            replay_has_event_baseline_ = false;
            replay_has_world_event_baseline_ = false;
            timeline_root_id_ = "n" + std::to_string(timeline_next_node_++);
            timeline_current_id_ = timeline_root_id_;
            TimelineNode root;
            root.id = timeline_root_id_;
            root.branch_no = 1;
            timeline_nodes_.emplace(root.id, std::move(root));
        }
        if (!pending_playback_branch_id_.empty()) {
            if (timeline_nodes_.find(pending_playback_branch_id_) !=
                timeline_nodes_.end()) {
                timeline_current_id_ = pending_playback_branch_id_;
            } else {
                log(kLogWarning, "回放分支节点不存在：" +
                                     pending_playback_branch_id_);
            }
            pending_playback_branch_id_.clear();
        }
        if (imported) {
            set_playback_path_locked(timeline_current_id_);
        } else {
            playback_path_.clear();
            playback_segment_ = 0;
            timeline_.replay_plan.clear();
            timeline_.replay_verify_events = true;
        }
        replay_event_check_valid_ = false;
        replay_event_mismatch_ = false;
        frame_actual_operations_.clear();
        frame_prepared_ = std::numeric_limits<std::uint32_t>::max();
        frame_policy_ = imported ? TimelineFramePolicy::CompareInject
                                 : TimelineFramePolicy::Record;
        reset_world_sampling();
        record_active_.store(false, std::memory_order_release);
        timeline_record_pending_.store(true, std::memory_order_release);
        record_dirty_.store(true, std::memory_order_release);
        timeline_dirty_.store(true, std::memory_order_release);
        if (has_document) {
            timeline_.pending.has_document = false;
            timeline_.pending.meta = ReplayMeta{};
            timeline_.pending.tree = ReplayTree{};
        }
    } catch (const std::exception& error) {
        log(kLogWarning, std::string("建立时间轴节点失败：") + error.what());
        record_active_.store(false, std::memory_order_release);
    }
}

bool LocalBattle::ensure_timeline_for_controller(void* controller) {
    if (!controller) {
        return false;
    }
    bool create = false;
    {
        std::lock_guard lock(record_mutex_);
        if (timeline_.scene_revision == scene_.revision) {
            return true;
        }
        if (!timeline_current_id_.empty() &&
            local_battle_active_.load(std::memory_order_acquire)) {
            timeline_.scene_revision = scene_.revision;
            timeline_waiting_for_controller_ = false;
            timeline_controller_absent_ = false;
            return true;
        }
        if (timeline_waiting_for_controller_) {
            timeline_.scene_revision = scene_.revision;
            timeline_waiting_for_controller_ = false;
            timeline_controller_absent_ = false;
            return true;
        }
        if (timeline_.scene_revision == 0 && !timeline_current_id_.empty() &&
            !timeline_controller_absent_) {
            timeline_.scene_revision = scene_.revision;
            return true;
        }
        create = true;
    }
    if (!create) {
        return false;
    }
    if (pending_timeline_mode_ != PendingTimelineMode::None) {
        return false;
    }

    BattleStart start;
    begin_timeline_session_from({}, {}, std::move(start), false, false,
                                "game", {}, false, -1, -1, -1, false);
    {
        std::lock_guard lock(record_mutex_);
        timeline_.scene_revision = scene_.revision;
        timeline_waiting_for_controller_ = false;
        timeline_controller_absent_ = false;
    }
    log(kLogInfo,
        "检测到游戏入口战斗，已建立时间轴根节点；"
        "关卡与编队将在首个逻辑帧采集");
    publish_timeline();
    return true;
}

bool LocalBattle::prepare_timeline_for_controller(void* controller) {
    if (!controller) {
        return false;
    }
    timeline_stale_controller_ = nullptr;
    if (timeline_waiting_for_controller_ &&
        scene_.controller == controller) {
        timeline_waiting_for_controller_ = false;
    }
    if (scene_.controller != controller) {
        try {
            adopt_scene(controller);
        } catch (...) {
            return false;
        }
    }
    if (!ensure_timeline_for_controller(controller)) {
        return false;
    }
    {
        std::lock_guard lock(record_mutex_);
        if (timeline_.scene_revision != scene_.revision) {
            return false;
        }
    }
    if (timeline_record_pending_.exchange(false, std::memory_order_acq_rel)) {
        bool hooks_ready = false;
        try {
            hooks_ready = install_record_hooks();
        } catch (const std::exception& error) {
            log(kLogWarning,
                std::string("时间轴录制 Hook 安装失败：") + error.what());
        }
        if (hooks_ready) {
            record_active_.store(true, std::memory_order_release);
            try {
                install_replay_hook();
            } catch (const std::exception& error) {
                log(kLogWarning,
                    std::string("时间轴 FixedUpdate Hook 安装失败：") +
                        error.what());
            }
        } else {
            log(kLogWarning, "时间轴录制 Hook 未就绪，仅保留节点元数据");
        }
    }
    std::uint32_t frame = 0;
    if (record_frame_get_) {
        try {
            frame = record_frame_get_(record_frame_method_);
        } catch (...) {
        }
    }
    if (frame == 0) {
        return true;
    }
    capture_timeline_metadata(controller);
    prepare_timeline_seed(controller);
    return true;
}

void LocalBattle::remember_previous_controller_for_restart() {
    if (pending_timeline_mode_ == PendingTimelineMode::None &&
        !replay_active_.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        timeline_stale_controller_ = scene_.controller;
        timeline_stale_scene_revision_ = scene_.revision;
    }
    {
        std::lock_guard lock(record_mutex_);
        timeline_waiting_for_controller_ = true;
    }
}

void LocalBattle::capture_timeline_metadata(void* controller) {
    if (!controller) {
        return;
    }
    {
        std::lock_guard lock(record_mutex_);
        if (scene_.controller != controller || timeline_metadata_captured_) {
            return;
        }
    }
    std::string level_id;
    try {
        void* controller_class = classes_.battle_controller;
        level_id =
            method_string(runtime_, controller, controller_class, "get_levelId");
    } catch (...) {
    }
    const CapturedStageMetadata stage_info = capture_stage_metadata(runtime_);
    const std::vector<SquadSlot> captured_squad =
        capture_squad_metadata(runtime_, controller);
    const GameModeConfig captured_game_mode =
        capture_game_mode_metadata(runtime_);
    std::lock_guard lock(record_mutex_);
    if (scene_.controller != controller || timeline_metadata_captured_) {
        return;
    }
    if (!level_id.empty()) {
        timeline_level_id_ = level_id;
    }
    if (!stage_info.level_id.empty()) {
        timeline_level_id_ = stage_info.level_id;
    }
    if (!stage_info.stage_code.empty()) {
        timeline_stage_code_ = stage_info.stage_code;
    }
    if (!stage_info.stage_name.empty()) {
        timeline_stage_name_ = stage_info.stage_name;
    }
    if (!timeline_rogue_ && !stage_info.stage_id.empty() &&
        (timeline_source_ == "game" || timeline_stage_id_.empty())) {
        timeline_stage_id_ = stage_info.stage_id;
    }
    if (timeline_squad_.empty() && !captured_squad.empty()) {
        timeline_squad_ = captured_squad;
    }
    if (captured_game_mode.valid) {
        timeline_game_mode_ = captured_game_mode;
    }
    if (timeline_start_.stage.empty()) {
        timeline_start_.stage = timeline_stage_id_;
    }
    timeline_start_.squad = timeline_squad_;
    timeline_start_.game_mode = timeline_game_mode_;
    timeline_metadata_captured_ =
        !timeline_stage_id_.empty() && !timeline_level_id_.empty() &&
        !timeline_squad_.empty() && timeline_game_mode_.valid;
    timeline_dirty_.store(true, std::memory_order_release);
    ++timeline_revision_;
}

void LocalBattle::prepare_timeline_seed(void* controller) {
    if (!controller) {
        return;
    }
    bool need_capture = false;
    bool need_apply = false;
    bool info_locked = false;
    int seed_to_apply = -1;
    {
        std::lock_guard lock(record_mutex_);
        if (scene_.controller != controller) {
            return;
        }
        need_capture = !timeline_.seed.captured() && !timeline_seed_info_locked_;
        need_apply = !timeline_seed_apply_done_;
        info_locked = timeline_seed_info_locked_;
        seed_to_apply = timeline_seed_to_apply_;
        if (!need_capture && !need_apply) {
            return;
        }
    }
    int observed = -1;
    int applied = -1;
    try {
        void* controller_class = classes_.battle_controller;
        observed = method_int(runtime_, controller, controller_class,
                              "get_randomSeed", -1);
        if (need_apply) {
            if (seed_to_apply >= 0) {
                void* reset_seed =
                    find_method_optional(runtime_, controller_class, "ResetSeed", 1);
                if (!reset_seed) {
                    throw std::runtime_error("BattleController.ResetSeed(int) 缺失");
                }
                int value = seed_to_apply;
                runtime_.invoke(reset_seed, controller, {&value});
                void* random_extensions =
                    runtime_.find("Torappu", "RandomExtensions");
                void* reset_prd =
                    find_method_optional(runtime_, random_extensions,
                                         "ResetPRDRandom", 0);
                if (reset_prd) {
                    runtime_.invoke(reset_prd, nullptr, {});
                }
                applied = method_int(runtime_, controller, controller_class,
                                     "get_randomSeed", seed_to_apply);
                tas_seed_.store(seed_to_apply, std::memory_order_release);
            }
        }
    } catch (const std::exception& error) {
        log(kLogWarning,
            std::string("时间轴随机种子应用失败：") + error.what());
    } catch (...) {
    }
    {
        std::lock_guard lock(record_mutex_);
        if (scene_.controller != controller) {
            return;
        }
        if (need_capture && observed >= 0) {
            timeline_seed_observed_ = observed;
        }
        if (need_apply) {
            if (seed_to_apply >= 0 && applied >= 0 && !info_locked) {
                timeline_seed_applied_ = applied;
            }
            timeline_seed_apply_done_ = true;
        }
        record_dirty_.store(true, std::memory_order_release);
        timeline_dirty_.store(true, std::memory_order_release);
        ++timeline_revision_;
    }
    log(kLogInfo, "timeline seed observed=" + std::to_string(observed) +
                      " requested=" + std::to_string(seed_to_apply) +
                      " applied=" + std::to_string(applied) +
                      " locked=" +
                      (timeline_.seed.captured() || timeline_seed_applied_ >= 0
                           ? "1"
                           : "0"));
}

void LocalBattle::reset_world_sampling() {
    world_sampling_valid_ = false;
    world_sampling_cost_ = -1;
    world_sampling_wave_ = 0;
    world_sampling_total_waves_ = 0;
    world_sampling_enemies_ = 0;
    world_sampling_wave_start_ = 0;
    world_sampling_wave_start_index_ = 0;
}

void LocalBattle::sample_world_events(void* controller,
                                      std::uint32_t frame, int cost) {
    if (!controller) {
        return;
    }
    int wave = -1;
    int total_waves = -1;
    int spawned_enemies = 0;
    std::int64_t wave_start_raw = 0;
    bool has_wave_start = false;
    if (classes_.scheduler) {
        void* scheduler = nullptr;
        try {
            read_field_deep(runtime_, controller, classes_.battle_controller,
                            "_scheduler", &scheduler);
        } catch (...) {
        }
        if (scheduler) {
            try {
                if (!read_field_deep(runtime_, scheduler,
                                     classes_.scheduler,
                                     "m_spawnedWavesCnt", &wave)) {
                    wave = method_int(runtime_, scheduler,
                                      classes_.scheduler,
                                      "get_spawnedWavesCnt", -1);
                }
                void* waves = nullptr;
                if (read_field_deep(runtime_, scheduler,
                                    classes_.scheduler, "m_waves",
                                    &waves) &&
                    waves) {
                    total_waves = runtime_.count(waves);
                } else {
                    total_waves = method_int(runtime_, scheduler,
                                             classes_.scheduler,
                                             "get_totalWavesCnt", -1);
                }
                read_field_deep(runtime_, scheduler,
                                classes_.scheduler,
                                "m_spawnedEnemiesCnt", &spawned_enemies);
                has_wave_start = read_fixed_field_raw(
                    runtime_, scheduler, classes_.scheduler,
                    "m_waveStartTime", &wave_start_raw);
            } catch (...) {
                wave = -1;
                total_waves = -1;
                has_wave_start = false;
            }
        }
    }

    std::lock_guard lock(record_mutex_);
    world_sampling_enemies_ = spawned_enemies;
    if (timeline_nodes_.empty()) {
        return;
    }
    if (!world_sampling_valid_) {
        world_sampling_valid_ = true;
        world_sampling_cost_ = cost;
        world_sampling_wave_ = wave >= 0 ? wave : 0;
        world_sampling_total_waves_ = total_waves;
        world_sampling_wave_start_ =
            has_wave_start ? wave_start_raw : 0;
        if (spawned_enemies > 0) {
            world_sampling_wave_ = std::max(world_sampling_wave_, 1);
            world_sampling_wave_start_index_ =
                std::max(world_sampling_wave_start_index_,
                         world_sampling_wave_);
        }
        if (has_wave_start && wave_start_raw != 0) {
            world_sampling_wave_start_index_ = 1;
            world_sampling_wave_ =
                std::max(world_sampling_wave_, 1);
        } else if (wave > 0 && frame <= 150) {
            world_sampling_wave_start_index_ = wave;
        }
        return;
    }
    if (cost >= 0 && world_sampling_cost_ >= 0 &&
        cost > world_sampling_cost_) {
        record_timeline_world_event_locked(frame, kWorldEventCost,
                                           cost - world_sampling_cost_, cost);
    }
    if (cost >= 0) {
        world_sampling_cost_ = cost;
    }
    if (spawned_enemies > 0 && world_sampling_wave_ == 0) {
        world_sampling_wave_ = 1;
        world_sampling_wave_start_index_ =
            std::max(world_sampling_wave_start_index_, 1);
    }
    if (has_wave_start && wave_start_raw != 0 &&
        wave_start_raw != world_sampling_wave_start_) {
        world_sampling_wave_start_ = wave_start_raw;
        ++world_sampling_wave_start_index_;
        world_sampling_wave_ = std::max(
            world_sampling_wave_, world_sampling_wave_start_index_);
    }
    if (wave >= 0) {
        world_sampling_wave_ = std::max(world_sampling_wave_, wave);
        world_sampling_total_waves_ = total_waves;
    }
}

void LocalBattle::verify_replay_events() {
    if (!replay_active_.load(std::memory_order_acquire) ||
        !timeline_.replay_verify_events ||
        (!replay_has_event_baseline_ &&
         !replay_has_world_event_baseline_) ||
        replay_event_mismatch_) {
        return;
    }
    const std::uint32_t current = replay_frame_;
    if (current == 0) {
        return;
    }
    // World events are sampled approximations (cost/wave timing), not
    // deterministic combat modifiers. They are recorded for display only and
    // must not gate replay progress.
    constexpr bool kVerifyWorldEvents = false;
    const auto normalized_keys = [](const std::vector<ReplayEvent>& values) {
        std::vector<std::string> keys;
        keys.reserve(values.size());
        for (const ReplayEvent& event : values) {
            keys.push_back(replay_event_key(event));
        }
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        return keys;
    };
    const auto normalized_world_keys =
        [](const std::vector<ReplayWorldEvent>& values) {
            std::vector<std::string> keys;
            keys.reserve(values.size());
            for (const ReplayWorldEvent& event : values) {
            if (event.kind == kWorldEventWave) {
                continue;
            }
                keys.push_back(replay_world_event_key(event));
            }
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            return keys;
        };

    bool mismatch = false;
    std::uint32_t mismatch_frame = 0;
    std::size_t expected_count = 0;
    std::size_t actual_count = 0;
    std::vector<ReplayEvent> mismatch_actual;
    std::vector<ReplayWorldEvent> mismatch_actual_world;
    std::vector<std::string> mismatch_expected;
    {
        std::lock_guard lock(record_mutex_);
        if (!replay_event_check_valid_) {
            replay_event_check_valid_ = true;
            replay_event_check_frame_ = 0;
        }
        std::uint32_t scanned = 0;
        while (replay_event_check_frame_ < current && scanned < 240) {
            const std::uint32_t frame = replay_event_check_frame_;
            const std::string node_id = timeline_node_for_frame_locked(frame);
            const auto node = timeline_nodes_.find(node_id);
            if (node != timeline_nodes_.end()) {
                const auto expected_it =
                    node->second.expected_events.find(frame);
                const auto actual_it = node->second.observed_events.find(frame);
                const std::vector<ReplayEvent> empty;
                const std::vector<std::string> expected = normalized_keys(
                    expected_it == node->second.expected_events.end()
                        ? empty
                        : expected_it->second);
                const std::vector<std::string> actual = normalized_keys(
                    actual_it == node->second.observed_events.end()
                        ? empty
                        : actual_it->second);
                const auto expected_world_it =
                    node->second.expected_world_events.find(frame);
                const auto actual_world_it =
                    node->second.observed_world_events.find(frame);
                const std::vector<ReplayWorldEvent> empty_world;
                const std::vector<std::string> expected_world =
                    normalized_world_keys(
                        expected_world_it ==
                                node->second.expected_world_events.end()
                            ? empty_world
                            : expected_world_it->second);
                const std::vector<std::string> actual_world =
                    normalized_world_keys(
                        actual_world_it ==
                                node->second.observed_world_events.end()
                            ? empty_world
                            : actual_world_it->second);
                const bool combat_mismatch =
                    replay_has_event_baseline_ && expected != actual;
                const bool world_mismatch =
                    kVerifyWorldEvents && replay_has_world_event_baseline_ &&
                    expected_world != actual_world;
                if (combat_mismatch || world_mismatch) {
                    mismatch = true;
                    mismatch_frame = frame;
                    expected_count = expected.size() +
                        (kVerifyWorldEvents ? expected_world.size() : 0);
                    actual_count = actual.size() +
                        (kVerifyWorldEvents ? actual_world.size() : 0);
                    if (actual_it != node->second.observed_events.end()) {
                        mismatch_actual = actual_it->second;
                    }
                    if (actual_world_it !=
                        node->second.observed_world_events.end()) {
                        mismatch_actual_world = actual_world_it->second;
                    }
                    mismatch_expected = expected;
                    break;
                }
            }
            ++replay_event_check_frame_;
            ++scanned;
        }
    }
    if (!mismatch) {
        return;
    }
    replay_event_mismatch_ = true;
    set_paused(true);
    const std::string detail =
        expected_count == actual_count
            ? "事件值不同"
            : ("事件数量不同：录像 " + std::to_string(expected_count) +
               " / 实际 " + std::to_string(actual_count));
    publish_result("Warning F" + std::to_string(mismatch_frame) +
                   " 帧事件与录像不同（" + detail + "）",
                   false);
    for (const ReplayEvent& event : mismatch_actual) {
        log(kLogWarning,
            "事件差异 F" + std::to_string(mismatch_frame) +
                " actual actor=" + event.actor + " target=" + event.target +
                " type=" + std::to_string(static_cast<unsigned>(event.type)) +
                " value=" + std::to_string(event.actual) +
                " auid=" + std::to_string(event.actor_uid) +
                " tuid=" + std::to_string(event.target_uid));
    }
    for (const std::string& key : mismatch_expected) {
        log(kLogWarning, "事件差异 F" + std::to_string(mismatch_frame) +
                             " expected key=" + key);
    }
    for (const ReplayWorldEvent& event : mismatch_actual_world) {
        log(kLogWarning,
            "世界事件差异 F" + std::to_string(mismatch_frame) +
                " actual kind=" + std::to_string(event.kind) +
                " value=" + std::to_string(event.value) +
                " total=" + std::to_string(event.total));
    }
}

std::vector<std::string> LocalBattle::timeline_path_to_root_locked(
    const std::string& node_id) const {
    std::vector<std::string> path;
    const auto node = timeline_nodes_.find(node_id);
    if (node == timeline_nodes_.end()) {
        return path;
    }
    for (const TimelineNode* cursor = &node->second;;) {
        path.push_back(cursor->id);
        if (cursor->parent_id.empty()) {
            break;
        }
        const auto parent = timeline_nodes_.find(cursor->parent_id);
        if (parent == timeline_nodes_.end()) {
            path.clear();
            break;
        }
        cursor = &parent->second;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

void LocalBattle::set_playback_path_locked(const std::string& node_id) {
    playback_path_ = timeline_path_to_root_locked(node_id);
    playback_segment_ = 0;
}

void LocalBattle::advance_playback_segment_locked(std::uint32_t frame) {
    if (playback_path_.empty()) {
        playback_segment_ = 0;
        return;
    }
    while (playback_segment_ + 1 < playback_path_.size()) {
        const auto next =
            timeline_nodes_.find(playback_path_[playback_segment_ + 1]);
        if (next == timeline_nodes_.end()) {
            playback_path_.resize(playback_segment_ + 1);
            break;
        }
        const std::uint32_t start = next->second.fork_frame + 1;
        if (frame < start) {
            break;
        }
        ++playback_segment_;
    }
    if (playback_segment_ >= playback_path_.size()) {
        playback_segment_ = playback_path_.size() - 1;
    }
}

std::string LocalBattle::playback_node_for_frame_locked(std::uint32_t frame) {
    if (playback_path_.empty()) {
        return {};
    }
    advance_playback_segment_locked(frame);
    if (playback_segment_ >= playback_path_.size()) {
        return {};
    }
    return playback_path_[playback_segment_];
}

std::string LocalBattle::playback_fork_parent_locked(std::uint32_t frame) {
    const std::string node_id = playback_node_for_frame_locked(frame);
    if (node_id.empty()) {
        return {};
    }
    const auto node = timeline_nodes_.find(node_id);
    if (node == timeline_nodes_.end()) {
        return {};
    }
    if (node->second.parent_id.empty()) {
        return node_id;
    }
    const std::uint32_t start = node->second.fork_frame + 1;
    if (frame > start) {
        return node_id;
    }
    return node->second.parent_id;
}

ReplayTree LocalBattle::current_tree_snapshot_locked(
    const std::string& current_id) const {
    ReplayTree tree;
    tree.root_id = timeline_root_id_;
    tree.current_id = current_id.empty() ? timeline_current_id_ : current_id;
    tree.nodes.reserve(timeline_nodes_.size());
    for (const auto& [id, node] : timeline_nodes_) {
        (void)id;
        tree.nodes.push_back(node);
    }
    return tree;
}

bool LocalBattle::build_plan_for_nodes_path_locked(
    const std::string& node_id,
    std::vector<std::pair<std::uint32_t,
                          std::vector<RecordedOperation>>>* plan) const {
    if (!plan || node_id.empty()) {
        return false;
    }
    const std::vector<std::string> path =
        timeline_path_to_root_locked(node_id);
    if (path.empty()) {
        return false;
    }
    RecordedFrameMap merged;
    for (const std::string& id : path) {
        const auto node = timeline_nodes_.find(id);
        if (node == timeline_nodes_.end()) {
            return false;
        }
        for (const auto& [frame, operations] : node->second.operations) {
            auto& target = merged[frame];
            target.insert(target.end(), operations.begin(), operations.end());
        }
    }
    plan->clear();
    plan->reserve(merged.size());
    for (auto& [frame, operations] : merged) {
        plan->emplace_back(frame, std::move(operations));
    }
    return true;
}

bool LocalBattle::build_timeline_jump_plan(std::uint32_t target,
                                           const std::string& node_id,
                                           std::string* parent_id) {
    if (!parent_id) {
        return false;
    }
    std::lock_guard lock(record_mutex_);
    const std::string selected =
        node_id.empty() ? timeline_current_id_ : node_id;
    auto selected_node = timeline_nodes_.find(selected);
    if (selected_node == timeline_nodes_.end()) {
        return false;
    }

    std::vector<const TimelineNode*> path;
    for (const TimelineNode* cursor = &selected_node->second;;) {
        path.push_back(cursor);
        if (cursor->parent_id.empty()) {
            break;
        }
        const auto parent = timeline_nodes_.find(cursor->parent_id);
        if (parent == timeline_nodes_.end()) {
            return false;
        }
        cursor = &parent->second;
    }
    std::reverse(path.begin(), path.end());

    const TimelineNode* containing = path.front();
    for (const TimelineNode* node : path) {
        const std::uint32_t start =
            node->parent_id.empty() ? 0 : node->fork_frame + 1;
        if (target >= start) {
            containing = node;
        } else {
            break;
        }
    }

    RecordedFrameMap plan;
    for (const TimelineNode* node : path) {
        for (const auto& [frame, operations] : node->operations) {
            (void)target;
            auto& merged = plan[frame];
            merged.insert(merged.end(), operations.begin(), operations.end());
        }
    }
    replay_steps_.clear();
    replay_steps_.reserve(plan.size());
    for (auto& [frame, operations] : plan) {
        replay_steps_.emplace_back(frame, std::move(operations));
    }
    reset_replay_consumed();
    replay_file_ = "timeline://" + containing->id + "@" + std::to_string(target);
    *parent_id = containing->id;
    return true;
}

bool LocalBattle::run_fast_timeline_jump(void* controller) {
    struct FastRunGuard {
        std::atomic<bool>& running;
        explicit FastRunGuard(std::atomic<bool>& value) : running(value) {
            running.store(true, std::memory_order_release);
        }
        ~FastRunGuard() {
            running.store(false, std::memory_order_release);
        }
    } fast_run_guard(timeline_fast_running_);
    const std::uint32_t target =
        replay_stop_frame_.load(std::memory_order_acquire);
    void* game_mode = nullptr;
    void* controller_class = nullptr;
    try {
        if (!controller || !replay_frame_get_ || !replay_frame_method_) {
            throw std::runtime_error("快速回退所需的逻辑帧接口未就绪");
        }
        adopt_scene(controller);
        controller_class = classes_.battle_controller;
        read_field_deep(runtime_, controller, controller_class, "m_gameMode",
                        &game_mode);
        if (!game_mode) {
            throw std::runtime_error("读取 m_gameMode 失败");
        }
        runtime_.set<bool>(game_mode, "m_allowManualTick", true);
        tas_spine_auto_.store(true, std::memory_order_release);
        ensure_spine_manual_hook();
        void* set_deterministic =
            find_method_optional(runtime_, controller_class,
                                 "set_isDeterministic", 1);
        if (set_deterministic) {
            bool deterministic = true;
            runtime_.invoke(set_deterministic, controller, {&deterministic});
        }

        void* method =
            runtime_.method(controller_class, "ManualFrameTick", 2);
        void* frame_class =
            runtime_.class_from_type(runtime_.method_param(method, 0));
        void* frame_value = box_zero(runtime_, frame_class);
        Root frame_value_root(runtime_, frame_value);
        void* queue_field = runtime_.field(frame_class, "opQueue");
        void* queue_class =
            runtime_.class_from_type(runtime_.field_type(queue_field));
        void* queue = runtime_.object_new(queue_class);
        Root queue_root(runtime_, queue);
        runtime_.invoke(runtime_.method(queue_class, ".ctor", 0), queue);
        runtime_.field_set_object(frame_value, queue_field, queue);

        bool flag = true;
        replay_next_step_ = 0;
        replay_dispatched_ = 0;
        replay_frame_ = 0;
        while (replay_frame_ < target) {
            replay_frame_ = replay_frame_get_(replay_frame_method_);
            while (replay_next_step_ < replay_steps_.size() &&
                   replay_steps_[replay_next_step_].first <= replay_frame_) {
                auto& operations = replay_steps_[replay_next_step_].second;
                for (std::size_t op_index = 0;
                     op_index < operations.size(); ++op_index) {
                    if (replay_operation_consumed(replay_next_step_,
                                                  op_index)) {
                        continue;
                    }
                    dispatch_replay_operation(operations[op_index]);
                    ++replay_dispatched_;
                }
                ++replay_next_step_;
            }
            if (replay_frame_ >= target) {
                break;
            }
            runtime_.invoke(method, controller, {frame_value, &flag});
        }
        replay_frame_ = replay_frame_get_(replay_frame_method_);
        while (replay_next_step_ < replay_steps_.size() &&
               replay_steps_[replay_next_step_].first <= replay_frame_) {
            auto& operations = replay_steps_[replay_next_step_].second;
            for (std::size_t op_index = 0; op_index < operations.size();
                 ++op_index) {
                if (replay_operation_consumed(replay_next_step_, op_index)) {
                    continue;
                }
                dispatch_replay_operation(operations[op_index]);
                ++replay_dispatched_;
            }
            ++replay_next_step_;
        }

        runtime_.set<bool>(game_mode, "m_allowManualTick", false);
        const bool auto_play = pending_timeline_play_;
        try {
            adopt_scene(controller);
        } catch (...) {
            clear_scene();
        }
        finish_timeline_rewind(replay_frame_, auto_play);
        return true;
    } catch (const std::exception& error) {
        if (game_mode) {
            try {
                runtime_.set<bool>(game_mode, "m_allowManualTick", false);
            } catch (...) {
            }
        }
        replay_active_.store(false, std::memory_order_release);
        replay_stop_frame_.store(0, std::memory_order_release);
        replay_stop_enabled_.store(false, std::memory_order_release);
        pending_timeline_jump_ = false;
        pending_timeline_fast_ = false;
        pending_timeline_play_ = false;
        pending_timeline_parent_id_.clear();
        pending_timeline_mode_ = PendingTimelineMode::None;
        record_active_.store(false, std::memory_order_release);
        set_paused(true);
        publish_result(std::string("快速(beta) 回退失败：") + error.what() +
                           "；战斗已暂停，请重新进入关卡",
                       false);
        return false;
    }
}

bool LocalBattle::run_fast_replay_seek(void* controller,
                                       std::uint32_t target) {
    struct FastRunGuard {
        std::atomic<bool>& running;
        explicit FastRunGuard(std::atomic<bool>& value) : running(value) {
            running.store(true, std::memory_order_release);
        }
        ~FastRunGuard() {
            running.store(false, std::memory_order_release);
        }
    } fast_run_guard(timeline_fast_running_);
    void* game_mode = nullptr;
    void* controller_class = nullptr;
    bool internal_paused = false;
    try {
        if (!controller || !replay_frame_get_ || !replay_frame_method_) {
            throw std::runtime_error("快速快进所需的逻辑帧接口未就绪");
        }
        {
            std::lock_guard lock(mutex_);
            if (scene_.controller != controller ||
                !local_battle_active_.load(std::memory_order_acquire)) {
                throw std::runtime_error("快速快进时战斗场景已变化");
            }
        }
        controller_class = classes_.battle_controller;
        read_field_deep(runtime_, controller, controller_class, "m_gameMode",
                        &game_mode);
        if (!game_mode) {
            throw std::runtime_error("读取 m_gameMode 失败");
        }
        internal_paused =
            set_internal_pause(runtime_, controller, controller_class, true);
        runtime_.set<bool>(game_mode, "m_allowManualTick", true);
        tas_spine_auto_.store(true, std::memory_order_release);
        ensure_spine_manual_hook();
        void* set_deterministic =
            find_method_optional(runtime_, controller_class,
                                 "set_isDeterministic", 1);
        if (set_deterministic) {
            bool deterministic = true;
            runtime_.invoke(set_deterministic, controller, {&deterministic});
        }

        void* method =
            runtime_.method(controller_class, "ManualFrameTick", 2);
        void* frame_class =
            runtime_.class_from_type(runtime_.method_param(method, 0));
        void* frame_value = box_zero(runtime_, frame_class);
        Root frame_value_root(runtime_, frame_value);
        void* queue_field = runtime_.field(frame_class, "opQueue");
        void* queue_class =
            runtime_.class_from_type(runtime_.field_type(queue_field));
        void* queue = runtime_.object_new(queue_class);
        Root queue_root(runtime_, queue);
        runtime_.invoke(runtime_.method(queue_class, ".ctor", 0), queue);
        runtime_.field_set_object(frame_value, queue_field, queue);

        bool flag = true;
        replay_frame_ = replay_frame_get_(replay_frame_method_);
        {
            std::lock_guard lock(record_mutex_);
            advance_playback_segment_locked(replay_frame_);
        }
        while (replay_frame_ < target) {
            while (replay_next_step_ < replay_steps_.size() &&
                   replay_steps_[replay_next_step_].first <= replay_frame_) {
                auto& operations = replay_steps_[replay_next_step_].second;
                for (std::size_t op_index = 0;
                     op_index < operations.size(); ++op_index) {
                    if (replay_operation_consumed(replay_next_step_,
                                                  op_index)) {
                        continue;
                    }
                    dispatch_replay_operation(operations[op_index]);
                    ++replay_dispatched_;
                }
                ++replay_next_step_;
            }
            runtime_.invoke(method, controller, {frame_value, &flag});
            replay_frame_ = replay_frame_get_(replay_frame_method_);
            {
                std::lock_guard lock(record_mutex_);
                advance_playback_segment_locked(replay_frame_);
            }
        }
        while (replay_next_step_ < replay_steps_.size() &&
               replay_steps_[replay_next_step_].first <= replay_frame_) {
            auto& operations = replay_steps_[replay_next_step_].second;
            for (std::size_t op_index = 0; op_index < operations.size();
                 ++op_index) {
                if (replay_operation_consumed(replay_next_step_, op_index)) {
                    continue;
                }
                dispatch_replay_operation(operations[op_index]);
                ++replay_dispatched_;
            }
            ++replay_next_step_;
        }

        runtime_.set<bool>(game_mode, "m_allowManualTick", false);
        if (internal_paused) {
            set_internal_pause(runtime_, controller, controller_class, false);
            internal_paused = false;
        }
        replay_stop_frame_.store(0, std::memory_order_release);
        replay_stop_enabled_.store(false, std::memory_order_release);
        pending_replay_seek_fast_.store(false, std::memory_order_release);
        pending_replay_seek_target_.store(0, std::memory_order_release);
        replay_active_.store(false, std::memory_order_release);
        set_paused(true);
        bool scene_current = false;
        {
            std::lock_guard lock(mutex_);
            scene_current = scene_.controller == controller;
        }
        if (scene_current) {
            try {
                adopt_scene(controller);
            } catch (...) {
                clear_scene();
            }
        }
        publish_timeline();
        publish_result("录像已快速快进到第 " + std::to_string(replay_frame_) +
                       " 帧");
        return true;
    } catch (const std::exception& error) {
        if (internal_paused && controller && controller_class) {
            try {
                set_internal_pause(runtime_, controller, controller_class,
                                   false);
            } catch (...) {
            }
            internal_paused = false;
        }
        if (game_mode) {
            try {
                runtime_.set<bool>(game_mode, "m_allowManualTick", false);
            } catch (...) {
            }
        }
        replay_active_.store(false, std::memory_order_release);
        replay_stop_frame_.store(0, std::memory_order_release);
        replay_stop_enabled_.store(false, std::memory_order_release);
        pending_replay_seek_fast_.store(false, std::memory_order_release);
        pending_replay_seek_target_.store(0, std::memory_order_release);
        set_paused(true);
        publish_result(std::string("录像快速快进失败：") + error.what(), false);
        return false;
    }
}

void LocalBattle::replay_seek(std::uint32_t target, bool fast,
                              const std::string& node_id) {
    try {
        if (replay_file_.empty()) {
            throw std::runtime_error("当前没有已装载的录像");
        }
        if (!replay_frame_get_ || !replay_frame_method_) {
            throw std::runtime_error("录像逻辑帧接口未就绪");
        }
        if (pending_timeline_jump_ || timeline_fast_running_ ||
            timeline_advance_target_.load(std::memory_order_acquire) != 0) {
            throw std::runtime_error("已有跳转或快进任务正在执行");
        }
        std::vector<void*> controllers =
            find_controllers(runtime_, classes_.battle_controller);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        Root controller_root(runtime_, controllers.front());
        std::uint32_t current = 0;
        try {
            current = replay_frame_get_(replay_frame_method_);
        } catch (...) {
            current = static_cast<std::uint32_t>(method_int(
                runtime_, controllers.front(), classes_.battle_controller,
                "get_fixedFrameCnt", 0));
        }
        std::string branch_id;
        {
            std::lock_guard lock(record_mutex_);
            if (!playback_path_.empty()) {
                branch_id = playback_path_.back();
            }
        }
        bool node_on_path = false;
        bool in_place_switch = false;
        if (!node_id.empty() && !branch_id.empty() && target >= current) {
            std::lock_guard lock(record_mutex_);
            const std::vector<std::string> path =
                timeline_path_to_root_locked(branch_id);
            node_on_path =
                std::find(path.begin(), path.end(), node_id) != path.end();
            if (!node_on_path) {
                const std::vector<std::string> target_path =
                    timeline_path_to_root_locked(node_id);
                if (!target_path.empty()) {
                    std::size_t common = 0;
                    while (common < path.size() &&
                           common < target_path.size() &&
                           path[common] == target_path[common]) {
                        ++common;
                    }
                    std::uint32_t shared_fork = 0;
                    bool possible = false;
                    if (common == path.size()) {
                        possible = true;
                    } else if (common > 0) {
                        const auto current_child =
                            timeline_nodes_.find(path[common]);
                        const auto target_child =
                            timeline_nodes_.find(target_path[common]);
                        if (current_child != timeline_nodes_.end() &&
                            target_child != timeline_nodes_.end()) {
                            shared_fork = std::min(
                                current_child->second.fork_frame,
                                target_child->second.fork_frame);
                            possible = current <= shared_fork;
                        }
                    }
                    if (possible) {
                        std::vector<std::pair<
                            std::uint32_t,
                            std::vector<RecordedOperation>>>
                            target_plan;
                        if (build_plan_for_nodes_path_locked(
                                node_id, &target_plan)) {
                            replay_steps_ = std::move(target_plan);
                            reset_replay_consumed();
                            replay_next_step_ = 0;
                            replay_dispatched_ = 0;
                            while (replay_next_step_ <
                                       replay_steps_.size() &&
                                   replay_steps_[replay_next_step_].first <=
                                       current) {
                                replay_dispatched_ +=
                                    replay_steps_[replay_next_step_]
                                        .second.size();
                                ++replay_next_step_;
                            }
                            timeline_.replay_plan = replay_steps_;
                            timeline_current_id_ = node_id;
                            set_playback_path_locked(node_id);
                            advance_playback_segment_locked(current);
                            ++timeline_revision_;
                            timeline_dirty_.store(
                                true, std::memory_order_release);
                            in_place_switch = true;
                        }
                    }
                }
            }
        }
        const bool switch_branch =
            !node_id.empty() && node_id != branch_id && !node_on_path &&
            !in_place_switch;
        const auto restart_from_plan = [&](const std::string& node) {
            ReplayMeta meta;
            bool valid = false;
            {
                std::lock_guard lock(record_mutex_);
                meta = replay_meta_;
                valid = replay_meta_valid_;
            }
            if (!valid) {
                throw std::runtime_error("当前没有可复用的录像启动参数");
            }
            std::vector<std::pair<std::uint32_t,
                                  std::vector<RecordedOperation>>>
                plan;
            bool verify_events = true;
            {
                std::lock_guard lock(record_mutex_);
                verify_events = timeline_.replay_verify_events;
                if (node == branch_id && !timeline_.replay_plan.empty()) {
                    plan = timeline_.replay_plan;
                } else if (!build_plan_for_nodes_path_locked(node, &plan)) {
                    throw std::runtime_error("时间轴里找不到分支节点: " + node);
                }
            }
            start_replay_plan(meta,
                              node.empty() ? std::string("plan://inline")
                                           : "plan://" + node,
                              {}, std::move(plan), target, fast, node, {},
                              verify_events);
        };
        if (target == current) {
            if (!switch_branch) {
                publish_result(
                    in_place_switch
                        ? ("已切换到分支 " + node_id + "，仍在第 " +
                           std::to_string(current) + " 帧")
                        : ("录像已在第 " + std::to_string(current) + " 帧"));
                return;
            }
            restart_from_plan(node_id);
            return;
        }
        if (target < current) {
            const std::string node =
                (!node_id.empty() && node_id != branch_id) ? node_id
                                                           : branch_id;
            timeline_jump(target, node, fast);
            return;
        }
        if (switch_branch) {
            log(kLogInfo, "录像切换分支并快进：selected=" + node_id +
                              " playback=" + branch_id + " target=" +
                              std::to_string(target));
            restart_from_plan(node_id);
            return;
        }
        const bool keep_branch = !node_id.empty() && node_id != branch_id;
        if (keep_branch && !in_place_switch) {
            log(kLogInfo, "录像快进保持当前分支：selected=" + node_id +
                              " playback=" + branch_id);
        }
        if (fast) {
            pending_replay_seek_target_.store(target,
                                              std::memory_order_release);
            pending_replay_seek_fast_.store(true, std::memory_order_release);
            replay_stop_frame_.store(0, std::memory_order_release);
            replay_stop_enabled_.store(false, std::memory_order_release);
            replay_active_.store(true, std::memory_order_release);
            set_paused(false);
            publish_result(
                std::string("录像快速快进中") +
                (in_place_switch
                     ? "（切换分支）"
                     : (keep_branch ? "（保持当前分支）" : "")) +
                "：F" +
                std::to_string(current) + " → F" +
                std::to_string(target));
            return;
        }
        replay_stop_frame_.store(target, std::memory_order_release);
        replay_stop_enabled_.store(true, std::memory_order_release);
        pending_replay_seek_fast_.store(false, std::memory_order_release);
        pending_replay_seek_target_.store(0, std::memory_order_release);
        replay_active_.store(true, std::memory_order_release);
        set_paused(false);
        publish_timeline();
        publish_result(std::string("录像快进中") +
                       (in_place_switch
                            ? "（切换分支）"
                            : (keep_branch ? "（保持当前分支）" : "")) +
                       "：F" +
                       std::to_string(current) + " → F" +
                       std::to_string(target));
    } catch (const std::exception& error) {
        publish_result(std::string("录像跳转失败：") + error.what(), false);
    }
}

void LocalBattle::timeline_jump(std::uint32_t target, const std::string& node_id,
                                bool fast, bool auto_play) {
    cancel_timeline_fast_forward();
    pending_replay_seek_fast_.store(false, std::memory_order_release);
    pending_replay_seek_target_.store(0, std::memory_order_release);
    std::uint32_t current_frame = 0;
    {
        std::lock_guard lock(mutex_);
        current_frame = snapshot_.frame;
    }
    if (target > current_frame) {
        publish_result("只能回退到当前帧或更早的帧", false);
        return;
    }

    std::string parent_id;
    if (!build_timeline_jump_plan(target, node_id, &parent_id)) {
        publish_result("找不到要回退的时间轴节点", false);
        return;
    }
    {
        std::lock_guard lock(record_mutex_);
        std::string selected = node_id;
        if (selected.empty()) {
            selected = !playback_path_.empty() ? playback_path_.back()
                                               : timeline_current_id_;
        }
        if (!selected.empty() &&
            timeline_nodes_.find(selected) != timeline_nodes_.end()) {
            set_playback_path_locked(selected);
        }
    }
    if (replay_file_.rfind("timeline://", 0) != 0) {
        publish_result("无法建立时间轴回放前缀", false);
        return;
    }

    BattleStart start;
    bool rogue = false;
    bool tas_allowed = true;
    int seed = -1;
    {
        std::lock_guard lock(record_mutex_);
        start = timeline_start_;
        start.squad = timeline_squad_;
        rogue = timeline_rogue_;
        tas_allowed = timeline_tas_allowed_;
        seed = timeline_seed_applied_ >= 0 ? timeline_seed_applied_
                                           : timeline_seed_observed_;
    }
    if (!tas_allowed) {
        publish_result(
            "该局由游戏 UI 入口启动，时间轴只记录操作，不能回退分叉；"
            "请通过 BetterBattle 启动关卡",
            false);
        return;
    }
    const bool has_stage =
        rogue ? (!start.stage.empty() && !start.path.empty())
              : !start.stage.empty();
    if (!has_stage || start.squad.empty()) {
        publish_result("时间轴缺少关卡或编队信息，无法重建确定性战斗", false);
        return;
    }
    if (seed < 0) {
        publish_result("时间轴缺少本局随机种子，无法保证确定性重放", false);
        return;
    }
    tas_seed_.store(seed, std::memory_order_release);
    {
        std::lock_guard lock(record_mutex_);
        bool event_baseline = false;
        bool world_event_baseline = false;
        for (const std::string& id :
             active_path_for(timeline_nodes_, parent_id)) {
            auto node = timeline_nodes_.find(id);
            if (node == timeline_nodes_.end()) {
                continue;
            }
            if (node->second.expected_events.empty()) {
                node->second.expected_events =
                    std::move(node->second.observed_events);
            }
            node->second.observed_events.clear();
            if (node->second.expected_world_events.empty()) {
                node->second.expected_world_events =
                    std::move(node->second.observed_world_events);
            }
            node->second.observed_world_events.clear();
            if (!node->second.expected_events.empty()) {
                event_baseline = true;
            }
            if (!node->second.expected_world_events.empty()) {
                world_event_baseline = true;
            }
        }
        replay_has_event_baseline_ = event_baseline;
        replay_has_world_event_baseline_ =
            world_event_baseline;
        replay_event_check_valid_ = false;
        replay_event_mismatch_ = false;
        reset_world_sampling();
    }

    pending_timeline_jump_ = true;
    pending_timeline_fast_ = fast;
    pending_timeline_play_ = auto_play;
    pending_timeline_parent_id_ = parent_id;
    pending_timeline_fork_frame_ = target;
    pending_timeline_mode_ = PendingTimelineMode::Jump;
    {
        std::lock_guard lock(record_mutex_);
        frame_actual_operations_.clear();
        frame_prepared_ =
            std::numeric_limits<std::uint32_t>::max();
        frame_policy_ = TimelineFramePolicy::CompareInject;
    }
    record_active_.store(false, std::memory_order_release);
    tas_spine_auto_.store(true, std::memory_order_release);
    replay_next_step_ = 0;
    replay_dispatched_ = 0;
    replay_frame_ = 0;
    replay_stop_frame_.store(0, std::memory_order_release);
    replay_stop_enabled_.store(false, std::memory_order_release);
    {
        std::lock_guard lock(record_mutex_);
        timeline_seed_to_apply_ = seed;
        timeline_seed_info_locked_ = true;
        timeline_seed_apply_done_ = false;
        ++timeline_revision_;
        timeline_dirty_.store(true, std::memory_order_release);
    }
    publish_timeline();
    {
        std::lock_guard lock(record_mutex_);
        timeline_waiting_for_controller_ = true;
    }

    try {
        clear_enemy_focus();
        if (!install_replay_hook()) {
            throw std::runtime_error("回放 Hook 未就绪");
        }
        if (rogue) {
            rogue_start(start);
        } else {
            start_battle(start);
        }
        bool started = false;
        {
            std::lock_guard lock(mutex_);
            started = last_ok_;
        }
        if (!started) {
            throw std::runtime_error("重建战斗失败");
        }
        replay_stop_frame_.store(target, std::memory_order_release);
        replay_stop_enabled_.store(!auto_play, std::memory_order_release);
        replay_active_.store(true, std::memory_order_release);
        if (!auto_play) {
            record_active_.store(false, std::memory_order_release);
        }
        set_paused(false);
        publish_result("时间轴" + std::string(fast ? "快速(beta) " : "") +
                       (auto_play ? "回退并播放中：目标帧 "
                                  : "回退中：目标帧 ") +
                       std::to_string(target) +
                       (auto_play ? "，到达后继续播放原录像"
                                  : "，到达后交由玩家继续"));
    } catch (const std::exception& error) {
        pending_timeline_jump_ = false;
        pending_timeline_fast_ = false;
        pending_timeline_play_ = false;
        pending_timeline_parent_id_.clear();
        replay_active_.store(false, std::memory_order_release);
        replay_stop_frame_.store(0, std::memory_order_release);
        replay_stop_enabled_.store(false, std::memory_order_release);
        {
            std::lock_guard lock(record_mutex_);
            timeline_waiting_for_controller_ = false;
        }
        pending_timeline_mode_ = PendingTimelineMode::None;
        replay_steps_.clear();
        remove_replay_hook();
        set_paused(true);
        publish_result(std::string("时间轴回退启动失败：") + error.what(), false);
        publish_timeline();
    }
}

void LocalBattle::timeline_fast_forward(std::uint32_t frames) {
    if (frames == 0) {
        publish_result("快进帧数必须大于 0", false);
        return;
    }
    if (replay_active_.load(std::memory_order_acquire)) {
        publish_result("回放中不能快进", false);
        return;
    }
    if (pending_timeline_jump_) {
        publish_result("回退尚未完成，不能快进", false);
        return;
    }
    if (timeline_advance_target_.load(std::memory_order_acquire) != 0) {
        publish_result("已有快进任务正在执行", false);
        return;
    }

    std::uint32_t frame = 0;
    std::uint32_t node_end = 0;
    try {
        void* controller_class =
            classes_.battle_controller;
        const auto controllers =
            find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前不在战斗场景");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        if (!method_bool(runtime_, controller, controller_class,
                         "get_isPaused")) {
            throw std::runtime_error("只能在暂停且位于当前时间段末尾时快进");
        }
        const int current =
            method_int(runtime_, controller, controller_class,
                       "get_fixedFrameCnt", -1);
        if (current < 0) {
            throw std::runtime_error("读取逻辑帧失败");
        }
        frame = static_cast<std::uint32_t>(current);
    } catch (const std::exception& error) {
        publish_result(std::string("快进失败：") + error.what(), false);
        return;
    }

    {
        std::lock_guard lock(record_mutex_);
        const auto current = timeline_nodes_.find(timeline_current_id_);
        if (current == timeline_nodes_.end()) {
            publish_result("当前没有活动时间轴分支", false);
            return;
        }
        if (!record_active_.load(std::memory_order_acquire) ||
            !timeline_tas_allowed_) {
            publish_result("当前分支不在可记录状态，不能快进", false);
            return;
        }
        node_end = std::max(current->second.end_frame, frame);
        if (frame < current->second.end_frame) {
            publish_result("只能在当前时间段末尾快进", false);
            return;
        }
        current->second.end_frame = node_end;
    }

    const std::uint64_t target =
        static_cast<std::uint64_t>(frame) + frames;
    if (target > std::numeric_limits<std::uint32_t>::max()) {
        publish_result("快进帧数溢出", false);
        return;
    }
    if (!record_frame_get_) {
        publish_result("快进失败：逻辑帧读取回调未就绪", false);
        return;
    }
    timeline_advance_frames_.store(frames, std::memory_order_release);
    timeline_advance_target_.store(static_cast<std::uint32_t>(target),
                                   std::memory_order_release);
    publish_timeline();
    publish_result("快进 " + std::to_string(frames) + " 帧：F" +
                   std::to_string(frame) + " → F" +
                   std::to_string(target));
    set_paused(false);
}

void LocalBattle::complete_timeline_fast_forward_if_due(void* controller) {
    const std::uint32_t target =
        timeline_advance_target_.load(std::memory_order_acquire);
    if (!controller || target == 0 || !record_frame_get_) {
        return;
    }
    {
        std::lock_guard lock(record_mutex_);
        if (scene_.controller != controller || timeline_controller_absent_) {
            return;
        }
    }
    std::uint32_t frame = 0;
    try {
        frame = record_frame_get_(record_frame_method_);
    } catch (...) {
        return;
    }
    if (frame < target) {
        return;
    }
    std::uint32_t expected = target;
    if (!timeline_advance_target_.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel)) {
        return;
    }
    const std::uint32_t frames =
        timeline_advance_frames_.exchange(0, std::memory_order_acq_rel);
    note_timeline_frame(frame);
    set_paused(true);
    write_recording();
    publish_timeline();
    publish_result("已快进 " + std::to_string(frames) +
                   " 帧，暂停在第 " + std::to_string(frame) + " 帧");
}

void LocalBattle::cancel_timeline_fast_forward() {
    timeline_advance_target_.store(0, std::memory_order_release);
    timeline_advance_frames_.store(0, std::memory_order_release);
}

void LocalBattle::finish_timeline_rewind(std::uint32_t frame,
                                         bool auto_play) {
    cancel_timeline_fast_forward();
    {
        std::lock_guard lock(record_mutex_);
        const std::string selected =
            !playback_path_.empty()
                ? playback_path_.back()
                : (pending_timeline_parent_id_.empty()
                       ? timeline_current_id_
                       : pending_timeline_parent_id_);
        if (!selected.empty() &&
            timeline_nodes_.find(selected) != timeline_nodes_.end()) {
            set_playback_path_locked(selected);
            advance_playback_segment_locked(frame);
            timeline_current_id_ = playback_path_.back();
        }
        timeline_.replay_plan = replay_steps_;
        frame_actual_operations_.clear();
        frame_prepared_ =
            std::numeric_limits<std::uint32_t>::max();
        frame_policy_ = auto_play ? TimelineFramePolicy::CompareInject
                                  : TimelineFramePolicy::CompareOnly;
        pending_timeline_jump_ = false;
        pending_timeline_fast_ = false;
        pending_timeline_play_ = false;
        pending_timeline_parent_id_.clear();
        pending_timeline_mode_ = PendingTimelineMode::None;
        record_active_.store(true, std::memory_order_release);
        record_capture_suppressed_ = false;
        ++timeline_revision_;
        timeline_dirty_.store(true, std::memory_order_release);
    }
    replay_stop_frame_.store(0, std::memory_order_release);
    replay_stop_enabled_.store(false, std::memory_order_release);
    if (auto_play) {
        replay_active_.store(true, std::memory_order_release);
        set_paused(false);
    } else {
        replay_active_.store(false, std::memory_order_release);
        set_paused(true);
    }
    publish_timeline();
    publish_result("已回退到第 " + std::to_string(frame) + " 帧；" +
                   (auto_play ? "继续播放原录像，玩家操作不一致时分裂"
                              : "不执行原录像操作，等待玩家继续"));
}

void LocalBattle::complete_timeline_jump(std::uint32_t frame,
                                         bool from_replay) {
    cancel_timeline_fast_forward();
    bool preserve_future = false;
    {
        std::lock_guard lock(record_mutex_);
        const auto parent = timeline_nodes_.find(pending_timeline_parent_id_);
        if (parent == timeline_nodes_.end()) {
            pending_timeline_jump_ = false;
            pending_timeline_fast_ = false;
            pending_timeline_play_ = false;
            pending_timeline_parent_id_.clear();
            return;
        }
        TimelineNode& node = parent->second;
        const std::uint32_t target = frame;
        const std::uint32_t old_end = std::max(node.end_frame, target);
        RecordedFrameMap future;
        for (auto item = node.operations.upper_bound(target);
             item != node.operations.end(); ++item) {
            future.emplace(item->first, item->second);
        }
        for (auto item = node.operations.upper_bound(target);
             item != node.operations.end();) {
            item = node.operations.erase(item);
        }
        std::map<std::uint32_t, std::vector<ReplayEvent>> future_events;
        for (auto item = node.observed_events.upper_bound(target);
             item != node.observed_events.end(); ++item) {
            future_events.emplace(item->first, item->second);
        }
        for (auto item = node.observed_events.upper_bound(target);
             item != node.observed_events.end();) {
            item = node.observed_events.erase(item);
        }
        std::map<std::uint32_t, std::vector<ReplayEvent>>
            future_expected_events;
        for (auto item = node.expected_events.upper_bound(target);
             item != node.expected_events.end(); ++item) {
            future_expected_events.emplace(item->first, item->second);
        }
        for (auto item = node.expected_events.upper_bound(target);
             item != node.expected_events.end();) {
            item = node.expected_events.erase(item);
        }
        std::map<std::uint32_t, std::vector<ReplayWorldEvent>>
            future_world_events;
        for (auto item = node.observed_world_events.upper_bound(target);
             item != node.observed_world_events.end(); ++item) {
            future_world_events.emplace(item->first, item->second);
        }
        for (auto item = node.observed_world_events.upper_bound(target);
             item != node.observed_world_events.end();) {
            item = node.observed_world_events.erase(item);
        }
        std::map<std::uint32_t, std::vector<ReplayWorldEvent>>
            future_expected_world_events;
        for (auto item = node.expected_world_events.upper_bound(target);
             item != node.expected_world_events.end(); ++item) {
            future_expected_world_events.emplace(item->first,
                                                 item->second);
        }
        for (auto item = node.expected_world_events.upper_bound(target);
             item != node.expected_world_events.end();) {
            item = node.expected_world_events.erase(item);
        }

        std::vector<std::string> old_children;
        for (const auto& [id, child] : timeline_nodes_) {
            if (child.parent_id == node.id && child.fork_frame >= target) {
                old_children.push_back(id);
            }
        }

        std::vector<std::string> obsolete_nodes;
        bool descendant_operations = false;
        std::vector<std::string> pending_children = old_children;
        while (!pending_children.empty()) {
            const std::string id = std::move(pending_children.back());
            pending_children.pop_back();
            const auto child = timeline_nodes_.find(id);
            if (child == timeline_nodes_.end()) {
                continue;
            }
            obsolete_nodes.push_back(id);
            if (!child->second.operations.empty() ||
                !child->second.observed_events.empty() ||
                !child->second.expected_events.empty() ||
                !child->second.observed_world_events.empty() ||
                !child->second.expected_world_events.empty()) {
                descendant_operations = true;
            }
            for (const auto& [nested_id, nested] : timeline_nodes_) {
                if (nested.parent_id == id) {
                    pending_children.push_back(nested_id);
                }
            }
        }

        const bool node_future =
            !future.empty() || !future_events.empty() ||
            !future_expected_events.empty() || !future_world_events.empty() ||
            !future_expected_world_events.empty();
        std::vector<std::string> reparent_children;
        std::uint32_t child_fork_boundary =
            std::numeric_limits<std::uint32_t>::max();
        for (const std::string& child_id : old_children) {
            const auto child = timeline_nodes_.find(child_id);
            if (child != timeline_nodes_.end() &&
                child->second.fork_frame > target) {
                reparent_children.push_back(child_id);
                child_fork_boundary =
                    std::min(child_fork_boundary,
                             child->second.fork_frame);
            }
        }
        const bool continuation_needed =
            node_future || !reparent_children.empty();
        preserve_future = continuation_needed || descendant_operations;
        if (continuation_needed) {
            TimelineNode continuation;
            continuation.id = "n" + std::to_string(timeline_next_node_++);
            continuation.parent_id = node.id;
            continuation.fork_frame = target;
            continuation.end_frame =
                reparent_children.empty() ? old_end : child_fork_boundary;
            continuation.branch_no = timeline_next_branch_++;
            continuation.operations = std::move(future);
            continuation.observed_events = std::move(future_events);
            continuation.expected_events =
                std::move(future_expected_events);
            continuation.observed_world_events =
                std::move(future_world_events);
            continuation.expected_world_events =
                std::move(future_expected_world_events);
            if (!node.name.empty()) {
                continuation.name = node.name + " · 原分支";
            }
            const std::string continuation_id = continuation.id;
            timeline_nodes_.emplace(continuation_id, std::move(continuation));
            for (const std::string& child_id : reparent_children) {
                auto child = timeline_nodes_.find(child_id);
                if (child != timeline_nodes_.end()) {
                    child->second.parent_id = continuation_id;
                }
            }
        } else if (!descendant_operations) {
            for (const std::string& id : obsolete_nodes) {
                timeline_nodes_.erase(id);
            }
        }

        node.end_frame = target;
        if (preserve_future || from_replay) {
            TimelineNode fresh;
            fresh.id = "n" + std::to_string(timeline_next_node_++);
            fresh.parent_id = node.id;
            fresh.fork_frame = target;
            fresh.end_frame = target;
            fresh.branch_no = timeline_next_branch_++;
            timeline_current_id_ = fresh.id;
            timeline_nodes_.emplace(fresh.id, std::move(fresh));
        } else {
            timeline_current_id_ = node.id;
        }
        timeline_source_ = "live";
        timeline_source_file_.clear();
        pending_timeline_jump_ = false;
        pending_timeline_fast_ = false;
        pending_timeline_play_ = false;
        pending_timeline_parent_id_.clear();
        pending_timeline_mode_ = PendingTimelineMode::None;
        record_active_.store(true, std::memory_order_release);
        record_capture_suppressed_ = false;
        playback_path_.clear();
        playback_segment_ = 0;
        timeline_.replay_plan.clear();
        reset_world_sampling();

        ++timeline_revision_;
        record_dirty_.store(true, std::memory_order_release);
        timeline_dirty_.store(true, std::memory_order_release);
    }
    write_recording();
    publish_timeline();
    if (from_replay) {
        publish_result("录像已在第 " + std::to_string(frame + 1) +
                       " 帧分叉；" +
                       (preserve_future
                            ? "原录像未来已保留，后续玩家操作进入新分支"
                            : "当前节点之后没有录像未来，已创建新分支继续录制"));
    } else {
        publish_result("已回退到第 " + std::to_string(frame) +
                       " 帧并暂停；" +
                       (preserve_future
                            ? "已创建空白分支，后续玩家操作从下一帧开始记录"
                            : "后续没有待保留操作，已直接在当前节点继续录制"));
    }
}

void LocalBattle::switch_timeline_branch(const std::string& node_id) {
    if (node_id.empty() || replay_file_.empty() ||
        !replay_frame_get_ || !replay_frame_method_) {
        return;
    }
    std::uint32_t current = 0;
    try {
        current = replay_frame_get_(replay_frame_method_);
    } catch (...) {
        return;
    }

    bool switched = false;
    {
        std::lock_guard lock(record_mutex_);
        if (playback_path_.empty() ||
            timeline_nodes_.find(node_id) == timeline_nodes_.end()) {
            return;
        }
        std::vector<std::string> active =
            timeline_path_to_root_locked(playback_path_.back());
        std::vector<std::string> target =
            timeline_path_to_root_locked(node_id);
        const std::string playback_node =
            playback_node_for_frame_locked(current);
        std::vector<std::string> base =
            timeline_path_to_root_locked(playback_node);
        if (active.empty() || target.empty() || base.empty() ||
            base.size() > target.size() ||
            !std::equal(base.begin(), base.end(), target.begin())) {
            return;
        }

        bool on_active_path = target.size() <= active.size() &&
            std::equal(target.begin(), target.end(), active.begin());
        if (on_active_path) {
            return;
        }

        std::size_t common = 0;
        while (common < active.size() && common < target.size() &&
               active[common] == target[common]) {
            ++common;
        }
        if (common == 0 || common >= target.size()) {
            return;
        }
        std::uint32_t shared_fork =
            std::numeric_limits<std::uint32_t>::max();
        if (common < active.size()) {
            const auto current_child =
                timeline_nodes_.find(active[common]);
            const auto target_child =
                timeline_nodes_.find(target[common]);
            if (current_child == timeline_nodes_.end() ||
                target_child == timeline_nodes_.end()) {
                return;
            }
            shared_fork =
                std::min(current_child->second.fork_frame,
                         target_child->second.fork_frame);
        } else {
            const auto target_child =
                timeline_nodes_.find(target[common]);
            if (target_child == timeline_nodes_.end()) {
                return;
            }
            shared_fork = target_child->second.fork_frame;
        }
        if (current > shared_fork) {
            return;
        }

        std::vector<std::pair<std::uint32_t,
                              std::vector<RecordedOperation>>>
            target_plan;
        if (!build_plan_for_nodes_path_locked(node_id, &target_plan)) {
            return;
        }
        replay_steps_ = std::move(target_plan);
        reset_replay_consumed();
        replay_next_step_ = 0;
        replay_dispatched_ = 0;
        while (replay_next_step_ < replay_steps_.size() &&
               replay_steps_[replay_next_step_].first <= current) {
            replay_dispatched_ +=
                static_cast<std::uint32_t>(
                    replay_steps_[replay_next_step_].second.size());
            ++replay_next_step_;
        }
        replay_frame_ = current;
        timeline_.replay_plan = replay_steps_;
        timeline_current_id_ = node_id;
        set_playback_path_locked(node_id);
        advance_playback_segment_locked(current);
        replay_stop_frame_.store(0, std::memory_order_release);
        replay_stop_enabled_.store(false, std::memory_order_release);
        pending_timeline_jump_ = false;
        pending_timeline_fast_ = false;
        pending_timeline_play_ = false;
        pending_timeline_parent_id_.clear();
        frame_actual_operations_.clear();
        frame_prepared_ =
            std::numeric_limits<std::uint32_t>::max();
        frame_policy_ = TimelineFramePolicy::CompareInject;
        record_active_.store(true, std::memory_order_release);
        record_capture_suppressed_ = false;
        replay_active_.store(true, std::memory_order_release);
        ++timeline_revision_;
        timeline_dirty_.store(true, std::memory_order_release);
        switched = true;
    }
    if (switched) {
        publish_timeline();
        publish_result("已切换后续播放分支：" + node_id);
    }
}

void LocalBattle::timeline_action(const std::string& action,
                                  const std::string& name,
                                  const std::string& label, int value,
                                  bool auto_play) {
    if (action == "jump") {
        timeline_jump(static_cast<std::uint32_t>(std::max(value, 0)), name,
                      label == "fast", auto_play);
        return;
    }
    if (action == "advance") {
        timeline_fast_forward(
            static_cast<std::uint32_t>(std::max(value, 0)));
        return;
    }
    if (action == "branch") {
        switch_timeline_branch(name);
        return;
    }
    if (action == "status") {
        publish_timeline();
        return;
    }
    if (action == "rename") {
        bool found = false;
        {
            std::lock_guard lock(record_mutex_);
            const auto node = timeline_nodes_.find(name);
            if (node != timeline_nodes_.end()) {
                node->second.name = label;
                ++timeline_revision_;
                timeline_dirty_.store(true, std::memory_order_release);
                record_dirty_.store(true, std::memory_order_release);
                found = true;
            }
        }
        if (!found) {
            publish_result("重命名失败：节点不存在", false);
            return;
        }
        write_recording();
        publish_timeline();
        publish_result("时间轴节点已重命名");
        return;
    }
    if (action == "delete") {
        bool found = false;
        bool active = false;
        bool root = false;
        {
            std::lock_guard lock(record_mutex_);
            const auto node = timeline_nodes_.find(name);
            if (node != timeline_nodes_.end()) {
                found = true;
                root = node->second.parent_id.empty();
                for (const TimelineNode* cursor = &timeline_nodes_.at(
                         timeline_current_id_);;) {
                    if (cursor->id == name) {
                        active = true;
                        break;
                    }
                    if (cursor->parent_id.empty()) {
                        break;
                    }
                    const auto parent = timeline_nodes_.find(cursor->parent_id);
                    if (parent == timeline_nodes_.end()) {
                        break;
                    }
                    cursor = &parent->second;
                }
                if (!root && !active) {
                    std::vector<std::string> remove{name};
                    for (std::size_t index = 0; index < remove.size(); ++index) {
                        const std::string parent_id = remove[index];
                        for (const auto& [id, child] : timeline_nodes_) {
                            if (child.parent_id == parent_id) {
                                remove.push_back(id);
                            }
                        }
                    }
                    for (const std::string& id : remove) {
                        timeline_nodes_.erase(id);
                    }
                    ++timeline_revision_;
                    timeline_dirty_.store(true, std::memory_order_release);
                    record_dirty_.store(true, std::memory_order_release);
                    found = true;
                }
            }
        }
        if (!found) {
            publish_result("删除失败：节点不存在", false);
        } else if (root) {
            publish_result("根节点不能删除", false);
        } else if (active) {
            publish_result("不能删除当前活动分支", false);
        } else {
            write_recording();
            publish_timeline();
            publish_result("时间轴分支已删除");
        }
        return;
    }
    publish_result("timeline 需要 jump、advance、status、rename 或 delete",
                   false);
}

ReplayMeta LocalBattle::replay_meta_locked() const {
    ReplayMeta meta;
    meta.source = timeline_source_;
    meta.stage_id = timeline_stage_id_;
    meta.stage_code = timeline_stage_code_;
    meta.stage_name = timeline_stage_name_;
    meta.level_id = timeline_level_id_;
    meta.seed_observed = timeline_seed_observed_;
    meta.seed_applied = timeline_seed_applied_;
    meta.seed = timeline_seed_applied_ >= 0 ? timeline_seed_applied_
                                           : timeline_seed_observed_;
    meta.rogue = timeline_rogue_;
    meta.game_mode = timeline_game_mode_;
    meta.start = timeline_start_;
    meta.start.squad = timeline_squad_;
    meta.start.game_mode = meta.game_mode;
    if (meta.start.stage.empty() && !meta.rogue) {
        meta.start.stage = timeline_stage_id_;
    }
    meta.created_unix_ms = timeline_created_unix_ms_;
    return meta;
}

std::string LocalBattle::build_timeline_json() {
    std::lock_guard lock(record_mutex_);
    return build_timeline_json_locked();
}

std::string LocalBattle::build_timeline_json_locked() {
    const auto current = timeline_nodes_.find(timeline_current_id_);
    std::uint32_t maximum_frame = 0;
    std::uint64_t operation_count = 0;
    for (const auto& [id, node] : timeline_nodes_) {
        (void)id;
        maximum_frame = std::max(maximum_frame, node.end_frame);
        for (const auto& [frame, operations] : node.operations) {
            (void)frame;
            operation_count += operations.size();
        }
    }
    Json root;
    root["schema"] = 1;
    root["meta"] = meta_to_json(replay_meta_locked());
    root["active"] = !timeline_nodes_.empty();
    root["recording"] = record_active_.load(std::memory_order_acquire);
    root["jumping"] = pending_timeline_jump_;
    root["revision"] = timeline_revision_;
    root["current_id"] = timeline_current_id_;
    root["root_id"] = timeline_root_id_;
    root["stage"] = timeline_stage_id_;
    root["stage_code"] = timeline_stage_code_;
    root["stage_name"] = timeline_stage_name_;
    root["level_id"] = timeline_level_id_;
    root["seed"] = timeline_seed_applied_ >= 0 ? timeline_seed_applied_
                                              : timeline_seed_observed_;
    root["source"] = timeline_source_;
    root["source_file"] = timeline_source_file_;
    root["rewind_allowed"] = timeline_tas_allowed_;
    root["advancing"] =
        timeline_advance_target_.load(std::memory_order_acquire) != 0;
    root["advance_target"] =
        timeline_advance_target_.load(std::memory_order_acquire);
    root["max_frame"] = maximum_frame;
    root["operation_count"] = operation_count;
    Json nodes = Json::array();
    for (const auto& [id, node] : timeline_nodes_) {
        (void)id;
        nodes.push_back({
            {"id", node.id},
            {"parent_id", node.parent_id},
            {"name", node.name},
            {"fork_frame", node.fork_frame},
            {"end_frame", node.end_frame},
            {"branch", node.branch_no},
            {"active", node.id == timeline_current_id_},
            {"operations", operations_to_json(node.operations)},
        });
    }
    root["nodes"] = std::move(nodes);
    Json damage_hits = Json::array();
    for (const auto& [node_id, node] : timeline_nodes_) {
        for (const auto& [frame, values] : merged_node_events(node)) {
            for (const ReplayEvent& hit : values) {
                damage_hits.push_back({
                    {"frame", frame},
                    {"node_id", node_id},
                    {"actor", hit.actor},
                    {"target", hit.target},
                    {"actor_uid", hit.actor_uid},
                    {"target_uid", hit.target_uid},
                    {"type", static_cast<unsigned>(hit.type)},
                    {"value", hit.actual},
                });
            }
        }
    }
    root["damage_hits"] = std::move(damage_hits);
    Json world_events = Json::array();
    for (const auto& [node_id, node] : timeline_nodes_) {
        for (const auto& [frame, values] : merged_node_world_events(node)) {
            for (const ReplayWorldEvent& event : values) {
                world_events.push_back({
                    {"frame", frame},
                    {"node_id", node_id},
                    {"kind", static_cast<unsigned>(event.kind)},
                    {"value", event.value},
                    {"total", event.total},
                });
            }
        }
    }
    root["world_events"] = std::move(world_events);
    std::vector<std::string> path;
    for (const TimelineNode* cursor =
             current == timeline_nodes_.end() ? nullptr : &current->second;
         cursor;) {
        path.push_back(cursor->id);
        if (cursor->parent_id.empty()) {
            break;
        }
        const auto parent = timeline_nodes_.find(cursor->parent_id);
        cursor = parent == timeline_nodes_.end() ? nullptr : &parent->second;
    }
    std::reverse(path.begin(), path.end());
    root["active_path"] = string_array_to_json(path);
    return root.dump();
}

std::string LocalBattle::build_replay_document_json(bool include_events) {
    std::lock_guard lock(record_mutex_);
    return build_replay_document_json_locked(include_events);
}

std::string LocalBattle::build_replay_document_json_locked(
    bool include_events) {
    const std::vector<std::string> path =
        active_path_for(timeline_nodes_, timeline_current_id_);
    std::vector<ReplayTreeNode> nodes;
    nodes.reserve(timeline_nodes_.size());
    for (const auto& [id, node] : timeline_nodes_) {
        (void)id;
        nodes.push_back(node);
    }
    Json document;
    document["schema"] = 1;
    document["meta"] = meta_to_json(replay_meta_locked());
    document["tree"] = {
        {"root_id", timeline_root_id_},
        {"current_id", timeline_current_id_},
        {"active_path", string_array_to_json(path)},
        {"nodes", nodes_to_json(nodes, include_events)},
    };
    return document.dump();
}

void LocalBattle::save_replay_file(const std::string& path,
                                   bool include_events) {
    try {
        std::string path_text = path;
        if (path_text.empty()) {
            throw std::runtime_error("保存路径为空");
        }
        std::filesystem::path output_path(path_text);
        if (output_path.is_relative()) {
            output_path = std::filesystem::path(host_.root_directory) / output_path;
        }
        output_path = output_path.lexically_normal();
        if (output_path.extension().empty()) {
            output_path += ".json";
        }
        {
            std::lock_guard lock(record_mutex_);
            if (timeline_nodes_.empty() || timeline_root_id_.empty() ||
                timeline_current_id_.empty()) {
                throw std::runtime_error("当前没有可保存的时间轴");
            }
        }
        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        const std::string document =
            build_replay_document_json(include_events);
        const std::filesystem::path temporary =
            output_path.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::out | std::ios::trunc |
                                                std::ios::binary);
            if (!output) {
                throw std::runtime_error("无法创建保存文件");
            }
            output << document;
            output.flush();
            if (!output) {
                throw std::runtime_error("写入保存文件失败");
            }
        }
        std::error_code remove_error;
        std::filesystem::remove(output_path, remove_error);
        std::error_code rename_error;
        std::filesystem::rename(temporary, output_path, rename_error);
        if (rename_error) {
            std::filesystem::remove(temporary);
            throw std::runtime_error("保存文件替换失败: " +
                                     rename_error.message());
        }
        {
            std::lock_guard lock(record_mutex_);
            timeline_source_file_ = output_path.string();
        }
        publish_timeline();
        publish_result("录像已保存：" + output_path.string());
    } catch (const std::exception& error) {
        publish_result(std::string("保存录像失败：") + error.what(), false);
    }
}

void LocalBattle::publish_timeline() {
    const std::uint64_t started = perf_now_us();
    std::string payload = build_timeline_json();
    perf_.timeline_json.add(perf_now_us() - started);
    perf_.timeline_bytes_last = payload.size();
    if (bridge_ && bridge_->publish) {
        std::lock_guard lock(web_payload_mutex_);
        if (payload == last_timeline_json_) {
            ++perf_.timeline_publish_skipped;
        } else {
            bridge_->publish("betterbattle.timeline", payload.data(), payload.size(), 0);
            last_timeline_json_ = std::move(payload);
        }
    }
    timeline_dirty_.store(false, std::memory_order_release);
}

void LocalBattle::write_recording() {
    const std::uint64_t started = perf_now_us();
    std::lock_guard write_lock(record_write_mutex_);
    std::string document_json;
    std::string timeline_json;
    std::uint64_t revision = 0;
    {
        std::lock_guard lock(record_mutex_);
        document_json = build_replay_document_json_locked(false);
        timeline_json = build_timeline_json_locked();
        revision = timeline_revision_;
    }
    const std::filesystem::path operations_path =
        std::filesystem::path(host_.root_directory) / "battle_operations.json";
    std::ofstream output(operations_path, std::ios::out | std::ios::trunc);
    if (!output) {
        record_dirty_.store(true, std::memory_order_release);
        log(kLogWarning, "无法创建 battle_operations.json");
        return;
    }
    output << document_json;
    output.flush();
    if (!output) {
        record_dirty_.store(true, std::memory_order_release);
        log(kLogWarning, "写入 battle_operations.json 失败");
        return;
    }
    output.close();

    const std::filesystem::path timeline_path =
        std::filesystem::path(host_.root_directory) / "battle_timeline.json";
    std::ofstream timeline_output(timeline_path,
                                  std::ios::out | std::ios::trunc);
    if (timeline_output) {
        timeline_output << timeline_json << '\n';
    }
    {
        std::lock_guard lock(record_mutex_);
        if (timeline_revision_ == revision) {
            record_dirty_.store(false, std::memory_order_release);
            timeline_dirty_.store(false, std::memory_order_release);
        }
    }
    perf_.record_write.add(perf_now_us() - started);
}

void LocalBattle::flush_recording() {
    if ((record_active_.load(std::memory_order_acquire) &&
         record_dirty_.load(std::memory_order_acquire)) ||
        timeline_dirty_.load(std::memory_order_acquire)) {
        write_recording();
        publish_timeline();
    }
}

bool LocalBattle::load_replay_plan(const std::string& path) {
    ReplayDocument document =
        read_replay_document_file(path, host_.root_directory);
    replay_steps_ = std::move(document.plan);
    reset_replay_consumed();
    replay_file_ = path;
    return true;
}

void LocalBattle::start_replay(const std::string& path,
                               std::uint32_t seek_target,
                               bool seek_fast,
                               const std::string& branch_id) {
    try {
        const std::string file =
            path.empty() ? "battle_operations.json" : path;
        ReplayDocument document =
            read_replay_document_file(file, host_.root_directory);
        RecordedFrameMap branch_plan;
        const bool has_branch = !branch_id.empty();
        if (has_branch) {
            if (!build_plan_for_tree_path(document.tree, branch_id,
                                          &branch_plan)) {
                throw std::runtime_error("录像时间树里找不到分支节点: " +
                                         branch_id);
            }
            document.tree.current_id = branch_id;
        }
        const ReplayMeta meta = document.meta;
        std::vector<std::pair<std::uint32_t,
                              std::vector<RecordedOperation>>>
            plan;
        if (has_branch) {
            plan.reserve(branch_plan.size());
            for (auto& [frame, operations] : branch_plan) {
                plan.emplace_back(frame, std::move(operations));
            }
        } else {
            plan = std::move(document.plan);
        }
        start_replay_plan(meta, file, file, std::move(plan), seek_target,
                          seek_fast, branch_id, std::move(document.tree));
    } catch (const std::exception& error) {
        replay_active_.store(false, std::memory_order_release);
        pending_timeline_mode_ = PendingTimelineMode::None;
        {
            std::lock_guard lock(record_mutex_);
            timeline_waiting_for_controller_ = false;
        }
        remove_replay_hook();
        publish_result(std::string("开始录像回放失败：") + error.what(), false);
    }
}

void LocalBattle::start_replay_plan(
    const ReplayMeta& meta, const std::string& label,
    const std::string& source_path,
    std::vector<std::pair<std::uint32_t, std::vector<RecordedOperation>>> plan,
    std::uint32_t seek_target, bool seek_fast,
    const std::string& origin_node, ReplayTree tree, bool verify_events) {
    try {
        cancel_timeline_fast_forward();
        clear_enemy_focus();
        battle_session_closed_.store(false, std::memory_order_release);
        if (meta.stage_id.empty() || meta.start.stage.empty() ||
            meta.start.squad.empty()) {
            throw std::runtime_error("录像 meta 缺少关卡或编队");
        }
        if (meta.rogue && meta.start.path.empty()) {
            throw std::runtime_error("肉鸽录像 meta 缺少肉鸽关卡");
        }
        if (meta.seed < 0) {
            throw std::runtime_error("录像 meta 缺少随机种子");
        }
        if (!meta.game_mode.valid) {
            throw std::runtime_error("录像 meta 缺少游戏模式参数");
        }
        if (!install_replay_hook()) {
            throw std::runtime_error("回放 Hook 未就绪");
        }
        BattleStart start = meta.start;
        start.rogue = meta.rogue;
        start.squad = meta.start.squad;
        start.game_mode = meta.game_mode;
        if (!meta.rogue) {
            start.stage = meta.stage_id;
        }
        replay_next_step_ = 0;
        replay_dispatched_ = 0;
        replay_frame_ = 0;
        replay_event_check_valid_ = false;
        replay_event_mismatch_ = false;
        replay_stop_frame_.store(0, std::memory_order_release);
        replay_stop_enabled_.store(false, std::memory_order_release);
        pending_replay_seek_fast_.store(false, std::memory_order_release);
        pending_replay_seek_target_.store(0, std::memory_order_release);
        {
            std::lock_guard lock(record_mutex_);
            timeline_.replay_plan = plan;
            frame_actual_operations_.clear();
            frame_prepared_ =
                std::numeric_limits<std::uint32_t>::max();
            frame_policy_ = TimelineFramePolicy::CompareInject;
        }
        replay_steps_ = std::move(plan);
        reset_replay_consumed();
        replay_file_ = label;
        replay_source_path_ = source_path;
        if (seek_target > 0) {
            replay_stop_frame_.store(seek_target, std::memory_order_release);
            replay_stop_enabled_.store(!seek_fast,
                                       std::memory_order_release);
            pending_replay_seek_fast_.store(seek_fast,
                                            std::memory_order_release);
            pending_replay_seek_target_.store(seek_target,
                                              std::memory_order_release);
        } else if (!replay_steps_.empty()) {
            replay_stop_frame_.store(
                replay_steps_.back().first, std::memory_order_release);
            replay_stop_enabled_.store(true, std::memory_order_release);
        }
        queue_timeline_start(PendingTimelineMode::ImportedReplay, start,
                             meta.stage_id, meta.seed, replay_file_);
        {
            std::lock_guard lock(record_mutex_);
            replay_meta_ = meta;
            replay_meta_valid_ = true;
            timeline_.replay_verify_events = verify_events;
            if (!tree.nodes.empty()) {
                if (!origin_node.empty()) {
                    tree.current_id = origin_node;
                }
                timeline_.pending.has_document = true;
                timeline_.pending.meta = meta;
                timeline_.pending.tree = std::move(tree);
            } else if (!timeline_nodes_.empty()) {
                timeline_.pending.has_document = true;
                timeline_.pending.meta = meta;
                timeline_.pending.tree =
                    current_tree_snapshot_locked(origin_node);
            } else {
                timeline_.pending.has_document = false;
            }
            timeline_waiting_for_controller_ = true;
            pending_playback_branch_id_ = origin_node;
        }
        record_active_.store(true, std::memory_order_release);
        replay_active_.store(true, std::memory_order_release);
        if (meta.rogue) {
            rogue_start(start);
        } else {
            start_battle(start);
        }
        bool started = false;
        {
            std::lock_guard lock(mutex_);
            started = last_ok_;
        }
        if (!started) {
            remove_replay_hook();
            pending_timeline_mode_ = PendingTimelineMode::None;
            throw std::runtime_error("回放战斗启动失败");
        }
        set_paused(false);
        {
            std::lock_guard lock(record_mutex_);
            timeline_stage_id_ = meta.stage_id;
            timeline_stage_code_ = meta.stage_code;
            timeline_stage_name_ = meta.stage_name;
            timeline_level_id_ = meta.level_id;
            timeline_created_unix_ms_ = meta.created_unix_ms;
            timeline_start_ = start;
            timeline_start_.squad = meta.start.squad;
            timeline_start_.game_mode = meta.game_mode;
            timeline_squad_ = meta.start.squad;
            timeline_game_mode_ = meta.game_mode;
            timeline_revision_++;
            timeline_dirty_.store(true, std::memory_order_release);
        }
        std::size_t operations = 0;
        for (const auto& step : replay_steps_) {
            operations += step.second.size();
        }
        publish_result("录像回放已装载：" + replay_file_ + "，" +
                       std::to_string(operations) + " 个操作，seed=" +
                       std::to_string(meta.seed) +
                       (meta.rogue ? "，肉鸽" : "，普通关卡"));
    } catch (const std::exception& error) {
        replay_active_.store(false, std::memory_order_release);
        pending_timeline_mode_ = PendingTimelineMode::None;
        {
            std::lock_guard lock(record_mutex_);
            timeline_waiting_for_controller_ = false;
        }
        remove_replay_hook();
        publish_result(std::string("开始录像回放失败：") + error.what(), false);
    }
}

void* LocalBattle::replay_card(void* controller, const std::string& unit,
                               std::uint32_t* uid) {
    if (!controller || !uid) {
        return nullptr;
    }
    void* controller_class = classes_.battle_controller;
    void* deck_class = classes_.deck;
    void* card_class = runtime_.nested(deck_class, "Card");
    void* decks = nullptr;
    if (!read_field_deep(runtime_, controller, controller_class, "m_deckDict", &decks) ||
        !decks) {
        return nullptr;
    }
    Root decks_root(runtime_, decks);
    const int deck_count = runtime_.count(decks);
    for (int deck_index = 0; deck_index < deck_count; ++deck_index) {
        void* deck = listdict_value_at(runtime_, decks, deck_index);
        if (!deck) {
            continue;
        }
        Root deck_root(runtime_, deck);
        void* cards = runtime_.call(deck, "get_cards");
        if (!cards) {
            continue;
        }
        Root cards_root(runtime_, cards);
        const int card_count = runtime_.count(cards);
        for (int card_index = 0; card_index < card_count; ++card_index) {
            void* card = runtime_.at(cards, card_index);
            if (!card) {
                continue;
            }
            void* data = nullptr;
            if (!read_field_deep(runtime_, card, card_class, "m_data", &data) || !data) {
                continue;
            }
            Root data_root(runtime_, data);
            const std::string id =
                read_string_field(runtime_, data, runtime_.object_class(data), "prefabKey");
            if (id != unit) {
                continue;
            }
            std::uint32_t value = 0;
            if (!read_field_deep(runtime_, data, runtime_.object_class(data), "uniqueId",
                                 &value)) {
                continue;
            }
            *uid = value;
            return card;
        }
    }
    return nullptr;
}

void* LocalBattle::replay_character(void* controller, const std::string& unit) {
    void* controller_class = classes_.battle_controller;
    void* character_class = classes_.character;
    void* entity_class = classes_.entity;
    void* manager = nullptr;
    if (!read_field_deep(runtime_, controller, controller_class, "m_objectManger", &manager) ||
        !manager) {
        return nullptr;
    }
    Root manager_root(runtime_, manager);
    void* list = nullptr;
    if (!read_field_deep(runtime_, manager, runtime_.object_class(manager), "m_cachedList",
                         &list) ||
        !list) {
        return nullptr;
    }
    Root list_root(runtime_, list);
    const int count = runtime_.count(list);
    for (int index = 0; index < count; ++index) {
        void* object = runtime_.at(list, index);
        if (!object || runtime_.object_class(object) != character_class) {
            continue;
        }
        const std::string id =
            read_string_field(runtime_, object, entity_class, "<id>k__BackingField");
        if (id != unit) {
            continue;
        }
        if (method_bool(runtime_, object, character_class, "get_alive")) {
            return object;
        }
    }
    return nullptr;
}

void LocalBattle::dispatch_replay_operation(const RecordedOperation& operation) {
    struct CaptureSuppression {
        bool& value;
        bool previous;
        explicit CaptureSuppression(bool& target)
            : value(target), previous(target) {
            value = true;
        }
        ~CaptureSuppression() {
            value = previous;
        }
    } suppress_record(record_capture_suppressed_);

    void* controller_class = classes_.battle_controller;
    std::vector<void*> controllers = find_controllers(runtime_, controller_class);
    if (controllers.empty()) {
        throw std::runtime_error("回放时不在战斗场景");
    }
    void* controller = scene_.controller;
    if (!controller ||
        std::find(controllers.begin(), controllers.end(), controller) ==
            controllers.end()) {
        controller = controllers.front();
    }
    Root controller_root(runtime_, controller);
    if (operation.kind == RecordedOperation::Kind::Deploy) {
        std::uint32_t uid = 0;
        void* card = replay_card(controller, operation.unit, &uid);
        if (!card || !uid) {
            throw std::runtime_error("回放找不到卡牌: " + operation.unit);
        }
        Root card_root(runtime_, card);
        void* tile = find_tile(runtime_, controller, controller_class, operation.row,
                               operation.col);
        if (!tile) {
            throw std::runtime_error("回放找不到地块: " + std::to_string(operation.row) +
                                     "," + std::to_string(operation.col));
        }
        Root tile_root(runtime_, tile);
        int direction = std::clamp(operation.direction, 0, 3);
        void* spawn = runtime_.method(controller_class, "PlayerOp_Spawn", 3);
        void* result = runtime_.invoke(spawn, controller, {&uid, &direction, tile});
        if (!result || !runtime_.unbox<bool>(result)) {
            throw std::runtime_error("回放部署被拒绝: " + operation.unit);
        }
        return;
    }

    void* character = replay_character(controller, operation.unit);
    if (!character) {
        throw std::runtime_error("回放找不到场上单位: " + operation.unit);
    }
    Root character_root(runtime_, character);
    if (operation.kind == RecordedOperation::Kind::Skill) {
        void* skill_id = runtime_.string_new("");
        Root skill_id_root(runtime_, skill_id);
        void* trigger = runtime_.method(controller_class, "PlayerOp_TrigSkill", 2);
        void* result =
            runtime_.invoke(trigger, controller, {character, skill_id});
        if (!result || !runtime_.unbox<bool>(result)) {
            throw std::runtime_error("回放技能被拒绝: " + operation.unit);
        }
        return;
    }
    void* withdraw = runtime_.method(controller_class, "PlayerOp_Withdraw", 1);
    void* result = runtime_.invoke(withdraw, controller, {character});
    if (!result || !runtime_.unbox<bool>(result)) {
        throw std::runtime_error("回放撤退被拒绝: " + operation.unit);
    }
}

void LocalBattle::on_battle_fixed_update(void* self, void* method) {
    LocalBattle* owner = fixed_observer_owner.load(std::memory_order_acquire);
    if (!owner) {
        return;
    }
    const std::uint64_t tick_started = perf_now_us();
    auto original =
        reinterpret_cast<void (*)(void*, void*)>(owner->battle_observer_original_);
    const bool stale_replay_controller =
        owner->replay_active_.load(std::memory_order_acquire) &&
        owner->timeline_waiting_for_controller_ &&
        (!self || self == owner->scene_.controller ||
         (self == owner->timeline_stale_controller_ &&
          owner->scene_.revision ==
              owner->timeline_stale_scene_revision_));
    if (!stale_replay_controller) {
        owner->prepare_timeline_for_controller(self);
    }
    if (original) {
        original(self, method);
    }
    if (stale_replay_controller || !self ||
        owner->stopping_.load(std::memory_order_acquire)) {
        owner->perf_.fixed_tick.add(perf_now_us() - tick_started);
        return;
    }
    owner->complete_timeline_fast_forward_if_due(self);
    if (owner->tas_mode_.load(std::memory_order_acquire)) {
        owner->enforce_spine_auto(self);
    }
    if (self && owner->probe_enabled_.load(std::memory_order_acquire)) {
        owner->probe_frame(self);
    }
    void* manager = nullptr;
    read_field_deep(owner->runtime_, self, owner->classes_.battle_controller,
                    "m_objectManger", &manager);
    if (!manager || self == owner->scene_.controller) {
        owner->perf_.fixed_tick.add(perf_now_us() - tick_started);
        return;
    }
    try {
        owner->adopt_scene(self);
    } catch (...) {
        owner->clear_scene();
    }
    owner->perf_.fixed_tick.add(perf_now_us() - tick_started);
}

void LocalBattle::on_battle_finish_game(void* self, int result, bool show,
                                        void* callback, void* method) {
    LocalBattle* owner = finish_game_owner.load(std::memory_order_acquire);
    auto original = owner
                        ? reinterpret_cast<void (*)(void*, int, bool, void*, void*)>(
                              owner->finish_game_original_)
                        : nullptr;
    if (!owner || !owner->local_battle_active_.load(std::memory_order_acquire) ||
        owner->finish_bypassed_) {
        if (original) {
            original(self, result, show, callback, method);
        }
        return;
    }
    owner->finish_bypassed_ = true;
    owner->finish_home_pending_.store(true, std::memory_order_release);
    owner->log(kLogInfo, "本地战斗结束 result=" + std::to_string(result) +
                             "，跳过服务器同步，准备返回 HOME");
}

void LocalBattle::on_replay_fixed_update(void* self, void* method) {
    LocalBattle* owner = replay_owner.load(std::memory_order_acquire);
    auto original = owner
                        ? reinterpret_cast<void (*)(void*, void*)>(
                              owner->replay_fixed_original_)
                        : nullptr;
    if (!owner) {
        return;
    }
    if (owner->timeline_fast_running_.load(std::memory_order_acquire)) {
        if (original) {
            original(self, method);
        }
        return;
    }
    if (owner->timeline_waiting_for_controller_) {
        if (owner->timeline_stale_controller_ &&
            self == owner->timeline_stale_controller_ &&
            owner->scene_.revision ==
                owner->timeline_stale_scene_revision_) {
            if (original) {
                original(self, method);
            }
            return;
        }
            if (!self) {
                if (original) {
                    original(self, method);
                }
                return;
            }
            owner->timeline_waiting_for_controller_ = false;
    }
    if (!self) {
        if (original) {
            original(self, method);
        }
        return;
    }
    owner->prepare_timeline_for_controller(self);

    if (owner->pending_replay_seek_fast_.load(std::memory_order_acquire)) {
        int state = -1;
        try {
            state = method_int(owner->runtime_, self,
                               owner->classes_.battle_controller,
                               "get_state", -1);
        } catch (...) {
            state = -1;
        }
        if (state < 2) {
            if (original) {
                original(self, method);
            }
            return;
        }
    }
    if (owner->pending_replay_seek_fast_.exchange(
            false, std::memory_order_acq_rel)) {
        const std::uint32_t target =
            owner->pending_replay_seek_target_.exchange(
                0, std::memory_order_acq_rel);
        owner->run_fast_replay_seek(self, target);
        return;
    }
    if (owner->pending_timeline_fast_) {
        bool seed_ready = false;
        {
            std::lock_guard lock(owner->record_mutex_);
            seed_ready = owner->timeline_seed_apply_done_;
        }
        if (!seed_ready) {
            if (original) {
                original(self, method);
            }
            return;
        }
        owner->run_fast_timeline_jump(self);
        return;
    }

    std::uint32_t before = 0;
    try {
        before = owner->replay_frame_get_
                     ? owner->replay_frame_get_(owner->replay_frame_method_)
                     : 0;
    } catch (...) {
        before = 0;
    }
    owner->timeline_frame_enter(self, before);
    if (original) {
        original(self, method);
    }
    std::uint32_t after = before;
    try {
        after = owner->replay_frame_get_
                    ? owner->replay_frame_get_(owner->replay_frame_method_)
                    : before;
    } catch (...) {
        after = before;
    }
    if (after > before) {
        for (std::uint32_t frame = before; frame < after; ++frame) {
            owner->timeline_frame_commit(frame);
        }
    }

    const bool stop_enabled =
        owner->replay_stop_enabled_.load(std::memory_order_acquire);
    const std::uint32_t stop_frame =
        owner->replay_stop_frame_.load(std::memory_order_acquire);
    if (stop_enabled && after > stop_frame) {
        owner->replay_frame_ = std::min(after, stop_frame);
        owner->replay_stop_frame_.store(0, std::memory_order_release);
        owner->replay_stop_enabled_.store(false,
                                          std::memory_order_release);
        try {
            owner->adopt_scene(self);
        } catch (...) {
            owner->clear_scene();
        }
        if (owner->pending_timeline_jump_) {
            owner->finish_timeline_rewind(
                stop_frame, owner->pending_timeline_play_);
        } else {
            owner->replay_active_.store(false, std::memory_order_release);
            owner->set_paused(true);
            owner->publish_result(
                "录像停靠在第 " + std::to_string(stop_frame) + " 帧");
        }
        return;
    }
    try {
        owner->adopt_scene(self);
    } catch (...) {
        owner->clear_scene();
    }
}

}  // namespace ark::better_battle
