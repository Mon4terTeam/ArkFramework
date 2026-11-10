#include "battle_helpers.h"

namespace ark::better_battle {

std::unordered_map<FieldCacheKey, void*, FieldCacheHash>& field_cache() {
    thread_local std::unordered_map<FieldCacheKey, void*, FieldCacheHash> cache;
    return cache;
}

std::unordered_map<MethodCacheKey, void*, MethodCacheHash>& method_cache() {
    thread_local std::unordered_map<MethodCacheKey, void*, MethodCacheHash> cache;
    return cache;
}

std::unordered_map<void*, std::size_t>& field_offset_cache() {
    thread_local std::unordered_map<void*, std::size_t> cache;
    return cache;
}

std::vector<void*> find_live_objects(il2cpp::Runtime& runtime, void* klass,
                                     std::size_t limit) {
    struct Context {
        il2cpp::Runtime* runtime = nullptr;
        void* klass = nullptr;
        std::vector<void*>* objects = nullptr;
        std::size_t limit = 0;
    };
    static Context* current = nullptr;
    Context context{&runtime, klass, nullptr, limit};
    std::vector<void*> objects;
    context.objects = &objects;
    current = &context;
    runtime.gc_foreach_heap(
        [](void* first, void* second) {
            Context* ctx = current;
            if (!ctx) {
                return;
            }
            void* object = first == static_cast<void*>(ctx) ? second : first;
            if (!object || ctx->objects->size() >= ctx->limit) {
                return;
            }
            try {
                if (ctx->runtime->object_class(object) == ctx->klass) {
                    ctx->objects->push_back(object);
                }
            } catch (...) {
            }
        },
        &context);
    current = nullptr;
    return objects;
}
std::vector<void*> find_rooted_objects(il2cpp::Runtime& runtime, void* klass,
                                       std::size_t limit) {
    struct Context {
        il2cpp::Runtime* runtime = nullptr;
        void* klass = nullptr;
        std::vector<void*>* objects = nullptr;
        std::size_t limit = 0;
    };
    static Context* current = nullptr;
    Context context{&runtime, klass, nullptr, limit};
    std::vector<void*> objects;
    context.objects = &objects;
    current = &context;
    if (runtime.gchandle_foreach_target) {
        runtime.gchandle_foreach_target(
            [](void* object, void*) {
                Context* ctx = current;
                if (!ctx || !object || ctx->objects->size() >= ctx->limit) {
                    return;
                }
                try {
                    if (ctx->runtime->object_class(object) == ctx->klass) {
                        ctx->objects->push_back(object);
                    }
                } catch (...) {
                }
            },
            &context);
    }
    current = nullptr;
    return objects;
}
std::uint64_t perf_now_us() {
    static const std::uint64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::uint64_t>(value.QuadPart);
    }();
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    if (!frequency) {
        return 0;
    }
    return static_cast<std::uint64_t>(
        static_cast<long double>(value.QuadPart) * 1000000.0L /
        static_cast<long double>(frequency));
}
void* find_field_deep(il2cpp::Runtime& runtime, void* klass, const char* name) {
    const FieldCacheKey key{&runtime, klass, name};
    if (const auto found = field_cache().find(key); found != field_cache().end()) {
        return found->second;
    }
    for (void* current = klass; current; current = runtime.class_parent(current)) {
        void* field = runtime.class_field(current, name);
        if (field) {
            field_cache().emplace(key, field);
            return field;
        }
    }
    field_cache().emplace(key, nullptr);
    return nullptr;
}
UniqueEquipPairLayout resolve_unique_equip_pair_class(il2cpp::Runtime& runtime,
                                                      void* pair_class) {
    UniqueEquipPairLayout layout;
    layout.klass = pair_class;
    if (!layout.klass) {
        return layout;
    }

    void* iterator = nullptr;
    while (void* field = runtime.class_fields(layout.klass, &iterator)) {
        const char* name = runtime.field_name(field);
        void* field_class =
            runtime.class_from_type(runtime.field_type(field));
        if (!name || !field_class) {
            continue;
        }
        const std::string type_name = runtime.klass_name(field_class);
        const std::string field_name(name);
        if (type_name == "System.String" && !layout.id_field) {
            layout.id_field = field;
            layout.id_name = field_name;
        } else if (type_name == "System.Int32" && !layout.level_field) {
            layout.level_field = field;
            layout.level_name = field_name;
        }
    }
    return layout;
}

