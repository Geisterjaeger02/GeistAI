// =============================================================================
//  GeistAI - a multilingual voice assistant for the LilyGO T-Embed
//
//  The T-Embed records your voice, sends it to the GeistAI server on your own
//  network (speech-to-text -> local LLM -> text-to-speech) and shows / speaks
//  the answer. All the heavy AI work runs on your PC; nothing leaves your LAN.
//
//  Controls
//    hold knob ......... push-to-talk (release to send)
//    tap knob .......... start listening, stops automatically when you pause
//                        (tap again to send right away)
//    double-tap ........ settings
//    turn knob ......... scroll the conversation / change settings
//    tap while speaking  stop the voice reply and talk again
// =============================================================================
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "audio.h"
#include "board.h"
#include "config.h"
#include "display.h"
#include "gfx.h"
#include "i18n.h"
#include "input.h"
#include "leds.h"
#include "net.h"
#include "ui.h"

using namespace geist;

#ifndef DEFAULT_UI_LANG
#define DEFAULT_UI_LANG UI_EN
#endif

// ----------------------------------------------------------------------------
//  Shared state
// ----------------------------------------------------------------------------
static UiModel model;
static SemaphoreHandle_t modelMutex;
static Canvas* canvas = nullptr;

struct ModelLock {
  ModelLock() { xSemaphoreTake(modelMutex, portMAX_DELAY); }
  ~ModelLock() { xSemaphoreGive(modelMutex); }
};

struct Settings {
  uint8_t speechLang = 0;  // index into kSpeechLangs (0 = auto)
  uint8_t uiLang = DEFAULT_UI_LANG;
  uint8_t volume = DEFAULT_VOLUME;
  bool voiceReplies = true;
} settings;

static Preferences prefs;

// Worker <-> UI signalling
enum : uint8_t { CMD_TURN = 1, CMD_FIND_SERVER, CMD_RESET };
struct Command {
  uint8_t type;
  uint32_t turnId;
};
static QueueHandle_t cmdQueue;
static volatile uint32_t turnSeq = 0;     // id of the newest turn
static volatile uint32_t cancelSeq = 0;   // turns with id <= this are cancelled
static volatile bool recStop = false;     // finish recording and send
static volatile bool tapMode = false;     // auto-stop on silence
static volatile bool playStop = false;    // stop the voice reply
static volatile bool workerBusy = false;
static volatile bool serverSearchDone = false;
static volatile float micLevel = 0.f;
static volatile float speakLevel = 0.f;
static std::string detectedLang;

// Recording buffer: 44-byte WAV header followed by the samples, so the
// whole thing can be POSTed without copying.
static uint8_t* wavBuf = nullptr;
static int16_t* recSamples = nullptr;
static const size_t kMaxSamples = (size_t)MIC_SAMPLE_RATE * MAX_RECORD_SECONDS;

// ----------------------------------------------------------------------------
//  Helpers
// ----------------------------------------------------------------------------
static Mode currentMode() {
  ModelLock l;
  return model.mode;
}

static void setModeLocked(Mode m) {
  if (model.mode != m) {
    model.mode = m;
    model.modeSinceMs = millis();
  }
}

static void setMode(Mode m) {
  ModelLock l;
  setModeLocked(m);
}

static bool isCancelled(uint32_t id) { return id <= cancelSeq; }

static std::string upper(const std::string& s) {
  std::string o = s;
  for (auto& c : o) c = toupper((unsigned char)c);
  return o;
}

static void updateBadgeLocked() {
  const SpeechLang& sl = kSpeechLangs[settings.speechLang];
  if (settings.speechLang == 0)
    model.langBadge = detectedLang.empty() ? "AUTO" : "AUTO \xC2\xB7 " + upper(detectedLang);
  else
    model.langBadge = upper(sl.code);
}

static void showErrorLocked(const std::string& text) {
  model.errorText = text;
  setModeLocked(Mode::Error);
}

static void trimChatLocked() {
  while (model.chat.size() > CHAT_HISTORY_MAX) model.chat.erase(model.chat.begin());
}

