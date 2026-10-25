#include "gfx.h"

#include <math.h>
#include <string.h>

namespace geist {

// ------------------------------------------------------------------ utf-8
uint32_t utf8Next(const char*& p, const char* end) {
  if (p >= end || *p == 0) return 0;
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
  if (extra == 0) return 0xFFFD;  // stray continuation byte
  uint32_t cp = c & (0x3F >> extra);
  for (int i = 0; i < extra; i++) {
    if (p >= end || ((uint8_t)*p & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | ((uint8_t)*p++ & 0x3F);
  }
  return cp;
}

const Glyph* findGlyph(const Font& f, uint32_t cp) {
  int lo = 0, hi = f.count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint16_t v = f.glyphs[mid].cp;
    if (v == cp) return &f.glyphs[mid];
    if (v < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

static const Glyph* glyphOrFallback(const Font& f, uint32_t cp) {
  const Glyph* g = findGlyph(f, cp);
  if (g) return g;
  // Map a few common typographic characters to ones we have.
  switch (cp) {
    case 0x00A0: cp = ' '; break;
    case 0x2010: case 0x2011: case 0x2212: cp = '-'; break;
    case 0x2032: cp = '\''; break;
    case 0x2033: cp = '"'; break;
    default: cp = '?'; break;
  }
  return findGlyph(f, cp);
}

int textWidth(const Font& f, const char* s, int len) {
  if (!s) return 0;
  const char* end = s + (len < 0 ? strlen(s) : (size_t)len);
  int w = 0;
  const char* p = s;
  while (p < end) {
    uint32_t cp = utf8Next(p, end);
    if (!cp) break;
    if (cp == '\n') continue;
    const Glyph* g = glyphOrFallback(f, cp);
    if (g) w += g->adv;
  }
  return w;
}

int wrapText(const Font& f, const char* s, int maxWidth, Line* out, int maxLines) {
  if (!s || maxLines <= 0) return 0;
  const char* base = s;
  const char* end = s + strlen(s);
  int n = 0;
  const char* lineStart = s;
  const char* p = s;
  const char* lastBreak = nullptr;  // position after last space
  int width = 0;
  int widthAtBreak = 0;

  auto emit = [&](const char* a, const char* b) {
    // trim trailing spaces
    while (b > a && (b[-1] == ' ')) b--;
    if (n < maxLines) out[n] = Line{(int)(a - base), (int)(b - a)};
    n++;
  };

  while (p < end) {
    const char* cpStart = p;
    uint32_t cp = utf8Next(p, end);
    if (!cp) break;
    if (cp == '\n') {
      emit(lineStart, cpStart);
      lineStart = p;
      lastBreak = nullptr;
      width = 0;
      if (n >= maxLines) return maxLines;
      continue;
    }
    const Glyph* g = glyphOrFallback(f, cp);
    int adv = g ? g->adv : 0;
    if (cp == ' ' && width == 0 && cpStart == lineStart) {  // skip leading spaces
      lineStart = p;
      continue;
    }
    if (width + adv > maxWidth && width > 0) {
      if (lastBreak && lastBreak > lineStart) {
        emit(lineStart, lastBreak);
        lineStart = lastBreak;
        width -= widthAtBreak;
      } else {  // hard break inside a long word
        emit(lineStart, cpStart);
        lineStart = cpStart;
        width = 0;
      }
      lastBreak = nullptr;
      if (n >= maxLines) return maxLines;
    }
    width += adv;
    if (cp == ' ' || cp == '-' || cp == '/') {
      lastBreak = p;
      widthAtBreak = width;
    }
  }
  if (lineStart < end) emit(lineStart, end);
  return n < maxLines ? n : maxLines;
}

// ------------------------------------------------------------------ canvas
Canvas::Canvas(uint16_t* buffer, int w, int h) : px_(buffer), w_(w), h_(h) { resetClip(); }

void Canvas::setClip(int x, int y, int w, int h) {
  int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  int x1 = x + w > w_ ? w_ : x + w, y1 = y + h > h_ ? h_ : y + h;
  clip_ = Rect{x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
}

void Canvas::resetClip() { clip_ = Rect{0, 0, w_, h_}; }

bool Canvas::clipSpan(int& x, int& y, int& w, int& h) const {
  int x1 = x + w, y1 = y + h;
  if (x < clip_.x) x = clip_.x;
  if (y < clip_.y) y = clip_.y;
  if (x1 > clip_.x + clip_.w) x1 = clip_.x + clip_.w;
  if (y1 > clip_.y + clip_.h) y1 = clip_.y + clip_.h;
  w = x1 - x;
  h = y1 - y;
  return w > 0 && h > 0;
}

void Canvas::fill(uint16_t c) { fillRect(0, 0, w_, h_, c); }

void Canvas::fillRect(int x, int y, int w, int h, uint16_t c) {
  if (!clipSpan(x, y, w, h)) return;
  for (int j = 0; j < h; j++) {
    uint16_t* row = px_ + (y + j) * w_ + x;
    for (int i = 0; i < w; i++) row[i] = c;
  }
}

void Canvas::blendRect(int x, int y, int w, int h, uint16_t c, uint8_t alpha) {
  if (alpha == 255) return fillRect(x, y, w, h, c);
  if (!alpha || !clipSpan(x, y, w, h)) return;
  for (int j = 0; j < h; j++) {
    uint16_t* row = px_ + (y + j) * w_ + x;
    for (int i = 0; i < w; i++) row[i] = blend(row[i], c, alpha);
  }
}

void Canvas::vGradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom) {
  if (h <= 0) return;
  for (int j = 0; j < h; j++) {
    uint8_t t = (uint8_t)((j * 255) / (h > 1 ? h - 1 : 1));
    fillRect(x, y + j, w, 1, mix(top, bottom, t));
  }
}

void Canvas::hLine(int x, int y, int w, uint16_t c, uint8_t alpha) { blendRect(x, y, w, 1, c, alpha); }

static inline uint8_t cov(float v) {
  if (v <= 0.f) return 0;
  if (v >= 1.f) return 255;
  return (uint8_t)(v * 255.f + 0.5f);
}

static inline uint8_t mul8(uint8_t a, uint8_t b) { return (uint8_t)((a * b + 127) / 255); }

void Canvas::fillRoundRect(int x, int y, int w, int h, int r, uint16_t c, uint8_t alpha) {
  if (w <= 0 || h <= 0) return;
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (r <= 0) return blendRect(x, y, w, h, c, alpha);
  // middle band
  blendRect(x, y + r, w, h - 2 * r, c, alpha);
  for (int j = 0; j < r; j++) {
    for (int pass = 0; pass < 2; pass++) {
      int py = pass == 0 ? y + j : y + h - 1 - j;
      float dy = (r - (j + 0.5f));
      // inner span without AA
      blendRect(x + r, py, w - 2 * r, 1, c, alpha);
      for (int i = 0; i < r; i++) {
        float dx = (r - (i + 0.5f));
        float d = sqrtf(dx * dx + dy * dy);
        uint8_t a = mul8(cov(r - d + 0.5f), alpha);
        if (!a) continue;
        plot(x + i, py, c, a);
        plot(x + w - 1 - i, py, c, a);
      }
    }
  }
}

void Canvas::fillCircle(float cx, float cy, float r, uint16_t c, uint8_t alpha) {
  int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  for (int y = y0; y <= y1; y++) {
    float dy = y + 0.5f - cy;
    for (int x = x0; x <= x1; x++) {
      float dx = x + 0.5f - cx;
      float d = sqrtf(dx * dx + dy * dy);
      uint8_t a = mul8(cov(r - d + 0.5f), alpha);
      if (a) plot(x, y, c, a);
    }
  }
}

void Canvas::ring(float cx, float cy, float r, float t, uint16_t c, uint8_t alpha) {
  arc(cx, cy, r, t, 0.f, 6.2831853f, c, alpha);
}

void Canvas::arc(float cx, float cy, float r, float t, float a0, float a1, uint16_t c,
                 uint8_t alpha) {
  const float TWO_PI_F = 6.2831853f;
  bool full = (a1 - a0) >= TWO_PI_F - 1e-3f;
  float ro = r + t * 0.5f, ri = r - t * 0.5f;
  int x0 = (int)floorf(cx - ro - 1), x1 = (int)ceilf(cx + ro + 1);
  int y0 = (int)floorf(cy - ro - 1), y1 = (int)ceilf(cy + ro + 1);
  // normalise start angle into [0, 2pi)
  float s = fmodf(a0, TWO_PI_F);
  if (s < 0) s += TWO_PI_F;
  float span = a1 - a0;
  for (int y = y0; y <= y1; y++) {
    float dy = y + 0.5f - cy;
    for (int x = x0; x <= x1; x++) {
      float dx = x + 0.5f - cx;
      float d = sqrtf(dx * dx + dy * dy);
      uint8_t a = mul8(cov(ro - d + 0.5f), cov(d - ri + 0.5f));
      if (!a) continue;
      if (!full) {
        float ang = atan2f(dy, dx);
        if (ang < 0) ang += TWO_PI_F;
        float rel = ang - s;
        if (rel < 0) rel += TWO_PI_F;
        if (rel > span) continue;
      }
      plot(x, y, c, mul8(a, alpha));
    }
  }
}

void Canvas::drawMask8(int x, int y, const uint8_t* mask, int w, int h, uint16_t c, uint8_t opacity,
                       int clipRight) {
  int cx0 = clip_.x, cy0 = clip_.y, cx1 = clip_.x + clip_.w, cy1 = clip_.y + clip_.h;
  if (clipRight < cx1) cx1 = clipRight;
  for (int j = 0; j < h; j++) {
    int py = y + j;
    if (py < cy0 || py >= cy1) continue;
    const uint8_t* src = mask + j * w;
    uint16_t* dst = px_ + py * w_;
    for (int i = 0; i < w; i++) {
      int px = x + i;
      if (px < cx0 || px >= cx1) continue;
      uint8_t a = src[i];
      if (!a) continue;
      if (opacity != 255) a = mul8(a, opacity);
      dst[px] = blend(dst[px], c, a);
    }
  }
}

void Canvas::drawGlyph(const Font& f, const Glyph& g, int x, int baseline, uint16_t c,
                       uint8_t opacity) {
  if (!g.w || !g.h) return;
  const uint8_t* bm = f.bitmap + g.off;
  int gx = x + g.xo, gy = baseline + g.yo;
  int idx = 0;
  for (int j = 0; j < g.h; j++) {
    for (int i = 0; i < g.w; i++, idx++) {
      uint8_t byte = bm[idx >> 1];
      uint8_t n = (idx & 1) ? (byte & 0x0F) : (byte >> 4);
      if (!n) continue;
      uint8_t a = n * 17;
      if (opacity != 255) a = mul8(a, opacity);
      plot(gx + i, gy + j, c, a);
    }
  }
}

int Canvas::drawText(const Font& f, int x, int y, const char* s, uint16_t c, uint8_t opacity,
                     int len) {
  if (!s) return x;
  const char* end = s + (len < 0 ? strlen(s) : (size_t)len);
  int baseline = y + f.ascent;
  const char* p = s;
  while (p < end) {
    uint32_t cp = utf8Next(p, end);
    if (!cp) break;
    if (cp == '\n') continue;
    const Glyph* g = glyphOrFallback(f, cp);
    if (!g) continue;
    drawGlyph(f, *g, x, baseline, c, opacity);
    x += g->adv;
  }
  return x;
}

int Canvas::drawTextCentered(const Font& f, int cx, int y, const char* s, uint16_t c,
                             uint8_t opacity) {
  int w = textWidth(f, s);
  return drawText(f, cx - w / 2, y, s, c, opacity);
}

int Canvas::drawTextRight(const Font& f, int rx, int y, const char* s, uint16_t c,
                          uint8_t opacity) {
  int w = textWidth(f, s);
  return drawText(f, rx - w, y, s, c, opacity);
}

}  // namespace geist
