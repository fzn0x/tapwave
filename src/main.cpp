// Tapwave firmware entry point. ARCHITECTURE.md explains how the pieces fit together.
#include <Arduino.h>

#include "app/app.hpp"

namespace {

tapwave::app::App app;

}  // namespace

// A new firmware image stays "pending verify" until Tapwave confirms it, 30 seconds after Wi-Fi
// comes up, instead of being confirmed by the Arduino core at boot. An image that crashes or never
// gets online is rolled back by the bootloader (ARCHITECTURE.md section 14).
extern "C" bool verifyRollbackLater() {
    return true;
}

void setup() {
    app.setup();
}

void loop() {
    app.loop();
}
