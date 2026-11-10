#include "battle_helpers.h"

namespace ark::better_battle {

void LocalBattle::inspect_type(const std::string& text, const std::string& path) {
    try {
        const std::size_t separator = path.find('/');
        void* klass = runtime_.find(
            text.c_str(),
            (separator == std::string::npos ? path : path.substr(0, separator))
                .c_str());
        std::size_t cursor = separator;
        while (klass && cursor != std::string::npos) {
            const std::size_t next = path.find('/', cursor + 1);
            klass = runtime_.nested(
                klass,
                path.substr(cursor + 1, next == std::string::npos
                                            ? std::string::npos
                                            : next - cursor - 1)
                    .c_str());
            cursor = next;
        }
        if (!klass) {
            throw std::runtime_error("类型不存在: " + text + "." + path);
        }
        std::string file_name = path;
        std::replace(file_name.begin(), file_name.end(), '/', '_');
        std::replace(file_name.begin(), file_name.end(), '.', '_');
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) /
            ("battle_type_" + file_name + ".txt");
        write_type_dump(runtime_, path, klass);
        publish_result("类型信息已写入 " + path.string());
    } catch (const std::exception& error) {
        publish_result(std::string("读取类型信息失败：") + error.what(), false);
    }
}

void LocalBattle::search_types(const std::string& text) {
    try {
        std::string query = text;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        if (query.empty()) {
            throw std::runtime_error("type-search 需要查询文本");
        }
        if (!runtime_.image_class_count || !runtime_.image_class) {
            throw std::runtime_error("当前 IL2CPP 不提供 image class 枚举");
        }
        const auto contains = [&](const std::string& value) {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return lower.find(query) != std::string::npos;
        };
        std::ostringstream output;
        std::size_t assembly_count = 0;
        void** assemblies =
            runtime_.domain_assemblies(runtime_.domain_get(), &assembly_count);
        std::size_t matches = 0;
        std::size_t scanned = 0;
        for (std::size_t assembly_index = 0;
             assemblies && assembly_index < assembly_count; ++assembly_index) {
            void* image = runtime_.assembly_image(assemblies[assembly_index]);
            if (!image) {
                continue;
            }
            const std::size_t class_count = runtime_.image_class_count(image);
            for (std::size_t class_index = 0; class_index < class_count;
                 ++class_index) {
                void* klass = runtime_.image_class(image, class_index);
                if (!klass) {
                    continue;
                }
                ++scanned;
                const char* name_space = runtime_.class_namespace(klass);
                const char* name = runtime_.class_name(klass);
                std::string full_name =
                    std::string(name_space && *name_space ? name_space : "") +
                    (name_space && *name_space ? "." : "") +
                    (name ? name : "?");
                std::vector<std::string> matched_fields;
                void* field_iterator = nullptr;
                while (void* field = runtime_.class_fields(klass, &field_iterator)) {
                    std::string field_type;
                    try {
                        field_type = runtime_.type(runtime_.field_type(field));
                    } catch (...) {
                        field_type = "?";
                    }
                    const std::string field_name =
                        runtime_.field_name(field) ? runtime_.field_name(field) : "?";
                    if (contains(field_type) || contains(field_name)) {
                        matched_fields.push_back(field_type + " " + field_name);
                    }
                }
                std::vector<std::string> matched_methods;
                void* method_iterator = nullptr;
                while (void* method = runtime_.class_methods(klass, &method_iterator)) {
                    const std::string method_name =
                        runtime_.method_name(method) ? runtime_.method_name(method) : "?";
                    bool matched = contains(method_name);
                    std::ostringstream signature;
                    signature << method_name << "(";
                    for (std::uint32_t index = 0;
                         index < runtime_.method_count(method); ++index) {
                        if (index) {
                            signature << ", ";
                        }
                        std::string parameter_type;
                        try {
                            parameter_type =
                                runtime_.type(runtime_.method_param(method, index));
                        } catch (...) {
                            parameter_type = "?";
                        }
                        matched = matched || contains(parameter_type);
                        signature << parameter_type;
                    }
                    signature << ") -> ";
                    std::string return_type;
                    try {
                        return_type = runtime_.type(runtime_.method_return(method));
                    } catch (...) {
                        return_type = "?";
                    }
                    matched = matched || contains(return_type);
                    signature << return_type;
                    if (matched) {
                        matched_methods.push_back(signature.str());
                    }
                }
                if (!contains(full_name) && matched_fields.empty() &&
                    matched_methods.empty()) {
                    continue;
                }
                ++matches;
                output << "type " << full_name << "\n";
                if (!matched_fields.empty()) {
                    output << " fields:\n";
                    for (const std::string& field : matched_fields) {
                        output << "  " << field << "\n";
                    }
                }
                if (!matched_methods.empty()) {
                    output << " methods:\n";
                    for (const std::string& method : matched_methods) {
                        output << "  " << method << "\n";
                    }
                }
                if (matches >= 500) {
                    output << "... output truncated at 500 classes\n";
                    break;
                }
            }
            if (matches >= 500) {
                break;
            }
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "battle_type_search.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << "query " << text << "\n";
        file << "scanned " << scanned << " classes, matches " << matches << "\n\n";
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("类型搜索结果写入失败");
        }
        publish_result("类型搜索已写入 " + path.string() + "，扫描 " +
                       std::to_string(scanned) + "，匹配 " +
                       std::to_string(matches));
    } catch (const std::exception& error) {
        publish_result(std::string("类型搜索失败：") + error.what(), false);
    }
}

