# MM HATS INSTALLER

<p align="center">
  <img src="images/preview.jpg" width="65%" />
</p>

Custom HATS installer for TechRepairs4U. It is based on HATS-Tools and uses
separate `mm-tools` folders so it can sit on the same SD card as the original
HATS-Tools app without clashing.

- **Fetch HATS Pack** - Download and install HATS pack releases
- **Fetch Firmware** - Download firmware for installation via Daybreak
- **Uninstall Components** - Remove installed components (except Atmosphere/Hekate)
- **File Browser** - Browse, manage, and extract files on your SD card
- **Cheats Manager** - Download and manage game cheats from multiple sources

## Installation

1. **Download MM HATS INSTALLER**: Download the latest `MM-HATS-INSTALLER` zip from the releases page
2. **Extract to SD Card**: Extract the zip file directly to the root of your Nintendo Switch SD card
3. The package includes the installer payload at `/switch/mm-tools/hats-installer.bin`.

The app installs to `/switch/mm-tools/mm-tools.nro` and stores config/cache files under `/config/mm-tools`.

## Features

### Cheats Manager
The cheats manager provides a comprehensive solution for managing game cheats on your Switch:

- **Multiple Sources**: Download cheats from CheatSlips and nx-cheats-db (local database)
- **View Installed Cheats**: Browse all games with cheats currently installed on your system
- **Cheat Preview**: Preview cheat codes before downloading them
- **Easy Management**: Delete individual cheat files or view detailed cheat information
- **Automatic Detection**: Automatically detects installed games and their build IDs
- **CheatSlips Integration**: Login support for CheatSlips to access premium cheat content

### Automatic Backup
Before installing a HATS pack, the tool can automatically back up your existing `/atmosphere` and `/bootloader` folders to `/sdbackup/` with timestamps (e.g., `/sdbackup/atmosphere_20231225_143000`). This feature can be toggled in the Advanced Options menu.

### Backup Warning
A red warning popup reminds you to backup your SD card before installation. This reminder can be disabled in Advanced Options if you prefer.

### Customizable Sources
MM HATS INSTALLER uses a local release list at `/config/mm-tools/releases.json`. The entries use GitHub latest-release asset URLs, so new HATS releases show up as long as the asset names stay the same.

To customize the URLs, edit `/config/mm-tools/config.ini` on your SD card:

```ini
[pack]
pack_url=file:///config/mm-tools/releases.json

[installer]
# HATS installer payload location
payload=/switch/mm-tools/hats-installer.bin

# Staging path for HATS extraction
staging_path=/config/mm-tools/hats-staging

install_mode=clean

[firmware]
# Firmware source (default: sthetix/NXFW releases)
firmware_url=https://api.github.com/repos/sthetix/NXFW/releases
```

The included release list points to:

- `HATS.MM.zip`
- `HATS.Minimalistic.zip`

## Building from source

This project is based on Sphaira code, but stripped down to the essentials.

You will first need to install [devkitPro](https://devkitpro.org/wiki/Getting_Started).

Next you will need to install the dependencies:
```sh
sudo pacman -S switch-dev deko3d switch-cmake switch-curl switch-glm switch-zlib switch-mbedtls
```

Also you need to have on your environment the packages `git`, `make`, `zip` and `cmake`

Once devkitPro and all dependencies are installed, you can build MM HATS INSTALLER.

```sh
git clone https://github.com/TechRepairs4U/MM-HATS-INSTALLER.git
cd MM-HATS-INSTALLER
./build_release.sh
```

The output will be found in `out/MM-HATS-INSTALLER-1.0.0.zip`.

## Credits

- [libpulsar](https://github.com/ITotalJustice/switch-libpulsar)
- [nanovg-deko3d](https://github.com/ITotalJustice/nanovg-deko3d)
- [stb](https://github.com/nothings/stb)
- [yyjson](https://github.com/ibireme/yyjson)
- [minIni](https://github.com/ITotalJustice/minIni-nx)
- [libnxtc](https://github.com/ITotalJustice/libnxtc)
- [zstd](https://github.com/facebook/zstd)
- [dr_libs](https://github.com/mackron/dr_libs)
- [id3v2lib](https://github.com/larsbs/id3v2lib)
- [nxdumptool](https://github.com/DarkMatterCore/nxdumptool) (for RSA verify code)
- [nx-hbloader](https://github.com/switchbrew/nx-hbloader)
- Everyone who has contributed to this project!


## Support My Work

If you find this project useful, please consider supporting me by buying me a coffee!

<a href="https://www.buymeacoffee.com/sthetixofficial" target="_blank">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" >
</a>
