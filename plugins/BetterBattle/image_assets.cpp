#include "battle_helpers.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace ark::better_battle {
namespace {

struct EncodedImage {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int tex_w = 0;
    int tex_h = 0;
};

bool texture_size(il2cpp::Runtime& runtime, void* texture, int* width, int* height) {
    if (!texture || !width || !height) {
        return false;
    }
    void* texture_class = runtime.object_class(texture);
    *width = method_int(runtime, texture, texture_class, "get_width", 0);
    *height = method_int(runtime, texture, texture_class, "get_height", 0);
    return *width > 0 && *height > 0;
}

bool write_file(const std::filesystem::path& path, const unsigned char* bytes,
                int size) {
    if (!bytes || size <= 0) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes), size);
    return static_cast<bool>(output);
}

bool encode_texture_png(il2cpp::Runtime& runtime, void* texture,
                        const std::filesystem::path& path, EncodedImage* image) {
    if (!texture || !image) {
        return false;
    }
    int width = 0;
    int height = 0;
    if (!texture_size(runtime, texture, &width, &height)) {
        return false;
    }
    void* image_conversion = runtime.find("UnityEngine", "ImageConversion");
    void* encode =
        runtime.method(image_conversion, "EncodeToPNG", 1, {"UnityEngine.Texture2D"});
    void* png = runtime.invoke(encode, nullptr, {texture});
    const int size = runtime.count(png);
    if (!png || size <= 0) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(png) + 0x20;
    if (!write_file(path, bytes, size)) {
        return false;
    }
    image->tex_w = width;
    image->tex_h = height;
    if (image->w <= 0 || image->h <= 0) {
        image->x = 0;
        image->y = 0;
        image->w = width;
        image->h = height;
    }
    return true;
}

bool read_texture_pixels_cpu(il2cpp::Runtime& runtime, void* texture,
                             std::vector<unsigned char>* rgba, int* width,
                             int* height) {
    if (!texture || !rgba || !width || !height) {
        return false;
    }
    if (!texture_size(runtime, texture, width, height)) {
        return false;
    }
    void* texture_class = runtime.object_class(texture);
    void* get_pixels = nullptr;
    try {
        get_pixels = runtime.method(texture_class, "GetPixels32", 0);
    } catch (...) {
        return false;
    }
    void* pixels = nullptr;
    try {
        pixels = runtime.invoke(get_pixels, texture);
    } catch (...) {
        return false;
    }
    const int count = runtime.count(pixels);
    const int expected = *width * *height;
    if (!pixels || count != expected) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(pixels) + 0x20;
    rgba->assign(bytes, bytes + static_cast<std::size_t>(expected) * 4);
    return true;
}

