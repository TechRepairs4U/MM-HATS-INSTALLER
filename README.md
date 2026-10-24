# Magic Monkei HATS Installer

Download the HATS installer for your device:

| Platform | Current download |
| --- | --- |
| Windows x64 | [Windows 2.1.8](https://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/tag/windows-v2.1.8) |
| macOS 14+ — Apple Silicon and Intel | [macOS 2.3.0](https://github.com/TechRepairs4U/Hats-App-Mac/releases/tag/macos-v2.3.0) |
| Nintendo Switch | [Switch v2.1.5](https://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/tag/v2.1.5) |

This is a downloads-only repository. It contains installer releases, installation
notes and SHA-256 checksums. It contains no Atmosphere source tree, source archive
or custom Atmosphere patches, and no history from the previous repository.
GitHub's automatic “Source code” downloads contain this download documentation,
not application or firmware source code.

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

The Switch updater uses this repository's **Latest** release, which remains
`v2.1.5`. Older releases and prereleases are retained for existing download links.
Recreated releases have new GitHub IDs, so an existing Switch installation may
once offer the current release again.

Each release includes `SHA256SUMS.txt` for its installer files. The binaries were
restored unchanged from verified local backups. `INSTALLERS.json` records those
filenames, checksums and download locations. No release build or announcement
workflow runs from this downloads repository.
