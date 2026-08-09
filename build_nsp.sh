#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/Release}"
NSP_DIR="${NSP_DIR:-${ROOT_DIR}/build/nsp}"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT_DIR}/out}"
TITLE_ID="${HATS_TOOLS_TITLE_ID:-0100f0f0a1b20000}"
KEYSET="${HACBREWPACK_KEYSET:-}"
HACBREWPACK="${HACBREWPACK:-}"
NPDMTOOL="${NPDMTOOL:-}"
ELF2NSO="${ELF2NSO:-}"
NACPTOOL="${NACPTOOL:-}"

usage() {
    cat <<'EOF'
Usage: ./build_nsp.sh [--keyset PATH] [--title-id ID]

Builds a genuine installable Nintendo Switch NSP from the MM HATS INSTALLER
ELF, NACP, icon, and ROMFS. The keyset is used only locally or from a CI
secret and is never copied into the repository or release output.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --keyset)
            [[ $# -ge 2 ]] || { echo "--keyset requires a path" >&2; exit 2; }
            KEYSET="$2"
            shift 2
            ;;
        --title-id)
            [[ $# -ge 2 ]] || { echo "--title-id requires a 16-digit hex ID" >&2; exit 2; }
            TITLE_ID="${2#0x}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

TITLE_ID="$(printf '%s' "$TITLE_ID" | tr '[:upper:]' '[:lower:]')"
if [[ ! "$TITLE_ID" =~ ^01[0-9a-f]{14}$ ]]; then
    echo "Invalid title ID '$TITLE_ID'; expected 16 lowercase hex digits beginning with 01" >&2
    exit 2
fi

if [[ -z "$KEYSET" || ! -f "$KEYSET" ]]; then
    echo "A local keyset is required. Set HACBREWPACK_KEYSET or pass --keyset PATH." >&2
    exit 1
fi

find_tool() {
    local requested="$1"
    local name="$2"
    if [[ -n "$requested" && -x "$requested" ]]; then
        printf '%s\n' "$requested"
        return 0
    fi
    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return 0
    fi
    if command -v "${name}.exe" >/dev/null 2>&1; then
        command -v "${name}.exe"
        return 0
    fi
    return 1
}

if [[ -z "$ELF2NSO" ]]; then
    ELF2NSO="$(find_tool '' elf2nso || true)"
fi
if [[ -z "$NPDMTOOL" ]]; then
    NPDMTOOL="$(find_tool '' npdmtool || true)"
fi
if [[ -z "$NACPTOOL" ]]; then
    NACPTOOL="$(find_tool '' nacptool || true)"
fi
if [[ -z "$ELF2NSO" || -z "$NPDMTOOL" || -z "$NACPTOOL" ]]; then
    echo "elf2nso, npdmtool, and nacptool must be installed and available on PATH." >&2
    exit 1
fi

if [[ -z "$HACBREWPACK" ]]; then
    if [[ -x "${ROOT_DIR}/tools/hacbrewpack/hacbrewpack" ]]; then
        HACBREWPACK="${ROOT_DIR}/tools/hacbrewpack/hacbrewpack"
    elif [[ -x "${ROOT_DIR}/tools/hacbrewpack/hacbrewpack.exe" ]]; then
        HACBREWPACK="${ROOT_DIR}/tools/hacbrewpack/hacbrewpack.exe"
    elif command -v hacbrewpack >/dev/null 2>&1; then
        HACBREWPACK="$(command -v hacbrewpack)"
    elif command -v hacbrewpack.exe >/dev/null 2>&1; then
        HACBREWPACK="$(command -v hacbrewpack.exe)"
    fi
fi

if [[ -z "$HACBREWPACK" ]]; then
    HACBREWPACK_REPO="${HACBREWPACK_REPO:-https://github.com/pplatoon/hacBrewPack.git}"
    HACBREWPACK_REF="${HACBREWPACK_REF:-master}"
    HACBREWPACK_DIR="${HACBREWPACK_DIR:-${ROOT_DIR}/build/hacbrewpack}"

    if [[ ! -d "${HACBREWPACK_DIR}/.git" ]]; then
        rm -rf "$HACBREWPACK_DIR"
        git clone --depth 1 --branch "$HACBREWPACK_REF" "$HACBREWPACK_REPO" "$HACBREWPACK_DIR"
    fi
    if [[ ! -f "${HACBREWPACK_DIR}/config.mk" && -f "${HACBREWPACK_DIR}/config.mk.template" ]]; then
        cp "${HACBREWPACK_DIR}/config.mk.template" "${HACBREWPACK_DIR}/config.mk"
    fi
    make -C "$HACBREWPACK_DIR"
    HACBREWPACK="${HACBREWPACK_DIR}/hacbrewpack"
fi

if [[ ! -x "$HACBREWPACK" ]]; then
    echo "hacBrewPack executable not found: $HACBREWPACK" >&2
    exit 1
fi

VERSION="${HATS_TOOLS_VERSION:-}"
if [[ -z "$VERSION" ]]; then
    VERSION="$(sed -n 's/^[[:space:]]*set(HATS_TOOLS_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' "${ROOT_DIR}/sphaira/CMakeLists.txt" | head -n 1)"
fi
[[ -n "$VERSION" ]] || { echo "Unable to determine HATS_TOOLS_VERSION" >&2; exit 1; }

ELF="${BUILD_DIR}/sphaira/mm-tools.elf"
NACP="${BUILD_DIR}/sphaira/mm-tools.nacp"
ROMFS_DIR="${BUILD_DIR}/sphaira/romfs"

if [[ ! -f "$ELF" || ! -f "$NACP" || ! -d "$ROMFS_DIR" ]]; then
    echo "Release build artifacts are missing; configuring and building Release..."
    cmake --preset Release
    cmake --build --preset Release
fi

[[ -f "$ELF" ]] || { echo "Missing ELF: $ELF" >&2; exit 1; }
[[ -f "$NACP" ]] || { echo "Missing NACP: $NACP" >&2; exit 1; }
[[ -d "$ROMFS_DIR" ]] || { echo "Missing ROMFS: $ROMFS_DIR" >&2; exit 1; }
[[ -f "${ROOT_DIR}/assets/icon.jpg" ]] || { echo "Missing app icon" >&2; exit 1; }

rm -rf "$NSP_DIR"
mkdir -p "${NSP_DIR}/exefs" "${NSP_DIR}/control" "${NSP_DIR}/romfs" "${OUTPUT_DIR}"

"$ELF2NSO" "$ELF" "${NSP_DIR}/exefs/main"
cp "$NACP" "${NSP_DIR}/control/control.nacp"
cp -R "${ROMFS_DIR}/." "${NSP_DIR}/romfs/"

# Keep the standalone NSP self-contained. These files are copied to their
# normal SD-card locations by the app during startup, so the installed app
# has the same runtime package as the downloadable ZIP.
PACKAGE_DIR="${NSP_DIR}/romfs/package"
mkdir -p "${PACKAGE_DIR}/switch/mm-tools" "${PACKAGE_DIR}/config/mm-tools/icons"
[[ -f "${ROOT_DIR}/payload/output/hats-installer.bin" ]] || {
    echo "Missing payload: ${ROOT_DIR}/payload/output/hats-installer.bin" >&2
    echo "Build the payload before building the NSP." >&2
    exit 1
}
cp "${ROOT_DIR}/config.ini" "${PACKAGE_DIR}/config/mm-tools/config.ini"
cp "${ROOT_DIR}/releases.json" "${PACKAGE_DIR}/config/mm-tools/releases.json"
cp "${ROOT_DIR}/assets/romfs/hekate_ipl_mod.ini" "${PACKAGE_DIR}/config/mm-tools/hekate_ipl_mod.ini"
cp "${ROOT_DIR}/assets/external-background/background.rgba" "${PACKAGE_DIR}/config/mm-tools/background.rgba"
cp "${ROOT_DIR}/assets/external-icons/"*.rgba "${PACKAGE_DIR}/config/mm-tools/icons/"
cp "${ROOT_DIR}/payload/output/hats-installer.bin" "${PACKAGE_DIR}/switch/mm-tools/hats-installer.bin"

# Keep the control metadata aligned with the NPDM and content-meta title ID.
# This must be done for both old and new hacBrewPack versions; relying on the
# packer alone can leave a zero or mismatched title ID in control.nacp, which
# makes the installed application exit immediately on launch.
"$NACPTOOL" --create "MM HATS INSTALLER" "TechRepairs4U" "$VERSION" \
    "${NSP_DIR}/control/control.nacp" "--titleid=${TITLE_ID}"

for language in \
    AmericanEnglish BritishEnglish Japanese French German LatinAmericanSpanish \
    Spanish Italian Dutch CanadianFrench Portuguese Russian Korean \
    TraditionalChinese SimplifiedChinese; do
    cp "${ROOT_DIR}/assets/icon.jpg" "${NSP_DIR}/control/icon_${language}.dat"
done

NPDM_JSON="${NSP_DIR}/npdm.json"
sed \
    -e 's/"name": "Application"/"name": "MM HATS INSTALLER"/' \
    -e "s/\"title_id\": \"[^\"]*\"/\"title_id\": \"0x${TITLE_ID}\"/" \
    -e 's/"title_id_range_min": "[^"]*"/"title_id_range_min": "0x0100000000000000"/' \
    -e 's/"title_id_range_max": "[^"]*"/"title_id_range_max": "0x01ffffffffffffff"/' \
    "${ROOT_DIR}/hbl/hbl.json" > "$NPDM_JSON"
"$NPDMTOOL" "$NPDM_JSON" "${NSP_DIR}/exefs/main.npdm"

rm -rf "${NSP_DIR}/hacbrewpack_nsp" "${NSP_DIR}/hacbrewpack_nca" "${NSP_DIR}/hacbrewpack_temp"
HACBREWPACK_HELP="$("$HACBREWPACK" --help 2>&1 || true)"
HACBREWPACK_ARGS=(--keyset "$KEYSET" --nologo)
if grep -q -- '--titleid' <<< "$HACBREWPACK_HELP"; then
    HACBREWPACK_ARGS+=(
        --titleid "$TITLE_ID"
        --titlename "MM HATS INSTALLER"
        --titlepublisher "TechRepairs4U"
    )
else
    echo "hacBrewPack has no --titleid override; using the explicit NACP title ID."
fi
(
    cd "$NSP_DIR"
    "$HACBREWPACK" "${HACBREWPACK_ARGS[@]}"
)

NSP_SOURCE="$(find "${NSP_DIR}/hacbrewpack_nsp" -maxdepth 1 -type f -iname '*.nsp' -print -quit)"
[[ -n "$NSP_SOURCE" && -f "$NSP_SOURCE" ]] || { echo "hacBrewPack did not produce an NSP" >&2; exit 1; }

NSP_OUTPUT="${OUTPUT_DIR}/MM-HATS-INSTALLER-${VERSION}.nsp"
cp "$NSP_SOURCE" "$NSP_OUTPUT"
echo "Created ${NSP_OUTPUT}"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$NSP_OUTPUT"
fi
