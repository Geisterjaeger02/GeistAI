// Desktop preview of the GeistAI screens. Renders each UI state with the exact
// firmware rendering code and writes PPM images (convert with render.py).
//
//   python3 tools/sim/render.py   (builds with g++ and writes tools/sim/out/screens.png)
#include <stdio.h>

#include <string>
#include <vector>

#include "gfx.h"
#include "i18n.h"
#include "ui.h"
#include "games/games.h"

using namespace geist;

static void savePPM(const std::string& path, const std::vector<uint16_t>& fb) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (uint16_t p : fb) {
    uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
    uint8_t rgb[3] = {(uint8_t)(r << 3 | r >> 2), (uint8_t)(g << 2 | g >> 4),
                      (uint8_t)(b << 3 | b >> 2)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static void renderAll(const std::string& out) {
  std::vector<uint16_t> fb(SCREEN_W * SCREEN_H);
  Canvas c(fb.data(), SCREEN_W, SCREEN_H);

  UiModel m;
  m.uiLang = UI_DE;
  m.version = "v0.1.0";
  m.wifiOk = true;
  m.serverOk = true;
  m.langBadge = "AUTO";

  auto shot = [&](const char* name) {
    render(c, m);
    savePPM(out + name + ".ppm", fb);
  };

  // Boot sequence frames
  m.mode = Mode::Boot;
  m.modeSinceMs = 0;
  m.bootStatus = tr(m.uiLang, S_CONNECTING_WIFI);
  m.timeMs = 700; m.bootProgress = 0.25f; shot("01_boot_writing");
  m.timeMs = 2300; m.bootProgress = 0.7f; m.bootStatus = tr(m.uiLang, S_CONNECTING_SERVER);
  shot("02_boot_done");

  // Idle, empty chat
  m.mode = Mode::Idle;
  m.timeMs = 1000;
  shot("03_idle_welcome");

  // Listening
  m.mode = Mode::Listening;
  m.modeSinceMs = 0; m.timeMs = 900; m.micLevel = 0.6f;
  shot("04_listening");

  // Thinking with conversation
  m.chat.push_back({true, "Wie wird das Wetter morgen in Bordesholm?"});
  m.mode = Mode::Thinking; m.pendingReply = true; m.timeMs = 1300;
  shot("05_thinking");

  // Speaking
  m.pendingReply = false;
  m.chat.push_back({false,
                    "Ich habe keinen Live-Zugriff aufs Wetter, aber du kannst mich fragen, "
                    "sobald der Wetter-Plugin aktiv ist. Soll ich dir stattdessen einen Witz "
                    "erzählen?"});
  m.mode = Mode::Speaking; m.speakLevel = 0.7f; m.timeMs = 2100;
  shot("06_speaking");

  // Spanish exchange + idle
  m.chat.push_back({true, "¿Cuál es la capital de Islandia?"});
  m.chat.push_back({false, "La capital de Islandia es Reikiavik. ¡Un lugar precioso!"});
  m.mode = Mode::Idle; m.langBadge = "ES"; m.timeMs = 5000;
  shot("07_idle_chat");

  // Error
  m.mode = Mode::Error; m.errorText = std::string(tr(m.uiLang, S_ERR_SERVER)) + " \xC2\xB7 192.168.178.42:8765 \xC2\xB7 connection refused";
  shot("08_error");

  // Settings
  m.mode = Mode::Settings;
  m.settings = {
      {tr(m.uiLang, S_SPEECH_LANG), "Auto"},
      {tr(m.uiLang, S_UI_LANG), "Deutsch"},
      {tr(m.uiLang, S_VOLUME), "70 %"},
      {tr(m.uiLang, S_VOICE_REPLY), tr(m.uiLang, S_ON)},
      {tr(m.uiLang, S_COLOR), kThemeNames[m.uiLang][0]},
      {tr(m.uiLang, S_AI_NAME), "Luna"},
      {tr(m.uiLang, S_AI_BRAIN), "ChatGPT"},
      {tr(m.uiLang, S_CLEAR_CHAT), ""},
      {tr(m.uiLang, S_DEVICE_INFO), ""},
      {tr(m.uiLang, S_BACK), ""},
  };
  m.settingsIndex = 2; m.settingsEditing = true;
  shot("09_settings");

  // Power button held
  m.mode = Mode::Idle; m.settingsEditing = false; m.powerHold = 0.6f;
  shot("10_power_hold");
  m.powerHold = 0.f; m.mode = Mode::Off;
  shot("11_goodbye");

  // Keyboard
  m.mode = Mode::Keyboard; m.kbText = "Wie spät ist es in Madrid?"; m.kbIndex = 18; m.timeMs = 100;
  shot("12_keyboard");
  m.kbText = ""; m.kbPage = 2; m.kbShift = true; m.kbIndex = 39;
  shot("13_keyboard_accents");

  // Cardputer: physical keyboard compose screen
  m.kbPhysical = true; m.kbPage = 0; m.kbShift = false; m.kbText = "Hola, ¿qué tal?";
  shot("14_keyboard_physical");

  // More languages: Cyrillic, Greek, Vietnamese and a transliterated answer
  m.kbPhysical = false; m.kbText = "";
  m.mode = Mode::Idle; m.timeMs = 5000; m.langBadge = "RU";
  m.chat.clear();
  m.chat.push_back({true, "Какая погода завтра в Москве?"});
  m.chat.push_back({false, "Завтра в Москве от 3 до 8 °C, облачно и небольшой дождь."});
  m.chat.push_back({true, "Γεια σου! Xin chào, bạn khỏe không?"});
  m.chat.push_back({false, "ni hao, wo shi GeistAI. jin tian tian qi hen hao!"});
  shot("15_languages");

  // Colour themes and a renamed assistant
  m.chat.clear(); m.langBadge = "AUTO"; m.assistantName = "Luna";
  for (int t = 1; t < 6; t++) {
    setTheme(t);
    m.mode = Mode::Idle; m.timeMs = 1000;
    if (t == 1) {
      shot("15b_theme_pink_welcome");
      continue;
    }
    m.chat = {{true, "Wie heißt du?"}, {false, "Ich bin Luna, deine Assistentin. Frag mich was!"}};
    char name[40];
    snprintf(name, sizeof(name), "15%c_theme_%d", 'b' + t, t);
    shot(name);
    m.chat.clear();
  }
  setTheme(0);
  m.mode = Mode::Keyboard; m.kbNameMode = true; m.kbText = "Luna"; m.kbIndex = 3;
  shot("15h_name_keyboard");
  m.kbNameMode = false; m.kbText = ""; m.assistantName = "GeistAI";
  m.mode = Mode::Keyboard; m.kbPage = 3; m.kbText = "Привет"; m.kbIndex = 5;
  shot("15i_keyboard_cyrillic");
  m.kbPage = 0; m.kbText = "";

  // Dark themes
  m.chat = {{true, "Wie wird das Wetter morgen?"}, {false, "Morgen 8 bis 11 Grad mit Schauern, nimm einen Schirm mit."}};
  m.mode = Mode::Idle; m.timeMs = 1000; m.assistantName = "GeistAI";
  for (int t = 10; t < THEME_COUNT; t++) {
    setTheme(t);
    char name[40];
    snprintf(name, sizeof(name), "16%c_dark_%d", 'a' + (t - 10), t);
    shot(name);
  }
  setTheme(0);
  m.chat.clear();

  // Russian menu
  m.uiLang = UI_RU; m.mode = Mode::Settings; m.settingsIndex = 1; m.settingsEditing = false;
  m.settings = {{tr(UI_RU, S_GAMES), "\xE2\x80\xBA"}, {tr(UI_RU, S_SPEECH_LANG), "Авто"},
                {tr(UI_RU, S_UI_LANG), "Русский"}, {tr(UI_RU, S_VOLUME), "70 %"},
                {tr(UI_RU, S_COLOR), kThemeNames[UI_RU][0]}};
  shot("16e_settings_ru");
  m.uiLang = UI_DE;

  // Games: menu and every game after a little play
  m.mode = Mode::Games;
  games::begin(7);
  games::setBestScore(0, 23);
  games::openMenu();
  games::key(games::Key::RotR, 0);
  m.timeMs = 500;
  shot("17a_games_menu");
  const games::Key script[][6] = {
      {games::Key::RotR, games::Key::RotR, games::Key::RotL, games::Key::RotL, games::Key::RotR, games::Key::RotL},
      {games::Key::Press, games::Key::RotR, games::Key::Press, games::Key::RotR, games::Key::RotR, games::Key::RotR},
      {games::Key::RotL, games::Key::Press, games::Key::RotR, games::Key::RotR, games::Key::Press, games::Key::LongPress},
      {games::Key::Left, games::Key::Up, games::Key::Right, games::Key::Down, games::Key::Left, games::Key::Up},
      {games::Key::PressDown, games::Key::RotR, games::Key::RotR, games::Key::RotL, games::Key::RotR, games::Key::RotR},
      {games::Key::PressDown, games::Key::PressDown, games::Key::PressDown, games::Key::PressDown, games::Key::PressDown, games::Key::PressDown},
      {games::Key::Press, games::Key::RotR, games::Key::RotR, games::Key::Press, games::Key::RotR, games::Key::Press},
      {games::Key::Press, games::Key::RotR, games::Key::RotR, games::Key::RotR, games::Key::LongPress, games::Key::RotR},
  };
  for (int g = 0; g < games::COUNT; g++) {
    uint32_t now = 1000;
    games::startGame(g, now);
    m.timeMs = now;
    char name[40];
    if (g == 0) {
      snprintf(name, sizeof(name), "17b_game%d_start", g);
      shot(name);
    }
    games::key(games::Key::Press, now);
    for (int step = 0; step < 6; step++) {
      for (int t = 0; t < 40; t++) {
        now += 16;
        games::tick(now);
        if (g == 1 || g == 6) {  // let GeistAI answer
          now += 400;
          games::tick(now);
        }
        if (g == 5 && t % 18 == 0) games::key(games::Key::PressDown, now);
      }
      games::key(script[g][step], now);
      if (g == 1) {  // chess: a few real moves
        now += 500;
        games::tick(now);
      }
    }
    m.timeMs = now;
    snprintf(name, sizeof(name), "17c_game%d", g);
    shot(name);
  }

  // Wi-Fi setup hotspot
  m.kbPhysical = false; m.kbText = "";
  m.mode = Mode::Setup;
  m.setupSsid = "GeistAI-3F2A"; m.setupPass = "48210375"; m.setupUrl = "192.168.4.1";
  shot("16_wifi_setup");
  m.setupSaved = true;
  shot("17_wifi_saved");
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : "./";
  setScreenSize(320, 170);
  renderAll(out + "tembed_");
  setScreenSize(240, 135);
  renderAll(out + "m5_");
  printf("ok\n");
  return 0;
}
