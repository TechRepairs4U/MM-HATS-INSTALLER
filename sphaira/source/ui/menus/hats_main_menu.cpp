#include "ui/menus/hats_main_menu.hpp"
#include "ui/menus/hats_pack_menu.hpp"
#include "ui/menus/firmware_menu.hpp"
#include "ui/menus/uninstaller_menu.hpp"
#include "ui/menus/cheats_menu.hpp"
#include "ui/menus/filebrowser.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"

#include "app.hpp"
#include "app_version.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "hats_version.hpp"
#include "i18n.hpp"

namespace sphaira::ui::menu::hats {

namespace {
constexpr int ICON_WIDTH = 256;
constexpr int ICON_HEIGHT = 256;
constexpr std::size_t ICON_RGBA_SIZE = ICON_WIDTH * ICON_HEIGHT * 4;
}

MainMenu::MainMenu() : MenuBase{APP_NAME " v" HATS_TOOLS_VERSION, MenuFlag_None} {
    // Initialize menu items with icon paths
    m_items = {
        {"Fetch HATS Pack", "Download and install HATS pack releases", "/config/mm-tools/icons/fetch-hats.rgba"},
        {"Fetch Firmware", "Download firmware for installation via Daybreak", "/config/mm-tools/icons/fetch-firmware.rgba"},
        {"Cheats", "Download cheat codes from nx-cheats-db", "/config/mm-tools/icons/cheats.rgba"},
        {"Uninstall Components", "Remove installed components (except Atmosphere/Hekate)", "/config/mm-tools/icons/uninstall-components.rgba"},
        {"File Browser", "Browse and manage files on SD Card", "/config/mm-tools/icons/file-browser.rgba"},
        {"Advanced Options", "Configure application settings including logging", "/config/mm-tools/icons/advanced-options.rgba"}
    };

    // Refresh version info
    RefreshVersionInfo();

    // Set up actions
    this->SetActions(
        std::make_pair(Button::A, Action{"Select"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Exit"_i18n, [this](){
            App::Exit();
        }}),
        std::make_pair(Button::START, Action{App::Exit})
    );

    // Set up single row of icons (6 items in 1 row), centered horizontally and vertically
    // Calculate total width: 6 icons * 174px + 5 gaps * 20px = 1144px
    // Screen width: 1280px, so left margin: (1280 - 1144) / 2 = 68px
    // Screen height: 720px, icon height: 174px, so top margin: (720 - 174) / 2 = 273px
    const Vec2 pad{20, 20};  // Padding between cells
    const Vec4 v{68, 300.f, 174, 174};  // Centered both horizontally and vertically
    m_list = std::make_unique<List>(6, 6, m_pos, v, pad);
    m_list->SetLayout(List::Layout::GRID);
}

MainMenu::~MainMenu() {
    // Clean up icon textures
    auto* vg = App::GetVg();
    if (vg) {
        for (auto& item : m_items) {
            if (item.icon_texture) {
                nvgDeleteImage(vg, item.icon_texture);
                item.icon_texture = 0;
            }
        }
    }
}

void MainMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect::Focus);
            SetIndex(i);
        }
    });
}

void MainMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (!m_icons_loaded) {
        LoadIcons();
    }

    const float header_y = GetY() + 20.f;
    const float info_x = 80.f;

    // Draw version info header
    gfx::drawTextArgsBold(vg, info_x, header_y, 22.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_TEXT_INFO),
        "HATS: %s", m_hats_version.c_str());

    gfx::drawTextArgsBold(vg, info_x, header_y + 26.f, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(ThemeEntryID_TEXT_INFO),
        "Firmware: %s | Atmosphere: %s",
        m_firmware_version.c_str(),
        m_atmosphere_version.c_str());

    // Draw separator
    gfx::drawRect(vg, 75.f, header_y + 55.f, 1220.f - 150.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

    // Draw icon grid menu items (matching homebrew style)
    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, auto& v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& item = m_items[i];
        const bool selected = (m_index == i);

        // Draw background and selection
        if (selected) {
            gfx::drawRectOutline(vg, theme, 4.f, v);
        }

        // Draw icon (matching homebrew style: use full cell size)
        if (item.icon_texture) {
            gfx::drawImage(vg, v, item.icon_texture, 5); // 5px corner radius like homebrew
        }

        // Draw label at bottom (only when selected, like homebrew)
        if (selected) {
            gfx::drawAppLable(vg, theme, m_scroll_name, x, y, w, item.label.c_str());
        }
    });
}

void MainMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    RefreshVersionInfo();
}

void MainMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
    m_scroll_name.Reset();
}

void MainMenu::OnSelect() {
    switch (m_index) {
        case 0: // Fetch HATS Pack
            App::Push<PackMenu>();
            break;
        case 1: // Fetch Firmware
            App::Push<FirmwareMenu>();
            break;
        case 2: // Cheats
            App::Push<CheatsMenu>();
            break;
        case 3: // Uninstall Components
            App::Push<UninstallerMenu>();
            break;
        case 4: // File Browser
            App::Push<ui::menu::filebrowser::Menu>(MenuFlag_None);
            break;
        case 5: // Advanced Options
            App::DisplayAdvancedOptions();
            break;
    }
}

void MainMenu::RefreshVersionInfo() {
    m_hats_version = sphaira::hats::getHatsVersion();
    m_firmware_version = sphaira::hats::getSystemFirmware();
    m_atmosphere_version = sphaira::hats::getAtmosphereVersion();
    m_is_erista = sphaira::hats::isErista();
}

void MainMenu::LoadIcons() {
    auto* vg = App::GetVg();
    if (!vg) return;

    // Load pre-decoded RGBA textures from SD to avoid PNG decoding and keep the NRO small.
    auto load_icon = [vg](const char* path, int& out_texture) -> bool {
        std::vector<u8> data;
        fs::FsNativeSd fs;
        const auto rc = fs.read_entire_file(path, data);
        if (R_FAILED(rc)) {
            log_write("Failed to read menu icon: %s (0x%X)\n", path, rc);
            return false;
        }

        if (data.size() != ICON_RGBA_SIZE) {
            log_write("Invalid menu icon size: %s (%zu bytes)\n", path, data.size());
            return false;
        }

        out_texture = nvgCreateImageRGBA(vg, ICON_WIDTH, ICON_HEIGHT, 0, data.data());
        if (!out_texture) {
            log_write("Failed to create NanoVG image texture from RGBA icon: %s\n", path);
            return false;
        }

        return true;
    };

    // Load all icons
    bool success = true;
    for (auto& item : m_items) {
        success &= load_icon(item.icon_path, item.icon_texture);
    }

    if (success) {
        log_write("Successfully loaded all menu icons\n");
    } else {
        log_write("Warning: Some icons failed to load\n");
    }
    m_icons_loaded = true;
}

} // namespace sphaira::ui::menu::hats
