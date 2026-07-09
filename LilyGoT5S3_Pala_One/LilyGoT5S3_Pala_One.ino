// ============================================================================
//  Pala One — firmware entry point. LilyGo T5 4.7" S3 (ED047TC1) port of the
//  3.0 architecture.
//
//  The real firmware lives under src/ (hal/, pure/, storage/, ui/, web/).
//  This file exists because Arduino IDE requires a .ino with the same name
//  as the sketch folder.
//
//  Build options:
//
//    - PlatformIO: no platformio.ini ships in this checkout yet — this port
//      was done source-level only, without a build/flash pass. Bringing up
//      a platformio.ini env for this board still needs doing; see lib_deps
//      below for what it needs to pull in.
//
//    - Arduino IDE 2:
//        1. Install the ESP32 board package, select an ESP32-S3 board
//           (8MB+ PSRAM enabled — the framebuffers live in PSRAM).
//        2. Install libraries: LilyGo-EPD47 (epdiy) — IMPORTANT: this
//           project's epdiy needs a *patched* checkout (see hal/
//           epd_backend.h's top comment) to coexist with Adafruit_GFX; Adafruit GFX;
//           U8g2_for_Adafruit_GFX; JPEGDEC library (bitbank2).
//        3. Compile and upload.
//
//  EPUB import is supported as part of upload (storage/
//  epub_import.h).
// ============================================================================

// ── Language selection: uncomment exactly one (Arduino IDE) ─────────────────
//   PlatformIO users pick the env in platformio.ini (-en / -es leaf envs)
//   and can leave these defines commented out. Default if nothing is set:
//   English (with a #pragma message warning from src/config.h).
#define LANG_EN
// #define LANG_ES_LA
// ────────────────────────────────────────────────────────────────────────────

// ── Web UI default theme: uncomment exactly one ─────────────────────────────
//   The web UI has a light and a dark palette and a per-page toggle button.
//   Once a visitor picks one the choice is remembered in their browser's
//   localStorage and overrides whatever's set here — this define only picks
//   the *first-visit* default. Default if nothing is set: light.
//   PlatformIO users can also pass -D WEB_THEME_DARK in build_flags.
#define WEB_THEME_LIGHT
// #define WEB_THEME_DARK
// ────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>          // rtc_gpio_deinit — release BTN from the RTC mux
#include <soc/sens_struct.h>        // SENS — RTC-IO clock gate rtc_gpio_deinit turns off
#include <soc/rtc_io_periph.h>      // rtc_io_desc/rtc_io_num_map — b12 diagnostics
#include <soc/gpio_periph.h>        // GPIO_PIN_MUX_REG — b12 diagnostics
#include <soc/gpio_struct.h>        // GPIO.pin[] interrupt config — b12 diagnostics

#include "src/config.h"
#include "src/state.h"
#include "src/hal/battery.h"
#include "src/hal/display.h"
#include "src/hal/input.h"
#include "src/hal/orientation.h"
#include "src/hal/wifi_provisioning.h"
#include "src/pure/hashing.h"
#include "src/storage/app_catalog.h"
#include "src/storage/fs_util.h"
#include "src/storage/library.h"
#include "src/storage/list_items.h"
#include "src/storage/page_cache.h"
#include "src/storage/statistics.h"
#include "src/ui/font.h"
#include "src/ui/pala_api_impl.h"
#include "src/ui/reader.h"
#include "src/ui/reader_menu.h"
#include "src/ui/reader_actions.h"  // Gestures::loadSettings
#include "src/ui/screen.h"
#include "src/ui/widgets.h"  // drawCenter
#include "src/ui/screens/about_screen.h"
#include "src/ui/screens/apps_screen.h"
#include "src/ui/screens/bookmarks/book_select_screen.h"
#include "src/ui/screens/bookmarks/bookmark_list_screen.h"
#include "src/ui/screens/bookmarks/preview_screen.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/screens/list_screen.h"
#include "src/ui/screens/reader_screen.h"
#include "src/ui/screens/settings_screen.h"
#include "src/ui/screens/statistics_screen.h"
#include "src/ui/screens/update_screen.h"
#include "src/ui/screens/upload_screen.h"
#include "src/ui/header_title.h"
#include "src/ui/lock.h"
#include "src/ui/screensavers.h"
#include "src/ui/sleep.h"
#include "src/ui/statusbar.h"
#include "src/ui/text.h"
#include "src/ui/toast.h"
#include "src/web/web.h"
#include "src/ui/screen_settings.h"

