#pragma once

#include <tesla.hpp>
#include <string>
#include <vector>

#include "elm_overlayframe.hpp"

class BrowserGui final : public tsl::Gui {
  private:
    QqMusicOverlayFrame* m_frame;
    tsl::elm::List *m_list;
    FsFileSystem m_fs;
    bool m_fs_open;
    std::string cwd;
    struct UnplayableEntry {
        std::string name;
        s8 type;
    };
    std::string m_scan_report;
    bool m_report_truncated = false;

  public:
    BrowserGui();
    ~BrowserGui();

    tsl::elm::Element *createUI() override;
    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override;

  private:
    void scanCwd();
    void upCwd();
    bool makePath(const std::string &name, std::string &path);
    bool readDirectory(std::vector<std::string> *folders, std::vector<std::string> &files,
                       size_t limit, std::vector<UnplayableEntry> *other = nullptr);
    void recordScanEntry(const FsDirectoryEntry &entry);
    void saveScanReport();
    void queueAlbum(bool play);
    void infoAlert(const std::string &title, const std::string &text);
};
