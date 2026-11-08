#include "ui/menus/hats_pack_menu.hpp"

#include <algorithm>

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"
#include "ui/warning_box.hpp"
#include "ui/menus/pack_details_menu.hpp"

#include "app.hpp"
#include "log.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "i18n.hpp"
#include "yyjson_helper.hpp"
#include "threaded_file_transfer.hpp"
#include "utils/utils.hpp"
#include "hats_contents_policy.hpp"

#include <yyjson.h>
#include <dirent.h>
#include <cstring>
#include <ctime>
#include <curl/curl.h>

namespace sphaira::ui::menu::hats {

namespace {

constexpr const char* CACHE_PATH = "/switch/mm-tools/cache/hats";
constexpr const char* RELEASES_CACHE = "/switch/mm-tools/cache/hats/releases.json";

// HATS paths are now configurable via config.ini under [hats] section
// Defaults: installer_payload=/switch/mm-tools/hats-installer.bin
//           staging_path=/hats-staging

void from_json(const fs::FsPath& path, std::vector<ReleaseEntry>& entries) {
    auto* doc = yyjson_read_file(path.s, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) return;
    ON_SCOPE_EXIT(yyjson_doc_free(doc));
    entries = ParsePackReleases(yyjson_doc_get_root(doc));
}

constexpr const char* BACKUP_PATH = "/sdbackup";

void LoadContentsPolicy(const fs::FsPath& staging_path, mm_contents_policy& policy) {
    // Installed and incoming manifests cover removed, existing and new modules.
    const fs::FsPath manifests[] = {"/manifest.json", staging_path + "/manifest.json"};
    for (const auto& path : manifests) {
        auto* doc = yyjson_read_file(path.s, 0, nullptr, nullptr);
        if (!doc) continue;
        AddManifestSysmodules(policy, yyjson_doc_get_root(doc));
        yyjson_doc_free(doc);
    }
}

constexpr Result Result_ExclusionsUnavailable = 0x66A;
constexpr Result Result_ExclusionsInvalid = 0x66B;

auto BackupExistingFolders(ProgressBox* pbox, const mm_contents_policy& policy) -> Result;

auto FetchGithubExclusions(ProgressBox* pbox, mm_contents_policy& policy, std::string& error) -> Result {
    pbox->NewTransfer("Checking GitHub exclusion list...");
    error.clear();
    // Themes and the NSP bootstrap unmount RomFS after reading their assets.
    // libcurl opens CAINFO during the TLS handshake, so own a mount until the
    // request and its handle have both finished, including all failure paths.
    const auto mount_result = romfsInit();
    if (R_FAILED(mount_result)) {
        char detail[96];
        std::snprintf(detail, sizeof(detail), "Cannot mount bundled certificates (0x%X).", mount_result);
        error = detail;
        hats_log_write("hats: exclusions: %s\n", error.c_str());
        return Result_ExclusionsUnavailable;
    }
    ON_SCOPE_EXIT(romfsExit());
    struct Transfer {
        ProgressBox* pbox;
        std::vector<char> data;
    } transfer{pbox, {}};
    auto* request = curl_easy_init();
    error = "Could not initialise the exclusion-list request.";
    R_UNLESS(request, Result_ExclusionsUnavailable);
    ON_SCOPE_EXIT(curl_easy_cleanup(request));
    auto* headers = curl_slist_append(nullptr, "Cache-Control: no-cache, no-store, max-age=0");
    error = "Could not allocate the exclusion-list request headers.";
    R_UNLESS(headers, Result_ExclusionsUnavailable);
    ON_SCOPE_EXIT(curl_slist_free_all(headers));
    // A new URL and no local HTTP cache make edits visible on the next install.
    const auto url = std::string(HATS_EXCLUSIONS_URL) + "?mm_install=" + std::to_string(armGetSystemTick());
    char curl_error[CURL_ERROR_SIZE]{};
    curl_easy_setopt(request, CURLOPT_ERRORBUFFER, curl_error);
    curl_easy_setopt(request, CURLOPT_URL, url.c_str());
    curl_easy_setopt(request, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(request, CURLOPT_USERAGENT, "MM-HATS-INSTALLER");
    curl_easy_setopt(request, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(request, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(request, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(request, CURLOPT_CAINFO, "romfs:/hats-ca-bundle.pem");
    curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(request, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(request, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(HATS_EXCLUSIONS_MAX_JSON));
    curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(request, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(request, CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(request, CURLOPT_XFERINFOFUNCTION,
        +[](void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            return static_cast<Transfer*>(context)->pbox->ShouldExit() ? 1 : 0;
        });
    curl_easy_setopt(request, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(request, CURLOPT_WRITEFUNCTION,
        +[](char* data, size_t size, size_t count, void* context) -> size_t {
            auto& state = *static_cast<Transfer*>(context);
            if (state.pbox->ShouldExit() || (size && count > HATS_EXCLUSIONS_MAX_JSON / size)) return 0;
            const size_t bytes = size * count;
            if (bytes > HATS_EXCLUSIONS_MAX_JSON - state.data.size()) return 0;
            state.data.insert(state.data.end(), data, data + bytes);
            return bytes;
        });
    const auto result = curl_easy_perform(request);
    long status = 0;
    curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status);
    R_TRY(pbox->ShouldExitResult());
    if (result != CURLE_OK || status != 200) {
        hats_log_write("hats: exclusions: curl %d (%s), HTTP %ld: %s\n",
            static_cast<int>(result), curl_easy_strerror(result), status, curl_error);
        error = "GitHub exclusion list: curl " + std::to_string(result);
        if (status) error += ", HTTP " + std::to_string(status);
        error += ".\n";
        std::string detail = *curl_error ? curl_error : curl_easy_strerror(result);
        if (detail.size() > 150) detail = detail.substr(0, 147) + "...";
        error += detail;
        return Result_ExclusionsUnavailable;
    }
    R_UNLESS(ParseGithubExclusions(policy, transfer.data.data(), transfer.data.size()), Result_ExclusionsInvalid);
    error.clear();
    hats_log_write("hats: fetched %u GitHub exclusions\n", policy.preserve_count);
    return 0;
}

auto PrepareProtectedInstaller(ProgressBox* pbox, fs::FsNativeSd& fs, const fs::FsPath& payload, std::string& error) -> Result {
    if (const auto rc = App::GetApp()->m_bundled_assets_result; R_FAILED(rc)) {
        error = "The app could not repair its files. Check free space and the SD card, then reopen the app.";
        return rc;
    }
    R_TRY(fs.GetFsOpenResult());
    // Invalidate the previous attempt before any network request or backup.
    if (fs.FileExists(MM_CONTENTS_POLICY_PATH)) R_TRY(fs.DeleteFile(MM_CONTENTS_POLICY_PATH));
    R_TRY(fs.Commit());
    std::vector<u8> data;
    R_TRY(fs.read_entire_file(payload, data));
    R_UNLESS(HasModProtection(data), 0x668);
    const auto staging = App::GetApp()->m_installer_staging_path.Get();
    fs::File core;
    R_TRY(fs.OpenFile((staging + "/atmosphere/package3").c_str(), FsOpenMode_Read, &core));
    s64 core_size = 0;
    R_TRY(core.GetSize(&core_size));
    R_UNLESS(core_size > 0, 0x669);
    core.Close();
    auto policy = std::make_unique<mm_contents_policy>();
    LoadContentsPolicy(staging.c_str(), *policy);
    R_TRY(FetchGithubExclusions(pbox, *policy, error));
    // Backup and the rebooted payload receive exactly the same fresh snapshot.
    if (App::GetBackupEnabled()) R_TRY(BackupExistingFolders(pbox, *policy));
    R_TRY(pbox->ShouldExitResult());
    const auto text = SerializeContentsPolicy(*policy);
    R_TRY(fs.write_entire_file(MM_CONTENTS_POLICY_PATH,
        std::span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size())));
    return fs.Commit();
}

std::string InstallerPreparationError(Result rc, const std::string& error) {
    if (rc == 0x668) return "The installer payload changed after startup. Reopen the latest MM HATS INSTALLER to repair it automatically.";
    if (rc == Result_ExclusionsUnavailable) return error + "\nInstallation has not started. Details: /hats-install.log";
    if (rc == Result_ExclusionsInvalid) return "The GitHub exclusion list is invalid or exceeds supported limits. Installation has not started.";
    return error.empty() ? "Failed to prepare the HATS installer" : error;
}

auto HasCachedZip() -> bool {
    fs::FsNativeSd fs;
    if (R_FAILED(fs.GetFsOpenResult()) || !fs.DirExists(CACHE_PATH)) {
        return false;
    }

    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(CACHE_PATH, FsDirOpenMode_ReadFiles, &dir))) {
        return false;
    }

    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(dir.ReadAll(entries))) {
        return false;
    }

    for (const auto& entry : entries) {
        if (entry.type != FsDirEntryType_File) {
            continue;
        }

        const std::string name = entry.name;
        if (name.size() > 4 && !strcasecmp(name.c_str() + name.size() - 4, ".zip")) {
            return true;
        }
    }

    return false;
}

auto CopyDirectoryRecursive(ProgressBox* pbox, fs::FsNativeSd& fs, const fs::FsPath& src, const fs::FsPath& dst, const mm_contents_policy& policy) -> Result {
    if (mm_is_protected_install_path(&policy, src.s, true)) {
        hats_log_write("hats: skipping protected game/mod/port folder in backup: %s\n", src.s);
        return 0;
    }
    // Create destination directory
    R_TRY(fs.CreateDirectory(dst));

    // Open source directory
    fs::Dir dir;
    R_TRY(fs.OpenDirectory(src, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir));

    // Read all entries
    std::vector<FsDirectoryEntry> entries;
    R_TRY(dir.ReadAll(entries));

    // Copy each entry
    for (const auto& entry : entries) {
        if (pbox->ShouldExit()) {
            break;
        }

        std::string name = entry.name;
        if (name == "." || name == "..") {
            continue;
        }

        fs::FsPath src_path = std::string(src.s) + "/" + name;
        fs::FsPath dst_path = std::string(dst.s) + "/" + name;

        if (mm_is_protected_install_path(&policy, src_path.s, entry.type == FsDirEntryType_Dir)) continue;

        if (entry.type == FsDirEntryType_Dir) {
            // Recursively copy subdirectory
            Result rc = CopyDirectoryRecursive(pbox, fs, src_path, dst_path, policy);
            if (R_FAILED(rc)) {
                hats_log_write("hats: warning - failed to copy directory %s: 0x%X, continuing...\n", static_cast<const char*>(src_path), rc);
                // Continue anyway - don't abort entire backup
            }
        } else if (entry.type == FsDirEntryType_File) {
            // Copy file
            Result rc = pbox->CopyFile(&fs, src_path, dst_path, true); // single threaded for SD to SD
            if (R_FAILED(rc)) {
                hats_log_write("hats: warning - failed to copy file %s: 0x%X, continuing...\n", static_cast<const char*>(src_path), rc);
                // Continue anyway - don't abort entire backup
            }
        }
    }

    return 0;
}

auto BackupExistingFolders(ProgressBox* pbox, const mm_contents_policy& policy) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    hats_log_write("hats: starting backup of existing folders\n");

    // Folders to backup
    const fs::FsPath folders_to_backup[] = {
        "/atmosphere",
        "/bootloader",
        "/switch"
    };

    // Create backup directory with timestamp
    time_t now = time(nullptr);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));

    // Create /sdbackup directory if it doesn't exist
    fs.CreateDirectoryRecursively(BACKUP_PATH);

    // Backup each folder
    for (size_t i = 0; i < sizeof(folders_to_backup) / sizeof(folders_to_backup[0]); i++) {
        const auto& folder = folders_to_backup[i];
        hats_log_write("hats: processing folder %zu: %s\n", i, static_cast<const char*>(folder));

        if (!fs.DirExists(folder)) {
            hats_log_write("hats: %s does not exist, skipping backup\n", static_cast<const char*>(folder));
            continue;
        }

        // Create backup path: /sdbackup/atmosphere_20231217_143000
        std::string backup_folder = std::string(BACKUP_PATH) + folder.s + "_" + timestamp;
        fs::FsPath backup_path = backup_folder.c_str();

        hats_log_write("hats: backing up %s to %s\n", static_cast<const char*>(folder), static_cast<const char*>(backup_path));

        // Clean up old backup if it exists
        if (fs.DirExists(backup_path)) {
            Result rc = fs.DeleteDirectoryRecursively(backup_path);
            if (R_FAILED(rc)) {
                hats_log_write("hats: warning - failed to delete old backup: 0x%X\n", rc);
            }
        }

        // Copy the folder recursively
        if (!pbox->ShouldExit()) {
            pbox->NewTransfer("Backing up " + std::string(folder.s));
            Result rc = CopyDirectoryRecursive(pbox, fs, folder, backup_path, policy);

            if (R_FAILED(rc)) {
                hats_log_write("hats: backup failed for %s: 0x%X\n", static_cast<const char*>(folder), rc);
                // Don't return - continue to next folder
            } else {
                hats_log_write("hats: successfully backed up %s\n", static_cast<const char*>(folder));
            }
        } else {
            hats_log_write("hats: backup cancelled for %s\n", static_cast<const char*>(folder));
        }
    }

    hats_log_write("hats: backup completed\n");
    return 0;
}

auto DownloadAndExtract(ProgressBox* pbox, const ReleaseEntry& release) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    // Get installer paths from config
    auto app = App::GetApp();
    const fs::FsPath staging_path = app->m_installer_staging_path.Get().c_str();

