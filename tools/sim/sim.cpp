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
