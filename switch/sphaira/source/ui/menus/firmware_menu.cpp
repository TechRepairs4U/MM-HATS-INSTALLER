#include "ui/menus/firmware_menu.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/menus/filebrowser.hpp"

#include "app.hpp"
#include "log.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "yyjson_helper.hpp"
#include "hats_version.hpp"
#include "threaded_file_transfer.hpp"
#include "ams_su.h"
#include "firmware_version.hpp"

#include <yyjson.h>
#include <cstring>
#include <sstream>
#include <memory>
#include <functional>
#include <algorithm>
#include <cctype>

namespace sphaira::ui::menu::hats {

namespace {

constexpr const char* CACHE_PATH = "/switch/mm-tools/cache/hats";
constexpr const char* RELEASES_CACHE = "/switch/mm-tools/cache/hats/firmware_releases.json";
constexpr const char* FUSES_CACHE = "/switch/mm-tools/cache/hats/fuses.json";
constexpr const char* FUSES_URL = "https://raw.githubusercontent.com/sthetix/FuseNCA/master/fuses.json";
constexpr const char* DOWNLOAD_TEMP = "/switch/mm-tools/cache/hats/firmware.zip";
constexpr const char* FIRMWARE_DEST = "/firmware";
constexpr size_t UPDATE_TASK_BUFFER_SIZE = 0x100000;

struct FirmwareValidation {
    AmsSuUpdateInformation info{};
    AmsSuUpdateValidationInfo validation{};
};

std::string FormatFirmwareVersion(u32 version) {
    return std::to_string((version >> 26) & 0x1f) + "." +
           std::to_string((version >> 20) & 0x1f) + "." +
           std::to_string((version >> 16) & 0xf);
}

std::string BuildFirmwareServicePath(const fs::FsPath& path) {
    std::string service_path = path.s;
    if (service_path.empty()) {
        service_path = FIRMWARE_DEST;
    }
    if (service_path.back() != '/') {
        service_path += '/';
    }
    return service_path;
}

Result ValidateFirmware(FirmwareValidation* out, const fs::FsPath& path) {
    Result rc = amssuInitialize();
    if (R_FAILED(rc)) return rc;
    ON_SCOPE_EXIT(amssuExit());

    const auto service_path = BuildFirmwareServicePath(path);
    R_TRY(amssuGetUpdateInformation(&out->info, service_path.c_str()));
    R_TRY(amssuValidateUpdate(&out->validation, service_path.c_str()));
    return out->validation.result;
}

Result InstallValidatedFirmware(ProgressBox* pbox, bool use_exfat, const fs::FsPath& path) {
    Result rc = amssuInitialize();
    if (R_FAILED(rc)) return rc;
    ON_SCOPE_EXIT(amssuExit());

    const auto service_path = BuildFirmwareServicePath(path);
    pbox->NewTransfer("Setting up system update...");
    R_TRY(amssuSetupUpdate(nullptr, UPDATE_TASK_BUFFER_SIZE, service_path.c_str(), use_exfat));

    AsyncResult prepare{};
    R_TRY(amssuRequestPrepareUpdate(&prepare));
    ON_SCOPE_EXIT(asyncResultClose(&prepare));
    pbox->NewTransfer("Preparing system update...");

    while (true) {
        rc = asyncResultWait(&prepare, 0);
        if (R_FAILED(rc) && rc != 0xea01) return rc;
        if (R_SUCCEEDED(rc)) R_TRY(asyncResultGet(&prepare));

        bool prepared = false;
        R_TRY(amssuHasPreparedUpdate(&prepared));
        if (prepared) break;

        NsSystemUpdateProgress progress{};
        R_TRY(amssuGetPrepareUpdateProgress(&progress));
        pbox->UpdateTransfer(progress.current_size, progress.total_size);
        svcSleepThread(50'000'000);
    }

    pbox->NewTransfer("Applying system update...");
    return amssuApplyPreparedUpdate();
}

Result RequestFirmwareReboot() {
    Result rc = spsmInitialize();
    log_write("firmware: spsm initialize result: 0x%X\n", rc);
    if (R_FAILED(rc)) return rc;

    rc = spsmShutdown(true);
    log_write("firmware: spsm reboot result: 0x%X\n", rc);
    if (R_SUCCEEDED(rc)) {
        while (true) svcSleepThread(86400000000000ULL);
    }

    spsmExit();
    return rc;
}

std::string GetFirmwareTargetName() {
    bool emummc = false;
    Result rc = splInitialize();
    if (R_SUCCEEDED(rc)) {
        u64 value{};
        if (R_SUCCEEDED(splGetConfig((SplConfigItem)65007, &value))) emummc = value != 0;
        splExit();
    }
    return emummc ? "emuMMC" : "sysMMC";
}

struct LocalFirmwarePicker final : filebrowser::Base {
    using Callback = std::function<void(const fs::FsPath& path)>;
    using CancelCallback = std::function<void()>;