    // Ensure cache directory exists
    fs.CreateDirectoryRecursively(CACHE_PATH);

    // Clean up staging directory if it exists, then recreate it
    hats_log_write("hats: cleaning staging directory: %s\n", static_cast<const char*>(staging_path));
    if (fs.DirExists(staging_path)) {
        Result rc = fs.DeleteDirectoryRecursively(staging_path);
        if (R_FAILED(rc)) {
            hats_log_write("hats: failed to delete staging directory: 0x%X\n", rc);
            return rc;
        } else {
            hats_log_write("hats: successfully deleted staging directory\n");
        }
    }
    R_TRY(fs.CreateDirectoryRecursively(staging_path));

    // Build download path: use original asset name for caching
    std::string download_path = std::string(CACHE_PATH) + "/" + release.asset_name;
    hats_log_write("hats: download path: %s\n", download_path.c_str());

    // Online installs always fetch fresh bytes, even when a release reuses its
    // tag or filename. Only Cache Manager explicitly installs an offline ZIP.
    const fs::FsPath partial_path = (download_path + ".part").c_str();
    ON_SCOPE_EXIT(fs.DeleteFile(partial_path));
    if (fs.FileExists(partial_path)) R_TRY(fs.DeleteFile(partial_path));

    // Download into a partial file so interrupted transfers cannot become cached ZIPs.
    if (!pbox->ShouldExit()) {
        pbox->NewTransfer("Downloading " + release.asset_name);
        hats_log_write("hats: downloading %s\n", release.download_url.c_str());

        const auto result = curl::Api().ToFile(
            curl::Url{release.download_url},
            curl::Path{partial_path},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );

        R_TRY(pbox->ShouldExitResult());
        R_UNLESS(result.success, 0x1);
        fs::File downloaded;
        R_TRY(fs.OpenFile(partial_path, FsOpenMode_Read, &downloaded));
        s64 size = 0;
        R_TRY(downloaded.GetSize(&size));
        R_UNLESS(size > 0 && (!release.size || static_cast<u64>(size) == release.size), 0x1);
        downloaded.Close();
        if (fs.FileExists(download_path.c_str())) R_TRY(fs.DeleteFile(download_path.c_str()));
        R_TRY(fs.RenameFile(partial_path, download_path.c_str()));
    }
    R_TRY(pbox->ShouldExitResult());

