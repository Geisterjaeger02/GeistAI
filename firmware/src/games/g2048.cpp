// 2048: slide the tiles, equal numbers merge. Reach 2048!
// Knob: turn to choose the direction (arrow on the board), press to slide.
// Arrow keys slide right away.
#include <math.h>

#include "draw.h"

namespace games {
namespace {

const uint16_t TILE[12] = {
    geist::rgb(238, 228, 218), geist::rgb(237, 224, 200), geist::rgb(242, 177, 121),
    geist::rgb(245, 149, 99),  geist::rgb(246, 124, 95),  geist::rgb(246, 94, 59),
    geist::rgb(237, 207, 114), geist::rgb(237, 204, 97),  geist::rgb(237, 200, 80),
    geist::rgb(237, 197, 63),  geist::rgb(237, 194, 46),  geist::rgb(60, 58, 50)};

class G2048 : public Game {
 public:
  geist::Str name() const override { return geist::S_G_2048; }
  geist::Str hint() const override { return geist::S_H_2048; }

  void reset(uint32_t now) override {
    for (auto& r : b_)
      for (auto& v : r) v = 0;
    for (auto& r : born_)
      for (auto& v : r) v = 0;
    score = 0;
    over = won = false;
    result = geist::S_GAME_OVER;
    dir_ = 0;
    addTile(now);
    addTile(now);
  }

  void key(Key k, uint32_t now) override {
    switch (k) {
      case Key::RotR: dir_ = (dir_ + 1) % 4; break;
      case Key::RotL: dir_ = (dir_ + 3) % 4; break;
      case Key::Press: slide(dir_, now); break;
      case Key::Up: dir_ = 0; slide(0, now); break;
      case Key::Right: dir_ = 1; slide(1, now); break;
      case Key::Down: dir_ = 2; slide(2, now); break;
      case Key::Left: dir_ = 3; slide(3, now); break;
      default: break;
    }
  }

  void tick(uint32_t) override {}

  void draw(Canvas& c, const Area& a, uint32_t now) override {
    Palette p = palette();
    int t = (a.h - 30) / 4;  // tile size (room for the direction arrows)
    int gap = t / 12 + 1;
    int bs = 4 * t + 5 * gap;
    int ox = a.x + (a.w - bs) / 2, oy = a.y + (a.h - bs) / 2;
    c.fillRoundRect(ox, oy, bs, bs, 8, p.field, 238);
    const geist::Font& f = t >= 30 ? geist::fontTitle() : geist::fontBody();
    for (int y = 0; y < 4; y++) {
      for (int x = 0; x < 4; x++) {
        int v = b_[y][x];
        int tx = ox + gap + x * (t + gap), ty = oy + gap + y * (t + gap);
        if (!v) {
          c.fillRoundRect(tx, ty, t, t, 5, p.fg, 35);
          continue;
        }
        // newly appeared tiles grow in
        float age = (now - born_[y][x]) / 120.f;
        int shrink = age < 1.f ? (int)((1.f - age) * t / 3) : 0;
        int lv = 0;
        while ((1 << (lv + 1)) <= v && lv < 11) lv++;
        uint16_t col = TILE[lv - 1 < 0 ? 0 : (lv - 1 > 11 ? 11 : lv - 1)];
        c.fillRoundRect(tx + shrink, ty + shrink, t - 2 * shrink, t - 2 * shrink, 5, col);
        const char* s = num(v);
        const geist::Font& ff = v >= 1000 && t < 40 ? geist::fontSmall() : f;
        uint16_t tc = v <= 4 ? geist::rgb(119, 110, 101) : geist::theme::SNOW;
        c.drawTextCentered(ff, tx + t / 2, ty + (t - ff.lineHeight) / 2, s, tc);
      }
    }
    // direction arrow next to the board
    float cx = ox + bs / 2.f, cy = oy + bs / 2.f, r = bs / 2.f + 7;
    static const float DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    for (int d = 0; d < 4; d++) {
      float ax = cx + DX[d] * r, ay = cy + DY[d] * r;
      bool sel = d == dir_;
      float s = sel ? 6.f : 4.f;
      uint16_t col = sel ? geist::theme::INK : geist::theme::INK_SOFT;
      if (sel) {  // pulse the chosen direction
        float k = 0.5f + 0.5f * sinf(now / 180.f);
        ax += DX[d] * k * 2;
        ay += DY[d] * k * 2;
      }
      // a chevron pointing outwards
      float tx = ax + DX[d] * s * 0.6f, ty = ay + DY[d] * s * 0.6f;
      drawLine(c, tx, ty, ax - DX[d] * s * 0.4f - DY[d] * s, ay - DY[d] * s * 0.4f + DX[d] * s,
               sel ? 3.f : 2.f, col);
      drawLine(c, tx, ty, ax - DX[d] * s * 0.4f + DY[d] * s, ay - DY[d] * s * 0.4f - DX[d] * s,
               sel ? 3.f : 2.f, col);
    }
  }

 private:
  void addTile(uint32_t now) {
    int empty[16], n = 0;
    for (int i = 0; i < 16; i++)
      if (!b_[i / 4][i % 4]) empty[n++] = i;
    if (!n) return;
    int i = empty[rng().range(n)];
    b_[i / 4][i % 4] = rng().range(10) == 0 ? 4 : 2;
    born_[i / 4][i % 4] = now;
  }

  // read / write the 4 cells of line `l` in direction d, index 0 = the far side
  int& cell(int d, int l, int i) {
    switch (d) {
      case 0: return b_[i][l];          // up
      case 1: return b_[l][3 - i];      // right
      case 2: return b_[3 - i][l];      // down
      default: return b_[l][i];         // left
    }
  }

  void slide(int d, uint32_t now) {
    bool moved = false;
    for (int l = 0; l < 4; l++) {
      int vals[4], n = 0;
      for (int i = 0; i < 4; i++)
        if (cell(d, l, i)) vals[n++] = cell(d, l, i);
      int out[4] = {0, 0, 0, 0}, m = 0;
      for (int i = 0; i < n; i++) {
        if (i + 1 < n && vals[i] == vals[i + 1]) {
          out[m++] = vals[i] * 2;
          score += vals[i] * 2;
          if (vals[i] * 2 == 2048 && !won) won = true;
          i++;
        } else {
          out[m++] = vals[i];
        }
      }
      for (int i = 0; i < 4; i++) {
        if (cell(d, l, i) != out[i]) moved = true;
        cell(d, l, i) = out[i];
      }
    }
    if (!moved) return;
    addTile(now);
    if (!canMove()) {
      over = true;
      result = won ? geist::S_YOU_WIN : geist::S_GAME_OVER;
    }
  }

  bool canMove() const {
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) {
        if (!b_[y][x]) return true;
        if (x < 3 && b_[y][x] == b_[y][x + 1]) return true;
        if (y < 3 && b_[y][x] == b_[y + 1][x]) return true;
      }
    return false;
  }

  int b_[4][4];
  uint32_t born_[4][4];
  int dir_ = 0;
};

}  // namespace

Game* make2048() { return new G2048(); }

}  // namespace games