    explicit LocalFirmwarePicker(const Callback& callback, const CancelCallback& cancel_callback = [](){})
    : Base{MenuFlag_None, filebrowser::FsOption_Picker}
    , m_callback{callback}
    , m_cancel_callback{cancel_callback} {
        SetTitle("Select Firmware Folder"_i18n);
        SetAction(Button::X, Action{"Select"_i18n, [this](){
            SelectCurrentFolder();
        }});
        SetAction(Button::B, Action{"Back"_i18n, [this](){
            m_cancel_callback();
            SetPop();
        }});
    }

private:
    void OnClick(filebrowser::FsView* view, const filebrowser::FsEntry& fs_entry, const filebrowser::FileEntry& entry, const fs::FsPath& path) override {
        if (entry.type == FsDirEntryType_Dir) {
            m_callback(path);
            SetPop();
            return;
        }

        App::Push<ErrorBox>(0x1, "Please select a folder containing extracted firmware files.");
    }

    void SelectCurrentFolder() {
        const auto path = view->m_path;
        if (path.empty()) {
            App::Push<ErrorBox>(0x1, "Please select a folder containing extracted firmware files.");
            return;
        }
        m_callback(path);
        SetPop();
    }

private:
    const Callback m_callback;
    const CancelCallback m_cancel_callback;
};

void from_json(yyjson_val* json, FirmwareEntry& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(tag_name);
        JSON_SET_STR(name);
        JSON_SET_STR(published_at);
        JSON_SET_BOOL(prerelease);

        case cexprHash("assets"): {
            if (yyjson_is_arr(val)) {
                size_t idx, max;
                yyjson_val* hit;
                yyjson_arr_foreach(val, idx, max, hit) {
                    if (!yyjson_is_obj(hit)) continue;

                    auto name_val = yyjson_obj_get(hit, "name");
                    auto url_val = yyjson_obj_get(hit, "browser_download_url");
                    auto size_val = yyjson_obj_get(hit, "size");

                    if (name_val && url_val) {
                        const char* name_str = yyjson_get_str(name_val);
                        if (name_str && strstr(name_str, ".zip")) {
                            e.asset_name = name_str;
                            e.download_url = yyjson_get_str(url_val);
                            if (size_val) {
                                e.size = yyjson_get_uint(size_val);
                            }
                            break;
                        }
                    }
                }
            }
        } break;
    );
}

void from_json(const fs::FsPath& path, std::vector<FirmwareEntry>& entries) {
    JSON_INIT_VEC_FILE(path, nullptr, nullptr);
    if (yyjson_is_arr(json)) {
        JSON_ARR_ITR(entries);
    }
}

void from_json(yyjson_val* json, FuseEntry& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(version);
        JSON_SET_UINT(fuses_production);
    );
}

void from_json(const fs::FsPath& path, std::vector<FuseEntry>& entries) {
    JSON_INIT_VEC_FILE(path, nullptr, nullptr);
    // Get the "data" array from the JSON
    auto data_val = yyjson_obj_get(json, "data");
    if (data_val && yyjson_is_arr(data_val)) {
        json = data_val;
        JSON_ARR_ITR(entries);
    }
}

