// Snake: eat, grow, don't bite yourself or the wall.
// Knob: turn left / right (relative). Arrow keys: absolute direction.
#include <math.h>

#include "draw.h"

namespace games {
namespace {

constexpr int CELL = 8;
constexpr int MAXLEN = 1024;
const int8_t DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};  // up right down left

class Snake : public Game {
 public:
  geist::Str name() const override { return geist::S_G_SNAKE; }
  geist::Str hint() const override { return geist::S_H_SNAKE; }

  void reset(uint32_t now) override {
    layout(playArea());
    score = 0;
    over = false;
    won = false;
    result = geist::S_GAME_OVER;
    len_ = 4;
    for (int i = 0; i < len_; i++) {
      bx_[i] = cols_ / 2 - i;
      by_[i] = rows_ / 2;
    }
    dir_ = 1;
    queued_ = 0;
    last_ = now;
    placeFood();
  }

  void key(Key k, uint32_t) override {
    int base = queued_ ? queue_[queued_ - 1] : dir_;
    int nd = -1;
    switch (k) {
      case Key::RotR: nd = (base + 1) % 4; break;
      case Key::RotL: nd = (base + 3) % 4; break;
      case Key::Up: nd = 0; break;
      case Key::Right: nd = 1; break;
      case Key::Down: nd = 2; break;
      case Key::Left: nd = 3; break;
      case Key::Press: wantPause = true; return;
      default: break;
    }
    if (nd < 0 || nd == base || nd == (base + 2) % 4) return;  // no U-turn
    if (queued_ < 3) queue_[queued_++] = nd;
  }

  void tick(uint32_t now) override {
    int interval = 150 - score * 3;
    if (interval < 70) interval = 70;
    if (now - last_ < (uint32_t)interval) return;
    last_ = now;
    if (queued_) {
      dir_ = queue_[0];
      for (int i = 1; i < queued_; i++) queue_[i - 1] = queue_[i];
      queued_--;
    }
    int nx = bx_[0] + DX[dir_], ny = by_[0] + DY[dir_];
    bool grow = nx == fx_ && ny == fy_;
    if (nx < 0 || ny < 0 || nx >= cols_ || ny >= rows_ || hits(nx, ny, grow ? len_ : len_ - 1)) {
      over = true;
      return;
    }
    if (grow && len_ < MAXLEN) len_++;
    for (int i = len_ - 1; i > 0; i--) {
      bx_[i] = bx_[i - 1];
      by_[i] = by_[i - 1];
    }
    bx_[0] = nx;
    by_[0] = ny;
    if (grow) {
      score++;
      if (len_ >= cols_ * rows_) {
        won = true;
        result = geist::S_YOU_WIN;
        over = true;
        return;
      }
      placeFood();
    }
  }

  void draw(Canvas& c, const Area& a, uint32_t now) override {
    layout(a);
    drawPanel(c, a);
    Palette p = palette();
    int ox = a.x + (a.w - cols_ * CELL) / 2, oy = a.y + (a.h - rows_ * CELL) / 2;
    // faint grid dots
    for (int y = 0; y < rows_; y++)
      for (int x = 0; x < cols_; x++)
        c.plot(ox + x * CELL + CELL / 2, oy + y * CELL + CELL / 2, p.fg, 40);
    float pulse = 0.8f + 0.2f * sinf(now / 150.f);
    c.fillCircle(ox + fx_ * CELL + CELL / 2.f, oy + fy_ * CELL + CELL / 2.f, 3.4f * pulse, p.danger);
    for (int i = len_ - 1; i >= 0; i--) {
      uint8_t alpha = (uint8_t)(255 - (i * 120) / (len_ + 1));
      c.fillRoundRect(ox + bx_[i] * CELL, oy + by_[i] * CELL, CELL - 1, CELL - 1, 3, p.fg, alpha);
    }
    // eyes
    float hx = ox + bx_[0] * CELL + CELL / 2.f, hy = oy + by_[0] * CELL + CELL / 2.f;
    float ex = DY[dir_] != 0 ? 1.8f : 0, ey = DX[dir_] != 0 ? 1.8f : 0;
    c.fillCircle(hx + ex + DX[dir_], hy + ey + DY[dir_], 1.1f, p.field);
    c.fillCircle(hx - ex + DX[dir_], hy - ey + DY[dir_], 1.1f, p.field);
  }

 private:
  void layout(const Area& a) {
    int c = (a.w - 4) / CELL, r = (a.h - 4) / CELL;
    if (c != cols_ || r != rows_) {
      cols_ = c;
      rows_ = r;
    }
  }

  bool hits(int x, int y, int n) const {
    for (int i = 0; i < n; i++)
      if (bx_[i] == x && by_[i] == y) return true;
    return false;
  }

  void placeFood() {
    for (int tries = 0; tries < 2000; tries++) {
      int x = rng().range(cols_), y = rng().range(rows_);
      if (!hits(x, y, len_)) {
        fx_ = x;
        fy_ = y;
        return;
      }
    }
  }

  int cols_ = 38, rows_ = 18;
  int16_t bx_[MAXLEN], by_[MAXLEN];
  int len_ = 4, dir_ = 1;
  int queue_[3], queued_ = 0;
  int fx_ = 0, fy_ = 0;
  uint32_t last_ = 0;
};

}  // namespace

Game* makeSnake() { return new Snake(); }

}  // namespace games
