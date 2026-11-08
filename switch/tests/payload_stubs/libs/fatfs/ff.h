#pragma once
#include <utils/types.h>
typedef unsigned UINT;
typedef struct { void* handle; } FIL;
typedef struct { void* handle; } DIR;
typedef struct { u8 fattrib; char fname[256]; } FILINFO;
enum { FR_OK, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE,
    FR_NO_PATH, FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED, FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED, FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE,
    FR_TOO_MANY_OPEN_FILES, FR_INVALID_PARAMETER };
enum { FA_READ = 1, FA_WRITE = 2, FA_OPEN_EXISTING = 0, FA_CREATE_ALWAYS = 8, AM_DIR = 16 };
#ifdef __cplusplus
extern "C" {
#endif
int f_open(FIL*, const char*, u8);
int f_close(FIL*);
int f_read(FIL*, void*, UINT, UINT*);
int f_write(FIL*, const void*, UINT, UINT*);
int f_sync(FIL*);
u64 f_size(FIL*);
int f_stat(const char*, FILINFO*);
int f_opendir(DIR*, const char*);
int f_readdir(DIR*, FILINFO*);
int f_closedir(DIR*);
int f_unlink(const char*);
int f_mkdir(const char*);
int f_chmod(const char*, u8, u8);
#ifdef __cplusplus
}
#endif