auto DownloadAndExtract(ProgressBox* pbox, const FirmwareEntry& release) -> Result {
    fs::FsNativeSd fs;
    Result fs_result = fs.GetFsOpenResult();
    log_write("firmware: FsNativeSd initialization result: 0x%X\n", fs_result);
    R_TRY(fs_result);

    // Clean up firmware directory if it exists, then recreate it
    log_write("firmware: cleaning firmware directory: %s\n", FIRMWARE_DEST);
    if (fs.DirExists(FIRMWARE_DEST)) {
        Result rc = fs.DeleteDirectoryRecursively(FIRMWARE_DEST);
        if (R_FAILED(rc)) {
            log_write("firmware: failed to delete firmware directory: 0x%X\n", rc);
            return rc;
        } else {
            log_write("firmware: successfully deleted firmware directory\n");
        }
    }
    R_TRY(fs.CreateDirectoryRecursively(FIRMWARE_DEST));

    // Ensure cache directory exists
    log_write("firmware: creating cache directory %s\n", CACHE_PATH);
    R_TRY(fs.CreateDirectoryRecursively(CACHE_PATH));
    ON_SCOPE_EXIT(fs.DeleteFile(DOWNLOAD_TEMP));

    // Clean up any existing temp file
    if (fs.FileExists(DOWNLOAD_TEMP)) {
        log_write("firmware: deleting existing temp file %s\n", DOWNLOAD_TEMP);
        Result del_result = fs.DeleteFile(DOWNLOAD_TEMP);
        log_write("firmware: temp file deletion result: 0x%X\n", del_result);
    }

    // Download the ZIP
    if (!pbox->ShouldExit()) {
        pbox->NewTransfer("Downloading " + release.asset_name);
        log_write("firmware: starting download of %s\n", release.download_url.c_str());
        log_write("firmware: release asset: %s (size: %zu bytes)\n",
                  release.asset_name.c_str(), release.size);

        const auto result = curl::Api().ToFile(
            curl::Url{release.download_url},
            curl::Path{DOWNLOAD_TEMP},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );

        if (!result.success) {
            log_write("firmware: download failed!\n");
            R_UNLESS(result.success, 0x1);
        }

        // Verify downloaded file exists
        bool download_exists = fs.FileExists(DOWNLOAD_TEMP);
        log_write("firmware: download completed, file exists: %s\n", download_exists ? "yes" : "no");
    }

    // Extract to /firmware
    if (!pbox->ShouldExit()) {
        pbox->NewTransfer("Extracting to /firmware...");
        log_write("firmware: starting extraction to %s\n", FIRMWARE_DEST);

        Result extract_result = thread::TransferUnzipAll(pbox, DOWNLOAD_TEMP, &fs, FIRMWARE_DEST);
        log_write("firmware: extraction result: 0x%X\n", extract_result);

        if (R_FAILED(extract_result)) {
            log_write("firmware: extraction failed!\n");
            return extract_result;
        }

        // Commit file system changes
        log_write("firmware: committing file system changes\n");
        Result commit_result = fs.Commit();
        log_write("firmware: commit result: 0x%X\n", commit_result);
        R_TRY(commit_result);
    }

    // Clean up temp file
    log_write("firmware: cleaning up temp file %s\n", DOWNLOAD_TEMP);
    Result cleanup_result = fs.DeleteFile(DOWNLOAD_TEMP);
    log_write("firmware: temp file cleanup result: 0x%X\n", cleanup_result);

    R_TRY(pbox->ShouldExitResult());
    log_write("firmware: extraction complete\n");
    return 0;
}

} // namespace

