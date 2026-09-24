#pragma once

#include <tesla.hpp>
#include <string>

// Settings: music folder, seek interval, live volume and game blacklist.
class SettingsGui final : public tsl::Gui {
  public:
    SettingsGui();
    tsl::elm::Element *createUI() override;

  private:
    tsl::elm::List *m_list = nullptr;
    int m_seek_skip = 15;
};