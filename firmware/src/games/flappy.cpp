// Flappy Ghost: the GeistAI ghost flies through the gaps.
// Press (or the up arrow) to fly up.
#include <math.h>

#include "draw.h"

namespace games {
namespace {

constexpr int PIPES = 4;

class Flappy : public Game {
 public:
  geist::Str name() const override { return geist::S_G_FLAPPY; }
  geist::Str hint() const override { return geist::S_H_FLAPPY; }

  void reset(uint32_t now) override {
    a_ = playArea();
    score = 0;
    over = won = false;
    result = geist::S_GAME_OVER;
    y_ = a_.y + a_.h * 0.45f;
    vy_ = 0;
    started_ = false;
    for (int i = 0; i < PIPES; i++) {
      px_[i] = a_.x + a_.w + i * spacing();
      gapY_[i] = newGap();
      passed_[i] = false;
    }
    last_ = now;
    flapAt_ = 0;
  }

  void key(Key k, uint32_t now) override {
    if (k == Key::PressDown || k == Key::Up || k == Key::RotL || k == Key::RotR) {
      started_ = true;
      vy_ = -a_.h * 1.75f;
      flapAt_ = now;
    }
  }

  void tick(uint32_t now) override {
    float dt = (now - last_) / 1000.f;
    last_ = now;
    if (dt > 0.05f) dt = 0.05f;
    if (!started_) {  // hover until the first flap
      y_ = a_.y + a_.h * 0.45f + sinf(now / 300.f) * 4;
      return;
    }
    vy_ += a_.h * 5.6f * dt;
    y_ += vy_ * dt;
    float speed = a_.w * (0.26f + score * 0.006f);
    if (speed > a_.w * 0.5f) speed = a_.w * 0.5f;
    float gx = ghostX(), r = radius();
    for (int i = 0; i < PIPES; i++) {
      px_[i] -= speed * dt;
      if (px_[i] + pipeW() < a_.x) {  // recycle at the right
        float maxX = px_[0];
        for (int j = 0; j < PIPES; j++) maxX = fmaxf(maxX, px_[j]);
        px_[i] = maxX + spacing();
        gapY_[i] = newGap();
        passed_[i] = false;
      }
      if (!passed_[i] && px_[i] + pipeW() < gx - r) {
        passed_[i] = true;
        score++;
      }
      // collision with the pipe (a little forgiving)
      if (gx + r * 0.8f > px_[i] && gx - r * 0.8f < px_[i] + pipeW()) {
        float half = gap() / 2;
        if (y_ - r * 0.8f < gapY_[i] - half || y_ + r * 0.8f > gapY_[i] + half) over = true;
      }
    }
    if (y_ + r > a_.y + a_.h || y_ - r < a_.y) over = true;
  }

  void draw(Canvas& c, const Area& a, uint32_t now) override {
    a_ = a;
    Palette p = palette();
    drawPanel(c, a);
    c.setClip(a.x, a.y, a.w, a.h);
    // drifting stars
    for (int i = 0; i < 24; i++) {
      int drift = (int)(now / (25u * (1 + i % 3)));  // far stars move slower
      int sx = a.x + ((i * 73 - drift) % a.w + a.w) % a.w;
      int sy = a.y + (i * 37) % a.h;
      c.plot(sx, sy, p.fg, 90);
    }
    for (int i = 0; i < PIPES; i++) {
      float half = gap() / 2;
      int x = (int)px_[i], w = (int)pipeW();
      int top = (int)(gapY_[i] - half), bot = (int)(gapY_[i] + half);
      c.fillRect(x, a.y, w, top - a.y, p.fg, 220);
      c.fillRect(x - 2, top - 6, w + 4, 6, p.fg);
      c.fillRect(x, bot, w, a.y + a.h - bot, p.fg, 220);
      c.fillRect(x - 2, bot, w + 4, 6, p.fg);
    }
    float tilt = started_ ? vy_ / (a_.h * 4.f) : 0;
    bool blink = (now / 1700) % 6 == 0 && (now % 1700) < 140;
    drawGhost(c, ghostX(), y_ + tilt * 2, radius(), geist::theme::SNOW, p.field, blink ? 1.f : 0.f);
    if (flapAt_ && now - flapAt_ < 150)  // little puff under the ghost
      c.fillCircle(ghostX() - radius(), y_ + radius(), 3.f, geist::theme::SNOW, 90);
    c.resetClip();
  }

 private:
  float radius() const { return a_.h / 15.f + 1; }
  float ghostX() const { return a_.x + a_.w * 0.25f; }
  float pipeW() const { return a_.w / 11.f; }
  float spacing() const { return a_.w * 0.42f; }
  float gap() const { return a_.h * 0.42f; }
  float newGap() { return a_.y + gap() / 2 + 6 + rng().range((int)(a_.h - gap() - 12)); }

  Area a_{0, 0, 300, 150};
  float y_ = 0, vy_ = 0;
  float px_[PIPES], gapY_[PIPES];
  bool passed_[PIPES];
  bool started_ = false;
  uint32_t last_ = 0, flapAt_ = 0;
};

}  // namespace

Game* makeFlappy() { return new Flappy(); }

}  // namespace games
