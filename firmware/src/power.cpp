// LilyGO T-Embed implementation (see hal_m5.cpp for the M5Stack boards).
#include "board.h"
#if GEIST_HAL_LILYGO

#include "power.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "board.h"
#include "config.h"
#include "display.h"
#include "leds.h"

namespace power {
namespace {

#if PIN_BTN_BACK >= 0
constexpr gpio_num_t kWakePin = (gpio_num_t)PIN_BTN_BACK;

void waitForRelease() {
  pinMode(PIN_BTN_BACK, INPUT_PULLUP);
  uint32_t releasedSince = 0;
  for (;;) {
    if (digitalRead(PIN_BTN_BACK) == HIGH) {
      if (!releasedSince) releasedSince = millis();
      if (millis() - releasedSince > 80) return;
    } else {
      releasedSince = 0;
    }
    delay(5);
  }
}

#if POWER_OFF_SHIP_MODE
// BQ25896 charger: REG09 bit5 BATFET_DIS cuts the battery ("ship mode").
// Only works while running from battery; on USB the board stays powered.
void cutBattery() {
  constexpr uint8_t kAddr = 0x6B, kReg = 0x09;
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(kAddr);
  Wire.write(kReg);
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom(kAddr, (uint8_t)1) != 1) return;
  uint8_t v = Wire.read();
  v &= ~(1 << 3);  // BATFET_DLY = 0 -> immediately
  v |= (1 << 5);   // BATFET_DIS = 1
  Wire.beginTransmission(kAddr);
  Wire.write(kReg);
  Wire.write(v);
  Wire.endTransmission();
  delay(200);
}
#endif

[[noreturn]] void deepSleep() {
  // Keep peripherals (display, LEDs, amp, radio modules) unpowered while asleep.
  digitalWrite(PIN_POWER_ON, LOW);
  gpio_hold_en((gpio_num_t)PIN_POWER_ON);
  gpio_deep_sleep_hold_en();

  rtc_gpio_pullup_en(kWakePin);
  rtc_gpio_pulldown_dis(kWakePin);
  esp_sleep_enable_ext0_wakeup(kWakePin, 0);  // wake when the button pulls low
  Serial.println("[power] off (deep sleep) - hold the back button to wake");
  Serial.flush();
  esp_deep_sleep_start();
  for (;;) {
  }
}
#endif

}  // namespace

void handleWakeUp() {
  // Undo the pin hold from switchOff() so the board can power up again.
  gpio_hold_dis((gpio_num_t)PIN_POWER_ON);
#if PIN_BTN_BACK >= 0
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) return;
  rtc_gpio_deinit(kWakePin);
  pinMode(PIN_BTN_BACK, INPUT_PULLUP);
  uint32_t t0 = millis();
  while (digitalRead(PIN_BTN_BACK) == LOW) {
    if (millis() - t0 >= POWER_WAKE_HOLD_MS) return;  // held long enough: boot
    delay(10);
  }
  deepSleep();  // only bumped - go back to sleep
#endif
}

void switchOff() {
  leds::off();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  display::sleep();
#if PIN_BTN_BACK >= 0
  waitForRelease();  // otherwise the held button would wake us right away
#if POWER_OFF_SHIP_MODE
  cutBattery();      // returns only if USB keeps the board powered
#endif
  deepSleep();
#else
  for (;;) delay(1000);
#endif
}

int batteryPercent() {
#if PIN_BAT_ADC >= 0
  uint32_t mv = analogReadMilliVolts(PIN_BAT_ADC) * 2;  // 1:2 divider
  if (mv > 4350 || mv < 2500) return -1;               // USB only / no battery
  int pct = (int)((mv - 3300) * 100 / (4150 - 3300));
  return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
#else
  return -1;
#endif
}

}  // namespace power

#endif  // GEIST_HAL_LILYGO