void LocalBattle::scan_objects(const std::string& text, const std::string& path) {
    try {
        void* klass = runtime_.find(text.c_str(), path.c_str());
        if (!klass) {
            throw std::runtime_error("object-scan 类型不存在");
        }
        std::vector<void*> objects = find_live_objects(runtime_, klass, 64);
        std::ostringstream output;
        output << "type " << runtime_.klass_name(klass) << "\n";
        output << "live objects " << objects.size() << "\n\n";
        for (void* object : objects) {
            il2cpp::Runtime::Root root(runtime_, object);
            output << "object " << object << "\n";
            output << describe_instance_fields(runtime_, object) << "\n\n";
        }
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "battle_object_scan.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("对象扫描结果写入失败");
        }
        publish_result("对象扫描已写入 " + path.string() + "，共 " +
                       std::to_string(objects.size()) + " 个");
    } catch (const std::exception& error) {
        publish_result(std::string("对象扫描失败：") + error.what(), false);
    }
}

void LocalBattle::static_scan(const std::string& text, const std::string& path) {
    try {
        void* target = runtime_.find(text.c_str(), path.c_str());
        if (!target) {
            throw std::runtime_error("static-scan 类型不存在");
        }
        std::ostringstream output;
        output << "target " << runtime_.klass_name(target) << "\n";

        static std::uint64_t heap_calls = 0;
        heap_calls = 0;
        if (runtime_.gc_foreach_heap) {
            runtime_.gc_foreach_heap(
                [](void*, void*) {
                    ++heap_calls;
                },
                nullptr);
        }
        output << "heap calls=" << heap_calls << "\n";

        std::size_t assembly_count = 0;
        void** assemblies =
            runtime_.domain_assemblies(runtime_.domain_get(), &assembly_count);
        std::size_t matches = 0;
        for (std::size_t assembly_index = 0;
             assemblies && assembly_index < assembly_count; ++assembly_index) {
            void* image = runtime_.assembly_image(assemblies[assembly_index]);
            if (!image) {
                continue;
            }
            const std::size_t class_count = runtime_.image_class_count(image);
            for (std::size_t class_index = 0; class_index < class_count;
                 ++class_index) {
                void* klass = runtime_.image_class(image, class_index);
                if (!klass) {
                    continue;
                }
                void* field_iterator = nullptr;
                while (void* field = runtime_.class_fields(klass, &field_iterator)) {
                    if (!(runtime_.field_flags(field) & 0x10)) {
                        continue;
                    }
                    void* field_class = nullptr;
                    try {
                        field_class =
                            runtime_.class_from_type(runtime_.field_type(field));
                    } catch (...) {
                        continue;
                    }
                    if (field_class != target) {
                        continue;
                    }
                    void* value = nullptr;
                    try {
                        runtime_.field_static_get(field, &value);
                    } catch (const std::exception& error) {
                        output << "read failed " << runtime_.klass_name(klass)
                               << "." << runtime_.field_name(field) << ": "
                               << error.what() << "\n";
                        continue;
                    }
                    if (!value) {
                        continue;
                    }
                    ++matches;
                    const char* name_space = runtime_.class_namespace(klass);
                    output << "holder "
                           << (name_space ? name_space : "") << "."
                           << runtime_.klass_name(klass) << "."
                           << runtime_.field_name(field) << "=" << value;
                    try {
                        output << " class="
                               << runtime_.klass_name(runtime_.object_class(value));
                    } catch (const std::exception& error) {
                        output << " class_error=" << error.what();
                    }
                    output << "\n";
                }
            }
        }
        output << "matches " << matches << "\n";
        std::string file_name = "battle_static_scan_" + text + "_" +
                                path;
        std::replace(file_name.begin(), file_name.end(), '/', '_');
        std::replace(file_name.begin(), file_name.end(), '.', '_');
        file_name += ".txt";
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / file_name;
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("静态扫描结果写入失败");
        }
        publish_result("静态扫描已写入 " + path.string() + "，匹配 " +
                       std::to_string(matches));
    } catch (const std::exception& error) {
        publish_result(std::string("静态扫描失败：") + error.what(), false);
    }
}

