#ifndef PALA_UI_SCREENS_UPDATE_SCREEN_H
#define PALA_UI_SCREENS_UPDATE_SCREEN_H

#include <Arduino.h>
#include "src/hal/wifi.h"
#include "src/ui/screen.h"

class UpdateScreen : public Screen {
public:
  void onEnter() override;
  void onButton(const ButtonEvent& e) override;
  void draw() override;
  void onIdleTick() override;

  // Block sleep only while the Wi-Fi session is active; once wifiEnd() is
  // called (after flash or on exit) the device may sleep normally.
  bool allowSleep() const override { return !wifiStarted_; }

  // Called from the OTA progress callback during the blocking download.
  // Redraws at every 10 % to limit e-ink refresh overhead.
  void setProgress(int pct);

private:
  enum class Phase {
    NoCreds,          // no stored Wi-Fi credentials — prompt to use web installer
    Connecting,       // STA association in flight
    ConnFailed,       // association error
    Idle,             // connected — ready to check
    Checking,         // probe + manifest fetch running (drawn then blocks)
    ServerFail,       // probe or manifest fetch failed
    UpToDate,         // versions match
    UpdateAvailable,  // newer version found — shows Install button
    Downloading,      // binary streaming to OTA partition
    DownloadFailed,   // write/validation error — shows Install button for retry
    RebootPrompt      // binary flashed — waiting for reboot confirmation
  };

  Phase       phase_         = Phase::Connecting;
  uint32_t    staStartMs_    = 0;
  WifiSession net_;
  bool        stableChan_    = true;
  bool        wifiStarted_   = false;  // true only between wifiStaBegin() and wifiEnd()
  int         focusItem_     = 0;  // 0=Stable  1=Dev  2=Check  3=Install
  String      remoteVersion_;
  int         progress_      = 0;

  void loadChannel();
  void saveChannel();
  void exitToLibrary();
};

extern UpdateScreen g_updateScreen;

#endif  // PALA_UI_SCREENS_UPDATE_SCREEN_H
