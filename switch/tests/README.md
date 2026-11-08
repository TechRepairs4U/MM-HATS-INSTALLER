# Host-side regression checks

`devoptab_registration_test.py` covers the v2.1.4 certificate mount failure.
It compiles the actual pinned devkitPro newlib registration functions on the
host, reproduces the obsolete startup workaround exhausting all device slots,
then tests 100 mount/unmount cycles with free slots left null, registrations
beyond slot 15, true exhaustion/recovery and preservation of existing devices.
The release toolchain uses devkita64-newlib 4.6.0.20260123-4; its linked
`AddDevice`/`RemoveDevice` behavior was also checked against this source.

```sh
curl -fL https://raw.githubusercontent.com/devkitPro/newlib/57bd8e1ade3cf46d2e61a72516ac8bfaf8fdfd62/libgloss/libsysbase/iosupport.c -o /tmp/mm-iosupport.c
python3 tests/devoptab_registration_test.py /tmp/mm-iosupport.c
```

The script verifies the source checksum. On hardware, launch both an online
and a cached HATS pack after app startup: the certificate mount must succeed,
the live exclusions must load, and cancellation must leave installation stopped.

`running_build_test.cpp` tests the running AMS number, exact Magic Monkei identity,
generic-build fallback and sysMMC/emuMMC display. `bundled_files_test.cpp` tests
NSP first-launch assets, version refresh, missing-file repair, configuration/save
preservation and recovery after failed or interrupted replacement. Both compile
with `c++ -std=c++20 -I sphaira/include tests/NAME.cpp -o /tmp/NAME`.

For the NSP edition, verify a HOME-menu launch with no NRO installed and check
that the three cards, background and installer payload are available. A failed
asset preparation must report an error. The NRO ZIP updater and hbmenu replacement
must remain inactive for the installed NSP. From v2.1.7, its separate NSP updater downloads, validates, installs and reloads the HOME-menu application.

These tests compile the same release, firmware-version and cheat-text helpers used by the application. They do not require libnx or a Switch.

With a C/C++20 compiler and yyjson 0.12.0 sources available:

```sh
cc -c /path/to/yyjson/src/yyjson.c -o /tmp/yyjson.o
c++ -std=c++20 -I sphaira/include -I /path/to/yyjson/src tests/release_logic_test.cpp /tmp/yyjson.o -o /tmp/release_logic_test
/tmp/release_logic_test
# Optionally check a saved GitHub release response too:
/tmp/release_logic_test /path/to/latest-release.json
```

Coverage includes arbitrary/non-numeric tags, all ZIP variants, renamed/replaced assets, encoded download URLs, malformed API fields, draft releases, unsafe cache filenames, same-generation firmware downgrades, support metadata, UTF-8 BOM/CRLF cheat imports, master codes, and rejection of UTF-16 or empty cheat files.

App-update checks cover arbitrary tags/ZIP names, lower-numbered Latest releases, same-tag reuploads, changed upload metadata/digests, missing or malformed receipts, multiple ZIP choices and suppressing repeat prompts for the installed asset.

It also checks mod-path boundaries, case handling, the GTA V exclusion, manifest-based sysmodule identification, malformed policy rejection, exact Switch utility-file recognition and installer compatibility markers. Old payloads without Switch-port protection are rejected.

Run the actual payload filesystem routines against an in-memory SD card:

```sh
cc -std=c11 -I tests/payload_stubs -c payload/source/fs.c -o /tmp/payload_fs.o
c++ -std=c++20 -I tests/payload_stubs tests/payload_fs_test.cpp /tmp/payload_fs.o -o /tmp/payload_fs_test
/tmp/payload_fs_test
```

These checks cover replacement and overwrite copying, stale sysmodule removal, mod/cheat preservation without traversing protected SD folders, destination-read errors, incoming pack conflicts, dynamic sysmodule policies, fresh-card installs and unrestricted staging cleanup. Switch coverage includes standalone game NROs, port trees, unknown overlays, saves and files sharing known utility folders, while approved utility files update normally. No real SD card is touched.

Before release, also build the full NRO and bundled payload with devkitPro and verify on hardware:

