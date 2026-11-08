"""Exercise current devkitPro newlib registration against the 0x559 regression.

Pass libgloss/libsysbase/iosupport.c from devkitPro/newlib commit
57bd8e1ade3cf46d2e61a72516ac8bfaf8fdfd62. No Switch or SD card is accessed.
"""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).resolve()
assert hashlib.sha256(source.read_bytes()).hexdigest() == (
    "7eebe6f20fe9c6f334eb45368c74488a2407fa94bf6b9582e46f215c273afe5e"
), "Use the pinned devkitPro newlib source described above"
repo = Path(__file__).resolve().parents[1]
for path in (repo / "sphaira/source").rglob("*.cpp"):
    assert "FixDkpBug" not in path.read_text(), f"Obsolete device-table workaround: {path}"

# The host stub provides only the devoptab fields used by iosupport.c; the
# registry implementation itself is compiled from the pinned library source.
header = r'''
#pragma once
#include <sys/types.h>
struct _reent;
enum { STD_MAX = 35 };
typedef struct {
    const char *name;
    int structSize;
    ssize_t (*write_r)(struct _reent*, void*, const char*, size_t);
    int dirStateSize;
} devoptab_t;
extern const devoptab_t dotab_stdnull;
extern const devoptab_t *devoptab_list[STD_MAX];
int AddDevice(const devoptab_t*);
int RemoveDevice(const char*);
int FindDevice(const char*);
'''
test = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>
int main(void) {
    const devoptab_t sd = { .name = "sdmc" };
    const devoptab_t romfs = { .name = "romfs" };
    assert(AddDevice(&sd) == 3);
    // Reproduce the removed startup workaround. AddDevice fails despite
    // there being no real mounts in the remaining slots. libnx translates
    // that registration failure to Module_Libnx / OutOfMemory (0x559).
    for (int i = 0; i < STD_MAX; ++i)
        if (!devoptab_list[i]) devoptab_list[i] = &dotab_stdnull;
    assert(AddDevice(&romfs) == -1);
    assert((345 | (2 << 9)) == 0x559);
    puts("Reproduced old startup: certificate mount registration fails (libnx 0x559).");
    for (int i = 3; i < STD_MAX; ++i)
        if (devoptab_list[i] == &dotab_stdnull) devoptab_list[i] = NULL;
    for (int i = 0; i < 100; ++i) {
        assert(AddDevice(&romfs) == 4);
        assert(FindDevice("romfs:/hats-ca-bundle.pem") == 4);
        assert(RemoveDevice("romfs:") == 0);
        assert(devoptab_list[4] == NULL);
        assert(FindDevice("romfs:/hats-ca-bundle.pem") == -1);
        assert(devoptab_list[3] == &sd);
    }
    // Exercise slots beyond the old 15-entry bug and actual exhaustion.
    devoptab_t mounts[31] = {0};
    char names[31][12];
    for (int i = 0; i < 31; ++i) {
        snprintf(names[i], sizeof(names[i]), "test%d", i);
        mounts[i].name = names[i];
        assert(AddDevice(&mounts[i]) == i + 4);
    }
    assert(AddDevice(&romfs) == -1);
    for (int i = 0; i < 31; ++i) {
        char path[16];
        snprintf(path, sizeof(path), "%s:", names[i]);
        assert(RemoveDevice(path) == 0);
    }
    assert(AddDevice(&romfs) == 4);
    assert(RemoveDevice("romfs:") == 0);
    for (int i = 0; i < 3; ++i) assert(devoptab_list[i] == &dotab_stdnull);
    assert(FindDevice("sdmc:/") == 3);
    puts("100 mount/unmount cycles, high slots, exhaustion/recovery and existing-device preservation passed.");
}
'''
with tempfile.TemporaryDirectory(prefix="mm-devoptab-test-") as directory:
    tmp = Path(directory)
    (tmp / "sys").mkdir()
    (tmp / "sys/iosupport.h").write_text(header)
    (tmp / "test.c").write_text(test)
    executable = tmp / "devoptab-test"
    subprocess.run(["cc", "-std=c11", "-I", str(tmp), str(source),
                    str(tmp / "test.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
