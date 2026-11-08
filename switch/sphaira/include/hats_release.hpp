#pragma once

#include <yyjson.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ui::menu::hats {

struct ReleaseEntry {
    std::string tag_name;
    std::string name;
    std::string published_at;
    std::string download_url;
    std::string asset_name;
    std::string body;
    std::string author;
    std::string author_url;
    std::uint64_t release_id{};
    std::uint64_t asset_id{};
    std::string asset_updated_at;
    std::string asset_digest;
    std::uint64_t size{};
    bool prerelease{};
};

// Tags are identifiers, not versions. Use exactly the assets GitHub returns.
inline void ParsePackRelease(yyjson_val* release, std::vector<ReleaseEntry>& entries, std::string_view extension = ".zip") {
    if (!yyjson_is_obj(release) || yyjson_get_bool(yyjson_obj_get(release, "draft"))) return;

    const auto string_value = [](yyjson_val* obj, const char* key) -> std::string {
        const auto* value = yyjson_get_str(yyjson_obj_get(obj, key));
        return value ? value : "";
    };
    ReleaseEntry base;
    base.release_id = yyjson_get_uint(yyjson_obj_get(release, "id"));
    base.tag_name = string_value(release, "tag_name");
    base.name = string_value(release, "name");
    base.published_at = string_value(release, "published_at");
    base.body = string_value(release, "body");
    base.prerelease = yyjson_get_bool(yyjson_obj_get(release, "prerelease"));
    auto* author = yyjson_obj_get(release, "author");
    base.author = string_value(author, "login");
    base.author_url = string_value(author, "html_url");

    size_t idx, max;
    yyjson_val* asset;
    yyjson_arr_foreach(yyjson_obj_get(release, "assets"), idx, max, asset) {
        auto entry = base;
        entry.asset_id = yyjson_get_uint(yyjson_obj_get(asset, "id"));
        entry.asset_updated_at = string_value(asset, "updated_at");
        entry.asset_digest = string_value(asset, "digest");
        entry.asset_name = string_value(asset, "name");
        entry.download_url = string_value(asset, "browser_download_url");
        auto lower = entry.asset_name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Asset names become cache filenames: never accept paths or partial ZIPs.
        if (lower.size() <= extension.size() || !lower.ends_with(extension) ||
            entry.asset_name.find_first_of("/\\:") != std::string::npos ||
            !entry.download_url.starts_with("https://")) continue;
        const auto state = string_value(asset, "state");
        if (!state.empty() && state != "uploaded") continue;
        entry.size = yyjson_get_uint(yyjson_obj_get(asset, "size"));
        entry.name = (base.name.empty() ? base.tag_name : base.name) + " - " + entry.asset_name;
        entries.push_back(std::move(entry));
    }
}

inline auto ParseAppReleases(yyjson_val* root, bool installed_nsp) -> std::vector<ReleaseEntry> {
    std::vector<ReleaseEntry> entries;
    if (yyjson_is_obj(root) && !yyjson_get_bool(yyjson_obj_get(root, "prerelease")))
        ParsePackRelease(root, entries, installed_nsp ? ".nsp" : ".zip");
    return entries;
}

inline auto ParsePackReleases(yyjson_val* root) -> std::vector<ReleaseEntry> {
    std::vector<ReleaseEntry> entries;
    if (yyjson_is_arr(root)) {
        size_t idx, max;
        yyjson_val* release;
        yyjson_arr_foreach(root, idx, max, release) {
            ParsePackRelease(release, entries);
        }
    } else {
        ParsePackRelease(root, entries);
    }
    return entries;
}

// Remember the actual installed GitHub asset, never a version or filename.
// Replacing an upload, editing it, or moving Latest to another release is
// detected even when its tag and filename are reused or numerically lower.
inline auto AppUpdateReceipt(const ReleaseEntry& entry) -> std::string {
    if (!entry.release_id || !entry.asset_id || entry.asset_updated_at.empty()) return {};
    return "mm-app-release-v1\n" + std::to_string(entry.release_id) + "\n" +
        std::to_string(entry.asset_id) + "\n" + entry.asset_updated_at + "\n" +
        std::to_string(entry.size) + "\n" + entry.asset_digest + "\n";
}

inline bool IsInstalledAppRelease(const std::vector<ReleaseEntry>& entries, const std::string& receipt) {
    if (receipt.empty()) return false;
    return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
        return AppUpdateReceipt(entry) == receipt;
    });
}

} // namespace sphaira::ui::menu::hats