- The home screen still has exactly Fetch HATS Pack, Fetch Firmware, and Cheats.
- Existing bundled-source configurations migrate, and MM pack variants download from GitHub's latest release.
- Cancel a HATS or firmware download; no launch/install prompt follows, and no partial ZIP appears in Cached Downloads.
- Cached HATS packs work with an internet connection for the exclusion refresh; local firmware folder selection works offline. Cancelling returns to the original menu.
- Invalid firmware is rejected. Verify downgrade/fuse prompts, running firmware and AMS/Magic Monkei identification, sysMMC/emuMMC target, exFAT handling, and post-install reboot. No HATS marker file is required, including offline local/cached installs.
- Verify game-card and updated-game Build IDs, manual cheat overwrite confirmation, duplicate handling and the 128-entry download limit.
- On a disposable SD-card copy, verify GTA V and another mod are excluded from backups, unchanged after online/cached installs, and that known sysmodules update. Check all four install modes.
- Include a game NRO, a port directory, saves inside a known utility directory and an unknown overlay under `/switch`: none should be backed up, removed or overwritten. Confirm the recognised utility files update.
- Verify the full-package updater installs the NRO, payload, wallpaper and three icons, and that an old payload is rejected before Hekate is modified. Incomplete or wrong-sized artwork must be rejected before replacement starts.
- Publish a test Latest release with an arbitrary tag and renamed ZIP. Verify the app offers it, records the upload only after a successful install, skips it on the next launch, and detects a replacement ZIP under the same tag. Multiple ZIPs must open a choice list; an unrelated ZIP must fail validation without replacing installed files.
- Check the refreshed home screen on hardware: all three cards fit, touch and controller selection agree, text is readable and the wallpaper/icons load correctly.

Host tests do not validate device services, real SD-card I/O, UI layout or reboot behavior. A complete devkitPro build and device checks are required before release.

## Renderer command memory

`option_box_layout_test.cpp` uses the fetched NanoVG C source and a host font
to measure the reported warning, firmware install/downgrade confirmations,
image dialogs and long scrolling messages. It checks that message viewports
fit inside the dialog above the separate button row. Real Switch font rendering,
controller scrolling and touch targets still need hardware verification.

```sh
c++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I tests/renderer_stubs -I sphaira/include tests/frame_command_memory_test.cpp \
    -o /tmp/frame_command_memory_test
/tmp/frame_command_memory_test
```

The actual command-memory provider is exercised against fake deko3d/pool
interfaces. The test reproduces fixed-buffer exhaustion, then checks 100 frames
of over 8 MiB, extension during fence recording, reuse without allocation growth,
oversized/aligned requests, allocation failure, overflow rejection, and freeing
only after the fence completes. A devkitPro build checks the real interfaces.
On hardware, open Firmware repeatedly, scroll its full list, and return home;
also check overlays, HATS and Cheats in handheld and docked modes. Host tests do
not execute GPU commands.

## NSP updater

NSP updater host checks (using the same yyjson object as above):

```sh
c++ -std=c++20 -I sphaira/include -I /path/to/yyjson/src tests/self_update_test.cpp /tmp/yyjson.o -o /tmp/self_update_test
/tmp/self_update_test /path/to/MM-HATS-INSTALLER.nsp
python3 tests/update_tls_test.py
```

These check NSP/ZIP selection, receipt separation, GitHub digest and URL rules,
real NSP bounds, malformed/overlapping entries, publication ordering and rollback
on simulated registration/metadata/launch-record failures. The TLS test compiles
the actual update option block against host libcurl, checks GitHub with the
bundled CA file, and verifies that an invalid CA fails.

On hardware, verify NSP update and automatic reload from both SD and internal
storage, first-boot old-content cleanup, unchanged game-install settings, and
missing on-SD prod.keys. Verify failed downloads and invalid packages leave the
current app launchable. v2.1.6 and earlier require a manual installation first.
Host tests cannot substitute for these device checks.

## GitHub exclusion list

Compile `github_exclusions_test.cpp` with the same include paths and yyjson object as `release_logic_test.cpp`. An optional first argument accepts a freshly downloaded `hats-exclusions.json` and checks every entry and the serialized payload handoff. The test also checks list replacement, an empty list, schema/type errors, traversal, embedded NULs, bounds, case/separator handling, component boundaries, truncated handoffs and rejection of v2 payload markers.

`payload_fs_test` additionally exercises root game folders, individual excluded files, excluded bootloader subfolders, parent retention, absent excluded destinations, exclusion priority over known utilities/sysmodules, and a changed list using the actual copy/delete routines.

On a test console, verify both online and cached packs fetch the list at Launch before backup. Test HTTP failure, invalid JSON, cancellation and an inaccurate console clock: no Hekate changes or install should follow. Add and remove a custom root-file exclusion on a test list and confirm the next install sees the change. Manually relaunching the payload without preparing a new snapshot must stop before modifying installed files. Extract the full v2.1.1 ZIP; the app refuses an older payload.
