#pragma once

#include "../../Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

struct ReadingBookStats;

class ReadingStatsActivity final : public Activity {
  static constexpr int kInxDayBars = 7;
  static constexpr int kInxBookPreview = 3;

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool waitForConfirmRelease = false;
  bool waitForBackRelease = false;
  bool mainTabEnabled = false;
  Rect moreHitRect_{};
  Rect dayBarHit_[kInxDayBars];
  uint32_t dayBarOrdinal_[kInxDayBars]{};
  int bookPreviewCount_ = 0;
  int bookPreviewIndex_[kInxBookPreview]{};
  Rect bookPreviewHit_[kInxBookPreview];
  void openSelectedEntry();
  void openDayDetail(uint32_t dayOrdinal);
  void handleInxTap(int x, int y);
  void confirmRemoveSelectedBook();
  void guardBackReturn();
  bool usesInxLayout() const;
  void renderInx();

 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool mainTabEnabled = false)
      : Activity("ReadingStats", renderer, mappedInput), mainTabEnabled(mainTabEnabled) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  MainTab mainTab() const override { return mainTabEnabled ? MainTab::Statistics : MainTab::None; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;
};