FirmwareMenu::FirmwareMenu(bool open_local_picker) : MenuBase{"Firmware Releases", MenuFlag_None}
, m_open_local_picker{open_local_picker}
, m_local_only{open_local_picker} {
    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_PATH);

    m_current_firmware = sphaira::hats::getSystemFirmware();
    m_running_build = sphaira::hats::getRunningBuildInfo();

    this->SetActions(
        std::make_pair(Button::A, Action{"Download"_i18n, [this](){
            if (!m_releases.empty() && !m_loading) {
                DownloadFirmware();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_loaded = false;
            FetchReleases();
        }}),
        std::make_pair(Button::Y, Action{"Local"_i18n, [this](){
            SelectLocalFirmware();
        }})
    );

    const Vec4 v{75, GetY() + 1.f + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

FirmwareMenu::~FirmwareMenu() {
}

void FirmwareMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_releases.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_releases.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect::Focus);
                SetIndex(i);
            }
        });
    }
}

void FirmwareMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    // Draw current firmware info
    const auto running_build = sphaira::hats::DescribeRunningBuild(m_running_build);
    if (m_current_fuse_count >= 0) {
        gfx::drawTextArgs(vg, 80.f, GetY() + 10.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Current Firmware: %s | %s | Fuses: %d", m_current_firmware.c_str(), running_build.c_str(), m_current_fuse_count);
    } else {
        gfx::drawTextArgs(vg, 80.f, GetY() + 10.f, 18.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Current Firmware: %s | %s", m_current_firmware.c_str(), running_build.c_str());
    }

    if (m_loading) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Loading releases...");
        return;
    }

    if (!m_error_message.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_ERROR),
            "%s", m_error_message.c_str());
        return;
    }

    if (m_releases.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No releases found");
        return;
    }

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_releases.size(), [this](auto* vg, auto* theme, auto& v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& release = m_releases[i];

        auto text_id = ThemeEntryID_TEXT;
        if (m_index == i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_releases.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        // Format date
        std::string date = release.published_at.substr(0, 10);

        // Display name - typically the firmware version
        std::string display_name = release.name.empty() ? release.tag_name : release.name;
        if (release.prerelease) {
            display_name += " (Pre-Release)";
        }

        // Check if this would be a downgrade
        bool is_downgrade = IsDowngrade(release.tag_name);
        if (is_downgrade) {
            display_name += " [DOWNGRADE]";
        }

        auto name_color = is_downgrade ? ThemeEntryID_ERROR : text_id;

        gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(name_color),
            "[%s] %s", date.c_str(), display_name.c_str());

        // Fuse count and size on the right
        float right_x = x + w - text_xoffset;
        if (release.size > 0) {
            float size_mb = release.size / 1024.0f / 1024.0f;
            gfx::drawTextArgs(vg, right_x, y + h / 2.f, 16.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%.1f MB", size_mb);
        }

        // Show fuse count if available, otherwise show ?
        auto fuse_color = (is_downgrade) ? ThemeEntryID_ERROR : ThemeEntryID_TEXT_INFO;
        if (release.fuse_count >= 0) {
            gfx::drawTextArgs(vg, right_x - 100.f, y + h / 2.f, 16.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                theme->GetColour(fuse_color),
                "Fuses: %d", release.fuse_count);
        } else {
            gfx::drawTextArgs(vg, right_x - 100.f, y + h / 2.f, 16.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "Fuses: ?");
        }
    });
}

void FirmwareMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    m_current_firmware = sphaira::hats::getSystemFirmware();
    m_running_build = sphaira::hats::getRunningBuildInfo();

    if (m_open_local_picker) {
        m_open_local_picker = false;
        SelectLocalFirmware();
        return;
    }

    if (m_local_only) {
        if (!m_local_selection_started) {
            SetPop();
        }
        return;
    }

    if (!m_loaded && !m_loading) {
        FetchReleases();
    }
    // Always try to fetch fuses (non-fatal if fails)
    FetchFuses();
}

void FirmwareMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
    UpdateSubheading();
}

