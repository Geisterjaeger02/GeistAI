// GeistAI - ST7789 panel driver (LovyanGFX) + full-screen framebuffer.
#pragma once
#include <stdint.h>

namespace display {

// Powers the panel, allocates the framebuffer. Returns false if out of memory.
bool begin();

// RGB565 framebuffer, 320x170, native byte order.
uint16_t* framebuffer();

// Push the whole framebuffer to the panel.
void present();

void setBrightness(uint8_t level);

}  // namespace display
