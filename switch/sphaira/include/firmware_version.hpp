#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace sphaira::ui::menu::hats {

// Parse version string into components for comparison
inline std::vector<int> parseVersion(const std::string& version) {
    std::vector<int> parts;
    std::stringstream ss(version);
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        if (segment.empty()) continue;
        // Extract numeric part only using strtol (no exceptions)
        char* end = nullptr;
        long part = std::strtol(segment.c_str(), &end, 10);
        if (end != segment.c_str()) {
            parts.push_back(static_cast<int>(part));
        } else {
            break;
        }
    }

    return parts;
}

// Compare versions: returns true if target < current (downgrade)
inline bool isVersionLower(const std::string& target, const std::string& current) {
    auto target_parts = parseVersion(target);
    auto current_parts = parseVersion(current);

    size_t max_len = std::max(target_parts.size(), current_parts.size());

    for (size_t i = 0; i < max_len; i++) {
        int t = (i < target_parts.size()) ? target_parts[i] : 0;
        int c = (i < current_parts.size()) ? current_parts[i] : 0;

        if (t < c) return true;
        if (t > c) return false;
    }

    return false;
}

inline bool isVersionNewer(const std::string& target, const std::string& current) {
    return isVersionLower(current, target);
}

inline std::string ExtractFirmwareVersion(const std::string& text) {
    for (size_t i = 0; i < text.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            continue;
        }

        size_t end = i;
        while (end < text.size() &&
               (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.')) {
            end++;
        }

        std::string version = text.substr(i, end - i);
        if (version.find('.') != std::string::npos) {
            return version;
        }
        i = end;
    }

    return "";
}

inline std::string ExtractSupportedFirmware(const std::string& body) {
    std::istringstream stream(body);
    std::string line;

    while (std::getline(stream, line)) {
        auto lowered = line;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lowered.find("supported firmware") == std::string::npos) {
            continue;
        }

        const auto version = ExtractFirmwareVersion(line);
        if (!version.empty()) {
            return version;
        }
    }

    return "";
}


} // namespace sphaira::ui::menu::hats