void LocalBattle::ref_scan(const std::string& text, const std::string& path) {
    try {
        void* target = runtime_.find(text.c_str(), path.c_str());
        if (!target) {
            throw std::runtime_error("ref-scan 类型不存在");
        }
        struct Context {
            il2cpp::Runtime* runtime = nullptr;
            void* target = nullptr;
            std::uint64_t handles = 0;
            std::vector<void*> direct;
            std::vector<std::tuple<void*, std::string, void*>> holders;
        };
        static Context* current = nullptr;
        Context context;
        context.runtime = &runtime_;
        context.target = target;
        current = &context;
        if (runtime_.gchandle_foreach_target) {
            runtime_.gchandle_foreach_target(
                [](void* object, void*) {
                    Context* ctx = current;
                    if (!ctx) {
                        return;
                    }
                    ++ctx->handles;
                    if (!object) {
                        return;
                    }
                    try {
                        void* klass = ctx->runtime->object_class(object);
                        if (!klass) {
                            return;
                        }
                        if (klass == ctx->target) {
                            if (ctx->direct.size() < 32) {
                                ctx->direct.push_back(object);
                            }
                            return;
                        }
                        void* field_iterator = nullptr;
                        while (void* field =
                                   ctx->runtime->class_fields(klass,
                                                              &field_iterator)) {
                            if (ctx->runtime->field_flags(field) & 0x10) {
                                continue;
                            }
                            void* field_class = nullptr;
                            try {
                                field_class = ctx->runtime->class_from_type(
                                    ctx->runtime->field_type(field));
                            } catch (...) {
                                continue;
                            }
                            if (field_class != ctx->target) {
                                continue;
                            }
                            void* value = nullptr;
                            try {
                                value = ctx->runtime->field_get_object(field,
                                                                       object);
                            } catch (...) {
                                continue;
                            }
                            if (value && ctx->holders.size() < 32) {
                                ctx->holders.emplace_back(
                                    object,
                                    ctx->runtime->field_name(field)
                                        ? ctx->runtime->field_name(field)
                                        : "?",
                                    value);
                            }
                        }
                    } catch (...) {
                    }
                },
                &context);
        }
        current = nullptr;

        std::ostringstream output;
        output << "target " << runtime_.klass_name(target) << "\n";
        output << "gchandles " << context.handles << "\n";
        output << "direct " << context.direct.size() << "\n";
        for (void* object : context.direct) {
            output << "  object " << object << " "
                   << runtime_.klass_name(runtime_.object_class(object)) << "\n";
        }
        output << "holder fields " << context.holders.size() << "\n";
        for (const auto& [holder, field, value] : context.holders) {
            const char* name_space =
                runtime_.class_namespace(runtime_.object_class(holder));
            output << "  " << (name_space ? name_space : "") << "."
                   << runtime_.klass_name(runtime_.object_class(holder)) << "."
                   << field << " -> " << value << "\n";
        }
        std::string file_name = "battle_ref_scan_" + text + "_" + path;
        std::replace(file_name.begin(), file_name.end(), '/', '_');
        std::replace(file_name.begin(), file_name.end(), '.', '_');
        file_name += ".txt";
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / file_name;
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("引用扫描结果写入失败");
        }
        publish_result("引用扫描已写入 " + path.string() + "，直接 " +
                       std::to_string(context.direct.size()) + "，持有 " +
                       std::to_string(context.holders.size()));
    } catch (const std::exception& error) {
        publish_result(std::string("引用扫描失败：") + error.what(), false);
    }
}

