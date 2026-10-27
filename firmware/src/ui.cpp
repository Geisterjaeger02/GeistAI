#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/fonts.h"
#include "assets/icons.h"
#include "assets/logo.h"
#include "i18n.h"
#include "keyboard.h"
#include "qrcode.h"
#include "games/games.h"

namespace geist {

using namespace theme;

namespace {

constexpr int TOPBAR_H = 26;
constexpr int VIEW_Y = 28;
int VIEW_H = 118;             // depends on the screen size, see setScreenSize()
int BOTTOM_Y = 148;
constexpr int BUBBLE_PAD_X = 8;
constexpr int BUBBLE_PAD_Y = 4;
constexpr int BUBBLE_GAP = 6;
int BUBBLE_MAX_TEXT_W = 232;
bool SMALL = false;           // 240x135 class screens
constexpr int MAX_LINES = 48;

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float easeOutCubic(float t) {
  t = clamp01(t);
  float u = 1 - t;
  return 1 - u * u * u;
}
uint8_t a8(float v) { return (uint8_t)(clamp01(v) * 255.f + 0.5f); }

// ------------------------------------------------------------ background
// The gradient + soft light blobs are pre-rendered once and copied per frame.
uint16_t* g_bgCache = nullptr;
size_t g_bgSize = 0;
bool g_bgFailed = false;

void paintBackground(Canvas& bg, int w, int h) {
  bg.vGradient(0, 0, w, h, BG_TOP, BG_BOTTOM);
  // soft "neon" light blobs
  for (int i = 0; i < 6; i++) {
    bg.fillCircle(w - 35.f, -10.f, 30.f + i * 18.f, GLOW, 14);
    bg.fillCircle(10.f, h + 20.f, 30.f + i * 16.f, BLOB, 10);
  }
  // fine diagonal sheen lines
  for (int x = -h; x < w; x += 22) {
    for (int y = 0; y < h; y++) {
      int px = x + y / 2;
      if (px >= 0 && px < w) bg.plot(px, y, GLOW, 10);
    }
  }
}

// The background is pre-rendered once and copied per frame. Boards without
// spare RAM (no PSRAM) skip the cache and draw a plain gradient instead.
void drawBackground(Canvas& c) {
  size_t n = (size_t)c.width() * c.height();
  if (!g_bgCache && !g_bgFailed) {
    g_bgCache = (uint16_t*)malloc(n * sizeof(uint16_t));
    if (g_bgCache) {
      Canvas bg(g_bgCache, c.width(), c.height());
      paintBackground(bg, c.width(), c.height());
      g_bgSize = n;
    } else {
      g_bgFailed = true;
    }
  }
  if (g_bgCache && g_bgSize == n)
    memcpy(c.data(), g_bgCache, n * sizeof(uint16_t));
  else
    c.vGradient(0, 0, c.width(), c.height(), BG_TOP, BG_BOTTOM);
}

void drawIcon(Canvas& c, int x, int y, const uint8_t* mask, int w, int h, uint16_t col,
              uint8_t op = 255) {
  c.drawMask8(x, y, mask, w, h, col, op);
}

// ------------------------------------------------------------ top bar
void drawTopBar(Canvas& c, const UiModel& m) {
  drawIcon(c, 8, (TOPBAR_H - logo_small_h) / 2 + 1, logo_small_mask, logo_small_w, logo_small_h,
           INK);

  int x = SCREEN_W - 8;
  // wifi
  x -= icon_wifi_w;
  if (m.wifiOk)
    drawIcon(c, x, 6, icon_wifi_mask, icon_wifi_w, icon_wifi_h, INK);
  else
    drawIcon(c, x, 6, icon_wifi_off_mask, icon_wifi_off_w, icon_wifi_off_h, ERR_RED);

  // server dot
  x -= 9;
  c.fillCircle(x + 2.5f, 13.f, 3.f, m.serverOk ? INK : ERR_RED, m.serverOk ? 200 : 255);
  x -= 8;

  // language badge
  int tw = textWidth(font_small, m.langBadge.c_str());
  int bw = tw + 10 + icon_globe_w + 3;
  x -= bw;
  c.fillRoundRect(x, 5, bw, 17, 8, INK, 34);
  drawIcon(c, x + 5, 7, icon_globe_mask, icon_globe_w, icon_globe_h, INK, 220);
  c.drawText(font_small, x + 5 + icon_globe_w + 3, 6, m.langBadge.c_str(), INK);

  if (m.batteryPct >= 0) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d%%", m.batteryPct);
    x -= 6;
    c.drawTextRight(font_small, x, 6, buf, INK_SOFT);
  }

