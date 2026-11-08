#include "hats_contents_policy.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace sphaira::ui::menu::hats;

int main(int argc, char** argv) {
    auto policy = std::make_unique<mm_contents_policy>();
    auto parse = [&](const std::string& json) { return ParseGithubExclusions(*policy, json.data(), json.size()); };
    assert(mm_policy_add(policy.get(), "4200000000001234"));
    assert(parse(R"({"schema_version":1,"preserve_paths":["Fallout1","saltysd/exceptions.txt","Games/Jazz2","switch/daybreak.nro","atmosphere/contents/4200000000001234","fallout1"]})"));
    assert(policy->preserve_count == 5);
    assert(mm_is_excluded_path(policy.get(), "sdmc:/FALLOUT1/SAVES/slot.dat"));
    assert(mm_is_excluded_path(policy.get(), "sd:\\games\\JAZZ2\\data.bin"));
    assert(!mm_is_excluded_path(policy.get(), "fallout10/file"));
    assert(!mm_is_excluded_path(policy.get(), "saltysd/exceptions.txt.bak"));
    assert(!mm_is_excluded_path(policy.get(), "config/mm-tools/hats-staging/fallout1/file"));
    assert(mm_is_protected_install_path(policy.get(), "switch/daybreak.nro", false));
    assert(mm_is_protected_install_path(policy.get(), "atmosphere/contents/4200000000001234/exefs.nsp", false));
    assert(mm_keep_install_parent(policy.get(), "SD:/SaltySD/"));
    assert(!mm_keep_install_parent(policy.get(), "saltysd-old"));
    const auto before = SerializeContentsPolicy(*policy);
    for (const auto& invalid : {
        R"({})", R"({"schema_version":2,"preserve_paths":[]})",
        R"({"schema_version":1,"preserve_paths":null})",
        R"({"schema_version":1,"preserve_paths":[42]})",
        R"({"schema_version":1,"preserve_paths":["../switch"]})",
        R"({"schema_version":1,"preserve_paths":["good","a/../b"]})",
        R"({"schema_version":1,"preserve_paths":["/switch"]})",
        R"({"schema_version":1,"preserve_paths":["sd:/switch"]})",
        R"({"schema_version":1,"preserve_paths":["a//b"]})",
        R"({"schema_version":1,"preserve_paths":["a/./b"]})",
        R"({"schema_version":1,"preserve_paths":["a./b"]})",
        R"({"schema_version":1,"preserve_paths":["a /b"]})",
        R"({"schema_version":1,"preserve_paths":["a\u0000b"]})",
        R"({"schema_version":1,"preserve_paths":["a\nb"]})",
        R"({"schema_version":1,"preserve_paths":["*"]})", "<html>error</html>"}) {
        assert(!parse(invalid));
        assert(SerializeContentsPolicy(*policy) == before);
    }
    auto restored = std::make_unique<mm_contents_policy>();
    assert(mm_parse_contents_policy(restored.get(), before.data(), before.size()));
    assert(SerializeContentsPolicy(*restored) == before);
    for (size_t length = 0; length < before.size(); ++length)
        assert(!mm_parse_contents_policy(restored.get(), before.data(), length));
    const auto old = std::string("MM game and port protection v2\n");
    assert(!mm_parse_contents_policy(restored.get(), old.data(), old.size()));
    assert(!HasModProtection({old.begin(), old.end()}));
    assert(parse(R"({"schema_version":1,"preserve_paths":["fallout2"]})"));
    assert(!mm_is_excluded_path(policy.get(), "fallout1/save"));
    assert(mm_is_excluded_path(policy.get(), "fallout2/save"));
    assert(policy->count == 1); // Exclusion refresh cannot grant sysmodule access.
    assert(parse(R"({"schema_version":1,"preserve_paths":[]})"));
    assert(policy->preserve_count == 0);

    std::string oversized = R"({"schema_version":1,"preserve_paths":[)";
    for (unsigned i = 0; i <= MM_MAX_PRESERVE_PATHS; ++i) {
        if (i) oversized += ',';
        oversized += '"' + std::to_string(i) + '"';
    }
    oversized += "]}";
    assert(!parse(oversized));
    assert(!parse(std::string(HATS_EXCLUSIONS_MAX_JSON + 1, ' ')));
    assert(!mm_policy_preserve(policy.get(), std::string(256, 'a').c_str(), 256));

    if (argc > 1) {
        std::ifstream file(argv[1]);
        const std::string live((std::istreambuf_iterator<char>(file)), {});
        assert(parse(live));
        auto* doc = yyjson_read(live.data(), live.size(), 0);
        assert(doc);
        size_t index, count;
        yyjson_val* value;
        auto* paths = yyjson_obj_get(yyjson_doc_get_root(doc), "preserve_paths");
        yyjson_arr_foreach(paths, index, count, value) {
            const std::string path = yyjson_get_str(value);
            assert(mm_is_excluded_path(policy.get(), path.c_str()));
            assert(mm_is_excluded_path(policy.get(), (path + "/child").c_str()));
        }
        const auto wire = SerializeContentsPolicy(*policy);
        assert(mm_parse_contents_policy(restored.get(), wire.data(), wire.size()));
        assert(SerializeContentsPolicy(*restored) == wire);
        yyjson_doc_free(doc);
        std::cout << "Verified every path in the current GitHub list: " << policy->preserve_count << "\n";
    }
    std::cout << "GitHub exclusion validation, refresh and payload handoff tests passed.\n";
}
