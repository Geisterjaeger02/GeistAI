// GeistAI - rotary encoder + buttons, interrupt driven so no press is missed
// while a frame is being drawn.
#pragma once
#include <stdint.h>

namespace input {

enum class EventType : uint8_t {
  None,
  Down, Up,            // main button (knob / BtnA): hold = talk, tap = keyboard / select
  Rotate,              // knob / arrow keys / BtnB
  BackDown, BackUp,    // back / Tab / PWR: short = settings, long = power off
  // physical keyboards (Cardputer)
  Char,                // `text` holds one UTF-8 character
  Enter, Backspace, Escape,
  SpaceDown, SpaceUp,  // space: types in the keyboard, otherwise hold-to-talk
};

struct Event {
  EventType type = EventType::None;
  int delta = 0;         // Rotate: detents (+ = clockwise)
  uint32_t heldMs = 0;   // Up / BackUp: how long the button was held
  uint32_t atMs = 0;
  char text[8] = {0};
};

void begin();

// Returns false when there are no more events.
bool poll(Event& ev);

bool buttonIsDown();
bool backIsDown();

}  // namespace input