    // Extract to staging directory
    if (!pbox->ShouldExit()) {
        pbox->NewTransfer("Preparing installation...");
        hats_log_write("hats: extracting to staging directory\n");

        bool download_exists = fs.FileExists(download_path.c_str());
        hats_log_write("hats: download file exists: %s\n", download_exists ? "yes" : "no");

        if (download_exists) {
            Result rc = thread::TransferUnzipAll(pbox, download_path.c_str(), &fs, staging_path,
                [&](const fs::FsPath& name, fs::FsPath& path) -> bool {
                    hats_log_write("hats: extracting file: %s -> %s\n", static_cast<const char*>(name), static_cast<const char*>(path));
                    return true;  // Extract all files
                });

            hats_log_write("hats: extraction completed with result: 0x%X\n", rc);

            if (R_FAILED(rc)) {
                hats_log_write("hats: extraction failed with error: 0x%X\n", rc);
                return rc;
            }
        } else {
            hats_log_write("hats: ERROR - download file does not exist!\n");
            return 0x2;
        }
    }

    R_TRY(pbox->ShouldExitResult());

    // Backup runs at launch after fetching the current GitHub exclusions.

    // Commit file system changes
    if (!pbox->ShouldExit()) {
        hats_log_write("hats: committing file system changes\n");
        Result commit_result = fs.Commit();
        hats_log_write("hats: commit result: 0x%X\n", commit_result);
        R_TRY(commit_result);
    }

