#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::self_update {
inline constexpr std::uint64_t TitleId = 0x0100f0f0a1b20000;
inline constexpr std::uint64_t MaxPackageSize = 64 * 1024 * 1024;
inline constexpr const char* ReceiptPath = "/config/mm-tools/installed-nsp-release.txt";
inline constexpr const char* CleanupPath = "/config/mm-tools/nsp-update-cleanup.bin";
inline constexpr std::uint32_t InvalidPackage = 0x66C;

inline bool IsHex(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
inline bool IsDigest(std::string_view digest) {
    return digest.size() == 71 && digest.starts_with("sha256:") && IsHex(digest.substr(7));
}
inline bool IsTrustedAsset(std::string_view url) {
    return url.starts_with("https://github.com/TechRepairs4U/MM-HATS-INSTALLER/releases/download/") &&
        url.find_first_of("\r\n\\") == std::string_view::npos;
}
struct PackageEntry { std::string name; std::uint64_t offset, size; };

// Our full application contains exactly Program, Control and Meta NCAs.
// Bound every table, string and extent before the native installer reads it.
inline bool ParsePackage(std::span<const std::uint8_t> data, std::vector<PackageEntry>& out) {
    out.clear();
    if (data.size() < 16 || data.size() > MaxPackageSize ||
        std::string_view(reinterpret_cast<const char*>(data.data()), 4) != "PFS0") return false;
    const auto number = [&](size_t at, size_t length) {
        std::uint64_t value{};
        for (size_t i = 0; i < length; ++i) value |= std::uint64_t(data[at + i]) << (i * 8);
        return value;
    };
    const auto count = number(4, 4), strings_size = number(8, 4);
    if (count != 3 || strings_size > 1024 || strings_size < 3 || data.size() < 88 + strings_size) return false;
    const size_t payload = 88 + strings_size;
    unsigned metas{};
    for (size_t i = 0; i < 3; ++i) {
        const size_t record = 16 + i * 24;
        const auto offset = number(record, 8), size = number(record + 8, 8), name_offset = number(record + 16, 4);
        if (name_offset >= strings_size || size < 0xC00 || offset > data.size() - payload ||
            size > data.size() - payload - offset) return false;
        const char* start = reinterpret_cast<const char*>(data.data() + 88 + name_offset);
        const auto end = std::find(start, start + strings_size - name_offset, '\0');
        if (end == start + strings_size - name_offset) return false;
        const std::string name(start, end);
        if ((name.size() != 36 && name.size() != 41) || !IsHex(std::string_view(name).substr(0, 32))) return false;
        const auto suffix = name.substr(32);
        if (suffix == ".cnmt.nca") ++metas;
        else if (suffix != ".nca") return false;
        PackageEntry entry{name, payload + offset, size};
        for (const auto& previous : out) {
            if (previous.name.substr(0, 32) == name.substr(0, 32) ||
                (entry.offset < previous.offset + previous.size && previous.offset < entry.offset + entry.size)) return false;
        }
        out.push_back(entry);
    }
    return metas == 1;
}

// Install new content first. Keep the old content available throughout; restore
// the old database/launch record if publishing the replacement fails.
template<class Backend>
std::uint32_t Publish(Backend& backend) {
    if (const auto rc = backend.RegisterContent()) return rc;
    auto rc = backend.SetMetadata();
    if (!rc) rc = backend.PushRecord();
    if (rc) backend.Rollback();
    return rc;
}
} // namespace sphaira::self_update
