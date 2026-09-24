#include "gui_browser.hpp"

#include "client.h"
#include "gui_main.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {

    bool StringTextCompare(const std::string& lhs, const std::string& rhs) {
        const int order = strcasecmp(lhs.c_str(), rhs.c_str());
        return order != 0 ? order < 0 : lhs < rhs;
    }

    bool EndsWith(const char *name, const char *ext) {
        const size_t name_length = std::strlen(name);
        const size_t ext_length = std::strlen(ext);
        return name_length >= ext_length && strcasecmp(name + name_length - ext_length, ext) == 0;
    }

    constexpr const std::array SupportedTypes = {
#ifdef WANT_MP3
        ".mp3",
#endif
#ifdef WANT_FLAC
        ".flac",
#endif
#ifdef WANT_WAV
        ".wav",
        ".wave",
#endif
    };

    bool SupportsType(const char *name) {
        for (auto &ext : SupportedTypes)
            if (EndsWith(name, ext))
                return true;
        return false;
    }

    std::string ResultText(Result rc) {
        char text[16];
        std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned int>(rc));
        return text;
    }

    constexpr const char *base_path = "/music";
    constexpr size_t playlist_limit = 300;
    constexpr size_t browser_limit = 2048;
    constexpr size_t report_limit = 64 * 1024;

}

BrowserGui::BrowserGui()
    : m_frame(nullptr), m_list(new tsl::elm::List()), m_fs(), m_fs_open(false), cwd(base_path) {
    const Result rc = fsOpenSdCardFileSystem(&m_fs);
    if (R_FAILED(rc)) {
        infoAlert("打开SD卡失败", ResultText(rc));
        return;
    }
    m_fs_open = true;
    scanCwd();
}

BrowserGui::~BrowserGui() {
    if (m_fs_open)
        fsFsClose(&m_fs);
}

tsl::elm::Element *BrowserGui::createUI() {
    m_frame = new QqMusicOverlayFrame();
    m_frame->setDescription("\uE0E1 返回  \uE0E0 打开/添加  \uE0E3 导出日志");
    m_frame->setContent(m_list);
    return m_frame;
}

bool BrowserGui::handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        if (cwd != base_path) {
            upCwd();
            return true;
        }
    } else if (keysDown & HidNpadButton_X) {
        queueAlbum(false);
        return true;
    } else if (keysDown & HidNpadButton_Y) {
        saveScanReport();
        return true;
    }
    return false;
}

void BrowserGui::infoAlert(const std::string &title, const std::string &text) {
    if (m_frame) {
        m_frame->setToast(title, text);
    } else {
        m_list->addItem(new tsl::elm::CategoryHeader(title));
        m_list->addItem(new tsl::elm::ListItem(text));
    }
}

bool BrowserGui::makePath(const std::string &name, std::string &path) {
    if (cwd.size() + 1 + name.size() >= FS_MAX_PATH) {
        infoAlert("Path too long", "Maximum " + std::to_string(FS_MAX_PATH - 1) + " UTF-8 bytes.");
        return false;
    }
    path = cwd + "/" + name;
    return true;
}

