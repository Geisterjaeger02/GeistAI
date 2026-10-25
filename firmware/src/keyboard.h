// GeistAI - on-screen keyboard layout (portable, shared by UI and input logic).
// Turn the knob to move the highlight, press to type the highlighted key.
#pragma once
#include <stdint.h>

namespace geist {
namespace kb {

enum Action : uint8_t { CHAR, SHIFT, PAGE, SPACE, BACKSPACE, SETTINGS, CLOSE, SEND };

struct Key {
  Action action;
  const char* lower;  // text typed (UTF-8) for CHAR keys
  const char* upper;  // with shift
  uint8_t halfUnits;  // width; every row is 22 half-units wide
};

enum Page : uint8_t { LETTERS = 0, NUMBERS, ACCENTS, PAGE_COUNT };

struct Rect {
  int x, y, w, h;
};

// All pages have the same number of keys, so the highlight stays put when
// switching pages.
int keyCount();
const Key& key(int page, int index);
Rect keyRect(int index);

// Label shown on the PAGE key for the given current page ("123", "áé", "abc").
const char* pageKeyLabel(int page);

// Removes the last UTF-8 character from s (in place).
void popUtf8(char* s);

constexpr int TEXT_MAX = 240;  // bytes

}  // namespace kb
}  // namespace geist