// ============================================================================
//  Screen instances + current-screen pointer
// ============================================================================
AboutScreen                g_aboutScreen;
AppsScreen                 g_appsScreen;
BookmarkBookSelectScreen   g_bmBookSelectScreen;
BookmarkListScreen         g_bmListScreen;
BookmarkPreviewScreen      g_bmPreviewScreen;
LibraryScreen              g_libraryScreen;
ListScreen                 g_listScreen;
ReaderScreen               g_readerScreen;
SettingsScreen             g_settingsScreen;
StatisticsScreen           g_statsScreen;
UpdateScreen               g_updateScreen;
UploadScreen               g_uploadScreen;

Screen* g_currentScreen = &g_libraryScreen;

#if HAS_BATTERY
static bool batteryIndicatorVisible() {
  return g_currentScreen == &g_aboutScreen
      || g_currentScreen == &g_appsScreen
      || g_currentScreen == &g_bmBookSelectScreen
      || g_currentScreen == &g_bmListScreen
      || g_currentScreen == &g_libraryScreen
      || g_currentScreen == &g_listScreen
      || g_currentScreen == &g_settingsScreen
      || g_currentScreen == &g_statsScreen
      || g_currentScreen == &g_updateScreen
      || g_currentScreen == &g_uploadScreen
      || (g_currentScreen == &g_readerScreen && ReaderMenu::isActive());
}
#endif

// ============================================================================
//  Setup
// ============================================================================
// cppcheck-suppress unusedFunction
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("[boot] wake cause: %d\n", esp_sleep_get_wakeup_cause());
  setCpuFrequencyMhz(240); // full speed for init; lowered to 80 MHz at end of setup

  // A prior deep sleep routes BTN to the RTC-IO mux (ui/sleep.cpp's
  // rtc_gpio_init). A normal deep-sleep WAKE reboots through the ROM path that
  // releases it, but a USB reflash resets the CPU without clearing the RTC
  // power domain — so the pad can come up still stranded in the RTC mux.
  // Deinit first to return the pad to the digital IO mux. Side effect: on the
  // S3, rtc_gpio_deinit()'s DIGITAL path clears SENS.sar_peri_clk_gate_conf
  // .iomux_clk_en, the SHARED clock gate for the whole RTC-IO block (esp-idf
  // issue #12681), so re-open it before the RTC-domain writes below.
  //
  // Set the pull-up in the RTC DOMAIN, not just via pinMode. Do it
  // here too, while the clock gate is open so the writes land.
  rtc_gpio_deinit((gpio_num_t)BTN);
  SENS.sar_peri_clk_gate_conf.iomux_clk_en = 1;
  rtc_gpio_pullup_en((gpio_num_t)BTN);
  rtc_gpio_pulldown_dis((gpio_num_t)BTN);
  pinMode(BTN, INPUT_PULLUP);

  // display.begin() (epd_init()) MUST come before attachInterrupt(): epdiy
  // reinstalls the GPIO ISR service internally, wiping any handler attached
  // earlier. Get this order wrong and the button silently stops generating
  // edges. See hal/epd_backend.cpp's begin().
  display.begin();
  u8g2.begin(gfx);

  // display.begin() re-clears the RTC-IO clock gate,epdiy reconfigures pads
  // in the RTC-capable 0-21 range and any RTC-mux release re-trips issue #12681.
  // The pull-up config latched above survives, but light-sleep ext0 wake wants the
  // block clock running, so re-assert the gate now that epdiy is done.
  SENS.sar_peri_clk_gate_conf.iomux_clk_en = 1;

  attachInterrupt(digitalPinToInterrupt(BTN), btnISR, CHANGE);

  // Button held through ext0 wake: its down-edge predates the ISR, so seed
  // the press state manually. Pass 0 (not millis()) to credit the full boot
  // time; millis() ≈ 200 here (after delay(200)) would shorten the hold and
  // misclassify a Long press as Short.
  //
  // ONLY on an ext0 wake. On a plain reset or power-on nobody is deliberately
  // holding the button through a wake — but a pad whose pull-up hasn't taken
  // effect DOES read LOW here, and unconditionally seeding turned that into a
  // phantom hold: by the first loop poll the "press" is already older than
  // VERY_LONG_MS (pressStart=0), a VeryLong gesture fires on a button nobody
  // touched, and the device locks/sleeps seconds after every RST.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 &&
      digitalRead(BTN) == LOW) {
    g_btns.seedPressOnWake(0);
  }

#if HAS_BATTERY
  adcSetupOnce();
#if BAT_ADC_CTRL >= 0
  pinMode(BAT_ADC_CTRL, INPUT);
#endif
  updateBatteryCached(true);