void FirmwareMenu::FetchReleases() {
    m_loading = true;
    m_error_message.clear();
    m_releases.clear();

    // Get firmware URL from config
    auto app = App::GetApp();
    const std::string firmware_url = app->m_firmware_url.Get();

    curl::Api().ToFileAsync(
        curl::Url{firmware_url},
        curl::Path{RELEASES_CACHE},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::Header{
            {"Accept", "application/vnd.github+json"},
        },
        curl::OnComplete{[this](auto& result) {
            m_loading = false;
            m_loaded = true;

            if (!result.success) {
                m_error_message = "Failed to fetch releases. Check your internet connection.";
                log_write("firmware: failed to fetch releases\n");
                App::Push<OptionBox>(
                    "Failed to fetch firmware releases.\n\nUse a local extracted firmware folder instead?",
                    "Cancel"_i18n, "Local Folder"_i18n, 1,
                    [this](auto op_index) {
                        if (op_index && *op_index == 1) {
                            SelectLocalFirmware();
                        }
                    });
                return false;
            }

            from_json(result.path, m_releases);

            // Populate fuse counts for releases (if fuse data is already loaded)
            for (auto& release : m_releases) {
                release.fuse_count = GetFuseCount(release.tag_name);
            }

            if (m_releases.empty()) {
                m_error_message = "No releases found.";
            } else {
                log_write("firmware: loaded %zu releases\n", m_releases.size());
                SetIndex(0);
            }

            return true;
        }}
    );
}

void FirmwareMenu::FetchFuses() {
    // Check if already loaded
    if (m_fuses_loaded) {
        return;
    }

    curl::Api().ToFileAsync(
        curl::Url{FUSES_URL},
        curl::Path{FUSES_CACHE},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this](auto& result) {
            if (!result.success) {
                log_write("firmware: failed to fetch fuses.json\n");
                // Non-fatal, continue without fuse data
                return false;
            }

            std::vector<FuseEntry> fuse_entries;
            from_json(result.path, fuse_entries);

            // Build the fuse map
            m_fuse_map.clear();
            for (const auto& entry : fuse_entries) {
                m_fuse_map[entry.version] = static_cast<int>(entry.fuses_production);
            }

            m_fuses_loaded = true;
            log_write("firmware: loaded %zu fuse entries\n", m_fuse_map.size());

            // Get current firmware's fuse count
            m_current_fuse_count = GetFuseCount(m_current_firmware);
            log_write("firmware: current firmware %s has %d fuses\n",
                      m_current_firmware.c_str(), m_current_fuse_count);

            // Update releases with fuse counts
            for (auto& release : m_releases) {
                release.fuse_count = GetFuseCount(release.tag_name);
            }

            return true;
        }}
    );
}

int FirmwareMenu::GetFuseCount(const std::string& version) {
    auto it = m_fuse_map.find(ExtractFirmwareVersion(version));
    if (it != m_fuse_map.end()) {
        return it->second;
    }
    return -1; // Unknown
}

void FirmwareMenu::DownloadFirmware() {
    if (m_releases.empty() || m_index >= (s64)m_releases.size()) {
        return;
    }

    const auto& release = m_releases[m_index];
    std::string display_name = release.name.empty() ? release.tag_name : release.name;

    // A valid /firmware directory can be reused, including after rebooting
    // from sysMMC into emuMMC. Avoid downloading the same package twice.
    fs::FsNativeSd fs;
    if (fs.DirExists(FIRMWARE_DEST)) {
        CheckCachedFirmware(release, display_name);
        return;
    }

    const bool is_downgrade = IsDowngrade(release.tag_name);
    const bool has_different_fuse_count = HasDifferentFuseCount(release.tag_name);

    std::string message = "Download firmware " + display_name + "?\n\n";
    message += "Firmware will be extracted to /firmware.";
    if (is_downgrade || has_different_fuse_count) {
        message = is_downgrade ? "WARNING: This is a DOWNGRADE!\n\n" : "WARNING: Firmware fuse count differs!\n\n";
        message += "Download firmware " + display_name + "?\n\n";
        message += "Firmware will be extracted to /firmware.";
    }

    message += "\nCurrent firmware: " + m_current_firmware;
    message += "\nRunning: " + sphaira::hats::DescribeRunningBuild(m_running_build);

    App::Push<OptionBox>(
        message,
        "Cancel"_i18n, is_downgrade ? "Downgrade" : "Download", 1,
        [this, release, display_name](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            App::Push<ProgressBox>(0, "Downloading"_i18n, display_name,
                [release](auto pbox) -> Result {
                    return DownloadAndExtract(pbox, release);
                },
                [this, display_name](Result rc) {
                    if (rc == Result_TransferCancelled) return;
                    if (R_SUCCEEDED(rc)) {
                        PromptInstallFirmware(display_name, FIRMWARE_DEST);
                    } else {
                        App::Push<ErrorBox>(rc, "Failed to download " + display_name);
                    }
                }
            );
        }
    );
}

