#include "src/ui/screen_settings.h"
#include "src/state.h"
#include "src/ui/toast.h"

namespace ScreenSettings {
    static bool s_screenFlipped = false;
    static constexpr const char *kKeyScreenFlipped = "cfg_scr_flip";

    void loadSettings() {
        s_screenFlipped = prefs.getBool(kKeyScreenFlipped, false);
    }

    void toggleScreenRotation() {
        s_screenFlipped = !s_screenFlipped;
        Toast::show("Flipped");
        prefs.putBool(kKeyScreenFlipped, s_screenFlipped);
    }
    
    bool isScreenFlipped() {
        return s_screenFlipped;
    }

    void setScreenRotation(bool inverted) {
        s_screenFlipped = inverted;
        prefs.putBool(kKeyScreenFlipped, s_screenFlipped);
    }
}