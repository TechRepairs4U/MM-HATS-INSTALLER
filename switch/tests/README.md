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
must remain inactive for the installed NSP; update it by installing a new NSP.

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
- Invalid firmware is rejected. Verify downgrade and HATS compatibility prompts, sysMMC/emuMMC identification, exFAT handling, and post-install reboot on a suitable test console.
- Verify game-card and updated-game Build IDs, manual cheat overwrite confirmation, duplicate handling and the 128-entry download limit.
- On a disposable SD-card copy, verify GTA V and another mod are excluded from backups, unchanged after online/cached installs, and that known sysmodules update. Check all four install modes.
- Include a game NRO, a port directory, saves inside a known utility directory and an unknown overlay under `/switch`: none should be backed up, removed or overwritten. Confirm the recognised utility files update.
- Verify the full-package updater installs the NRO, payload, wallpaper and three icons, and that an old payload is rejected before Hekate is modified. Incomplete or wrong-sized artwork must be rejected before replacement starts.
- Publish a test Latest release with an arbitrary tag and renamed ZIP. Verify the app offers it, records the upload only after a successful install, skips it on the next launch, and detects a replacement ZIP under the same tag. Multiple ZIPs must open a choice list; an unrelated ZIP must fail validation without replacing installed files.
- Check the refreshed home screen on hardware: all three cards fit, touch and controller selection agree, text is readable and the wallpaper/icons load correctly.

Host tests do not validate device services, real SD-card I/O, UI layout or reboot behavior. A complete devkitPro build and device checks are required before release.

## GitHub exclusion list

Compile `github_exclusions_test.cpp` with the same include paths and yyjson object as `release_logic_test.cpp`. An optional first argument accepts a freshly downloaded `hats-exclusions.json` and checks every entry and the serialized payload handoff. The test also checks list replacement, an empty list, schema/type errors, traversal, embedded NULs, bounds, case/separator handling, component boundaries, truncated handoffs and rejection of v2 payload markers.

`payload_fs_test` additionally exercises root game folders, individual excluded files, excluded bootloader subfolders, parent retention, absent excluded destinations, exclusion priority over known utilities/sysmodules, and a changed list using the actual copy/delete routines.

On a test console, verify both online and cached packs fetch the list at Launch before backup. Test HTTP failure, invalid JSON, cancellation and an inaccurate console clock: no Hekate changes or install should follow. Add and remove a custom root-file exclusion on a test list and confirm the next install sees the change. Manually relaunching the payload without preparing a new snapshot must stop before modifying installed files. Extract the full v2.1.1 ZIP; the app refuses an older payload.