  c.hLine(8, TOPBAR_H, SCREEN_W - 16, INK, 40);
}

// ------------------------------------------------------------ chat
struct BubbleLayout {
  int w, h;
  int lines;
};

BubbleLayout layoutBubble(const ChatMsg& msg, Line* lines) {
  int n = wrapText(font_body, msg.text.c_str(), BUBBLE_MAX_TEXT_W, lines, MAX_LINES);
  if (n == 0) n = 1, lines[0] = Line{0, 0};
  int maxW = 0;
  for (int i = 0; i < n; i++) {
    int w = textWidth(font_body, msg.text.c_str() + lines[i].start, lines[i].len);
    if (w > maxW) maxW = w;
  }
  BubbleLayout b;
  b.w = maxW + BUBBLE_PAD_X * 2;
  if (b.w < 28) b.w = 28;
  b.h = n * font_body.lineHeight + BUBBLE_PAD_Y * 2;
  b.lines = n;
  return b;
}

constexpr int PENDING_H = 26;

void drawTypingBubble(Canvas& c, int x, int y, uint32_t t) {
  c.fillRoundRect(x, y, 50, PENDING_H, 10, SURFACE, 170);
  for (int i = 0; i < 3; i++) {
    float ph = (t / 1000.f) * 6.2832f * 1.3f - i * 0.9f;
    float lift = (sinf(ph) * 0.5f + 0.5f);
    c.fillCircle(x + 14 + i * 11, y + PENDING_H / 2 + 1 - lift * 4, 3.f, INK, 120 + a8(lift) / 2);
  }
}

void drawChat(Canvas& c, const UiModel& m) {
  c.setClip(0, VIEW_Y, SCREEN_W, VIEW_H);
  static Line lines[MAX_LINES];

  int bottom = VIEW_Y + VIEW_H - 3 + m.scroll;
  if (m.pendingReply) {
    int y = bottom - PENDING_H;
    drawTypingBubble(c, 8, y, m.timeMs);
    bottom = y - BUBBLE_GAP;
  }
  for (int i = (int)m.chat.size() - 1; i >= 0 && bottom > VIEW_Y; i--) {
    const ChatMsg& msg = m.chat[i];
    BubbleLayout b = layoutBubble(msg, lines);
    int y = bottom - b.h;
    if (y < VIEW_Y + VIEW_H) {
      int x = msg.fromUser ? SCREEN_W - 8 - b.w : 8;
      if (msg.fromUser) {
        c.fillRoundRect(x, y, b.w, b.h, 10, INK, 235);
      } else {
        c.fillRoundRect(x, y, b.w, b.h, 10, SURFACE, 175);
      }
      uint16_t col = msg.fromUser ? USER_TEXT : INK;
      for (int l = 0; l < b.lines; l++) {
        c.drawText(font_body, x + BUBBLE_PAD_X, y + BUBBLE_PAD_Y + l * font_body.lineHeight,
                   msg.text.c_str() + lines[l].start, col, 255, lines[l].len);
      }
    }
    bottom = y - BUBBLE_GAP;
  }
  c.resetClip();

  // soft fade where messages scroll under the top bar
  for (int i = 0; i < 8; i++) c.blendRect(0, VIEW_Y + i, SCREEN_W, 1, BG_TOP, (uint8_t)(220 - i * 27));

  // scrollbar
  int content = chatContentHeight(m);
  if (content > VIEW_H) {
    float frac = (float)VIEW_H / content;
    int barH = (int)(VIEW_H * frac);
    if (barH < 12) barH = 12;
    float pos = 1.f - (float)m.scroll / (content - VIEW_H);
    int barY = VIEW_Y + (int)((VIEW_H - barH) * clamp01(pos));
    c.fillRoundRect(SCREEN_W - 4, barY, 3, barH, 1, INK, 70);
  }
}

void drawWelcome(Canvas& c, const UiModel& m) {
  int y = VIEW_Y + 8;
  if (!SMALL) {  // on small screens the top bar logo is enough
    int lx = (SCREEN_W - logo_mid_w) / 2;
    int ly = VIEW_Y + 4;
    float pulse = 0.75f + 0.25f * sinf(m.timeMs / 900.f);
    c.drawMask8(lx, ly, logo_mid_glow, logo_mid_w, logo_mid_h, GLOW, a8(0.85f * pulse));
    c.drawMask8(lx, ly, logo_mid_mask, logo_mid_w, logo_mid_h, INK);
    y = ly + logo_mid_h + 2;
  }

  static Line lines[4];
  static char buf[200];
  snprintf(buf, sizeof(buf), tr(m.uiLang, S_WELCOME), m.assistantName.c_str());
  const char* text = buf;
  int n = wrapText(font_body, text, SCREEN_W - 60, lines, 4);
  for (int i = 0; i < n; i++) {
    int w = textWidth(font_body, text + lines[i].start, lines[i].len);
    c.drawText(font_body, (SCREEN_W - w) / 2, y + i * font_body.lineHeight, text + lines[i].start,
               INK_SOFT, 255, lines[i].len);
  }
}

// ------------------------------------------------------------ bottom bar
void drawPill(Canvas& c, int x, int y, int w, uint16_t fill, uint8_t alpha) {
  c.fillRoundRect(x, y, w, 19, 9, fill, alpha);
}

void drawLevelBars(Canvas& c, int x, int cy, int bars, float level, uint32_t t, uint16_t col,
                   bool animated) {
  for (int i = 0; i < bars; i++) {
    float v;
    if (animated) {
      float wob = 0.5f + 0.5f * sinf(t / 110.f + i * 1.7f) * cosf(t / 270.f + i * 0.6f);
      v = clamp01(level * (0.45f + 0.75f * wob));
    } else {
      float center = 1.f - fabsf((i - (bars - 1) / 2.f) / (bars / 2.f));
      v = clamp01(level * (0.5f + 0.7f * center));
    }
    int h = 3 + (int)(v * 12);
    c.fillRoundRect(x + i * 5, cy - h / 2, 3, h, 1, col, 220);
  }
}

void drawBottomBar(Canvas& c, const UiModel& m) {
  int y = BOTTOM_Y + 1;
  switch (m.mode) {
    case Mode::Idle: {
      bool alt = ((m.timeMs / 4000) % 2) == 1;
      const char* hint = tr(m.uiLang, (Str)(alt ? m.hintMenu : m.hintTalk));
      int w = textWidth(font_small, hint) + 14 + icon_mic_w + 4;
      int x = (SCREEN_W - w) / 2;
      drawPill(c, x, y, w, INK, 30);
      if (alt)
        drawIcon(c, x + 7, y + 3, icon_gear_mask, icon_gear_w, icon_gear_h, INK);
      else
        drawIcon(c, x + 7, y + 3, icon_mic_mask, icon_mic_w, icon_mic_h, INK);
      c.drawText(font_small, x + 7 + icon_mic_w + 4, y + 3, hint, INK);
      break;
    }
    case Mode::Listening: {
      const char* hint = tr(m.uiLang, m.tapMode ? S_TAP_TO_SEND : S_RELEASE_TO_SEND);
      int w = textWidth(font_small, hint) + 14 + 12 * 5 + 6;
      int x = (SCREEN_W - w) / 2;
      drawPill(c, x, y, w, INK, 235);
      drawLevelBars(c, x + 8, y + 10, 12, m.micLevel, m.timeMs, USER_TEXT, false);
      c.drawText(font_small, x + 8 + 12 * 5 + 4, y + 3, hint, USER_TEXT);
      break;
    }
    case Mode::Thinking: {
      const char* t = tr(m.uiLang, S_THINKING);
      int w = textWidth(font_small, t) + 14 + 16;
      int x = (SCREEN_W - w) / 2;
      drawPill(c, x, y, w, INK, 30);
      float a0 = (m.timeMs % 1000) / 1000.f * 6.2832f;
      c.arc(x + 13, y + 9.5f, 5.f, 2.2f, a0, a0 + 4.2f, INK);
      c.drawText(font_small, x + 23, y + 3, t, INK);
      break;
    }
    case Mode::Speaking: {
      const char* t = tr(m.uiLang, S_CLICK_TO_STOP);
      int w = textWidth(font_small, t) + 14 + icon_speaker_w + 4 + 6 * 5 + 4;
      int x = (SCREEN_W - w) / 2;
      drawPill(c, x, y, w, INK, 30);
      drawIcon(c, x + 7, y + 3, icon_speaker_mask, icon_speaker_w, icon_speaker_h, INK);
      drawLevelBars(c, x + 7 + icon_speaker_w + 4, y + 10, 6, 0.3f + m.speakLevel * 0.9f,
                    m.timeMs, INK, true);
      c.drawText(font_small, x + 7 + icon_speaker_w + 4 + 6 * 5 + 4, y + 3, t, INK);
      break;
    }
    case Mode::Error: {
      // Wrapped over up to 3 lines so long details (address, reason) stay readable.
      const char* t = m.errorText.empty() ? tr(m.uiLang, S_ERR_GENERIC) : m.errorText.c_str();
      static Line lines[3];
      int n = wrapText(font_small, t, SCREEN_W - 40, lines, 3);
      int maxW = 0;
      for (int i = 0; i < n; i++) {
        int lw = textWidth(font_small, t + lines[i].start, lines[i].len);
        if (lw > maxW) maxW = lw;
      }
      int w = maxW + 20, h = n * font_small.lineHeight + 6;
      int x = (SCREEN_W - w) / 2, top = SCREEN_H - 3 - h;
      c.fillRoundRect(x, top, w, h, 9, ERR_RED, 240);
      for (int i = 0; i < n; i++) {
        int lw = textWidth(font_small, t + lines[i].start, lines[i].len);
        c.drawText(font_small, (SCREEN_W - lw) / 2, top + 3 + i * font_small.lineHeight,
                   t + lines[i].start, SNOW, 255, lines[i].len);
      }
      break;
    }
    default:
      break;
  }
}

// ------------------------------------------------------------ listening overlay
void drawListeningOverlay(Canvas& c, const UiModel& m) {
  // frost the chat behind
  c.blendRect(0, VIEW_Y, SCREEN_W, VIEW_H, BG_TOP, 225);

  float t = (m.timeMs - m.modeSinceMs) / 1000.f;
  float appear = easeOutCubic(t / 0.25f);
  float cx = SCREEN_W / 2.f, cy = VIEW_Y + (SMALL ? 32.f : 50.f);
  float lvl = clamp01(m.micLevel);

  // expanding ripples
  for (int i = 0; i < 3; i++) {
    float ph = fmodf(t * 0.9f + i / 3.f, 1.f);
    c.ring(cx, cy, 30.f + ph * (SMALL ? 14.f : 34.f), 2.f, INK, a8((1.f - ph) * 0.35f * appear));
  }
  // level halo
  c.fillCircle(cx, cy, (31.f + lvl * 16.f) * appear, GLOW, 110);
  c.fillCircle(cx, cy, 30.f * appear, INK, 240);
  c.drawMask8((int)(cx - icon_mic_big_w / 2), (int)(cy - icon_mic_big_h / 2), icon_mic_big_mask,
              icon_mic_big_w, icon_mic_big_h, USER_TEXT, a8(appear));

  c.drawTextCentered(font_title, SCREEN_W / 2, VIEW_Y + VIEW_H - (SMALL ? 22 : 30),
                     tr(m.uiLang, S_LISTENING), INK, a8(appear));
}

// ------------------------------------------------------------ boot
void drawBoot(Canvas& c, const UiModel& m) {
  drawBackground(c);
  float t = (m.timeMs - m.modeSinceMs) / 1000.f;

  // the big wordmark on 320 px screens, the medium one on 240 px screens
  const uint8_t* mask = SMALL ? logo_mid_mask : logo_big_mask;
  const uint8_t* glowMask = SMALL ? logo_mid_glow : logo_big_glow;
  const int lw = SMALL ? logo_mid_w : logo_big_w, lh = SMALL ? logo_mid_h : logo_big_h;
  int lx = (SCREEN_W - lw) / 2;
  int ly = (SMALL ? 44 : 58) - lh / 2;

  // handwriting reveal from left to right
  float reveal = easeOutCubic(t / 1.5f);
  int revealX = lx + (int)(lw * reveal);

  float glow = clamp01((t - 0.6f) / 1.0f);
  float pulse = 0.8f + 0.2f * sinf(t * 2.6f);
  c.drawMask8(lx, ly, glowMask, lw, lh, GLOW, a8(glow * pulse), revealX);
  c.drawMask8(lx, ly, mask, lw, lh, INK, 255, revealX);

  if (reveal < 0.995f) {  // the glowing "pen tip"
    float py = ly + lh * 0.55f + sinf(t * 18.f) * (SMALL ? 4.f : 6.f);
    c.fillCircle((float)revealX, py, 9.f, GLOW, 70);
    c.fillCircle((float)revealX, py, 3.5f, GLOW, 230);
  }

  // tagline fades in
  float tag = clamp01((t - 1.2f) / 0.6f);
  if (tag > 0) {
    c.drawTextCentered(font_small, SCREEN_W / 2, ly + lh - (SMALL ? 12 : 13),
                       "your local AI companion", INK_SOFT, a8(tag));
  }

  // progress bar
  int bw = SMALL ? 140 : 168, bx = (SCREEN_W - bw) / 2, by = SCREEN_H - 34;
  c.fillRoundRect(bx, by, bw, 5, 2, INK, 40);
  int fw = (int)(bw * clamp01(m.bootProgress));
  if (fw > 4) c.fillRoundRect(bx, by, fw, 5, 2, INK, 230);
  // shimmer
  float sh = fmodf(t * 0.8f, 1.f);
  c.setClip(bx, by, fw, 5);
  c.fillCircle(bx + sh * bw, by + 2.5f, 8.f, GLOW, 90);
  c.resetClip();

  c.drawTextCentered(font_small, SCREEN_W / 2, by + 10, m.bootStatus.c_str(), INK_SOFT);
  if (!m.version.empty() && !SMALL)
    c.drawTextRight(font_small, SCREEN_W - 6, SCREEN_H - 15, m.version.c_str(), INK, 90);
}

// ------------------------------------------------------------ settings
void drawSettings(Canvas& c, const UiModel& m) {
  drawBackground(c);
  drawIcon(c, 10, 8, icon_gear_mask, icon_gear_w, icon_gear_h, INK);
  c.drawText(font_title, 30, 3, tr(m.uiLang, S_SETTINGS), INK);
  if (!SMALL)
    c.drawMask8(SCREEN_W - 8 - logo_small_w, 1, logo_small_mask, logo_small_w, logo_small_h,
                INK, 120);
  c.hLine(8, TOPBAR_H + 1, SCREEN_W - 16, INK, 40);

  const int rowH = SMALL ? 24 : 26, top = 32, visible = (SCREEN_H - top) / rowH;
  int n = (int)m.settings.size();
  int first = m.settingsIndex - visible / 2;
  if (first > n - visible) first = n - visible;
  if (first < 0) first = 0;

  for (int i = first; i < n && i < first + visible; i++) {
    int y = top + (i - first) * rowH;
    bool sel = i == m.settingsIndex;
    const SettingsRow& row = m.settings[i];
    uint16_t fg = sel ? USER_TEXT : INK;
    if (sel) c.fillRoundRect(6, y, SCREEN_W - 12, rowH - 3, 9, INK, 235);
    c.drawText(font_body, 16, y + 3, row.label.c_str(), fg);
    if (!row.value.empty()) {
      if (sel && m.settingsEditing) {
        std::string v = "\xE2\x80\xB9 " + row.value + " \xE2\x80\xBA";  // ‹ value ›
        int w = textWidth(font_body, v.c_str()) + 12;
        c.fillRoundRect(SCREEN_W - 16 - w + 6, y + 2, w, rowH - 7, 8, BG_TOP, 60);
        c.drawTextRight(font_body, SCREEN_W - 16, y + 3, v.c_str(), USER_TEXT);
      } else {
        c.drawTextRight(font_body, SCREEN_W - 16, y + 3, row.value.c_str(), fg, sel ? 255 : 200);
      }
    }
  }
  // scroll hint
  if (n > visible) {
    int area = SCREEN_H - top - 4;
    int barH = area * visible / n;
    int barY = top + (area - barH) * first / (n - visible);
    c.fillRoundRect(SCREEN_W - 4, barY, 3, barH, 1, INK, 70);
  }
}

// ------------------------------------------------------------ voice menu
void drawVoices(Canvas& c, const UiModel& m) {
  drawBackground(c);
  drawIcon(c, 10, 8, icon_speaker_mask, icon_speaker_w, icon_speaker_h, INK);
  c.drawText(font_title, 30, 3, tr(m.uiLang, S_VOICE), INK);
  c.hLine(8, TOPBAR_H + 1, SCREEN_W - 16, INK, 40);

  const int rowH = SMALL ? 22 : 24, top = 30;
  const int bottom = SCREEN_H - font_small.lineHeight - 4;
  const int visible = (bottom - top) / rowH;
  int n = (int)m.voiceRows.size();
  int first = m.voiceIndex - visible / 2;
  if (first > n - visible) first = n - visible;
  if (first < 0) first = 0;
  c.setClip(0, top, SCREEN_W, bottom - top);
  for (int i = first; i < n && i < first + visible; i++) {
    int y = top + (i - first) * rowH;
    const SettingsRow& row = m.voiceRows[i];
    if (row.section) {
      c.drawText(font_small, 12, y + rowH - font_small.lineHeight - 2, row.label.c_str(), INK_SOFT);
      continue;
    }
    bool sel = i == m.voiceIndex;
    uint16_t fg = sel ? USER_TEXT : INK;
    if (sel) c.fillRoundRect(6, y, SCREEN_W - 12, rowH - 3, 9, INK, 235);
    int tx = 16;
    if (i == m.voicePlaying) {  // speaker icon while this voice is speaking
      drawIcon(c, tx, y + (rowH - 3 - icon_speaker_h) / 2, icon_speaker_mask, icon_speaker_w,
               icon_speaker_h, fg);
      tx += icon_speaker_w + 5;
    }
    c.drawText(font_body, tx, y + (rowH - 3 - font_body.lineHeight) / 2, row.label.c_str(), fg);
    if (row.current) {  // the voice in use: a glowing dot
      float cy = y + (rowH - 3) / 2.f;
      c.fillCircle(SCREEN_W - 22, cy, 6.f, sel ? USER_TEXT : INK, 60);
      c.fillCircle(SCREEN_W - 22, cy, 3.5f, sel ? USER_TEXT : INK);
    } else if (!row.value.empty()) {
      std::string v = row.value;
      if (sel && m.voiceEditing) v = "\xE2\x80\xB9 " + v + " \xE2\x80\xBA";
      c.drawTextRight(font_body, SCREEN_W - 16, y + (rowH - 3 - font_body.lineHeight) / 2,
                      v.c_str(), fg, sel ? 255 : 200);
    }
  }
  c.resetClip();
  const char* foot = m.voiceStatus.empty() ? tr(m.uiLang, S_VOICE_HINT) : m.voiceStatus.c_str();
  c.drawTextCentered(font_small, SCREEN_W / 2, SCREEN_H - font_small.lineHeight - 2, foot,
                     INK_SOFT);
}

// ------------------------------------------------------------ power
void drawPowerIcon(Canvas& c, float cx, float cy, float r, uint16_t col, uint8_t a) {
  // the classic power symbol: a ring open at the top plus a vertical bar
  const float gap = 0.7f;
  float a0 = -1.5708f + gap;
  c.arc(cx, cy, r, 2.6f, a0, a0 + 6.2832f - 2.f * gap, col, a);
  c.fillRoundRect((int)(cx - 1.5f), (int)(cy - r - 2), 3, (int)(r + 1), 1, col, a);
}

void drawPowerHold(Canvas& c, const UiModel& m) {
  float p = clamp01(m.powerHold);
  c.blendRect(0, 0, SCREEN_W, SCREEN_H, BG_TOP, 238);
  float cx = SCREEN_W / 2.f, cy = SCREEN_H * 0.41f;
  c.ring(cx, cy, 34.f, 6.f, INK, 40);
  c.arc(cx, cy, 34.f, 6.f, -1.5708f, -1.5708f + 6.2832f * p, INK, 255);
  drawPowerIcon(c, cx, cy, 13.f, INK, 255);
  c.drawTextCentered(font_body, SCREEN_W / 2, (int)(cy + 44), tr(m.uiLang, S_HOLD_TO_POWER_OFF), INK);
}

void drawOff(Canvas& c, const UiModel& m) {
  drawBackground(c);
  int lx = (SCREEN_W - logo_mid_w) / 2;
  int ly = (SCREEN_H - logo_mid_h - 26) / 2;
  c.drawMask8(lx, ly, logo_mid_glow, logo_mid_w, logo_mid_h, GLOW, 200);
  c.drawMask8(lx, ly, logo_mid_mask, logo_mid_w, logo_mid_h, INK);
  c.drawTextCentered(font_title, SCREEN_W / 2, ly + logo_mid_h + 4, tr(m.uiLang, S_GOODBYE), INK);
}

// ------------------------------------------------------------ Wi-Fi setup
// QR code that joins the setup hotspot (standard "WIFI:" format, understood by
// the iPhone and Android cameras).
void drawQr(Canvas& c, int x, int y, int module, const std::string& text) {
  static std::string cachedText;
  static uint8_t buf[256];  // >= qr_getBufferSize(6)
  static geistqr::QRCode qr;
  static bool ok = false;
  if (text != cachedText) {
    cachedText = text;
    ok = false;
    for (uint8_t v = 4; v <= 6 && !ok; v++)
      ok = geistqr::qr_initText(&qr, buf, v, QR_ECC_LOW, text.c_str()) == 0;
  }
  const int quiet = 2;
  if (!ok) return;
  int size = (qr.size + 2 * quiet) * module;
  c.fillRoundRect(x, y, size, size, 6, SNOW);
  for (int my = 0; my < qr.size; my++)
    for (int mx = 0; mx < qr.size; mx++)
      if (geistqr::qr_getModule(&qr, mx, my))
        c.fillRect(x + (mx + quiet) * module, y + (my + quiet) * module, module, module, INK);
}

std::string wifiQrEscape(const std::string& s) {
  std::string o;
  for (char ch : s) {
    if (ch == '\\' || ch == ';' || ch == ',' || ch == ':' || ch == '"') o += '\\';
    o += ch;
  }
  return o;
}

void drawSetup(Canvas& c, const UiModel& m) {
  drawBackground(c);
  const uint8_t L = m.uiLang;
  if (m.setupSaved) {
    float cx = SCREEN_W / 2.f, cy = SCREEN_H * 0.4f;
    c.fillCircle(cx, cy, 24.f, INK);
    // check mark
    for (int i = 0; i < 9; i++) c.fillCircle(cx - 10 + i, cy + i * 0.9f - 1, 2.2f, USER_TEXT);
    for (int i = 0; i < 16; i++) c.fillCircle(cx - 2 + i, cy + 7 - i * 1.1f, 2.2f, USER_TEXT);
    c.drawTextCentered(font_title, SCREEN_W / 2, (int)cy + 32, tr(L, S_SETUP_SAVED), INK);
    return;
  }

  const std::string qrText =
      "WIFI:T:WPA;S:" + wifiQrEscape(m.setupSsid) + ";P:" + wifiQrEscape(m.setupPass) + ";;";
  const int module = SMALL ? 3 : 4;
  const int qrPx = (33 + 4) * module;  // version 4 + quiet zone
  const int qx = 8, qy = (SCREEN_H - qrPx) / 2;
  drawQr(c, qx, qy, module, qrText);

  const int tx = qx + qrPx + (SMALL ? 8 : 12);
  const int tw = SCREEN_W - tx - 6;
  static Line lines[4];
  int y = 0;
  auto para = [&](const Font& f, const std::string& s, uint16_t col) {
    int n = wrapText(f, s.c_str(), tw, lines, 3);
    for (int i = 0; i < n; i++, y += f.lineHeight)
      c.drawText(f, tx, y, s.c_str() + lines[i].start, col, 255, lines[i].len);
  };

  if (SMALL) {  // narrow column: just the essentials, the QR code explains itself
    y = 6;
    para(font_small, tr(L, S_SETUP_JOIN), INK_SOFT);
    para(font_body, m.setupSsid, INK);
    y += 3;
    para(font_small, tr(L, S_SETUP_PASS), INK_SOFT);
    para(font_body, m.setupPass, INK);
    y += 3;
    para(font_small, tr(L, S_SETUP_OPEN), INK_SOFT);
    para(font_body, m.setupUrl, INK);
    return;
  }

  y = 8;
  c.drawText(font_title, tx, y, tr(L, S_WIFI_SETUP), INK);
  y += font_title.lineHeight + 6;
  para(font_small, std::string("1. ") + tr(L, S_SETUP_SCAN), INK);
  para(font_small, tr(L, S_SETUP_JOIN), INK_SOFT);
  para(font_body, m.setupSsid, INK);
  para(font_small, std::string(tr(L, S_SETUP_PASS)) + ": " + m.setupPass, INK);
  y += 5;
  para(font_small, std::string("2. ") + tr(L, S_SETUP_OPEN) + " " + m.setupUrl, INK);
  c.drawText(font_small, tx, SCREEN_H - font_small.lineHeight - 4, tr(L, S_SETUP_CANCEL), INK_SOFT, 200);
}

// ------------------------------------------------------------ keyboard
void drawKeyboard(Canvas& c, const UiModel& m) {
  drawBackground(c);

  // text field (shows the end of the text, at most 2 lines)
  const int fx = 6, fy = 4, fw = SCREEN_W - 12;
  const int fh = m.kbPhysical ? SCREEN_H - 34 : (SMALL ? 22 : 38);
  const int maxLines = m.kbPhysical ? (fh - 4) / 17 : (SMALL ? 1 : 2);
  c.fillRoundRect(fx, fy, fw, fh, 10, SURFACE, 185);
  const char* text = m.kbText.c_str();
  bool blink = (m.timeMs / 500) % 2 == 0;
  if (m.kbText.empty()) {
    c.drawText(font_body, fx + 10, fy + 3,
               tr(m.uiLang, m.kbNameMode ? S_NAME_PLACEHOLDER : S_TYPE_PLACEHOLDER), INK_SOFT, 200);
    if (blink) c.fillRect(fx + 8, fy + 3, 2, 17, INK);
  } else {
    static Line lines[32];
    int n = wrapText(font_body, text, fw - 22, lines, 32);
    int first = n > maxLines ? n - maxLines : 0;
    int y = fy + 2;
    int endX = fx + 10;
    for (int i = first; i < n; i++, y += 17) {
      endX = c.drawText(font_body, fx + 10, y, text + lines[i].start, INK, 255, lines[i].len);
    }
    // a trailing space isn't part of the wrapped line, so nudge the cursor
    if (!m.kbText.empty() && m.kbText.back() == ' ') endX += 4;
    if (blink) c.fillRect(endX + 1, y - 17 + 1, 2, 16, INK);
  }

  if (m.kbPhysical) {  // Cardputer: real keyboard, just show a hint
    c.drawTextCentered(font_small, SCREEN_W / 2, SCREEN_H - 22,
                       m.kbNameMode ? "Enter: OK \xC2\xB7 Esc: cancel" : "Enter: send \xC2\xB7 Esc: close",
                       INK_SOFT);
    return;
  }

  // keys
  const int count = kb::keyCount();
  for (int i = 0; i < count; i++) {
    const kb::Key& k = kb::key(m.kbPage, i);
    kb::Rect r = kb::keyRect(i);
    bool sel = i == m.kbIndex;
    bool special = k.action != kb::CHAR;
    uint16_t fg = sel ? USER_TEXT : INK;
    if (sel) {
      c.fillRoundRect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, 8, GLOW, 120);
      c.fillRoundRect(r.x, r.y, r.w, r.h, 7, INK, 245);
    } else if (k.action == kb::SEND) {
      c.fillRoundRect(r.x, r.y, r.w, r.h, 7, INK, 70);
    } else if (k.action == kb::SHIFT && m.kbShift) {
      c.fillRoundRect(r.x, r.y, r.w, r.h, 7, INK, 90);
    } else {
      c.fillRoundRect(r.x, r.y, r.w, r.h, 7, SURFACE, special ? 90 : 140);
    }
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    auto icon = [&](const uint8_t* mask, int w, int h) {
      c.drawMask8(cx - w / 2, cy - h / 2, mask, w, h, fg);
    };
    switch (k.action) {
      case kb::CHAR: {
        const char* label = m.kbShift ? k.upper : k.lower;
        c.drawTextCentered(font_body, cx, cy - font_body.lineHeight / 2, label, fg);
        break;
      }
      case kb::SHIFT: icon(icon_shift_mask, icon_shift_w, icon_shift_h); break;
      case kb::PAGE:
        c.drawTextCentered(font_small, cx, cy - font_small.lineHeight / 2,
                           kb::pageKeyLabel(m.kbPage), fg);
        break;
      case kb::SPACE: c.fillRoundRect(cx - r.w / 3, cy + 3, (r.w * 2) / 3, 3, 1, fg, 200); break;
      case kb::BACKSPACE: icon(icon_backspace_mask, icon_backspace_w, icon_backspace_h); break;
      case kb::SETTINGS: icon(icon_gear_mask, icon_gear_w, icon_gear_h); break;
      case kb::CLOSE: icon(icon_close_mask, icon_close_w, icon_close_h); break;
      case kb::SEND: icon(icon_send_mask, icon_send_w, icon_send_h); break;
    }
  }
}

}  // namespace

