#pragma once

namespace ark::il2cpp {
class Runtime;
}

namespace ark::better_battle {

// 唯一的游戏类型表。resolve() 找不到类型就抛异常，不做降级。
struct ClassIds {
    void* battle_controller = nullptr;
    void* entity = nullptr;
    void* character = nullptr;
    void* deck = nullptr;
    void* deck_card = nullptr;
    void* enemy = nullptr;
    void* enemy_db = nullptr;
    void* battle_object = nullptr;
    void* buff = nullptr;
    void* attributes = nullptr;
    void* attribute_type = nullptr;
    void* battle_in_out = nullptr;
    void* battle_stage_info = nullptr;
    void* game_mode_meta = nullptr;
    void* scheduler = nullptr;
    void* level_utils = nullptr;
    void* battle_data_converter = nullptr;
    void* battle_sys_menu_style = nullptr;
    void* player_data = nullptr;
    void* player_data_model = nullptr;
    void* game_flow_controller = nullptr;
    void* character_util = nullptr;
    void* stage_data = nullptr;
    void* stage_data_util = nullptr;
    void* player_prefs = nullptr;
    void* char_query = nullptr;
    void* player_side = nullptr;
    void* rune_table = nullptr;
    void* advanced_character_inst = nullptr;
    void* roguelike_topic_db = nullptr;
    void* roguelike_data_util = nullptr;
    void* roguelike_topic_extra_buff_data = nullptr;
    void* unity_object = nullptr;

    void resolve(il2cpp::Runtime& runtime);
};

} // namespace ark::better_battle
