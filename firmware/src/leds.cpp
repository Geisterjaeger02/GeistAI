#include "leds.h"

#include <Arduino.h>
#include <math.h>

#include "board.h"

#if LED_APA102
#include <APA102.h>
static APA102<PIN_LED_DATA, PIN_LED_CLK> strip;
#elif LED_WS2812
#include <Adafruit_NeoPixel.h>
static Adafruit_NeoPixel strip(LED_COUNT, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);
#endif

namespace leds {
namespace {

struct RGB {
  uint8_t r, g, b;
};
RGB px[LED_COUNT];
uint32_t lastShow = 0;

RGB scale(RGB c, float k) {
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  return RGB{(uint8_t)(c.r * k), (uint8_t)(c.g * k), (uint8_t)(c.b * k)};
}

void show() {
#if LED_APA102
  rgb_color colors[LED_COUNT];
  for (int i = 0; i < LED_COUNT; i++) colors[i] = rgb_color(px[i].r, px[i].g, px[i].b);
  strip.write(colors, LED_COUNT, 6);  // global brightness 0..31
#elif LED_WS2812
  for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, px[i].r, px[i].g, px[i].b);
  strip.show();
#endif
}

const RGB C_CYAN{0, 230, 255};
const RGB C_WHITE{200, 255, 255};
const RGB C_RED{255, 0, 50};

}  // namespace

void begin() {
#if LED_WS2812
  strip.begin();
  strip.setBrightness(60);
#endif
  for (auto& p : px) p = RGB{0, 0, 0};
  show();
}

void update(geist::Mode mode, float level, uint32_t now) {
  if (now - lastShow < 30) return;  // ~33 fps is plenty
  lastShow = now;
  float t = now / 1000.f;

  switch (mode) {
    case geist::Mode::Boot: {
      // a comet that circles the ring
      float head = fmodf(t * 1.6f, 1.f) * LED_COUNT;
      for (int i = 0; i < LED_COUNT; i++) {
        float d = fmodf(head - i + LED_COUNT, (float)LED_COUNT);
        px[i] = scale(C_CYAN, 1.f - d / 3.f);
      }
      break;
    }
    case geist::Mode::Idle:
    case geist::Mode::Settings: {
      float b = 0.06f + 0.06f * (sinf(t * 1.4f) * 0.5f + 0.5f);
      for (auto& p : px) p = scale(C_CYAN, b);
      break;
    }
    case geist::Mode::Listening: {
      float lit = 0.15f + level * 1.4f;
      for (int i = 0; i < LED_COUNT; i++) px[i] = scale(i % 2 ? C_CYAN : C_WHITE, lit);
      break;
    }
    case geist::Mode::Thinking: {
      float head = fmodf(t * 2.4f, 1.f) * LED_COUNT;
      for (int i = 0; i < LED_COUNT; i++) {
        float d = fmodf(head - i + LED_COUNT, (float)LED_COUNT);
        px[i] = scale(C_CYAN, 0.9f - d / 2.5f);
      }
      break;
    }
    case geist::Mode::Speaking: {
      float b = 0.15f + level * 1.2f;
      for (auto& p : px) p = scale(C_CYAN, b);
      break;
    }
    case geist::Mode::Error: {
      float b = (fmodf(t, 1.f) < 0.5f) ? 0.5f : 0.05f;
      for (auto& p : px) p = scale(C_RED, b);
      break;
    }
  }
  show();
}

}  // namespace leds
