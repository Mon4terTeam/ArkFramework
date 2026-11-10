#include "battle_helpers.h"

namespace ark::better_battle {

void LocalBattle::rogue_start(const BattleStart& start) {
    if (start.stage.empty() || start.path.empty()) {
        publish_result("肉鸽主题或关卡为空", false);
        return;
    }
    if (start.squad.empty() || start.squad.size() > 12) {
        publish_result("编队必须包含 1 到 12 名干员", false);
        return;
    }
    try {
        remember_previous_controller_for_restart();
        void* db_class = classes_.roguelike_topic_db;
        void* db = runtime_.singleton(db_class);
        Root db_root(runtime_, db);
        void* get_inited = runtime_.method(db_class, "get_inited", 0);
        void* inited_value = runtime_.invoke(get_inited, db);
        if (!inited_value || !runtime_.unbox<bool>(inited_value)) {
            throw std::runtime_error(
                "RoguelikeTopicDB 未初始化，请先回到主界面再重试");
        }
        void* get_data = runtime_.method(db_class, "get_data", 0);
        void* table = runtime_.invoke(get_data, db);
        if (!table) {
            throw std::runtime_error("RoguelikeTopicDB.get_data() 返回 null");
        }
        Root table_root(runtime_, table);
        void* table_class = runtime_.object_class(table);
        void* details = nullptr;
        read_field_deep(runtime_, table, table_class, "details", &details);
        if (!details) {
            throw std::runtime_error("RoguelikeTopicTable.details 为 null");
        }
        void* detail = dictionary_value_for_key(runtime_, details, start.stage);
        if (!detail) {
            throw std::runtime_error("肉鸽主题不存在: " + start.stage);
        }
        Root detail_root(runtime_, detail);
        void* detail_class = runtime_.object_class(detail);
        void* stages = nullptr;
        read_field_deep(runtime_, detail, detail_class, "stages", &stages);
        if (!stages) {
            throw std::runtime_error("肉鸽主题没有 stage 表: " + start.stage);
        }
        void* stage = dictionary_value_for_key(runtime_, stages, start.path);
        if (!stage) {
            throw std::runtime_error("肉鸽关卡不存在: " + start.path);
        }
        Root stage_root(runtime_, stage);
        void* stage_class = runtime_.object_class(stage);

        const std::string stage_code =
            read_string_field(runtime_, stage, stage_class, "code");
        const std::string stage_name =
            read_string_field(runtime_, stage, stage_class, "name");
        const std::string loading_pic =
            read_string_field(runtime_, stage, stage_class, "loadingPicId");
        const std::string base_level =
            read_string_field(runtime_, stage, stage_class, "levelId");

        std::string final_level;
        void* data_util = classes_.roguelike_data_util;
        void* get_final_level =
            runtime_.method(data_util, "GetStageFinalLevelId", 1);
        void* final_level_text =
            runtime_.invoke(get_final_level, nullptr, {stage});
        if (final_level_text) {
            Root final_level_root(runtime_, final_level_text);
            final_level = runtime_.string(final_level_text);
        }
        if (final_level.empty()) {
            final_level = base_level;
        }
        if (final_level.empty()) {
            throw std::runtime_error("RoguelikeDataUtil.GetStageFinalLevelId 为空");
        }

        void* level_utils = classes_.level_utils;
        void* load_level = runtime_.method(level_utils, "LoadLevel", 1);
        void* level_text = runtime_.string_new(final_level.c_str());
        Root level_text_root(runtime_, level_text);
        void* level_data = runtime_.invoke(load_level, nullptr, {level_text});
        if (!level_data) {
            throw std::runtime_error("LevelUtils.LoadLevel 返回 null: " + final_level);
        }
        Root level_root(runtime_, level_data);

        void* query_class = classes_.char_query;
        void* inst_class = classes_.advanced_character_inst;
        void* simple_char = runtime_.method(query_class, "SimpleChar", 1);
        void* inst_ctor = runtime_.method(inst_class, ".ctor", 5);
        void* pair_list_class = runtime_.class_from_type(runtime_.method_param(inst_ctor, 4));
        if (!pair_list_class) {
            throw std::runtime_error("AdvancedCharacterInst 模组列表类型不可用");
        }
        void* pair_list_ctor = runtime_.method(pair_list_class, ".ctor", 0);
        void* pair_list_add = runtime_.method(pair_list_class, "Add", 1);
        const UniqueEquipPairLayout pair_layout =
            resolve_unique_equip_pair(runtime_, inst_class);
        if (!pair_layout.valid()) {
            throw std::runtime_error("UniqueEquipPair 字段不可用");
        }

        void* converter = classes_.battle_data_converter;
        void* convert = runtime_.method(converter, "ConvertToBattlePlayerData", 8);
        void* runtime_convert =
            runtime_.method(converter, "ConvertToRuntimeCharacterData", 4,
                            {"System.Collections.Generic.List<Torappu.AdvancedCharacterInst>",
                             "System.Boolean", "System.Boolean", "Torappu.PlayerSide"});
        void* inst_list_class =
            runtime_.class_from_type(runtime_.method_param(runtime_convert, 0));
        void* inst_list_ctor = runtime_.method(inst_list_class, ".ctor", 0);
        void* inst_list_add = runtime_.method(inst_list_class, "Add", 1);
        void* instances = construct_with_args(runtime_, inst_list_class, inst_list_ctor, {});
        Root instances_root(runtime_, instances);

        void* character_util = classes_.character_util;
        void* create_master_infos = nullptr;
        void* master_dict_class = nullptr;
        void* master_dict_ctor = nullptr;
        void* master_dict_add = nullptr;
        if (character_util && !start.masters.empty()) {
            create_master_infos = runtime_.method(
                character_util, "CreateMasterInfosFromMasterDict", 1);
            master_dict_class = runtime_.class_from_type(
                runtime_.method_param(create_master_infos, 0));
            master_dict_ctor = runtime_.method(master_dict_class, ".ctor", 0);
            master_dict_add = runtime_.method(master_dict_class, "Add", 2);
        }

        for (const SquadSlot& slot : start.squad) {
            void* char_text = runtime_.string_new(slot.id.c_str());
            Root char_text_root(runtime_, char_text);
            void* query = runtime_.invoke(simple_char, nullptr, {char_text});
            if (!query) {
                throw std::runtime_error("CharQuery.SimpleChar 返回 null: " + slot.id);
            }
            Root query_root(runtime_, query);
            void* pair_list =
                construct_with_args(runtime_, pair_list_class, pair_list_ctor, {});
            Root pair_list_root(runtime_, pair_list);
            if (!slot.module_id.empty()) {
                void* module_text = runtime_.string_new(slot.module_id.c_str());
                Root module_text_root(runtime_, module_text);
                void* pair = box_zero(runtime_, pair_layout.klass);
                Root pair_root(runtime_, pair);
                runtime_.field_set_object(pair, pair_layout.id_field, module_text);
                int module_level = std::clamp(slot.module_level, 1, 3);
                runtime_.field_set(pair, pair_layout.level_field, &module_level);
                runtime_.invoke(pair_list_add, pair_list, {runtime_.object_unbox(pair)});
            }
            int level = slot.level;
            int evolve = slot.evolve;
            int potential = slot.potential;
            void* query_value = runtime_.object_unbox(query);
            if (!query_value) {
                throw std::runtime_error("CharQuery 无法拆箱: " + slot.id);
            }
            void* instance = runtime_.object_new(inst_class);
            Root instance_root(runtime_, instance);
            runtime_.invoke(inst_ctor, instance,
                            {query_value, &level, &evolve, &potential, pair_list});
            const int skill_index = std::clamp(slot.skill_index, 0, 2);
            const int skill_level = std::clamp(slot.skill_level, 1, 10);
            write_field_deep(runtime_, instance, inst_class, "skillIndex", skill_index);
            write_field_deep(runtime_, instance, inst_class, "mainSkillLvl", skill_level);
            if (!write_character_inst_trust(runtime_, instance, inst_class,
                                            slot.trust)) {
                throw std::runtime_error("写入干员信赖失败: " + slot.id);
            }
            std::vector<const BattleStart::MasterSpec*> master_specs;
            for (const auto& spec : start.masters) {
                if (spec.char_id == slot.id) {
                    master_specs.push_back(&spec);
                }
            }
            if (!master_specs.empty() && create_master_infos) {
                void* master_dict = construct_with_args(
                    runtime_, master_dict_class, master_dict_ctor, {});
                Root master_dict_root(runtime_, master_dict);
                std::string summary;
                for (const BattleStart::MasterSpec* spec : master_specs) {
                    void* master_key = runtime_.string_new(
                        spec->master_id.c_str());
                    Root master_key_root(runtime_, master_key);
                    int master_level = std::clamp(spec->level, 1, 3);
                    runtime_.invoke(master_dict_add, master_dict,
                                    {master_key, &master_level});
                    if (!summary.empty()) {
                        summary += ',';
                    }
                    summary += spec->master_id + "@" +
                               std::to_string(master_level);
                }
                void* master_infos = runtime_.invoke(
                    create_master_infos, nullptr, {master_dict});
                if (master_infos) {
                    runtime_.set<void*>(instance, "masterInfos",
                                        master_infos);
                    log(kLogInfo, "特勤战斗精通注入: " + slot.id + " " +
                                      summary);
                } else {
                    log(kLogWarning, "特勤战斗精通转换失败: " + slot.id);
                }
            }
            runtime_.invoke(inst_list_add, instances, {instance});
        }

        int difficulty = 0;
        read_field_deep(runtime_, stage, stage_class, "difficulty", &difficulty);
        void* menu_style_class = classes_.battle_sys_menu_style;
        void* side_class = classes_.player_side;
        const int menu_style = runtime_.static_value<int>(menu_style_class, "NO_REWARD");
        int player_side = runtime_.static_value<int>(side_class, "DEFAULT");
        if (start.game_mode.valid) {
            player_side = start.game_mode.player_side;
        }

        void* player_list_field = runtime_.field(
            runtime_.nested(classes_.battle_in_out, "InParams"),
            "playerDataList");
        void* player_list_class =
            runtime_.class_from_type(runtime_.field_type(player_list_field));
        void* player_list_ctor = runtime_.method(player_list_class, ".ctor", 0);
        void* player_list_add = runtime_.method(player_list_class, "Add", 1);
        void* player_list =
            construct_with_args(runtime_, player_list_class, player_list_ctor, {});
        Root player_list_root(runtime_, player_list);

        unsigned char reserved = 0;
        unsigned char add_player = 1;
        unsigned char tail = 0;
        void* player_data = runtime_.invoke(
            convert, nullptr,
            {instances, level_data, &difficulty, nullptr, &reserved, &player_side,
             &add_player, &tail});
        if (!player_data) {
            throw std::runtime_error("ConvertToBattlePlayerData 返回 null");
        }
        Root player_data_root(runtime_, player_data);
        runtime_.invoke(player_list_add, player_list, {player_data});

        void* stage_info_class = classes_.battle_stage_info;
        void* stage_info = box_zero(runtime_, stage_info_class);
        Root stage_info_root(runtime_, stage_info);
        void* final_level_value = runtime_.string_new(final_level.c_str());
        Root final_level_value_root(runtime_, final_level_value);
        void* code_value = runtime_.string_new(stage_code.c_str());
        Root code_value_root(runtime_, code_value);
        void* name_value = runtime_.string_new(stage_name.c_str());
        Root name_value_root(runtime_, name_value);
        void* pic_value = runtime_.string_new(loading_pic.c_str());
        Root pic_value_root(runtime_, pic_value);
        runtime_.set<void*>(stage_info, "stageId", final_level_value);
        runtime_.set<void*>(stage_info, "code", code_value);
        runtime_.set<void*>(stage_info, "name", name_value);
        runtime_.set<void*>(stage_info, "levelId", final_level_value);
        runtime_.set<void*>(stage_info, "loadingPicId", pic_value);
        runtime_.set<int>(stage_info, "difficulty", difficulty);
        void* stage_type_class = runtime_.find("Torappu", "StageType");
        const int stage_type_enum = runtime_.static_value<int>(stage_type_class, "ENUM");
        runtime_.set<int>(stage_info, "stageType", stage_type_enum);

        void* meta_class = runtime_.find("Torappu.Battle", "BattleRoguelikeMeta");
        void* meta = box_zero(runtime_, meta_class);
        Root meta_root(runtime_, meta);
        void* topic_value = runtime_.string_new(start.stage.c_str());
        Root topic_value_root(runtime_, topic_value);
        runtime_.set<void*>(meta, "topicId", topic_value);
        runtime_.set<int>(meta, "gold", std::max(start.gold_bonus, 0));
        runtime_.set<int>(meta, "remainPopulation", 6);
        runtime_.set<int>(meta, "san", 100);
        runtime_.set<int>(meta, "mode", start.mode);
        int scrap_count = start.scrap_count >= 0 ? start.scrap_count : 0;
        int scrap_limit = start.scrap_limit >= 0 ? start.scrap_limit : 10;
        bool has_scrap_meta = start.scrap_count >= 0 || start.scrap_limit >= 0;
        for (const auto& [relic_id, layer] : start.relics) {
            if (relic_id == "rogue_6_relic_cargo_2") {
                has_scrap_meta = true;
                if (start.scrap_count < 0) {
                    scrap_count = std::max(scrap_count, std::clamp(layer, 0, 99));
                }
            } else if (relic_id == "rogue_6_relic_cargo_3") {
                has_scrap_meta = true;
            }
        }
        if (has_scrap_meta) {
            runtime_.set<int>(meta, "scrapInventoryCount", scrap_count);
            runtime_.set<int>(meta, "scrapInventoryLimit", scrap_limit);
            runtime_.set<int>(meta, "scrapInventoryValue", scrap_count);
        }
        void* event_type_class = runtime_.find("Torappu", "RoguelikeEventType");
        const char* node_name = "BATTLE_NORMAL";
        if (start.path.find("_b_") != std::string::npos) {
            node_name = "BATTLE_BOSS";
        } else if (start.path.find("_e_") != std::string::npos) {
            node_name = "BATTLE_ELITE";
        }
        runtime_.set<int>(meta, "nodeType",
                          runtime_.static_value<int>(event_type_class, node_name));
        runtime_.set<int>(meta, "seed", start.rogue_seed);
        void* zone_type_class = runtime_.find("Torappu", "PlayerRoguelikeZoneType");
        runtime_.set<int>(meta, "zoneType",
                          runtime_.static_value<int>(zone_type_class, "NORMAL"));
        void* sp_zone_class = runtime_.find("Torappu", "RoguelikeSpZoneNodeType");
        runtime_.set<int>(meta, "spZoneNodeType",
                          runtime_.static_value<int>(sp_zone_class, "NORMAL"));
        void* fail_display_class =
            runtime_.find("Torappu", "RoguelikeBattleFailDisplay");
        runtime_.set<int>(meta, "battleFailDisplay",
                          runtime_.static_value<int>(fail_display_class, "NORMAL"));
        runtime_.set<void*>(meta, "tmpCharList",
                            empty_collection_for_field(runtime_, meta_class, "tmpCharList"));
        void* meta_char_buffs =
            empty_collection_for_field(runtime_, meta_class, "charBuffs");
        Root meta_char_buffs_root(runtime_, meta_char_buffs);
        void* candle_holder_dict = empty_collection_for_field(
            runtime_, meta_class, "hasCandleHolderBuffCharDict");
        Root candle_holder_dict_root(runtime_, candle_holder_dict);
        void* candle_holder_add =
            runtime_.method(runtime_.object_class(candle_holder_dict), "Add", 2);
        std::string candle_holder_buff_id;
        try {
            void* get_candle_buff_id =
                runtime_.method(data_util, "GetCandleBuffId", 1);
            void* candle_buff_value =
                runtime_.invoke(get_candle_buff_id, nullptr, {topic_value});
            if (candle_buff_value) {
                candle_holder_buff_id = runtime_.string(candle_buff_value);
            }
        } catch (...) {
        }
        void* local_char_buff_table = nullptr;
        read_field_deep(runtime_, detail, detail_class, "charBuffData",
                        &local_char_buff_table);
        std::size_t applied_char_buffs = 0;
        std::unordered_map<std::string, std::uint32_t> player_char_unique_ids;
        void* player_characters = nullptr;
        if (read_field_deep(runtime_, player_data,
                            runtime_.object_class(player_data), "characters",
                            &player_characters) &&
            player_characters) {
            Root player_characters_root(runtime_, player_characters);
            const int character_count = runtime_.count(player_characters);
            for (int index = 0; index < character_count; ++index) {
                void* character = nullptr;
                try {
                    character = runtime_.at(player_characters, index);
                } catch (...) {
                    continue;
                }
                if (!character) {
                    continue;
                }
                Root character_root(runtime_, character);
                void* character_class = runtime_.object_class(character);
                std::string prefab_key;
                std::uint32_t unique_id = 0;
                try {
                    prefab_key = read_string_field(
                        runtime_, character, character_class, "prefabKey");
                    read_field_deep(runtime_, character, character_class,
                                    "uniqueId", &unique_id);
                } catch (...) {
                    continue;
                }
                if (!prefab_key.empty() && unique_id) {
                    player_char_unique_ids.emplace(std::move(prefab_key),
                                                   unique_id);
                }
            }
        }
        for (const auto& char_buff_spec : start.char_buffs) {
            const std::string& char_buff_id = char_buff_spec.id;
            const int layer = char_buff_spec.layer;
            void* source_entry =
                local_char_buff_table
                    ? dictionary_value_for_key(runtime_, local_char_buff_table,
                                               char_buff_id)
                    : nullptr;
            void* source_buffs = nullptr;
            if (source_entry) {
                read_field_deep(runtime_, source_entry,
                                runtime_.object_class(source_entry), "buffs",
                                &source_buffs);
            }
            void* char_buff_class =
                runtime_.find("Torappu", "RoguelikeGameCharBuffBattleData");
            void* char_buff = box_zero(runtime_, char_buff_class);
            Root char_buff_root(runtime_, char_buff);
            void* id_value = runtime_.string_new(char_buff_id.c_str());
            Root id_value_root(runtime_, id_value);
            runtime_.set<void*>(char_buff, "charBuffId", id_value);
            void* unique_ids = empty_collection_for_field(
                runtime_, char_buff_class, "charUniqueIds");
            Root unique_ids_root(runtime_, unique_ids);
            void* unique_ids_add =
                runtime_.method(runtime_.object_class(unique_ids), "Add", 1);
            std::size_t matched_targets = 0;
            for (const std::string& target : char_buff_spec.targets) {
                const auto found = player_char_unique_ids.find(target);
                if (found == player_char_unique_ids.end()) {
                    log(kLogWarning, "CharBuff 目标干员不在本局: " +
                                         char_buff_id + " target=" + target);
                    continue;
                }
                std::uint32_t unique_id = found->second;
                runtime_.invoke(unique_ids_add, unique_ids, {&unique_id});
                if (!candle_holder_buff_id.empty() &&
                    char_buff_id == candle_holder_buff_id) {
                    void* char_id_value =
                        runtime_.string_new(target.c_str());
                    Root char_id_value_root(runtime_, char_id_value);
                    int candle_count = 1;
                    runtime_.invoke(candle_holder_add, candle_holder_dict,
                                    {char_id_value, &candle_count});
                }
                ++matched_targets;
            }
            runtime_.set<void*>(char_buff, "charUniqueIds", unique_ids);
            runtime_.set<void*>(
                char_buff, "buffs",
                source_buffs
                    ? source_buffs
                    : empty_collection_for_field(runtime_, char_buff_class,
                                                 "buffs"));
            collection_add(runtime_, meta_char_buffs, char_buff);
            ++applied_char_buffs;
            log(kLogInfo, "CharBuff 注入: " + char_buff_id + " layer=" +
                              std::to_string(layer) + " table=" +
                              (source_buffs ? "1" : "0") + " targets=" +
                              std::to_string(char_buff_spec.targets.size()) +
                              " matched=" + std::to_string(matched_targets));
        }
        runtime_.set<void*>(meta, "charBuffs", meta_char_buffs);
        if (!candle_holder_buff_id.empty()) {
            log(kLogInfo, "伺烛客注入: buff=" + candle_holder_buff_id +
                              " count=" +
                              std::to_string(runtime_.count(candle_holder_dict)));
        }
        runtime_.set<void*>(meta, "diceRoll",
                            empty_collection_for_field(runtime_, meta_class, "diceRoll"));
        runtime_.set<void*>(meta, "boxInfo",
                            empty_collection_for_field(runtime_, meta_class, "boxInfo"));
        runtime_.set<void*>(meta, "fragmentCarryCharUniqueList",
                            empty_collection_for_field(
                                runtime_, meta_class, "fragmentCarryCharUniqueList"));
        runtime_.set<void*>(meta, "enemyHpInfo",
                            empty_collection_for_field(runtime_, meta_class, "enemyHpInfo"));
        runtime_.set<void*>(meta, "hasCandleHolderBuffCharDict",
                            candle_holder_dict);
        runtime_.set<void*>(meta, "employCharDict",
                            empty_collection_for_field(runtime_, meta_class, "employCharDict"));

        void* input_class =
            runtime_.find("Torappu.Battle.Roguelike", "RoguelikeInput");
        void* input_ctor = runtime_.method(input_class, ".ctor", 0);
        void* input = construct_with_args(runtime_, input_class, input_ctor, {});
        Root input_root(runtime_, input);
        void* outer_buffs =
            empty_collection_for_field(runtime_, input_class, "outerBuffs");
        Root outer_buffs_root(runtime_, outer_buffs);
        void* relic_buffs =
            empty_collection_for_field(runtime_, input_class, "relicBuffs");
        Root relic_buffs_root(runtime_, relic_buffs);
        void* extra_buffs =
            empty_collection_for_field(runtime_, input_class, "extraBuffs");
        Root extra_buffs_root(runtime_, extra_buffs);

        void* relic_table = nullptr;
        void* char_buff_table = nullptr;
        void* squad_buff_table = nullptr;
        read_field_deep(runtime_, detail, detail_class, "relics", &relic_table);
        read_field_deep(runtime_, detail, detail_class, "charBuffData",
                        &char_buff_table);
        read_field_deep(runtime_, detail, detail_class, "squadBuffData",
                        &squad_buff_table);

        std::size_t applied_relics = 0;
        for (const auto& [relic_id, layer] : start.relics) {
            void* relic_data =
                relic_table ? dictionary_value_for_key(runtime_, relic_table,
                                                       relic_id)
                            : nullptr;
            if (!relic_data) {
                log(kLogWarning, "肉鸽藏品不存在，已跳过: " + relic_id);
                continue;
            }
            Root relic_data_root(runtime_, relic_data);
            const std::string relic_data_class =
                runtime_.klass_name(runtime_.object_class(relic_data));
            const std::string relic_data_id =
                read_string_field(runtime_, relic_data,
                                  runtime_.object_class(relic_data), "id");
            void* relic_buffs_data = nullptr;
            read_field_deep(runtime_, relic_data, runtime_.object_class(relic_data),
                            "buffs", &relic_buffs_data);
            int relic_buff_count = 0;
            std::string relic_buff_keys;
            if (relic_buffs_data) {
                Root relic_buffs_data_root(runtime_, relic_buffs_data);
                relic_buff_count = runtime_.count(relic_buffs_data);
                for (int index = 0; index < relic_buff_count; ++index) {
                    void* buff_data = nullptr;
                    try {
                        buff_data = runtime_.at(relic_buffs_data, index);
                    } catch (...) {
                        continue;
                    }
                    if (!buff_data) {
                        continue;
                    }
                    Root buff_data_root(runtime_, buff_data);
                    const std::string key = read_string_field(
                        runtime_, buff_data, runtime_.object_class(buff_data),
                        "key");
                    if (!relic_buff_keys.empty()) {
                        relic_buff_keys += ',';
                    }
                    relic_buff_keys += key;
                }
            }
            log(kLogInfo, "肉鸽藏品注入: " + relic_id + " layer=" +
                              std::to_string(layer) + " buffs=" +
                              std::to_string(relic_buff_count) + " id=" +
                              relic_data_id + " class=" + relic_data_class +
                              " keys=" + relic_buff_keys);
            void* item_class =
                runtime_.find("Torappu.Battle", "BattleRoguelikeRelicBuff");
            void* item = box_zero(runtime_, item_class);
            Root item_root(runtime_, item);
            runtime_.set<void*>(item, "relicData", relic_data);
            runtime_.set<int>(item, "layer", std::clamp(layer, 1, 999));
            void* stored_relic_data = runtime_.get_object(item, "relicData");
            const int stored_layer = runtime_.get<int>(item, "layer");
            log(kLogInfo, "肉鸽藏品实例: " + relic_id + " data_match=" +
                              (stored_relic_data == relic_data ? "1" : "0") +
                              " stored_layer=" + std::to_string(stored_layer));
            collection_add(runtime_, relic_buffs, item);
            ++applied_relics;
        }

        std::size_t applied_buff_items = 0;
        for (const auto& [item_id, layer] : start.buff_items) {
            void* item_data =
                relic_table ? dictionary_value_for_key(runtime_, relic_table,
                                                       item_id)
                            : nullptr;
            if (!item_data) {
                log(kLogWarning, "Buff 物品不存在，已跳过: " + item_id);
                continue;
            }
            Root item_data_root(runtime_, item_data);
            void* item_buffs = nullptr;
            read_field_deep(runtime_, item_data, runtime_.object_class(item_data),
                            "buffs", &item_buffs);
            if (!item_buffs) {
                log(kLogWarning, "Buff 物品没有 buffs: " + item_id);
                continue;
            }
            Root item_buffs_root(runtime_, item_buffs);
            const int buff_count = runtime_.count(item_buffs);
            if (buff_count <= 0) {
                log(kLogWarning, "Buff 物品 buffs 为空: " + item_id);
                continue;
            }
            void* extra_class =
                classes_.roguelike_topic_extra_buff_data;
            void* extra = box_zero(runtime_, extra_class);
            Root extra_root(runtime_, extra);
            void* extra_id_value = runtime_.string_new(item_id.c_str());
            Root extra_id_root(runtime_, extra_id_value);
            runtime_.set<void*>(extra, "id", extra_id_value);
            runtime_.set<int>(extra, "layer", std::clamp(layer, 1, 999));
            runtime_.set<void*>(extra, "buffs", item_buffs);
            collection_add(runtime_, extra_buffs, extra);
            ++applied_buff_items;
            log(kLogInfo, "Buff 物品注入: " + item_id + " layer=" +
                              std::to_string(layer) + " buffs=" +
                              std::to_string(buff_count));
        }

        std::size_t applied_extras = 0;
        for (const auto& [kind, extra_id, layer] : start.extra_buffs) {
            void* source_buffs = nullptr;
            void* source = nullptr;
            if (kind == "mutation" && char_buff_table) {
                source =
                    dictionary_value_for_key(runtime_, char_buff_table, extra_id);
            } else if ((kind == "virtue" || kind == "variation") &&
                       squad_buff_table) {
                source =
                    dictionary_value_for_key(runtime_, squad_buff_table, extra_id);
            }
            if (source) {
                read_field_deep(runtime_, source, runtime_.object_class(source),
                                "buffs", &source_buffs);
            }
            int source_buff_count = 0;
            std::string source_buff_keys;
            if (source_buffs) {
                Root source_buffs_root(runtime_, source_buffs);
                source_buff_count = runtime_.count(source_buffs);
                for (int index = 0; index < source_buff_count; ++index) {
                    void* buff_data = nullptr;
                    try {
                        buff_data = runtime_.at(source_buffs, index);
                    } catch (...) {
                        continue;
                    }
                    if (!buff_data) {
                        continue;
                    }
                    Root buff_data_root(runtime_, buff_data);
                    const std::string key = read_string_field(
                        runtime_, buff_data, runtime_.object_class(buff_data),
                        "key");
                    if (!source_buff_keys.empty()) {
                        source_buff_keys += ',';
                    }
                    source_buff_keys += key;
                }
            }
            log(kLogInfo, "肉鸽特殊Buff注入: " + kind + ":" + extra_id +
                              " source=" + (source ? "1" : "0") +
                              " buffs=" + std::to_string(source_buff_count) +
                              " keys=" + source_buff_keys);
            if (kind == "virtue" && source_buffs) {
                Root source_buffs_root(runtime_, source_buffs);
                const int count = runtime_.count(source_buffs);
                for (int index = 0; index < count; ++index) {
                    void* buff_data = nullptr;
                    try {
                        buff_data = runtime_.at(source_buffs, index);
                    } catch (...) {
                        continue;
                    }
                    if (buff_data) {
                        collection_add(runtime_, outer_buffs, buff_data);
                    }
                }
                ++applied_extras;
                continue;
            }
            void* extra_class =
                classes_.roguelike_topic_extra_buff_data;
            void* extra = box_zero(runtime_, extra_class);
            Root extra_root(runtime_, extra);
            void* extra_id_value = runtime_.string_new(extra_id.c_str());
            Root extra_id_root(runtime_, extra_id_value);
            runtime_.set<void*>(extra, "id", extra_id_value);
            runtime_.set<int>(extra, "layer", std::clamp(layer, 0, 9));
            if (source_buffs) {
                runtime_.set<void*>(extra, "buffs", source_buffs);
            } else {
                runtime_.set<void*>(
                    extra, "buffs",
                    empty_collection_for_field(runtime_, extra_class, "buffs"));
            }
            collection_add(runtime_, extra_buffs, extra);
            ++applied_extras;
        }

        if (start.official_extra) {
            const std::vector<std::vector<std::string>> plugin_paths = {
                {"Torappu.UI.Roguelike", "RoguelikeSquadStatePlugin"},
                {"Torappu.UI.Roguelike.RL05", "RL05SquadStatePlugin"},
                {"Torappu.UI.Roguelike.RL06", "RL06SquadStatePlugin"}};
            void* topic_value = runtime_.string_new(start.stage.c_str());
            Root topic_value_root(runtime_, topic_value);
            for (const auto& path : plugin_paths) {
                try {
                    void* plugin_class =
                        runtime_.find(path[0].c_str(), path[1].c_str());
                    void* plugin_ctor =
                        runtime_.method(plugin_class, ".ctor", 0);
                    void* plugin = construct_with_args(
                        runtime_, plugin_class, plugin_ctor, {});
                    Root plugin_root(runtime_, plugin);
                    void* method = runtime_.method(
                        plugin_class, "GetTopicExtraBuffs", 1,
                        {"System.String"});
                    void* list = runtime_.invoke(method, plugin, {topic_value});
                    Root list_root(runtime_, list);
                    const int count = runtime_.count(list);
                    for (int index = 0; index < count; ++index) {
                        void* item = nullptr;
                        try {
                            item = runtime_.at(list, index);
                        } catch (...) {
                            continue;
                        }
                        if (item) {
                            collection_add(runtime_, extra_buffs, item);
                        }
                    }
                    log(kLogInfo, "官方肉鸽机制并入 " + path[1] + " count=" +
                                      std::to_string(count));
                } catch (const std::exception& error) {
                    log(kLogWarning, "官方肉鸽机制跳过 " + path[1] + ": " +
                                         error.what());
                }
            }
        }

        void* buff_class = runtime_.find("Torappu", "RoguelikeBuff");
        void* blackboard_class = runtime_.find("Torappu", "Blackboard");
        void* blackboard_ctor = runtime_.method(blackboard_class, ".ctor", 0);
        void* assign_float = runtime_.method(
            blackboard_class, "Assign", 2,
            {"System.String", "System.Single"});
        void* assign_string = runtime_.method(
            blackboard_class, "Assign", 2,
            {"System.String", "System.String"});
        std::size_t applied_outer = 0;
        for (const auto& [key, field, value] : start.outer_buffs) {
            void* buff = box_zero(runtime_, buff_class);
            Root buff_root(runtime_, buff);
            void* key_value = runtime_.string_new(key.c_str());
            Root key_root(runtime_, key_value);
            void* blackboard =
                construct_with_args(runtime_, blackboard_class, blackboard_ctor,
                                    {});
            Root blackboard_root(runtime_, blackboard);
            void* field_value = runtime_.string_new(field.c_str());
            Root field_root(runtime_, field_value);
            float number = static_cast<float>(value);
            runtime_.invoke(assign_float, blackboard, {field_value, &number});
            runtime_.set<void*>(buff, "key", key_value);
            runtime_.set<void*>(buff, "blackboard", blackboard);
            collection_add(runtime_, outer_buffs, buff);
            ++applied_outer;
        }
        std::size_t applied_outer_specs = 0;
        for (const auto& spec : start.outer_buff_specs) {
            void* buff = box_zero(runtime_, buff_class);
            Root buff_root(runtime_, buff);
            void* key_value = runtime_.string_new(spec.key.c_str());
            Root key_root(runtime_, key_value);
            void* blackboard =
                construct_with_args(runtime_, blackboard_class, blackboard_ctor,
                                    {});
            Root blackboard_root(runtime_, blackboard);
            std::string summary;
            for (const auto& field : spec.fields) {
                void* field_value = runtime_.string_new(field.key.c_str());
                Root field_root(runtime_, field_value);
                char* end = nullptr;
                const double number = std::strtod(field.value.c_str(), &end);
                const bool numeric =
                    !field.value.empty() && end && *end == '\0';
                if (numeric) {
                    float value = static_cast<float>(number);
                    runtime_.invoke(assign_float, blackboard,
                                    {field_value, &value});
                } else {
                    void* value_value =
                        runtime_.string_new(field.value.c_str());
                    Root value_root(runtime_, value_value);
                    runtime_.invoke(assign_string, blackboard,
                                    {field_value, value_value});
                }
                if (!summary.empty()) {
                    summary += ';';
                }
                summary += field.key + "=" + field.value;
            }
            runtime_.set<void*>(buff, "key", key_value);
            runtime_.set<void*>(buff, "blackboard", blackboard);
            collection_add(runtime_, outer_buffs, buff);
            ++applied_outer_specs;
            log(kLogInfo, "RoguelikeBuff 注入: " + spec.key + " " + summary);
        }
        runtime_.set<void*>(input, "outerBuffs", outer_buffs);
        runtime_.set<void*>(input, "relicBuffs", relic_buffs);
        runtime_.set<void*>(input, "extraBuffs", extra_buffs);
        runtime_.set<int>(input, "hp",
                          10 + std::max(start.hp_bonus, 0));
        runtime_.set<int>(input, "maxHp",
                          10 + std::max(start.max_hp_bonus, 0));
        runtime_.set<int>(input, "shield", std::max(start.shield_bonus, 0));
        runtime_.set_value(input, "meta", meta);

        void* mode_class = classes_.game_mode_meta;
        void* mode = box_zero(runtime_, mode_class);
        Root mode_root(runtime_, mode);
        void* mode_type_field = runtime_.field(mode_class, "modeType");
        void* mode_type_class =
            runtime_.class_from_type(runtime_.field_type(mode_type_field));
        int rogue_mode_value = 0;
        runtime_.field_static_get(
            runtime_.field(mode_type_class, "ROGUELIKE"), &rogue_mode_value);
        const std::uint8_t rogue_mode =
            static_cast<std::uint8_t>(rogue_mode_value);
        write_field_deep(runtime_, mode, mode_class, "modeType", rogue_mode);
        runtime_.set<bool>(mode, "isDeterministic", true);
        runtime_.set<bool>(mode, "allowManualTick", false);
        runtime_.set<int>(mode, "playerSide", player_side);
        runtime_.set<void*>(mode, "extraData", input);

        void* in_out_class = classes_.battle_in_out;
        void* in_params_class = runtime_.nested(in_out_class, "InParams");
        void* in_params = box_zero(runtime_, in_params_class);
        Root in_params_root(runtime_, in_params);
        static unsigned rogue_counter = 0;
        const std::string battle_id = "local-rogue-" +
            std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(++rogue_counter);
        void* battle_id_value = runtime_.string_new(battle_id.c_str());
        Root battle_id_root(runtime_, battle_id_value);
        runtime_.set<void*>(in_params, "battleId", battle_id_value);
        runtime_.set<void*>(in_params, "levelData", level_data);
        runtime_.set<void*>(in_params, "playerDataList", player_list);
        runtime_.set_value(in_params, "stageInfo", stage_info);
        runtime_.set<bool>(in_params, "isPractise", true);
        runtime_.set<int>(in_params, "difficulty", difficulty);
        runtime_.set<int>(in_params, "diffGroup", 0);
        runtime_.set<int>(in_params, "sysMenuStyle", menu_style);
        runtime_.set<bool>(in_params, "uploadBattleLog", false);

        void* rune_table = classes_.rune_table;
        void* rune_input_class = runtime_.nested(rune_table, "PackedRuneInput");
        void* rune_input_ctor = runtime_.method(rune_input_class, ".ctor", 0);
        void* rune_input =
            construct_with_args(runtime_, rune_input_class, rune_input_ctor, {});
        Root rune_input_root(runtime_, rune_input);
        void* rune_list_class =
            runtime_.class_from_type(runtime_.field_type(
                runtime_.field(rune_input_class, "runes")));
        void* rune_list_ctor = runtime_.method(rune_list_class, ".ctor", 0);
        void* rune_list =
            construct_with_args(runtime_, rune_list_class, rune_list_ctor, {});
        Root rune_list_root(runtime_, rune_list);
        runtime_.set<void*>(rune_input, "runes", rune_list);
        runtime_.set<void*>(in_params, "runeInput", rune_input);
        runtime_.set_value(in_params, "roguelikeMeta", meta);
        runtime_.set_nullable_value(in_params, "gameModeMeta", mode);

        void* in_out = runtime_.singleton(in_out_class);
        Root in_out_root(runtime_, in_out);
        void* set_in_params = runtime_.method(in_out_class, "SetInParams", 1);
        void* in_params_value = runtime_.object_unbox(in_params);
        if (!in_params_value) {
            throw std::runtime_error("InParams 无法拆箱");
        }
        runtime_.invoke(set_in_params, in_out, {in_params_value});

        void* flow_class = classes_.game_flow_controller;
        void* options_class = runtime_.nested(flow_class, "Options");
        void* flow = runtime_.singleton(flow_class);
        Root flow_root(runtime_, flow);
        void* options = box_zero(runtime_, options_class);
        Root options_root(runtime_, options);
        runtime_.set<bool>(options, "stopMusic", true);
        void* mode_field = runtime_.field(options_class, "mode");
        void* mode_enum = runtime_.class_from_type(runtime_.field_type(mode_field));
        const int loading_mode = runtime_.static_value<int>(mode_enum, "LOADING_MODE");
        runtime_.set<int>(options, "mode", loading_mode);

        const std::string scene = scene_constant(runtime_, "BATTLE_LOADER");
        if (scene.empty()) {
            throw std::runtime_error("SceneConsts.BATTLE_LOADER 为空");
        }
        void* scene_text = runtime_.string_new(scene.c_str());
        Root scene_text_root(runtime_, scene_text);
        void* start_scene = runtime_.method(flow_class, "StartScene", 2);
        void* options_value = runtime_.object_unbox(options);
        void* started = runtime_.invoke(start_scene, flow, {scene_text, options_value});
        if (!started || !runtime_.unbox<bool>(started)) {
            throw std::runtime_error("GameFlowController.StartScene(" + scene + ") 返回 false");
        }
        finish_bypassed_ = false;
        finish_home_pending_.store(false, std::memory_order_release);
        local_battle_active_.store(true, std::memory_order_release);
        activate_pending_timeline();
        clear_static_cache();
        perf_ = PerfStats{};
        {
            std::lock_guard lock(mutex_);
            profiles_.clear();
            profiles_dirty_ = true;
            scene_.stage.id = start.path;
            scene_.stage.code = stage_code;
            scene_.stage.name = stage_name;
            scene_.stage.level_id = final_level;
            scene_.stage.valid = true;
            scene_.stage_request = start.stage;
            scene_.stage_pending = true;
            squad_ = start.squad;
        }
        publish_result("已提交肉鸽战斗：" + start.stage + "/" + start.path +
                       "，干员 " + std::to_string(start.squad.size()) +
                       " 名，level=" + final_level +
                       "，藏品 " + std::to_string(applied_relics) +
                       "，机制 " +
                       std::to_string(applied_extras + applied_outer_specs) +
                       "，成长 Buff " + std::to_string(applied_outer));
    } catch (const std::exception& error) {
        if (pending_timeline_rogue_ &&
            pending_timeline_mode_ == PendingTimelineMode::NewRoot) {
            pending_timeline_mode_ = PendingTimelineMode::None;
        }
        publish_result(std::string("启动肉鸽战斗失败：") + error.what(), false);
    }
}

void LocalBattle::game_mode_info() {
    try {
        void* controller_class = classes_.battle_controller;
        const auto controllers = find_controllers(runtime_, controller_class);
        if (controllers.empty()) {
            throw std::runtime_error("当前没有 BattleController");
        }
        void* controller = controllers.front();
        Root controller_root(runtime_, controller);
        void* game_mode = runtime_.get_object(controller, "m_gameMode");
        std::ostringstream output;
        output << "controller=" << runtime_.klass_name(runtime_.object_class(controller))
               << " gameMode="
               << (game_mode ? runtime_.klass_name(runtime_.object_class(game_mode))
                             : "<null>")
               << "\n";

        void* cached_field = runtime_.field(controller_class, "s_cachedModeMeta");
        alignas(8) unsigned char cached[16]{};
        runtime_.field_static_get(cached_field, cached);
        output << "modeType=" << static_cast<int>(cached[0])
               << " isDeterministic=" << static_cast<int>(cached[1])
               << " allowManualTick=" << static_cast<int>(cached[2])
               << " playerSide=" << *reinterpret_cast<int*>(cached + 4)
               << "\n";
        void* extra = *reinterpret_cast<void**>(cached + 8);
        output << "extraData="
               << (extra ? runtime_.klass_name(runtime_.object_class(extra)) : "<null>")
               << "\n";
        if (extra) {
            void* extra_class = runtime_.object_class(extra);
            if (runtime_.klass_name(extra_class).find("RoguelikeInput") !=
                std::string::npos) {
                int hp = 0;
                int max_hp = 0;
                int shield = 0;
                read_field_deep(runtime_, extra, extra_class, "hp", &hp);
                read_field_deep(runtime_, extra, extra_class, "maxHp", &max_hp);
                read_field_deep(runtime_, extra, extra_class, "shield", &shield);
                output << "roguelike hp=" << hp << " maxHp=" << max_hp
                       << " shield=" << shield << "\n";
            }
        }
        publish_result(output.str());
    } catch (const std::exception& error) {
        publish_result(std::string("读取游戏模式失败：") + error.what(), false);
    }
}

}  // namespace ark::better_battle
