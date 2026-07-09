#include "src/ui/screens/settings_screen.h"

#include "src/hal/display.h"
#include "src/hal/orientation.h"
#include "src/ui/font.h"
#include "src/ui/screens/library_screen.h" // g_libraryScreen
#include "src/ui/widgets.h"

// ============================================================================
//  Settings item table
//
//  Each item is either a toggle (no options) or a choice (a fixed option
//  list). Reading/writing the settings goes through the same set of Font:: functions
//  that are used by the Web UI. Toggles are modeled as choices' degenerate cousin:
//  get() returns 0/1, set() takes 0/1. Labels are mostly borrowed from Web UI too,
//  but we have to redefine some due to embedded html.
// ============================================================================

// Matches the library screen's folder indent so the expanded option lists
// read the same way as an opened folder.
static const int SETTINGS_OPTION_INDENT = 10;

struct SettingOption {
  int         value;
  const char* label;
};

struct SettingItem {
  const char*          label;
  const SettingOption* options;      // nullptr for toggles
  int                  optionCount;  // 0 for toggles
  int  (*get)();
  void (*set)(int);
};

static const SettingOption kSizeOptions[] = {
  {  8, D_SETTINGS_SIZE_8  },
  { 10, D_SETTINGS_SIZE_10 },
  { 12, D_SETTINGS_SIZE_12 },
  { 14, D_SETTINGS_SIZE_14 },
};

static const SettingOption kFamilyOptions[] = {
  { (int)Font::Family::Helvetica,    D_WEB_FONT_FAMILY_HELVETICA },
  { (int)Font::Family::OpenDyslexic, D_WEB_FONT_FAMILY_DYSLEXIC  },
};

static const SettingOption kGapOptions[] = {
  { 0, D_SETTINGS_GAP_0 },
  { 1, D_SETTINGS_GAP_1 },
  { 2, D_SETTINGS_GAP_2 },
  { 3, D_SETTINGS_GAP_3 },
};

// The web card's option labels (D_WEB_ORIENT_*) are plain text — unlike the
// font/gap ones with their &mdash; entities — so the device list reuses them
// rather than minting near-duplicate keys.
static const SettingOption kOrientOptions[] = {
  { 1, D_WEB_ORIENT_PORTRAIT  },
  { 0, D_WEB_ORIENT_LANDSCAPE },
};

static int  getSize(void)     { return Font::currentBodySize(); }
static void setSize(int v)    { Font::setBodySize(v); }
static int  getFamily(void)   { return (int)Font::currentFamily(); }
static void setFamily(int v)  { Font::setFamily((Font::Family)v); }
static int  getLineGap(void)  { return Font::currentLineGap(); }
static void setLineGap(int v) { Font::setLineGap(v); }
static int  getHalfGaps(void) { return Font::halfParagraphGapsEnabled() ? 1 : 0; }
static void setHalfGaps(int v){ Font::setHalfParagraphGaps(v != 0); }
static int  getOrient(void)   { return Orientation::isPortrait() ? 1 : 0; }
static void setOrient(int v)  { Orientation::set(v != 0); }
static int  getBionic(void)   { return Font::bionicEnabled() ? 1 : 0; }
static void setBionic(int v)  { Font::setBionic(v != 0); }

static const SettingItem kItems[] = {
  { D_WEB_FONT_SIZE_LABEL,    kSizeOptions,   4, getSize,     setSize     },
  { D_WEB_FONT_FAMILY_LABEL,  kFamilyOptions, 2, getFamily,   setFamily   },
  { D_WEB_LINE_SPACING_LABEL, kGapOptions,    4, getLineGap,  setLineGap  },
  { D_WEB_ORIENT_LABEL,       kOrientOptions, 2, getOrient,   setOrient   },
  { D_WEB_PARA_GAP_LABEL,     nullptr,        0, getHalfGaps, setHalfGaps },
  { D_WEB_BIONIC_LABEL,       nullptr,        0, getBionic,   setBionic   },
};
static const int kItemCount = sizeof(kItems) / sizeof(kItems[0]);

// ----------------------------------------------------------------------------
//  Row list — rebuilt each draw from the item table + expansion state, read
//  by onButton until the next draw (same lifecycle as the library screen's
//  s_entries). A row is either an item's own line (option < 0) or one of an
//  expanded choice item's options.
// ----------------------------------------------------------------------------
struct SettingRow {
  int8_t item;
  int8_t option;  // < 0: the item's own row
};

