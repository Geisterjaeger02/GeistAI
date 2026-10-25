// GeistAI - hardware layer for the M5Stack boards (Cardputer, StickC Plus2),
// built on M5Unified / M5Cardputer. Implements the same interfaces as the
// LilyGO files: display.h, input.h, audio.h, power.h.
#include "board.h"
#if GEIST_HAL_M5

#include <Arduino.h>
#include <esp_heap_caps.h>

#if defined(GEIST_BOARD_CARDPUTER)
#include <M5Cardputer.h>
#else
#include <M5Unified.h>
#endif

#include "audio.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "power.h"

namespace {
bool g_m5Started = false;

void startM5() {
  if (g_m5Started) return;
  g_m5Started = true;
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;  // we start Serial ourselves
#if defined(GEIST_BOARD_CARDPUTER)
  M5Cardputer.begin(cfg, true);
#else
  M5.begin(cfg);
#endif
}

// M5Unified is not thread safe: the input task and the worker (audio) share it.
SemaphoreHandle_t g_m5Lock = nullptr;
struct M5Lock {
  M5Lock() {
    if (g_m5Lock) xSemaphoreTake(g_m5Lock, portMAX_DELAY);
  }
  ~M5Lock() {
    if (g_m5Lock) xSemaphoreGive(g_m5Lock);
  }
};
}  // namespace

// =============================================================================
//  display
// =============================================================================
namespace display {
namespace {
uint16_t* fb = nullptr;
int W = 240, H = 135;
}  // namespace

bool begin() {
  startM5();
  if (!g_m5Lock) g_m5Lock = xSemaphoreCreateMutex();
  M5.Display.setRotation(1);  // landscape
  W = M5.Display.width();
  H = M5.Display.height();
  M5.Display.fillScreen(0x0000);
  M5.Display.setBrightness(LCD_BRIGHTNESS);
  const size_t bytes = (size_t)W * H * sizeof(uint16_t);
  fb = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!fb) fb = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return fb != nullptr;
}

uint16_t* framebuffer() { return fb; }
int width() { return W; }
int height() { return H; }

void present() {
  if (fb) M5.Display.pushImage(0, 0, W, H, (const lgfx::rgb565_t*)fb);
}

void setBrightness(uint8_t level) { M5.Display.setBrightness(level); }

void sleep() {
  M5.Display.setBrightness(0);
  M5.Display.sleep();
}

void status(const char* msg) {
  Serial.printf("[boot] %s\n", msg);
  M5.Display.fillRect(0, H - 26, W, 26, 0x0000);
  M5.Display.setTextColor(0xFFFF, 0x0000);
  M5.Display.setTextSize(1);
  M5.Display.drawString("GeistAI starting...", 4, H - 24);
  M5.Display.drawString(msg, 4, H - 12);
}

}  // namespace display