    // Verify staging files exist
    hats_log_write("hats: verifying staging files...\n");
    // Build paths dynamically based on staging_path
    fs::FsPath staging_dirs[] = {
        staging_path + "/atmosphere",
        staging_path + "/bootloader",
        staging_path + "/switch"
    };
    for (const auto& dir : staging_dirs) {
        bool exists = fs.DirExists(dir);
        hats_log_write("hats: %s exists: %s\n", static_cast<const char*>(dir), exists ? "yes" : "no");
    }

    // Clean up or keep the downloaded zip based on config
    if (App::GetKeepZipsEnabled()) {
        hats_log_write("hats: keeping zip in cache: %s\n", download_path.c_str());
    } else {
        hats_log_write("hats: deleting zip: %s\n", download_path.c_str());
        fs.DeleteFile(download_path.c_str());
    }

    R_TRY(pbox->ShouldExitResult());
    hats_log_write("hats: staging complete\n");
    return 0;
}

} // namespace

PackMenu::PackMenu() : MenuBase{"HATS Pack Releases", MenuFlag_None} {
    fs::FsNativeSd fs;
    fs.CreateDirectoryRecursively(CACHE_PATH);

    // Ensure staging directory exists for the HATS installer payload
    auto app = App::GetApp();
    const fs::FsPath staging_path = app->m_installer_staging_path.Get().c_str();
    fs.CreateDirectoryRecursively(staging_path);

    // Clean up legacy installer payload paths.
    constexpr const char* OLD_PAYLOAD_PATH = "/bootloader/payloads/hats-installer.bin";
    constexpr const char* OLD_PAYLOAD_PATH2 = "/bootloader/payloads/HATS Installer.bin";

    if (fs.FileExists(OLD_PAYLOAD_PATH)) {
        hats_log_write("hats: removing old payload at /bootloader/payloads/hats-installer.bin\n");
        fs.DeleteFile(OLD_PAYLOAD_PATH);
    }
    if (fs.FileExists(OLD_PAYLOAD_PATH2)) {
        hats_log_write("hats: removing old payload at /bootloader/payloads/HATS Installer.bin\n");
        fs.DeleteFile(OLD_PAYLOAD_PATH2);
    }

    // Check and auto-revert stale swaps on menu creation (cleanup from previous sessions)
    if (utils::isPayloadSwapped()) {
        hats_log_write("hats: detected stale payload swap on menu creation, reverting\n");
        utils::revertPayloadSwap();
    }
    if (utils::isHekateAutobootActive()) {
        hats_log_write("hats: detected stale hekate autoboot on menu creation, reverting\n");
        utils::restoreHekateIni();
    }

    this->SetActions(
        std::make_pair(Button::A, Action{"Install"_i18n, [this](){
            if (!m_releases.empty() && !m_loading) {
                DownloadAndInstall();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_loaded = false;
            FetchReleases();
        }}),
        std::make_pair(Button::Y, Action{"Details"_i18n, [this](){
            if (!m_releases.empty() && !m_loading) {
                ShowReleaseDetails();
            }
        }}),
        std::make_pair(Button::L2, Action{"Cache"_i18n, [this](){
            ShowCacheManager();
        }})
    );

    const Vec4 v{75, GetY() + 1.f + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

PackMenu::~PackMenu() {
    // No swap handling needed - HATS installer handles everything
}

void PackMenu::Update(Controller* controller, TouchInfo* touch) {
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

void PackMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

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

        // Format date (published_at is like "2024-12-17T10:00:00Z")
        std::string date = release.published_at.substr(0, 10);

        // Display name
        std::string display_name = release.name.empty() ? release.tag_name : release.name;
        if (release.prerelease) {
            display_name += " (Pre-Release)";
        }

        gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "[%s] %s", date.c_str(), display_name.c_str());

        // Size on the right
        if (release.size > 0) {
            float size_mb = release.size / 1024.0f / 1024.0f;
            gfx::drawTextArgs(vg, x + w - text_xoffset, y + h / 2.f, 16.f,
                NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                theme->GetColour(ThemeEntryID_TEXT_INFO),
                "%.1f MB", size_mb);
        }
    });
}

void PackMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_loaded && !m_loading) {
        FetchReleases();
    }
}

void PackMenu::OnFocusLost() {
    // No swap handling needed - HATS installer handles everything
}

void PackMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
    UpdateSubheading();
}

void PackMenu::FetchReleases() {
    m_loading = true;
    m_error_message.clear();
    m_releases.clear();

    // Get pack URL from config
    auto app = App::GetApp();
    const std::string pack_url = app->m_pack_url.Get();

    curl::Api().ToFileAsync(
        curl::Url{pack_url},
        curl::Path{RELEASES_CACHE},
        curl::StopToken{this->GetToken()},
        curl::Header{
            {"Accept", "application/vnd.github+json"},
            {"Cache-Control", "no-cache"},
        },
        curl::OnComplete{[this](auto& result) {
            m_loading = false;
            m_loaded = true;

            if (!result.success) {
                if (HasCachedZip()) {
                    m_error_message = "Failed to fetch releases. Opening cached downloads.";
                    App::Notify("Offline: opening cached HATS zips"_i18n);
                    ShowCacheManager();
                } else {
                    m_error_message = "Failed to fetch releases. Check your internet connection.";
                }
                hats_log_write("hats: failed to fetch releases\n");
                return false;
            }

            from_json(result.path, m_releases);

            if (m_releases.empty()) {
                m_error_message = "No downloadable ZIP assets found in the latest release.";
            } else {
                hats_log_write("hats: loaded %zu releases\n", m_releases.size());
                SetIndex(0);
            }

            return true;
        }}
    );
}

void PackMenu::DownloadAndInstall() {
    if (m_releases.empty() || m_index >= (s64)m_releases.size()) {
        return;
    }

    const auto& release = m_releases[m_index];
    std::string display_name = release.name.empty() ? release.tag_name : release.name;

    // Get staging path from config for display message
    auto app = App::GetApp();
    const fs::FsPath staging_path = app->m_installer_staging_path.Get().c_str();

    // Show backup warning first (unless skipped in Advanced options)
    if (!App::GetSkipBackupWarning()) {
        App::Push<WarningBox>(
            "Make sure you have backed up\nyour SD card!",
            "Cancel"_i18n, "Continue"_i18n, 1, [this, release, display_name, staging_path](auto op_index) {
                if (!op_index || *op_index != 1) {
                    return;
                }

                // Show download/install confirmation
                const std::string confirm_msg = "Download " + display_name + "?\n\n"
                                  "Files will be extracted to " + std::string(staging_path) + ".";

                App::Push<OptionBox>(
                    confirm_msg,
                    "Cancel"_i18n, "Download"_i18n, 1, [this, release, display_name](auto op_index) {
                        if (!op_index || *op_index != 1) {
                            return;
                        }

                        App::Push<ProgressBox>(0, "Installing"_i18n, display_name,
                            [this, release](auto pbox) -> Result {
                                return DownloadAndExtract(pbox, release);
                            },
                            [this, display_name](Result rc) {
                                if (rc == Result_TransferCancelled) return;
                                if (R_SUCCEEDED(rc)) {
                                    hats_log_write("hats: download complete, ready to launch\n");
                                    // Show notification if zip was saved to cache
                                    if (App::GetKeepZipsEnabled()) {
                                        App::Notify("Zip saved to cache"_i18n);
                                    }
                                    ShowLaunchDialog();
                                } else {
                                    App::Push<ErrorBox>(rc, "Failed to download " + display_name);
                                }
                            }
                        );
                    }
                );
            }
        );
    } else {
        // Skip backup warning, go directly to download confirmation
        const std::string confirm_msg = "Download " + display_name + "?\n\n"
                          "Files will be extracted to " + std::string(staging_path) + ".";

        App::Push<OptionBox>(
            confirm_msg,
            "Cancel"_i18n, "Download"_i18n, 1, [this, release, display_name](auto op_index) {
                if (!op_index || *op_index != 1) {
                    return;
                }

                App::Push<ProgressBox>(0, "Installing"_i18n, display_name,
                    [this, release](auto pbox) -> Result {
                        return DownloadAndExtract(pbox, release);
                    },
                    [this, display_name](Result rc) {
                        if (rc == Result_TransferCancelled) return;
                        if (R_SUCCEEDED(rc)) {
                            hats_log_write("hats: download complete, ready to launch\n");
                            // Show notification if zip was saved to cache
                            if (App::GetKeepZipsEnabled()) {
                                App::Notify("Zip saved to cache"_i18n);
                            }
                            ShowLaunchDialog();
                        } else {
                            App::Push<ErrorBox>(rc, "Failed to download " + display_name);
                        }
                    }
                );
            }
        );
    }
}

