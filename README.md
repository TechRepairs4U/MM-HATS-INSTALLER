# Magic Monkei HATS Installer

Download the HATS installer for your device:

| Platform | Current download |
| --- | --- |
| Windows x64 | [Windows 2.1.8](https://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/tag/windows-v2.1.8) |
| macOS 14+ — Apple Silicon and Intel | [macOS 2.3.0](https://github.com/TechRepairs4U/Hats-App-Mac/releases/tag/macos-v2.3.0) |
| Nintendo Switch | [Switch v2.1.9](https://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/tag/v2.1.9) |

The Nintendo Switch app source is in [switch/](switch/), with a matching source
archive attached to the Switch release. Build instructions are in
[switch/BUILD_NSP.md](switch/BUILD_NSP.md). No custom Atmosphere source or patches
are included. Desktop installers remain available at the links above.

Switch v2.1.9 reads the **current firmware and running AMS/Magic Monkei build**
directly from the Switch. Firmware installation no longer requires
`HATS_VERSION.txt` or a HATS release-note compatibility check. The old
unverified-HATS/Force dialog is removed; firmware validation, downgrade/fuse
warnings and the final confirmation remain.

Confirmation boxes now wrap and measure their text, grow to fit and scroll long
messages while keeping buttons separate. The v2.1.8 graphics-buffer fix remains.

From NSP v2.1.7 or later, press **X / Check updates**, then **Update**. Earlier
NSPs require one manual installation over the existing app. Startup repair,
installer-theme cleanup, live GitHub exclusions and NSP self-update remain.
Build, host layout/regression tests and package checks passed. Physical Switch
verification of the new dialog layout and controls remains pending.

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

The Switch updater uses this repository's **Latest** release, now `v2.1.9`.
Older releases and prereleases remain available for existing download links.
Install the new NSP over the previous app, or update the NRO and restart it.

Each release includes `SHA256SUMS.txt`. `INSTALLERS.json` records filenames,
checksums and download locations. No release build or announcement workflow
runs from this repository.