// ------------------------------------------------------------ public
#define W_ rgb(255, 255, 255)
const ThemeColors kThemes[THEME_COUNT] = {
    // bgTop, bgBottom, ink, inkSoft, userText, blob, LED r g b, surface, glow
    {rgb(40, 246, 255), rgb(0, 206, 240), rgb(0, 24, 36), rgb(0, 86, 108),          // neon cyan
     rgb(150, 250, 255), rgb(0, 160, 220), 0, 230, 255, W_, W_},
    {rgb(255, 128, 222), rgb(240, 70, 186), rgb(42, 0, 32), rgb(112, 12, 84),       // neon pink
     rgb(255, 196, 240), rgb(200, 30, 150), 255, 40, 180, W_, W_},
    {rgb(160, 255, 100), rgb(70, 230, 100), rgb(0, 38, 12), rgb(22, 98, 40),        // neon green
     rgb(196, 255, 160), rgb(20, 180, 70), 60, 255, 60, W_, W_},
    {rgb(196, 160, 255), rgb(148, 100, 250), rgb(22, 0, 54), rgb(72, 34, 132),      // violet
     rgb(222, 204, 255), rgb(104, 56, 222), 140, 60, 255, W_, W_},
    {rgb(255, 206, 96), rgb(255, 136, 64), rgb(52, 16, 0), rgb(128, 56, 10),        // sunset
     rgb(255, 224, 168), rgb(240, 96, 32), 255, 120, 0, W_, W_},
    {rgb(238, 249, 255), rgb(194, 226, 255), rgb(10, 30, 60), rgb(62, 92, 132),     // ice
     rgb(204, 232, 255), rgb(124, 172, 240), 180, 220, 255, W_, W_},
    {rgb(255, 118, 96), rgb(232, 44, 64), rgb(44, 0, 6), rgb(120, 10, 24),          // lava
     rgb(255, 204, 196), rgb(200, 20, 40), 255, 20, 20, W_, W_},
    {rgb(255, 234, 112), rgb(246, 190, 40), rgb(48, 32, 0), rgb(118, 84, 0),        // gold
     rgb(255, 242, 176), rgb(230, 160, 0), 255, 180, 0, W_, W_},
    {rgb(174, 255, 226), rgb(92, 230, 190), rgb(0, 40, 32), rgb(10, 100, 80),       // mint
     rgb(196, 255, 236), rgb(30, 190, 150), 40, 255, 180, W_, W_},
    {rgb(96, 176, 255), rgb(42, 112, 242), rgb(0, 16, 50), rgb(10, 46, 112),        // ocean
     rgb(186, 220, 255), rgb(20, 70, 200), 30, 90, 255, W_, rgb(220, 240, 255)},
    // dark themes: light text on a dark background
    {rgb(18, 28, 56), rgb(6, 12, 30), rgb(176, 246, 255), rgb(112, 168, 200),       // midnight
     rgb(8, 20, 36), rgb(40, 90, 170), 0, 180, 255, rgb(44, 64, 104), rgb(80, 220, 255)},
    {rgb(6, 26, 12), rgb(0, 8, 4), rgb(124, 255, 144), rgb(64, 170, 84),            // hacker
     rgb(0, 24, 8), rgb(0, 96, 34), 0, 255, 60, rgb(22, 64, 32), rgb(60, 255, 100)},
    {rgb(62, 62, 68), rgb(28, 28, 32), rgb(242, 242, 244), rgb(170, 170, 178),      // graphite
     rgb(22, 22, 26), rgb(96, 96, 104), 255, 255, 255, rgb(88, 88, 96), W_},
};
#undef W_

