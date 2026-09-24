#include "sdmc.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

namespace sdmc {

    namespace {
        FsFileSystem sdmc;

        // 同一个 FsFileSystem 会话不允许两个线程同时发起请求：libnx 的 fs 协议是
        // 请求/应答式，交错下发会让应答错配，表现为调用永不返回（整个界面卡死）。
        // 现在有 IPC 线程 / 播放线程 / 封面线程三方同时用 SD，必须串行化。
        Mutex g_fs_mutex;
        bool g_fs_mutex_ready = false;

        void FsLockInit() {
            if (!g_fs_mutex_ready) {
                mutexInit(&g_fs_mutex);
                g_fs_mutex_ready = true;
            }
        }

        struct FsGuard {
            FsGuard() { FsLockInit(); mutexLock(&g_fs_mutex); }
            ~FsGuard() { mutexUnlock(&g_fs_mutex); }
            FsGuard(const FsGuard &) = delete;
            FsGuard &operator=(const FsGuard &) = delete;
        };
    }

    Result Open() {
        FsLockInit(); // 单线程阶段初始化（main 在任何工作线程启动前调用），避免惰性初始化竞争
        return fsOpenSdCardFileSystem(&sdmc);
    }

    void Close() {
        fsFsClose(&sdmc);
    }

    Result OpenFile(FsFile *file, const char *path, int open_mode) {
        FsGuard guard;
        char buf[FS_MAX_PATH];
        std::strncpy(buf, path, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return fsFsOpenFile(&sdmc, buf, open_mode, file);
    }

    Result OpenDir(FsDir *dir, const char *path, int open_mode) {
        FsGuard guard;
        char buf[FS_MAX_PATH];
        std::strncpy(buf, path, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return fsFsOpenDirectory(&sdmc, buf, open_mode, dir);
    }

    Result GetType(const char* path, FsDirEntryType* type) {
        FsGuard guard;
        char buf[FS_MAX_PATH];
        std::strncpy(buf, path, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return fsFsGetEntryType(&sdmc, buf, type);
    }

    bool FileExists(const char* path) {
        FsDirEntryType type;
        return R_SUCCEEDED(GetType(path, &type)) && type == FsDirEntryType_File;
    }

    Result CreateFolder(const char* path) {
        FsGuard guard;
        char buf[FS_MAX_PATH];
        std::strncpy(buf, path, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return fsFsCreateDirectory(&sdmc, buf);
    }

    Result DeleteFile(const char* path) {
        FsGuard guard;
        char buf[FS_MAX_PATH];
        std::strncpy(buf, path, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return fsFsDeleteFile(&sdmc, buf);
    }

    Result ListDir(const char* path, std::vector<DirEntry>* out) {
        FsGuard guard;
        out->clear();
        char dir_path[FS_MAX_PATH];
        std::strncpy(dir_path, path, sizeof(dir_path) - 1);
        dir_path[sizeof(dir_path) - 1] = '\0';
        FsDir dir;
        Result rc = fsFsOpenDirectory(&sdmc, dir_path, FsDirOpenMode_ReadFiles, &dir);
        if (R_FAILED(rc)) return rc;

        std::vector<FsDirectoryEntry> entries(32);
        char full[FS_MAX_PATH];
        for (;;) {
            s64 read_n = 0;
            rc = fsDirRead(&dir, &read_n, entries.size(), entries.data());
            if (R_FAILED(rc) || read_n <= 0) break;
            for (s64 i = 0; i < read_n; i++) {
                DirEntry e{};
                e.name = entries[i].name;
                e.mtime = 0;
                std::snprintf(full, sizeof(full), "%s/%s", path, entries[i].name);
                FsTimeStampRaw ts{};
                if (R_SUCCEEDED(fsFsGetFileTimeStampRaw(&sdmc, full, &ts)))
                    e.mtime = ts.modified;
                out->push_back(std::move(e));
            }
            if (read_n < (s64)entries.size()) break;
        }
        fsDirClose(&dir);
        return 0;
    }

    Result RenameFile(const char* old_path, const char* new_path) {
        FsGuard guard;
        char old_buf[FS_MAX_PATH];
        char new_buf[FS_MAX_PATH];
        std::strncpy(old_buf, old_path, sizeof(old_buf) - 1);
        old_buf[sizeof(old_buf) - 1] = '\0';
        std::strncpy(new_buf, new_path, sizeof(new_buf) - 1);
        new_buf[sizeof(new_buf) - 1] = '\0';
        return fsFsRenameFile(&sdmc, old_buf, new_buf);
    }
    Result WriteFile(const char* path, const void* data, size_t size) {
        FsGuard guard;
        char buf[FS_MAX_PATH];
        std::strncpy(buf, path, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        fsFsDeleteFile(&sdmc, buf);   // ignore error if absent
        Result rc = fsFsCreateFile(&sdmc, buf, (s64)size, 0);
        if (R_FAILED(rc)) return rc;
        FsFile file;
        rc = fsFsOpenFile(&sdmc, buf, FsOpenMode_Write, &file);
        if (R_FAILED(rc)) return rc;
        rc = fsFileWrite(&file, 0, data, size, FsWriteOption_Flush);
        fsFileClose(&file);
        return rc;
    }

}