// =============================================================================
//  input
// =============================================================================
namespace input {
namespace {
QueueHandle_t events = nullptr;
volatile bool mainDown = false;  // BtnA (G0) or, on the Cardputer, Space
volatile bool backDown = false;

void push(EventType t, uint32_t now, int delta = 0, uint32_t held = 0, const char* text = nullptr) {
  Event ev;
  ev.type = t;
  ev.atMs = now;
  ev.delta = delta;
  ev.heldMs = held;
  if (text) strncpy(ev.text, text, sizeof(ev.text) - 1);
  xQueueSend(events, &ev, 0);
}

// Turns a button into Down/Up (or BackDown/BackUp) events with hold times.
struct EdgeButton {
  bool last = false;
  uint32_t downAt = 0;
  void update(bool pressed, uint32_t now, EventType downEv, EventType upEv, volatile bool* flag) {
    if (pressed == last) return;
    last = pressed;
    if (flag) *flag = pressed;
    if (pressed) {
      downAt = now;
      push(downEv, now);
    } else {
      push(upEv, now, 0, now - downAt);
    }
  }
};

#if defined(GEIST_BOARD_CARDPUTER)
// ---- Cardputer: G0 button + full keyboard
EdgeButton btnA, space, tab;
std::vector<Point2D_t> lastKeys;

bool contains(const std::vector<Point2D_t>& v, const Point2D_t& p) {
  for (const auto& q : v)
    if (q.x == p.x && q.y == p.y) return true;
  return false;
}

void scanKeyboard(uint32_t now) {
  auto& kb = M5Cardputer.Keyboard;
  const auto& keys = kb.keyList();
  auto& st = kb.keysState();
  bool spaceNow = false, tabNow = false;
  for (const auto& k : keys) {
    KeyValue_t v = kb.getKeyValue(k);
    if (v.value_first == ' ') spaceNow = true;
    if (v.value_first == KEY_TAB) tabNow = true;
  }
  // newly pressed keys only, so holding one key while pressing another
  // doesn't repeat characters
  for (const auto& k : keys) {
    if (contains(lastKeys, k)) continue;
    KeyValue_t v = kb.getKeyValue(k);
    uint8_t first = (uint8_t)v.value_first;
    if (st.fn) {  // Fn layer: ; . , / are the arrow keys, ` is Esc, Del deletes
      switch (first) {
        case ';': push(EventType::Rotate, now, -1); break;
        case '.': push(EventType::Rotate, now, 1); break;
        case ',': push(EventType::Rotate, now, -1); break;
        case '/': push(EventType::Rotate, now, 1); break;
        case '`': push(EventType::Escape, now); break;
        case KEY_BACKSPACE: push(EventType::Backspace, now); break;
        default: break;
      }
      continue;
    }
    switch (first) {
      case KEY_ENTER: push(EventType::Enter, now); break;
      case KEY_BACKSPACE: push(EventType::Backspace, now); break;
      case KEY_TAB:
      case ' ':
      case KEY_FN:
      case KEY_LEFT_SHIFT:
      case KEY_LEFT_CTRL:
      case KEY_LEFT_ALT:
      case KEY_OPT:
        break;  // handled separately / modifiers
      default: {
        char c = (st.shift || st.ctrl) ? v.value_second : v.value_first;
        if (c == '`' && !st.shift) {  // the top-left key doubles as Esc
          push(EventType::Escape, now);
          break;
        }
        char text[2] = {c, 0};
        if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) push(EventType::Char, now, 0, 0, text);
        break;
      }
    }
  }
  lastKeys = keys;
  space.update(spaceNow && !st.fn, now, EventType::SpaceDown, EventType::SpaceUp, nullptr);
  tab.update(tabNow, now, EventType::BackDown, EventType::BackUp, &backDown);
}
#else
// ---- StickC Plus2: A = main button, B = next (hold: previous), PWR = back / power
EdgeButton btnA, btnPwr;
uint32_t bDownAt = 0, bLastRepeat = 0;
bool bRepeated = false;
#endif

void scanTask(void*) {
  for (;;) {
    uint32_t now = millis();
    {
      M5Lock l;
#if defined(GEIST_BOARD_CARDPUTER)
      M5Cardputer.update();
      btnA.update(M5.BtnA.isPressed(), now, EventType::Down, EventType::Up, nullptr);
      if (M5Cardputer.Keyboard.isChange()) scanKeyboard(now);
      mainDown = btnA.last || space.last;
#else
      M5.update();
      btnA.update(M5.BtnA.isPressed(), now, EventType::Down, EventType::Up, &mainDown);
      btnPwr.update(M5.BtnPWR.isPressed(), now, EventType::BackDown, EventType::BackUp, &backDown);
      if (M5.BtnB.wasPressed()) {
        bDownAt = now;
        bRepeated = false;
      }
      if (M5.BtnB.isPressed() && now - bDownAt > 500 && now - bLastRepeat > 150) {
        bLastRepeat = now;  // hold B: go backwards
        bRepeated = true;
        push(EventType::Rotate, now, -1);
      }
      if (M5.BtnB.wasReleased() && !bRepeated) push(EventType::Rotate, now, 1);
#endif
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}  // namespace

void begin() {
  events = xQueueCreate(32, sizeof(Event));
  xTaskCreatePinnedToCore(scanTask, "input", 4096, nullptr, 3, nullptr, 1);
}

bool poll(Event& ev) { return events && xQueueReceive(events, &ev, 0) == pdTRUE; }
bool buttonIsDown() { return mainDown; }
bool backIsDown() { return backDown; }

}  // namespace input

// =============================================================================
//  audio
// =============================================================================
namespace audio {
namespace {
uint8_t g_volume = DEFAULT_VOLUME;

// mic: three buffers in rotation - two queued in M5Unified, one being read
constexpr size_t MIC_CHUNK = 256;
int16_t micBuf[3][MIC_CHUNK];
int micNext = 0, micReady = 0;
bool micRunning = false;

// speaker: three buffers so a queued one is never overwritten
constexpr size_t SPK_CHUNK = 1024;
int16_t* spkBuf[3] = {nullptr, nullptr, nullptr};
int spkIdx = 0;
uint32_t g_spkRate = 22050;
}  // namespace

bool begin() {
  for (auto& b : spkBuf) b = (int16_t*)malloc(SPK_CHUNK * sizeof(int16_t));
  M5Lock l;
  M5.Speaker.setVolume(g_volume * 255 / 100);
  return M5.Mic.isEnabled() || M5.Mic.begin();
}

void micStart() {
  M5Lock l;
  M5.Speaker.end();  // Cardputer: mic and speaker share a pin
  M5.Mic.begin();
  M5.Mic.record(micBuf[0], MIC_CHUNK, MIC_SAMPLE_RATE);
  M5.Mic.record(micBuf[1], MIC_CHUNK, MIC_SAMPLE_RATE);
  micNext = 2;
  micReady = 0;
  micRunning = true;
}

size_t micRead(int16_t* dst, size_t maxSamples, float* rms) {
  if (!micRunning) return 0;
  // Queueing the next buffer waits until a slot is free, i.e. until the
  // oldest queued buffer has been filled.
  {
    M5Lock l;
    M5.Mic.record(micBuf[micNext], MIC_CHUNK, MIC_SAMPLE_RATE);
  }
  size_t n = maxSamples < MIC_CHUNK ? maxSamples : MIC_CHUNK;
  memcpy(dst, micBuf[micReady], n * sizeof(int16_t));
  micReady = (micReady + 1) % 3;
  micNext = (micNext + 1) % 3;
  double acc = 0;
  for (size_t i = 0; i < n; i++) acc += (double)dst[i] * dst[i];
  if (rms) *rms = n ? sqrtf((float)(acc / n)) : 0.f;
  return n;
}

void micStop() {
  M5Lock l;
  micRunning = false;
  M5.Mic.end();
}

void speakerStart(uint32_t sampleRate) {
  M5Lock l;
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(g_volume * 255 / 100);
  g_spkRate = sampleRate;
  spkIdx = 0;
}

float speakerWrite(const int16_t* samples, size_t count) {
  double acc = 0;
  for (size_t i = 0; i < count; i++) acc += (double)samples[i] * samples[i];
  size_t done = 0;
  while (done < count) {
    size_t n = count - done;
    if (n > SPK_CHUNK) n = SPK_CHUNK;
    while (M5.Speaker.isPlaying(0) >= 2) vTaskDelay(1);  // both slots busy
    memcpy(spkBuf[spkIdx], samples + done, n * sizeof(int16_t));
    {
      M5Lock l;
      M5.Speaker.playRaw(spkBuf[spkIdx], n, g_spkRate, false, 1, 0);
    }
    spkIdx = (spkIdx + 1) % 3;
    done += n;
  }
  return count ? sqrtf((float)(acc / count)) / 32768.f : 0.f;
}

void speakerStop() {
  while (M5.Speaker.isPlaying(0)) vTaskDelay(2);
  M5Lock l;
  M5.Speaker.end();
}

void setVolume(uint8_t percent) {
  g_volume = percent > 100 ? 100 : percent;
  M5.Speaker.setVolume(g_volume * 255 / 100);
}
uint8_t volume() { return g_volume; }

}  // namespace audio

// =============================================================================
//  power
// =============================================================================
namespace power {

void handleWakeUp() { startM5(); }  // on the StickC Plus2 this latches the power on

void switchOff() {
  M5.Display.setBrightness(0);
  M5.Power.powerOff();  // StickC Plus2: cuts power; Cardputer: deep sleep
  for (;;) delay(1000);
}

int batteryPercent() {
  int level = M5.Power.getBatteryLevel();
  return level < 0 || level > 100 ? -1 : level;
}

}  // namespace power

#endif  // GEIST_HAL_M5