UniqueEquipPairLayout resolve_unique_equip_pair(il2cpp::Runtime& runtime, void* inst_class) {
    void* array_field = find_field_deep(runtime, inst_class, "uniEquipIds");
    if (!array_field) {
        return {};
    }
    void* array_class = runtime.class_from_type(runtime.field_type(array_field));
    if (!array_class) {
        return {};
    }
    return resolve_unique_equip_pair_class(runtime, runtime.class_element(array_class));
}
void* find_method_optional(il2cpp::Runtime& runtime, void* klass, const char* name, int count) {
    const MethodCacheKey key{&runtime, klass, name, count};
    if (const auto found = method_cache().find(key); found != method_cache().end()) {
        return found->second;
    }
    for (void* current = klass; current; current = runtime.class_parent(current)) {
        void* iterator = nullptr;
        while (void* method = runtime.class_methods(current, &iterator)) {
            if (runtime.method_count(method) == static_cast<std::uint32_t>(count) &&
                std::strcmp(runtime.method_name(method), name) == 0) {
                method_cache().emplace(key, method);
                return method;
            }
        }
    }
    method_cache().emplace(key, nullptr);
    return nullptr;
}
std::string read_string_field(il2cpp::Runtime& runtime, void* object, void* klass,
                              const char* name) {
    void* text = nullptr;
    if (!read_field_deep(runtime, object, klass, name, &text) || !text) {
        return {};
    }
    return runtime.string(text);
}
bool write_character_inst_trust(il2cpp::Runtime& runtime, void* instance,
                                       void* instance_class,
                                       int trust_percent) {
    void* inst_field = find_field_deep(runtime, instance_class, "inst");
    if (!inst_field) {
        return false;
    }
    void* metadata_class =
        runtime.class_from_type(runtime.field_type(inst_field));
    if (!metadata_class) {
        return false;
    }
    void* favor_field =
        runtime.class_field(metadata_class, "favorBattlePhase");
    if (!favor_field) {
        return false;
    }
    void* field_iterator = nullptr;
    void* first_field = runtime.class_fields(metadata_class, &field_iterator);
    if (!first_field) {
        return false;
    }
    const int battle_phase =
        std::clamp(trust_percent, 0, 200) / 2;
    const std::size_t relative_offset =
        runtime.field_offset(favor_field) -
        runtime.field_offset(first_field);
    unsigned char* destination =
        static_cast<unsigned char*>(instance) +
        runtime.field_offset(inst_field) + relative_offset;
    std::memcpy(destination, &battle_phase, sizeof(battle_phase));
    return true;
}
std::string describe_instance_fields(il2cpp::Runtime& runtime, void* object) {
    if (!object) {
        return "null";
    }
    void* klass = runtime.object_class(object);
    std::ostringstream output;
    output << runtime.klass_name(klass) << '{';
    bool first = true;
    void* iterator = nullptr;
    while (void* field = runtime.class_fields(klass, &iterator)) {
        if (!first) {
            output << ", ";
        }
        first = false;
        const char* name = runtime.field_name(field);
        void* field_class = runtime.class_from_type(runtime.field_type(field));
        output << (name ? name : "?") << '=';
        if (!field_class) {
            output << "unknown";
            continue;
        }
        if (!runtime.class_is_value(field_class)) {
            void* value = runtime.field_get_object(field, object);
            output << value;
            if (!value) {
                continue;
            }
            void* value_class = runtime.object_class(value);
            const std::string value_type = runtime.klass_name(value_class);
            output << '(' << value_type;
            if (value_type == "System.String") {
                output << ",\"" << runtime.string(value) << '"';
            }
            output << ')';
            continue;
        }
        std::uint32_t alignment = 0;
        const int size = runtime.class_value_size(field_class, &alignment);
        const auto* bytes = static_cast<const unsigned char*>(object) +
                            runtime.field_offset(field);
        output << "0x";
        for (int index = 0; index < std::min(size, 16); ++index) {
            char buffer[4]{};
            std::snprintf(buffer, sizeof(buffer), "%02x", bytes[size - 1 - index]);
            output << buffer;
        }
    }
    output << '}';
    return output.str();
}
bool method_bool(il2cpp::Runtime& runtime, void* object, void* klass, const char* name,
                 bool fallback) {
    void* method = find_method_optional(runtime, klass, name, 0);
    if (!method) {
        return fallback;
    }
    void* boxed = runtime.invoke(method, object);
    return boxed ? runtime.unbox<bool>(boxed) : fallback;
}
int method_int(il2cpp::Runtime& runtime, void* object, void* klass, const char* name,
               int fallback) {
    void* method = find_method_optional(runtime, klass, name, 0);
    if (!method) {
        return fallback;
    }
    void* boxed = runtime.invoke(method, object);
    return boxed ? runtime.unbox<int>(boxed) : fallback;
}
bool read_fixed_play_time(il2cpp::Runtime& runtime, void* klass, double* value) {
    if (!klass || !value) {
        return false;
    }
    void* method =
        find_method_optional(runtime, klass, "get_fixedPlayTime", 0);
    if (!method) {
        return false;
    }
    void* entry = *static_cast<void**>(method);
    if (!il2cpp::is_executable(entry)) {
        return false;
    }
    using FixedTimeGetter = std::int64_t (*)(void*);
    auto getter = reinterpret_cast<FixedTimeGetter>(entry);
    *value = static_cast<double>(getter(method)) / kFixedPointScale;
    return true;
}
std::string method_string(il2cpp::Runtime& runtime, void* object, void* klass, const char* name) {
    void* method = find_method_optional(runtime, klass, name, 0);
    if (!method) {
        return {};
    }
    void* text = runtime.invoke(method, object);
    return runtime.string(text);
}
double unbox_fp(il2cpp::Runtime& runtime, void* boxed) {
    if (!boxed) {
        return 0;
    }
    std::int64_t raw = 0;
    if (!read_at(runtime.object_unbox(boxed), &raw)) {
        return 0;
    }
    return static_cast<double>(raw) / kFixedPointScale;
}
double method_fp(il2cpp::Runtime& runtime, void* object, void* klass, const char* name) {
    void* method = find_method_optional(runtime, klass, name, 0);
    return method ? unbox_fp(runtime, runtime.invoke(method, object)) : 0;
}
float method_float(il2cpp::Runtime& runtime, void* object, void* klass, const char* name,
                   float fallback) {
    void* method = find_method_optional(runtime, klass, name, 0);
    if (!method) {
        return fallback;
    }
    void* boxed = runtime.invoke(method, object);
    return boxed ? runtime.unbox<float>(boxed) : fallback;
}
std::string trim_float_text(std::string text) {
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}
std::string format_blackboard_number(float value, const std::string& format) {
    if (!std::isfinite(value)) {
        return "--";
    }
    const bool percent = format.find('%') != std::string::npos;
    const double scaled = percent ? static_cast<double>(value) * 100.0
                                  : static_cast<double>(value);
    int decimals = 0;
    const auto dot = format.find('.');
    if (dot != std::string::npos) {
        for (std::size_t index = dot + 1; index < format.size(); ++index) {
            if (format[index] != '0' && format[index] != '#') {
                break;
            }
            ++decimals;
        }
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << scaled;
    std::string text = out.str();
    if (format.find('#') != std::string::npos) {
        text = trim_float_text(std::move(text));
    }
    if (percent) {
        text += '%';
    }
    return text;
}
std::string skill_description_text(il2cpp::Runtime& runtime, void* skill, void* skill_class,
                                   const std::string& raw) {
    if (raw.empty()) {
        return {};
    }
    void* blackboard = nullptr;
    read_field_deep(runtime, skill, skill_class, "blackboard", &blackboard);
    void* blackboard_class = blackboard ? runtime.object_class(blackboard) : nullptr;
    void* try_float =
        blackboard_class ? find_method_optional(runtime, blackboard_class, "TryGetFloat", 2)
                         : nullptr;
    void* try_string =
        blackboard_class ? find_method_optional(runtime, blackboard_class, "TryGetString", 2)
                         : nullptr;

    std::string replaced;
    replaced.reserve(raw.size() + 32);
    for (std::size_t index = 0; index < raw.size();) {
        if (raw[index] != '{') {
            replaced.push_back(raw[index++]);
            continue;
        }
        const auto close = raw.find('}', index + 1);
        if (close == std::string::npos) {
            replaced.append(raw.substr(index));
            break;
        }
        const std::string token = raw.substr(index + 1, close - index - 1);
        const auto colon = token.find(':');
        const std::string key = token.substr(0, colon);
        const std::string format =
            colon == std::string::npos ? std::string() : token.substr(colon + 1);
        bool resolved = false;
        std::string resolved_text;
        if (blackboard && !key.empty()) {
            void* key_object = runtime.string_new(key.c_str());
            Root key_root(runtime, key_object);
            if (try_string && key_object) {
                void* string_value = nullptr;
                void* result =
                    runtime.invoke(try_string, blackboard, {key_object, &string_value});
                if (result && runtime.unbox<bool>(result) && string_value) {
                    resolved_text = runtime.string(string_value);
                    resolved = true;
                }
            }
            if (!resolved && try_float) {
                float number = 0;
                void* result =
                    runtime.invoke(try_float, blackboard, {key_root.get(), &number});
                if (result && runtime.unbox<bool>(result)) {
                    resolved_text = format_blackboard_number(number, format);
                    resolved = true;
                }
            }
        }
        replaced += resolved ? resolved_text : key;
        index = close + 1;
    }

    std::string text;
    text.reserve(replaced.size());
    for (std::size_t index = 0; index < replaced.size();) {
        if (replaced[index] == '\\' && index + 1 < replaced.size()) {
            const char escaped = replaced[index + 1];
            if (escaped == 'n' || escaped == 'r') {
                text += '\n';
                index += 2;
                continue;
            }
            if (escaped == 't') {
                text += '\t';
                index += 2;
                continue;
            }
        }
        if (replaced[index] != '<') {
            text.push_back(replaced[index++]);
            continue;
        }
        const auto close = replaced.find('>', index + 1);
        if (close == std::string::npos) {
            text.append(replaced.substr(index));
            break;
        }
        std::string tag = replaced.substr(index + 1, close - index - 1);
        std::transform(tag.begin(), tag.end(), tag.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (tag.rfind("br", 0) == 0) {
            text += '\n';
        }
        index = close + 1;
    }
    return text;
}
void* box_zero(il2cpp::Runtime& runtime, void* klass) {
    if (!klass) {
        return nullptr;
    }
    void* boxed = runtime.object_new(klass);
    if (!boxed || !runtime.object_unbox) {
        return boxed;
    }
    std::uint32_t alignment = 0;
    const int size = runtime.class_value_size(klass, &alignment);
    if (size > 0) {
        std::memset(runtime.object_unbox(boxed), 0, static_cast<std::size_t>(size));
    }
    return boxed;
}
void* construct_with_args(il2cpp::Runtime& runtime, void* klass, void* constructor,
                          std::initializer_list<void*> arguments) {
    if (!klass || !constructor) {
        throw std::runtime_error("缺少构造器");
    }
    void* instance = runtime.object_new(klass);
    if (!instance) {
        throw std::runtime_error("托管对象分配失败");
    }
    if (runtime.class_is_value(klass)) {
        return instance;
    }
    runtime.invoke(constructor, instance, arguments);
    return instance;
}
std::string scene_constant(il2cpp::Runtime& runtime, const char* name) {
    void* klass = nullptr;
    try {
        klass = runtime.find("Torappu", "SceneConsts");
    } catch (...) {
        return {};
    }
    void* field = runtime.class_field(klass, name);
    if (!field) {
        return {};
    }
    void* text = nullptr;
    runtime.field_static_get(field, &text);
    return runtime.string(text);
}
std::string sanitize_cache_token(std::string value) {
    for (char& c : value) {
        const unsigned char code = static_cast<unsigned char>(c);
        if (!(std::isalnum(code) || c == '_' || c == '-' || c == '.')) {
            c = '_';
        }
    }
    if (value.empty()) {
        value = "unknown";
    }
    return value;
}
std::vector<void*> find_controllers(il2cpp::Runtime& runtime, void* controller_class) {
    std::vector<void*> result;
    void* object_class = runtime.find("UnityEngine", "Object");
    void* find = runtime.method(object_class, "FindObjectsOfType", 1, {"System.Type"});
    void* type = runtime.type_object(runtime.class_type(controller_class));
    Root type_root(runtime, type);
    void* array = runtime.invoke(find, nullptr, {type});
    Root array_root(runtime, array);
    const int count = runtime.count(array);
    for (int index = 0; index < count; ++index) {
        if (void* controller = runtime.at(array, index)) {
            result.push_back(controller);
        }
    }
    return result;
}
void* listdict_value_at(il2cpp::Runtime& runtime, void* listdict, int index) {
    void* item = runtime.at(listdict, index);
    if (!item) {
        return nullptr;
    }
    void* klass = runtime.object_class(item);
    const char* name = klass ? runtime.class_name(klass) : nullptr;
    if (name && std::strstr(name, "KeyValuePair")) {
        void* value = nullptr;
        if (read_field_deep(runtime, item, klass, "value", &value)) {
            return value;
        }
    }
    return item;
}
void* player_side_of(il2cpp::Runtime& runtime, void* controller, void* controller_class) {
    void* method = find_method_optional(runtime, controller_class, "get_playerSide", 0);
    if (!method) {
        return nullptr;
    }
    void* boxed = runtime.invoke(method, controller);
    return boxed ? runtime.object_unbox(boxed) : nullptr;
}
int player_side_value(il2cpp::Runtime& runtime, void* controller, void* controller_class) {
    void* value = player_side_of(runtime, controller, controller_class);
    int side = 0;
    if (value) {
        std::memcpy(&side, value, sizeof(side));
    }
    return side;
}
void* find_tile(il2cpp::Runtime& runtime, void* controller, void* controller_class, int row,
                int col) {
    void* map = nullptr;
    void* get_map = find_method_optional(runtime, controller_class, "get_map", 0);
    if (get_map) {
        map = runtime.invoke(get_map, controller);
    }
    if (!map) {
        read_field_deep(runtime, controller, controller_class, "_map", &map);
    }
    if (!map) {
        return nullptr;
    }
    Root map_root(runtime, map);
    void* map_class = runtime.object_class(map);
    void* get_tile = find_method_optional(runtime, map_class, "GetTile", 2);
    if (!get_tile) {
        return nullptr;
    }
    int row_value = row;
    int col_value = col;
    return runtime.invoke(get_tile, map, {&row_value, &col_value});
}
void* find_character(il2cpp::Runtime& runtime, void* controller, void* controller_class,
                     std::uint32_t uid) {
    void* manager = nullptr;
    if (!read_field_deep(runtime, controller, controller_class, "m_objectManger", &manager) ||
        !manager) {
        return nullptr;
    }
    Root manager_root(runtime, manager);
    void* manager_class = runtime.object_class(manager);
    void* list = nullptr;
    if (!read_field_deep(runtime, manager, manager_class, "m_cachedList", &list) || !list) {
        return nullptr;
    }
    Root list_root(runtime, list);
    void* bobject_class = runtime.find("Torappu.Battle", "BObject");
    void* character_class = runtime.find("Torappu.Battle", "Character");
    const int count = runtime.count(list);
    for (int index = 0; index < count; ++index) {
        void* object = runtime.at(list, index);
        if (!object) {
            continue;
        }
        void* klass = runtime.object_class(object);
        if (klass != character_class) {
            continue;
        }
        std::uint32_t instance_uid = 0;
        if (read_field_deep(runtime, object, bobject_class, "<instanceUid>k__BackingField",
                            &instance_uid) &&
            instance_uid == uid) {
            return object;
        }
        std::uint32_t card_uid = 0;
        if (read_field_deep(runtime, object, character_class, "<cardUid>k__BackingField",
                            &card_uid) &&
            card_uid == uid) {
            return object;
        }
    }
    return nullptr;
}
void* find_card(il2cpp::Runtime& runtime, void* controller, void* controller_class,
                std::uint32_t uid, void** deck_out) {
    void* decks = nullptr;
    if (!read_field_deep(runtime, controller, controller_class, "m_deckDict", &decks) || !decks) {
        return nullptr;
    }
    Root decks_root(runtime, decks);
    void* deck_class = runtime.find("Torappu.Battle", "Deck");
    void* find = find_method_optional(runtime, deck_class, "FindCard", 1);
    if (!find) {
        return nullptr;
    }
    const int count = runtime.count(decks);
    for (int index = 0; index < count; ++index) {
        void* deck = listdict_value_at(runtime, decks, index);
        if (!deck) {
            continue;
        }
        Root deck_root(runtime, deck);
        std::uint32_t wanted = uid;
        void* card = runtime.invoke(find, deck, {&wanted});
        if (card) {
            if (deck_out) {
                *deck_out = deck;
            }
            return card;
        }
    }
    return nullptr;
}
std::string describe_class_layout(il2cpp::Runtime& runtime, void* klass, bool include_methods) {
    if (!klass) {
        return "<null>";
    }
    std::ostringstream output;
    output << runtime.klass_name(klass) << " instance_size=";
    uint32_t alignment = 0;
    output << runtime.class_value_size(klass, &alignment) << " alignment=" << alignment << '\n';
    void* field_iterator = nullptr;
    int field_count = 0;
    for (void* owner = klass; owner; owner = runtime.class_parent(owner)) {
        field_iterator = nullptr;
        while (void* field = runtime.class_fields(owner, &field_iterator)) {
            const char* name = runtime.field_name(field);
            std::string type;
            try {
                type = runtime.type(runtime.field_type(field));
            } catch (...) {
                type = "?";
            }
            output << "  F[" << runtime.klass_name(owner) << "] "
                   << (name ? name : "?") << " @" << runtime.field_offset(field)
                   << " : " << type << '\n';
            if (++field_count >= 256) {
                output << "  ... fields truncated\n";
                break;
            }
        }
        if (field_count >= 256) {
            break;
        }
    }
    if (include_methods) {
        void* method_iterator = nullptr;
        int method_count = 0;
        while (void* method = runtime.class_methods(klass, &method_iterator)) {
            const char* name = runtime.method_name(method);
            std::string method_text = name ? name : "?";
            if (method_text.find("buff") != std::string::npos ||
                method_text.find("Buff") != std::string::npos ||
                method_text.find("Count") != std::string::npos ||
                method_text.find("Item") != std::string::npos ||
                method_text.find("Add") != std::string::npos ||
                method_text.find("Remove") != std::string::npos ||
                method_text.find("Get") != std::string::npos) {
                output << "  M " << method_text << '/'
                       << runtime.method_count(method) << " -> ";
                try {
                    output << runtime.type(runtime.method_return(method));
                } catch (...) {
                    output << '?';
                }
                output << " entry=" << *static_cast<void**>(method) << '\n';
            }
            if (++method_count >= 1024) {
                output << "  ... methods truncated\n";
                break;
            }
        }
    }
    if (void* parent = runtime.class_parent(klass)) {
        output << "  parent=" << runtime.klass_name(parent) << '\n';
    }
    return output.str();
}
void collect_buff_uid_map(il2cpp::Runtime& runtime, void* map, void* buff_class,
                          std::vector<void*>& output, std::unordered_set<void*>& seen,
                          int& budget) {
    if (!map || budget <= 0) {
        return;
    }
    void* map_class = runtime.object_class(map);
    void* entries = nullptr;
    if (!read_field_deep(runtime, map, map_class, "_entries", &entries) || !entries) {
        return;
    }
    Root entries_root(runtime, entries);
    int count = 0;
    try {
        count = runtime.count(entries);
    } catch (...) {
        return;
    }
    for (int index = 0; index < count && budget > 0; ++index) {
        --budget;
        void* entry = nullptr;
        try {
            entry = runtime.at(entries, index);
        } catch (...) {
            continue;
        }
        if (!entry) {
            continue;
        }
        Root entry_root(runtime, entry);
        void* entry_class = runtime.object_class(entry);
        void* value_field = nullptr;
        try {
            value_field = runtime.field(entry_class, "value");
        } catch (...) {
        }
        if (!value_field) {
            continue;
        }
        void* buff = nullptr;
        try {
            buff = runtime.field_get_object(value_field, entry);
        } catch (...) {
        }
        if (buff && runtime.object_class(buff) == buff_class && !seen.contains(buff)) {
            seen.insert(buff);
            output.push_back(buff);
        }
    }
}
void collect_buff_internal_list(il2cpp::Runtime& runtime, void* list, void* buff_class,
                                std::vector<void*>& output, std::unordered_set<void*>& seen) {
    if (!list) {
        return;
    }
    void* list_class = runtime.object_class(list);
    void* items = nullptr;
    int size = 0;
    if (!read_field_deep(runtime, list, list_class, "_items", &items) || !items ||
        !read_field_deep(runtime, list, list_class, "_size", &size) || size <= 0) {
        return;
    }
    Root items_root(runtime, items);
    int array_count = 0;
    try {
        array_count = runtime.count(items);
    } catch (...) {
        return;
    }
    for (int index = 0; index < size && index < array_count && index < 512; ++index) {
        void* item = nullptr;
        try {
            item = runtime.at(items, index);
        } catch (...) {
            continue;
        }
        if (!item) {
            continue;
        }
        void* item_class = runtime.object_class(item);
        const std::string name = runtime.klass_name(item_class);
        void* target = item;
        if (name.starts_with("Torappu.ObjectPtr")) {
            target = nullptr;
            if (runtime.class_is_value(item_class)) {
                void* storage = runtime.object_unbox(item);
                if (storage) {
                    std::memcpy(&target, storage, sizeof(target));
                }
            } else {
                void* obj_field = runtime.class_field(item_class, "obj");
                if (obj_field) {
                    target = runtime.field_get_object(obj_field, item);
                }
            }
        }
        if (target) {
            const bool is_buff = runtime.object_class(target) == buff_class;
            if (is_buff && seen.insert(target).second) {
                output.push_back(target);
            }
        }
    }
}
std::string join_nonempty(const std::vector<std::string>& values, const char* separator) {
    std::string result;
    for (const std::string& value : values) {
        if (value.empty()) {
            continue;
        }
        if (!result.empty()) {
            result += separator;
        }
        result += value;
    }
    return result;
}
void read_buff_modifiers(il2cpp::Runtime& runtime, void* buff, void* buff_class,
                         std::vector<BuffModifierInfo>& output) {
    output.clear();
    std::int64_t attribute_mask = 0;
    read_field_deep(runtime, buff, buff_class, "<attributeMask>k__BackingField",
                    &attribute_mask);
    const auto collect = [&](const char* field_name, BuffModifierKind kind,
                             double identity) {
        void* values = nullptr;
        if (!read_field_deep(runtime, buff, buff_class, field_name, &values) || !values) {
            return;
        }
        Root values_root(runtime, values);
        int count = 0;
        try {
            count = runtime.count(values);
        } catch (...) {
            return;
        }
        for (int index = 0;
             index < count &&
             index < static_cast<int>(std::size(kBuffAttributeNames));
             ++index) {
            if (attribute_mask != 0 &&
                (attribute_mask & (std::int64_t{1} << index)) == 0) {
                continue;
            }
            void* boxed = nullptr;
            try {
                boxed = runtime.at(values, index);
            } catch (...) {
                continue;
            }
            const double value = unbox_fp(runtime, boxed);
            if (std::abs(value - identity) < 1e-9) {
                continue;
            }
            output.push_back(
                {kind, kBuffAttributeNames[index], value});
        }
    };
    collect("m_attributeAdditions", BuffModifierKind::Addition, 0.0);
    collect("m_attributeMultipliers", BuffModifierKind::Multiplier, 0.0);
    collect("m_attributeFinalAdditions", BuffModifierKind::FinalAddition, 0.0);
    collect("m_attributeFinalScalers", BuffModifierKind::FinalScaler, 1.0);
}
std::string format_buff_modifiers(const std::vector<BuffModifierInfo>& entries) {
    std::vector<std::string> parts;
    parts.reserve(entries.size());
    for (const BuffModifierInfo& entry : entries) {
        char text[96]{};
        switch (entry.kind) {
        case BuffModifierKind::Addition:
            std::snprintf(text, sizeof(text), "%s+%.4f",
                          entry.attribute.c_str(), entry.value);
            break;
        case BuffModifierKind::Multiplier:
            std::snprintf(text, sizeof(text), "%s%.2f%%",
                          entry.attribute.c_str(), entry.value * 100.0);
            break;
        case BuffModifierKind::FinalAddition:
            std::snprintf(text, sizeof(text), "%s+%.4f(final)",
                          entry.attribute.c_str(), entry.value);
            break;
        case BuffModifierKind::FinalScaler:
            std::snprintf(text, sizeof(text), "%s*%.4f(final)",
                          entry.attribute.c_str(), entry.value);
            break;
        }
        parts.emplace_back(text);
    }
    return join_nonempty(parts, ", ");
}
std::string format_buff_modifiers(il2cpp::Runtime& runtime, void* buff,
                                  void* buff_class) {
    std::vector<BuffModifierInfo> entries;
    read_buff_modifiers(runtime, buff, buff_class, entries);
    return format_buff_modifiers(entries);
}
std::int64_t buff_modifier_time_bucket(std::int64_t raw) {
    if (raw < 0) {
        return std::numeric_limits<std::int64_t>::lowest();
    }
    constexpr std::int64_t kBucketSize =
        static_cast<std::int64_t>(kFixedPointScale / 2.0);
    return raw / kBucketSize;
}
std::string format_abnormal_mask(std::uint64_t mask, const char* const* names,
                                 std::size_t count) {
    std::vector<std::string> parts;
    for (std::size_t index = 0; index < count; ++index) {
        if (mask & (std::uint64_t{1} << index)) {
            parts.emplace_back(names[index]);
        }
    }
    return join_nonempty(parts, ", ");
}
void read_blackboard_entries(il2cpp::Runtime& runtime, void* board,
                             std::vector<BuffBlackboardEntry>& output) {
    output.clear();
    if (!board) {
        return;
    }
    Root board_root(runtime, board);
    void* pairs = nullptr;
    if (!read_field_deep(runtime, board, runtime.object_class(board), "_items",
                         &pairs) ||
        !pairs) {
        return;
    }
    Root pairs_root(runtime, pairs);
    int count = 0;
    try {
        count = runtime.count(pairs);
    } catch (...) {
        return;
    }
    for (int index = 0; index < count && index < 128; ++index) {
        void* pair = nullptr;
        try {
            pair = runtime.at(pairs, index);
        } catch (...) {
            continue;
        }
        if (!pair) {
            continue;
        }
        Root pair_root(runtime, pair);
        void* pair_class = runtime.object_class(pair);
        const std::string key =
            read_string_field(runtime, pair, pair_class, "key");
        const std::string value_str =
            read_string_field(runtime, pair, pair_class, "valueStr");
        float value = 0.0f;
        read_field_deep(runtime, pair, pair_class, "value", &value);
        if (key.empty() && value_str.empty()) {
            continue;
        }
        BuffBlackboardEntry entry;
        entry.key = key;
        entry.value_str = value_str;
        entry.value = static_cast<double>(value);
        output.push_back(std::move(entry));
    }
}
void read_buff_blackboard(il2cpp::Runtime& runtime, void* buff, void* buff_class,
                          std::vector<BuffBlackboardEntry>& output) {
    output.clear();
    void* board = nullptr;
    if (!read_field_deep(runtime, buff, buff_class, "m_blackboard", &board) ||
        !board) {
        return;
    }
    read_blackboard_entries(runtime, board, output);
}
std::string format_buff_blackboard(
    const std::vector<BuffBlackboardEntry>& entries) {
    std::vector<std::string> parts;
    parts.reserve(entries.size());
    for (const BuffBlackboardEntry& entry : entries) {
        std::string text = entry.key + "=";
        if (!entry.value_str.empty()) {
            text += entry.value_str;
        } else {
            char number[64]{};
            std::snprintf(number, sizeof(number), "%.6g",
                          entry.value);
            text += number;
        }
        parts.emplace_back(std::move(text));
    }
    return join_nonempty(parts, ", ");
}
std::string format_buff_blackboard(il2cpp::Runtime& runtime, void* buff,
                                   void* buff_class) {
    std::vector<BuffBlackboardEntry> entries;
    read_buff_blackboard(runtime, buff, buff_class, entries);
    return format_buff_blackboard(entries);
}
void fill_buff_static(il2cpp::Runtime& runtime, void* buff, void* buff_class,
                      void* entity_class, BuffStaticInfo& info) {
    info.key = read_string_field(runtime, buff, buff_class, "<key>k__BackingField");
    info.override_key =
        read_string_field(runtime, buff, buff_class, "<overrideKey>k__BackingField");
    info.effect_key =
        read_string_field(runtime, buff, buff_class, "<effectKey>k__BackingField");
    info.priority = 0;
    read_field_deep(runtime, buff, buff_class, "<priority>k__BackingField",
                    &info.priority);
    info.event_priority = 0;
    read_field_deep(runtime, buff, buff_class, "<onEventPriority>k__BackingField",
                    &info.event_priority);

    void* data = nullptr;
    if (read_field_deep(runtime, buff, buff_class, "m_data", &data) && data) {
        void* data_class = runtime.object_class(data);
        info.template_key = read_string_field(runtime, data, data_class, "templateKey");
        int data_max_stack = 0;
        if (read_field_deep(runtime, data, data_class, "maxStackCnt", &data_max_stack)) {
            info.data_max_stack_count = data_max_stack;
        }
    }
    if (info.template_key.empty()) {
        info.template_key = info.key;
    }

    const auto read_mask = [&](const char* field, std::int64_t* mask) {
        *mask = 0;
        read_field_deep(runtime, buff, buff_class, field, mask);
    };
    read_mask("<abnormalFlagMask>k__BackingField", &info.abnormal_flag_mask);
    read_mask("<abnormalImmuneMask>k__BackingField", &info.abnormal_immune_mask);
    read_mask("<abnormalAntiMask>k__BackingField", &info.abnormal_anti_mask);
    read_mask("<abnormalComboMask>k__BackingField", &info.abnormal_combo_mask);
    read_mask("<abnormalComboImmuneMask>k__BackingField",
              &info.abnormal_combo_immune_mask);
    info.abnormal_flags = format_abnormal_mask(
        static_cast<std::uint64_t>(info.abnormal_flag_mask),
        kBuffAbnormalNames, std::size(kBuffAbnormalNames));
    info.abnormal_immune_flags = format_abnormal_mask(
        static_cast<std::uint64_t>(info.abnormal_immune_mask),
        kBuffAbnormalNames, std::size(kBuffAbnormalNames));
    info.abnormal_anti_flags = format_abnormal_mask(
        static_cast<std::uint64_t>(info.abnormal_anti_mask),
        kBuffAbnormalNames, std::size(kBuffAbnormalNames));
    info.abnormal_combo_flags = format_abnormal_mask(
        static_cast<std::uint64_t>(info.abnormal_combo_mask),
        kBuffAbnormalComboNames, std::size(kBuffAbnormalComboNames));
    info.abnormal_combo_immune_flags = format_abnormal_mask(
        static_cast<std::uint64_t>(info.abnormal_combo_immune_mask),
        kBuffAbnormalComboNames, std::size(kBuffAbnormalComboNames));
    read_buff_blackboard(runtime, buff, buff_class, info.blackboard_entries);
    info.blackboard = format_buff_blackboard(info.blackboard_entries);
    read_buff_modifiers(runtime, buff, buff_class, info.modifier_entries);
    info.modifiers = format_buff_modifiers(info.modifier_entries);

    void* get_source = find_method_optional(runtime, buff_class, "get_source", 0);
    if (get_source) {
        void* source = nullptr;
        try {
            source = runtime.invoke(get_source, buff);
        } catch (...) {
        }
        if (source && entity_class) {
            Root source_root(runtime, source);
            info.source_id = method_string(runtime, source, entity_class, "get_id");
        }
    }
}
void fill_buff_dynamic(il2cpp::Runtime& runtime, void* buff, void* buff_class,
                       std::uint32_t uid, BuffStaticInfo& static_info,
                       UnitInfo::BuffInfo& info) {
    const auto read_fp_raw = [&](const char* name) {
        std::int64_t raw = 0;
        read_field_deep(runtime, buff, buff_class, name, &raw);
        return raw;
    };
    const auto read_bool = [&](const char* name, bool fallback = false) {
        bool value = fallback;
        read_field_deep(runtime, buff, buff_class, name, &value);
        return value;
    };
    const auto read_int = [&](const char* name, int fallback = 0) {
        int value = fallback;
        read_field_deep(runtime, buff, buff_class, name, &value);
        return value;
    };

    info.key = static_info.key;
    info.template_key = static_info.template_key;
    info.override_key = static_info.override_key;
    info.effect_key = static_info.effect_key;
    info.source_id = static_info.source_id;
    info.abnormal_flags = static_info.abnormal_flags;
    info.abnormal_immune_flags = static_info.abnormal_immune_flags;
    info.abnormal_anti_flags = static_info.abnormal_anti_flags;
    info.abnormal_combo_flags = static_info.abnormal_combo_flags;
    info.abnormal_combo_immune_flags =
        static_info.abnormal_combo_immune_flags;
    info.abnormal_flag_mask = static_info.abnormal_flag_mask;
    info.abnormal_immune_mask = static_info.abnormal_immune_mask;
    info.abnormal_anti_mask = static_info.abnormal_anti_mask;
    info.abnormal_combo_mask = static_info.abnormal_combo_mask;
    info.abnormal_combo_immune_mask =
        static_info.abnormal_combo_immune_mask;
    info.uid = uid;
    info.priority = static_info.priority;
    info.event_priority = static_info.event_priority;
    const std::int64_t life_time_raw = read_fp_raw("m_lifeTime");
    const std::int64_t remaining_time_raw = read_fp_raw("m_remainingTime");
    const std::int64_t existing_time_raw = read_fp_raw("m_existingTime");
    info.life_time = static_cast<double>(life_time_raw) / kFixedPointScale;
    info.remaining_time = static_cast<double>(remaining_time_raw) / kFixedPointScale;
    info.existing_time = static_cast<double>(existing_time_raw) / kFixedPointScale;
    info.remaining_ratio =
        method_fp(runtime, buff, runtime.object_class(buff), "get_remainingRatio");
    info.trigger_interval =
        method_fp(runtime, buff, runtime.object_class(buff), "get_triggerInterval");
    info.stack_count = read_int("m_stackCnt", 0);
    const int max_valid_stack = read_int("m_maxValidStackCnt", 0);
    info.valid_stack_count =
        max_valid_stack > 0 ? std::min(info.stack_count, max_valid_stack)
                            : info.stack_count;
    info.max_stack_count = std::max(max_valid_stack, static_info.data_max_stack_count);
    info.trigger_count =
        method_int(runtime, buff, runtime.object_class(buff), "get_triggerCnt",
                   read_int("m_triggerCnt", 0));
    const bool time_dynamic =
        info.life_time >= 0 || info.trigger_interval >= 0 ||
        !static_info.modifiers.empty();
    const std::int64_t remaining_bucket =
        time_dynamic && info.life_time >= 0
            ? buff_modifier_time_bucket(remaining_time_raw)
            : std::numeric_limits<std::int64_t>::lowest();
    const std::int64_t existing_bucket =
        time_dynamic && info.life_time < 0
            ? buff_modifier_time_bucket(existing_time_raw)
            : std::numeric_limits<std::int64_t>::lowest();
    const bool refresh_modifiers_every_snapshot =
        !static_info.modifiers.empty() || info.trigger_interval >= 0;
    if (refresh_modifiers_every_snapshot || !static_info.modifiers_initialized ||
        static_info.modifiers_remaining_bucket != remaining_bucket ||
        static_info.modifiers_existing_bucket != existing_bucket ||
        static_info.modifiers_stack_count != info.stack_count ||
        static_info.modifiers_trigger_count != info.trigger_count) {
        read_buff_modifiers(runtime, buff, buff_class,
                            static_info.modifier_entries);
        static_info.modifiers =
            format_buff_modifiers(static_info.modifier_entries);
        static_info.modifiers_remaining_bucket = remaining_bucket;
        static_info.modifiers_existing_bucket = existing_bucket;
        static_info.modifiers_stack_count = info.stack_count;
        static_info.modifiers_trigger_count = info.trigger_count;
        read_buff_blackboard(runtime, buff, buff_class,
                             static_info.blackboard_entries);
        static_info.blackboard =
            format_buff_blackboard(static_info.blackboard_entries);
        static_info.modifiers_initialized = true;
    }
    info.modifiers = static_info.modifiers;
    info.blackboard = static_info.blackboard;
    info.modifier_entries = static_info.modifier_entries;
    info.blackboard_entries = static_info.blackboard_entries;
    info.enabled =
        method_bool(runtime, buff, runtime.object_class(buff), "get_isEnabled");
    info.finished =
        method_bool(runtime, buff, runtime.object_class(buff), "get_isFinished");
    info.valid = method_bool(runtime, buff, runtime.object_class(buff), "get_isValid",
                             true);
    info.triggerable =
        method_bool(runtime, buff, runtime.object_class(buff), "get_triggerable");
    info.status_resistable = method_bool(runtime, buff, runtime.object_class(buff),
                                         "get_isStatusResistable");
    info.has_shield = read_bool("<hasShield>k__BackingField");
}
void read_talents_from_data(il2cpp::Runtime& runtime, void* data, void* data_class,
                            int potential, std::vector<TalentInfo>& talents) {
    void* list = nullptr;
    if (!read_field_deep(runtime, data, data_class, "talents", &list) || !list) {
        return;
    }
    std::unordered_map<std::string, std::size_t> positions;
    const int count = runtime.count(list);
    for (int index = 0; index < count; ++index) {
        void* talent = runtime.at(list, index);
        if (!talent) {
            continue;
        }
        void* talent_class = runtime.object_class(talent);
        TalentInfo info;
        info.name = read_string_field(runtime, talent, talent_class, "name");
        info.description = read_string_field(runtime, talent, talent_class, "description");
        read_field_deep(runtime, talent, talent_class, "requiredPotentialRank",
                        &info.potential);
        read_field_deep(runtime, talent, talent_class, "isHideTalent", &info.hidden);
        try {
            const std::string formatted =
                method_string(runtime, talent, talent_class, "GetDescription");
            if (!formatted.empty()) {
                info.description = formatted;
            }
        } catch (...) {
        }
        if (info.hidden || info.name.empty() || info.potential > potential) {
            continue;
        }
        const auto existing = positions.find(info.name);
        if (existing == positions.end()) {
            positions.emplace(info.name, talents.size());
            talents.push_back(std::move(info));
        } else if (info.potential >= talents[existing->second].potential) {
            talents[existing->second] = std::move(info);
        }
    }
}
void read_modules_from_data(il2cpp::Runtime& runtime, void* data, void* data_class,
                            std::vector<ModuleInfo>& modules) {
    modules.clear();
    if (!data || !data_class) {
        return;
    }
    void* queries = nullptr;
    if (read_field_deep(runtime, data, data_class, "uniEquipQueries", &queries) && queries) {
        Root queries_root(runtime, queries);
        void* queries_class = runtime.object_class(queries);
        void* pair_class = runtime.class_element(queries_class);
        const UniqueEquipPairLayout layout =
            resolve_unique_equip_pair_class(runtime, pair_class);
        if (layout.valid()) {
            const int count = runtime.count(queries);
            for (int index = 0; index < count && index < 8; ++index) {
                void* boxed = nullptr;
                try {
                    boxed = runtime.at(queries, index);
                } catch (...) {
                    continue;
                }
                void* pair = boxed ? runtime.object_unbox(boxed) : nullptr;
                if (!pair) {
                    continue;
                }
                ModuleInfo module;
                void* text = nullptr;
                if (!read_field_deep(runtime, pair, pair_class, layout.id_name.c_str(),
                                     &text) ||
                    !text) {
                    continue;
                }
                module.id = runtime.string(text);
                read_field_deep(runtime, pair, pair_class, layout.level_name.c_str(),
                                &module.level);
                if (!module.id.empty()) {
                    modules.push_back(std::move(module));
                }
            }
        }
    }
    if (!modules.empty()) {
        return;
    }

    // Older or partially converted battle data may only expose the resolved modules.
    void* equips = nullptr;
    if (!read_field_deep(runtime, data, data_class, "uniEquips", &equips) || !equips) {
        return;
    }
    Root equips_root(runtime, equips);
    const int count = runtime.count(equips);
    for (int index = 0; index < count && index < 8; ++index) {
        void* equip = nullptr;
        try {
            equip = runtime.at(equips, index);
        } catch (...) {
            continue;
        }
        if (!equip) {
            continue;
        }
        void* equip_class = runtime.object_class(equip);
        void* equip_data = nullptr;
        if (!read_field_deep(runtime, equip, equip_class, "m_eData", &equip_data) ||
            !equip_data) {
            continue;
        }
        const std::string key = read_string_field(
            runtime, equip_data, runtime.object_class(equip_data), "resKey");
        if (!key.empty()) {
            modules.push_back({key, 3});
        }
    }
}
void read_character_modules(il2cpp::Runtime& runtime, void* character, void* character_class,
                            std::vector<ModuleInfo>& modules) {
    modules.clear();
    void* data = nullptr;
    if (!read_field_deep(runtime, character, character_class, "<data>k__BackingField",
                         &data) ||
        !data) {
        return;
    }
    read_modules_from_data(runtime, data, runtime.object_class(data), modules);
}
void read_entity_buffs(il2cpp::Runtime& runtime, void* entity, void* entity_class,
                       void* buff_class,
                       std::map<std::pair<void*, std::uint32_t>, BuffStaticInfo>&
                           static_cache,
                       std::uint64_t generation, std::uint64_t& cache_hits,
                       std::uint64_t& cache_misses,
                       std::vector<UnitInfo::BuffInfo>& output,
                       int* shield_layers) {
    void* container = nullptr;
    if (!read_field_deep(runtime, entity, entity_class,
                         "<buffContainer>k__BackingField", &container) ||
        !container) {
        return;
    }
    Root container_root(runtime, container);
    void* container_class = runtime.object_class(container);
    if (!buff_class) {
        return;
    }

    std::vector<void*> objects;
    std::unordered_set<void*> seen;
    int budget = 512;
    void* buffs = nullptr;
    if (read_field_deep(runtime, container, container_class, "m_buffs", &buffs) &&
        buffs) {
        Root buffs_root(runtime, buffs);
        void* internal_list = nullptr;
        if (read_field_deep(runtime, buffs, runtime.object_class(buffs),
                            "m_internalList", &internal_list) &&
            internal_list) {
            Root internal_root(runtime, internal_list);
            std::unordered_set<void*> list_seen;
            collect_buff_internal_list(runtime, internal_list, buff_class, objects,
                                       list_seen);
        }
    }
    void* uid_map = nullptr;
    if (read_field_deep(runtime, container, container_class, "m_buffUidMap",
                        &uid_map) &&
        uid_map) {
        Root uid_map_root(runtime, uid_map);
        collect_buff_uid_map(runtime, uid_map, buff_class, objects, seen, budget);
    }
    std::sort(objects.begin(), objects.end());
    objects.erase(std::unique(objects.begin(), objects.end()), objects.end());

    for (void* buff : objects) {
        Root buff_root(runtime, buff);
        std::uint32_t uid = 0;
        read_field_deep(runtime, buff, buff_class, "<instanceUid>k__BackingField", &uid);
        const auto key = std::make_pair(buff, uid);
        UnitInfo::BuffInfo info;
        try {
            auto found = static_cache.find(key);
            if (found == static_cache.end()) {
                ++cache_misses;
                BuffStaticInfo static_info;
                fill_buff_static(runtime, buff, buff_class, entity_class, static_info);
                static_info.seen_generation = generation;
                found = static_cache.emplace(key, std::move(static_info)).first;
            } else {
                ++cache_hits;
                found->second.seen_generation = generation;
            }
            fill_buff_dynamic(runtime, buff, buff_class, uid, found->second, info);
        } catch (...) {
            continue;
        }
        if (info.finished) {
            continue;
        }
        if (shield_layers && info.has_shield && info.enabled && info.valid) {
            *shield_layers +=
                std::max(1, info.valid_stack_count > 0 ? info.valid_stack_count
                                                       : info.stack_count);
        }
        output.push_back(std::move(info));
    }
    std::sort(output.begin(), output.end(),
              [](const UnitInfo::BuffInfo& left, const UnitInfo::BuffInfo& right) {
                  return left.uid < right.uid;
              });
}
void write_type_dump(il2cpp::Runtime& runtime, const std::filesystem::path& path,
                     void* klass) {
    std::ostringstream output;
    output << "type " << runtime.klass_name(klass) << "\n";
    output << "fields:\n";
    void* field_iterator = nullptr;
    while (void* field = runtime.class_fields(klass, &field_iterator)) {
        output << "  +0x" << std::hex << runtime.field_offset(field) << std::dec << " ";
        try {
            output << runtime.type(runtime.field_type(field));
        } catch (...) {
            output << "?";
        }
        output << " " << (runtime.field_name(field) ? runtime.field_name(field) : "?")
               << "\n";
    }
    output << "methods:\n";
    void* method_iterator = nullptr;
    while (void* method = runtime.class_methods(klass, &method_iterator)) {
        const char* name = runtime.method_name(method);
        if (!name) {
            continue;
        }
        output << "  " << name << "(";
        for (std::uint32_t index = 0; index < runtime.method_count(method); ++index) {
            if (index) {
                output << ", ";
            }
            try {
                output << runtime.type(runtime.method_param(method, index));
            } catch (...) {
                output << "?";
            }
        }
        output << ") -> ";
        try {
            output << runtime.type(runtime.method_return(method));
        } catch (...) {
            output << "?";
        }
        char pointer[32];
        std::snprintf(pointer, sizeof(pointer), "%p", *static_cast<void**>(method));
        output << " entry=" << pointer << "\n";
    }
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    file << output.str();
    file.flush();
    if (!file) {
        throw std::runtime_error("类型信息写入失败");
    }
}
std::vector<DictionaryPair> read_dictionary_pairs(il2cpp::Runtime& runtime, void* dictionary,
                                                  int limit) {
    std::vector<DictionaryPair> result;
    if (!dictionary) {
        return result;
    }
    void* dictionary_class = runtime.object_class(dictionary);
    void* entries = nullptr;
    if (!read_field_deep(runtime, dictionary, dictionary_class, "_entries", &entries) ||
        !entries) {
        return result;
    }
    Root entries_root(runtime, entries);
    const int count = runtime.count(entries);
    result.reserve(static_cast<std::size_t>(std::clamp(count, 0, limit)));
    for (int index = 0; index < count && index < limit; ++index) {
        void* entry = nullptr;
        try {
            entry = runtime.at(entries, index);
        } catch (...) {
            continue;
        }
        if (!entry) {
            continue;
        }
        Root entry_root(runtime, entry);
        void* entry_class = runtime.object_class(entry);
        void* key_field = nullptr;
        void* value_field = nullptr;
        try {
            key_field = runtime.field(entry_class, "key");
            value_field = runtime.field(entry_class, "value");
        } catch (...) {
            continue;
        }
        DictionaryPair pair;
        pair.key = runtime.field_get_object(key_field, entry);
        pair.value = runtime.field_get_object(value_field, entry);
        result.push_back(pair);
    }
    return result;
}
void* dictionary_value_for_key(il2cpp::Runtime& runtime, void* dictionary,
                               const std::string& key) {
    for (const auto& pair : read_dictionary_pairs(runtime, dictionary)) {
        if (pair.key && runtime.string(pair.key) == key) {
            return pair.value;
        }
    }
    return nullptr;
}
void* empty_collection_for_field(il2cpp::Runtime& runtime, void* klass,
                                 const char* field_name) {
    void* field = runtime.field(klass, field_name);
    void* collection = runtime.class_from_type(runtime.field_type(field));
    if (!collection) {
        throw std::runtime_error(std::string("字段不是集合类型: ") + field_name);
    }
    void* constructor = runtime.method(collection, ".ctor", 0);
    return construct_with_args(runtime, collection, constructor, {});
}
void collection_add(il2cpp::Runtime& runtime, void* collection, void* value) {
    void* collection_class = runtime.object_class(collection);
    void* add = runtime.method(collection_class, "Add", 1);
    void* item = value;
    if (value && runtime.object_unbox) {
        void* value_class = runtime.object_class(value);
        if (value_class && runtime.class_is_value(value_class)) {
            item = runtime.object_unbox(value);
        }
    }
    runtime.invoke(add, collection, {item});
}

} // namespace ark::better_battle
