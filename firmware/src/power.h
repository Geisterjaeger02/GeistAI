// GeistAI - switching off and on with the back button (T-Embed CC1101).
#pragma once

namespace power {

// Call first thing in setup(): after a wake-up from "off" it checks that the
// button is really being held (not just bumped in a pocket) and otherwise
// goes straight back to sleep.
void handleWakeUp();

// Turns everything off. Never returns. The caller shows the goodbye screen first.
void switchOff();

}  // namespace power