#endif

  // Load Sleep, Lock and Orientation settings early — before display.clear()
  // — so the sleep/lock flags are available to gate the full-refresh boot
  // clear below, and the very first frame renders in the user's chosen
  // orientation rather than the portrait default.
  prefs.begin("ereader", false);
  Sleep::loadSettings();
  Lock::loadSettings();
  Orientation::loadSettings();

  // Skip the full-refresh boot clear when waking from deep sleep AND either:
  //   (a) the device is locked — the screensaver (with its lock badge) is
  //       already on the e-ink; clearing to white and then drawing nothing
  //       leaves a blank screen until the idle timeout fires, OR
  //   (b) no-screensaver mode is on and we were reading — the last reader
  //       page sits cleanly on the panel; a clear would briefly flash white
  //       before the page redraws.
  // On a fresh boot (not ext0 wake) always clear, regardless of lock state.
  bool wokeFromSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
  bool wereReading   = (prefs.getString("wake_path", "").length() > 0);
  display.fastmodeOff();
  bool skipClear = wokeFromSleep &&
                   (Lock::isLocked() ||
                    (Sleep::noScreensaver() && wereReading));
  if (!skipClear) {
    display.clear();
  }

  if (!fsBegin()) {
    drawCenter(D_BOOT_STORAGE_ERROR, D_BOOT_TRY_FACTORY_RESET);
    return;
  }
  ensureBooksDir();

  {
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(AP_SSID, sizeof(AP_SSID), "PALA-%06llX", chipId & 0xFFFFFFULL);
  }

  Font::loadSettings();
  Screensavers::loadSettings();
  Statusbar::loadSettings();
  Gestures::loadSettings();
  HeaderTitle::loadSettings();
  ScreenSettings::loadSettings();

  // Sleep::loadSettings() and Lock::loadSettings() already ran earlier in
  // setup() so both flags were available for the boot-clear gate above —
  // don't reload them here.
  loadBooks();
  loadListItems();
  loadApps();
  initPalaAPI();
  registerWebRoutes();
  markUserActivity();

  // Reload lifetime counters from NVS into RTC RAM (no-op on warm wake).
  // Streak state bootstraps lazily on the first page turn.
  Statistics::loadOnBoot();

  // When locked at boot, we skip the screen draw and leave the sleep image
  // (with its lock indicator) on the e-ink. Otherwise a short-press wake
  // would render the reader/library over the screensaver while the loop is
  // still swallowing input — the device looks alive but ignores presses
  // until an unlock gesture. The unlock branch in loop() calls
  // g_currentScreen->draw() to paint the real screen once unlocked.
  if (tryRestoreReadingSession()) {
    g_currentScreen = &g_readerScreen;
    if (Lock::isLocked()) {
      // Keep the wake-press edges so a click-then-hold can wake AND unlock
      // in one motion. resetInputFrontend would otherwise drain them and
      // force the user to repeat the unlock gesture.
      markUserActivity();
    } else {
      renderCurrentPage();      // ~300ms draw — wake-press releases during this
      resetInputFrontend();     // discard the wake-press only
    }
  } else {
    g_currentScreen = &g_libraryScreen;
    if (Lock::isLocked()) {
      markUserActivity();
    } else {
      g_libraryScreen.onEnter();
      resetInputFrontend();
    }
  }

  // Drop to 80 MHz for normal operation — saves significant power.
  // Upload mode will raise it back to 240 MHz temporarily.
  setCpuFrequencyMhz(80);

  // Browser-side Wi-Fi provisioning over USB-CDC (Improv Serial under the
  // hood). Once registered, WifiProvisioning::loop() listens whenever a host
  // has the USB-CDC port open. No host = no listening = no battery cost. See
  // src/hal/wifi_provisioning.h for the contract.
  WifiProvisioning::begin();
}


