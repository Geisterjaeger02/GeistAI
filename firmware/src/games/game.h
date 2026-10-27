// GeistAI games - shared pieces. Pure logic + drawing (no hardware), so the
// games also run in the desktop simulator.
#pragma once
#include <stdint.h>

#include "../gfx.h"
#include "../i18n.h"
#include "../ui.h"

namespace games {

using geist::Canvas;

// Everything a game can receive. Rotating the knob gives RotL/RotR; the
// Cardputer arrow keys (; . , /  or W A S D) give Up/Down/Left/Right.
enum class Key : uint8_t {
  RotL, RotR,           // knob counter-clockwise / clockwise (StickC: B)
  Up, Down, Left, Right,
  PressDown,            // button pressed (fires immediately)
  Press,                // short press, on release
  LongPress,            // held >= LONG_PRESS_MS, on release (Cardputer: F key)
};
constexpr uint32_t LONG_PRESS_MS = 400;

// Small, deterministic random numbers (same on device and simulator).
struct Rng {
  uint32_t s = 2463534242u;
  void seed(uint32_t v) { s = v ? v : 2463534242u; }
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int range(int n) { return n > 0 ? (int)(next() % (uint32_t)n) : 0; }
};

// The play field. Games draw inside it; the frame shows name and score.
struct Area {
  int x, y, w, h;
};

class Game {
 public:
  virtual ~Game() {}
  virtual geist::Str name() const = 0;
  virtual geist::Str hint() const = 0;
  virtual void reset(uint32_t now) = 0;
  virtual void key(Key k, uint32_t now) = 0;
  virtual void tick(uint32_t now) = 0;
  virtual void draw(Canvas& c, const Area& a, uint32_t now) = 0;
  // The knob's back button is used by the game itself (e.g. chess: undo a
  // selection). Return true if it was consumed, false to open the pause menu.
  virtual bool back() { return false; }
  // Games without a score (chess, tic-tac-toe) show a result text instead.
  virtual bool hasScore() const { return true; }
  virtual bool lowerIsBetter() const { return false; }  // e.g. minesweeper time

  int score = 0;
  bool over = false;
  geist::Str result = geist::S_GAME_OVER;  // shown when over
  bool won = false;
  bool wantPause = false;  // the game asks for the pause menu (e.g. snake: press)
};

// Colours taken from the current theme so every game matches the interface.
struct Palette {
  uint16_t field;   // play field (dark on light themes, light on dark ones)
  uint16_t fg;      // main game colour on the field
  uint16_t soft;    // secondary colour on the field
  uint16_t text;    // text on the field
  uint16_t danger;  // food, bombs ...
};
Palette palette();

// Menu language (for texts inside games).
uint8_t lang();

// The play field for the current screen size (same as passed to draw()).
Area playArea();

// Shared helpers
void drawPanel(Canvas& c, const Area& a);
void drawCenteredText(Canvas& c, const geist::Font& f, const Area& a, int y, const char* s,
                      uint16_t col, uint8_t alpha = 255);

// Factories (one per game, defined in the game's .cpp)
Game* makeSnake();
Game* makeChess();
Game* makeBlocks();
Game* make2048();
Game* makeBreakout();
Game* makeFlappy();
Game* makeTicTacToe();
Game* makeMines();

// Chess piece drawing (chess.cpp): piece = one of "KQRBNP", size 18 or 14.
void drawChessPiece(Canvas& c, int x, int y, char piece, int size, uint16_t fill, uint16_t line);

}  // namespace games