bool read_texture_pixels_gpu(il2cpp::Runtime& runtime, void* texture,
                             std::vector<unsigned char>* rgba, int* width,
                             int* height, std::string* error) {
    if (!texture || !rgba || !width || !height) {
        return false;
    }
    if (!texture_size(runtime, texture, width, height)) {
        return false;
    }
    const char* stage = "render-texture";
    try {
        void* render_texture_class = runtime.find("UnityEngine", "RenderTexture");
        void* render_texture = runtime.object_new(render_texture_class);
        if (!render_texture) {
            if (error) {
                *error = "RenderTexture 分配失败";
            }
            return false;
        }
        il2cpp::Runtime::Root render_texture_root(runtime, render_texture);
        void* render_constructor = runtime.method(
            render_texture_class, ".ctor", 4,
            {"System.Int32", "System.Int32", "System.Int32",
             "UnityEngine.RenderTextureFormat"});
        int depth = 0;
        int render_format = 0;
        runtime.invoke(render_constructor, render_texture,
                       {width, height, &depth, &render_format});

        stage = "graphics-blit";
        void* graphics = runtime.find("UnityEngine", "Graphics");
        void* blit = runtime.method(
            graphics, "Blit", 2,
            {"UnityEngine.Texture", "UnityEngine.RenderTexture"});
        runtime.invoke(blit, nullptr, {texture, render_texture});

        stage = "set-active";
        void* set_active = runtime.method(
            render_texture_class, "set_active", 1,
            {"UnityEngine.RenderTexture"});
        runtime.invoke(set_active, nullptr, {render_texture});

        stage = "readable-texture";
        void* texture_class = runtime.find("UnityEngine", "Texture2D");
        void* readable = runtime.object_new(texture_class);
        if (!readable) {
            if (error) {
                *error = "可读 Texture2D 分配失败";
            }
            return false;
        }
        il2cpp::Runtime::Root readable_root(runtime, readable);
        void* texture_constructor = runtime.method(
            texture_class, ".ctor", 4,
            {"System.Int32", "System.Int32", "UnityEngine.TextureFormat",
             "System.Boolean"});
        int texture_format = 4;
        bool mip_chain = false;
        runtime.invoke(texture_constructor, readable,
                       {width, height, &texture_format, &mip_chain});
        stage = "read-pixels";
        void* read_pixels = runtime.method(
            texture_class, "ReadPixels", 4,
            {"UnityEngine.Rect", "System.Int32", "System.Int32",
             "System.Boolean"});
        float rect[4] = {0, 0, static_cast<float>(*width),
                         static_cast<float>(*height)};
        int destination_x = 0;
        int destination_y = 0;
        bool recalculate_mipmaps = false;
        runtime.invoke(read_pixels, readable,
                       {rect, &destination_x, &destination_y,
                        &recalculate_mipmaps});
        void* apply = nullptr;
        try {
            apply = runtime.method(texture_class, "Apply", 1, {"System.Boolean"});
        } catch (...) {
        }
        if (apply) {
            runtime.invoke(apply, readable, {&recalculate_mipmaps});
        } else {
            apply = runtime.method(texture_class, "Apply", 0);
            runtime.invoke(apply, readable);
        }

        stage = "copy-pixels";
        void* get_pixels = runtime.method(texture_class, "GetPixels32", 0);
        void* pixels = runtime.invoke(get_pixels, readable);
        const int count = runtime.count(pixels);
        const int expected = *width * *height;
        if (!pixels || count != expected) {
            if (error) {
                *error = "读回像素数量不匹配";
            }
            return false;
        }
        const auto* bytes =
            reinterpret_cast<const unsigned char*>(pixels) + 0x20;
        rgba->assign(bytes, bytes + static_cast<std::size_t>(expected) * 4);
        try {
            runtime.invoke(set_active, nullptr, {nullptr});
        } catch (...) {
        }
        try {
            void* release = runtime.method(render_texture_class, "Release", 0);
            runtime.invoke(release, render_texture);
        } catch (...) {
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            *error = std::string(stage) + ": " + exception.what();
        }
    }
    if (error && error->empty()) {
        *error = "GPU 读回未产生有效像素";
    }
    return false;
}

bool read_texture_pixels(il2cpp::Runtime& runtime, void* texture,
                         std::vector<unsigned char>* rgba, int* width, int* height,
                         std::string* error) {
    if (read_texture_pixels_cpu(runtime, texture, rgba, width, height)) {
        return true;
    }
    return read_texture_pixels_gpu(runtime, texture, rgba, width, height, error);
}

bool encode_rgba_png(il2cpp::Runtime& runtime,
                     const std::vector<unsigned char>& rgba, int width, int height,
                     const std::filesystem::path& path, EncodedImage* image) {
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4) {
        return false;
    }
    void* texture_class = runtime.find("UnityEngine", "Texture2D");
    void* constructor = runtime.method(
        texture_class, ".ctor", 4,
        {"System.Int32", "System.Int32", "UnityEngine.TextureFormat",
         "System.Boolean"});
    void* texture = runtime.object_new(texture_class);
    if (!texture) {
        return false;
    }
    il2cpp::Runtime::Root texture_root(runtime, texture);
    int format = 4;
    bool mip_chain = false;
    runtime.invoke(constructor, texture, {&width, &height, &format, &mip_chain});
    void* color32 = runtime.find("UnityEngine", "Color32");
    void* pixels = runtime.array_new(color32, static_cast<std::uintptr_t>(width) *
                                                  static_cast<std::uintptr_t>(height));
    if (!pixels) {
        return false;
    }
    il2cpp::Runtime::Root pixels_root(runtime, pixels);
    std::memcpy(reinterpret_cast<unsigned char*>(pixels) + 0x20, rgba.data(),
                rgba.size());
    void* set_pixels = nullptr;
    try {
        set_pixels = runtime.method(texture_class, "SetPixels32", 1,
                                    {"UnityEngine.Color32[]"});
    } catch (...) {
        set_pixels = runtime.method(texture_class, "SetPixels32", 1);
    }
    runtime.invoke(set_pixels, texture, {pixels});
    void* apply = nullptr;
    try {
        apply = runtime.method(texture_class, "Apply", 1, {"System.Boolean"});
    } catch (...) {
    }
    if (apply) {
        bool update_mipmaps = false;
        runtime.invoke(apply, texture, {&update_mipmaps});
    } else {
        apply = runtime.method(texture_class, "Apply", 0);
        runtime.invoke(apply, texture);
    }
    return encode_texture_png(runtime, texture, path, image);
}

