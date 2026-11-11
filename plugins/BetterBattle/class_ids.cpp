#include "class_ids.h"

#include "../common/il2cpp_runtime.h"

namespace ark::better_battle {

void ClassIds::resolve(il2cpp::Runtime& runtime) {
    battle_controller = runtime.find("Torappu.Battle", "BattleController");
    entity = runtime.find("Torappu.Battle", "Entity");
    character = runtime.find("Torappu.Battle", "Character");
    deck = runtime.find("Torappu.Battle", "Deck");
    deck_card = runtime.nested(deck, "Card");
    enemy = runtime.find("Torappu.Battle", "Enemy");
    try {
        enemy_db = runtime.find("Torappu", "EnemyDB");
    } catch (...) {
        enemy_db = nullptr;
    }
    battle_object = runtime.find("Torappu.Battle", "BObject");
    buff = runtime.find("Torappu.Battle", "Buff");
    attributes = runtime.find("Torappu.Battle", "Attributes");
    attribute_type = runtime.find("Torappu", "AttributeType");
    battle_in_out = runtime.find("Torappu.Battle", "BattleInOut");
    battle_stage_info = runtime.find("Torappu.Battle", "BattleStageInfo");
    game_mode_meta = runtime.find("Torappu.Battle", "GameModeMeta");
    scheduler = runtime.find("Torappu.Battle", "Scheduler");
    level_utils = runtime.find("Torappu.Battle", "LevelUtils");
    battle_data_converter = runtime.find("Torappu.Battle", "BattleDataConverter");
    battle_sys_menu_style = runtime.find("Torappu.Battle.UI", "BattleSysMenuStyle");
    player_data = runtime.find("Torappu", "PlayerData");
    player_data_model = runtime.find("Torappu", "PlayerDataModel");
    game_flow_controller = runtime.find("Torappu", "GameFlowController");
    character_util = runtime.find("Torappu", "CharacterUtil");
    stage_data = runtime.find("Torappu", "StageData");
    stage_data_util = runtime.find("Torappu", "StageDataUtil");
    player_prefs = runtime.find("Torappu", "PlayerPrefsWrapper");
    char_query = runtime.find("Torappu", "CharQuery");
    player_side = runtime.find("Torappu", "PlayerSide");
    rune_table = runtime.find("Torappu", "RuneTable");
    advanced_character_inst = runtime.find("Torappu", "AdvancedCharacterInst");
    roguelike_topic_db = runtime.find("Torappu", "RoguelikeTopicDB");
    roguelike_data_util = runtime.find("Torappu", "RoguelikeDataUtil");
    roguelike_topic_extra_buff_data =
        runtime.find("Torappu", "RoguelikeTopicExtraBuffData");
    unity_object = runtime.find("UnityEngine", "Object");
}

} // namespace ark::better_battle
