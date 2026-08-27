#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"

class ReaderTapZonesActivity final : public Activity {
 public:
  explicit ReaderTapZonesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReaderTapZones", renderer, mappedInput) {}

  void loop() override;
  void render(RenderLock&&) override;

 private:
  OptionPopup optionPopup_;
  int selected_ = 4;

  void openPicker();
  static const char* actionLabel(uint8_t action);
};