void LocalBattle::root_bfs(const std::string& text, const std::string& path, int value) {
    try {
        const auto resolve_type = [&](const std::string& name_space,
                                      std::string name) -> void* {
            try {
                return runtime_.find(name_space.c_str(), name.c_str());
            } catch (...) {
            }
            const std::size_t separator = name.find('/');
            if (separator == std::string::npos) {
                return nullptr;
            }
            void* klass =
                runtime_.find(name_space.c_str(), name.substr(0, separator).c_str());
            std::size_t begin = separator + 1;
            while (klass && begin <= name.size()) {
                const std::size_t end = name.find('/', begin);
                const std::string part =
                    name.substr(begin, end == std::string::npos ? std::string::npos
                                                                : end - begin);
                klass = runtime_.nested(klass, part.c_str());
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1;
            }
            return klass;
        };
        void* target = resolve_type(text, path);
        if (!target) {
            throw std::runtime_error("root-bfs 类型不存在");
        }
        struct Node {
            void* object = nullptr;
            int parent = -1;
            std::string field;
        };
        std::vector<Node> nodes;
        std::unordered_set<void*> visited;
        const int max_depth = std::clamp(value, 1, 4);
        constexpr std::size_t kMaxNodes = 300000;

        static std::vector<void*>* roots = nullptr;
        static std::size_t root_limit = 0;
        roots = new std::vector<void*>();
        root_limit = 60000;
        if (runtime_.gchandle_foreach_target) {
            runtime_.gchandle_foreach_target(
                [](void* object, void*) {
                    if (object && roots->size() < root_limit) {
                        roots->push_back(object);
                    }
                },
                nullptr);
        }
        for (void* root : *roots) {
            if (visited.insert(root).second) {
                nodes.push_back(Node{root, -1, "root"});
            }
        }
        delete roots;
        roots = nullptr;

        std::vector<std::pair<std::size_t, void*>> matches;
        for (std::size_t index = 0;
             index < nodes.size() && nodes.size() < kMaxNodes; ++index) {
            void* object = nodes[index].object;
            void* klass = nullptr;
            try {
                klass = runtime_.object_class(object);
            } catch (...) {
                continue;
            }
            if (!klass) {
                continue;
            }
            if (klass == target) {
                matches.emplace_back(index, object);
                if (matches.size() >= 8) {
                    break;
                }
                continue;
            }
            const int depth = [&](std::size_t at) {
                int value = 0;
                while (at < nodes.size() && nodes[at].parent >= 0) {
                    ++value;
                    at = static_cast<std::size_t>(nodes[at].parent);
                }
                return value;
            }(index);
            if (depth >= max_depth) {
                continue;
            }
            if (runtime_.class_rank(klass) > 0 || runtime_.class_is_value(klass)) {
                continue;
            }
            if (!std::strcmp(runtime_.class_namespace(klass), "System") &&
                !std::strcmp(runtime_.class_name(klass), "String")) {
                continue;
            }
            void* field_iterator = nullptr;
            int fields = 0;
            while (void* field = runtime_.class_fields(klass, &field_iterator)) {
                if (++fields > 96 || nodes.size() >= kMaxNodes) {
                    break;
                }
                if (runtime_.field_flags(field) & 0x10) {
                    continue;
                }
                void* field_class = nullptr;
                try {
                    field_class =
                        runtime_.class_from_type(runtime_.field_type(field));
                } catch (...) {
                    continue;
                }
                if (!field_class || runtime_.class_is_value(field_class) ||
                    runtime_.class_rank(field_class) > 0) {
                    continue;
                }
                void* value = nullptr;
                try {
                    value = runtime_.field_get_object(field, object);
                } catch (...) {
                    continue;
                }
                if (!value || !visited.insert(value).second) {
                    continue;
                }
                nodes.push_back(Node{
                    value, static_cast<int>(index),
                    runtime_.field_name(field) ? runtime_.field_name(field) : "?"});
            }
        }

        std::ostringstream output;
        output << "target " << runtime_.klass_name(target)
               << " depth=" << max_depth << "\n";
        output << "nodes " << nodes.size() << " matches " << matches.size()
               << "\n";
        for (const auto& [index, object] : matches) {
            std::vector<std::string> path;
            std::size_t at = index;
            while (at < nodes.size()) {
                path.push_back(nodes[at].field);
                if (nodes[at].parent < 0) {
                    break;
                }
                at = static_cast<std::size_t>(nodes[at].parent);
            }
            output << "match " << object << " path=";
            for (auto iterator = path.rbegin(); iterator != path.rend(); ++iterator) {
                output << *iterator;
                if (iterator + 1 != path.rend()) {
                    output << " -> ";
                }
            }
            output << "\n";
        }
        std::string file_name = "battle_root_bfs_" + text + "_" + path;
        std::replace(file_name.begin(), file_name.end(), '/', '_');
        std::replace(file_name.begin(), file_name.end(), '.', '_');
        file_name += ".txt";
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / file_name;
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("根遍历结果写入失败");
        }
        publish_result("根遍历已写入 " + path.string() + "，节点 " +
                       std::to_string(nodes.size()) + "，匹配 " +
                       std::to_string(matches.size()));
    } catch (const std::exception& error) {
        publish_result(std::string("根遍历失败：") + error.what(), false);
    }
}