bool export_texture_region(il2cpp::Runtime& runtime, void* main_texture,
                           void* alpha_texture, int x, int y, int w, int h,
                           const std::filesystem::path& path, EncodedImage* image) {
    int texture_width = 0;
    int texture_height = 0;
    if (!texture_size(runtime, main_texture, &texture_width, &texture_height)) {
        return false;
    }
    if (w <= 0 || h <= 0) {
        x = 0;
        y = 0;
        w = texture_width;
        h = texture_height;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > texture_width) {
        w = texture_width - x;
    }
    if (y + h > texture_height) {
        h = texture_height - y;
    }
    if (w <= 0 || h <= 0) {
        x = 0;
        y = 0;
        w = texture_width;
        h = texture_height;
    }

    std::vector<unsigned char> main_pixels;
    int main_width = 0;
    int main_height = 0;
    std::string pixel_error;
    const bool main_readable =
        read_texture_pixels(runtime, main_texture, &main_pixels, &main_width,
                            &main_height, &pixel_error);
    if (main_readable && main_width == texture_width &&
        main_height == texture_height) {
        std::vector<unsigned char> alpha_pixels;
        int alpha_width = 0;
        int alpha_height = 0;
        std::string alpha_error;
        const bool has_alpha =
            alpha_texture &&
            read_texture_pixels(runtime, alpha_texture, &alpha_pixels, &alpha_width,
                                &alpha_height, &alpha_error) &&
            alpha_width == main_width && alpha_height == main_height;
        std::vector<unsigned char> output(static_cast<std::size_t>(w) * h * 4);
        for (int row = 0; row < h; ++row) {
            const std::size_t source_offset =
                (static_cast<std::size_t>(y + row) * main_width + x) * 4;
            const std::size_t output_offset = static_cast<std::size_t>(row) * w * 4;
            std::memcpy(output.data() + output_offset,
                        main_pixels.data() + source_offset,
                        static_cast<std::size_t>(w) * 4);
            if (has_alpha) {
                for (int column = 0; column < w; ++column) {
                    output[output_offset + static_cast<std::size_t>(column) * 4 + 3] =
                        alpha_pixels[source_offset +
                                     static_cast<std::size_t>(column) * 4 + 3];
                }
            }
        }
        try {
            if (encode_rgba_png(runtime, output, w, h, path, image)) {
                image->x = 0;
                image->y = 0;
                image->w = w;
                image->h = h;
                image->tex_w = texture_width;
                image->tex_h = texture_height;
                return true;
            }
            pixel_error = "RGBA PNG 编码返回空";
        } catch (const std::exception& exception) {
            pixel_error = std::string("RGBA PNG 编码失败: ") + exception.what();
        }
    } else if (pixel_error.empty()) {
        pixel_error = "纹理尺寸不匹配";
    }

    try {
        if (encode_texture_png(runtime, main_texture, path, image)) {
            image->x = x;
            image->y = y;
            image->w = w;
            image->h = h;
            image->tex_w = texture_width;
            image->tex_h = texture_height;
            return true;
        }
    } catch (const std::exception& exception) {
        throw std::runtime_error(std::string("纹理不可读（") + pixel_error +
                                 "），PNG 编码失败：" + exception.what());
    }
    throw std::runtime_error(std::string("纹理不可读（") + pixel_error +
                             "），PNG 编码返回空");
}

