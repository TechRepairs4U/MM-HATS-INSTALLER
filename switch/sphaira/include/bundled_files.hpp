#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace sphaira::hats {

inline constexpr const char* BundledFiles[] = {
    "/switch/mm-tools/hats-installer.bin",
    "/config/mm-tools/background.rgba",
    "/config/mm-tools/hekate_ipl_mod.ini",
    "/config/mm-tools/icons/fetch-hats.rgba",
    "/config/mm-tools/icons/fetch-firmware.rgba",
    "/config/mm-tools/icons/cheats.rgba",
    "/config/mm-tools/icons/advanced-options.rgba",
    "/config/mm-tools/icons/file-browser.rgba",
    "/config/mm-tools/icons/uninstall-components.rgba",
};
inline constexpr const char* BundledFilesReceipt = "/config/mm-tools/installed-nsp-assets.txt";

struct BundledDirectoryEntry { std::string name; bool directory; };

inline std::string NormalizeAppPath(std::string path) {
    for (auto& c : path) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    if (path.starts_with("sdmc:")) path.erase(0, 5);
    else if (path.starts_with("sd:")) path.erase(0, 3);
    return path;
}

inline bool KeepAppFile(std::string_view path, std::string_view running_nro) {
    for (const auto bundled : BundledFiles) if (path == bundled) return true;
    for (const auto kept : {
        BundledFilesReceipt, "/switch/mm-tools/mm-tools.nro",
        "/config/mm-tools/config.ini", "/config/mm-tools/playlog.ini", "/config/mm-tools/log.txt",
        "/switch/mm-tools/cache/etag_v2.json",
        "/config/mm-tools/installed-app-release.txt", "/config/mm-tools/managed-sysmodules.txt",
        "/config/mm-tools/hekate_ipl.ini.bak", "/config/mm-tools/cheat-metadata.json",
        "/config/mm-tools/cheatslips_token.json"
    }) if (path == kept) return true;
    return !running_nro.empty() && path == running_nro;
}

inline bool KeepAppDataTree(std::string_view path) {
    return path == "/switch/mm-tools/cache/hats" || path == "/config/mm-tools/hats-staging" ||
        path == "/config/mm-tools/cheats-db";
}

// Traversal starts ONLY at these two app-owned roots. Never follow a configured
// theme or payload path for deletion, and never touch HOME-menu themes/bootloaders.
template<class Destination>
std::uint32_t CleanObsoleteAppFiles(Destination& destination, std::string_view running_nro) {
    const auto visit = [&](auto&& self, const std::string& root) -> std::uint32_t {
        if (!destination.DirExists(root)) return 0;
        std::vector<BundledDirectoryEntry> entries;
        if (const auto rc = destination.ListDirectory(root, entries)) return rc;
        for (const auto& entry : entries) {
            if (entry.name.empty() || entry.name == "." || entry.name == ".." ||
                entry.name.find_first_of("/\\:") != std::string::npos) return 1;
            const auto path = root + "/" + entry.name;
            const auto normalized = NormalizeAppPath(path);
            if (entry.directory) {
                if (KeepAppDataTree(normalized)) continue;
                // Only directories containing the current files need traversal.
                bool inspect = normalized == "/config/mm-tools/icons" ||
                    normalized == "/switch/mm-tools/cache" ||
                    running_nro.starts_with(normalized + "/");
                if (inspect) {
                    if (const auto rc = self(self, path)) return rc;
                } else if (const auto rc = destination.DeleteDirectoryRecursively(path)) return rc;
            } else if (!KeepAppFile(normalized, running_nro)) {
                if (const auto rc = destination.DeleteFile(path)) return rc;
            }
        }
        return 0;
    };
    for (const auto root : {"/switch/mm-tools", "/config/mm-tools"}) {
        if (const auto rc = visit(visit, std::string(root))) return rc;
    }
    return destination.Commit();
}

// Stage each replacement before moving the old file, and retain a recoverable
// previous copy until the replacement is committed. Only fixed app paths enter here.
template<class Destination>
std::uint32_t ReplaceBundledFile(Destination& destination, const char* path,
        std::span<const std::uint8_t> data) {
    const auto staged = std::string(path) + ".nsp-update";
    const auto previous = std::string(path) + ".nsp-previous";
    std::uint32_t rc;
    if ((rc = destination.CreateDirectoryRecursivelyWithPath(path))) return rc;
    if (destination.FileExists(previous)) {
        if (!destination.FileExists(path)) {
            if ((rc = destination.RenameFile(previous, path))) return rc;
        } else if ((rc = destination.DeleteFile(previous))) return rc;
    }
    if (destination.FileExists(staged) && (rc = destination.DeleteFile(staged))) return rc;
    if ((rc = destination.write_entire_file(staged, data))) return rc;
    if ((rc = destination.Commit())) return rc;
    const bool existed = destination.FileExists(path);
    if (existed && (rc = destination.RenameFile(path, previous))) return rc;
    if ((rc = destination.RenameFile(staged, path))) {
        if (existed) destination.RenameFile(previous, path);
        return rc;
    }
    if ((rc = destination.Commit())) return rc;
    if (existed && (rc = destination.DeleteFile(previous))) return rc;
    return 0;
}

template<class Source, class Destination>
std::uint32_t PrepareBundledFiles(Source& source, Destination& destination, std::string_view version) {
    std::vector<std::uint8_t> data;
    // A receipt cannot prove the files are still current: an older HATS pack or
    // partial manual ZIP extraction may have replaced them since the last boot.
    for (const auto path : BundledFiles) {
        if (const auto rc = source.read_entire_file(path, data)) return rc;
        if (data.empty()) return 1;
        if (destination.FileMatches(path, data)) continue;
        if (const auto rc = ReplaceBundledFile(destination, path, data)) return rc;
    }
    const auto receipt = std::span(reinterpret_cast<const std::uint8_t*>(version.data()), version.size());
    if (!destination.FileMatches(BundledFilesReceipt, receipt)) {
        if (const auto rc = ReplaceBundledFile(destination, BundledFilesReceipt,
            receipt)) return rc;
    }
    return destination.Commit();
}

} // namespace sphaira::hats
