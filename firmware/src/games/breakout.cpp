// Breakout: bounce the ball, clear all bricks. Three lives.
// Knob / arrows: move the paddle · press: launch the ball.
#include <math.h>

#include "draw.h"

namespace games {
namespace {

constexpr int COLS = 10, ROWS = 5;

const uint16_t ROWCOL[ROWS] = {geist::rgb(255, 70, 110), geist::rgb(255, 150, 50),
                               geist::rgb(255, 220, 60), geist::rgb(80, 230, 110),
                               geist::rgb(60, 200, 255)};

class Breakout : public Game {
 public:
  geist::Str name() const override { return geist::S_G_BREAKOUT; }
  geist::Str hint() const override { return geist::S_H_BREAKOUT; }

  void reset(uint32_t now) override {
    a_ = playArea();
    score = 0;
    over = won = false;
    result = geist::S_GAME_OVER;
    lives_ = 3;
    level_ = 1;
    fillBricks();
    paddleX_ = target_ = a_.x + a_.w / 2.f;
    serve();
    last_ = now;
  }

  void key(Key k, uint32_t) override {
    float step = paddleW() * 0.45f;
    switch (k) {
      case Key::RotL: target_ -= step; break;
      case Key::RotR: target_ += step; break;
      case Key::Left: target_ -= step * 1.5f; break;
      case Key::Right: target_ += step * 1.5f; break;
      case Key::PressDown:
      case Key::Up:
        if (stuck_) {
          stuck_ = false;
          float s = speed();
          vx_ = s * 0.55f * (rng().range(2) ? 1 : -1);
          vy_ = -s * 0.83f;
        }
        break;
      default: break;
    }
    float half = paddleW() / 2;
    if (target_ < a_.x + half + 3) target_ = a_.x + half + 3;
    if (target_ > a_.x + a_.w - half - 3) target_ = a_.x + a_.w - half - 3;
  }

  void tick(uint32_t now) override {
    float dt = (now - last_) / 1000.f;
    last_ = now;
    if (dt > 0.05f) dt = 0.05f;
    paddleX_ += (target_ - paddleX_) * (1.f - expf(-dt * 18.f));
    if (stuck_) {
      bx_ = paddleX_;
      by_ = paddleY() - 4;
      return;
    }
    const int sub = 4;
    for (int i = 0; i < sub && !over; i++) step(dt / sub);
  }

  void draw(Canvas& c, const Area& a, uint32_t now) override {
    a_ = a;
    Palette p = palette();
    drawPanel(c, a);
    float bw, bh, ox, oy;
    brickGeom(bw, bh, ox, oy);
    for (int r = 0; r < ROWS; r++)
      for (int col = 0; col < COLS; col++)
        if (bricks_[r][col])
          c.fillRoundRect((int)(ox + col * bw) + 1, (int)(oy + r * bh) + 1, (int)bw - 2,
                          (int)bh - 2, 2, ROWCOL[r], bricks_[r][col] > 1 ? 255 : 200);
    float half = paddleW() / 2;
    c.fillRoundRect((int)(paddleX_ - half), (int)paddleY(), (int)(half * 2), 5, 2, p.fg);
    c.fillCircle(bx_, by_, 3.2f, geist::theme::SNOW);
    for (int i = 0; i < lives_; i++) c.fillCircle(a.x + 8 + i * 9, a.y + a.h - 7, 2.6f, p.fg, 160);
    if (stuck_ && (now / 500) % 2) c.fillCircle(bx_, by_, 6.f, geist::theme::SNOW, 60);
  }

 private:
  float paddleW() const { return a_.w / 6.5f; }
  float paddleY() const { return a_.y + a_.h - 14; }
  float speed() const { return a_.h * (1.25f + 0.15f * level_); }

  void brickGeom(float& bw, float& bh, float& ox, float& oy) const {
    bw = (a_.w - 10) / (float)COLS;
    bh = a_.h / 13.f;
    ox = a_.x + 5;
    oy = a_.y + 6;
  }

  void fillBricks() {
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++) bricks_[r][c] = (level_ > 1 && r < 2) ? 2 : 1;
  }

  void serve() {
    stuck_ = true;
    bx_ = paddleX_;
    by_ = paddleY() - 4;
    vx_ = vy_ = 0;
  }

  void step(float dt) {
    bx_ += vx_ * dt;
    by_ += vy_ * dt;
    const float r = 3.f;
    if (bx_ < a_.x + r) bx_ = a_.x + r, vx_ = fabsf(vx_);
    if (bx_ > a_.x + a_.w - r) bx_ = a_.x + a_.w - r, vx_ = -fabsf(vx_);
    if (by_ < a_.y + r) by_ = a_.y + r, vy_ = fabsf(vy_);
    // paddle: the angle depends on where the ball hits
    float half = paddleW() / 2;
    if (vy_ > 0 && by_ + r >= paddleY() && by_ < paddleY() + 5 && fabsf(bx_ - paddleX_) <= half + r) {
      float off = (bx_ - paddleX_) / half;  // -1 .. 1
      float s = speed();
      vx_ = s * off * 0.85f;
      vy_ = -sqrtf(fmaxf(s * s - vx_ * vx_, s * s * 0.25f));
      by_ = paddleY() - r;
    }
    if (by_ > a_.y + a_.h) {
      if (--lives_ <= 0) {
        over = true;
        return;
      }
      serve();
      return;
    }
    // bricks
    float bw, bh, ox, oy;
    brickGeom(bw, bh, ox, oy);
    int col = (int)((bx_ - ox) / bw), row = (int)((by_ - oy) / bh);
    if (row >= 0 && row < ROWS && col >= 0 && col < COLS && bricks_[row][col]) {
      if (--bricks_[row][col] == 0) score += (ROWS - row) * 10 * level_;
      // bounce on the side that was hit
      float cx = ox + (col + 0.5f) * bw, cy = oy + (row + 0.5f) * bh;
      if (fabsf(bx_ - cx) / bw > fabsf(by_ - cy) / bh) vx_ = -vx_;
      else vy_ = -vy_;
      bool left = false;
      for (auto& rr : bricks_)
        for (auto v : rr) left |= v != 0;
      if (!left) {
        level_++;
        fillBricks();
        serve();
      }
    }
  }

  Area a_{0, 0, 300, 150};
  uint8_t bricks_[ROWS][COLS];
  float paddleX_ = 0, target_ = 0, bx_ = 0, by_ = 0, vx_ = 0, vy_ = 0;
  bool stuck_ = true;
  int lives_ = 3, level_ = 1;
  uint32_t last_ = 0;
};

}  // namespace

Game* makeBreakout() { return new Breakout(); }

}  // namespace games