bool export_sprite(il2cpp::Runtime& runtime, void* sprite,
                   const std::filesystem::path& path, EncodedImage* image) {
    if (!sprite || !image) {
        return false;
    }
    void* texture = runtime.call(sprite, "get_texture");
    if (!texture) {
        return false;
    }
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    try {
        void* rect = runtime.call(sprite, "get_textureRect");
        const void* base = rect ? runtime.object_unbox(rect) : nullptr;
        if (base) {
            float fx = 0;
            float fy = 0;
            float fw = 0;
            float fh = 0;
            std::memcpy(&fx, static_cast<const unsigned char*>(base) + 0, 4);
            std::memcpy(&fy, static_cast<const unsigned char*>(base) + 4, 4);
            std::memcpy(&fw, static_cast<const unsigned char*>(base) + 8, 4);
            std::memcpy(&fh, static_cast<const unsigned char*>(base) + 12, 4);
            x = static_cast<int>(std::lround(fx));
            y = static_cast<int>(std::lround(fy));
            w = static_cast<int>(std::lround(fw));
            h = static_cast<int>(std::lround(fh));
        }
    } catch (...) {
    }
    return export_texture_region(runtime, texture, nullptr, x, y, w, h, path, image);
}

bool export_render_data(il2cpp::Runtime& runtime, void* render_data,
                        const std::filesystem::path& path, EncodedImage* image) {
    if (!render_data || !image) {
        return false;
    }
    void* texture = runtime.get_object(render_data, "mainTex");
    if (!texture) {
        return false;
    }
    void* alpha_texture = nullptr;
    try {
        alpha_texture = runtime.get_object(render_data, "alphaTex");
    } catch (...) {
    }
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    try {
        void* rect = runtime.get_object(render_data, "rect");
        if (rect) {
            x = runtime.get<int>(rect, "x");
            y = runtime.get<int>(rect, "y");
            w = runtime.get<int>(rect, "w");
            h = runtime.get<int>(rect, "h");
        }
    } catch (...) {
    }
    return export_texture_region(runtime, texture, alpha_texture, x, y, w, h, path,
                                 image);
}

void* find_component(il2cpp::Runtime& runtime, void* unity_object_class,
                     const char* name_space, const char* name) {
    void* klass = runtime.find(name_space, name);
    void* find_one =
        runtime.method(unity_object_class, "FindObjectOfType", 1, {"System.Type"});
    void* type = runtime.type_object(runtime.class_type(klass));
    il2cpp::Runtime::Root type_root(runtime, type);
    return runtime.invoke(find_one, nullptr, {type});
}

void* load_string_object(il2cpp::Runtime& runtime, void* klass, const char* name,
                         const std::string& id) {
    if (!klass || id.empty()) {
        return nullptr;
    }
    void* method = nullptr;
    try {
        method = runtime.method(klass, name, 1, {"System.String"});
    } catch (...) {
        return nullptr;
    }
    if (!method) {
        return nullptr;
    }
    void* text = runtime.string_new(id.c_str());
    il2cpp::Runtime::Root text_root(runtime, text);
    return runtime.invoke(method, nullptr, {text});
}

} // namespace

