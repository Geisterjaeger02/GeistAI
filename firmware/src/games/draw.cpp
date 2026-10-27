#include "draw.h"

#include <math.h>
#include <stdio.h>

namespace games {

Rng& rng() {
  static Rng r;
  return r;
}

void drawLine(Canvas& c, float x0, float y0, float x1, float y1, float width, uint16_t col) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int steps = (int)(len / (width * 0.35f)) + 1;
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;
    c.fillCircle(x0 + dx * t, y0 + dy * t, width / 2, col);
  }
}

void drawX(Canvas& c, float cx, float cy, float r, uint16_t col) {
  float w = r * 0.42f;
  drawLine(c, cx - r, cy - r, cx + r, cy + r, w, col);
  drawLine(c, cx - r, cy + r, cx + r, cy - r, w, col);
}

void drawO(Canvas& c, float cx, float cy, float r, uint16_t col) { c.ring(cx, cy, r, r * 0.42f, col); }

void drawGhost(Canvas& c, float cx, float cy, float r, uint16_t body, uint16_t eyes, float blink) {
  c.fillCircle(cx, cy - r * 0.15f, r, body);
  c.fillRect((int)(cx - r), (int)(cy - r * 0.15f), (int)(2 * r) + 1, (int)(r * 0.95f), body);
  // wavy hem: three little bumps
  for (int i = 0; i < 3; i++)
    c.fillCircle(cx - r + r / 3 + i * (2 * r / 3), cy + r * 0.8f, r / 3, body);
  float ey = cy - r * 0.25f, ex = r * 0.38f, er = r * 0.2f;
  float h = 1.f - blink;
  if (h > 0.15f) {
    c.fillCircle(cx - ex, ey, er, eyes);
    c.fillCircle(cx + ex, ey, er, eyes);
  } else {
    c.fillRect((int)(cx - ex - er), (int)ey, (int)(er * 2), 2, eyes);
    c.fillRect((int)(cx + ex - er), (int)ey, (int)(er * 2), 2, eyes);
  }
}

const char* num(int v) {
  static char buf[16];
  snprintf(buf, sizeof(buf), "%d", v);
  return buf;
}

}  // namespace games