bool BrowserGui::readDirectory(std::vector<std::string> *folders, std::vector<std::string> &files,
                               size_t limit, std::vector<UnplayableEntry> *other) {
    if (folders) {
        m_scan_report = "QQMusic browser scan/1\npath_utf8=" + cwd + "\n";
        m_report_truncated = false;
    }
    if (!m_fs_open) {
        if (folders)
            m_scan_report += "SD filesystem unavailable\n";
        infoAlert("SD card unavailable", "Cannot read album.");
        return false;
    }

    FsDir dir;
    s64 pre_count = -1;
    u32 mode = FsDirOpenMode_ReadFiles | (folders ? FsDirOpenMode_ReadDirs : 0);
    Result rc = fsFsOpenDirectory(&m_fs, cwd.c_str(), mode, &dir);
    if (R_SUCCEEDED(rc)) {
        Result count_rc = fsDirGetEntryCount(&dir, &pre_count);
        if (folders)
            m_scan_report += "Open standard mode=0x" + std::to_string(mode) + " ok, pre_count=" +
                (R_SUCCEEDED(count_rc) ? std::to_string(pre_count) : "fail " + ResultText(count_rc)) + "\n";
    } else {
        if (folders)
            m_scan_report += "Open standard mode failed: " + ResultText(rc) + ", retrying with NoFileSize\n";
        mode |= FsDirOpenMode_NoFileSize;
        rc = fsFsOpenDirectory(&m_fs, cwd.c_str(), mode, &dir);
        if (R_SUCCEEDED(rc)) {
            Result count_rc = fsDirGetEntryCount(&dir, &pre_count);
            if (folders)
                m_scan_report += "Open NoFileSize mode=0x" + std::to_string(mode) + " ok, pre_count=" +
                    (R_SUCCEEDED(count_rc) ? std::to_string(pre_count) : "fail " + ResultText(count_rc)) + "\n";
        }
    }
    if (R_FAILED(rc)) {
        if (folders)
            m_scan_report += "OpenDirectory failed: " + ResultText(rc) + "\n";
        infoAlert("打开目录失败", ResultText(rc));
        return false;
    }
    tsl::hlp::ScopeGuard dirGuard([&] { fsDirClose(&dir); });

    std::vector<FsDirectoryEntry> entries(64);
    while (true) {
        s64 count = 0;
        rc = fsDirRead(&dir, &count, entries.size(), entries.data());
        if (R_FAILED(rc)) {
            if (folders)
                m_scan_report += "ReadDirectory failed: " + ResultText(rc) + "\n";
            infoAlert("读取目录失败", ResultText(rc));
            return false;
        }
        if (folders)
            m_scan_report += "Batch: count=" + std::to_string(count) + "\n";
        if (count == 0)
            break;

        for (s64 i = 0; i < count; ++i) {
            const auto &entry = entries[i];
            if (folders)
                recordScanEntry(entry);
            const bool is_folder = folders && entry.type == FsDirEntryType_Dir;
            const bool is_song = entry.type == FsDirEntryType_File && SupportsType(entry.name);
            if (!is_folder && !is_song && !other)
                continue;
            if (is_folder && (!std::strcmp(entry.name, ".") || !std::strcmp(entry.name, "..")))
                continue;

            if (files.size() + (folders ? folders->size() : 0) + (other ? other->size() : 0) >= limit) {
                infoAlert(folders ? "Folder too large" : "Album too large",
                          "Limit: " + std::to_string(limit) + (folders ? " entries." : " songs; queue unchanged."));
                if (folders)
                    m_scan_report += "Scan stopped: entry limit reached\n";
                return false;
            }
            if (is_folder)
                folders->emplace_back(entry.name);
            else if (is_song)
                files.emplace_back(entry.name);
            else
                other->push_back({entry.name, entry.type});
        }
    }

    if (folders)
        std::sort(folders->begin(), folders->end(), StringTextCompare);
    std::sort(files.begin(), files.end(), StringTextCompare);
    if (other)
        std::sort(other->begin(), other->end(), [](const auto &a, const auto &b) {
            return StringTextCompare(a.name, b.name);
        });
    if (folders)
        m_scan_report += "Complete: folders=" + std::to_string(folders->size()) +
            " songs=" + std::to_string(files.size()) +
            " other=" + std::to_string(other ? other->size() : 0) + "\n";
    return true;
}

