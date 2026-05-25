#include <switch.h>
#include <memory>
#include "app.hpp"
#include "log.hpp"
#include "ui/menus/hats_main_menu.hpp"

int main(int argc, char** argv) {
    if (!argc || !argv) {
        return 1;
    }

    auto app = std::make_unique<sphaira::App>(argv[0]);
    app->Push<sphaira::ui::menu::hats::MainMenu>();
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