void PackMenu::ShowLaunchDialog() {
    App::Push<OptionBox>(
        "HATS Pack ready!\n\nLaunch HATS installer?",
        "Back"_i18n, "Launch"_i18n, 1, [](auto op_index) {
            if (!op_index || *op_index != 1) {
                hats_log_write("hats: user chose not to launch installer\n");
                return;
            }

            hats_log_write("hats: user clicked Launch, setting up hekate autoboot...\n");

            // Get installer payload path
            auto app = App::GetApp();
            const fs::FsPath installer_payload = app->m_installer_payload.Get().c_str();
            auto preparation_error = std::make_shared<std::string>();

            // Show progress box while modifying hekate_ipl.ini
            App::Push<ProgressBox>(0, "Preparing..."_i18n, "Configuring hekate",
                [installer_payload, preparation_error](auto pbox) -> Result {
                    fs::FsNativeSd fs;

                    hats_log_write("hats: checking HATS installer at: %s\n", static_cast<const char*>(installer_payload));

                    if (!fs.FileExists(installer_payload)) {
                        hats_log_write("hats: HATS installer not found at: %s\n", static_cast<const char*>(installer_payload));
                        return 0x666; // Error code
                    }

                    R_TRY(PrepareProtectedInstaller(pbox, fs, installer_payload, *preparation_error));
                    R_TRY(pbox->ShouldExitResult());

                    // Set hekate_ipl.ini to auto-boot HATS installer
                    hats_log_write("hats: configuring hekate autoboot...\n");
                    pbox->NewTransfer("Modifying hekate_ipl.ini");
                    bool success = utils::setHekateAutobootPayload(static_cast<const char*>(installer_payload));

                    if (!success) {
                        hats_log_write("hats: failed to configure hekate autoboot\n");
                        return 0x667;
                    }

                    hats_log_write("hats: hekate configured, ready to reboot\n");
                    return 0;
                },
                [preparation_error](Result rc) {
                    if (rc == Result_TransferCancelled) return;
                    if (R_FAILED(rc)) {
                        hats_log_write("hats: configuration failed with result: 0x%X\n", rc);
                        App::Push<ErrorBox>(rc, InstallerPreparationError(rc, *preparation_error));
                        return;
                    }

                    // Configuration successful, now reboot
                    hats_log_write("hats: launching HATS installer (rebooting to hekate...)\n");

                    rc = utils::requestForcedReboot();
                    if (R_FAILED(rc)) {
                        hats_log_write("hats: reboot failed with result: 0x%X\n", rc);
                        App::Push<ErrorBox>(rc, "Failed to reboot");
                    }
                }
            );
        }
    );
}

void PackMenu::UpdateSubheading() {
    const auto index = m_releases.empty() ? 0 : m_index + 1;
    this->SetSubHeading(std::to_string(index) + " / " + std::to_string(m_releases.size()));
}

void PackMenu::ShowReleaseDetails() {
    if (m_releases.empty() || m_index >= (s64)m_releases.size()) {
        return;
    }

    const auto& release = m_releases[m_index];
    App::Push<PackDetailsMenu>(release, [this, release]() {
        // User clicked Download in details view, trigger download flow
        // Find the release again to ensure correct index
        for (size_t i = 0; i < m_releases.size(); i++) {
            if (m_releases[i].download_url == release.download_url) {
                m_index = i;
                break;
            }
        }
        DownloadAndInstall();
    });
}

void PackMenu::ShowCacheManager() {
    App::Push<CacheManagerMenu>();
}

// ============================================================================
// Cache Manager Menu Implementation
// ============================================================================

namespace {
    // Helper function to format file size
    std::string FormatFileSize(u64 size) {
        if (size < 1024) {
            return std::to_string(size) + " B";
        } else if (size < 1024 * 1024) {
            return std::to_string(size / 1024) + " KB";
        } else if (size < 1024 * 1024 * 1024) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f MB", size / 1024.0 / 1024.0);
            return std::string(buf);
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f GB", size / 1024.0 / 1024.0 / 1024.0);
            return std::string(buf);
        }
    }
}