void BrowserGui::scanCwd() {
    removeFocus();
    m_list->clear();
    m_list->addItem(new tsl::elm::CategoryHeader(cwd, true));

    std::vector<std::string> folders, files;
    std::vector<UnplayableEntry> other;
    if (!readDirectory(&folders, files, browser_limit, &other))
        return;

    m_list->addItem(new tsl::elm::CategoryHeader(
        "Folders: " + std::to_string(folders.size()) + "  Songs: " + std::to_string(files.size()) +
        "  Other: " + std::to_string(other.size())));
    auto play_album = new tsl::elm::ListItem("Play album", "Replace queue");
    play_album->setClickListener([this](u64 down) -> bool {
        if (!(down & HidNpadButton_A))
            return false;
        queueAlbum(true);
        return true;
    });
    m_list->addItem(play_album);

    auto add_album = new tsl::elm::ListItem("Add album", "Append songs");
    add_album->setClickListener([this](u64 down) -> bool {
        if (!(down & HidNpadButton_A))
            return false;
        queueAlbum(false);
        return true;
    });
    m_list->addItem(add_album);

    if (!folders.empty())
        m_list->addItem(new tsl::elm::CategoryHeader("Folders / albums"));
    for (const auto &name : folders) {
        std::string album_tag;
        std::string folder_path;
        if (makePath(name, folder_path)) {
            FsDir sub_dir;
            if (R_SUCCEEDED(fsFsOpenDirectory(&m_fs, folder_path.c_str(), FsDirOpenMode_ReadFiles, &sub_dir))) {
                FsDirectoryEntry sub_entries[4];
                s64 sub_count = 0;
                if (R_SUCCEEDED(fsDirRead(&sub_dir, &sub_count, 4, sub_entries)) && sub_count > 0) {
                    for (s64 s = 0; s < sub_count; s++) {
                        if (SupportsType(sub_entries[s].name)) {
                            std::string song_path = folder_path + "/" + sub_entries[s].name;
                            QqMusicTrackMeta meta = {};
                            if (R_SUCCEEDED(qqmusicGetPathMeta(song_path.c_str(), &meta)) && meta.valid && meta.album[0]) {
                                album_tag = meta.album;
                                break;
                            }
                        }
                    }
                }
                fsDirClose(&sub_dir);
            }
        }
        auto item = new tsl::elm::ListItem(name + "/", album_tag);
        item->setClickListener([this, name](u64 down) -> bool {
            if (!(down & HidNpadButton_A))
                return false;
            std::string path;
            if (makePath(name, path)) {
                cwd = std::move(path);
                scanCwd();
            }
            return true;
        });
        m_list->addItem(item);
    }

    m_list->addItem(new tsl::elm::CategoryHeader(files.empty() ? "当前文件夹无曲目" : "歌曲列表 (按A添加)"));
    for (const auto &name : files) {
        std::string full_path;
        makePath(name, full_path);

        QqMusicTrackMeta meta = {};
        std::string display_title = name;
        std::string display_artist;
        if (!full_path.empty() && R_SUCCEEDED(qqmusicGetPathMeta(full_path.c_str(), &meta)) && meta.valid && meta.title[0]) {
            display_title = meta.title;
            if (meta.artist[0]) {
                display_artist = meta.artist;
            }
        }

        auto item = new tsl::elm::ListItem(display_title, display_artist);
        item->setClickListener([this, full_path, display_title](u64 down) -> bool {
            if (!(down & HidNpadButton_A))
                return false;
            if (full_path.empty())
                return true;
            u32 count = 0;
            Result rc = qqmusicGetQueueSize(&count);
            if (R_FAILED(rc)) {
                infoAlert("读取队列失败", ResultText(rc));
                return true;
            }
            if (count >= playlist_limit) {
                infoAlert("队列已满", "上限300首歌曲");
                return true;
            }
            rc = qqmusicEnqueue(full_path.c_str(), 0);
            if (R_FAILED(rc))
                infoAlert("添加歌曲失败", ResultText(rc));
            else
                infoAlert("队列已更新", "已添加: " + display_title);
            return true;
        });
        m_list->addItem(item);
    }

    if (!other.empty())
        m_list->addItem(new tsl::elm::CategoryHeader("Other entries (not playable)"));
    for (const auto &entry : other) {
        const std::string type = entry.type == FsDirEntryType_File ? "FS: file" :
            "FS type: " + std::to_string(entry.type);
        auto item = new tsl::elm::ListItem(entry.name, type);
        item->setClickListener([this, type](u64 keys) {
            if (!(keys & HidNpadButton_A))
                return false;
            infoAlert(type + "; not a supported song",
                      "If this is an album folder, check its SD Archive attribute. Y saves scan.");
            return true;
        });
        m_list->addItem(item);
    }
}