void LocalBattle::method_at(const std::string& text) {
    try {
        HMODULE module = GetModuleHandleW(L"GameAssembly.dll");
        if (!module) {
            throw std::runtime_error("GameAssembly.dll 未加载");
        }
        const std::uint64_t base =
            reinterpret_cast<std::uint64_t>(module);
        std::vector<std::pair<std::uint64_t, std::string>> targets;
        std::size_t begin = 0;
        while (begin <= text.size()) {
            const std::size_t end = text.find(',', begin);
            const std::string item = text.substr(
                begin, end == std::string::npos ? std::string::npos : end - begin);
            if (end == std::string::npos) {
                begin = text.size() + 1;
            } else {
                begin = end + 1;
            }
            if (item.empty()) {
                continue;
            }
            const std::uint64_t rva =
                std::strtoull(item.c_str(), nullptr, 0);
            targets.emplace_back(base + rva, item);
        }
        std::ostringstream output;
        for (const auto& [address, text] : targets) {
            output << "target " << text << " -> 0x" << std::hex << address
                   << std::dec << "\n";
        }
        std::size_t matches = 0;
        std::size_t assembly_count = 0;
        void** assemblies =
            runtime_.domain_assemblies(runtime_.domain_get(), &assembly_count);
        for (std::size_t assembly_index = 0;
             assemblies && assembly_index < assembly_count; ++assembly_index) {
            void* image = runtime_.assembly_image(assemblies[assembly_index]);
            if (!image) {
                continue;
            }
            const std::size_t class_count = runtime_.image_class_count(image);
            for (std::size_t class_index = 0; class_index < class_count;
                 ++class_index) {
                void* klass = runtime_.image_class(image, class_index);
                if (!klass) {
                    continue;
                }
                void* method_iterator = nullptr;
                while (void* method = runtime_.class_methods(klass, &method_iterator)) {
                    const std::uint64_t entry = reinterpret_cast<std::uint64_t>(
                        *static_cast<void**>(method));
                    for (const auto& [address, text] : targets) {
                        if (entry != address) {
                            continue;
                        }
                        ++matches;
                        const char* name_space = runtime_.class_namespace(klass);
                        output << "match " << text << " "
                               << (name_space ? name_space : "") << "."
                               << runtime_.klass_name(klass) << "."
                               << (runtime_.method_name(method)
                                       ? runtime_.method_name(method)
                                       : "?")
                               << "(";
                        for (std::uint32_t index = 0;
                             index < runtime_.method_count(method); ++index) {
                            if (index) {
                                output << ", ";
                            }
                            try {
                                output << runtime_.type(
                                    runtime_.method_param(method, index));
                            } catch (...) {
                                output << "?";
                            }
                        }
                        output << ")\n";
                    }
                }
            }
        }
        output << "matches " << matches << "\n";
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / "battle_method_at.txt";
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        publish_result("方法地址解析已写入 " + path.string() + "，匹配 " +
                       std::to_string(matches));
    } catch (const std::exception& error) {
        publish_result(std::string("方法地址解析失败：") + error.what(), false);
    }
}