namespace theme {
uint16_t BG_TOP = kThemes[0].bgTop;
uint16_t BG_BOTTOM = kThemes[0].bgBottom;
uint16_t INK = kThemes[0].ink;
uint16_t INK_SOFT = kThemes[0].inkSoft;
uint16_t USER_TEXT = kThemes[0].userText;
uint16_t BLOB = kThemes[0].blob;
uint16_t SURFACE = kThemes[0].surface;
uint16_t GLOW = kThemes[0].glow;
}  // namespace theme

static int g_theme = 0;

void setTheme(int index) {
  if (index < 0 || index >= THEME_COUNT) index = 0;
  g_theme = index;
  const ThemeColors& t = kThemes[index];
  theme::BG_TOP = t.bgTop;
  theme::BG_BOTTOM = t.bgBottom;
  theme::INK = t.ink;
  theme::INK_SOFT = t.inkSoft;
  theme::USER_TEXT = t.userText;
  theme::BLOB = t.blob;
  theme::SURFACE = t.surface;
  theme::GLOW = t.glow;
  free(g_bgCache);  // repaint the background in the new colours
  g_bgCache = nullptr;
  g_bgFailed = false;
}

int currentTheme() { return g_theme; }

int SCREEN_W = 320;
int SCREEN_H = 170;

