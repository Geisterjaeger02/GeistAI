#include "display.h"

#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>

#include "board.h"
#include "config.h"
#include "ui.h"

namespace {

class Panel : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

 public:
  Panel() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = PIN_LCD_MISO;
      cfg.pin_dc = PIN_LCD_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = PIN_LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 170;
      cfg.panel_height = 320;
      cfg.offset_x = 35;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = LCD_BUS_SHARED ? true : false;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }
};

Panel lcd;
uint16_t* fb = nullptr;

#if LCD_VENDOR_INIT
// LilyGO's tuned ST7789V parameters for the original T-Embed panel
// (porch, gate, VCOM, power and gamma) - from their tft example.
struct Cmd {
  uint8_t cmd;
  uint8_t data[14];
  uint8_t len;  // bit7 = wait 120 ms afterwards
};
const Cmd kVendorInit[] = {
    {0x11, {0}, 0 | 0x80},
    {0x3A, {0x05}, 1},
    {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x75}, 1},
    {0xBB, {0x28}, 1},
    {0xC0, {0x2C}, 1},
    {0xC2, {0x01}, 1},
    {0xC3, {0x1F}, 1},
    {0xC6, {0x13}, 1},
    {0xD0, {0xA7}, 1},
    {0xD0, {0xA4, 0xA1}, 2},
    {0xD6, {0xA1}, 1},
    {0xE0, {0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B, 0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32}, 14},
    {0xE1, {0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B, 0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37}, 14},
};

void applyVendorInit() {
  lcd.startWrite();
  for (const Cmd& c : kVendorInit) {
    lcd.writeCommand(c.cmd);
    for (int i = 0; i < (c.len & 0x7F); i++) lcd.writeData(c.data[i]);
    if (c.len & 0x80) delay(120);
  }
  lcd.endWrite();
}
#endif

}  // namespace

namespace display {

bool begin() {
#if LCD_BUS_SHARED
  // Keep the other devices on the shared SPI bus deselected.
  for (int pin : {PIN_SD_CS, PIN_RADIO_CS, PIN_NRF24_CS}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  delay(20);
#endif

  lcd.init();
#if LCD_VENDOR_INIT
  applyVendorInit();
#endif
  lcd.setRotation(LCD_ROTATION);
  lcd.fillScreen(0x0000);
  lcd.setBrightness(LCD_BRIGHTNESS);

  const size_t bytes = geist::SCREEN_W * geist::SCREEN_H * sizeof(uint16_t);
  // Internal RAM is faster to draw into, but leave plenty for Wi-Fi.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > bytes + 100000)
    fb = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!fb) fb = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return fb != nullptr;
}

uint16_t* framebuffer() { return fb; }

void present() {
  if (!fb) return;
  lcd.pushImage(0, 0, geist::SCREEN_W, geist::SCREEN_H, (const lgfx::rgb565_t*)fb);
}

void setBrightness(uint8_t level) { lcd.setBrightness(level); }

void status(const char* msg) {
  Serial.printf("[boot] %s\n", msg);
  lcd.fillRect(0, 140, geist::SCREEN_W, 30, 0x0000);
  lcd.setTextColor(0xFFFF, 0x0000);
  lcd.setTextSize(1);
  lcd.drawString("GeistAI starting...", 6, 142);
  lcd.drawString(msg, 6, 156);
}

}  // namespace display