void BrowserGui::upCwd() {
    if (cwd == base_path)
        return;
    const size_t separator = cwd.find_last_of('/');
    if (separator == std::string::npos || separator < std::strlen(base_path))
        cwd = base_path;
    else
        cwd.resize(separator);
    scanCwd();
}

void BrowserGui::queueAlbum(bool play) {
    std::vector<std::string> files;
    if (!readDirectory(nullptr, files, playlist_limit))
        return;
    if (files.empty()) {
        infoAlert("当前专辑无歌曲", "子文件夹为独立专辑");
        return;
    }

    // Validate every full path before changing the queue, preserving the original UTF-8 bytes.
    for (auto &file : files) {
        std::string path;
        if (!makePath(file, path))
            return;
        file = std::move(path);
    }

    Result rc;
    if (play) {
        rc = qqmusicPause();
        if (R_FAILED(rc)) {
            infoAlert("Couldn't pause playback", ResultText(rc));
            return;
        }
        rc = qqmusicClearQueue();
        if (R_FAILED(rc)) {
            infoAlert("Couldn't replace queue", ResultText(rc));
            return;
        }
    } else {
        u32 count = 0;
        rc = qqmusicGetQueueSize(&count);
        if (R_FAILED(rc)) {
            infoAlert("读取队列失败", ResultText(rc));
            return;
        }
        if (count > playlist_limit || files.size() > playlist_limit - count) {
            infoAlert("超出队列容量", "上限300首，队列未变动");
            return;
        }
    }

    size_t added = 0;
    for (const auto &path : files) {
        rc = qqmusicEnqueue(path.c_str(), 0);
        if (R_FAILED(rc)) {
            infoAlert(play ? "Album incomplete; paused" : "Album not fully added",
                      std::to_string(added) + "/" + std::to_string(files.size()) + " added; " + ResultText(rc));
            return;
        }
        ++added;
    }

    if (play) {
        rc = qqmusicSelect(0);
        if (R_FAILED(rc)) {
            infoAlert("Album queued; play failed", ResultText(rc));
            return;
        }
    }
    infoAlert(play ? "正在播放专辑" : "专辑已加入队列", "共 " + std::to_string(added) + " 首歌曲");
    tsl::changeTo<MainGui>();
}

void BrowserGui::recordScanEntry(const FsDirectoryEntry &entry) {
    if (m_report_truncated)
        return;
    const size_t length = std::strlen(entry.name);
    if (m_scan_report.size() + 3 * length + 96 > report_limit) {
        m_scan_report += "Raw entry report truncated at 64 KiB; scanning continues.\n";
        m_report_truncated = true;
        return;
    }
    m_scan_report += "type=" + std::to_string(entry.type) + " name_utf8=" + entry.name + "\nname_hex=";
    constexpr char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(entry.name[i]);
        m_scan_report += hex[byte >> 4];
        m_scan_report += hex[byte & 15];
    }
    m_scan_report += '\n';
}

void BrowserGui::saveScanReport() {
    if (!m_fs_open || m_scan_report.empty()) {
        infoAlert("Scan report unavailable", "No directory scan was captured.");
        return;
    }
    constexpr const char *path = "/qqmusic-browser.log";
    FsFile file;
    Result rc = fsFsOpenFile(&m_fs, path, FsOpenMode_Write, &file);
    if (R_FAILED(rc)) {
        rc = fsFsCreateFile(&m_fs, path, m_scan_report.size(), 0);
        if (R_SUCCEEDED(rc))
            rc = fsFsOpenFile(&m_fs, path, FsOpenMode_Write, &file);
    }
    if (R_FAILED(rc)) {
        infoAlert("Couldn't save scan", ResultText(rc));
        return;
    }
    rc = fsFileSetSize(&file, m_scan_report.size());
    if (R_SUCCEEDED(rc))
        rc = fsFileWrite(&file, 0, m_scan_report.data(), m_scan_report.size(), FsWriteOption_Flush);
    fsFileClose(&file);
    if (R_FAILED(rc))
        infoAlert("Couldn't save scan", ResultText(rc));
    else
        infoAlert("Scan saved", "SD:/qqmusic-browser.log (UTF-8 and raw filename bytes)");
}
