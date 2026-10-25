#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "assets/fonts.h"
#include "assets/icons.h"
#include "assets/logo.h"
#include "i18n.h"

namespace geist {

using namespace theme;

namespace {

constexpr int TOPBAR_H = 26;
constexpr int VIEW_Y = 28;
constexpr int VIEW_H = 118;
constexpr int BOTTOM_Y = 148;
constexpr int BUBBLE_PAD_X = 8;
constexpr int BUBBLE_PAD_Y = 4;
constexpr int BUBBLE_GAP = 6;
constexpr int BUBBLE_MAX_TEXT_W = 232;
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
std::vector<uint16_t> g_bgCache;

void buildBackground(int w, int h) {
  g_bgCache.assign((size_t)w * h, 0);
  Canvas bg(g_bgCache.data(), w, h);
  bg.vGradient(0, 0, w, h, BG_TOP, BG_BOTTOM);
  // soft "neon" light blobs
  for (int i = 0; i < 6; i++) {
    bg.fillCircle(285.f, -10.f, 30.f + i * 18.f, SNOW, 14);
    bg.fillCircle(10.f, 190.f, 30.f + i * 16.f, rgb(0, 160, 220), 10);
  }
  // fine diagonal sheen lines
  for (int x = -h; x < w; x += 22) {
    for (int y = 0; y < h; y++) {
      int px = x + y / 2;
      if (px >= 0 && px < w) bg.plot(px, y, SNOW, 10);
    }
  }
}

void drawBackground(Canvas& c) {
  if (g_bgCache.size() != (size_t)c.width() * c.height()) buildBackground(c.width(), c.height());
  memcpy(c.data(), g_bgCache.data(), g_bgCache.size() * sizeof(uint16_t));
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
  c.fillRoundRect(x, y, 50, PENDING_H, 10, SNOW, 170);
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
        c.fillRoundRect(x, y, b.w, b.h, 10, SNOW, 175);
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
  int lx = (SCREEN_W - logo_mid_w) / 2;
  int ly = VIEW_Y + 4;
  float pulse = 0.75f + 0.25f * sinf(m.timeMs / 900.f);
  c.drawMask8(lx, ly, logo_mid_glow, logo_mid_w, logo_mid_h, SNOW, a8(0.85f * pulse));
  c.drawMask8(lx, ly, logo_mid_mask, logo_mid_w, logo_mid_h, INK);

  static Line lines[4];
  const char* text = tr(m.uiLang, S_WELCOME);
  int n = wrapText(font_body, text, 260, lines, 4);
  int y = ly + logo_mid_h + 2;
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
      const char* hint = tr(m.uiLang, alt ? S_DOUBLE_FOR_MENU : S_HOLD_TO_TALK);
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
      const char* t = m.errorText.empty() ? tr(m.uiLang, S_ERR_GENERIC) : m.errorText.c_str();
      int w = textWidth(font_small, t) + 20;
      if (w > SCREEN_W - 16) w = SCREEN_W - 16;
      int x = (SCREEN_W - w) / 2;
      drawPill(c, x, y, w, ERR_RED, 240);
      c.setClip(x + 6, y, w - 12, 19);
      c.drawText(font_small, x + 10, y + 3, t, SNOW);
      c.resetClip();
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
  float cx = SCREEN_W / 2.f, cy = VIEW_Y + 50.f;
  float lvl = clamp01(m.micLevel);

  // expanding ripples
  for (int i = 0; i < 3; i++) {
    float ph = fmodf(t * 0.9f + i / 3.f, 1.f);
    c.ring(cx, cy, 30.f + ph * 34.f, 2.f, INK, a8((1.f - ph) * 0.35f * appear));
  }
  // level halo
  c.fillCircle(cx, cy, (31.f + lvl * 16.f) * appear, SNOW, 110);
  c.fillCircle(cx, cy, 30.f * appear, INK, 240);
  c.drawMask8((int)(cx - icon_mic_big_w / 2), (int)(cy - icon_mic_big_h / 2), icon_mic_big_mask,
              icon_mic_big_w, icon_mic_big_h, USER_TEXT, a8(appear));

  c.drawTextCentered(font_title, SCREEN_W / 2, VIEW_Y + 88, tr(m.uiLang, S_LISTENING), INK,
                     a8(appear));
}

// ------------------------------------------------------------ boot
void drawBoot(Canvas& c, const UiModel& m) {
  drawBackground(c);
  float t = (m.timeMs - m.modeSinceMs) / 1000.f;

  int lx = (SCREEN_W - logo_big_w) / 2;
  int ly = 58 - logo_big_h / 2;

  // handwriting reveal from left to right
  float reveal = easeOutCubic(t / 1.5f);
  int revealX = lx + (int)(logo_big_w * reveal);

  float glow = clamp01((t - 0.6f) / 1.0f);
  float pulse = 0.8f + 0.2f * sinf(t * 2.6f);
  c.drawMask8(lx, ly, logo_big_glow, logo_big_w, logo_big_h, SNOW, a8(glow * pulse), revealX);
  c.drawMask8(lx, ly, logo_big_mask, logo_big_w, logo_big_h, INK, 255, revealX);

  if (reveal < 0.995f) {  // the glowing "pen tip"
    float py = ly + logo_big_h * 0.55f + sinf(t * 18.f) * 6.f;
    c.fillCircle((float)revealX, py, 9.f, SNOW, 70);
    c.fillCircle((float)revealX, py, 3.5f, SNOW, 230);
  }

  // tagline fades in
  float tag = clamp01((t - 1.2f) / 0.6f);
  if (tag > 0) {
    c.drawTextCentered(font_small, SCREEN_W / 2, ly + logo_big_h - 13, "your local AI companion",
                       INK_SOFT, a8(tag));
  }

  // progress bar
  int bw = 168, bx = (SCREEN_W - bw) / 2, by = 136;
  c.fillRoundRect(bx, by, bw, 5, 2, INK, 40);
  int fw = (int)(bw * clamp01(m.bootProgress));
  if (fw > 4) c.fillRoundRect(bx, by, fw, 5, 2, INK, 230);
  // shimmer
  float sh = fmodf(t * 0.8f, 1.f);
  c.setClip(bx, by, fw, 5);
  c.fillCircle(bx + sh * bw, by + 2.5f, 8.f, SNOW, 90);
  c.resetClip();

  c.drawTextCentered(font_small, SCREEN_W / 2, by + 10, m.bootStatus.c_str(), INK_SOFT);
  if (!m.version.empty()) c.drawTextRight(font_small, SCREEN_W - 6, SCREEN_H - 15, m.version.c_str(), INK, 90);
}

// ------------------------------------------------------------ settings
void drawSettings(Canvas& c, const UiModel& m) {
  drawBackground(c);
  drawIcon(c, 10, 8, icon_gear_mask, icon_gear_w, icon_gear_h, INK);
  c.drawText(font_title, 30, 3, tr(m.uiLang, S_SETTINGS), INK);
  c.drawMask8(SCREEN_W - 8 - logo_small_w, 1, logo_small_mask, logo_small_w, logo_small_h,
              INK, 120);
  c.hLine(8, TOPBAR_H + 1, SCREEN_W - 16, INK, 40);

  const int rowH = 26, top = 32, visible = 5;
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
        c.fillRoundRect(SCREEN_W - 16 - w + 6, y + 2, w, rowH - 7, 8, SNOW, 60);
        c.drawTextRight(font_body, SCREEN_W - 16, y + 3, v.c_str(), SNOW);
      } else {
        c.drawTextRight(font_body, SCREEN_W - 16, y + 3, row.value.c_str(), fg, sel ? 255 : 200);
      }
    }
  }
  // scroll hint
  if (n > visible) {
    int barH = VIEW_H * visible / n;
    int barY = top + (VIEW_H + 12 - barH) * first / (n - visible);
    c.fillRoundRect(SCREEN_W - 4, barY, 3, barH, 1, INK, 70);
  }
}

}  // namespace

// ------------------------------------------------------------ public
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
}

}  // namespace geist
