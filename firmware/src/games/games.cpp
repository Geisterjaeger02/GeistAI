#include "games.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../i18n.h"
#include "draw.h"

using namespace geist;
using namespace geist::theme;

#define font_body geist::fontBody()
#define font_small geist::fontSmall()
#define font_title geist::fontTitle()

namespace games {

void (*onNewBest)(int game, int value) = nullptr;

namespace {

Game* (*const kFactories[COUNT])() = {makeSnake,    makeChess, makeBlocks,    make2048,
                                      makeBreakout, makeFlappy, makeTicTacToe, makeMines};

Game* g_games[COUNT] = {nullptr};
int g_best[COUNT] = {0};
int g_sel = 0;         // menu selection
uint8_t g_lang = 0;
int g_active = -1;     // running game
int g_pauseSel = 0;    // pause menu: resume / restart / quit
bool g_newRecord = false;
bool g_ready = false;  // showing the "press to start" card
Screen g_screen = Screen::Menu;

Game* game(int i) {
  if (!g_games[i]) g_games[i] = kFactories[i]();
  return g_games[i];
}

void finishGame() {
  Game* gm = game(g_active);
  g_newRecord = false;
  if (!gm->hasScore()) return;
  bool better = gm->lowerIsBetter() ? (gm->won && (g_best[g_active] == 0 ||
                                                   gm->score < g_best[g_active]))
                                    : gm->score > g_best[g_active];
  if (better && gm->score > 0) {
    g_best[g_active] = gm->score;
    g_newRecord = true;
    if (onNewBest) onNewBest(g_active, gm->score);
  }
}

// ------------------------------------------------------------ menu icons
void iconSnake(Canvas& c, int cx, int cy, int s, uint16_t col) {
  float r = s * 0.11f;
  const float pts[][2] = {{-0.38f, 0.25f}, {-0.2f, 0.25f}, {-0.02f, 0.25f}, {-0.02f, 0.05f},
                          {-0.02f, -0.15f}, {0.18f, -0.15f}, {0.36f, -0.15f}};
  for (auto& p : pts) c.fillCircle(cx + p[0] * s, cy + p[1] * s, r, col);
  c.fillCircle(cx + 0.36f * s, cy + 0.2f * s, r * 0.9f, ERR_RED);
}

void iconChess(Canvas& c, int cx, int cy, int s, uint16_t col) {
  drawChessPiece(c, cx - 9, cy - 9, 'N', 18, col, col);
  (void)s;
}

void iconBlocks(Canvas& c, int cx, int cy, int s, uint16_t col) {
  int b = s / 5;
  int x0 = cx - b * 3 / 2, y0 = cy - b;
  for (int i = 0; i < 3; i++) c.fillRoundRect(x0 + i * b, y0, b - 1, b - 1, 2, col);
  c.fillRoundRect(x0 + b, y0 + b, b - 1, b - 1, 2, col);
}

void icon2048(Canvas& c, int cx, int cy, int s, uint16_t col) {
  c.fillRoundRect(cx - s * 2 / 5, cy - s * 2 / 5, s * 4 / 5, s * 4 / 5, 5, col);
  c.drawTextCentered(font_small, cx, cy - font_small.lineHeight / 2, "2048", BG_TOP);
}

void iconBreakout(Canvas& c, int cx, int cy, int s, uint16_t col) {
  int bw = s / 5, bh = s / 9 + 1;
  for (int r = 0; r < 2; r++)
    for (int i = 0; i < 4; i++)
      c.fillRect(cx - bw * 2 + i * bw + 1, cy - s / 3 + r * (bh + 1), bw - 1, bh, col);
  c.fillCircle(cx + 2, cy + s / 8, s / 14.f + 1, col);
  c.fillRoundRect(cx - s / 5, cy + s / 3, s * 2 / 5, 3, 1, col);
}

void iconFlappy(Canvas& c, int cx, int cy, int s, uint16_t col) {
  c.fillRect(cx + s / 4, cy - s / 2, s / 6, s / 3, col);
  c.fillRect(cx + s / 4, cy + s / 5, s / 6, s / 3, col);
  drawGhost(c, cx - s / 8, cy, s * 0.22f, col, BG_TOP, 0);
}

void iconTicTacToe(Canvas& c, int cx, int cy, int s, uint16_t col) {
  int g = s / 3;
  for (int i = 1; i < 3; i++) {
    c.fillRect(cx - s / 2 + i * g, cy - s / 2, 2, s, col);
    c.fillRect(cx - s / 2, cy - s / 2 + i * g, s, 2, col);
  }
  drawX(c, cx - g, cy - g, g * 0.32f, col);
  drawO(c, cx, cy, g * 0.32f, col);
  drawX(c, cx + g, cy + g, g * 0.32f, col);
}

void iconMines(Canvas& c, int cx, int cy, int s, uint16_t col) {
  float r = s * 0.2f;
  for (int i = 0; i < 8; i++) {
    float a = i * 0.785f;
    c.fillCircle(cx + cosf(a) * r * 1.45f, cy + sinf(a) * r * 1.45f, r * 0.28f, col);
  }
  c.fillCircle(cx, cy, r, col);
  c.fillCircle(cx - r * 0.35f, cy - r * 0.35f, r * 0.25f, BG_TOP);
}

void (*const kIcons[COUNT])(Canvas&, int, int, int, uint16_t) = {
    iconSnake, iconChess, iconBlocks, icon2048, iconBreakout, iconFlappy, iconTicTacToe, iconMines};

void drawMenu(Canvas& c, uint8_t L, uint32_t now) {
  const int W = c.width(), H = c.height();
  const bool small = W < 300;
  c.drawText(small ? font_body : font_title, 10, small ? 3 : 2, tr(L, S_GAMES), INK);
  c.hLine(8, small ? 21 : 25, W - 16, INK, 40);

  const int cols = 4, rows = 2;
  const int top = small ? 26 : 32;
  const int tileW = (W - 16) / cols, tileH = small ? 38 : 50;
  for (int i = 0; i < COUNT; i++) {
    int col = i % cols, row = i / cols;
    int x = 8 + col * tileW, y = top + row * (tileH + 4);
    bool sel = i == g_sel;
    if (sel) {
      float pulse = 0.5f + 0.5f * sinf(now / 260.f);
      c.fillRoundRect(x + 1, y, tileW - 2, tileH, 10, INK, 240);
      c.fillRoundRect(x - 1, y - 2, tileW + 2, tileH + 4, 11, GLOW, (uint8_t)(40 + 50 * pulse));
      c.fillRoundRect(x + 1, y, tileW - 2, tileH, 10, INK, 240);
    } else {
      c.fillRoundRect(x + 1, y, tileW - 2, tileH, 10, SURFACE, 120);
    }
    kIcons[i](c, x + tileW / 2, y + tileH / 2, small ? 32 : 44, sel ? BG_TOP : INK);
  }

  // name, best score and controls of the selected game
  int y = top + rows * (tileH + 4) + (small ? 0 : 2);
  Game* gm = game(g_sel);
  char line[96];
  if (gm->hasScore() && g_best[g_sel] > 0)
    snprintf(line, sizeof(line), "%s  \xC2\xB7  %s %d", tr(L, gm->name()), tr(L, S_BEST),
             g_best[g_sel]);
  else
    snprintf(line, sizeof(line), "%s", tr(L, gm->name()));
  c.drawTextCentered(font_body, W / 2, y, line, INK);
  if (y + font_body.lineHeight + font_small.lineHeight <= H)
    c.drawTextCentered(font_small, W / 2, y + font_body.lineHeight, tr(L, S_GAMES_HINT),
                       INK_SOFT);
}

// ------------------------------------------------------------ overlays
void overlayCard(Canvas& c, int& x, int& y, int& w, int& h, int lines) {
  w = c.width() < 300 ? 190 : 230;
  h = 16 + lines * 20;
  x = (c.width() - w) / 2;
  y = (c.height() - h) / 2;
  c.blendRect(0, 0, c.width(), c.height(), BG_BOTTOM, 120);
  c.fillRoundRect(x, y, w, h, 14, SURFACE);
}

void drawPause(Canvas& c, uint8_t L) {
  int x, y, w, h;
  overlayCard(c, x, y, w, h, 4);
  c.drawTextCentered(font_title, x + w / 2, y + 6, tr(L, S_PAUSED), INK);
  const Str opts[3] = {S_RESUME, S_RESTART, S_QUIT};
  for (int i = 0; i < 3; i++) {
    int ry = y + 30 + i * 20;
    if (i == g_pauseSel) c.fillRoundRect(x + 14, ry - 1, w - 28, 19, 9, INK, 235);
    c.drawTextCentered(font_body, x + w / 2, ry, tr(L, opts[i]), i == g_pauseSel ? USER_TEXT : INK);
  }
}

void drawOver(Canvas& c, uint8_t L, uint32_t now) {
  Game* gm = game(g_active);
  int x, y, w, h;
  overlayCard(c, x, y, w, h, gm->hasScore() ? 4 : 3);
  c.drawTextCentered(font_title, x + w / 2, y + 6, tr(L, gm->result), gm->won ? INK : ERR_RED);
  int ly = y + 32;
  char buf[64];
  if (gm->hasScore()) {
    snprintf(buf, sizeof(buf), "%s: %d   %s: %d", tr(L, S_SCORE), gm->score, tr(L, S_BEST),
             g_best[g_active]);
    c.drawTextCentered(font_body, x + w / 2, ly, buf, INK);
    ly += 20;
    if (g_newRecord && (now / 400) % 2 == 0)
      c.drawTextCentered(font_body, x + w / 2, ly, tr(L, S_NEW_RECORD), INK_SOFT);
    ly += 20;
  } else {
    ly += 10;
  }
  c.drawTextCentered(font_small, x + w / 2, ly, tr(L, S_PLAY_AGAIN), INK_SOFT);
}

void drawReady(Canvas& c, uint8_t L) {
  Game* gm = game(g_active);
  int x, y, w, h;
  overlayCard(c, x, y, w, h, 4);
  c.drawTextCentered(font_title, x + w / 2, y + 6, tr(L, gm->name()), INK);
  static Line lines[3];
  const char* hint = tr(L, gm->hint());
  int n = wrapText(font_small, hint, w - 20, lines, 3);
  for (int i = 0; i < n; i++) {
    int lw = textWidth(font_small, hint + lines[i].start, lines[i].len);
    c.drawText(font_small, x + (w - lw) / 2, y + 32 + i * font_small.lineHeight,
               hint + lines[i].start, INK_SOFT, 255, lines[i].len);
  }
  c.fillRoundRect(x + 20, y + h - 26, w - 40, 19, 9, INK, 235);
  c.drawTextCentered(font_body, x + w / 2, y + h - 26, tr(L, S_PRESS_START), USER_TEXT);
}

}  // namespace

// ------------------------------------------------------------ palette & helpers
Palette palette() {
  Palette p;
  p.field = INK;
  p.fg = BG_TOP;
  p.soft = USER_TEXT;
  p.text = USER_TEXT;
  p.danger = ERR_RED;
  return p;
}

uint8_t lang() { return g_lang; }

Area playArea() { return Area{4, 16, geist::SCREEN_W - 8, geist::SCREEN_H - 19}; }

void drawPanel(Canvas& c, const Area& a) { c.fillRoundRect(a.x, a.y, a.w, a.h, 8, INK, 238); }

void drawCenteredText(Canvas& c, const Font& f, const Area& a, int y, const char* s, uint16_t col,
                      uint8_t alpha) {
  c.drawTextCentered(f, a.x + a.w / 2, y, s, col, alpha);
}

// ------------------------------------------------------------ public
void begin(uint32_t seed) { rng().seed(seed); }

void setBestScore(int g, int value) {
  if (g >= 0 && g < COUNT) g_best[g] = value;
}
int bestScore(int g) { return g >= 0 && g < COUNT ? g_best[g] : 0; }

void openMenu() {
  g_screen = Screen::Menu;
  g_active = -1;
}

Screen screen() { return g_screen; }
Game* current() { return g_active >= 0 ? game(g_active) : nullptr; }

void startGame(int index, uint32_t now) {
  g_active = index;
  g_sel = index;
  game(index)->reset(now);
  g_ready = true;
  g_screen = Screen::Playing;
}

bool key(Key k, uint32_t now) {
  switch (g_screen) {
    case Screen::Menu: {
      int d = 0;
      if (k == Key::RotR || k == Key::Right) d = 1;
      if (k == Key::RotL || k == Key::Left) d = -1;
      if (k == Key::Down) d = 4;
      if (k == Key::Up) d = -4;
      if (d) g_sel = (g_sel + d + COUNT) % COUNT;
      if (k == Key::Press) startGame(g_sel, now);
      return true;
    }
    case Screen::Playing: {
      Game* gm = game(g_active);
      if (g_ready) {
        if (k == Key::Press) {
          g_ready = false;
          gm->reset(now);
        }
        return true;
      }
      gm->key(k, now);
      if (gm->wantPause) {
        gm->wantPause = false;
        g_pauseSel = 0;
        g_screen = Screen::Paused;
        return true;
      }
      if (gm->over) {
        finishGame();
        g_screen = Screen::Over;
      }
      return true;
    }
    case Screen::Paused:
      if (k == Key::RotR || k == Key::Down || k == Key::Right) g_pauseSel = (g_pauseSel + 1) % 3;
      if (k == Key::RotL || k == Key::Up || k == Key::Left) g_pauseSel = (g_pauseSel + 2) % 3;
      if (k == Key::Press) {
        if (g_pauseSel == 0) {
          g_screen = Screen::Playing;
        } else if (g_pauseSel == 1) {
          startGame(g_active, now);
        } else {
          openMenu();
        }
      }
      return true;
    case Screen::Over:
      if (k == Key::Press) startGame(g_active, now);
      return true;
  }
  return true;
}

bool backPressed(uint32_t now) {
  switch (g_screen) {
    case Screen::Menu:
      return false;  // leave the games
    case Screen::Playing:
      if (g_ready) {
        openMenu();
      } else if (!game(g_active)->back()) {
        g_pauseSel = 0;
        g_screen = Screen::Paused;
      }
      return true;
    case Screen::Paused:
      g_screen = Screen::Playing;
      return true;
    case Screen::Over:
      openMenu();
      return true;
  }
  (void)now;
  return true;
}

void tick(uint32_t now) {
  if (g_screen != Screen::Playing || g_ready || g_active < 0) return;
  Game* gm = game(g_active);
  gm->tick(now);
  if (gm->over) {
    finishGame();
    g_screen = Screen::Over;
  }
}

void draw(Canvas& c, uint8_t L, uint32_t now) {
  g_lang = L;
  geist::drawThemeBackground(c);
  if (g_screen == Screen::Menu) {
    drawMenu(c, L, now);
    return;
  }
  Game* gm = game(g_active);
  const int W = c.width();
  // header: name left, score right
  c.drawText(font_small, 8, 2, tr(L, gm->name()), INK);
  if (gm->hasScore()) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%d  \xC2\xB7  %s %d", gm->score, tr(L, S_BEST), g_best[g_active]);
    c.drawTextRight(font_small, W - 8, 2, buf, INK);
  }
  Area a = playArea();
  gm->draw(c, a, now);
  if (g_ready) drawReady(c, L);
  else if (g_screen == Screen::Paused) drawPause(c, L);
  else if (g_screen == Screen::Over) drawOver(c, L, now);
}

}  // namespace games
