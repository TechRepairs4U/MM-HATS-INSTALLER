#include "self_update_policy.hpp"
#include "hats_release.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace sphaira::self_update;
using namespace sphaira::ui::menu::hats;

struct Backend {
    int failure{};
    bool old_content{true}, new_content{}, new_metadata{}, new_record{};
    std::string events;
    std::uint32_t RegisterContent() { events += 'C'; new_content = true; return failure == 1 ? 11 : 0; }
    std::uint32_t SetMetadata() { assert(new_content); events += 'M'; new_metadata = true; return failure == 2 ? 12 : 0; }
    std::uint32_t PushRecord() { assert(new_metadata); events += 'P'; new_record = true; return failure == 3 ? 13 : 0; }
    void Rollback() { events += 'R'; new_metadata = new_record = false; }
};

int main(int argc, char** argv) {
    for (int fail = 0; fail <= 3; ++fail) {
        Backend backend{fail};
        const auto rc = Publish(backend);
        assert(backend.old_content); // Never remove the running application's NCAs.
        if (!fail) {
            assert(rc == 0 && backend.events == "CMP" && backend.new_record);
        } else {
            assert(rc == 10U + fail && !backend.new_metadata && !backend.new_record);
            assert(backend.events == (fail == 1 ? "C" : fail == 2 ? "CMR" : "CMPR"));
        }
    }
    assert(IsDigest("sha256:" + std::string(64, 'a')));
    assert(!IsDigest("sha256:abc") && !IsDigest("md5:" + std::string(64, 'a')));
    assert(!IsDigest("sha256:" + std::string(64, 'z')));
    assert(IsTrustedAsset("https://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/download/v2.1.8/app.nsp"));
    assert(!IsTrustedAsset("https://github.com/another/repository/releases/download/v2/app.nsp"));
    assert(!IsTrustedAsset("http://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/download/v2/app.nsp"));
    assert(std::string(ReceiptPath) != "/config/mm-tools/installed-app-release.txt");

    const std::string json = R"({"id":100,"tag_name":"any-tag","assets":[
        {"id":1,"name":"update.NSP","browser_download_url":"https://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/download/any-tag/update.NSP","updated_at":"today","size":8000},
        {"id":2,"name":"update.zip","browser_download_url":"https://example.org/update.zip","updated_at":"today","size":4000},
        {"id":3,"name":"source.tar.gz","browser_download_url":"https://example.org/source.tar.gz"},
        {"id":4,"name":"../bad.nsp","browser_download_url":"https://example.org/bad.nsp"},
        {"id":5,"name":"partial.nsp","state":"starter","browser_download_url":"https://example.org/partial.nsp"}
    ]})";
    auto* doc = yyjson_read(json.data(), json.size(), 0);
    auto* root = yyjson_doc_get_root(doc);
    const auto nsp = ParseAppReleases(root, true);
    const auto nro = ParseAppReleases(root, false);
    assert(nsp.size() == 1 && nsp[0].asset_name == "update.NSP");
    assert(nro.size() == 1 && nro[0].asset_name == "update.zip");
    assert(!IsInstalledAppRelease(nsp, AppUpdateReceipt(nro[0])));
    assert(IsInstalledAppRelease(nsp, AppUpdateReceipt(nsp[0])));
    auto replacement = nsp;
    replacement[0].asset_id++;
    assert(!IsInstalledAppRelease(replacement, AppUpdateReceipt(nsp[0])));
    yyjson_doc_free(doc);
    for (const auto state : {"draft", "prerelease"}) {
        auto text = json;
        text.insert(1, std::string("\"") + state + "\":true,");
        doc = yyjson_read(text.data(), text.size(), 0);
        assert(ParseAppReleases(yyjson_doc_get_root(doc), true).empty());
        yyjson_doc_free(doc);
    }

    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    assert(file);
    const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(file), {}};
    std::vector<PackageEntry> entries;
    assert(ParsePackage(data, entries) && entries.size() == 3);
    assert(!ParsePackage(std::span(data).first(15), entries));
    assert(!ParsePackage(std::span(data).first(88), entries));
    auto changed = data;
    changed[4] = 255; // Excessive file count.
    assert(!ParsePackage(changed, entries));
    changed = data;
    changed[8] = changed[9] = 255; // Oversized string table.
    assert(!ParsePackage(changed, entries));
    changed = data;
    changed[32] = changed[33] = 255; // Name outside table.
    assert(!ParsePackage(changed, entries));
    changed = data;
    changed[16 + 7] = 255; // Offset overflow/out of range.
    assert(!ParsePackage(changed, entries));
    changed = data;
    changed[24 + 7] = 255; // Size overflow/out of range.
    assert(!ParsePackage(changed, entries));
    changed = data;
    for (int i = 0; i < 8; ++i) changed[40 + i] = changed[16 + i]; // Overlap.
    assert(!ParsePackage(changed, entries));
    changed = data;
    changed[88] = '/'; // Invalid NCA filename.
    assert(!ParsePackage(changed, entries));
    assert(!ParsePackage(std::span(data).first(data.size() - 1), entries));
    std::cout << "NSP selection, receipt separation, digest/URL checks, real NSP bounds and transactional failure recovery passed.\n";
}