static void writeWavHeader(uint8_t* h, uint32_t dataBytes, uint32_t rate) {
  auto w32 = [](uint8_t* p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
  };
  auto w16 = [](uint8_t* p, uint16_t v) {
    p[0] = v;
    p[1] = v >> 8;
  };
  memcpy(h, "RIFF", 4);
  w32(h + 4, 36 + dataBytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  w32(h + 16, 16);
  w16(h + 20, 1);  // PCM
  w16(h + 22, 1);  // mono
  w32(h + 24, rate);
  w32(h + 28, rate * 2);
  w16(h + 32, 2);
  w16(h + 34, 16);
  memcpy(h + 36, "data", 4);
  w32(h + 40, dataBytes);
}

static void loadSettings() {
  prefs.begin("geistai", true);
  settings.speechLang = prefs.getUChar("slang", 0);
  settings.uiLang = prefs.getUChar("ulang", DEFAULT_UI_LANG);
  settings.volume = prefs.getUChar("vol", DEFAULT_VOLUME);
  settings.voiceReplies = prefs.getBool("voice", true);
  prefs.end();
  if (settings.speechLang >= kSpeechLangCount) settings.speechLang = 0;
  if (settings.uiLang >= UI_LANG_COUNT) settings.uiLang = UI_EN;
}

static void saveSettings() {
  prefs.begin("geistai", false);
  prefs.putUChar("slang", settings.speechLang);
  prefs.putUChar("ulang", settings.uiLang);
  prefs.putUChar("vol", settings.volume);
  prefs.putBool("voice", settings.voiceReplies);
  prefs.end();
}

// ----------------------------------------------------------------------------
//  Worker task: recording, network and playback (keeps the UI fluid)
// ----------------------------------------------------------------------------
static void runTurn(uint32_t id) {
  {
    ModelLock l;
    if (isCancelled(id)) return;
    model.tapMode = tapMode;
    model.scroll = 0;
    setModeLocked(Mode::Listening);
  }

  // ---------------- record
  audio::micStart();
  size_t n = 0;
  float noise = -1.f;
  bool speech = false, noSpeech = false;
  uint32_t elapsedMs = 0, silenceMs = 0;
  double energy = 0;
  while (n < kMaxSamples) {
    float rms = 0;
    size_t want = kMaxSamples - n;
    if (want > 512) want = 512;
    size_t got = audio::micRead(recSamples + n, want, &rms);
    n += got;
    uint32_t chunkMs = got * 1000 / MIC_SAMPLE_RATE;
    elapsedMs += chunkMs;
    energy += (double)rms * got;
    float lvl = rms / 3000.f;
    micLevel = lvl > 1.f ? 1.f : lvl;
    {
      ModelLock l;
      model.micLevel = micLevel;
      model.tapMode = tapMode;
    }
    if (isCancelled(id) || recStop) break;
    if (tapMode) {
      if (noise < 0) noise = rms;
      if (!speech) noise = noise * 0.9f + rms * 0.1f;
      float thr = noise * 2.5f;
      if (thr < VAD_MIN_RMS) thr = VAD_MIN_RMS;
      if (rms > thr) {
        speech = true;
        silenceMs = 0;
      } else if (speech) {
        silenceMs += chunkMs;
      }
      if (speech && silenceMs >= VAD_SILENCE_END_MS) break;
      if (!speech && elapsedMs >= VAD_NO_SPEECH_MS) {
        noSpeech = true;
        break;
      }
    }
  }
  audio::micStop();
  micLevel = 0;
  if (isCancelled(id)) return;

  uint32_t durMs = n * 1000 / MIC_SAMPLE_RATE;
  float avgRms = n ? (float)(energy / n) : 0.f;
  Serial.printf("[turn %u] recorded %u ms, avg rms %.0f\n", id, durMs, avgRms);
  if (durMs < MIN_RECORD_MS || noSpeech || avgRms < VAD_MIN_RMS * 0.25f) {
    ModelLock l;
    showErrorLocked(tr(settings.uiLang, S_ERR_NOTHING_HEARD));
    return;
  }

  // ---------------- send
  {
    ModelLock l;
    model.chat.push_back(ChatMsg{true, "\xE2\x80\xA6"});  // placeholder "…"
    trimChatLocked();
    model.pendingReply = true;
    model.scroll = 0;
    setModeLocked(Mode::Thinking);
  }
  writeWavHeader(wavBuf, n * 2, MIC_SAMPLE_RATE);
  const char* lang = kSpeechLangs[settings.speechLang].code;
  net::ChatResult res = net::sendVoice(wavBuf, 44 + n * 2, lang, settings.voiceReplies);
  Serial.printf("[turn %u] http %d ok=%d lang=%s\n", id, res.httpCode, res.ok,
                res.language.c_str());

  {
    ModelLock l;
    model.pendingReply = false;
    // drop the placeholder bubble, we'll add the real transcript below
    if (!model.chat.empty() && model.chat.back().fromUser &&
        model.chat.back().text == "\xE2\x80\xA6")
      model.chat.pop_back();
    if (isCancelled(id)) return;

    model.serverOk = res.httpCode > 0;
    if (!res.ok) {
      if (res.error == "wifi")
        showErrorLocked(tr(settings.uiLang, S_ERR_WIFI));
      else if (res.error == "server" || res.httpCode <= 0)
        showErrorLocked(tr(settings.uiLang, S_ERR_SERVER));
      else if (res.error == "no_speech")
        showErrorLocked(tr(settings.uiLang, S_ERR_NOTHING_HEARD));
      else
        showErrorLocked(res.error.empty() ? tr(settings.uiLang, S_ERR_GENERIC) : res.error);
      return;
    }
    if (!res.transcript.empty()) model.chat.push_back(ChatMsg{true, res.transcript});
    model.chat.push_back(ChatMsg{false, res.reply});
    trimChatLocked();
    model.scroll = 0;
    if (!res.language.empty()) detectedLang = res.language;
    updateBadgeLocked();
  }

  // ---------------- speak
  if (settings.voiceReplies && !res.audioPath.empty()) {
    playStop = false;
    setMode(Mode::Speaking);
    net::playReply(res.audioPath, &playStop, &speakLevel);
  }
  ModelLock l;
  if (!isCancelled(id) && (model.mode == Mode::Speaking || model.mode == Mode::Thinking))
    setModeLocked(Mode::Idle);
}

static void workerTask(void*) {
  Command cmd;
  for (;;) {
    if (xQueueReceive(cmdQueue, &cmd, portMAX_DELAY) != pdTRUE) continue;
    workerBusy = true;
    switch (cmd.type) {
      case CMD_TURN:
        runTurn(cmd.turnId);
        break;
      case CMD_FIND_SERVER: {
        bool ok = net::wifiConnected() && net::findServer();
        {
          ModelLock l;
          model.serverOk = ok;
        }
        serverSearchDone = true;
        Serial.printf("[net] server %s %s\n", ok ? "found at" : "not found",
                      net::serverUrl().c_str());
        break;
      }
      case CMD_RESET:
        net::resetConversation();
        break;
    }
    workerBusy = false;
  }
}

static void sendCommand(uint8_t type, uint32_t id = 0) {
  Command c{type, id};
  xQueueSend(cmdQueue, &c, 0);
}

// ----------------------------------------------------------------------------
//  Settings screen
// ----------------------------------------------------------------------------
enum SettingRow : uint8_t {
  ROW_SPEECH_LANG,
  ROW_UI_LANG,
  ROW_VOLUME,
  ROW_VOICE,
  ROW_CLEAR,
  ROW_INFO,
  ROW_BACK,
  ROW_COUNT
};
static bool chatCleared = false;

static void refreshSettingsLocked() {
  uint8_t L = settings.uiLang;
  char vol[12];
  snprintf(vol, sizeof(vol), "%u %%", settings.volume);
  std::string info = net::wifiConnected() ? std::string(WiFi.localIP().toString().c_str()) : "-";
  model.uiLang = L;
  model.settings = {
      {tr(L, S_SPEECH_LANG), kSpeechLangs[settings.speechLang].label},
      {tr(L, S_UI_LANG), kUiLangNames[settings.uiLang]},
      {tr(L, S_VOLUME), vol},
      {tr(L, S_VOICE_REPLY), tr(L, settings.voiceReplies ? S_ON : S_OFF)},
      {tr(L, S_CLEAR_CHAT), chatCleared ? tr(L, S_DONE) : ""},
      {tr(L, S_DEVICE_INFO), info},
      {tr(L, S_BACK), ""},
  };
}

static void openSettings() {
  ModelLock l;
  chatCleared = false;
  model.settingsIndex = 0;
  model.settingsEditing = false;
  refreshSettingsLocked();
  setModeLocked(Mode::Settings);
}

static void closeSettings() {
  saveSettings();
  ModelLock l;
  model.settingsEditing = false;
  updateBadgeLocked();
  setModeLocked(Mode::Idle);
}

static void settingsRotate(int delta) {
  ModelLock l;
  if (!model.settingsEditing) {
    int i = model.settingsIndex + delta;
    if (i < 0) i = 0;
    if (i >= ROW_COUNT) i = ROW_COUNT - 1;
    model.settingsIndex = i;
    return;
  }
  switch (model.settingsIndex) {
    case ROW_SPEECH_LANG:
      settings.speechLang = (settings.speechLang + kSpeechLangCount + (delta > 0 ? 1 : -1)) %
                            kSpeechLangCount;
      break;
    case ROW_UI_LANG:
      settings.uiLang = (settings.uiLang + UI_LANG_COUNT + (delta > 0 ? 1 : -1)) % UI_LANG_COUNT;
      break;
    case ROW_VOLUME: {
      int v = settings.volume + delta * 5;
      settings.volume = v < 0 ? 0 : (v > 100 ? 100 : v);
      audio::setVolume(settings.volume);
      break;
    }
  }
  refreshSettingsLocked();
}

static void settingsClick() {
  bool close = false;
  {
    ModelLock l;
    switch (model.settingsIndex) {
      case ROW_SPEECH_LANG:
      case ROW_UI_LANG:
      case ROW_VOLUME:
        model.settingsEditing = !model.settingsEditing;
        break;
      case ROW_VOICE:
        settings.voiceReplies = !settings.voiceReplies;
        break;
      case ROW_CLEAR:
        model.chat.clear();
        model.scroll = 0;
        detectedLang.clear();
        chatCleared = true;
        sendCommand(CMD_RESET);
        break;
      case ROW_BACK:
        close = true;
        break;
      default:
        break;
    }
    refreshSettingsLocked();
  }
  if (close) closeSettings();
}

// ----------------------------------------------------------------------------
//  Input handling
// ----------------------------------------------------------------------------
static bool pressStartedTurn = false;
static bool ignoreNextUp = false;
static uint32_t lastTurnPressMs = 0;

static void startTurn() {
  recStop = false;
  tapMode = false;
  uint32_t id = ++turnSeq;
  sendCommand(CMD_TURN, id);
}

static void cancelTurn() { cancelSeq = turnSeq; }

static void onButtonDown(const input::Event& ev) {
  Mode m = currentMode();
  switch (m) {
    case Mode::Boot:
      return;
    case Mode::Settings:
      return;  // handled on release
    case Mode::Thinking: {
      cancelTurn();
      ModelLock l;
      model.pendingReply = false;
      if (!model.chat.empty() && model.chat.back().fromUser &&
          model.chat.back().text == "\xE2\x80\xA6")
        model.chat.pop_back();
      setModeLocked(Mode::Idle);
      return;
    }
    case Mode::Listening:
      if (tapMode) {
        if (ev.atMs - lastTurnPressMs < DOUBLE_CLICK_MS) {  // double tap -> settings
          cancelTurn();
          openSettings();
          ignoreNextUp = true;
        } else {
          recStop = true;  // second tap sends
        }
      }
      return;
    case Mode::Speaking:
      playStop = true;
      break;
    case Mode::Idle:
    case Mode::Error:
      if (workerBusy) return;  // still finishing a cancelled request
      break;
  }
  startTurn();
  lastTurnPressMs = ev.atMs;
  pressStartedTurn = true;
}

static void onButtonUp(const input::Event& ev) {
  if (ignoreNextUp) {
    ignoreNextUp = false;
    return;
  }
  Mode m = currentMode();
  if (m == Mode::Settings) {
    if (ev.heldMs >= 700)
      closeSettings();  // long press leaves the menu
    else
      settingsClick();
    return;
  }
  if (!pressStartedTurn) return;
  pressStartedTurn = false;
  if (ev.heldMs >= HOLD_THRESHOLD_MS) {
    recStop = true;  // push-to-talk released
  } else {
    tapMode = true;  // tap: keep listening until a pause
  }
}

static void onRotate(int delta) {
  Mode m = currentMode();
  if (m == Mode::Settings) return settingsRotate(delta);
  if (m == Mode::Boot || m == Mode::Listening) return;
  ModelLock l;
  int maxScroll = chatContentHeight(model) - chatViewportHeight();
  if (maxScroll < 0) maxScroll = 0;
  int s = model.scroll + delta * SCROLL_STEP_PX;  // clockwise scrolls back in time
  model.scroll = s < 0 ? 0 : (s > maxScroll ? maxScroll : s);
}

static void handleInput() {
  input::Event ev;
  while (input::poll(ev)) {
    switch (ev.type) {
      case input::EventType::Down:
        onButtonDown(ev);
        break;
      case input::EventType::Up:
        onButtonUp(ev);
        break;
      case input::EventType::Rotate:
        onRotate(ev.delta);
        break;
      case input::EventType::Back:
        if (currentMode() == Mode::Settings)
          closeSettings();
        else if (currentMode() != Mode::Boot)
          openSettings();
        break;
      default:
        break;
    }
  }
}

// ----------------------------------------------------------------------------
//  Rendering
// ----------------------------------------------------------------------------
static void drawFrame() {
  static bool first = true;
  if (first) {
    first = false;
    Serial.println("[ui] first frame");
  }
  {
    ModelLock l;
    model.timeMs = millis();
    model.speakLevel = speakLevel;
    if (model.mode != Mode::Listening) model.micLevel = 0;
    render(*canvas, model);
  }
  display::present();
}

static int readBatteryPct() {
#if PIN_BAT_ADC >= 0
  uint32_t mv = analogReadMilliVolts(PIN_BAT_ADC) * 2;  // 1:2 divider
  if (mv > 4350 || mv < 2500) return -1;               // USB only / no battery
  int pct = (int)((mv - 3300) * 100 / (4150 - 3300));
  return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
#else
  return -1;
#endif
}

// ----------------------------------------------------------------------------
//  Setup / boot screen
// ----------------------------------------------------------------------------
static void bootSequence() {
  const uint32_t start = millis();
  {
    ModelLock l;
    model.mode = Mode::Boot;
    model.modeSinceMs = start;
    model.version = GEIST_VERSION;
    model.bootStatus = tr(settings.uiLang, S_STARTING);
  }

  net::wifiBegin();
  enum { WIFI, SERVER, DONE } phase = WIFI;
  uint32_t phaseStart = millis();
  uint32_t lastFrame = 0;

  for (;;) {
    uint32_t now = millis();
    float progress = 0.f;
    const char* status = "";

    if (phase == WIFI) {
      status = tr(settings.uiLang, S_CONNECTING_WIFI);
      progress = 0.08f + 0.5f * (float)(now - phaseStart) / WIFI_CONNECT_TIMEOUT_MS;
      if (net::wifiConnected()) {
        phase = SERVER;
        phaseStart = now;
        serverSearchDone = false;
        sendCommand(CMD_FIND_SERVER);
      } else if (now - phaseStart > WIFI_CONNECT_TIMEOUT_MS) {
        phase = DONE;
      }
    } else if (phase == SERVER) {
      status = tr(settings.uiLang, S_CONNECTING_SERVER);
      progress = 0.6f + 0.35f * (float)(now - phaseStart) / 6000.f;
      if (serverSearchDone) phase = DONE;
    } else {
      status = tr(settings.uiLang, S_READY);
      progress = 1.f;
      if (now - start > 2800) break;  // let the logo animation finish
    }

    {
      ModelLock l;
      model.bootProgress = progress > 0.97f && phase != DONE ? 0.97f : progress;
      model.bootStatus = status;
      model.wifiOk = net::wifiConnected();
    }
    leds::update(Mode::Boot, 0, now);
    handleInput();  // drain early presses
    if (now - lastFrame >= 33) {
      lastFrame = now;
      drawFrame();
    }
    delay(2);
  }

  ModelLock l;
  model.wifiOk = net::wifiConnected();
  updateBadgeLocked();
  if (!model.wifiOk)
    showErrorLocked(tr(settings.uiLang, S_ERR_WIFI));
  else if (!model.serverOk)
    showErrorLocked(tr(settings.uiLang, S_ERR_SERVER));
  else
    setModeLocked(Mode::Idle);
}

void setup() {
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  Serial.begin(115200);
  // give the USB serial monitor a moment to attach so the boot log isn't lost
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 1500;) delay(10);
  Serial.printf("\nGeistAI %s on %s\n", GEIST_VERSION, BOARD_NAME);
  Serial.printf("PSRAM %u KB free, heap %u KB free\n", (unsigned)(ESP.getFreePsram() / 1024),
                (unsigned)(ESP.getFreeHeap() / 1024));

  modelMutex = xSemaphoreCreateMutex();
  cmdQueue = xQueueCreate(4, sizeof(Command));

  loadSettings();
  model.uiLang = settings.uiLang;

  // Each step is printed on the screen as well, so a hang is easy to locate.
  bool fbOk = display::begin();
  display::status("1/6 display ok");
  if (!fbOk) {
    display::status("ERROR: no memory for the screen buffer (PSRAM?)");
    for (;;) delay(1000);
  }
  canvas = new Canvas(display::framebuffer(), SCREEN_W, SCREEN_H);

  display::status("2/6 leds");
  leds::begin();
  display::status("3/6 input");
  input::begin();

  display::status("4/6 memory");
  wavBuf = (uint8_t*)heap_caps_malloc(44 + kMaxSamples * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!wavBuf) {
    display::status("ERROR: no PSRAM for the recording buffer");
    for (;;) delay(1000);
  }
  recSamples = (int16_t*)(wavBuf + 44);

  display::status("5/6 audio");
  audio::begin();
  audio::setVolume(settings.volume);

  display::status("6/6 starting");
  // The worker runs on the other core so drawing never stutters.
  BaseType_t workerCore = xPortGetCoreID() == 0 ? 1 : 0;
  xTaskCreatePinnedToCore(workerTask, "worker", 12288, nullptr, 2, nullptr, workerCore);

  bootSequence();
}

