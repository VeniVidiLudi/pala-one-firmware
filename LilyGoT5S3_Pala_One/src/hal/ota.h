#ifndef PALA_HAL_OTA_H
#define PALA_HAL_OTA_H

#include <Arduino.h>

struct OtaCheckResult {
  bool   updateAvailable = false;
  String remoteVersion;
};

struct OtaDownloadResult {
  bool success           = false;
  bool partitionTooSmall = false;
};

// Callback fired during OTA::download() with progress 0–100.
// Only called when Content-Length is known. Callers typically redraw at 10%
// granularity to avoid hammering the e-ink display.
using OtaProgressFn = void (*)(int percent);

class OTA {
public:
  // HTTPS HEAD probe to the firmware distribution root.
  // Blocks for up to 5 s. Call only when Wi-Fi STA is connected.
  static bool isUpdateServerReachable();

  // Fetches the manifest for the given channel and compares its version
  // against the running FW_VERSION. Board and language tokens are resolved
  // from build-time defines. Blocks for up to 5 s.
  static OtaCheckResult checkAvailable(const String& channel);

  // Downloads the firmware binary for the given channel into the idle OTA
  // partition, validates it, and sets it as the next boot partition.
  // Blocks until complete (typically 5–30 s depending on network speed).
  // Does NOT reboot — caller decides when to call esp_restart().
  static OtaDownloadResult download(const String& channel,
                                    OtaProgressFn progressCb = nullptr);
};

#endif  // PALA_HAL_OTA_H
