# Magic Monkei HATS Installer

Download the HATS installer for your device:

| Platform | Current download |
| --- | --- |
| Windows x64 | [Windows 2.1.8](https://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/tag/windows-v2.1.8) |
| macOS 14+ — Apple Silicon and Intel | [macOS 2.3.0](https://github.com/TechRepairs4U/Hats-App-Mac/releases/tag/macos-v2.3.0) |
| Nintendo Switch | [Switch v2.1.7](https://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/tag/v2.1.7) |

The Nintendo Switch app source is in [switch/](switch/), with a matching source
archive attached to the Switch release. Build instructions are in
[switch/BUILD_NSP.md](switch/BUILD_NSP.md). No custom Atmosphere source or patches
are included. Desktop installers remain available at the links above.

Switch v2.1.7 adds **NSP self-updates from GitHub**. It checks at startup; press
**X / Check updates** to retry. Choose Update to download, verify, install and
reload automatically. It keeps the app in its existing storage and retains the
running content until the updated app starts. On-console prod.keys is not needed.

**NSP v2.1.6 and earlier need one manual installation of v2.1.7** to gain this
updater. Install over the existing app, close it fully, then reopen from HOME.
Future NSP updates can run inside the app.

Startup repair for outdated payloads (`0x668 / Module 104`), old-theme cleanup,
live GitHub exclusions and running AMS/Magic Monkei identification are retained.
Build/package, host updater/recovery and HTTPS checks passed. Physical Switch
installation and automatic-reload testing is pending.

## Install

- **Windows:** download `Magic-Monkei-HATS-Installer.x64.exe`, or extract the Windows x64 ZIP and run the included executable.
- **macOS:** open `Magic-Monkei-HATS-Universal.dmg`, drag the app to Applications, eject the image, then open the installed app. This build is ad-hoc signed; macOS may request per-app opening approval.
- **Switch NRO:** extract the complete installer ZIP to the SD-card root.
- **Switch NSP:** install the NSP over the existing installer, close the previous app completely, and reopen it from HOME.

Choose the correct SD card and keep a backup before using an installer. The app
contains its own confirmations and installation instructions.

## App updates

Windows app updates use stable `windows-v…` releases here and require the direct
`Magic-Monkei-HATS-Installer.x64.exe` asset. macOS app updates use the separate
[Hats-App-Mac repository](https://github.com/TechRepairs4U/Hats-App-Mac/releases).
Older desktop builds without an updater need one manual installation first.

The Switch updater uses this repository's **Latest** release, now `v2.1.7`.
Older releases and prereleases remain available for existing download links.
Install the new NSP over the previous app, or update the NRO and restart it.

Each release includes `SHA256SUMS.txt`. `INSTALLERS.json` records filenames,
checksums and download locations. No release build or announcement workflow
runs from this repository.