void loop() {
  static uint32_t lastFrame = 0, lastPoll = 0;
  uint32_t now = millis();

  handleInput();

  // Leave the error message after a few seconds.
  {
    ModelLock l;
    if (model.mode == Mode::Error && now - model.modeSinceMs > 4500) setModeLocked(Mode::Idle);
  }

  // Background housekeeping every 10 s
  if (now - lastPoll > 10000) {
    lastPoll = now;
    bool wifi = net::wifiConnected();
    int bat = readBatteryPct();
    bool needServer;
    {
      ModelLock l;
      model.wifiOk = wifi;
      model.batteryPct = bat;
      needServer = wifi && !model.serverOk;
    }
    if (needServer && !workerBusy) sendCommand(CMD_FIND_SERVER);
  }

  Mode m = currentMode();
  float level = m == Mode::Listening ? micLevel : speakLevel;
  leds::update(m, level, now);

  static uint32_t lastBeat = 0;
  if (now - lastBeat > 5000) {
    lastBeat = now;
    Serial.printf("[alive] mode=%d wifi=%d heap=%uKB psram=%uKB\n", (int)m, net::wifiConnected(),
                  (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));
  }

  if (now - lastFrame >= 40) {  // ~25 fps
    lastFrame = now;
    drawFrame();
  }
  delay(1);
}
