#pragma once

#include "ui/menus/menu_base.hpp"
#include "ui/list.hpp"
#include "hats_release.hpp"
#include <string>
#include <vector>

namespace sphaira::ui::menu::hats {

struct MainMenuItem {
    std::string label;
    std::string description;
    const char* icon_path; // Path to external raw RGBA icon
    int icon_texture{};    // NanoVG texture handle
};

struct MainMenu final : MenuBase {
    MainMenu();
    ~MainMenu();

    auto GetShortTitle() const -> const char* override { return APP_NAME; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void OnSelect();
    void RefreshVersionInfo();
    void LoadIcons();
    void CheckForUpdates(bool manual = false);
    void StartNspUpdate(const ReleaseEntry& release);
    void PromptAppUpdate(const ReleaseEntry& release);
    void StartUpdate();

private:
    std::vector<MainMenuItem> m_items;
    s64 m_index{};
    std::unique_ptr<List> m_list;

    // Scrolling text for app labels
    ui::ScrollingText m_scroll_name;

    // Version info
    std::string m_build_label;
    std::string m_build_version;
    std::string m_firmware_version;
    std::string m_running_mode;
    std::string m_update_version;
    std::string m_update_download_url;
    std::string m_update_receipt;
    bool m_is_erista{true};
    bool m_icons_loaded{};
    bool m_update_check_started{};
    bool m_update_check_inflight{};
};

} // namespace sphaira::ui::menu::hats
