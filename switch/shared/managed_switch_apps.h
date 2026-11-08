#pragma once

/* Reviewed utility files from manifest.json, plus the bundled installer payload.
 * Keep this list explicit: games and ports can also be NROs and may live inside
 * app folders. A folder name or extension must never grant recursive deletion.
 * Unknown apps/data remain protected until their exact files are reviewed. */
static const char* const mm_managed_switch_files[] = {
    "switch/aio-switch-updater/aio-switch-updater.nro",
    "switch/daybreak.nro",
    "switch/haze.nro",
    "switch/reboot_to_payload.nro",
    "switch/dbi_ru/dbi.nro",
    "switch/dbi_ru/dbibackend",
    "switch/dbi_ru/dbi.config",
    "switch/goldleaf.nro",
    "switch/jksv.nro",
    "switch/linkalho/linkalho.nro",
    "switch/quick-reboot.nro",
    "switch/mm-tools/mm-tools.nro",
    "switch/mm-tools/hats-installer.bin",
    "switch/switch_90dns_tester.nro",
    "switch/tinwoo/tinwoo.nro",
    "switch/ultrahand-reload/ultrahand-reload.nro",
    "switch/sys-clk-manager.nro",
    "switch/.overlays/dns-mitm_manager.ovl",
    "switch/.overlays/ovledizon.ovl",
    "switch/.overlays/quick-reboot.ovl",
    "switch/.overlays/quickntp.ovl",
    "switch/.overlays/status-monitor-overlay.ovl",
    "switch/.overlays/sys-patch-overlay.ovl",
    "switch/.overlays/ovlsysmodules.ovl",
    "switch/.overlays/ovlmenu.ovl",
    "switch/.overlays/emuiibo.ovl",
    "switch/.overlays/sys-clk-overlay.ovl"
};