void LocalBattle::singleton_scan(const std::string& text, const std::string& path) {
    try {
        void* target = runtime_.find(text.c_str(), path.c_str());
        if (!target) {
            throw std::runtime_error("singleton-scan 类型不存在");
        }
        std::ostringstream output;
        output << "target " << runtime_.klass_name(target) << "\n";
        std::size_t roots = 0;
        std::size_t matches = 0;
        std::size_t assembly_count = 0;
        void** assemblies =
            runtime_.domain_assemblies(runtime_.domain_get(), &assembly_count);
        for (std::size_t assembly_index = 0;
             assemblies && assembly_index < assembly_count; ++assembly_index) {
            void* image = runtime_.assembly_image(assemblies[assembly_index]);
            if (!image) {
                continue;
            }
            const std::size_t class_count = runtime_.image_class_count(image);
            for (std::size_t class_index = 0; class_index < class_count;
                 ++class_index) {
                void* klass = runtime_.image_class(image, class_index);
                if (!klass) {
                    continue;
                }
                void* field_iterator = nullptr;
                while (void* field = runtime_.class_fields(klass, &field_iterator)) {
                    if (!(runtime_.field_flags(field) & 0x10)) {
                        continue;
                    }
                    void* field_class = nullptr;
                    try {
                        field_class =
                            runtime_.class_from_type(runtime_.field_type(field));
                    } catch (...) {
                        continue;
                    }
                    if (!field_class || runtime_.class_is_value(field_class)) {
                        continue;
                    }
                    void* instance = nullptr;
                    try {
                        runtime_.field_static_get(field, &instance);
                    } catch (...) {
                        continue;
                    }
                    if (!instance) {
                        continue;
                    }
                    ++roots;
                    void* instance_class = nullptr;
                    try {
                        instance_class = runtime_.object_class(instance);
                    } catch (...) {
                        continue;
                    }
                    const char* name_space = runtime_.class_namespace(klass);
                    const std::string root_name =
                        std::string(name_space ? name_space : "") + "." +
                        runtime_.klass_name(klass) + "." +
                        (runtime_.field_name(field) ? runtime_.field_name(field) : "?");
                    if (instance_class == target) {
                        ++matches;
                        output << "direct " << root_name << " -> " << instance
                               << "\n";
                        continue;
                    }
                    void* member_iterator = nullptr;
                    while (void* member =
                               runtime_.class_fields(instance_class, &member_iterator)) {
                        if (runtime_.field_flags(member) & 0x10) {
                            continue;
                        }
                        void* member_class = nullptr;
                        try {
                            member_class = runtime_.class_from_type(
                                runtime_.field_type(member));
                        } catch (...) {
                            continue;
                        }
                        if (member_class != target) {
                            continue;
                        }
                        void* value = nullptr;
                        try {
                            value = runtime_.field_get_object(member, instance);
                        } catch (...) {
                            continue;
                        }
                        if (!value) {
                            continue;
                        }
                        ++matches;
                        output << "holder " << root_name << " -> "
                               << runtime_.klass_name(instance_class) << "."
                               << (runtime_.field_name(member)
                                       ? runtime_.field_name(member)
                                       : "?")
                               << " -> " << value << "\n";
                    }
                }
            }
        }
        output << "static roots " << roots << " matches " << matches << "\n";
        std::string file_name =
            "battle_static_ref_scan_" + text + "_" + path;
        std::replace(file_name.begin(), file_name.end(), '/', '_');
        std::replace(file_name.begin(), file_name.end(), '.', '_');
        file_name += ".txt";
        const std::filesystem::path path =
            std::filesystem::path(host_.root_directory) / file_name;
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        file << output.str();
        file.flush();
        if (!file) {
            throw std::runtime_error("单例扫描结果写入失败");
        }
        publish_result("静态引用扫描已写入 " + path.string() + "，根 " +
                       std::to_string(roots) + "，匹配 " +
                       std::to_string(matches));
    } catch (const std::exception& error) {
        publish_result(std::string("静态引用扫描失败：") + error.what(), false);
    }
}

}  // namespace ark::better_battle
