// GeistAI - screen layouts. Pure rendering: takes a snapshot of the app state
// (UiModel) and paints it into a Canvas. No hardware access here.
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

#include "gfx.h"

namespace geist {

// ---------------------------------------------------------------- theme
// Colour themes, chosen in the settings. "Neon cyan" is the original look.
struct ThemeColors {
  uint16_t bgTop, bgBottom;  // background gradient
  uint16_t ink, inkSoft;     // text, secondary text (also the user's bubble)
  uint16_t userText;         // text inside the user's bubble
  uint16_t blob;             // soft light in the background
  uint8_t ledR, ledG, ledB;  // LED colour
  uint16_t surface;          // chat bubbles, text field, keys (white on light themes)
  uint16_t glow;             // logo glow and light effects
};
constexpr int THEME_COUNT = 13;
extern const ThemeColors kThemes[THEME_COUNT];
void setTheme(int index);  // also redraws the cached background
int currentTheme();

namespace theme {
extern uint16_t BG_TOP;
extern uint16_t BG_BOTTOM;
extern uint16_t INK;
extern uint16_t INK_SOFT;
extern uint16_t USER_TEXT;
extern uint16_t BLOB;
extern uint16_t SURFACE;
extern uint16_t GLOW;
constexpr uint16_t SNOW = rgb(255, 255, 255);
constexpr uint16_t ERR_RED = rgb(196, 0, 70);
}  // namespace theme

// Screen size in landscape. 320x170 (T-Embed) by default; 240x135 on the
// M5Stack Cardputer / StickC Plus2. Call setScreenSize() once at startup.
extern int SCREEN_W;
extern int SCREEN_H;
void setScreenSize(int w, int h);

enum class Mode : uint8_t {
  Boot, Idle, Listening, Thinking, Speaking, Error, Settings, Off, Keyboard, Setup, Games, Voices
};

struct ChatMsg {
  bool fromUser;
  std::string text;
};

struct SettingsRow {
  std::string label;
  std::string value;  // empty for action rows
  bool section = false;  // a small heading between rows (not selectable)
  bool current = false;  // shows a check mark (e.g. the voice in use)
};

struct UiModel {
  Mode mode = Mode::Boot;
  uint32_t timeMs = 0;      // animation clock
  uint32_t modeSinceMs = 0; // when the current mode started
  uint8_t uiLang = 0;
  uint16_t hintTalk = 0;  // Str ids of the two idle hints (board specific)
  uint16_t hintMenu = 1;

  // boot
  float bootProgress = 0.f;  // 0..1
  std::string bootStatus;
  std::string version;

  // status bar
  bool wifiOk = false;
  bool serverOk = false;
  std::string langBadge = "AUTO";
  int batteryPct = -1;

  // conversation
  std::vector<ChatMsg> chat;
  int scroll = 0;            // pixels scrolled up from the newest message
  bool pendingReply = false; // show the "typing" bubble
  bool tapMode = false;      // listening was started with a click

  // audio meters 0..1
  float micLevel = 0.f;
  float speakLevel = 0.f;

  // errors
  std::string errorText;

  // settings
  std::vector<SettingsRow> settings;
  int settingsIndex = 0;
  bool settingsEditing = false;

  // on-screen keyboard
  std::string kbText;
  int kbIndex = 0;
  int kbPage = 0;
  bool kbShift = false;
  bool kbPhysical = false;  // device has a real keyboard (Cardputer): no on-screen keys
  bool kbNameMode = false;  // editing the assistant's name instead of a message

  // voice menu (rows like the settings)
  std::vector<SettingsRow> voiceRows;
  int voiceIndex = 0;
  bool voiceEditing = false;
  int voicePlaying = -1;     // row being previewed
  std::string voiceStatus;   // "Loading…", "Saved!" ...

  // Wi-Fi setup hotspot
  std::string setupSsid;
  std::string setupPass;
  std::string setupUrl;
  bool setupSaved = false;

  // the assistant's name (settings), e.g. in the welcome text
  std::string assistantName = "GeistAI";

  // power button: 0 = not held, 0..1 = progress towards switching off
  float powerHold = 0.f;
};

// The three UI fonts (body 14 px, small 11 px, title 18 px).
const Font& fontBody();
const Font& fontSmall();
const Font& fontTitle();

// The theme background (gradient and soft lights), e.g. behind the games.
void drawThemeBackground(Canvas& c);

// Renders the whole frame.
void render(Canvas& c, const UiModel& m);

// Total height of the chat content in pixels, used to clamp scrolling.
int chatContentHeight(const UiModel& m);
int chatViewportHeight();

}  // namespace geist
