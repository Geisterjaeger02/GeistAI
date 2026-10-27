// Minesweeper: open every field without a mine. Numbers tell how many mines
// touch a field. Knob: move · press: open · hold (Cardputer: F): flag.
// The score is your time in seconds - lower is better.
#include <stdio.h>
#include <stdlib.h>

#include "draw.h"

namespace games {
namespace {

constexpr int GW = 10, GH = 6, MINES = 9;

const uint16_t NUMCOL[9] = {0, geist::rgb(60, 140, 255), geist::rgb(60, 200, 90),
                            geist::rgb(255, 80, 80),   geist::rgb(170, 90, 255),
                            geist::rgb(255, 150, 40),  geist::rgb(40, 200, 200),
                            geist::rgb(230, 230, 230), geist::rgb(160, 160, 160)};

class Mines : public Game {
 public:
  geist::Str name() const override { return geist::S_G_MINES; }
  geist::Str hint() const override { return geist::S_H_MINES; }
  bool lowerIsBetter() const override { return true; }

  void reset(uint32_t now) override {
    for (int i = 0; i < GW * GH; i++) mine_[i] = open_[i] = flag_[i] = false;
    placed_ = false;
    over = won = false;
    result = geist::S_GAME_OVER;
    cursor_ = GW * (GH / 2) + GW / 2;
    score = 0;
    start_ = now;
    boom_ = -1;
  }

  void key(Key k, uint32_t now) override {
    switch (k) {
      case Key::RotR: cursor_ = (cursor_ + 1) % (GW * GH); break;
      case Key::RotL: cursor_ = (cursor_ + GW * GH - 1) % (GW * GH); break;
      case Key::Up: if (cursor_ >= GW) cursor_ -= GW; break;
      case Key::Down: if (cursor_ < GW * (GH - 1)) cursor_ += GW; break;
      case Key::Left: if (cursor_ % GW) cursor_--; break;
      case Key::Right: if (cursor_ % GW < GW - 1) cursor_++; break;
      case Key::LongPress:
        if (!open_[cursor_]) flag_[cursor_] = !flag_[cursor_];
        break;
      case Key::Press: reveal(cursor_, now); break;
      default: break;
    }
  }

  void tick(uint32_t now) override {
    if (placed_) score = (int)((now - start_) / 1000);
  }

  void draw(Canvas& c, const Area& a, uint32_t now) override {
    Palette p = palette();
    drawPanel(c, a);
    int cs = (a.h - 8) / GH;
    if (cs * GW > a.w - 8) cs = (a.w - 8) / GW;
    int ox = a.x + (a.w - cs * GW) / 2, oy = a.y + (a.h - cs * GH) / 2;
    const geist::Font& f = cs >= 20 ? geist::fontBody() : geist::fontSmall();
    for (int i = 0; i < GW * GH; i++) {
      int x = ox + (i % GW) * cs, y = oy + (i / GW) * cs;
      float cx = x + cs / 2.f, cy = y + cs / 2.f;
      if (open_[i] || (over && mine_[i])) {
        c.fillRoundRect(x + 1, y + 1, cs - 2, cs - 2, 3, p.fg, 40);
        if (mine_[i]) {
          if (i == boom_) c.fillRoundRect(x + 1, y + 1, cs - 2, cs - 2, 3, p.danger);
          drawMine(c, cx, cy, cs * 0.24f, i == boom_ ? geist::theme::SNOW : p.danger);
        } else if (int n = count(i)) {
          c.drawTextCentered(f, (int)cx, (int)cy - f.lineHeight / 2, num(n), NUMCOL[n]);
        }
      } else {
        c.fillRoundRect(x + 1, y + 1, cs - 2, cs - 2, 3, p.fg, 150);
        if (flag_[i]) {  // a little flag
          c.fillRect((int)cx - 1, (int)(cy - cs * 0.28f), 2, (int)(cs * 0.56f), p.field);
          c.fillCircle(cx + cs * 0.1f, cy - cs * 0.16f, cs * 0.16f, p.danger);
        }
      }
      if (i == cursor_ && !over) {
        uint8_t al = (now / 300) % 2 ? 255 : 150;
        c.fillRoundRect(x, y, cs, 2, 1, p.soft, al);
        c.fillRoundRect(x, y + cs - 2, cs, 2, 1, p.soft, al);
        c.fillRoundRect(x, y, 2, cs, 1, p.soft, al);
        c.fillRoundRect(x + cs - 2, y, 2, cs, 1, p.soft, al);
      }
    }
    // mines left
    int flags = 0;
    for (int i = 0; i < GW * GH; i++) flags += flag_[i];
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", MINES - flags);
    drawMine(c, a.x + 9, a.y + a.h - 8, 3.f, p.danger);
    c.drawText(geist::fontSmall(), a.x + 15, a.y + a.h - 14, buf, p.text);
  }

 private:
  static void drawMine(Canvas& c, float cx, float cy, float r, uint16_t col) {
    c.fillCircle(cx, cy, r, col);
    c.fillRect((int)(cx - r * 1.5f), (int)cy, (int)(r * 3), 1, col);
    c.fillRect((int)cx, (int)(cy - r * 1.5f), 1, (int)(r * 3), col);
  }

  int count(int i) const {
    int n = 0, x = i % GW, y = i / GW;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if ((dx || dy) && nx >= 0 && ny >= 0 && nx < GW && ny < GH && mine_[ny * GW + nx]) n++;
      }
    return n;
  }

  void place(int safe, uint32_t now) {
    int sx = safe % GW, sy = safe / GW, n = 0;
    while (n < MINES) {
      int i = rng().range(GW * GH);
      int x = i % GW, y = i / GW;
      if (mine_[i] || (abs(x - sx) <= 1 && abs(y - sy) <= 1)) continue;  // first field opens up
      mine_[i] = true;
      n++;
    }
    placed_ = true;
    start_ = now;
  }

  void reveal(int i, uint32_t now) {
    if (flag_[i] || open_[i]) return;
    if (!placed_) place(i, now);
    if (mine_[i]) {
      boom_ = i;
      over = true;
      result = geist::S_YOU_LOSE;
      return;
    }
    // flood fill through empty fields
    int stack[GW * GH], sp = 0;
    stack[sp++] = i;
    while (sp) {
      int j = stack[--sp];
      if (open_[j] || flag_[j]) continue;
      open_[j] = true;
      if (count(j)) continue;
      int x = j % GW, y = j / GW;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          int nx = x + dx, ny = y + dy;
          if (nx >= 0 && ny >= 0 && nx < GW && ny < GH && !open_[ny * GW + nx] && sp < GW * GH)
            stack[sp++] = ny * GW + nx;
        }
    }
    int closed = 0;
    for (int j = 0; j < GW * GH; j++) closed += !open_[j];
    if (closed == MINES) {
      over = true;
      won = true;
      result = geist::S_YOU_WIN;
      score = (int)((now - start_) / 1000) + 1;
    }
  }

  bool mine_[GW * GH], open_[GW * GH], flag_[GW * GH];
  bool placed_ = false;
  int cursor_ = 0, boom_ = -1;
  uint32_t start_ = 0;
};

}  // namespace

Game* makeMines() { return new Mines(); }

}  // namespace games
