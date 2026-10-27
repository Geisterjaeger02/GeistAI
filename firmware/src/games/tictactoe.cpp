// Tic-Tac-Toe against GeistAI. You are X and start.
// Knob: choose a free field · press: place. Arrows move the cursor.
#include "draw.h"

namespace games {
namespace {

const int LINES[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                         {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

class TicTacToe : public Game {
 public:
  geist::Str name() const override { return geist::S_G_TICTACTOE; }
  geist::Str hint() const override { return geist::S_H_TICTACTOE; }
  bool hasScore() const override { return false; }

  void reset(uint32_t now) override {
    for (auto& c : b_) c = 0;
    over = won = false;
    result = geist::S_DRAW;
    cursor_ = 4;
    winLine_ = -1;
    aiAt_ = 0;
    (void)now;
  }

  void key(Key k, uint32_t now) override {
    if (aiAt_) return;  // GeistAI is thinking
    switch (k) {
      case Key::RotR: moveCursor(1); break;
      case Key::RotL: moveCursor(-1); break;
      case Key::Up: if (cursor_ >= 3) cursor_ -= 3; break;
      case Key::Down: if (cursor_ < 6) cursor_ += 3; break;
      case Key::Left: if (cursor_ % 3) cursor_--; break;
      case Key::Right: if (cursor_ % 3 < 2) cursor_++; break;
      case Key::Press:
        if (b_[cursor_]) break;
        b_[cursor_] = 1;
        if (!checkEnd()) aiAt_ = now + 350;  // a short "thinking" pause feels natural
        break;
      default: break;
    }
  }

  void tick(uint32_t now) override {
    if (!aiAt_ || now < aiAt_) return;
    aiAt_ = 0;
    int m = chooseMove();
    if (m >= 0) b_[m] = 2;
    if (!checkEnd() && b_[cursor_]) moveCursor(1);
  }

  void draw(Canvas& c, const Area& a, uint32_t now) override {
    Palette p = palette();
    drawPanel(c, a);
    int s = (a.h - 10) / 3;
    int ox = a.x + (a.w - 3 * s) / 2, oy = a.y + (a.h - 3 * s) / 2;
    for (int i = 1; i < 3; i++) {
      c.fillRoundRect(ox + i * s - 1, oy + 3, 3, 3 * s - 6, 1, p.fg, 110);
      c.fillRoundRect(ox + 3, oy + i * s - 1, 3 * s - 6, 3, 1, p.fg, 110);
    }
    for (int i = 0; i < 9; i++) {
      float cx = ox + (i % 3) * s + s / 2.f, cy = oy + (i / 3) * s + s / 2.f;
      bool hl = winLine_ >= 0 && (LINES[winLine_][0] == i || LINES[winLine_][1] == i ||
                                  LINES[winLine_][2] == i);
      if (hl && (now / 250) % 2) c.fillCircle(cx, cy, s * 0.42f, p.fg, 50);
      if (b_[i] == 1) drawX(c, cx, cy, s * 0.26f, p.fg);
      if (b_[i] == 2) drawO(c, cx, cy, s * 0.28f, p.danger);
      if (i == cursor_ && !over && !aiAt_) {
        float pulse = 0.6f + 0.4f * ((now / 300) % 2);
        c.ring(cx, cy, s * 0.42f, 2.f, p.soft, (uint8_t)(200 * pulse));
      }
    }
    // little "thinking" dots for GeistAI
    if (aiAt_)
      for (int i = 0; i < 3; i++)
        c.fillCircle(a.x + a.w - 24 + i * 8, a.y + 10, 2.f, p.danger,
                     (now / 120) % 3 == (uint32_t)i ? 255 : 90);
  }

 private:
  void moveCursor(int d) {
    for (int n = 0; n < 9; n++) {
      cursor_ = (cursor_ + d + 9) % 9;
      if (!b_[cursor_]) return;
    }
  }

  int winner(int* line = nullptr) const {
    for (int l = 0; l < 8; l++) {
      int v = b_[LINES[l][0]];
      if (v && v == b_[LINES[l][1]] && v == b_[LINES[l][2]]) {
        if (line) *line = l;
        return v;
      }
    }
    return 0;
  }

  bool full() const {
    for (int v : b_)
      if (!v) return false;
    return true;
  }

  bool checkEnd() {
    int w = winner(&winLine_);
    if (w || full()) {
      over = true;
      won = w == 1;
      result = w == 1 ? geist::S_YOU_WIN : (w == 2 ? geist::S_YOU_LOSE : geist::S_DRAW);
      return true;
    }
    return false;
  }

  int minimax(int player, int depth) {
    int w = winner();
    if (w == 2) return 10 - depth;
    if (w == 1) return depth - 10;
    if (full()) return 0;
    int best = player == 2 ? -100 : 100;
    for (int i = 0; i < 9; i++) {
      if (b_[i]) continue;
      b_[i] = player;
      int v = minimax(3 - player, depth + 1);
      b_[i] = 0;
      best = player == 2 ? (v > best ? v : best) : (v < best ? v : best);
    }
    return best;
  }

  int chooseMove() {
    int free[9], n = 0;
    for (int i = 0; i < 9; i++)
      if (!b_[i]) free[n++] = i;
    if (!n) return -1;
    // GeistAI plays perfectly most of the time, so you can still win sometimes
    if (rng().range(100) < 20) return free[rng().range(n)];
    int best = -1000, choice = free[0];
    for (int k = 0; k < n; k++) {
      b_[free[k]] = 2;
      int v = minimax(1, 1);
      b_[free[k]] = 0;
      if (v > best || (v == best && rng().range(2))) best = v, choice = free[k];
    }
    return choice;
  }

  int b_[9];
  int cursor_ = 4, winLine_ = -1;
  uint32_t aiAt_ = 0;
};

}  // namespace

Game* makeTicTacToe() { return new TicTacToe(); }

}  // namespace games
