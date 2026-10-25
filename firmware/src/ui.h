// GeistAI - screen layouts. Pure rendering: takes a snapshot of the app state
// (UiModel) and paints it into a Canvas. No hardware access here.
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

#include "gfx.h"

namespace geist {

// ---------------------------------------------------------------- theme
namespace theme {
constexpr uint16_t BG_TOP = rgb(40, 246, 255);    // neon cyan
constexpr uint16_t BG_BOTTOM = rgb(0, 206, 240);  // slightly deeper cyan
constexpr uint16_t INK = rgb(0, 24, 36);          // deep navy text
constexpr uint16_t INK_SOFT = rgb(0, 86, 108);    // secondary text
constexpr uint16_t SNOW = rgb(255, 255, 255);
constexpr uint16_t USER_TEXT = rgb(150, 250, 255);
constexpr uint16_t ERR_RED = rgb(196, 0, 70);
}  // namespace theme

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 170;

enum class Mode : uint8_t {
  Boot, Idle, Listening, Thinking, Speaking, Error, Settings, Off, Keyboard
};

struct ChatMsg {
  bool fromUser;
  std::string text;
};

struct SettingsRow {
  std::string label;
  std::string value;  // empty for action rows
};

struct UiModel {
  Mode mode = Mode::Boot;
  uint32_t timeMs = 0;      // animation clock
  uint32_t modeSinceMs = 0; // when the current mode started
  uint8_t uiLang = 0;

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

  // power button: 0 = not held, 0..1 = progress towards switching off
  float powerHold = 0.f;
};

// Renders the whole frame.
void render(Canvas& c, const UiModel& m);

// Total height of the chat content in pixels, used to clamp scrolling.
int chatContentHeight(const UiModel& m);
int chatViewportHeight();

}  // namespace geist
