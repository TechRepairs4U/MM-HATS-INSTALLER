#pragma once
#include "../../shared/install_protection.h"
#include <yyjson.h>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <cstring>

namespace sphaira::ui::menu::hats {

inline constexpr const char* HATS_EXCLUSIONS_URL = "https://raw.githubusercontent.com/TechRepairs4U/HATS/main/hats-exclusions.json";
inline constexpr size_t HATS_EXCLUSIONS_MAX_JSON = 256 * 1024;

// Replace the previous list atomically; malformed/oversized documents never
// produce a partial list. Sysmodule permissions remain separate and exclusions
// always take priority over them.
inline bool ParseGithubExclusions(mm_contents_policy& policy, const char* data, size_t size) {
    if (!data || !size || size > HATS_EXCLUSIONS_MAX_JSON) return false;
    auto* doc = yyjson_read(data, size, 0);
    if (!doc) return false;
    auto* root = yyjson_doc_get_root(doc);
    auto* schema = yyjson_obj_get(root, "schema_version");
    auto* paths = yyjson_obj_get(root, "preserve_paths");
    bool valid = yyjson_is_obj(root) && yyjson_is_uint(schema) && yyjson_get_uint(schema) == 1 &&
        yyjson_is_arr(paths) && yyjson_arr_size(paths) <= MM_MAX_PRESERVE_PATHS;
    auto parsed = std::make_unique<mm_contents_policy>(policy);
    parsed->preserve_count = parsed->preserve_size = 0;
    if (valid) {
        size_t index, count;
        yyjson_val* value;
        yyjson_arr_foreach(paths, index, count, value) {
            if (!yyjson_is_str(value) || !mm_policy_preserve(parsed.get(), yyjson_get_str(value), yyjson_get_len(value))) {
                valid = false;
                break;
            }
        }
    }
    if (valid) policy = *parsed;
    yyjson_doc_free(doc);
    return valid;
}

// New sysmodules need both an executable and a boot flag in a pack component.
// Ordinary romfs/exefs/cheats title folders never become managed by inference.
inline void AddManifestSysmodules(mm_contents_policy& policy, yyjson_val* root) {
    auto* components = yyjson_obj_get(root, "components");
    if (!yyjson_is_obj(components)) return;
    size_t index, count;
    yyjson_val *key, *component;
    yyjson_obj_foreach(components, index, count, key, component) {
        auto* files = yyjson_obj_get(component, "files");
        if (!yyjson_is_arr(files)) continue;
        std::vector<std::string> paths;
        size_t i, n;
        yyjson_val* file;
        yyjson_arr_foreach(files, i, n, file) {
            if (const char* value = yyjson_get_str(file)) {
                std::string path = mm_sd_relative_path(value);
                for (char& c : path) c = c == '\\' ? '/' : mm_path_lower(c);
                paths.push_back(std::move(path));
            }
        }
        for (const auto& path : paths) {
            char id[17];
            if (!mm_contents_title_id(path.c_str(), id)) continue;
            const std::string base = std::string("atmosphere/contents/") + id;
            if (path == base + "/flags/boot2.flag" &&
                std::find(paths.begin(), paths.end(), base + "/exefs.nsp") != paths.end()) {
                mm_policy_add(&policy, id);
            }
        }
    }
}

inline std::string SerializeContentsPolicy(const mm_contents_policy& policy) {
    std::string result = MM_CONTENTS_PROTECTION_MARKER "\n";
    for (unsigned i = 0; i < policy.count; ++i) {
        result += "S ";
        result += policy.ids[i];
        result += '\n';
    }
    for (unsigned pos = 0; pos < policy.preserve_size;) {
        const char* path = policy.preserve_paths + pos;
        result += "P ";
        result += path;
        result += '\n';
        pos += std::strlen(path) + 1;
    }
    result += "END\n";
    return result;
}

inline bool HasModProtection(const std::vector<unsigned char>& payload) {
    constexpr char marker[] = MM_CONTENTS_PROTECTION_MARKER;
    return std::search(payload.begin(), payload.end(), marker, marker + sizeof(marker) - 1) != payload.end();
}
}