void FirmwareMenu::CheckCachedFirmware(const FirmwareEntry& release, const std::string& display_name) {
    auto validation = std::make_shared<FirmwareValidation>();
    App::Push<ProgressBox>(0, "Validating"_i18n, "Cached firmware",
        [validation](auto pbox) -> Result {
            pbox->NewTransfer("Checking /firmware...");
            R_TRY(ValidateFirmware(validation.get(), FIRMWARE_DEST));
            return pbox->ShouldExitResult();
        },
        [this, release, display_name, validation](Result rc) {
            if (rc == Result_TransferCancelled) return;
            if (R_SUCCEEDED(rc)) {
                const auto cached_version = FormatFirmwareVersion(validation->info.version);
                std::string message = "Valid cached firmware " + cached_version + " found.\n\n";
                message += "Install it on " + GetFirmwareTargetName() + " without downloading again?";
                App::Push<OptionBox>(message, "Download Selected"_i18n, "Install Cached"_i18n, 1,
                    [this, release, display_name, cached_version](auto op_index) {
                        if (!op_index) return;
                        if (*op_index == 1) {
                            PromptInstallFirmware(cached_version);
                        } else {
                            // Remove only the extracted cache; DownloadAndExtract recreates it.
                            const auto delete_rc = fs::FsNativeSd().DeleteDirectoryRecursively(FIRMWARE_DEST);
                            if (R_FAILED(delete_rc)) {
                                App::Push<ErrorBox>(delete_rc, "Failed to clear cached firmware");
                                return;
                            }
                            DownloadFirmware();
                        }
                    });
            } else {
                log_write("firmware: cached firmware validation failed: 0x%X\n", rc);
                App::Push<OptionBox>(
                    "The cached firmware is incomplete or invalid.\n\nDownload " + display_name + " again?",
                    "Cancel"_i18n, "Redownload"_i18n, 1,
                    [this](auto op_index) {
                        if (op_index && *op_index == 1) {
                            const auto delete_rc = fs::FsNativeSd().DeleteDirectoryRecursively(FIRMWARE_DEST);
                            if (R_FAILED(delete_rc)) {
                                App::Push<ErrorBox>(delete_rc, "Failed to clear cached firmware");
                                return;
                            }
                            DownloadFirmware();
                        }
                    });
            }
        });
}

void FirmwareMenu::SelectLocalFirmware() {
    App::Push<LocalFirmwarePicker>(
        [this](const fs::FsPath& path) {
            m_local_selection_started = true;
            UseLocalFirmware(path);
        },
        [this]() {
            OnLocalFirmwareCancelled();
        });
}

void FirmwareMenu::OnLocalFirmwareCancelled() {
    if (m_local_only) {
        SetPop();
    }
}

void FirmwareMenu::UseLocalFirmware(const fs::FsPath& path) {
    auto validation = std::make_shared<FirmwareValidation>();
    App::Push<ProgressBox>(0, "Validating"_i18n, "Local firmware",
        [validation, path](auto pbox) -> Result {
            pbox->NewTransfer("Checking selected folder...");
            R_TRY(ValidateFirmware(validation.get(), path));
            return pbox->ShouldExitResult();
        },
        [this, path, validation](Result rc) {
            if (rc == Result_TransferCancelled) return;
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Selected folder is not valid firmware");
                return;
            }

            PromptInstallFirmware(FormatFirmwareVersion(validation->info.version), path);
        });
}

