#pragma once
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "managed_switch_apps.h"

/* Shared by the NRO backup and bare-metal installer. No platform dependencies. */
#define MM_CONTENTS_PROTECTION_MARKER "MM GitHub exclusions v3"
#define MM_CONTENTS_POLICY_PATH "/config/mm-tools/managed-sysmodules.txt"
#define MM_MAX_EXTRA_SYSMODULES 64
#define MM_MAX_PRESERVE_PATHS 1024
#define MM_MAX_PRESERVE_BYTES 65536
#define MM_MAX_PRESERVE_PATH_LENGTH 255
#define MM_MAX_POLICY_TEXT (MM_MAX_PRESERVE_BYTES + MM_MAX_PRESERVE_PATHS * 2 + MM_MAX_EXTRA_SYSMODULES * 19 + 128)

typedef struct {
    unsigned count;
    char ids[MM_MAX_EXTRA_SYSMODULES][17];
    unsigned preserve_count;
    unsigned preserve_size;
    char preserve_paths[MM_MAX_PRESERVE_BYTES];
} mm_contents_policy;

static inline char mm_path_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static inline const char* mm_sd_relative_path(const char* path) {
    if (!path) return "";
    if (mm_path_lower(path[0]) == 's' && mm_path_lower(path[1]) == 'd') {
        if (path[2] == ':') path += 3;
        else if (mm_path_lower(path[2]) == 'm' && mm_path_lower(path[3]) == 'c' && path[4] == ':') path += 5;
    }
    while (*path == '/' || *path == '\\') ++path;
    return path;
}

static inline int mm_path_is_within(const char* path, const char* directory) {
    path = mm_sd_relative_path(path);
    while (*directory) {
        char c = *path == '\\' ? '/' : mm_path_lower(*path);
        if (c != *directory++) return 0;
        ++path;
    }
    return *path == '\0' || *path == '/' || *path == '\\';
}

static inline int mm_path_is_root(const char* path, const char* directory) {
    if (!mm_path_is_within(path, directory)) return 0;
    path = mm_sd_relative_path(path) + strlen(directory);
    while (*path == '/' || *path == '\\') ++path;
    return !*path;
}

// Only SD-relative literal paths are accepted. Reject traversal, ambiguous
// separators, wildcards, embedded NULs and control characters before any install.
static inline int mm_policy_preserve(mm_contents_policy* policy, const char* path, size_t length) {
    if (!path || !length || length > MM_MAX_PRESERVE_PATH_LENGTH) return 0;
    char normalized[MM_MAX_PRESERVE_PATH_LENGTH + 1];
    size_t component = 0;
    for (size_t i = 0; i <= length; ++i) {
        const char c = i == length ? '\0' : (path[i] == '\\' ? '/' : mm_path_lower(path[i]));
        if (c == '/' || i == length) {
            const size_t n = i - component;
            if (!n || (n == 1 && normalized[component] == '.') ||
                (n == 2 && normalized[component] == '.' && normalized[component + 1] == '.')) return 0;
            if (normalized[i - 1] == ' ' || normalized[i - 1] == '.') return 0;
            component = i + 1;
        } else if ((unsigned char)c < 32 || c == 127 || c == ':' || c == '*' || c == '?' ||
                   c == '"' || c == '<' || c == '>' || c == '|') return 0;
        normalized[i] = c;
    }
    for (unsigned pos = 0; pos < policy->preserve_size;) {
        const char* existing = policy->preserve_paths + pos;
        if (!strcmp(existing, normalized)) return 1;
        pos += (unsigned)strlen(existing) + 1;
    }
    if (policy->preserve_count == MM_MAX_PRESERVE_PATHS ||
        length + 1 > MM_MAX_PRESERVE_BYTES - policy->preserve_size) return 0;
    memcpy(policy->preserve_paths + policy->preserve_size, normalized, length + 1);
    policy->preserve_size += (unsigned)length + 1;
    ++policy->preserve_count;
    return 1;
}

static inline int mm_is_excluded_path(const mm_contents_policy* policy, const char* path) {
    if (policy) for (unsigned pos = 0; pos < policy->preserve_size;) {
        const char* excluded = policy->preserve_paths + pos;
        if (mm_path_is_within(path, excluded)) return 1;
        pos += (unsigned)strlen(excluded) + 1;
    }
    return 0;
}

