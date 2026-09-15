#pragma once

#include "activities/MainTab.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class MainTabOrderSettingsActivity final : public UiListActivity {
 public:
  MainTabOrderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("MainTabOrderSettings", renderer, mappedInput) {}
  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int TAB_COUNT = MainTabs::values.size();
  static constexpr int SAVE_ROW = TAB_COUNT;
  static constexpr int RESET_ROW = TAB_COUNT + 1;
  static constexpr int ROW_COUNT = TAB_COUNT + 2;
  enum class DraftStatus { Editing, Saved };
  MainTabs::Ranks draftRanks{};
  char rankLabels[TAB_COUNT][2]{};
  DraftStatus draftStatus = DraftStatus::Saved;
  OptionPopup optionPopup;
  freeink::ui::ListItem rows[ROW_COUNT]{};
  bool waitForConfirmRelease = false;

  int listCount() const override { return ROW_COUNT; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void showError(StrId message);
};
