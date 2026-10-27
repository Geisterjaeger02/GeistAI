// Checks the games without a device: chess move generation (perft) and
// that every game survives a few thousand random inputs.
//   built and run by tools/sim/render.py
#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include "games/games.h"
#include "ui.h"

namespace games {
long chessPerft(const char* fen, int depth);
int chessSelfPlay(int maxPlies, uint32_t nodes);
}

using namespace geist;

int main() {
  int fails = 0;
  struct Case { const char* fen; int depth; long want; };
  const Case cases[] = {
      {nullptr, 1, 20}, {nullptr, 2, 400}, {nullptr, 3, 8902}, {nullptr, 4, 197281},
      // "Kiwipete": castling, en passant, promotions, pins
      {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 1, 48},
      {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 2, 2039},
      {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 3, 97862},
      // position 3 (en passant + checks)
      {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", 4, 43238},
      // position 4 (promotions, castling through check)
      {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -", 3, 9467},
  };
  for (const Case& c : cases) {
    long got = games::chessPerft(c.fen, c.depth);
    printf("perft %d %-12s %ld %s\n", c.depth, c.fen ? "(position)" : "(start)", got,
           got == c.want ? "ok" : "WRONG");
    if (got != c.want) fails++;
  }
  printf("self-play: %d plies\n", games::chessSelfPlay(60, 3000));

  // random input soak test for every game, both screen sizes
  std::vector<uint16_t> fb(320 * 170);
  for (int size = 0; size < 2; size++) {
    setScreenSize(size ? 240 : 320, size ? 135 : 170);
    Canvas c(fb.data(), SCREEN_W, SCREEN_H);
    for (int g = 0; g < games::COUNT; g++) {
      uint32_t now = 1000;
      games::begin(1234 + g);
      games::startGame(g, now);
      games::key(games::Key::Press, now);  // leave the "press to start" card
      int restarts = 0;
      for (int i = 0; i < 4000; i++) {
        now += 16;
        games::Key k = (games::Key)(rand() % 9);
        games::key(k, now);
        games::tick(now);
        if (games::screen() == games::Screen::Over) {
          restarts++;
          games::key(games::Key::Press, now);
          games::key(games::Key::Press, now);
        }
        if (games::screen() == games::Screen::Paused) games::backPressed(now);
        if (i % 50 == 0) games::draw(c, 1, now);
      }
      printf("game %d (%dx%d): ok, %d rounds\n", g, SCREEN_W, SCREEN_H, restarts);
    }
  }
  printf(fails ? "FAILED\n" : "all ok\n");
  return fails ? 1 : 0;
}
