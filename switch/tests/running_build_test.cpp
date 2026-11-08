#include "running_build.hpp"
#include <cassert>
#include <iostream>
using namespace sphaira::hats;
int main() {
    const std::uint64_t version = (std::uint64_t{1} << 56) | (std::uint64_t{12} << 48) | (std::uint64_t{1} << 40) | 0x123456;
    const auto branded = FormatRunningBuild(true, version, "Magicmonkei.com", true, 1);
    assert(branded.label == "MAGIC MONKEI BUILD" && branded.version == "AMS 1.12.1" && branded.mode == "emuMMC");
    assert(DescribeRunningBuild(branded) == "Magic Monkei build (AMS 1.12.1)");
    const auto generic = FormatRunningBuild(true, version, "", true, 0);
    assert(generic.label == "ATMOSPHERE" && generic.version == "AMS 1.12.1" && generic.mode == "sysMMC");
    assert(DescribeRunningBuild(generic) == "AMS 1.12.1");
    for (const auto& identity : {"AnotherBuild", "Magicmonkei.com-other", "Magicmonkei.co", "Magic Monkei"})
        assert(FormatRunningBuild(true, version, identity, false, 0).label == "ATMOSPHERE");
    const auto unknown = FormatRunningBuild(false, version, "Magicmonkei.com", true, 1);
    assert(unknown.label == "ATMOSPHERE" && unknown.version == "Unavailable" && unknown.mode == "Unknown");
    assert(DescribeRunningBuild(unknown) == "AMS unavailable");
    assert(FormatRunningBuild(true, version, "", false, 0).mode == "Unknown");
    const auto different = FormatRunningBuild(true, (std::uint64_t{2} << 56) | (std::uint64_t{5} << 40), "Magicmonkei.com", true, 1);
    assert(different.version == "AMS 2.0.5");
    std::cout << "Running AMS version, Magic Monkei identity, other-build fallback and mode tests passed.\n";
}