CacheManagerMenu::CacheManagerMenu() : MenuBase{"Cached Downloads", MenuFlag_None} {
    hats_log_write("hats: opening cache manager\n");

    // Scan for cached zips
    ScanCachedZips();

    this->SetActions(
        std::make_pair(Button::A, Action{"Reinstall"_i18n, [this](){
            if (!m_cached_zips.empty()) {
                ReinstallFromCache();
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Delete"_i18n, [this](){
            if (!m_cached_zips.empty()) {
                DeleteCachedZip();
            }
        }})
    );

    const Vec4 v{75, GetY() + 1.f + 42.f, 1220.f - 150.f, 60.f};
    m_list = std::make_unique<List>(1, 8, m_pos, v);
    m_list->SetLayout(List::Layout::GRID);
}

CacheManagerMenu::~CacheManagerMenu() {
    // Cleanup
}

void CacheManagerMenu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (!m_cached_zips.empty()) {
        m_list->OnUpdate(controller, touch, m_index, m_cached_zips.size(), [this](bool touch, auto i) {
            if (touch && m_index == i) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect::Focus);
                SetIndex(i);
            }
        });
    }
}

void CacheManagerMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_empty) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 24.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No cached HATS pack found");
        return;
    }

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_cached_zips.size(), [this](auto* vg, auto* theme, auto& v, auto i) {
        const auto& [x, y, w, h] = v;
        const auto& entry = m_cached_zips[i];

        auto text_id = ThemeEntryID_TEXT;
        if (m_index == i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_cached_zips.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        // Display filename
        gfx::drawTextArgs(vg, x + text_xoffset, y + h / 2.f, 20.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(text_id),
            "%s", entry.display_name.c_str());

        // Size on the right
        gfx::drawTextArgs(vg, x + w - text_xoffset, y + h / 2.f, 16.f,
            NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", FormatFileSize(entry.size).c_str());
    });

    // Draw storage info at the bottom
    if (m_total_size > 0) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT - 30.f, 16.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO),
            "Cache: %s (%zu files)", FormatFileSize(m_total_size).c_str(), m_cached_zips.size());
    }
}

void CacheManagerMenu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }
}

