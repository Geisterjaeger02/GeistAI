// GeistAI games - small drawing helpers shared by several games.
#pragma once
#include "game.h"

namespace games {

// One random generator for all games (seeded at start-up).
Rng& rng();

// The GeistAI ghost: round head, wavy hem, two eyes. `blink` 0..1 closes the eyes.
void drawGhost(Canvas& c, float cx, float cy, float r, uint16_t body, uint16_t eyes,
               float blink);
void drawX(Canvas& c, float cx, float cy, float r, uint16_t col);
void drawO(Canvas& c, float cx, float cy, float r, uint16_t col);
// A line made of small dots (anti-aliased enough for thin shapes).
void drawLine(Canvas& c, float x0, float y0, float x1, float y1, float width, uint16_t col);
// Integer to text in a static buffer.
const char* num(int v);

}  // namespace games