// Upper bound: every item plus the largest option list fully expanded.
static SettingRow s_rows[kItemCount * 5];
static int        s_rowCount = 0;

static bool s_expanded[kItemCount] = {false};
static int  s_cursor = 0;

static bool isChoice(const SettingItem& it) { return it.options != nullptr; }

static void buildRows() {
  s_rowCount = 0;
  for (int i = 0; i < kItemCount; i++) {
    s_rows[s_rowCount++] = { (int8_t)i, (int8_t)-1 };
    if (isChoice(kItems[i]) && s_expanded[i]) {
      for (int o = 0; o < kItems[i].optionCount; o++) {
        s_rows[s_rowCount++] = { (int8_t)i, (int8_t)o };
      }
    }
  }
}

static const char* currentOptionLabel(const SettingItem& it) {
  int v = it.get();
  for (int o = 0; o < it.optionCount; o++) {
    if (it.options[o].value == v) return it.options[o].label;
  }
  return "";
}

static String rowLabel(const SettingRow& r) {
  const SettingItem& it = kItems[r.item];
  if (r.option >= 0) {
    const SettingOption& op = it.options[r.option];
    return String(it.get() == op.value ? "[x] " : "[ ] ") + op.label;
  }
  if (isChoice(it)) {
    return String(s_expanded[r.item] ? "- " : "+ ")
         + it.label + ": " + currentOptionLabel(it);
  }
  return String(it.get() ? "[x] " : "[ ] ") + it.label;
}

// ----------------------------------------------------------------------------
//  Screen lifecycle
// ----------------------------------------------------------------------------
void SettingsScreen::onEnter() {
  // Fresh visit: everything collapsed, cursor at the top. Unlike library
  // folder expansion there's nothing worth preserving across visits — the
  // lists are pickers, not places.
  for (int i = 0; i < kItemCount; i++) s_expanded[i] = false;
  s_cursor = 0;
  draw();
}

void SettingsScreen::draw() {
  prepareMenuFrame();
  Font::useBody();

  buildRows();
  if (s_cursor < 0) s_cursor = 0;
  if (s_cursor >= s_rowCount) s_cursor = max(0, s_rowCount - 1);

  int y = drawSectionHeader(D_SETTINGS_HEADER);

  drawScrollableList(y, s_rowCount, s_cursor,
    [&](int idx, int rowY, bool selected, int /*budget*/) {
      const SettingRow& r = s_rows[idx];
      drawMenuRow(rowY, rowLabel(r), selected,
                  r.option >= 0 ? SETTINGS_OPTION_INDENT : 0);
      return 1;
    });

  display.update();
}

void SettingsScreen::onButton(const ButtonEvent& e) {
  if (!e.any()) return;

  if (e.kind == ButtonEvent::Triple) {
    nextScreen = &g_libraryScreen;
    return;
  }

  if (e.kind == ButtonEvent::Short) {
    if (s_rowCount > 0) {
      s_cursor = (s_cursor + 1) % s_rowCount;
    }
    draw();
    return;
  }

  if (e.kind != ButtonEvent::Double) return;

  if (s_cursor < 0 || s_cursor >= s_rowCount) {
    draw();
    return;
  }

  const SettingRow sel = s_rows[s_cursor];  // copy — draw() rebuilds s_rows
  const SettingItem& it = kItems[sel.item];

  if (sel.option >= 0) {
    // Option picked: apply, collapse the list, park the cursor back on the
    // item's own row (its position shifts when the options disappear).
    it.set(it.options[sel.option].value);
    s_expanded[sel.item] = false;
    // Size/family/spacing changes alter this menu's own row metrics — and an
    // orientation change swaps the whole panel mapping — so force a full
    // refresh so the old layout doesn't ghost through.
    forceNextMenuFrameFull();
    buildRows();
    for (int i = 0; i < s_rowCount; i++) {
      if (s_rows[i].item == sel.item && s_rows[i].option < 0) {
        s_cursor = i;
        break;
      }
    }
    draw();
    return;
  }

  if (isChoice(it)) {
    s_expanded[sel.item] = !s_expanded[sel.item];
  } else {
    //toggle
    it.set(it.get() ? 0 : 1);
  }
  draw();
}
