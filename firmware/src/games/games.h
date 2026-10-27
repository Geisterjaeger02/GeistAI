// GeistAI games - the games menu, pause menu and game-over screen.
// main.cpp feeds input and calls draw(); scores are saved through callbacks.
#pragma once
#include <stdint.h>

#include "../gfx.h"
#include "../ui.h"
#include "game.h"

namespace games {

constexpr int COUNT = 8;

// Where the games live in the interface.
enum class Screen : uint8_t { Menu, Playing, Paused, Over };

void begin(uint32_t seed);
// Best scores are kept by main.cpp in flash.
void setBestScore(int game, int value);
int bestScore(int game);
// Called when a game ends with a new best score.
extern void (*onNewBest)(int game, int value);

void openMenu();
Screen screen();
// Input. Returns false when the player leaves the games (back in the menu).
bool key(Key k, uint32_t now);
bool backPressed(uint32_t now);
void tick(uint32_t now);
void draw(Canvas& c, uint8_t uiLang, uint32_t now);

// For the simulator: start a game directly and feed it keys.
void startGame(int index, uint32_t now);
Game* current();

}  // namespace games