// ============================================================================
//  Main loop
// ============================================================================
void loop() {
  g_btns.poll();
  maybeRecoverFromIsrOverflow();

#if HAS_BATTERY
  updateBatteryCached();
#endif

  ButtonEvent ev = ButtonEvent::fromButtonState(g_btns);

  // Lifetime button-press counter. peekPressCount is monotonic-up except
  // when the apps API consumes-and-resets — guard by clamping lastSeen to
  // the current value if it ran backwards. Runs before the lock check so
  // physical presses count toward lifetime stats even when the UI is
  // swallowing them.
  {
    static uint32_t lastSeenPressCount = 0;
    uint32_t pc = g_btns.peekPressCount();
    if (pc < lastSeenPressCount) lastSeenPressCount = pc;
    if (pc != lastSeenPressCount) {
      Statistics::bumpButtons(pc - lastSeenPressCount);
      lastSeenPressCount = pc;
    }
  }

  // Locked: swallow all input except unlock gestures (Long/VeryLong/ClickHold).
  // Does NOT call markUserActivity for non-unlock events so the idle deadline
  // keeps ticking. cfg_locked persists in NVS so a re-sleep stays locked.
  //
  // Wake-press handling: the short press that woke the device re-appears
  // through the classifier in the first loop iteration. We absorb it silently
  // (s_lockedWakePressConsumed). A *subsequent* non-unlock press while still
  // locked means the user deliberately tapped again → re-enter deep sleep
  // immediately. A short 1500ms locked-idle timeout (independent of the user's
  // sleep setting) also returns to deep sleep so an accidental wake doesn't
  // leave the device on indefinitely.
  {
    static bool s_lockedWakePressConsumed = false;  // reset each deep-sleep wake

    if (Lock::isLocked()) {
      if (Lock::isUnlockGesture(ev)) {
        s_lockedWakePressConsumed = false;
        Lock::disengage();
        markUserActivity();
        Toast::show(D_TOAST_UNLOCKED);
        // Full refresh to clear screensaver ghosting on unlock.
        // forceNextRenderFull() overrides the reader's per-page fast-mode
        // decision so renderCurrentPage() uses fastmodeOff regardless of
        // pageTurnsSinceFull. forceNextMenuFrameFull() does the same for menu
        // screens whose prepareMenuFrame() would otherwise override fastmodeOff.
        forceNextRenderFull();
        forceNextMenuFrameFull();
        display.fastmodeOff();
        g_currentScreen->draw();
        return;
      }
      if (ev.any()) {
        if (!s_lockedWakePressConsumed) {
          s_lockedWakePressConsumed = true;   // absorb wake press
        } else {
          Sleep::enter();                     // second tap → back to screensaver
          return;
        }
      }
      // Short locked-idle: re-sleep after 1500ms with no input.
      // Don't sleep while the button is held — a Long-press unlock gesture
      // fires on release, so sleeping mid-hold would swallow the gesture.
      if (ENABLE_DEEP_SLEEP && g_currentScreen->allowSleep()
          && userIdleMs() > 1500 && !g_btns.isPressed()) {
        Sleep::enter();
        return;
      }
      return;
    }
    s_lockedWakePressConsumed = false;  // clear when unlocked so state is fresh on next lock
  }

  if (ev.any()) markUserActivity();

#if HAS_BATTERY
  if (batteryChargingChanged() && batteryIndicatorVisible()) {
    if (ReaderMenu::isActive()) ReaderMenu::draw();
    else g_currentScreen->draw();
  }
#endif

  if (ENABLE_DEEP_SLEEP && g_currentScreen->allowSleep() && !WifiProvisioning::isActive()) {
    if (userIdleMs() > Sleep::idleTimeoutMs()) {
      Sleep::enter();
      return;
    }
  }

  WifiProvisioning::loop();   // no-op unless a USB host is on the bus

  g_currentScreen->onButton(ev);
  g_currentScreen->onIdleTick();

  // Toast just expired? Repaint so its pixels actually disappear.
  if (Toast::clearIfExpired()) g_currentScreen->draw();

  if (g_currentScreen->nextScreen) {
    g_currentScreen = g_currentScreen->nextScreen;
    g_currentScreen->nextScreen = nullptr;
    g_currentScreen->onEnter();
  }

  // Light-sleep idle gating. The single biggest battery saver while reading:
  // between page turns the loop has nothing to do, so we drop the CPU until
  // either the button is pressed or a short timer fires for housekeeping.
  // Skipped on screens that need the CPU active (UploadScreen → SoftAP), and
  // mid-click-sequence — the classifier's trailing-silence wait runs against
  // millis(), and sleeping through it would add up to one tick interval of
  // latency per emit. Cost of staying awake during a click sequence is at
  // most ~550ms (MAX_CLICK_SEQUENCE_MS); the long quiet gaps between page
  // turns are where the battery savings actually come from.
  //
  // The `buttonQueueNonEmpty()` check closes a race: the ISR can queue a
  // release edge after this iter's `poll()` ran but before we reach this
  // gate — `clickCount_` is still 0 at that instant, but the edge is
  // sitting in the ring buffer waiting to be drained. Without the check we
  // sleep through it; ext0 (level-low) doesn't fire on a release, so we'd
  // only re-process the edge on the next timer wake (~150ms later in the
  // worst case under the bound below).
  if (g_currentScreen->allowSleep()
      && !g_btns.hasPendingClicks()
      && !buttonQueueNonEmpty()
      && !WifiProvisioning::isActive()) {
    Sleep::idleLightSleep(Toast::isActive());
  }
}