void FirmwareMenu::PromptInstallFirmware(const std::string& display_name, const fs::FsPath& path) {
    auto validation = std::make_shared<FirmwareValidation>();
    App::Push<ProgressBox>(0, "Validating"_i18n, display_name,
        [validation, path](auto pbox) -> Result {
            pbox->NewTransfer("Validating firmware contents...");
            R_TRY(ValidateFirmware(validation.get(), path));
            return pbox->ShouldExitResult();
        },
        [this, display_name, path, validation](Result rc) {
            if (rc == Result_TransferCancelled) return;
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Firmware validation failed");
                return;
            }

            const auto version = FormatFirmwareVersion(validation->info.version);
            const bool use_exfat = validation->info.exfat_supported &&
                                   R_SUCCEEDED(validation->validation.exfat_result);

            const auto show_install_prompt = [this, display_name, path, version, use_exfat]() {
                std::string message = "Install firmware " + version + " on " + GetFirmwareTargetName() + "?\n\n";
                if (IsDowngrade(version) || HasDifferentFuseCount(version)) {
                    message = IsDowngrade(version) ? "WARNING: This is a DOWNGRADE!\n\n" : "WARNING: Firmware fuse count differs!\n\n";
                    message += "Install firmware " + version + " on " + GetFirmwareTargetName() + "?\n\n";
                }
                message += "Current firmware: " + m_current_firmware + "\n";
                message += "Running: " + sphaira::hats::DescribeRunningBuild(m_running_build) + "\n\n";
                message += use_exfat ? "FAT32 + exFAT support\n" : "FAT32 support only\n";
                message += "Do not power off the console during installation.";
                App::Push<OptionBox>(message, "Cancel"_i18n, "Install"_i18n, 1,
                    [this, display_name, path](auto op_index) {
                        if (op_index && *op_index == 1) {
                            m_local_only = false;
                            m_local_selection_started = false;
                            InstallFirmware(display_name, path);
                        } else if (m_local_only) {
                            SetPop();
                        }
                    });
            };

            show_install_prompt();
        });
}

void FirmwareMenu::InstallFirmware(const std::string& display_name, const fs::FsPath& path) {
    App::Push<ProgressBox>(0, "Updating Firmware"_i18n, display_name,
        [path](auto pbox) -> Result {
            FirmwareValidation validation{};
            R_TRY(ValidateFirmware(&validation, path));
            const bool use_exfat = validation.info.exfat_supported &&
                                   R_SUCCEEDED(validation.validation.exfat_result);
            return InstallValidatedFirmware(pbox, use_exfat, path);
        },
        [](Result rc) {
            if (rc == Result_TransferCancelled) return;
            if (R_FAILED(rc)) {
                App::Push<ErrorBox>(rc, "Firmware update failed");
                return;
            }
            App::Push<OptionBox>(
                "Firmware update applied successfully.\n\nReboot now?",
                "Later"_i18n, "Reboot"_i18n, 1,
                [](auto op_index) {
                    if (op_index && *op_index == 1) {
                        const Result rc = RequestFirmwareReboot();
                        App::Push<ErrorBox>(rc, "Failed to reboot after firmware update");
                    }
                });
        }, false);
}

void FirmwareMenu::UpdateSubheading() {
    const auto index = m_releases.empty() ? 0 : m_index + 1;
    this->SetSubHeading(std::to_string(index) + " / " + std::to_string(m_releases.size()));
}

bool FirmwareMenu::IsDowngrade(const std::string& target_version) {
    return isVersionLower(ExtractFirmwareVersion(target_version), m_current_firmware);
}

bool FirmwareMenu::HasDifferentFuseCount(const std::string& target_version) {
    if (m_current_fuse_count < 0) {
        return IsDowngrade(target_version);
    }

    const int target_fuses = GetFuseCount(target_version);
    if (target_fuses < 0) {
        return IsDowngrade(target_version);
    }

    return target_fuses != m_current_fuse_count;
}

} // namespace sphaira::ui::menu::hats
