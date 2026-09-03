#pragma once

#include "../../Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

class ReadingStatsExtendedActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int scrollOffset = 0;
  int selectedIndex = 0;
  bool waitForConfirmRelease = false;
  bool waitForBackRelease = false;
  Rect bookListRect_{};
  int bookListInnerTop_ = 0;
  int bookRowHeight_ = 110;

  bool usesInxLayout() const;
  void openSelectedBook();
  void loopInx();
  void renderInx();

 public:
  explicit ReadingStatsExtendedActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStatsExtended", renderer, mappedInput) {}

  // INX: share the tab-bar header with the stats tab instead of the plain
  // text header.
  MainTab mainTab() const override { return MainTab::Statistics; }

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
