#include "hats_release.hpp"
#include "firmware_version.hpp"
#include "cheat_text.hpp"
#include "hats_contents_policy.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace sphaira::ui::menu::hats;

auto parse(const char* json) {
    auto* doc = yyjson_read(json, std::strlen(json), 0);
    assert(doc);
    auto entries = ParsePackReleases(yyjson_doc_get_root(doc));
    yyjson_doc_free(doc);
    return entries;
}

int main(int argc, char** argv) {
    // Arbitrary tags and changed filenames; every attached ZIP remains selectable.
    auto entries = parse(R"({"tag_name":"banana","name":"Latest pack","author":null,"assets":[
        {"name":"New-MM.ZIP","browser_download_url":"https://example.org/New-MM.ZIP","size":123},
        {"name":"HATS.MM+FW.zip","browser_download_url":"https://example.org/HATS.MM%2BFW.zip"},
        {"name":"Minimal.zip","browser_download_url":"https://example.org/Minimal.zip"},
        {"name":"Guide.pdf","browser_download_url":"https://example.org/Guide.pdf"}
    ]})");
    assert(entries.size() == 3);
    assert(entries[0].tag_name == "banana");
    assert(entries[0].asset_name == "New-MM.ZIP" && entries[0].size == 123);
    assert(entries[1].download_url == "https://example.org/HATS.MM%2BFW.zip");
    assert(entries[0].name != entries[1].name);

    // Replacing assets without changing the tag is observed on the next fetch.
    auto replaced = parse(R"({"tag_name":"banana","assets":[
        {"name":"New-MM.ZIP","browser_download_url":"https://example.org/replaced.zip","size":456}
    ]})");
    assert(replaced[0].size == 456);
    assert(replaced[0].download_url == "https://example.org/replaced.zip");

    assert(parse(R"({"message":"API rate limit exceeded"})").empty());
    assert(parse(R"({"assets":null})").empty());
    assert(parse(R"({"assets":[null,7,{"name":null,"browser_download_url":12}]})").empty());
    assert(parse(R"({"draft":true,"assets":[{"name":"pack.zip","browser_download_url":"https://example.org/pack.zip"}]})").empty());
    assert(parse(R"({"assets":[
        {"name":"../pack.zip","browser_download_url":"https://example.org/pack.zip"},
        {"name":"dir\\pack.zip","browser_download_url":"https://example.org/pack.zip"},
        {"name":"pack.zip.part","browser_download_url":"https://example.org/pack.zip.part"},
        {"name":"pack.zip","browser_download_url":"https://example.org/pack.zip","state":"starter"},
        {"name":"pack.zip","browser_download_url":"file:///pack.zip"}
    ]})").empty());

    // Custom release-array sources still work; we do not sort tags as versions.
    auto legacy = parse(R"([{"tag_name":"v2","assets":[{"name":"a.zip","browser_download_url":"https://example.org/a.zip"}]},
        {"tag_name":"v99","assets":[{"name":"b.zip","browser_download_url":"https://example.org/b.zip"}]}])");
    assert(legacy.size() == 2 && legacy[0].tag_name == "v2");

    // App updates use release/asset identity, never semantic tag ordering or a
    // required filename prefix. The receipt prevents repeated update prompts.
    auto app_release = parse(R"({"id":100,"tag_name":"banana","name":"Magic refresh","assets":[
        {"id":200,"name":"Anything-I-Choose.ZIP","browser_download_url":"https://example.org/app.zip","updated_at":"2026-10-23T12:00:00Z","size":123,"digest":"sha256:abc"},
        {"id":201,"name":"another-package.zip","browser_download_url":"https://example.org/other.zip","updated_at":"2026-10-23T12:00:00Z","size":456}
    ]})");
    assert(app_release.size() == 2 && app_release[0].release_id == 100 && app_release[0].asset_id == 200);
    const auto installed = AppUpdateReceipt(app_release[0]);
    assert(!installed.empty());
    assert(IsInstalledAppRelease(app_release, installed));
    assert(!IsInstalledAppRelease(app_release, ""));
    assert(!IsInstalledAppRelease(app_release, "bad receipt"));
    assert(!IsInstalledAppRelease(app_release, installed.substr(0, installed.size() - 1)));
    assert(AppUpdateReceipt(entries[0]).empty()); // Incomplete API metadata cannot mark an update installed.

    // Editing display metadata alone or adding another ZIP is not a new upload.
    auto renamed = app_release;
    renamed[0].tag_name = "v0.0.1";
    renamed[0].asset_name = "renamed.zip";
    renamed.push_back(renamed[1]);
    assert(IsInstalledAppRelease(renamed, installed));
    auto new_release = app_release;
    for (auto& item : new_release) { item.release_id = 101; item.tag_name = "v0.0.1"; }
    assert(!IsInstalledAppRelease(new_release, installed));
    auto replaced_app = app_release;
    ++replaced_app[0].asset_id; // Re-upload with exactly the same tag and name.
    assert(!IsInstalledAppRelease(replaced_app, installed));
    replaced_app = app_release;
    replaced_app[0].asset_updated_at = "2026-10-23T13:00:00Z";
    assert(!IsInstalledAppRelease(replaced_app, installed));
    replaced_app = app_release;
    replaced_app[0].asset_digest = "sha256:def";
    assert(!IsInstalledAppRelease(replaced_app, installed));
    replaced_app = app_release;
    ++replaced_app[0].size;
    assert(!IsInstalledAppRelease(replaced_app, installed));
    assert(!IsInstalledAppRelease({app_release[1]}, installed));
    assert(IsInstalledAppRelease(app_release, AppUpdateReceipt(app_release[1])));

    assert(ExtractFirmwareVersion("Firmware v23.0.1") == "23.0.1");
    assert(isVersionLower(ExtractFirmwareVersion("v22.1.0"), "23.0.1"));
    assert(isVersionLower("23.0.0", "23.0.1")); // Same-fuse-generation downgrade.
    assert(!isVersionLower("23.0.1", "23.0.1"));
    assert(isVersionNewer("23.0.1", "22.5.0"));
    assert(ExtractSupportedFirmware("Summary\r\nSupported Firmware: Up to 23.0.1\r\n") == "23.0.1");
    assert(ExtractSupportedFirmware("No firmware metadata") == "");

    // Manual imports accept UTF-8 BOM/CRLF, remove notes and normalize hex.
    const std::string imported = "\xEF\xBB\xBF[Example]\r\n(note)\r\n// comment\r\n04000000 abcdef01 // inline\r\n";
    std::string sanitized;
    assert(SanitizeManualCheatContent({imported.begin(), imported.end()}, sanitized));
    assert(sanitized == "[Example]\n04000000 ABCDEF01\n");
    assert(!SanitizeManualCheatContent({}, sanitized));
    assert(!SanitizeManualCheatContent({0xff, 0xfe, 0x41, 0}, sanitized));
    const std::string no_codes = "[Empty]\nnot a code\n";
    assert(!SanitizeManualCheatContent({no_codes.begin(), no_codes.end()}, sanitized));
    const std::string master = "{Master}\n04000000 00000001\n[Empty heading]\n";
    assert(SanitizeManualCheatContent({master.begin(), master.end()}, sanitized));
    assert(sanitized == "{Master}\n04000000 00000001\n");

    mm_contents_policy policy{};
    const char* manifest = R"({"components":{
        "new-module":{"files":["atmosphere/contents/4200000000001234/exefs.nsp","atmosphere/contents/4200000000001234/flags/boot2.flag"]},
        "game":{"files":["atmosphere/contents/0100123412340000/exefs.nsp","atmosphere/contents/0100123412340000/romfs/a"]},
        "gta":{"files":["atmosphere/contents/0100B00B51230000/exefs.nsp","atmosphere/contents/0100B00B51230000/flags/boot2.flag"]},
        "bad":{"files":[null,42,"atmosphere/contents/../flags/boot2.flag"]}
    }})";
    auto* manifest_doc = yyjson_read(manifest, std::strlen(manifest), 0);
    assert(manifest_doc);
    AddManifestSysmodules(policy, yyjson_doc_get_root(manifest_doc));
    yyjson_doc_free(manifest_doc);
    assert(policy.count == 1);
    assert(!mm_is_protected_contents(&policy, "sd:/atmosphere/contents/4200000000001234/exefs.nsp"));
    assert(!mm_is_protected_contents(&policy, "SDMC:/Atmosphere/Contents/010000000000BD00/flags/boot2.flag"));
    assert(!mm_is_protected_contents(&policy, "atmosphere\\contents\\420000000007E51B"));
    assert(mm_is_protected_contents(&policy, "/atmosphere/contents/0100123412340000/romfs/a"));
    assert(mm_is_protected_contents(&policy, "/atmosphere/contents/0100B00B51230000"));
    assert(mm_is_protected_contents(&policy, "/atmosphere/contents/notes.txt"));
    assert(mm_is_protected_contents(&policy, "/atmosphere/contents/010000000000bd001"));
    assert(!mm_is_protected_contents(&policy, "/config/mm-tools/hats-staging/atmosphere/contents/0100B00B51230000"));
    assert(!mm_is_protected_contents(&policy, "/atmosphere/contents-old"));
    assert(!mm_is_protected_contents(&policy, ""));
    assert(!mm_is_protected_contents(&policy, "s"));
    assert(mm_keep_contents_parent("sd:/ATMOSPHERE/contents/"));
    assert(!mm_keep_contents_parent("sd:/atmosphere-old"));
    const auto wire = SerializeContentsPolicy(policy);
    mm_contents_policy parsed{};
    assert(mm_parse_contents_policy(&parsed, wire.data(), wire.size()));
    assert(parsed.count == 1 && !std::strcmp(parsed.ids[0], "4200000000001234"));
    assert(!mm_parse_contents_policy(&parsed, wire.data(), wire.size() - 1));
    assert(!mm_parse_contents_policy(&parsed, "garbage", 7));
    assert(!mm_policy_add(&parsed, "0100B00B51230000"));
    assert(!mm_policy_add(&parsed, "../atmosphere"));
    assert(!HasModProtection({}));
    const std::string marker = "binary prefix" MM_CONTENTS_PROTECTION_MARKER "suffix";
    assert(HasModProtection({marker.begin(), marker.end()}));
    const std::string old_marker = "MM selective mod protection v1";
    assert(!HasModProtection({old_marker.begin(), old_marker.end()}));

    // Utilities are identified by exact paths, never by a .nro suffix alone.
    assert(!mm_is_protected_install_path(&policy, "sdmc:/SWITCH/DBI_RU/DBI.nro", false));
    assert(!mm_is_protected_install_path(&policy, "switch\\dbi_ru\\", true));
    assert(!mm_is_protected_install_path(&policy, "switch/daybreak.nro", false));
    assert(mm_is_protected_install_path(&policy, "switch/daybreak.nro", true));
    assert(mm_is_protected_install_path(&policy, "switch/dbi_ru", false));
    assert(mm_is_protected_install_path(&policy, "switch/dbi_ru/ports", true));
    assert(mm_is_protected_install_path(&policy, "switch/dbi_ru/mygame.nro", false));
    assert(mm_is_protected_install_path(&policy, "switch/gta5", true));
    assert(mm_is_protected_install_path(&policy, "switch/homebrew-game.nro", false));
    assert(mm_is_protected_install_path(&policy, "switch/mm-tools/cache", true));
    assert(mm_is_protected_install_path(&policy, "switch/.overlays/custom.ovl", false));
    assert(mm_is_protected_install_path(&policy, "switch/daybreak.nro.backup", false));
    assert(!mm_is_protected_install_path(&policy, "switch", true));
    assert(mm_keep_install_parent(&policy, "switch/dbi_ru"));
    assert(!mm_is_protected_install_path(&policy, "config/mm-tools/hats-staging/switch/gta5", true));
    assert(!mm_is_protected_install_path(&policy, "switch-old/gta5", true));

    if (argc > 1) {
        auto* doc = yyjson_read_file(argv[1], 0, nullptr, nullptr);
        assert(doc);
        auto live = ParsePackReleases(yyjson_doc_get_root(doc));
        yyjson_doc_free(doc);
        assert(!live.empty());
        for (const auto& entry : live) std::cout << entry.name << '\n';
    }
    std::cout << "Release parsing, firmware version, cheat import and mod policy tests passed.\n";
}
