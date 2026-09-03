#pragma once

#include <SloppyDigits.h>

#include <memory>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

/**
 * 倒数日 (Countdown): up to MAX 5 events (label + target date), persisted in
 * CountdownStore. The screen lists all events with their remaining days;
 * tapping an event opens its actions popup (set date / rename / delete);
 * tapping the add row appends a new event. Back returns to the Apps menu.
 */
class CountdownActivity final : public Activity {
 public:
  CountdownActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Countdown", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class PopupTarget { None, AddRow, Event, Label };

  void refreshRows();
  void showActionPopup(size_t entryIndex);
  void showAddFlow();
  void runEventAction(size_t entryIndex, int action);
  void openDateSelection(size_t entryIndex);  // entryIndex == SIZE_MAX for the add flow
  void openLabelPicker(size_t entryIndex);
  void applyLabelChoice(size_t entryIndex, int choice);
  void openLabelEditor(size_t entryIndex);
  int hitRow(int y) const;

  std::unique_ptr<sloppy::Style> heroStyle_;
  std::unique_ptr<sloppy::Seeds> heroSeeds_;
  OptionPopup actionPopup_;
  int actionChoice_ = -1;
  int selectedRow_ = 0;  // button-modality selection over [events..., add row]
  PopupTarget popupTarget_ = PopupTarget::None;
  size_t popupEntry_ = 0;

  static constexpr int ROW_H = 96;
  static constexpr int ADD_ROW_H = 72;
  int listTop_ = 0;
  int rowsDrawn_ = 0;  // event rows painted in the last render (for hit testing)
  bool hasAddRow_ = false;
};
