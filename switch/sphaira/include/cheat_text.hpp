#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace sphaira::ui::menu::hats {

inline auto IsCheatHeaderLine(const std::string& line) -> bool {
    return line.size() >= 3 &&
        ((line.front() == '[' && line.back() == ']') ||
         (line.front() == '{' && line.back() == '}'));
}

inline auto IsParenthesizedNoteLine(const std::string& line) -> bool {
    return line.size() >= 2 && line.front() == '(' && line.back() == ')';
}

inline auto GetCheatHeaderName(const std::string& line) -> std::string {
    if (!IsCheatHeaderLine(line)) {
        return {};
    }

    return line.substr(1, line.length() - 2);
}

inline auto StripInlineCheatComment(std::string line) -> std::string {
    const auto comment_pos = line.find("//");
    if (comment_pos != std::string::npos) {
        line.erase(comment_pos);
    }

    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
        line.pop_back();
    }

    return line;
}

inline auto IsHexCodeLine(const std::string& line) -> bool {
    std::istringstream stream(line);
    std::string part;
    size_t count = 0;

    while (stream >> part) {
        if (part.size() != 8) {
            return false;
        }

        for (const auto c : part) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                return false;
            }
        }

        count++;
    }

    return count >= 1 && count <= 4;
}

inline auto NormalizeHexCodeLine(const std::string& line) -> std::string {
    std::istringstream stream(line);
    std::ostringstream out;
    std::string part;
    bool first = true;

    while (stream >> part) {
        std::transform(part.begin(), part.end(), part.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });

        if (!first) {
            out << ' ';
        }
        out << part;
        first = false;
    }

    return out.str();
}

inline auto SanitizeCheatContentForAtmosphere(const std::string& content) -> std::string {
    std::istringstream stream(content);
    std::ostringstream sanitized;
    std::string line;
    std::string pending_header;
    bool wrote_code_for_header = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }

        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));

        if (trimmed.empty()) {
            continue;
        }

        if (trimmed.rfind("//", 0) == 0 || IsParenthesizedNoteLine(trimmed)) {
            continue;
        }

        if (IsCheatHeaderLine(trimmed)) {
            pending_header = trimmed;
            wrote_code_for_header = false;
            continue;
        }

        trimmed = StripInlineCheatComment(trimmed);
        if (trimmed.empty()) {
            continue;
        }

        if (IsHexCodeLine(trimmed)) {
            if (!pending_header.empty() && !wrote_code_for_header) {
                sanitized << pending_header << '\n';
                wrote_code_for_header = true;
            }
            sanitized << NormalizeHexCodeLine(trimmed) << '\n';
        }
    }

    return sanitized.str();
}

inline auto SanitizeManualCheatContent(const std::vector<std::uint8_t>& data, std::string& out) -> bool {
    if (data.empty()) {
        return false;
    }

    // Atmosphere/EdiZon cheat parsers can be picky about BOM-prefixed files.
    size_t offset = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        offset = 3;
    } else if (data.size() >= 2 &&
               ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        return false;
    }

    std::string text(reinterpret_cast<const char*>(data.data() + offset), data.size() - offset);
    if (!text.empty() && static_cast<unsigned char>(text.front()) == 0xEF) {
        return false;
    }

    std::istringstream stream(text);
    std::ostringstream sanitized;
    std::string line;
    std::string pending_header;
    bool wrote_any_code = false;
    bool wrote_code_for_header = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }

        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));

        if (trimmed.empty() || trimmed.rfind("//", 0) == 0 || IsParenthesizedNoteLine(trimmed)) {
            continue;
        }

        if (IsCheatHeaderLine(trimmed)) {
            pending_header = trimmed;
            wrote_code_for_header = false;
            continue;
        }

        trimmed = StripInlineCheatComment(trimmed);
        if (trimmed.empty()) {
            continue;
        }

        if (!IsHexCodeLine(trimmed) || pending_header.empty()) {
            continue;
        }

        if (!wrote_code_for_header) {
            sanitized << pending_header << '\n';
            wrote_code_for_header = true;
        }

        sanitized << NormalizeHexCodeLine(trimmed) << '\n';
        wrote_any_code = true;
    }

    out = sanitized.str();
    return wrote_any_code;
}

} // namespace sphaira::ui::menu::hats
