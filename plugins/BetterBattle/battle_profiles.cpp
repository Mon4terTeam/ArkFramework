#include "battle_helpers.h"

#include <nlohmann/json.hpp>

namespace ark::better_battle {

void LocalBattle::read_talents_cached(void* data, void* data_class, int potential,
                                      std::vector<TalentInfo>& output) {
    if (!data || !data_class) {
        return;
    }
    const auto key = std::make_pair(data, potential);
    if (const auto found = talent_cache_.find(key); found != talent_cache_.end()) {
        ++perf_.talent_hits;
        output = found->second;
        return;
    }
    ++perf_.talent_misses;
    std::vector<TalentInfo> talents;
    read_talents_from_data(runtime_, data, data_class, potential, talents);
    const auto [inserted, unused] =
        talent_cache_.emplace(key, std::move(talents));
    (void)unused;
    output = inserted->second;
}

void LocalBattle::read_character_talents_cached(void* character, void* character_class,
                                                std::vector<TalentInfo>& output) {
    void* data = nullptr;
    if (!read_field_deep(runtime_, character, character_class,
                         "<data>k__BackingField", &data) ||
        !data) {
        return;
    }
    void* data_class = runtime_.object_class(data);
    int potential = 0;
    read_field_deep(runtime_, data, data_class, "potentialRank", &potential);
    read_talents_cached(data, data_class, potential, output);
}

void LocalBattle::prune_buff_static_cache() {
    for (auto it = buff_static_cache_.begin(); it != buff_static_cache_.end();) {
        if (it->second.seen_generation != buff_cache_generation_) {
            it = buff_static_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string LocalBattle::operator_label(const std::string& id) const {
    const auto found = operator_index_.find(id);
    if (found == operator_index_.end() || found->second >= operators_.size()) {
        return id;
    }
    const OperatorEntry& entry = operators_[found->second];
    if (entry.name.empty() || entry.name == entry.id) {
        return id;
    }
    return entry.name + "  " + entry.id;
}

std::string LocalBattle::operator_name(const std::string& id) const {
    const auto found = operator_index_.find(id);
    if (found == operator_index_.end() || found->second >= operators_.size()) {
        return id;
    }
    const OperatorEntry& entry = operators_[found->second];
    return entry.name.empty() ? id : entry.name;
}

void LocalBattle::ensure_profile_from_data(void* data, std::uint32_t key,
                                           const std::string& operator_id) {
    if (!data) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        const auto found = profiles_.find(key);
        if (found != profiles_.end() && found->second.operator_id == operator_id) {
            return;
        }
    }

    UnitProfile profile;
    profile.card_uid = key;
    profile.operator_id = operator_id;
    profile.name = operator_name(profile.operator_id);
    profile.level = 0;
    profile.evolve = 0;
    profile.potential = 0;

    Root battle_data_root(runtime_, data);
    void* battle_data = data;
    try {
        if (data) {
            Root data_root(runtime_, data);
            void* data_class = runtime_.object_class(data);
            profile.char_id = read_string_field(runtime_, data, data_class, "prefabKey");
            read_field_deep(runtime_, data, data_class, "level", &profile.level);
            read_field_deep(runtime_, data, data_class, "evolvePhase", &profile.evolve);
            read_field_deep(runtime_, data, data_class, "potentialRank", &profile.potential);

            void* skill = nullptr;
            if (read_field_deep(runtime_, data, data_class, "mainSkill", &skill) && skill) {
                Root skill_root(runtime_, skill);
                void* skill_class = runtime_.object_class(skill);
                profile.skill_id = read_string_field(runtime_, skill, skill_class, "skillId");
                profile.skill_name = read_string_field(runtime_, skill, skill_class, "name");
                const std::string raw_description =
                    read_string_field(runtime_, skill, skill_class, "description");
                profile.skill_description =
                    skill_description_text(runtime_, skill, skill_class, raw_description);
                if (!raw_description.empty()) {
                    void* blackboard = nullptr;
                    read_field_deep(runtime_, skill, skill_class, "blackboard", &blackboard);
                    try {
                        void* panel_class = runtime_.find(
                            "Torappu.Battle.UI", "UICharacterInfoTabGroupSubPanel");
                        void* object_class = classes_.unity_object;
                        void* find_one = runtime_.method(
                            object_class, "FindObjectOfType", 1, {"System.Type"});
                        void* panel_type =
                            runtime_.type_object(runtime_.class_type(panel_class));
                        Root panel_type_root(runtime_, panel_type);
                        void* panel =
                            runtime_.invoke(find_one, nullptr, {panel_type});
                        bool temporary_panel = false;
                        std::unique_ptr<Root> panel_root;
                        if (!panel) {
                            panel = runtime_.object_new(panel_class);
                            if (panel) {
                                panel_root = std::make_unique<Root>(runtime_, panel);
                                void* constructor =
                                    find_method_optional(runtime_, panel_class, ".ctor", 0);
                                if (constructor) {
                                    try {
                                        runtime_.invoke(constructor, panel);
                                    } catch (const std::exception& error) {
                                        log(kLogWarning,
                                            "技能描述解析：临时面板构造失败 " +
                                                std::string(error.what()));
                                    }
                                }
                                temporary_panel = true;
                            }
                        }
                        if (panel) {
                            if (!panel_root) {
                                panel_root = std::make_unique<Root>(runtime_, panel);
                            }
                            void* parse = runtime_.method(
                                panel_class, "_ParseSkillDescription", 3);
                            void* raw_text = runtime_.string_new(raw_description.c_str());
                            Root raw_text_root(runtime_, raw_text);
                            void* parsed = runtime_.invoke(
                                parse, panel, {raw_text, blackboard, data});
                            const std::string official = runtime_.string(parsed);
                            if (!official.empty()) {
                                profile.skill_description = official;
                            }
                            log(kLogInfo,
                                "技能描述解析 " + profile.operator_id +
                                    (temporary_panel ? " temp-panel" : " scene-panel") +
                                    (official.empty() ? " empty" : " ok"));
                        } else {
                            log(kLogWarning,
                                "技能描述解析 " + profile.operator_id + " 无面板");
                        }
                    } catch (const std::exception& error) {
                        log(kLogWarning, "技能描述解析 " + profile.operator_id +
                                             " 失败：" + error.what());
                    }
                }
                profile.skill_icon_id = read_string_field(runtime_, skill, skill_class, "iconId");
                float duration_seconds = 0;
                read_field_deep(runtime_, skill, skill_class, "duration", &duration_seconds);
                profile.skill_duration = duration_seconds;
                read_field_deep(runtime_, skill, skill_class, "skillType",
                                &profile.skill_type);
                read_field_deep(runtime_, skill, skill_class, "durationType",
                                &profile.skill_duration_type);
                profile.sp_cost =
                    method_int(runtime_, skill, skill_class, "get_spCost", 0);
                profile.max_sp =
                    method_int(runtime_, skill, skill_class, "get_maxSp", 0);
            }

            void* skin = nullptr;
            if (read_field_deep(runtime_, data, data_class, "skinData", &skin) && skin) {
                Root skin_root(runtime_, skin);
                void* skin_class = runtime_.object_class(skin);
                profile.skin_id = read_string_field(runtime_, skin, skin_class, "skinId");
                profile.avatar_id =
                    method_string(runtime_, skin, skin_class, "GetAvatarId");
                profile.portrait_id =
                    method_string(runtime_, skin, skin_class, "GetPortraitId");
                profile.illust_id =
                    method_string(runtime_, skin, skin_class, "GetIllustId");
                profile.dyn_illust_id =
                    method_string(runtime_, skin, skin_class, "GetDynIllustId");
            }
        }
    } catch (const std::exception& error) {
        profile.art_error = std::string("静态数据读取失败：") + error.what();
    }

    if (profile.char_id.empty()) {
        profile.char_id = profile.operator_id;
    }
    if (profile.illust_id.empty() && !profile.portrait_id.empty()) {
        profile.illust_id = profile.portrait_id;
    }

    const std::string cache_token =
        sanitize_cache_token(profile.char_id.empty() ? profile.operator_id : profile.char_id) +
        "_" +
        sanitize_cache_token(profile.skin_id.empty() ? "default" : profile.skin_id);
    const std::string cache_name = cache_token + ".png";
    const std::filesystem::path cache_dir =
        std::filesystem::path(host_.root_directory) / "BetterBattleWeb" / "art_cache";
    const std::filesystem::path cache_path = cache_dir / cache_name;
    std::error_code ignored;
    std::filesystem::create_directories(cache_dir, ignored);
    if (std::filesystem::exists(cache_path, ignored)) {
        profile.art_file = "../art_cache/" + cache_name;
    } else if (std::getenv("BETTERBATTLE_EXPERIMENTAL_ART")) {
        try {
            void* character_util = classes_.character_util;
            void* load_portrait = nullptr;
            void* get_portrait = nullptr;
            void* load_avatar = nullptr;
            try {
                load_portrait = runtime_.method(
                    character_util, "LoadCharPortrait", 1, {"System.String"});
            } catch (...) {
            }
            try {
                get_portrait = runtime_.method(
                    character_util, "GetPortraitSprite", 1, {"System.String"});
            } catch (...) {
            }
            try {
                load_avatar = runtime_.method(
                    character_util, "LoadCharAvatar", 1, {"System.String"});
            } catch (...) {
            }

            void* image_conversion = runtime_.find("UnityEngine", "ImageConversion");
            void* encode_png = runtime_.method(
                image_conversion, "EncodeToPNG", 1, {"UnityEngine.Texture2D"});
            void* sprite_data_class =
                runtime_.find("Torappu.UI.Atlas", "SpriteRenderData");
            void* main_tex_field = runtime_.field(sprite_data_class, "mainTex");
            void* rect_field = runtime_.field(sprite_data_class, "rect");
            const std::size_t main_tex_offset =
                main_tex_field ? runtime_.field_offset(main_tex_field) : 0;
            const std::size_t rect_offset =
                rect_field ? runtime_.field_offset(rect_field) : 0;

            const auto texture_size = [&](void* texture, int* width, int* height) {
                if (!texture || !width || !height) {
                    return false;
                }
                void* texture_class = runtime_.object_class(texture);
                *width = method_int(runtime_, texture, texture_class, "get_width", 0);
                *height = method_int(runtime_, texture, texture_class, "get_height", 0);
                return *width > 0 && *height > 0;
            };

            const auto encode_texture = [&](void* texture, int x, int y, int width,
                                            int height) {
                int texture_width = 0;
                int texture_height = 0;
                if (!texture || !texture_size(texture, &texture_width, &texture_height)) {
                    return false;
                }
                void* png = runtime_.invoke(encode_png, nullptr, {texture});
                const int size = runtime_.count(png);
                if (!png || size <= 0) {
                    return false;
                }
                const auto* bytes =
                    reinterpret_cast<const unsigned char*>(png) + 0x20;
                std::ofstream output(cache_path, std::ios::binary | std::ios::trunc);
                if (!output) {
                    profile.art_error = "无法写入立绘缓存";
                    return false;
                }
                output.write(reinterpret_cast<const char*>(bytes), size);
                if (!output) {
                    profile.art_error = "写入立绘缓存失败";
                    return false;
                }
                profile.art_file = "../art_cache/" + cache_name;
                profile.art_tex_w = texture_width;
                profile.art_tex_h = texture_height;
                profile.art_x = x;
                profile.art_y = y;
                profile.art_w = width > 0 ? width : texture_width;
                profile.art_h = height > 0 ? height : texture_height;
                profile.art_error.clear();
                return true;
            };

            const auto decode_render_data = [&](void* boxed, const std::string& source) {
                if (!boxed || !main_tex_offset) {
                    return false;
                }
                void* base = runtime_.object_unbox(boxed);
                if (!base) {
                    return false;
                }
                void* texture = nullptr;
                read_at(static_cast<unsigned char*>(base) + main_tex_offset, &texture);
                if (!texture) {
                    return false;
                }
                int x = 0;
                int y = 0;
                int width = 0;
                int height = 0;
                if (rect_offset) {
                    read_at(static_cast<unsigned char*>(base) + rect_offset, &x);
                    read_at(static_cast<unsigned char*>(base) + rect_offset + 4, &y);
                    read_at(static_cast<unsigned char*>(base) + rect_offset + 8, &width);
                    read_at(static_cast<unsigned char*>(base) + rect_offset + 12, &height);
                }
                if (encode_texture(texture, x, y, width, height)) {
                    return true;
                }
                profile.art_error = "无法编码立绘纹理：" + source;
                return false;
            };

            const auto try_portrait = [&](const std::string& value,
                                          const std::string& source) {
                if (value.empty()) {
                    return false;
                }
                for (void* method : {load_portrait, get_portrait}) {
                    if (!method) {
                        continue;
                    }
                    try {
                        void* text = runtime_.string_new(value.c_str());
                        Root text_root(runtime_, text);
                        void* result = runtime_.invoke(method, nullptr, {text});
                        if (!result) {
                            continue;
                        }
                        Root result_root(runtime_, result);
                        if (decode_render_data(result, source)) {
                            return true;
                        }
                    } catch (...) {
                    }
                }
                return false;
            };

            bool captured = false;
            for (const auto& [value, source] :
                 std::initializer_list<std::pair<std::string, std::string>>{
                     {profile.illust_id, "illust"},
                     {profile.portrait_id, "portrait"},
                     {profile.skin_id, "skinId"},
                     {profile.char_id, "charId"},
                 }) {
                if (try_portrait(value, source)) {
                    captured = true;
                    break;
                }
            }
            if (!captured && load_avatar) {
                for (const std::string& value :
                     {profile.avatar_id, profile.skin_id, profile.char_id}) {
                    if (value.empty()) {
                        continue;
                    }
                    try {
                        void* text = runtime_.string_new(value.c_str());
                        Root text_root(runtime_, text);
                        void* sprite = runtime_.invoke(load_avatar, nullptr, {text});
                        if (!sprite) {
                            continue;
                        }
                        Root sprite_root(runtime_, sprite);
                        void* sprite_class = runtime_.object_class(sprite);
                        void* get_texture =
                            find_method_optional(runtime_, sprite_class, "get_texture", 0);
                        void* texture =
                            get_texture ? runtime_.invoke(get_texture, sprite) : nullptr;
                        if (encode_texture(texture, 0, 0, 0, 0)) {
                            captured = true;
                            break;
                        }
                    } catch (...) {
                    }
                }
            }
            if (!captured && profile.art_error.empty()) {
                profile.art_error = "未找到可读的立绘资源";
            }

            if (!captured && battle_data) {
                try {
                    void* art_collection_class =
                        runtime_.find("Torappu.Battle", "ArtCollection");
                    void* object_class = classes_.unity_object;
                    void* find_one = runtime_.method(
                        object_class, "FindObjectOfType", 1, {"System.Type"});
                    void* art_type =
                        runtime_.type_object(runtime_.class_type(art_collection_class));
                    Root art_type_root(runtime_, art_type);
                    void* art_collection =
                        runtime_.invoke(find_one, nullptr, {art_type});
                    if (art_collection) {
                        Root art_root(runtime_, art_collection);
                        void* get_illust = runtime_.method(
                            art_collection_class, "GetIllust", 1,
                            {"Torappu.Battle.BattleCharacterData"});
                        void* image =
                            runtime_.invoke(get_illust, art_collection, {battle_data});
                        if (image) {
                            Root image_root(runtime_, image);
                            void* sprite = runtime_.call(image, "get_sprite");
                            if (sprite) {
                                Root sprite_root(runtime_, sprite);
                                void* texture = runtime_.call(sprite, "get_texture");
                                captured = encode_texture(texture, 0, 0, 0, 0);
                            }
                        }
                        if (!captured) {
                            void* get_avatar = runtime_.method(
                                art_collection_class, "GetAvatar", 1,
                                {"Torappu.Battle.BattleCharacterData"});
                            void* sprite =
                                runtime_.invoke(get_avatar, art_collection, {battle_data});
                            if (sprite) {
                                Root sprite_root(runtime_, sprite);
                                void* texture = runtime_.call(sprite, "get_texture");
                                captured = encode_texture(texture, 0, 0, 0, 0);
                            }
                        }
                    }
                } catch (...) {
                }
            }
            if (captured) {
                profile.art_error.clear();
            }
        } catch (const std::exception& error) {
            profile.art_error = std::string("立绘加载失败：") + error.what();
        }
    }

    {
        std::lock_guard lock(mutex_);
        profiles_[key] = std::move(profile);
        profiles_dirty_ = true;
    }
}

void LocalBattle::ensure_unit_profile(void* character, const UnitInfo& unit) {
    if (!character || !unit.mine) {
        return;
    }
    const std::uint32_t key = unit.card_uid ? unit.card_uid : unit.uid;
    const std::string operator_id = unit.id.empty() ? unit.tmpl_id : unit.id;
    {
        std::lock_guard lock(mutex_);
        const auto found = profiles_.find(key);
        if (found != profiles_.end() && found->second.operator_id == operator_id) {
            return;
        }
    }
    void* data = nullptr;
    try {
        void* character_class = classes_.character;
        void* get_data = find_method_optional(runtime_, character_class, "get_data", 0);
        data = get_data ? runtime_.invoke(get_data, character) : nullptr;
    } catch (...) {
        return;
    }
    ensure_profile_from_data(data, key, operator_id);
}

void LocalBattle::ensure_card_profile(void* data, const CardInfo& card) {
    ensure_profile_from_data(data, card.uid, card.id);
}

std::string LocalBattle::build_profiles_json() const {
    nlohmann::ordered_json payload;
    payload["profiles"] = nlohmann::ordered_json::array();
    {
        std::lock_guard lock(mutex_);
        for (const auto& [key, profile] : profiles_) {
            payload["profiles"].push_back({
                {"key", key},
                {"operator", profile.operator_id},
                {"name", profile.name},
                {"char_id", profile.char_id},
                {"skin_id", profile.skin_id},
                {"level", profile.level},
                {"evolve", profile.evolve},
                {"potential", profile.potential},
                {"skill", {
                    {"id", profile.skill_id},
                    {"name", profile.skill_name},
                    {"description", profile.skill_description},
                    {"icon", profile.skill_icon_id},
                    {"sp_cost", profile.sp_cost},
                    {"max_sp", profile.max_sp},
                    {"duration", profile.skill_duration},
                    {"type", profile.skill_type},
                    {"duration_type", profile.skill_duration_type},
                }},
                {"art", {
                    {"avatar", profile.avatar_id},
                    {"portrait", profile.portrait_id},
                    {"illust", profile.illust_id},
                    {"dyn_illust", profile.dyn_illust_id},
                    {"file", profile.art_file},
                    {"error", profile.art_error},
                    {"x", profile.art_x},
                    {"y", profile.art_y},
                    {"w", profile.art_w},
                    {"h", profile.art_h},
                    {"tex_w", profile.art_tex_w},
                    {"tex_h", profile.art_tex_h},
                }},
            });
        }
    }
    return payload.dump();
}

void LocalBattle::publish_profiles() {
    if (!bridge_ || !bridge_->publish) {
        return;
    }
    const std::string json = build_profiles_json();
    bridge_->publish("betterbattle.profiles", json.data(), json.size(), 0);
    std::lock_guard lock(mutex_);
    profiles_dirty_ = false;
}

}  // namespace ark::better_battle
