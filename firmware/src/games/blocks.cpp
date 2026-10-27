// Blocks: falling-block puzzle. Complete lines to clear them.
// Knob: move left/right · press: rotate · hold: drop. Arrows: move, up rotates, down drops.
#include <stdio.h>

#include "draw.h"

namespace games {
namespace {

constexpr int BW = 10, BH = 20;

// 7 pieces, 4 rotations, 4 cells each (x, y in a 4x4 box)
const uint8_t SHAPES[7][4][4][2] = {
    // I
    {{{0, 1}, {1, 1}, {2, 1}, {3, 1}}, {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
     {{0, 2}, {1, 2}, {2, 2}, {3, 2}}, {{1, 0}, {1, 1}, {1, 2}, {1, 3}}},
    // O
    {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}, {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
     {{1, 0}, {2, 0}, {1, 1}, {2, 1}}, {{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
    // T
    {{{1, 0}, {0, 1}, {1, 1}, {2, 1}}, {{1, 0}, {1, 1}, {2, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {1, 2}}, {{1, 0}, {0, 1}, {1, 1}, {1, 2}}},
    // S
    {{{1, 0}, {2, 0}, {0, 1}, {1, 1}}, {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
     {{1, 1}, {2, 1}, {0, 2}, {1, 2}}, {{0, 0}, {0, 1}, {1, 1}, {1, 2}}},
    // Z
    {{{0, 0}, {1, 0}, {1, 1}, {2, 1}}, {{2, 0}, {1, 1}, {2, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {1, 2}, {2, 2}}, {{1, 0}, {0, 1}, {1, 1}, {0, 2}}},
    // J
    {{{0, 0}, {0, 1}, {1, 1}, {2, 1}}, {{1, 0}, {2, 0}, {1, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {2, 2}}, {{1, 0}, {1, 1}, {0, 2}, {1, 2}}},
    // L
    {{{2, 0}, {0, 1}, {1, 1}, {2, 1}}, {{1, 0}, {1, 1}, {1, 2}, {2, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {0, 2}}, {{0, 0}, {1, 0}, {1, 1}, {1, 2}}},
};

const uint16_t COLORS[7] = {geist::rgb(40, 230, 255), geist::rgb(255, 220, 40),
                            geist::rgb(190, 90, 255),  geist::rgb(70, 230, 90),
                            geist::rgb(255, 70, 90),   geist::rgb(60, 120, 255),
                            geist::rgb(255, 150, 40)};

class Blocks : public Game {
 public:
  geist::Str name() const override { return geist::S_G_BLOCKS; }
  geist::Str hint() const override { return geist::S_H_BLOCKS; }

  void reset(uint32_t now) override {
    for (auto& row : board_)
      for (auto& cell : row) cell = 0;
    score = 0;
    lines_ = 0;
    over = won = false;
    result = geist::S_GAME_OVER;
    bagLeft_ = 0;
    next_ = fromBag();
    spawn();
    last_ = now;
    flashUntil_ = 0;
  }

  void key(Key k, uint32_t now) override {
    switch (k) {
      case Key::RotL:
      case Key::Left: tryMove(-1, 0, rot_); break;
      case Key::RotR:
      case Key::Right: tryMove(1, 0, rot_); break;
      case Key::Press:
      case Key::Up: rotate(); break;
      case Key::LongPress:
      case Key::Down: {
        int d = 0;
        while (fits(px_, py_ + 1, rot_)) py_++, d++;
        score += d * 2;
        lock(now);
        break;
      }
      default: break;
    }
  }

  void tick(uint32_t now) override {
    int level = lines_ / 10 + 1;
    int interval = 650 - (level - 1) * 55;
    if (interval < 90) interval = 90;
    if (now - last_ < (uint32_t)interval) return;
    last_ = now;
    if (fits(px_, py_ + 1, rot_)) py_++;
    else lock(now);
  }

  void draw(Canvas& c, const Area& a, uint32_t now) override {
    Palette p = palette();
    int cell = (a.h - 6) / BH;
    int bw = BW * cell, bh = BH * cell;
    int ox = a.x + (a.w - bw) / 2 - cell * 3, oy = a.y + (a.h - bh) / 2;
    Area board{ox - 3, oy - 3, bw + 6, bh + 6};
    drawPanel(c, board);
    for (int y = 0; y < BH; y++) {
      for (int x = 0; x < BW; x++) {
        int v = board_[y][x];
        if (v) block(c, ox + x * cell, oy + y * cell, cell, COLORS[v - 1], 255);
        else c.plot(ox + x * cell + cell / 2, oy + y * cell + cell / 2, p.fg, 45);
      }
    }
    if (!over) {
      // ghost: where the piece will land
      int gy = py_;
      while (fits(px_, gy + 1, rot_)) gy++;
      for (auto& s : SHAPES[cur_][rot_])
        c.fillRect(ox + (px_ + s[0]) * cell, oy + (gy + s[1]) * cell, cell - 1, cell - 1,
                   COLORS[cur_], 60);
      for (auto& s : SHAPES[cur_][rot_])
        if (py_ + s[1] >= 0)
          block(c, ox + (px_ + s[0]) * cell, oy + (py_ + s[1]) * cell, cell, COLORS[cur_], 255);
    }
    // side panel: next piece, lines, level
    int sx = ox + bw + 10, sw = a.x + a.w - sx;
    Area side{sx, oy - 3, sw, cell * 6};
    drawPanel(c, side);
    int nc = cell;
    for (auto& s : SHAPES[next_][0])
      block(c, sx + (sw - 4 * nc) / 2 + s[0] * nc, oy + nc + s[1] * nc, nc, COLORS[next_], 255);
    char buf[32];
    int ty = oy + cell * 6 + 4;
    snprintf(buf, sizeof(buf), "%s %d", geist::tr(lang(), geist::S_LEVEL), lines_ / 10 + 1);
    c.drawText(geist::fontSmall(), sx + 2, ty, buf, geist::theme::INK);
    // cleared lines: a little stack of bars, then the number
    for (int i = 0; i < 3; i++) c.fillRect(sx + 2, ty + 16 + i * 3, 9, 2, geist::theme::INK_SOFT);
    snprintf(buf, sizeof(buf), "%d", lines_);
    c.drawText(geist::fontSmall(), sx + 15, ty + 13, buf, geist::theme::INK_SOFT);
    if (flashUntil_ > now)  // lines cleared: the board lights up briefly
      c.fillRoundRect(board.x, board.y, board.w, board.h, 8, geist::theme::SNOW,
                      (uint8_t)((flashUntil_ - now) * 90 / 120));
  }

 private:
  static void block(Canvas& c, int x, int y, int cell, uint16_t col, uint8_t a) {
    c.fillRect(x, y, cell - 1, cell - 1, col, a);
    c.fillRect(x, y, cell - 1, 1, geist::theme::SNOW, 110);  // a little shine on top
  }

  int fromBag() {
    if (bagLeft_ == 0) {
      for (int i = 0; i < 7; i++) bag_[i] = i;
      for (int i = 6; i > 0; i--) {
        int j = rng().range(i + 1);
        int t = bag_[i];
        bag_[i] = bag_[j];
        bag_[j] = t;
      }
      bagLeft_ = 7;
    }
    return bag_[--bagLeft_];
  }

  void spawn() {
    cur_ = next_;
    next_ = fromBag();
    rot_ = 0;
    px_ = 3;
    py_ = cur_ == 0 ? -1 : 0;
    if (!fits(px_, py_, rot_)) over = true;
  }

  bool fits(int x, int y, int r) const {
    for (auto& s : SHAPES[cur_][r]) {
      int bx = x + s[0], by = y + s[1];
      if (bx < 0 || bx >= BW || by >= BH) return false;
      if (by >= 0 && board_[by][bx]) return false;
    }
    return true;
  }

  void tryMove(int dx, int dy, int r) {
    if (fits(px_ + dx, py_ + dy, r)) {
      px_ += dx;
      py_ += dy;
      rot_ = r;
    }
  }

  void rotate() {
    int r = (rot_ + 1) % 4;
    for (int kick : {0, -1, 1, -2, 2}) {
      if (fits(px_ + kick, py_, r)) {
        px_ += kick;
        rot_ = r;
        return;
      }
    }
  }

  void lock(uint32_t now) {
    for (auto& s : SHAPES[cur_][rot_]) {
      int by = py_ + s[1];
      if (by < 0) {
        over = true;
        return;
      }
      board_[by][px_ + s[0]] = cur_ + 1;
    }
    int cleared = 0;
    for (int y = BH - 1; y >= 0; y--) {
      bool full = true;
      for (int x = 0; x < BW; x++) full &= board_[y][x] != 0;
      if (!full) continue;
      cleared++;
      for (int yy = y; yy > 0; yy--)
        for (int x = 0; x < BW; x++) board_[yy][x] = board_[yy - 1][x];
      for (int x = 0; x < BW; x++) board_[0][x] = 0;
      y++;  // check the same row again
    }
    if (cleared) {
      static const int pts[5] = {0, 100, 300, 500, 800};
      score += pts[cleared] * (lines_ / 10 + 1);
      lines_ += cleared;
      flashUntil_ = now + 120;
    }
    spawn();
    last_ = now;
  }

  uint8_t board_[BH][BW];
  int cur_ = 0, next_ = 0, rot_ = 0, px_ = 3, py_ = 0;
  int lines_ = 0;
  int bag_[7], bagLeft_ = 0;
  uint32_t last_ = 0, flashUntil_ = 0;
};

}  // namespace

Game* makeBlocks() { return new Blocks(); }

}  // namespace games
