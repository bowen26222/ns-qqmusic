#pragma once

#include <tesla.hpp>
#include <functional>
#include <string>
#include <vector>

// A self-contained keyboard for the sign in page (overlays run as
// AppletType_None so we can't access the switch's native keyboard UI. so i had to make this lol)
class KeyboardGui final : public tsl::Gui {
  public:
    using DoneCb = std::function<void(const std::string &)>;
    KeyboardGui(const std::string &title, const std::string &initial, DoneCb on_done);
    tsl::elm::Element *createUI() override;

  private:
    std::string m_title;
    std::string m_initial;
    DoneCb m_on_done;
};
