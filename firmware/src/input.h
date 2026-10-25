// GeistAI - rotary encoder + buttons, interrupt driven so no press is missed
// while a frame is being drawn.
#pragma once
#include <stdint.h>

namespace input {

enum class EventType : uint8_t { None, Down, Up, Rotate, Back };

struct Event {
  EventType type = EventType::None;
  int delta = 0;         // Rotate: detents (+ = clockwise)
  uint32_t heldMs = 0;   // Up: how long the button was held
  uint32_t atMs = 0;
};

void begin();

// Returns false when there are no more events.
bool poll(Event& ev);

bool buttonIsDown();

}  // namespace input