void setScreenSize(int w, int h) {
  SCREEN_W = w;
  SCREEN_H = h;
  SMALL = w < 300;
  VIEW_H = h - 52;
  BOTTOM_Y = h - 22;
  BUBBLE_MAX_TEXT_W = w - 88;
  kb::setLayout(w, h);
  free(g_bgCache);
  g_bgCache = nullptr;
  g_bgFailed = false;
}

void drawThemeBackground(Canvas& c) { drawBackground(c); }
const Font& fontBody() { return font_body; }
const Font& fontSmall() { return font_small; }
const Font& fontTitle() { return font_title; }

int chatViewportHeight() { return VIEW_H; }

int chatContentHeight(const UiModel& m) {
  static Line lines[MAX_LINES];
  int h = 3;
  if (m.pendingReply) h += PENDING_H + BUBBLE_GAP;
  for (const ChatMsg& msg : m.chat) h += layoutBubble(msg, lines).h + BUBBLE_GAP;
  return h;
}

void render(Canvas& c, const UiModel& m) {
  c.resetClip();
  switch (m.mode) {
    case Mode::Boot:
      drawBoot(c, m);
      return;
    case Mode::Settings:
      drawSettings(c, m);
      if (m.powerHold > 0.f) drawPowerHold(c, m);
      return;
    case Mode::Off:
      drawOff(c, m);
      return;
    case Mode::Keyboard:
      drawKeyboard(c, m);
      if (m.powerHold > 0.f) drawPowerHold(c, m);
      return;
    case Mode::Setup:
      drawSetup(c, m);
      if (m.powerHold > 0.f) drawPowerHold(c, m);
      return;
    case Mode::Voices:
      drawVoices(c, m);
      if (m.powerHold > 0.f) drawPowerHold(c, m);
      return;
    case Mode::Games:
      games::draw(c, m.uiLang, m.timeMs);
      if (m.powerHold > 0.f) drawPowerHold(c, m);
      return;
    default:
      break;
  }

  drawBackground(c);
  drawTopBar(c, m);
  if (m.chat.empty() && !m.pendingReply) {
    if (m.mode != Mode::Listening) drawWelcome(c, m);
  } else {
    drawChat(c, m);
  }
  if (m.mode == Mode::Listening) drawListeningOverlay(c, m);
  drawBottomBar(c, m);
  if (m.powerHold > 0.f) drawPowerHold(c, m);
}

}  // namespace geist
