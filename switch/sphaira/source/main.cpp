#include <switch.h>
#include <memory>
#include "app.hpp"
#include "log.hpp"
#include "ui/menus/hats_main_menu.hpp"
#include "app_version.hpp"
#include "bundled_files.hpp"
#include <array>

namespace {
struct StartupSd {
    fs::FsNativeSd sd;
    bool FileExists(const fs::FsPath& path) { return sd.FileExists(path); }
    bool DirExists(const fs::FsPath& path) { return sd.DirExists(path); }
    Result CreateDirectoryRecursivelyWithPath(const fs::FsPath& path) { return sd.CreateDirectoryRecursivelyWithPath(path); }
    Result Commit() { return sd.Commit(); }
    Result DeleteFile(const fs::FsPath& path) { return sd.DeleteFile(path); }
    Result DeleteDirectoryRecursively(const fs::FsPath& path) { return sd.DeleteDirectoryRecursively(path); }
    Result RenameFile(const fs::FsPath& from, const fs::FsPath& to) { return sd.RenameFile(from, to); }
    Result write_entire_file(const fs::FsPath& path, std::span<const std::uint8_t> data) { return sd.write_entire_file(path, data); }
    bool FileMatches(const fs::FsPath& path, std::span<const std::uint8_t> expected) {
        fs::File file;
        if (R_FAILED(sd.OpenFile(path, FsOpenMode_Read, &file))) return false;
        s64 size{};
        if (R_FAILED(file.GetSize(&size)) || size != static_cast<s64>(expected.size())) return false;
        std::array<std::uint8_t, 64 * 1024> buffer;
        for (size_t offset = 0; offset < expected.size();) {
            const auto length = std::min(buffer.size(), expected.size() - offset);
            u64 actual{};
            if (R_FAILED(file.Read(offset, buffer.data(), length, FsReadOption_None, &actual)) ||
                actual != length || !std::equal(buffer.begin(), buffer.begin() + length, expected.begin() + offset)) return false;
            offset += length;
        }
        return true;
    }
    Result ListDirectory(const fs::FsPath& path, std::vector<sphaira::hats::BundledDirectoryEntry>& entries) {
        fs::Dir directory;
        R_TRY(sd.OpenDirectory(path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &directory));
        std::vector<FsDirectoryEntry> native;
        R_TRY(directory.ReadAll(native));
        for (const auto& entry : native) entries.push_back({entry.name, entry.type == FsDirEntryType_Dir});
        return 0;
    }
};
}

int main(int argc, char** argv) {
    const char* argv0 = argc > 0 && argv && argv[0] ? argv[0] : "";
    if (!envIsNso() && !*argv0) {
        return 1;
    }

    Result bundled_result = romfsInit();
    {
        if (R_SUCCEEDED(bundled_result)) {
            fs::FsStdio source{true, "romfs:/mm-package/"};
            StartupSd destination;
            bundled_result = destination.sd.GetFsOpenResult();
            if (R_SUCCEEDED(bundled_result)) {
                bundled_result = sphaira::hats::PrepareBundledFiles(source, destination, HATS_TOOLS_VERSION);
            }
            if (R_SUCCEEDED(bundled_result)) {
                bundled_result = sphaira::hats::CleanObsoleteAppFiles(destination, sphaira::hats::NormalizeAppPath(argv0));
            }
            romfsExit();
        }
    }

    auto app = std::make_unique<sphaira::App>(argv0);
    app->m_bundled_assets_result = bundled_result;
    app->Push<sphaira::ui::menu::hats::MainMenu>();
    if (R_FAILED(bundled_result)) {
        sphaira::App::PushErrorBox(bundled_result, "Unable to refresh the installer's files. Check free space and the SD card, then reopen the app. HATS installation is blocked until repair succeeds.");
    }
    app->Loop();
    return 0;
}

extern "C" {

namespace {

bool g_applet_locked{};
bool g_socket_init{};
bool g_pl_init{};
bool g_nifm_init{};
bool g_account_init{};
bool g_set_init{};
bool g_hidsys_init{};
bool g_ncm_init{};

void try_init(const char* name, Result rc, bool& flag) {
    if (R_SUCCEEDED(rc)) {
        flag = true;
    } else {
        log_write("[INIT] %s failed: 0x%X\n", name, rc);
    }
}

} // namespace

void userAppInit(void) {
    sphaira::App::SetBoostMode(true);

    // https://github.com/mtheall/ftpd/blob/e27898f0c3101522311f330e82a324861e0e3f7e/source/switch/init.c#L31
    const SocketInitConfig socket_config_application = {
        .tcp_tx_buf_size = 1024 * 64,
        .tcp_rx_buf_size = 1024 * 64,
        .tcp_tx_buf_max_size = 1024 * 1024 * 4,
        .tcp_rx_buf_max_size = 1024 * 1024 * 4,
        .udp_tx_buf_size = 0x2400, // same as default
        .udp_rx_buf_size = 0xA500, // same as default
        .sb_efficiency = 8,
        .num_bsd_sessions = 3,
        .bsd_service_type = BsdServiceType_Auto,
    };

    const SocketInitConfig socket_config_applet = {
        .tcp_tx_buf_size = 1024 * 32,
        .tcp_rx_buf_size = 1024 * 64,
        .tcp_tx_buf_max_size = 1024 * 256,
        .tcp_rx_buf_max_size = 1024 * 256,
        .udp_tx_buf_size = 0x2400, // same as default
        .udp_rx_buf_size = 0xA500, // same as default
        .sb_efficiency = 4,
        .num_bsd_sessions = 3,
        .bsd_service_type = BsdServiceType_Auto,
    };

    const auto is_application = sphaira::App::IsApplication();

    const auto socket_config = is_application ? socket_config_application : socket_config_applet;

    try_init("appletLockExit", appletLockExit(), g_applet_locked);
    try_init("socketInitialize", socketInitialize(&socket_config), g_socket_init);
    try_init("plInitialize", plInitialize(PlServiceType_User), g_pl_init);
    try_init("nifmInitialize", nifmInitialize(NifmServiceType_User), g_nifm_init);
    try_init("accountInitialize", accountInitialize(is_application ? AccountServiceType_Application : AccountServiceType_System), g_account_init);
    try_init("setInitialize", setInitialize(), g_set_init);
    try_init("hidsysInitialize", hidsysInitialize(), g_hidsys_init);
    try_init("ncmInitialize", ncmInitialize(), g_ncm_init);

    // it doesn't matter if this fails.
    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);

    log_nxlink_init();
}

void userAppExit(void) {
    log_nxlink_exit();

    if (g_ncm_init) ncmExit();
    if (g_hidsys_init) hidsysExit();
    if (g_set_init) setExit();
    if (g_account_init) accountExit();
    if (g_nifm_init) nifmExit();
    if (g_pl_init) plExit();
    if (g_socket_init) socketExit();
    // NOTE (DMC): prevents exfat corruption.
    if (auto fs = fsdevGetDeviceFileSystem("sdmc:")) {
        fsFsCommit(fs);
    }

    sphaira::App::SetBoostMode(false);
    if (g_applet_locked) appletUnlockExit();
}

} // extern "C"
