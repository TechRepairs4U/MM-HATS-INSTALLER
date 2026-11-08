#include "ui/menus/hats_main_menu.hpp"
#include "ui/menus/hats_pack_menu.hpp"
#include "ui/menus/firmware_menu.hpp"
#include "ui/menus/cheats_menu.hpp"
#include "ui/error_box.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/popup_list.hpp"

#include "ui/nvg_util.hpp"

#include "app.hpp"
#include "app_version.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "nro.hpp"
#include "threaded_file_transfer.hpp"
#include "hats_version.hpp"
#include "hats_contents_policy.hpp"
#include "i18n.hpp"
#include "self_update_policy.hpp"
#include "bundled_files.hpp"
#include "yati/yati.hpp"

#include <yyjson.h>
#include <cstring>

namespace sphaira::ui::menu::hats {

namespace {
constexpr int ICON_WIDTH = 256;
constexpr int ICON_HEIGHT = 256;
constexpr std::size_t ICON_RGBA_SIZE = ICON_WIDTH * ICON_HEIGHT * 4;
constexpr const char* UPDATE_REPOSITORY = "TechRepairs4U/MM-HATS-INSTALLER";
constexpr const char* UPDATE_RELEASES_URL = "https://api.github.com/repos/TechRepairs4U/MM-HATS-INSTALLER/releases/latest";
constexpr const char* UPDATE_RECEIPT_PATH = "/config/mm-tools/installed-app-release.txt";
struct UpdateArtwork {
    const char* entry;
    std::size_t size;
};
constexpr UpdateArtwork UPDATE_ARTWORK[] = {
    {"/config/mm-tools/background.rgba", 1280 * 720 * 4},
    {"/config/mm-tools/icons/fetch-hats.rgba", ICON_RGBA_SIZE},
    {"/config/mm-tools/icons/fetch-firmware.rgba", ICON_RGBA_SIZE},
    {"/config/mm-tools/icons/cheats.rgba", ICON_RGBA_SIZE},
};

void DrawHomeText(NVGcontext* vg, float x, float y, float size, const NVGcolor& colour, const char* text, float spacing = 0.f) {
    nvgSave(vg);
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLetterSpacing(vg, spacing);
    nvgFillColor(vg, colour);
    nvgText(vg, x, y, text, nullptr);
    nvgRestore(vg);
}

}

MainMenu::MainMenu() : MenuBase{"MAGIC MONKEI", MenuFlag_None} {
    SetTitleSubHeading(envIsNso() ? "HATS INSTALLER / v" HATS_TOOLS_VERSION " / NSP" : "HATS INSTALLER / v" HATS_TOOLS_VERSION);
    // Initialize menu items with icon paths
    m_items = {
        {"Fetch HATS Pack", "Get your latest HATS release", "/config/mm-tools/icons/fetch-hats.rgba"},
        {"Fetch Firmware", "Download and install system updates", "/config/mm-tools/icons/fetch-firmware.rgba"},
        {"Cheats", "Find and manage your game cheats", "/config/mm-tools/icons/cheats.rgba"}
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
        std::make_pair(Button::X, Action{"Check updates"_i18n, [this](){
            CheckForUpdates(true);
        }}),
        std::make_pair(Button::START, Action{App::Exit})
    );

    // Three large cards with persistent labels and the same touch/focus bounds.
    const Vec2 pad{24, 20};
    const Vec4 v{80, 350.f, 356, 250};
    m_list = std::make_unique<List>(3, 3, m_pos, v, pad);
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

    const auto text = theme->GetColour(ThemeEntryID_TEXT);
    const auto muted = theme->GetColour(ThemeEntryID_TEXT_INFO);
    const auto accent = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
    DrawHomeText(vg, 80.f, 135.f, 38.f, text, "Manage your Switch.");
    DrawHomeText(vg, 80.f, 185.f, 18.f, muted, "HATS packs, system updates and game cheats.");

    // Leave space on the right for the Magic Monkei mascot in the wallpaper.
    const char* labels[] = {m_build_label.c_str(), "FIRMWARE", "RUNNING MODE"};
    const std::string* values[] = {&m_build_version, &m_firmware_version, &m_running_mode};
    const float columns[] = {80.f, 400.f, 620.f};
    const float widths[] = {290.f, 190.f, 200.f};
    for (unsigned i = 0; i < 3; ++i) {
        DrawHomeText(vg, columns[i], 245.f, 12.f, accent, labels[i], 1.5f);
        nvgSave(vg);
        nvgIntersectScissor(vg, columns[i], 265.f, widths[i], 32.f);
        DrawHomeText(vg, columns[i], 268.f, 20.f, text, values[i]->c_str());
        nvgRestore(vg);
    }

    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, auto& v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& item = m_items[i];
        const bool selected = (m_index == i);

        const auto accent = theme->GetColour(ThemeEntryID_HIGHLIGHT_1);
        auto surface = theme->GetColour(selected ? ThemeEntryID_SELECTED_BACKGROUND : ThemeEntryID_GRID);
        surface.a = 0.97f;
        gfx::drawRect(vg, v, surface, 20.f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, 20.f);
        nvgStrokeWidth(vg, selected ? 2.f : 1.f);
        nvgStrokeColor(vg, selected ? accent : theme->GetColour(ThemeEntryID_LINE));
        nvgStroke(vg);

        if (item.icon_texture) {
            gfx::drawImage(vg, x + 24.f, y + 16.f, 120.f, 120.f, item.icon_texture);
        } else {
            DrawHomeText(vg, x + 32.f, y + 40.f, 48.f, accent, i == 0 ? "H" : i == 1 ? "F" : "C");
        }
        if (selected) {
            DrawHomeText(vg, x + w - 111.f, y + 30.f, 11.f, accent, "SELECTED", 1.f);
        }
        DrawHomeText(vg, x + 28.f, y + 150.f, 25.f, theme->GetColour(ThemeEntryID_TEXT), item.label.c_str());
        DrawHomeText(vg, x + 28.f, y + 190.f, 16.f, theme->GetColour(ThemeEntryID_TEXT_INFO), item.description.c_str());
    });
    DrawHomeText(vg, 80.f, 619.f, 14.f, muted, "Game mods and ports are preserved during HATS updates.");
}

void MainMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    RefreshVersionInfo();
    CheckForUpdates();
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
    }
}

void MainMenu::RefreshVersionInfo() {
    const auto running = sphaira::hats::getRunningBuildInfo();
    m_build_label = running.label;
    m_build_version = running.version;
    m_firmware_version = sphaira::hats::getSystemFirmware();
    m_running_mode = running.mode;
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

void MainMenu::CheckForUpdates(bool manual) {
    if (m_update_check_inflight || (m_update_check_started && !manual)) {
        return;
    }

    m_update_check_started = true;
    m_update_check_inflight = true;
    const bool installed_nsp = envIsNso();

    const bool queued = curl::Api().ToMemoryAsync(
        curl::Url{std::string(UPDATE_RELEASES_URL) + "?mm_update=" + std::to_string(armGetSystemTick())},
        curl::Flags{curl::Flag_VerifyTls},
        curl::StopToken{GetToken()},
        curl::Header{
            {"Accept", "application/vnd.github+json"},
            {"X-GitHub-Api-Version", "2022-11-28"},
            {"Cache-Control", "no-cache"},
        },
        curl::OnProgress{[](s64, s64 downloaded, s64, s64) { return downloaded <= 4 * 1024 * 1024; }},
        curl::OnComplete{[this, installed_nsp, manual](auto& result) {
            m_update_check_inflight = false;
            if (!result.success || result.data.empty()) {
                log_write("update check failed for %s (HTTP %ld)\n", UPDATE_REPOSITORY, result.code);
                App::Notify("Could not check GitHub updates. Press X to retry.");
                return;
            }

            auto* document = yyjson_read(
                reinterpret_cast<const char*>(result.data.data()),
                result.data.size(),
                YYJSON_READ_NOFLAG
            );
            if (!document) {
                log_write("update check returned invalid JSON\n");
                return;
            }

            auto* root = yyjson_doc_get_root(document);
            if (!yyjson_is_obj(root) || yyjson_get_bool(yyjson_obj_get(root, "prerelease"))) {
                yyjson_doc_free(document);
                log_write("update check did not return a stable Latest release\n");
                return;
            }

            auto entries = ParseAppReleases(root, installed_nsp);
            yyjson_doc_free(document);
            std::erase_if(entries, [installed_nsp](const auto& entry) {
                return AppUpdateReceipt(entry).empty() || (installed_nsp &&
                    (!self_update::IsTrustedAsset(entry.download_url) || !self_update::IsDigest(entry.asset_digest) ||
                    !entry.size || entry.size > self_update::MaxPackageSize));
            });

            if (entries.empty()) {
                log_write("update check: Latest has no valid %s assets\n", installed_nsp ? "NSP" : "ZIP");
                if (manual) App::Notify("Latest has no compatible update package.");
                return;
            }

            fs::FsNativeSd fs;
            std::vector<u8> receipt;
            const auto receipt_path = installed_nsp ? self_update::ReceiptPath : UPDATE_RECEIPT_PATH;
            if (R_SUCCEEDED(fs.read_entire_file(receipt_path, receipt)) &&
                IsInstalledAppRelease(entries, std::string(receipt.begin(), receipt.end()))) {
                log_write("update check: Latest upload is already installed\n");
                if (manual) App::Notify("MM HATS INSTALLER is up to date.");
                return;
            }

            // A first manual NSP installation has no asset receipt. Seed it for
            // our exact running release; future reuploads still compare identity.
            if (installed_nsp && receipt.empty() && entries.size() == 1 &&
                entries[0].tag_name == "v" HATS_TOOLS_VERSION) {
                const auto baseline = AppUpdateReceipt(entries[0]);
                const auto rc = sphaira::hats::ReplaceBundledFile(fs, receipt_path,
                    std::span(reinterpret_cast<const u8*>(baseline.data()), baseline.size()));
                log_write("self-update: initial release receipt: 0x%X\n", rc);
                if (manual) App::Notify("MM HATS INSTALLER is up to date.");
                return;
            }

            // With unrestricted filenames, multiple uploaded ZIPs need a choice.
            // Package contents are checked before any installed files change.
            App::Push<ui::OptionBox>(
                "The latest MM HATS INSTALLER release is available on GitHub.\n"
                "Download and install it? The NSP edition reloads automatically.",
                "Later"_i18n,
                "Update"_i18n,
                1,
                [this, entries, installed_nsp](auto index) {
                    if (!index || *index != 1) return;
                    if (entries.size() == 1) {
                        PromptAppUpdate(entries.front());
                        return;
                    }
                    ui::PopupList::Items items;
                    for (const auto& entry : entries) items.push_back(entry.asset_name);
                    App::Push<ui::PopupList>(installed_nsp ? "Choose installer NSP" : "Choose installer ZIP", items,
                        [this, entries](auto selected) {
                            if (selected && *selected >= 0 && static_cast<size_t>(*selected) < entries.size())
                                PromptAppUpdate(entries[*selected]);
                        });
                }
            );
        }}
    );

    if (!queued) {
        m_update_check_inflight = false;
        log_write("failed to queue update check for %s\n", UPDATE_REPOSITORY);
    }
}

void MainMenu::PromptAppUpdate(const ReleaseEntry& release) {
    if (envIsNso()) {
        StartNspUpdate(release);
        return;
    }
    m_update_version = release.name;
    m_update_download_url = release.download_url;
    m_update_receipt = AppUpdateReceipt(release);
    StartUpdate();
}

void MainMenu::StartNspUpdate(const ReleaseEntry& release) {
    App::Push<ui::ProgressBox>(0, "Updating MM HATS INSTALLER", release.asset_name,
        [release](auto pbox) -> Result {
            R_UNLESS(self_update::IsTrustedAsset(release.download_url) && self_update::IsDigest(release.asset_digest) &&
                release.size && release.size <= self_update::MaxPackageSize, self_update::InvalidPackage);
            fs::FsNativeSd sd;
            R_TRY(sd.GetFsOpenResult());
            const fs::FsPath path{"/switch/mm-tools/cache/mm-tools-update.nsp"};
            R_TRY(sd.CreateDirectoryRecursivelyWithPath(path));
            if (sd.FileExists(path)) R_TRY(sd.DeleteFile(path));
            ON_SCOPE_EXIT(sd.DeleteFile(path));
            pbox->NewTransfer("Downloading update from GitHub");
            const auto result = curl::Api().ToFile(
                curl::Url{release.download_url}, curl::Path{path}, curl::Flags{curl::Flag_VerifyTls},
                curl::Header{{"Cache-Control", "no-cache, no-store"}},
                curl::OnProgress{[pbox, limit = release.size](s64, s64 received, s64, s64) {
                    pbox->UpdateTransfer(received, limit);
                    return received >= 0 && static_cast<u64>(received) <= limit && !pbox->ShouldExit();
                }});
            R_UNLESS(result.success, Result_MainFailedToDownloadUpdate);
            pbox->NewTransfer("Verifying update");
            fs::File file;
            R_TRY(sd.OpenFile(path, FsOpenMode_Read, &file));
            s64 size{};
            R_TRY(file.GetSize(&size));
            R_UNLESS(size > 0 && static_cast<u64>(size) == release.size, self_update::InvalidPackage);
            Sha256Context hash;
            sha256ContextCreate(&hash);
            std::vector<u8> buffer(64 * 1024);
            for (s64 offset = 0; offset < size;) {
                const auto amount = std::min<s64>(buffer.size(), size - offset);
                u64 actual{};
                R_TRY(file.Read(offset, buffer.data(), amount, FsReadOption_None, &actual));
                R_UNLESS(actual == static_cast<u64>(amount), self_update::InvalidPackage);
                sha256ContextUpdate(&hash, buffer.data(), actual);
                offset += actual;
            }
            file.Close();
            u8 digest[SHA256_HASH_SIZE];
            sha256ContextGetHash(&hash, digest);
            constexpr char hex[] = "0123456789abcdef";
            std::string expected = "sha256:";
            for (auto byte : digest) { expected += hex[byte >> 4]; expected += hex[byte & 15]; }
            R_UNLESS(expected == release.asset_digest, self_update::InvalidPackage);
            R_TRY(yati::InstallSelfUpdate(pbox, &sd, path));
            const auto receipt = AppUpdateReceipt(release);
            const auto receipt_result = sphaira::hats::ReplaceBundledFile(sd, self_update::ReceiptPath,
                std::span(reinterpret_cast<const u8*>(receipt.data()), receipt.size()));
            // Installation has committed; receipt failure must not prevent reload.
            log_write("self-update: installed; receipt result: 0x%X\n", receipt_result);
            return 0;
        },
        [](Result rc) {
            if (R_FAILED(rc)) {
                App::PushErrorBox(rc, "Could not complete the NSP update. Check the connection and free space, then press X to retry.");
                return;
            }
            App::ExitRestart();
        }, false);
}

void MainMenu::StartUpdate() {
    if (envIsNso()) return;
    const auto download_url = m_update_download_url;
    const auto update_version = m_update_version;
    const auto update_receipt = m_update_receipt;

    App::Push<ui::ProgressBox>(
        0,
        "Updating"_i18n,
        update_version,
        [download_url, update_version, update_receipt](auto pbox) -> Result {
            fs::FsNativeSd fs;
            R_TRY(fs.GetFsOpenResult());

            const auto app_path = App::GetExePath();
            const fs::FsPath zip_path{"/switch/mm-tools/cache/mm-tools-update.zip"};
            const auto temp_path = app_path + ".update";
            const fs::FsPath payload_path = App::GetApp()->m_installer_payload.Get().c_str();
            const auto payload_temp = payload_path + ".update";
            const auto receipt_temp = fs::FsPath{UPDATE_RECEIPT_PATH} + ".update";
            ON_SCOPE_EXIT({
                fs.DeleteFile(zip_path);
                fs.DeleteFile(temp_path);
                fs.DeleteFile(payload_temp);
                fs.DeleteFile(receipt_temp);
                for (const auto& art : UPDATE_ARTWORK) fs.DeleteFile(fs::FsPath{art.entry} + ".update");
            });
            R_TRY(fs.CreateDirectoryRecursively("/switch/mm-tools/cache"));
            R_TRY(fs.CreateDirectoryRecursivelyWithPath(payload_temp));
            for (const auto& art : UPDATE_ARTWORK) {
                const auto staged = fs::FsPath{art.entry} + ".update";
                R_TRY(fs.CreateDirectoryRecursivelyWithPath(staged));
                if (fs.FileExists(staged)) R_TRY(fs.DeleteFile(staged));
            }

            if (fs.FileExists(zip_path)) {
                R_TRY(fs.DeleteFile(zip_path));
            }
            if (fs.FileExists(temp_path)) {
                R_TRY(fs.DeleteFile(temp_path));
            }
            if (fs.FileExists(payload_temp)) R_TRY(fs.DeleteFile(payload_temp));

            if (!pbox->ShouldExit()) {
                pbox->NewTransfer("Downloading MM HATS INSTALLER package " + update_version);
                const auto result = curl::Api().ToFile(
                    curl::Url{download_url},
                    curl::Flags{curl::Flag_VerifyTls},
                    curl::Path{zip_path},
                    curl::Header{{"Cache-Control", "no-cache"}},
                    curl::OnProgress{pbox->OnDownloadProgressCallback()}
                );
                R_UNLESS(result.success, Result_MainFailedToDownloadUpdate);
            }

            R_TRY(pbox->ShouldExitResult());
            R_UNLESS(!update_receipt.empty(), Result_MainFailedToDownloadUpdate);
            R_TRY(fs.write_entire_file(receipt_temp,
                std::span<const u8>(reinterpret_cast<const u8*>(update_receipt.data()), update_receipt.size())));

            pbox->NewTransfer("Extracting app, installer and artwork");
            R_TRY(thread::TransferUnzipAll(
                pbox,
                zip_path,
                &fs,
                "/",
                [&](const fs::FsPath& name, fs::FsPath& path) -> bool {
                    if (std::strcmp(name, "switch/mm-tools/mm-tools.nro") == 0) {
                        path = temp_path;
                        return true;
                    }
                    if (std::strcmp(name, "switch/mm-tools/hats-installer.bin") == 0) {
                        path = payload_temp;
                        return true;
                    }
                    for (const auto& art : UPDATE_ARTWORK) {
                        if (std::strcmp(name, art.entry + 1) == 0) {
                            path = fs::FsPath{art.entry} + ".update";
                            return true;
                        }
                    }
                    return false;
                }
            ));

            R_UNLESS(fs.FileExists(temp_path), Result_MainFailedToDownloadUpdate);

            std::vector<u8> data;
            R_TRY(fs.read_entire_file(temp_path, data));
            R_TRY(nro_verify(data));
            R_TRY(fs.read_entire_file(payload_temp, data));
            R_UNLESS(HasModProtection(data), Result_MainFailedToDownloadUpdate);
            for (const auto& art : UPDATE_ARTWORK) {
                R_TRY(fs.read_entire_file(fs::FsPath{art.entry} + ".update", data));
                R_UNLESS(data.size() == art.size, Result_MainFailedToDownloadUpdate);
            }
            R_TRY(pbox->ShouldExitResult());

            // Validate the complete package first. Install protection before the new app;
            // the older app also works with this payload if its update fails.
            auto replace = [&fs](const fs::FsPath& source, const fs::FsPath& target) -> Result {
                const auto previous = target + ".previous";
                const bool existed = fs.FileExists(target);
                if (fs.FileExists(previous)) R_TRY(fs.DeleteFile(previous));
                if (existed) R_TRY(fs.RenameFile(target, previous));
                const Result rc = fs.RenameFile(source, target);
                if (R_FAILED(rc)) {
                    if (existed) fs.RenameFile(previous, target);
                    return rc;
                }
                if (existed) fs.DeleteFile(previous);
                return 0;
            };
            R_TRY(replace(payload_temp, payload_path));
            for (const auto& art : UPDATE_ARTWORK) {
                R_TRY(replace(fs::FsPath{art.entry} + ".update", art.entry));
            }
            R_TRY(replace(temp_path, app_path));
            // Only record success after every file has been installed.
            R_TRY(replace(receipt_temp, UPDATE_RECEIPT_PATH));
            return fs.Commit();
        },
        [update_version](Result rc) {
            if (rc == Result_TransferCancelled) return;
            if (R_FAILED(rc)) {
                App::Push<ui::ErrorBox>(rc, "Failed to update MM HATS INSTALLER.");
                return;
            }

            App::Push<ui::OptionBox>(
                "MM HATS INSTALLER " + update_version + " was installed.\nRestart now?",
                "Later"_i18n,
                "Restart"_i18n,
                1,
                [](auto index) {
                    if (index && *index == 1) {
                        App::ExitRestart();
                    }
                }
            );
        }
    );
}

} // namespace sphaira::ui::menu::hats
