#include "MainTabOrderSettingsActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace {
constexpr StrId positions[] = {StrId::STR_TAB_POSITION_1, StrId::STR_TAB_POSITION_2, StrId::STR_TAB_POSITION_3,
                               StrId::STR_TAB_POSITION_4, StrId::STR_TAB_POSITION_5};

StrId tabTitle(const MainTab tab) {
  switch (tab) {
    case MainTab::Recent:
      return StrId::STR_NOW_READING;
    case MainTab::Library:
      return StrId::STR_PAPER_LIBRARY;
    case MainTab::Statistics:
      return StrId::STR_PAPER_JOURNAL_TAB;
    case MainTab::Settings:
      return StrId::STR_SETTINGS_TITLE;
    case MainTab::Apps:
      return StrId::STR_APPS_TITLE;
    case MainTab::None:
      return StrId::STR_MAIN_TAB_ORDER;
  }
  return StrId::STR_MAIN_TAB_ORDER;
}
}  // namespace

void MainTabOrderSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  draftRanks = MainTabs::ranksFor(SETTINGS.mainTabOrder);
  draftStatus = DraftStatus::Saved;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
}

const char* MainTabOrderSettingsActivity::headerTitle() const { return tr(STR_MAIN_TAB_ORDER); }

bool MainTabOrderSettingsActivity::handleCustomInput() {
  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitForConfirmRelease = false;
    return true;
  }
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void MainTabOrderSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive() || index < 0 || index >= ROW_COUNT) return;
  app.clearTapFlash();
  if (index == RESET_ROW) {
    draftRanks = MainTabs::ranksFor(MainTabs::values);
    draftStatus = DraftStatus::Editing;
    requestUpdate();
    return;
  }
  if (index == SAVE_ROW) {
    MainTabs::Order order{};
    if (!MainTabs::orderFromRanks(draftRanks, order)) {
      showError(StrId::STR_TAB_ORDER_UNIQUE);
      return;
    }
    const auto previous = SETTINGS.mainTabOrder;
    SETTINGS.mainTabOrder = order;
    if (!SETTINGS.saveToFile()) {
      SETTINGS.mainTabOrder = previous;
      showError(StrId::STR_TAB_ORDER_SAVE_FAILED);
      return;
    }
    draftStatus = DraftStatus::Saved;
    requestUpdate();
    return;
  }
  optionPopup.show(tabTitle(MainTabs::values[index]), positions, TAB_COUNT, draftRanks[index] - 1,
                   [this, index](const int position) {
                     if (position < 0 || position >= TAB_COUNT) return;
                     draftRanks[index] = static_cast<uint8_t>(position + 1);
                     draftStatus = DraftStatus::Editing;
                     requestUpdate();
                   });
  requestUpdate();
}

void MainTabOrderSettingsActivity::showError(const StrId message) {
  optionPopup.show(StrId::STR_MAIN_TAB_ORDER, &message, 1, 0, [](int) {});
  requestUpdate();
}

void MainTabOrderSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(freeink::ui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                              static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < TAB_COUNT; ++i) {
    rows[i].label = I18N.get(tabTitle(MainTabs::values[i]));
    rankLabels[i][0] = static_cast<char>('0' + draftRanks[i]);
    rankLabels[i][1] = '\0';
    rows[i].value = rankLabels[i];
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  rows[SAVE_ROW].label = tr(STR_TAB_ORDER_SAVE);
  rows[SAVE_ROW].subtitle = tr(STR_TAB_ORDER_HINT);
  rows[SAVE_ROW].value = draftStatus == DraftStatus::Saved ? tr(STR_TAB_ORDER_SAVED) : nullptr;
  rows[SAVE_ROW].actionValue = SAVE_ROW;
  rows[RESET_ROW].label = tr(STR_TAB_ORDER_RESET);
  rows[RESET_ROW].actionValue = RESET_ROW;
  freeink::ui::ListProps props;
  props.items = rows;
  props.count = ROW_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = freeink::ui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props, true);
  screen.list(props);
}

void MainTabOrderSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}
