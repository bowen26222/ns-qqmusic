#pragma once

#include <switch.h>
#include <string>
#include <vector>

namespace sdmc {

    Result Open();
    void Close();

    Result OpenFile(FsFile *file, const char* path, int open_mode = FsOpenMode_Read);
    Result OpenDir(FsDir *dir, const char *path, int open_mode);

    Result GetType(const char* path, FsDirEntryType* type);
    bool FileExists(const char* path);

    // 目录枚举（名字 + 修改时间），用于本地缓存按 LRU 淘汰。
    struct DirEntry {
        std::string name;
        u64 mtime;
    };
    Result ListDir(const char* path, std::vector<DirEntry>* out);

    Result CreateFolder(const char* path);
    Result DeleteFile(const char* path);
    Result RenameFile(const char* old_path, const char* new_path);

    // Create (overwrite) a file and write the whole buffer. For logs/config.
    Result WriteFile(const char* path, const void* data, size_t size);
}