void LocalBattle::export_asset(const std::string& kind, const std::string& id) {
    try {
        if (kind.empty() || id.empty()) {
            throw std::runtime_error(
                "asset 需要 <avatar|portrait|skill|enemy-icon> <id>");
        }
        const std::string token = sanitize_cache_token(id);
        const std::string file_name = kind + "_" + token + ".png";
        const std::filesystem::path cache_dir =
            std::filesystem::path(host_.root_directory) / "BetterBattleWeb" /
            "art_cache";
        std::error_code ignored;
        std::filesystem::create_directories(cache_dir, ignored);
        const std::filesystem::path path = cache_dir / file_name;

        EncodedImage image;
        bool ok = false;
        std::string source;

        const auto find_battle_data = [&](const std::string& operator_id) -> void* {
            for (const il2cpp::Runtime::Root& root : card_roots_) {
                void* card = root.get();
                if (!card) {
                    continue;
                }
                void* data = nullptr;
                if (!read_field_deep(runtime_, card, classes_.deck_card, "m_data",
                                     &data) ||
                    !data) {
                    continue;
                }
                void* data_class = runtime_.object_class(data);
                if (read_string_field(runtime_, data, data_class, "prefabKey") ==
                    operator_id) {
                    return data;
                }
            }
            return nullptr;
        };

        const auto try_battle_art = [&](const std::string& operator_id) -> bool {
            void* data = find_battle_data(operator_id);
            if (!data) {
                return false;
            }
            il2cpp::Runtime::Root data_root(runtime_, data);
            void* art_collection = find_component(runtime_, classes_.unity_object,
                                                  "Torappu.Battle", "ArtCollection");
            if (!art_collection) {
                return false;
            }
            il2cpp::Runtime::Root art_root(runtime_, art_collection);
            void* art_class = runtime_.object_class(art_collection);
            if (kind == "avatar") {
                void* getter = nullptr;
                try {
                    getter = runtime_.method(
                        art_class, "GetAvatar", 1,
                        {"Torappu.Battle.BattleCharacterData"});
                } catch (...) {
                }
                if (!getter) {
                    return false;
                }
                void* sprite = runtime_.invoke(getter, art_collection, {data});
                if (!sprite) {
                    return false;
                }
                il2cpp::Runtime::Root sprite_root(runtime_, sprite);
                source = "ArtCollection.GetAvatar";
                return export_sprite(runtime_, sprite, path, &image);
            }
            if (kind == "illust") {
                void* getter = nullptr;
                try {
                    getter = runtime_.method(
                        art_class, "GetIllust", 1,
                        {"Torappu.Battle.BattleCharacterData"});
                } catch (...) {
                }
                if (!getter) {
                    return false;
                }
                void* panel = runtime_.invoke(getter, art_collection, {data});
                if (!panel) {
                    return false;
                }
                il2cpp::Runtime::Root panel_root(runtime_, panel);
                void* sprite = runtime_.call(panel, "get_sprite");
                if (!sprite) {
                    return false;
                }
                il2cpp::Runtime::Root sprite_root(runtime_, sprite);
                source = "ArtCollection.GetIllust";
                return export_sprite(runtime_, sprite, path, &image);
            }
            return false;
        };

        const auto try_battle_skill = [&](const std::string& skill_id) -> bool {
            void* art_collection = find_component(runtime_, classes_.unity_object,
                                                  "Torappu.Battle", "ArtCollection");
            if (!art_collection) {
                return false;
            }
            il2cpp::Runtime::Root art_root(runtime_, art_collection);
            void* art_class = runtime_.object_class(art_collection);
            void* getter = nullptr;
            try {
                getter = runtime_.method(art_class, "GetSkillIcon", 1,
                                         {"Torappu.ISkillData"});
            } catch (...) {
                return false;
            }
            for (const il2cpp::Runtime::Root& root : card_roots_) {
                void* card = root.get();
                if (!card) {
                    continue;
                }
                void* data = nullptr;
                if (!read_field_deep(runtime_, card, classes_.deck_card, "m_data",
                                     &data) ||
                    !data) {
                    continue;
                }
                void* data_class = runtime_.object_class(data);
                void* skill = nullptr;
                if (!read_field_deep(runtime_, data, data_class, "mainSkill",
                                     &skill) ||
                    !skill) {
                    continue;
                }
                il2cpp::Runtime::Root skill_root(runtime_, skill);
                void* skill_class = runtime_.object_class(skill);
                const std::string current =
                    read_string_field(runtime_, skill, skill_class, "skillId");
                if (!current.empty() && current != skill_id) {
                    continue;
                }
                void* sprite =
                    runtime_.invoke(getter, art_collection, {skill});
                if (!sprite) {
                    continue;
                }
                il2cpp::Runtime::Root sprite_root(runtime_, sprite);
                source = "ArtCollection.GetSkillIcon";
                return export_sprite(runtime_, sprite, path, &image);
            }
            return false;
        };

        if (kind == "enemy-icon") {
            try {
                void* convert = runtime_.find("Torappu", "DataConvertUtil");
                void* sprite = load_string_object(
                    runtime_, convert, "LoadEnemyIcon", id);
                if (sprite) {
                    il2cpp::Runtime::Root sprite_root(runtime_, sprite);
                    ok = export_sprite(runtime_, sprite, path, &image);
                    source = "DataConvertUtil.LoadEnemyIcon";
                }
            } catch (...) {
            }
            if (!ok) {
                try {
                    void* art_collection = find_component(
                        runtime_, classes_.unity_object, "Torappu.Battle",
                        "ArtCollection");
                    if (art_collection) {
                        il2cpp::Runtime::Root art_root(runtime_, art_collection);
                        void* getter = runtime_.method(
                            runtime_.object_class(art_collection),
                            "GetEnemyIcon", 1, {"System.String"});
                        void* text = runtime_.string_new(id.c_str());
                        il2cpp::Runtime::Root text_root(runtime_, text);
                        void* sprite =
                            runtime_.invoke(getter, art_collection, {text});
                        if (sprite) {
                            il2cpp::Runtime::Root sprite_root(runtime_, sprite);
                            ok = export_sprite(runtime_, sprite, path, &image);
                            source = "ArtCollection.GetEnemyIcon";
                        }
                    }
                } catch (...) {
                }
            }
        } else if (kind == "avatar") {
            ok = try_battle_art(id);
            if (!ok) {
                try {
                    void* convert = runtime_.find("Torappu", "DataConvertUtil");
                    void* sprite = load_string_object(
                        runtime_, convert, "LoadPlayerCharAvatar", id);
                    if (sprite) {
                        il2cpp::Runtime::Root sprite_root(runtime_, sprite);
                        ok = export_sprite(runtime_, sprite, path, &image);
                        source = "DataConvertUtil.LoadPlayerCharAvatar";
                    }
                } catch (...) {
                }
            }
            if (!ok) {
                try {
                    void* sprite = load_string_object(
                        runtime_, classes_.character_util, "LoadCharAvatar", id);
                    if (sprite) {
                        il2cpp::Runtime::Root sprite_root(runtime_, sprite);
                        ok = export_sprite(runtime_, sprite, path, &image);
                        source = "CharacterUtil.LoadCharAvatar";
                    }
                } catch (...) {
                }
            }
        } else if (kind == "portrait" || kind == "illust") {
            if (kind == "illust") {
                ok = try_battle_art(id);
            } else {
                try {
                    void* art_collection = find_component(
                        runtime_, classes_.unity_object, "Torappu.Battle",
                        "ArtCollection");
                    if (art_collection) {
                        il2cpp::Runtime::Root art_root(runtime_, art_collection);
                        void* loader =
                            runtime_.call(art_collection, "get_sharedAssetLoader");
                        if (loader) {
                            il2cpp::Runtime::Root loader_root(runtime_, loader);
                            void* load_portrait = runtime_.method(
                                classes_.character_util, "LoadCharPortrait", 2,
                                {"System.String", "Torappu.UI.ILoadAsset"});
                            for (const std::string& candidate :
                                 {id + "_2", id + "#2", "illust_" + id + "_2",
                                  id + "_1", id}) {
                                void* text = runtime_.string_new(candidate.c_str());
                                il2cpp::Runtime::Root text_root(runtime_, text);
                                void* render = runtime_.invoke(
                                    load_portrait, nullptr, {text, loader});
                                if (!render) {
                                    continue;
                                }
                                il2cpp::Runtime::Root render_root(runtime_, render);
                                if (export_render_data(runtime_, render, path,
                                                       &image)) {
                                    ok = true;
                                    source =
                                        "CharacterUtil.LoadCharPortrait(ILoadAsset)";
                                    break;
                                }
                            }
                        }
                        if (!ok) {
                            try {
                                void* load_portrait = runtime_.method(
                                    runtime_.object_class(art_collection),
                                    "LoadCharPortrait", 1, {"System.String"});
                                void* text =
                                    runtime_.string_new((id + "_2").c_str());
                                il2cpp::Runtime::Root text_root(runtime_, text);
                                void* render = runtime_.invoke(
                                    load_portrait, art_collection, {text});
                                if (render) {
                                    il2cpp::Runtime::Root render_root(runtime_,
                                                                      render);
                                    if (export_render_data(runtime_, render, path,
                                                           &image)) {
                                        ok = true;
                                        source = "ArtCollection.LoadCharPortrait";
                                    }
                                }
                            } catch (...) {
                            }
                        }
                    }
                } catch (...) {
                }
                try {
                    void* char_query_class =
                        runtime_.find("Torappu", "CharQuery");
                    void* simple_char = runtime_.method(
                        char_query_class, "SimpleChar", 1, {"System.String"});
                    void* text = runtime_.string_new(id.c_str());
                    il2cpp::Runtime::Root text_root(runtime_, text);
                    void* query =
                        runtime_.invoke(simple_char, nullptr, {text});
                    if (query) {
                        il2cpp::Runtime::Root query_root(runtime_, query);
                        void* query_data = runtime_.object_unbox(query);
                        void* getter = runtime_.method(
                            classes_.character_util, "GetPortraitSprite", 2,
                            {"Torappu.CharQuery", "Torappu.EvolvePhase"});
                        for (int phase : {2, 1, 0}) {
                            void* render = runtime_.invoke(
                                getter, nullptr, {query_data, &phase});
                            if (!render) {
                                continue;
                            }
                            il2cpp::Runtime::Root render_root(runtime_, render);
                            if (export_render_data(runtime_, render, path, &image)) {
                                ok = true;
                                source = "CharacterUtil.GetPortraitSprite(CharQuery)";
                                break;
                            }
                        }
                    }
                } catch (...) {
                }
                for (const std::string& candidate : {id, id + "_2", id + "_1"}) {
                    if (candidate.empty()) {
                        continue;
                    }
                    if (ok) {
                        break;
                    }
                    for (const char* method_name :
                         {"LoadCharPortrait", "GetPortraitSprite"}) {
                        try {
                            void* render = load_string_object(
                                runtime_, classes_.character_util, method_name,
                                candidate);
                            if (!render) {
                                continue;
                            }
                            il2cpp::Runtime::Root render_root(runtime_, render);
                            if (export_render_data(runtime_, render, path, &image)) {
                                ok = true;
                                source = std::string("CharacterUtil.") + method_name;
                                break;
                            }
                        } catch (...) {
                        }
                    }
                    if (ok) {
                        break;
                    }
                }
            }
        } else if (kind == "skill") {
            ok = try_battle_skill(id);
            if (!ok) {
                void* convert = runtime_.find("Torappu", "DataConvertUtil");
                for (const char* method_name :
                     {"LoadSkillIcon", "LoadSkillIconByIconId"}) {
                    try {
                        void* sprite =
                            load_string_object(runtime_, convert, method_name, id);
                        if (!sprite) {
                            continue;
                        }
                        il2cpp::Runtime::Root sprite_root(runtime_, sprite);
                        if (export_sprite(runtime_, sprite, path, &image)) {
                            ok = true;
                            source = std::string("DataConvertUtil.") + method_name;
                            break;
                        }
                    } catch (...) {
                    }
                }
            }
        } else {
            throw std::runtime_error("未知资源类型: " + kind);
        }

        if (!ok) {
            throw std::runtime_error("未取到可读资源（" + source + "）");
        }

        std::ostringstream sidecar;
        sidecar << "{\"kind\":\"" << kind << "\",\"id\":\"" << id
                << "\",\"file\":\"" << file_name << "\",\"x\":" << image.x
                << ",\"y\":" << image.y << ",\"w\":" << image.w
                << ",\"h\":" << image.h << ",\"tex_w\":" << image.tex_w
                << ",\"tex_h\":" << image.tex_h << ",\"source\":\"" << source
                << "\"}";
        {
            std::ofstream meta(cache_dir / (kind + "_" + token + ".json"),
                               std::ios::binary | std::ios::trunc);
            const std::string text = sidecar.str();
            meta.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        publish_result("资源已导出 " + kind + " " + id + " -> " + path.string() +
                       " (" + std::to_string(image.w) + "x" +
                       std::to_string(image.h) + ", tex=" +
                       std::to_string(image.tex_w) + "x" +
                       std::to_string(image.tex_h) + ", source=" + source + ")");
    } catch (const std::exception& error) {
        publish_result(std::string("导出资源失败：") + error.what(), false);
    }
}

} // namespace ark::better_battle
