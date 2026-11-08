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
PREPARE_ONLY=false

usage() {
    cat <<'EOF'
Usage: ./build_nsp.sh [--keyset PATH] [--title-id ID] [--prepare-only]

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
        --prepare-only)
            PREPARE_ONLY=true
            shift
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

if [[ "$PREPARE_ONLY" == false && ( -z "$KEYSET" || ! -f "$KEYSET" ) ]]; then
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

if [[ "$PREPARE_ONLY" == false && -z "$HACBREWPACK" ]]; then
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

if [[ "$PREPARE_ONLY" == false && -z "$HACBREWPACK" ]]; then
    HACBREWPACK_REPO="${HACBREWPACK_REPO:-https://github.com/pplatoon/hacBrewPack.git}"
    HACBREWPACK_DIR="${HACBREWPACK_DIR:-${ROOT_DIR}/build/hacbrewpack}"

    if [[ ! -d "${HACBREWPACK_DIR}/.git" ]]; then
        [[ ! -e "$HACBREWPACK_DIR" ]] || { echo "Refusing to replace existing hacBrewPack directory" >&2; exit 1; }
        git clone "$HACBREWPACK_REPO" "$HACBREWPACK_DIR"
    fi
    git -C "$HACBREWPACK_DIR" checkout --detach "${HACBREWPACK_REF:-56698df456eac4d5076e85499fe045a90fba790b}"
    if [[ ! -f "${HACBREWPACK_DIR}/config.mk" && -f "${HACBREWPACK_DIR}/config.mk.template" ]]; then
        cp "${HACBREWPACK_DIR}/config.mk.template" "${HACBREWPACK_DIR}/config.mk"
    fi
    make -C "$HACBREWPACK_DIR"
    HACBREWPACK="${HACBREWPACK_DIR}/hacbrewpack"
fi

if [[ "$PREPARE_ONLY" == false && ! -x "$HACBREWPACK" ]]; then
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

# Always bring program and bundled support files up to date before packaging.
cmake --preset Release
cmake --build "$BUILD_DIR"

[[ -f "$ELF" ]] || { echo "Missing ELF: $ELF" >&2; exit 1; }
[[ -f "$NACP" ]] || { echo "Missing NACP: $NACP" >&2; exit 1; }
[[ -d "$ROMFS_DIR" ]] || { echo "Missing ROMFS: $ROMFS_DIR" >&2; exit 1; }
[[ -f "${ROOT_DIR}/assets/icon.jpg" ]] || { echo "Missing app icon" >&2; exit 1; }

NSP_DIR="$(realpath -m "$NSP_DIR")"
BUILD_DIR="$(realpath -m "$BUILD_DIR")"
case "$NSP_DIR" in
    "$ROOT_DIR"/build/*) ;;
    *) echo "NSP_DIR must be a dedicated directory below $ROOT_DIR/build" >&2; exit 1 ;;
esac
[[ "$NSP_DIR" != "$BUILD_DIR" && "$NSP_DIR" != "$BUILD_DIR/"* ]] || { echo "NSP_DIR overlaps the app build directory" >&2; exit 1; }
rm -rf -- "$NSP_DIR"
mkdir -p "${NSP_DIR}/exefs" "${NSP_DIR}/control" "${NSP_DIR}/romfs" "${OUTPUT_DIR}"

"$ELF2NSO" "$ELF" "${NSP_DIR}/exefs/main"
"$NACPTOOL" --create "MM HATS INSTALLER" "TechRepairs4U" "$VERSION" \
    "${NSP_DIR}/control/control.nacp" "--titleid=${TITLE_ID}"
cp -R "${ROMFS_DIR}/." "${NSP_DIR}/romfs/"

# CMake supplies the identical repair package for both NSP and NRO.
[[ -s "${NSP_DIR}/romfs/mm-package/switch/mm-tools/hats-installer.bin" ]] || {
    echo "Missing bundled repair payload" >&2; exit 1;
}

for language in \
    AmericanEnglish BritishEnglish Japanese French German LatinAmericanSpanish \
    Spanish Italian Dutch CanadianFrench Portuguese Russian Korean \
    TraditionalChinese SimplifiedChinese BrazilianPortuguese; do
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

if [[ "$PREPARE_ONLY" == true ]]; then
    echo "Prepared full NSP program, metadata, control data and bundled assets in $NSP_DIR"
    exit 0
fi

# Pass only the two keys this packer uses. Older hacBrewPack versions print
# unknown key values, so never pass an unfiltered prod.keys directly.
umask 077
FILTERED_KEYSET="$(mktemp "${TMPDIR:-/tmp}/mm-hats-keys.XXXXXX")"
trap 'rm -f -- "$FILTERED_KEYSET"' EXIT
python3 - "$KEYSET" "$FILTERED_KEYSET" <<'PY'
from pathlib import Path
import re
import sys
values = {}
for line in Path(sys.argv[1]).read_text(encoding='utf-8-sig').splitlines():
    if '=' in line:
        name, value = (part.strip() for part in line.split('=', 1))
        values[name] = value
required = {'header_key': 64, 'key_area_key_application_00': 32}
for name, length in required.items():
    if not re.fullmatch(r'[0-9a-fA-F]{%d}' % length, values.get(name, '')) or int(values[name], 16) == 0:
        sys.exit('Missing or invalid required key: ' + name)
Path(sys.argv[2]).write_text(''.join(f'{name} = {values[name]}\n' for name in required))
PY
HACBREWPACK_HELP="$("$HACBREWPACK" --help 2>&1 || true)"
HACBREWPACK_ARGS=(--keyset "$FILTERED_KEYSET" --nologo)
if grep -q -- '--titleid' <<< "$HACBREWPACK_HELP"; then
    HACBREWPACK_ARGS+=(
        --titleid "$TITLE_ID"
        --titlename "MM HATS INSTALLER"
        --titlepublisher "TechRepairs4U"
    )
else
    echo "hacBrewPack has no --titleid override; writing the title ID into NACP before packaging."
    "$NACPTOOL" --create "MM HATS INSTALLER" "TechRepairs4U" "$VERSION" \
        "${NSP_DIR}/control/control.nacp" "--titleid=${TITLE_ID}"
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
