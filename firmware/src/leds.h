// GeistAI - LED ring effects (APA102 on T-Embed, WS2812 on T-Embed CC1101).
#pragma once
#include <stdint.h>

#include "ui.h"

namespace leds {

void begin();
// Call every frame; level is the mic or speaker loudness 0..1.
void update(geist::Mode mode, float level, uint32_t nowMs);

}  // namespace leds
