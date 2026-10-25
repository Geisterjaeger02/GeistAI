#include "input.h"

#include <Arduino.h>
#include <RotaryEncoder.h>

#include "board.h"
#include "config.h"

// A small 1 kHz scanning task reads the encoder and buttons. This avoids
// calling flash-resident code from interrupts (which crashes while NVS writes
// to flash) and never misses a press while the UI is busy drawing.

namespace input {
namespace {

RotaryEncoder* encoder = nullptr;
QueueHandle_t events = nullptr;
volatile bool btnDown = false;

void scanTask(void*) {
  bool stable = digitalRead(PIN_ENC_BTN) == LOW;
  bool last = stable;
  uint32_t changedAt = millis();
  uint32_t downAt = 0;
#if PIN_BTN_BACK >= 0
  bool backLast = false;
#endif
  long lastPos = 0;

  for (;;) {
    encoder->tick();
    uint32_t now = millis();

    long pos = encoder->getPosition();
    if (pos != lastPos) {
      Event ev;
      ev.type = EventType::Rotate;
      ev.delta = (int)(pos - lastPos);
#if ENCODER_REVERSE
      ev.delta = -ev.delta;
#endif
      ev.atMs = now;
      lastPos = pos;
      xQueueSend(events, &ev, 0);
    }

    bool raw = digitalRead(PIN_ENC_BTN) == LOW;
    if (raw != last) {
      last = raw;
      changedAt = now;
    } else if (raw != stable && now - changedAt >= 20) {  // 20 ms debounce
      stable = raw;
      btnDown = stable;
      Event ev;
      ev.atMs = now;
      if (stable) {
        downAt = now;
        ev.type = EventType::Down;
      } else {
        ev.type = EventType::Up;
        ev.heldMs = now - downAt;
      }
      xQueueSend(events, &ev, 0);
    }

#if PIN_BTN_BACK >= 0
    bool back = digitalRead(PIN_BTN_BACK) == LOW;
    if (back && !backLast) {
      Event ev;
      ev.type = EventType::Back;
      ev.atMs = now;
      xQueueSend(events, &ev, 0);
    }
    backLast = back;
#endif

    vTaskDelay(1);
  }
}

}  // namespace

void begin() {
  pinMode(PIN_ENC_BTN, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
#if PIN_BTN_BACK >= 0
  pinMode(PIN_BTN_BACK, INPUT_PULLUP);
#endif
  encoder = new RotaryEncoder(PIN_ENC_A, PIN_ENC_B, RotaryEncoder::LatchMode::TWO03);
  events = xQueueCreate(32, sizeof(Event));
  xTaskCreatePinnedToCore(scanTask, "input", 3072, nullptr, 3, nullptr, 1);
}

bool buttonIsDown() { return btnDown; }

bool poll(Event& ev) {
  if (!events) return false;
  return xQueueReceive(events, &ev, 0) == pdTRUE;
}

}  // namespace input
