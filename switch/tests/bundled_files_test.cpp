#include "bundled_files.hpp"
#include <cassert>
#include <iostream>
#include <map>
using namespace sphaira::hats;

struct MemoryFiles {
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::string fail_write, fail_rename_from;
    unsigned writes{};
    bool FileMatches(const std::string& path, std::span<const std::uint8_t> data) {
        return FileExists(path) && files.at(path) == std::vector<std::uint8_t>(data.begin(), data.end());
    }
    bool FileExists(const std::string& path) { return files.contains(path); }
    bool DirExists(const std::string& path) {
        for (const auto& [name, data] : files) if (name.starts_with(path + "/")) return true;
        return false;
    }
    std::uint32_t ListDirectory(const std::string& path, std::vector<BundledDirectoryEntry>& entries) {
        std::map<std::string, bool> children;
        for (const auto& [name, data] : files) if (name.starts_with(path + "/")) {
            const auto relative = name.substr(path.size() + 1);
            const auto slash = relative.find('/');
            children[relative.substr(0, slash)] = slash != std::string::npos;
        }
        for (const auto& [name, directory] : children) entries.push_back({name, directory});
        return 0;
    }
    std::uint32_t DeleteDirectoryRecursively(const std::string& path) {
        std::erase_if(files, [&](const auto& entry) { return entry.first.starts_with(path + "/"); });
        return 0;
    }
    std::uint32_t CreateDirectoryRecursivelyWithPath(const std::string&) { return 0; }
    std::uint32_t Commit() { return 0; }
    std::uint32_t read_entire_file(const std::string& path, std::vector<std::uint8_t>& data) {
        if (!files.contains(path)) return 10;
        data = files.at(path);
        return 0;
    }
    std::uint32_t write_entire_file(const std::string& path, std::span<const std::uint8_t> data) {
        if (path == fail_write) return 11;
        ++writes;
        files[path] = {data.begin(), data.end()};
        return 0;
    }
    std::uint32_t DeleteFile(const std::string& path) { return files.erase(path) ? 0 : 12; }
    std::uint32_t RenameFile(const std::string& from, const std::string& to) {
        if (from == fail_rename_from) return 13;
        if (!files.contains(from) || files.contains(to)) return 14;
        files[to] = files.at(from);
        files.erase(from);
        return 0;
    }
};

int main() {
    MemoryFiles source, sd;
    for (const auto path : BundledFiles) source.files[path] = {1, 2, 3};
    sd.files["/config/mm-tools/config.ini"] = {55};
    sd.files["/switch/my-game/save.dat"] = {66};
    assert(PrepareBundledFiles(source, sd, "2.1.3") == 0);
    for (const auto path : BundledFiles) assert(sd.files.at(path) == source.files.at(path));
    assert(sd.files.at("/config/mm-tools/config.ini") == std::vector<std::uint8_t>{55});
    assert(sd.files.at("/switch/my-game/save.dat") == std::vector<std::uint8_t>{66});
    sd.files[BundledFiles[1]] = {99};
    sd.files.erase(BundledFiles[2]);
    assert(PrepareBundledFiles(source, sd, "2.1.3") == 0);
    assert(sd.files.at(BundledFiles[1]) == source.files.at(BundledFiles[1]));
    assert(sd.files.at(BundledFiles[2]) == source.files.at(BundledFiles[2]));
    assert(PrepareBundledFiles(source, sd, "2.1.4") == 0);
    assert(sd.files.at(BundledFiles[1]) == source.files.at(BundledFiles[1]));

    const std::string target = BundledFiles[0];
    sd.files[target] = {77};
    const auto old_receipt = sd.files.at(BundledFilesReceipt);
    sd.fail_rename_from = target + ".nsp-update";
    assert(PrepareBundledFiles(source, sd, "2.1.5") == 13);
    assert(sd.files.at(target) == std::vector<std::uint8_t>{77});
    assert(sd.files.at(BundledFilesReceipt) == old_receipt);
    sd.fail_rename_from.clear();
    assert(PrepareBundledFiles(source, sd, "2.1.5") == 0);

    // Recover an interrupted rename before a subsequent write fails.
    sd.files.erase(target);
    sd.files[target + ".nsp-previous"] = {88};
    sd.fail_write = target + ".nsp-update";
    assert(PrepareBundledFiles(source, sd, "2.1.6") == 11);
    assert(sd.files.at(target) == std::vector<std::uint8_t>{88});
    sd.fail_write.clear();
    assert(PrepareBundledFiles(source, sd, "2.1.6") == 0);

    // Receipts do not hide same-version downgrades or corrupt payloads.
    sd.files[target] = {0};
    assert(PrepareBundledFiles(source, sd, "2.1.6") == 0);
    assert(sd.files.at(target) == source.files.at(target));
    const auto writes = sd.writes;
    assert(PrepareBundledFiles(source, sd, "2.1.6") == 0);
    assert(sd.writes == writes); // Current files are checked, not rewritten.

    // Cleanup removes only obsolete app files, after successful repair.
    const auto running = NormalizeAppPath("sdmc:/switch/mm-tools/renamed/Installer.nro");
    for (const auto kept : {running.c_str(), "/switch/mm-tools/mm-tools.nro",
        "/switch/mm-tools/cache/hats/pack.zip", "/config/mm-tools/hats-staging/pack/file.bin",
        "/config/mm-tools/cheats-db/cheats.txt", "/config/mm-tools/installed-app-release.txt",
        "/config/mm-tools/hekate_ipl.ini.bak", "/payload.bak", "/bootloader/update.bak",
        "/themes/my-home-theme.nxtheme", "/atmosphere/contents/0100000000001000/romfs/file"}) sd.files[kept] = {42};
    const auto keep = sd.files;
    for (const auto obsolete : {"/config/mm-tools/themes/old.ini", "/config/mm-tools/icons/old.rgba",
        "/switch/mm-tools/old-payload.bin", "/switch/mm-tools/old-version.nro",
        "/switch/mm-tools/cache/mm-tools-update.zip", "/switch/mm-tools/cache/update.zip",
        "/switch/mm-tools/mm-tools.nro.previous", "/switch/mm-tools/hats-installer.bin.nsp-update",
        "/switch/mm-tools/hats-installer.bin.nsp-previous", "/config/mm-tools/old-settings.ini"}) sd.files[obsolete] = {99};
    assert(CleanObsoleteAppFiles(sd, running) == 0);
    assert(sd.files == keep);
    assert(CleanObsoleteAppFiles(sd, running) == 0 && sd.files == keep);

    MemoryFiles missing_source, fresh_sd;
    assert(PrepareBundledFiles(missing_source, fresh_sd, "2.1.3") == 10);
    assert(!fresh_sd.FileExists(BundledFilesReceipt));
    std::cout << "Startup repair, same-version corruption, no-op boot, bounded cleanup, data preservation and interrupted-copy recovery passed.\n";
}