static inline int mm_normalize_title_id(const char* id, char out[17]) {
    if (!id || strlen(id) != 16) return 0;
    for (unsigned i = 0; i < 16; ++i) {
        char c = mm_path_lower(id[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
        out[i] = c;
    }
    out[16] = 0;
    return 1;
}

static inline int mm_contents_title_id(const char* path, char out[17]) {
    if (!mm_path_is_within(path, "atmosphere/contents")) return 0;
    path = mm_sd_relative_path(path) + strlen("atmosphere/contents");
    if (*path != '/' && *path != '\\') return 0;
    ++path;
    char id[17];
    for (unsigned i = 0; i < 16; ++i) {
        if (!path[i] || path[i] == '/' || path[i] == '\\') return 0;
        id[i] = path[i];
    }
    id[16] = 0;
    if (path[16] && path[16] != '/' && path[16] != '\\') return 0;
    return mm_normalize_title_id(id, out);
}

static inline int mm_policy_add(mm_contents_policy* policy, const char* id) {
    char normalized[17];
    if (!mm_normalize_title_id(id, normalized)) return 0;
    // Always protect the user's GTA V port, even if a manifest lists it.
    if (!strcmp(normalized, "0100b00b51230000")) return 0;
    for (unsigned i = 0; i < policy->count; ++i)
        if (!strcmp(policy->ids[i], normalized)) return 1;
    if (policy->count == MM_MAX_EXTRA_SYSMODULES) return 0;
    memcpy(policy->ids[policy->count++], normalized, 17);
    return 1;
}

static inline int mm_is_managed_sysmodule(const mm_contents_policy* policy, const char* id) {
    // Sysmodule titles from this repository's manifest.json. Also support
    // replacement when an older installed pack has no manifest.
    static const char* const known[] = {
        "010000000000bd00", /* MissionControl */
        "420000000000000b", /* sys-patch */
        "0100000000000352", /* emuiibo */
        "420000000007e51a", /* nx-ovlloader */
        "420000000007e51b", /* companion overlay loader */
        "00ff0000636c6bff"  /* sys-clk */
    };
    if (!strcmp(id, "0100b00b51230000")) return 0;
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
        if (!strcmp(id, known[i])) return 1;
    if (policy) for (unsigned i = 0; i < policy->count; ++i)
        if (!strcmp(id, policy->ids[i])) return 1;
    return 0;
}

static inline int mm_is_protected_contents(const mm_contents_policy* policy, const char* path) {
    if (!mm_path_is_within(path, "atmosphere/contents") ||
        mm_path_is_root(path, "atmosphere/contents")) return 0;
    char id[17];
    return !mm_contents_title_id(path, id) || !mm_is_managed_sysmodule(policy, id);
}

static inline int mm_keep_contents_parent(const char* path) {
    return mm_path_is_root(path, "atmosphere") || mm_path_is_root(path, "atmosphere/contents");
}

static inline int mm_is_managed_switch_path(const char* path, int is_directory) {
    path = mm_sd_relative_path(path);
    size_t length = strlen(path);
    while (length && (path[length - 1] == '/' || path[length - 1] == '\\')) --length;
    if (!length) return 0;
    for (size_t i = 0; i < sizeof(mm_managed_switch_files) / sizeof(mm_managed_switch_files[0]); ++i) {
        const char* known = mm_managed_switch_files[i];
        const size_t known_length = strlen(known);
        if (is_directory ? (length >= known_length || known[length] != '/') : (length != known_length)) continue;
        size_t pos = 0;
        while (pos < length) {
            const char c = path[pos] == '\\' ? '/' : mm_path_lower(path[pos]);
            if (c != known[pos]) break;
            ++pos;
        }
        if (pos == length) return 1;
    }
    return 0;
}

static inline int mm_is_protected_install_path(const mm_contents_policy* policy, const char* path, int is_directory) {
    // The running installer owns its versioned files. A HATS pack must never
    // downgrade them. The staging tree is temporary input, not installed data.
    return mm_path_is_within(path, "switch/mm-tools") ||
        (mm_path_is_within(path, "config/mm-tools") && !mm_path_is_within(path, "config/mm-tools/hats-staging")) ||
        mm_is_excluded_path(policy, path) || mm_is_protected_contents(policy, path) ||
        (mm_path_is_within(path, "switch") && !mm_is_managed_switch_path(path, is_directory));
}

static inline int mm_keep_install_parent(const mm_contents_policy* policy, const char* path) {
    // Shared app directories may contain protected saves, ports or caches.
    if (mm_keep_contents_parent(path) || mm_path_is_within(path, "switch") ||
        mm_path_is_within("config/mm-tools", mm_sd_relative_path(path))) return 1;
    char ancestor[MM_MAX_PRESERVE_PATH_LENGTH + 1];
    const char* relative = mm_sd_relative_path(path);
    size_t length = strlen(relative);
    while (length && (relative[length - 1] == '/' || relative[length - 1] == '\\')) --length;
    if (!length || length > MM_MAX_PRESERVE_PATH_LENGTH) return 0;
    for (size_t i = 0; i < length; ++i) ancestor[i] = relative[i] == '\\' ? '/' : mm_path_lower(relative[i]);
    ancestor[length] = 0;
    if (policy) for (unsigned pos = 0; pos < policy->preserve_size;) {
        const char* excluded = policy->preserve_paths + pos;
        // Compare a normalized ancestor using the same component boundary rule.
        if (mm_path_is_within(excluded, ancestor)) return 1;
        pos += (unsigned)strlen(excluded) + 1;
    }
    return 0;
}

// Strict, bounded wire format. Invalid/truncated policies never grant access.
static inline int mm_parse_contents_policy(mm_contents_policy* policy, const char* data, size_t size) {
    const size_t header_size = sizeof(MM_CONTENTS_PROTECTION_MARKER "\n") - 1;
    if (!data || size < header_size + 4 || size > MM_MAX_POLICY_TEXT ||
        memcmp(data, MM_CONTENTS_PROTECTION_MARKER "\n", header_size)) return 0;
    mm_contents_policy* parsed = (mm_contents_policy*)malloc(sizeof(mm_contents_policy));
    if (!parsed) return 0;
    memset(parsed, 0, sizeof(*parsed));
    int valid = 0;
    data += header_size;
    size -= header_size;
    while (size) {
        if (size == 4 && !memcmp(data, "END\n", 4)) { valid = 1; break; }
        const char* end = (const char*)memchr(data, '\n', size);
        if (!end || end - data < 3 || data[1] != ' ') break;
        const size_t length = (size_t)(end - data) - 2;
        if (data[0] == 'S') {
            char id[17];
            if (length != 16) break;
            memcpy(id, data + 2, 16);
            id[16] = 0;
            if (!mm_policy_add(parsed, id)) break;
        } else if (data[0] == 'P') {
            if (!mm_policy_preserve(parsed, data + 2, length)) break;
        } else break;
        size -= (size_t)(end - data) + 1;
        data = end + 1;
    }
    if (valid) memcpy(policy, parsed, sizeof(*policy));
    free(parsed);
    return valid;
}