void CacheManagerMenu::ScanCachedZips() {
    fs::FsNativeSd fs;
    if (R_FAILED(fs.GetFsOpenResult())) {
        hats_log_write("hats: failed to open SD for cache scan\n");
        m_empty = true;
        return;
    }

    m_cached_zips.clear();
    m_total_size = 0;

    // Check if cache directory exists
    if (!fs.DirExists(CACHE_PATH)) {
        hats_log_write("hats: cache directory does not exist\n");
        m_empty = true;
        return;
    }

    // Open directory and scan for .zip files
    fs::Dir dir;
    if (R_FAILED(fs.OpenDirectory(CACHE_PATH, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
        hats_log_write("hats: failed to open cache directory\n");
        m_empty = true;
        return;
    }

    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(dir.ReadAll(entries))) {
        hats_log_write("hats: failed to read cache directory entries\n");
        m_empty = true;
        return;
    }

    // Filter for .zip files
    for (const auto& entry : entries) {
        if (entry.type == FsDirEntryType_File) {
            std::string name = entry.name;
            if (name.size() > 4 && !strcasecmp(name.c_str() + name.size() - 4, ".zip")) {
                // Use file_size from directory entry (cast to u64)
                u64 file_size = static_cast<u64>(entry.file_size);
                CachedZipEntry cached_entry;
                cached_entry.filename = name;
                cached_entry.display_name = name;
                cached_entry.size = file_size;
                m_cached_zips.push_back(cached_entry);
                m_total_size += file_size;
                hats_log_write("hats: found cached zip: %s (%llu bytes)\n", name.c_str(), file_size);
            }
        }
    }

    // Sort by filename (which contains date in ISO format for natural sorting)
    std::sort(m_cached_zips.begin(), m_cached_zips.end(),
        [](const CachedZipEntry& a, const CachedZipEntry& b) {
            return a.filename > b.filename; // Descending order (newest first)
        });

    m_empty = m_cached_zips.empty();
    hats_log_write("hats: cache scan complete, found %zu zips, total size: %llu bytes\n",
        m_cached_zips.size(), m_total_size);
}

void CacheManagerMenu::ReinstallFromCache() {
    if (m_cached_zips.empty() || m_index >= (s64)m_cached_zips.size()) {
        return;
    }

    const auto& entry = m_cached_zips[m_index];
    std::string zip_path = std::string(CACHE_PATH) + "/" + entry.filename;

    // Verify the zip still exists
    fs::FsNativeSd fs;
    if (!fs.FileExists(zip_path.c_str())) {
        App::Push<ErrorBox>(0x666, "Cached zip not found. It may have been deleted.");
        hats_log_write("hats: cached zip not found: %s\n", zip_path.c_str());
        // Refresh the list
        ScanCachedZips();
        return;
    }

    // Show confirmation dialog
    std::string message = "Reinstall from cache?\n\n" + entry.display_name +
        "\n\nInternet is required at launch to check the current GitHub exclusion list.";
    App::Push<OptionBox>(
        message,
        "Cancel"_i18n, "Reinstall"_i18n, 1, [this, entry, zip_path](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            // Create a fake ReleaseEntry for reinstall
            ReleaseEntry release;
            release.asset_name = entry.filename;
            release.name = entry.display_name;
            release.size = entry.size;
            // Set download_url to empty to indicate we're using local file
            release.download_url = "";

            App::Push<ProgressBox>(0, "Reinstalling"_i18n, entry.display_name,
                [this, release, zip_path](auto pbox) -> Result {
                    fs::FsNativeSd fs;
                    R_TRY(fs.GetFsOpenResult());

                    auto app = App::GetApp();
                    const fs::FsPath staging_path = app->m_installer_staging_path.Get().c_str();

                    // Clean up staging directory
                    hats_log_write("hats: cleaning staging directory: %s\n", static_cast<const char*>(staging_path));
                    if (fs.DirExists(staging_path)) {
                        Result rc = fs.DeleteDirectoryRecursively(staging_path);
                        if (R_FAILED(rc)) {
                            hats_log_write("hats: failed to delete staging directory: 0x%X\n", rc);
                            return rc;
                        }
                    }
                    R_TRY(fs.CreateDirectoryRecursively(staging_path));


                    // Extract from cached zip
                    if (!pbox->ShouldExit()) {
                        pbox->NewTransfer("Extracting cached pack...");
                        hats_log_write("hats: extracting from cache: %s\n", zip_path.c_str());

                        Result rc = thread::TransferUnzipAll(pbox, zip_path.c_str(), &fs, staging_path,
                            [&](const fs::FsPath& name, fs::FsPath& path) -> bool {
                                return true;  // Extract all files
                            });

                        if (R_FAILED(rc)) {
                            hats_log_write("hats: extraction failed: 0x%X\n", rc);
                            return rc;
                        }
                    }

                    R_TRY(pbox->ShouldExitResult());

                    // Commit file system changes
                    if (!pbox->ShouldExit()) {
                        R_TRY(fs.Commit());
                    }

                    return pbox->ShouldExitResult();
                },
                [this, entry](Result rc) {
                    if (rc == Result_TransferCancelled) return;
                    if (R_SUCCEEDED(rc)) {
                        hats_log_write("hats: reinstall from cache complete\n");
                        // Show launch dialog similar to normal install
                        App::Push<OptionBox>(
                            "HATS Pack ready!\n\nLaunch HATS installer?",
                            "Back"_i18n, "Launch"_i18n, 1, [](auto op_index) {
                                if (!op_index || *op_index != 1) {
                                    return;
                                }

                                auto app = App::GetApp();
                                const fs::FsPath installer_payload = app->m_installer_payload.Get().c_str();
                                auto preparation_error = std::make_shared<std::string>();

                                App::Push<ProgressBox>(0, "Preparing..."_i18n, "Configuring hekate",
                                    [installer_payload, preparation_error](auto pbox) -> Result {
                                        fs::FsNativeSd fs;

                                        if (!fs.FileExists(installer_payload)) {
                                            return 0x666;
                                        }

                                        R_TRY(PrepareProtectedInstaller(pbox, fs, installer_payload, *preparation_error));
                                        R_TRY(pbox->ShouldExitResult());
                                        pbox->NewTransfer("Modifying hekate_ipl.ini");
                                        bool success = utils::setHekateAutobootPayload(static_cast<const char*>(installer_payload));

                                        if (!success) {
                                            return 0x667;
                                        }

                                        return 0;
                                    },
                                    [preparation_error](Result rc) {
                                        if (rc == Result_TransferCancelled) return;
                                        if (R_FAILED(rc)) {
                                            App::Push<ErrorBox>(rc, InstallerPreparationError(rc, *preparation_error));
                                            return;
                                        }

                                        rc = utils::requestForcedReboot();
                                        if (R_FAILED(rc)) {
                                            App::Push<ErrorBox>(rc, "Failed to reboot");
                                        }
                                    }
                                );
                            }
                        );
                    } else {
                        App::Push<ErrorBox>(rc, "Failed to extract " + entry.display_name);
                    }
                }
            );
        }
    );
}

void CacheManagerMenu::DeleteCachedZip() {
    if (m_cached_zips.empty() || m_index >= (s64)m_cached_zips.size()) {
        return;
    }

    const auto& entry = m_cached_zips[m_index];
    std::string zip_path = std::string(CACHE_PATH) + "/" + entry.filename;

    // Show confirmation dialog
    std::string message = "Delete from cache?\n\n" + entry.display_name;
    App::Push<OptionBox>(
        message,
        "Cancel"_i18n, "Delete"_i18n, 1, [this, entry, zip_path](auto op_index) {
            if (!op_index || *op_index != 1) {
                return;
            }

            fs::FsNativeSd fs;
            Result rc = fs.DeleteFile(zip_path.c_str());

            if (R_SUCCEEDED(rc)) {
                hats_log_write("hats: deleted cached zip: %s\n", zip_path.c_str());
                App::Notify(i18n::Reorder("Deleted ", entry.display_name));

                // Refresh the list
                ScanCachedZips();

                // Adjust index if needed
                if (m_index >= (s64)m_cached_zips.size()) {
                    SetIndex(std::max<s64>(0, m_cached_zips.size() - 1));
                }
            } else {
                hats_log_write("hats: failed to delete cached zip: 0x%X\n", rc);
                App::Push<ErrorBox>(rc, "Failed to delete " + entry.display_name);
            }
        }
    );
}

} // namespace sphaira::ui::menu::hats
