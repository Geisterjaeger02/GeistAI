// GeistAI - tiny software renderer (portable C++, no Arduino dependency).
// Everything is drawn into an RGB565 framebuffer which is then pushed to the
// panel in one go. Keeping this portable lets tools/sim render the exact same
// UI on a PC for previews.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace geist {

// ---------------------------------------------------------------- colours
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Blend fg over bg with alpha 0..255.
inline uint16_t blend(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a == 0) return bg;
  if (a == 255) return fg;
  // 5-6-5 split, 5-bit alpha keeps it fast and good enough on a 1.9" panel
  uint32_t a5 = (a + 4) >> 3;  // 0..32
  uint32_t b = bg, f = fg;
  uint32_t bgx = (b | (b << 16)) & 0x07E0F81F;
  uint32_t fgx = (f | (f << 16)) & 0x07E0F81F;
  uint32_t res = ((((fgx - bgx) * a5) >> 5) + bgx) & 0x07E0F81F;
  return (uint16_t)((res >> 16) | res);
}

// Interpolate two colours, t in 0..255
inline uint16_t mix(uint16_t a, uint16_t b, uint8_t t) { return blend(a, b, t); }

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

// ---------------------------------------------------------------- fonts
struct Glyph {
  uint16_t cp;
  uint8_t w, h;
  int8_t xo, yo;  // offset from pen position / baseline
  uint8_t adv;
  uint32_t off;   // into 4bpp bitmap
};

struct Font {
  const Glyph* glyphs;
  uint16_t count;
  const uint8_t* bitmap;
  uint8_t lineHeight;
  uint8_t ascent;
};

// Decode one UTF-8 code point and advance p. Returns 0 at end of string.
uint32_t utf8Next(const char*& p, const char* end);

const Glyph* findGlyph(const Font& f, uint32_t cp);

int textWidth(const Font& f, const char* s, int len = -1);

// A wrapped line: byte range into the source string.
struct Line {
  int start;
  int len;
};

// Word-wraps s into lines no wider than maxWidth. Returns the number of lines
// written (at most maxLines). Honours '\n'.
int wrapText(const Font& f, const char* s, int maxWidth, Line* out, int maxLines);

// ---------------------------------------------------------------- canvas
class Canvas {
 public:
  Canvas(uint16_t* buffer, int w, int h);

  int width() const { return w_; }
  int height() const { return h_; }
  uint16_t* data() { return px_; }

  void setClip(int x, int y, int w, int h);
  void resetClip();
  Rect clip() const { return clip_; }

  void fill(uint16_t c);
  void fillRect(int x, int y, int w, int h, uint16_t c);
  void fillRect(int x, int y, int w, int h, uint16_t c, uint8_t alpha) {
    if (alpha == 255) fillRect(x, y, w, h, c);
    else blendRect(x, y, w, h, c, alpha);
  }
  void blendRect(int x, int y, int w, int h, uint16_t c, uint8_t alpha);
  void vGradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom);
  void hLine(int x, int y, int w, uint16_t c, uint8_t alpha = 255);

  // Anti-aliased primitives
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c, uint8_t alpha = 255);
  void fillCircle(float cx, float cy, float r, uint16_t c, uint8_t alpha = 255);
  void ring(float cx, float cy, float r, float thickness, uint16_t c, uint8_t alpha = 255);
  void arc(float cx, float cy, float r, float thickness, float a0, float a1, uint16_t c,
           uint8_t alpha = 255);

  // 8-bit alpha mask (e.g. logo, icons). clipRight limits drawing to x < clipRight (reveal fx).
  void drawMask8(int x, int y, const uint8_t* mask, int w, int h, uint16_t c,
                 uint8_t opacity = 255, int clipRight = 1 << 30);

  // Text. y is the top of the line box. Returns pen x after the text.
  int drawText(const Font& f, int x, int y, const char* s, uint16_t c, uint8_t opacity = 255,
               int len = -1);
  int drawTextCentered(const Font& f, int cx, int y, const char* s, uint16_t c,
                       uint8_t opacity = 255);
  int drawTextRight(const Font& f, int rx, int y, const char* s, uint16_t c,
                    uint8_t opacity = 255);

  inline void plot(int x, int y, uint16_t c, uint8_t a) {
    if (x < clip_.x || y < clip_.y || x >= clip_.x + clip_.w || y >= clip_.y + clip_.h) return;
    uint16_t& d = px_[y * w_ + x];
    d = blend(d, c, a);
  }

 private:
  void drawGlyph(const Font& f, const Glyph& g, int x, int baseline, uint16_t c, uint8_t opacity);
  bool clipSpan(int& x, int& y, int& w, int& h) const;

  uint16_t* px_;
  int w_, h_;
  Rect clip_;
};

}  // namespace geist
