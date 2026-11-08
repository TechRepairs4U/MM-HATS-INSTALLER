#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace sphaira::hats {

inline constexpr unsigned MagicMonkeiBuildIdentityCommand = 65000;
inline constexpr std::string_view MagicMonkeiBuildIdentity = "Magicmonkei.com";

struct RunningBuildInfo {
    std::string label{"ATMOSPHERE"};
    std::string version{"Unavailable"};
    std::string mode{"Unknown"};
};

inline RunningBuildInfo FormatRunningBuild(bool has_ams, std::uint64_t api_version,
        std::string_view identity, bool has_mode, std::uint64_t emummc) {
    RunningBuildInfo info;
    if (!has_ams) return info;
    info.version = "AMS " + std::to_string((api_version >> 56) & 0xff) + "." +
        std::to_string((api_version >> 48) & 0xff) + "." + std::to_string((api_version >> 40) & 0xff);
    if (identity == MagicMonkeiBuildIdentity) info.label = "MAGIC MONKEI BUILD";
    if (has_mode) info.mode = emummc ? "emuMMC" : "sysMMC";
    return info;
}

} // namespace sphaira::hats
