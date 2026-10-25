// GeistAI - ST7789 panel driver (LovyanGFX) + full-screen framebuffer.
#pragma once
#include <stdint.h>

namespace display {

// Powers the panel, allocates the framebuffer. Returns false if out of memory.
bool begin();

// RGB565 framebuffer in landscape, native byte order.
uint16_t* framebuffer();
int width();
int height();

// Push the whole framebuffer to the panel.
void present();

void setBrightness(uint8_t level);

// Backlight off + panel sleep (used when switching off).
void sleep();

// Startup diagnostics: prints a line at the bottom of the screen (and to Serial)
// before the real UI takes over.
void status(const char* msg);

}  // namespace display
