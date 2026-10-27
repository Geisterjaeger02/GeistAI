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
#include <ArduinoJson.h>
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
#include "keyboard.h"
#include "leds.h"
#include "net.h"
#include "power.h"
#include "provision.h"
#include "games/games.h"
#include "ui.h"

using namespace geist;

// Chess thinks a few levels deep; give the main loop a roomier stack.
#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

static_assert(THEME_NAME_COUNT == THEME_COUNT, "theme names in tools/gen_i18n.py");

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
  bool voiceReplies = BOARD_HAS_SPEAKER;
  uint8_t theme = 0;       // colour theme (index into geist::kThemes)
  std::string nickname;    // the assistant's name, empty = "GeistAI"
  bool cloud = false;      // answer with ChatGPT (needs an OpenAI key on the server)
} settings;

constexpr size_t NICKNAME_MAX = 20;  // bytes (UTF-8)

static std::string assistantName() {
  return settings.nickname.empty() ? std::string("GeistAI") : settings.nickname;
}

static Preferences prefs;

// Worker <-> UI signalling
enum : uint8_t { CMD_TURN = 1, CMD_TEXT, CMD_FIND_SERVER, CMD_RESET, CMD_VOICES, CMD_VOICE_PLAY,
                  CMD_VOICE_SET, CMD_SPEED };
struct Command {
  uint8_t type;
  uint32_t turnId;
};
static QueueHandle_t cmdQueue;
static volatile uint32_t turnSeq = 0;     // id of the newest turn
static volatile uint32_t cancelSeq = 0;   // turns with id <= this are cancelled
static volatile bool recStop = false;     // finish recording and send
static volatile bool tapMode = false;     // auto-stop on silence (currently unused)
static volatile bool turnVisible = false; // the press became a long press -> show "listening"
static std::string pendingText;           // typed message waiting for the worker
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

static std::string serverErrorText() {
  // e.g. "Server not reachable · 192.168.178.42:8765 · read Timeout"
  std::string t = tr(settings.uiLang, S_ERR_SERVER);
  t += " \xC2\xB7 " + net::serverHost();
  std::string d = net::lastErrorDetail();
  if (!d.empty()) t += " \xC2\xB7 " + d;
  // our own address: the first three numbers must match the PC's
  if (net::wifiConnected()) t += std::string("\n" BOARD_NAME ": ") + WiFi.localIP().toString().c_str();
  return t;
}

// "No Wi-Fi connection · Settings → Wi-Fi setup"
static std::string wifiErrorText() {
  uint8_t L = settings.uiLang;
  return std::string(tr(L, S_ERR_WIFI)) + "\n" + tr(L, S_SETTINGS) + " \xE2\x86\x92 " +
         tr(L, S_WIFI_SETUP);
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
  settings.voiceReplies = prefs.getBool("voice", BOARD_HAS_SPEAKER);
  settings.theme = prefs.getUChar("theme", 0);
  settings.nickname = prefs.isKey("nick") ? prefs.getString("nick", "").c_str() : "";
  settings.cloud = prefs.getBool("cloud", false);
  prefs.end();
  if (settings.theme >= THEME_COUNT) settings.theme = 0;
  prefs.begin("geistai", true);
  for (int g = 0; g < games::COUNT; g++) {
    char k[8];
    snprintf(k, sizeof(k), "hs%d", g);
    games::setBestScore(g, prefs.getInt(k, 0));
  }
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
  prefs.putUChar("theme", settings.theme);
  prefs.putString("nick", settings.nickname.c_str());
  prefs.putBool("cloud", settings.cloud);
  prefs.end();
  net::setAssistant(settings.cloud, settings.nickname);
}

// ----------------------------------------------------------------------------
//  Worker task: recording, network and playback (keeps the UI fluid)
// ----------------------------------------------------------------------------
static void deliver(uint32_t id, const net::ChatResult& res, const std::string* typed);

// Without PSRAM the first moments of a press are kept here until we know it
// is a long press; then the stream to the server is opened and this is sent.
static int16_t* prerollBuf = nullptr;
static const size_t kPrerollSamples = MIC_SAMPLE_RATE * 6 / 10;  // 0.6 s

static void runTurn(uint32_t id) {
  if (isCancelled(id)) return;
  bool shown = false;  // "listening" is only shown once the press is a long press
  const bool streaming = recSamples == nullptr;
  const char* lang = kSpeechLangs[settings.speechLang].code;
  bool streamOpen = false, streamFailed = false;
  size_t prerollN = 0;
  static int16_t chunk[512];
  if (streaming && !prerollBuf) prerollBuf = (int16_t*)malloc(kPrerollSamples * sizeof(int16_t));

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
    int16_t* dst = streaming ? chunk : recSamples + n;
    size_t got = audio::micRead(dst, want, &rms);
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
      if (!shown && turnVisible && !isCancelled(id)) {
        shown = true;
        model.scroll = 0;
        setModeLocked(Mode::Listening);
      }
    }
    if (streaming && !streamFailed) {
      if (!streamOpen && shown) {  // long press confirmed: open the stream, send what we have
        streamOpen = net::streamBegin(lang, settings.voiceReplies);
        if (streamOpen && prerollN) streamOpen = net::streamWrite(prerollBuf, prerollN);
        if (!streamOpen) streamFailed = true;
      }
      if (streamOpen) {
        if (!net::streamWrite(chunk, got)) {
          streamOpen = false;
          streamFailed = true;
        }
      } else if (!streamFailed && prerollBuf) {
        size_t room = kPrerollSamples - prerollN;
        size_t take = got < room ? got : room;
        memcpy(prerollBuf + prerollN, chunk, take * sizeof(int16_t));
        prerollN += take;
      }
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
  if (isCancelled(id) || !shown) {
    if (streamOpen) net::streamAbort();
    return;
  }

  uint32_t durMs = n * 1000 / MIC_SAMPLE_RATE;
  float avgRms = n ? (float)(energy / n) : 0.f;
  Serial.printf("[turn %u] recorded %u ms, avg rms %.0f%s\n", id, durMs, avgRms,
                streaming ? " (streamed)" : "");
  if (durMs < MIN_RECORD_MS || noSpeech || avgRms < VAD_MIN_RMS * 0.25f) {
    if (streamOpen) net::streamAbort();
    ModelLock l;
    showErrorLocked(tr(settings.uiLang, S_ERR_NOTHING_HEARD));
    return;
  }
  if (streaming && !streamOpen) {  // could not reach the server while recording
    ModelLock l;
    showErrorLocked(serverErrorText());
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
  net::ChatResult res;
  if (streaming) {
    res = net::streamFinish();
  } else {
    writeWavHeader(wavBuf, n * 2, MIC_SAMPLE_RATE);
    res = net::sendVoice(wavBuf, 44 + n * 2, lang, settings.voiceReplies);
  }
  Serial.printf("[turn %u] http %d ok=%d lang=%s\n", id, res.httpCode, res.ok,
                res.language.c_str());

  deliver(id, res, nullptr);
}

// Shows the server's answer (or error) and speaks it. `typed` is the message
// for typed turns (its bubble is already shown), nullptr for voice turns.
static void deliver(uint32_t id, const net::ChatResult& res, const std::string* typed) {
  {
    ModelLock l;
    model.pendingReply = false;
    // drop the placeholder bubble, we'll add the real transcript below
    if (!typed && !model.chat.empty() && model.chat.back().fromUser &&
        model.chat.back().text == "\xE2\x80\xA6")
      model.chat.pop_back();
    if (isCancelled(id)) return;

    model.serverOk = res.httpCode > 0;
    if (!res.ok) {
      if (typed) {  // give the text back so it isn't lost
        if (!model.chat.empty() && model.chat.back().fromUser) model.chat.pop_back();
        model.kbText = *typed;
      }
      if (res.error == "wifi")
        showErrorLocked(wifiErrorText());
      else if (res.error == "server" || res.httpCode <= 0)
        showErrorLocked(serverErrorText());
      else if (res.error == "no_speech")
        showErrorLocked(tr(settings.uiLang, S_ERR_NOTHING_HEARD));
      else
        showErrorLocked(res.error.empty() ? tr(settings.uiLang, S_ERR_GENERIC) : res.error);
      return;
    }
    if (!typed && !res.transcript.empty()) model.chat.push_back(ChatMsg{true, res.transcript});
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

static void runTextTurn(uint32_t id) {
  std::string text;
  {
    ModelLock l;
    text = pendingText;
    pendingText.clear();
    if (isCancelled(id) || text.empty()) return;
    model.chat.push_back(ChatMsg{true, text});
    trimChatLocked();
    model.pendingReply = true;
    model.scroll = 0;
    setModeLocked(Mode::Thinking);
  }
  const char* lang = kSpeechLangs[settings.speechLang].code;
  net::ChatResult res = net::sendText(text, lang, settings.voiceReplies);
  Serial.printf("[text %u] http %d ok=%d lang=%s\n", id, res.httpCode, res.ok,
                res.language.c_str());
  deliver(id, res, &text);
}

// ----------------------------------------------------------------------------
//  Voice menu: pick and preview the server's voices on the device
// ----------------------------------------------------------------------------
struct VoiceOpt {
  std::string id, label;
  uint8_t engine;  // 0 local (Piper), 1 Microsoft, 2 OpenAI
};
static std::vector<VoiceOpt> voiceOpts;    // guarded by ModelLock
static std::vector<int> voiceRowRef;       // voice row -> index in voiceOpts, -1 otherwise
static std::string voiceCurrent;           // "engine:name" in use for voiceLang
static uint8_t voiceLang = 1;              // index into kSpeechLangs (never 0 = auto)
static uint8_t voiceLangLoaded = 0;
static int voiceSpeedSel = 1;              // 0 slower, 1 normal, 2 faster
static int voicePreviewed = -1;            // row that was played last
static std::string pendingVoice;           // for CMD_VOICE_PLAY / CMD_VOICE_SET
static int pendingVoiceRow = -1;
static const float kSpeeds[3] = {1.2f, 1.0f, 0.85f};  // Piper length scale
enum : int { VROW_LANG = 0, VROW_SPEED = 1 };

static void refreshVoiceRowsLocked() {
  uint8_t L = settings.uiLang;
  static const Str speedNames[3] = {S_SPEED_SLOW, S_SPEED_NORMAL, S_SPEED_FAST};
  static const Str groups[3] = {S_VOICES_LOCAL, S_VOICES_MS, S_VOICES_OPENAI};
  model.voiceRows.clear();
  voiceRowRef.clear();
  model.voiceRows.push_back({tr(L, S_LANGUAGE), kSpeechLangs[voiceLang].label});
  model.voiceRows.push_back({tr(L, S_SPEED), tr(L, speedNames[voiceSpeedSel])});
  voiceRowRef = {-1, -1};
  for (uint8_t g = 0; g < 3; g++) {
    bool any = false;
    for (size_t i = 0; i < voiceOpts.size(); i++) {
      if (voiceOpts[i].engine != g) continue;
      if (!any) {
        SettingsRow head{tr(L, groups[g]), ""};
        head.section = true;
        model.voiceRows.push_back(head);
        voiceRowRef.push_back(-1);
        any = true;
      }
      SettingsRow r{voiceOpts[i].label, ""};
      r.current = voiceOpts[i].id == voiceCurrent;
      model.voiceRows.push_back(r);
      voiceRowRef.push_back((int)i);
    }
  }
  if (model.voiceIndex >= (int)model.voiceRows.size()) model.voiceIndex = 0;
}

// Removes the ♀ ♂ signs (not in the device font) from Microsoft voice names.
static std::string cleanLabel(std::string s) {
  for (const char* sym : {" \xE2\x99\x80", " \xE2\x99\x82", "\xE2\x99\x80", "\xE2\x99\x82"}) {
    size_t p;
    while ((p = s.find(sym)) != std::string::npos) s.erase(p, strlen(sym));
  }
  return s;
}

static void loadVoices() {
  std::string lang = kSpeechLangs[voiceLang].code;
  std::vector<VoiceOpt> opts;
  std::string current;
  int speedSel = voiceSpeedSel;
  bool ok = false;
  {
    JsonDocument ov;
    if (!deserializeJson(ov, net::getText("/api/voices"))) {
      float sp = ov["speed"] | 1.0f;
      speedSel = sp > 1.1f ? 0 : (sp < 0.95f ? 2 : 1);
    }
  }
  {
    JsonDocument doc;
    std::string body = net::getText("/api/voices/" + lang, 30000);
    if (!body.empty() && !deserializeJson(doc, body)) {
      ok = true;
      current = (const char*)(doc["current"] | "");
      for (JsonObject o : doc["options"].as<JsonArray>()) {
        std::string eng = (const char*)(o["engine"] | "piper");
        opts.push_back({(const char*)(o["id"] | ""), cleanLabel((const char*)(o["label"] | "")),
                        (uint8_t)(eng == "edge" ? 1 : (eng == "openai" ? 2 : 0))});
      }
    }
  }
  ModelLock l;
  voiceOpts = opts;
  voiceCurrent = current;
  voiceSpeedSel = speedSel;
  voiceLangLoaded = voiceLang;
  voicePreviewed = -1;
  refreshVoiceRowsLocked();
  model.voiceStatus = ok ? "" : serverErrorText();
}

static void playVoice() {
  std::string lang = kSpeechLangs[voiceLang].code;
  {
    ModelLock l;
    model.voiceStatus = tr(settings.uiLang, S_LOADING);
    model.voicePlaying = pendingVoiceRow;
  }
  playStop = false;
  bool ok = net::playReply("/api/voices/" + lang + "/preview?voice=" + net::urlEncode(pendingVoice),
                           &playStop, &speakLevel, 65000);
  speakLevel = 0.f;
  ModelLock l;
  model.voicePlaying = -1;
  model.voiceStatus = ok ? "" : tr(settings.uiLang, S_VOICE_RETRY);
  if (ok) voicePreviewed = pendingVoiceRow;
}

static void setVoice() {
  std::string lang = kSpeechLangs[voiceLang].code;
  bool ok = net::postJson("/api/voices/" + lang, "{\"voice\":\"" + pendingVoice + "\"}");
  ModelLock l;
  if (ok) voiceCurrent = pendingVoice;
  refreshVoiceRowsLocked();
  model.voiceStatus = ok ? tr(settings.uiLang, S_SAVED) : serverErrorText();
}

static void setSpeed() {
  char body[32];
  snprintf(body, sizeof(body), "{\"scale\":%.2f}", kSpeeds[voiceSpeedSel]);
  bool ok = net::postJson("/api/speed", body);
  ModelLock l;
  model.voiceStatus = ok ? tr(settings.uiLang, S_SAVED) : serverErrorText();
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
      case CMD_TEXT:
        runTextTurn(cmd.turnId);
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
      case CMD_VOICES:
        loadVoices();
        break;
      case CMD_VOICE_PLAY:
        playVoice();
        break;
      case CMD_VOICE_SET:
        setVoice();
        break;
      case CMD_SPEED:
        setSpeed();
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
  ROW_GAMES,
  ROW_SPEECH_LANG,
  ROW_UI_LANG,
  ROW_VOLUME,
  ROW_VOICE,
  ROW_VOICE_CHOICE,
  ROW_THEME,
  ROW_NAME,
  ROW_AI,
  ROW_CLEAR,
  ROW_WIFI,
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
  std::string wifiName = provision::hasWifi() ? provision::creds().ssid : "-";
  if (wifiName.size() > 14) wifiName = wifiName.substr(0, 13) + "\xE2\x80\xA6";
  std::string aiValue = tr(L, settings.cloud ? S_AI_CLOUD : S_AI_LOCAL);
  if (settings.cloud && model.serverOk && !net::cloudAvailable())
    aiValue += std::string(" (") + tr(L, S_NOT_SET_UP) + ")";
  model.uiLang = L;
  model.settings = {
      {tr(L, S_GAMES), "\xE2\x80\xBA"},
      {tr(L, S_SPEECH_LANG), kSpeechLangs[settings.speechLang].label},
      {tr(L, S_UI_LANG), kUiLangNames[settings.uiLang]},
      {tr(L, S_VOLUME), vol},
      {tr(L, S_VOICE_REPLY), tr(L, settings.voiceReplies ? S_ON : S_OFF)},
      {tr(L, S_VOICE), "\xE2\x80\xBA"},
      {tr(L, S_COLOR), kThemeNames[L][settings.theme]},
      {tr(L, S_AI_NAME), assistantName()},
      {tr(L, S_AI_BRAIN), aiValue},
      {tr(L, S_CLEAR_CHAT), chatCleared ? tr(L, S_DONE) : ""},
      {tr(L, S_WIFI_SETUP), wifiName},
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
    case ROW_THEME:  // the whole screen changes colour while you turn
      settings.theme = (settings.theme + THEME_COUNT + (delta > 0 ? 1 : -1)) % THEME_COUNT;
      setTheme(settings.theme);
      break;
  }
  refreshSettingsLocked();
}

static void startSetup();
static void editName();

static const char* const kUiLangCodes[UI_LANG_COUNT] = {"en", "de", "es", "fr", "it",
                                                       "pt", "nl", "pl", "tr", "ru"};

static void openVoices() {
  // start with the language GeistAI speaks right now
  std::string code = settings.speechLang ? kSpeechLangs[settings.speechLang].code
                     : !detectedLang.empty() ? detectedLang
                                             : kUiLangCodes[settings.uiLang];
  for (uint8_t i = 1; i < kSpeechLangCount; i++)
    if (code == kSpeechLangs[i].code) voiceLang = i;
  ModelLock l;
  model.voiceIndex = 0;
  model.voiceEditing = false;
  model.voicePlaying = -1;
  voiceOpts.clear();
  refreshVoiceRowsLocked();
  model.voiceStatus = tr(settings.uiLang, S_LOADING);
  setModeLocked(Mode::Voices);
  sendCommand(CMD_VOICES);
}

static void closeVoices() {
  playStop = true;
  ModelLock l;
  model.voiceEditing = false;
  refreshSettingsLocked();
  model.settingsIndex = ROW_VOICE_CHOICE;
  setModeLocked(Mode::Settings);
}

static void voicesRotate(int delta) {
  ModelLock l;
  if (model.voiceEditing && model.voiceIndex == VROW_LANG) {
    int n = kSpeechLangCount - 1;  // without "auto"
    voiceLang = 1 + ((voiceLang - 1 + (delta > 0 ? 1 : -1)) % n + n) % n;
  } else if (model.voiceEditing && model.voiceIndex == VROW_SPEED) {
    int v = voiceSpeedSel + (delta > 0 ? 1 : -1);
    voiceSpeedSel = v < 0 ? 0 : (v > 2 ? 2 : v);
  } else {
    int n = (int)model.voiceRows.size(), i = model.voiceIndex, step = delta > 0 ? 1 : -1;
    for (int k = 0; k < (delta > 0 ? delta : -delta); k++) {
      int j = i + step;
      while (j >= 0 && j < n && model.voiceRows[j].section) j += step;  // skip headings
      if (j >= 0 && j < n) i = j;
    }
    model.voiceIndex = i;
  }
  refreshVoiceRowsLocked();
}

static void voicesClick() {
  ModelLock l;
  int i = model.voiceIndex;
  if (i == VROW_LANG || i == VROW_SPEED) {
    model.voiceEditing = !model.voiceEditing;
    if (!model.voiceEditing) {
      if (i == VROW_LANG && voiceLang != voiceLangLoaded) {
        voiceOpts.clear();
        refreshVoiceRowsLocked();
        model.voiceStatus = tr(settings.uiLang, S_LOADING);
        sendCommand(CMD_VOICES);
      }
      if (i == VROW_SPEED) sendCommand(CMD_SPEED);
    }
    return;
  }
  if (i < 0 || i >= (int)voiceRowRef.size() || voiceRowRef[i] < 0) return;
  if (model.voicePlaying == i) {  // pressed while it speaks: stop
    playStop = true;
    return;
  }
  pendingVoice = voiceOpts[voiceRowRef[i]].id;
  pendingVoiceRow = i;
  if (voicePreviewed == i || pendingVoice == voiceCurrent) {
    sendCommand(CMD_VOICE_SET);  // second press: use this voice
  } else {
    sendCommand(CMD_VOICE_PLAY);  // first press: listen
  }
}

static void openGames() {
  saveSettings();
  games::openMenu();
  ModelLock l;
  setModeLocked(Mode::Games);
}

static void settingsClick() {
  bool close = false, setup = false, name = false, play = false, voices = false;
  {
    ModelLock l;
    switch (model.settingsIndex) {
      case ROW_SPEECH_LANG:
      case ROW_UI_LANG:
      case ROW_VOLUME:
      case ROW_THEME:
        model.settingsEditing = !model.settingsEditing;
        break;
      case ROW_NAME:
        name = true;
        break;
      case ROW_GAMES:
        play = true;
        break;
      case ROW_VOICE_CHOICE:
        voices = true;
        break;
      case ROW_AI:
        settings.cloud = !settings.cloud;
        net::setAssistant(settings.cloud, settings.nickname);
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
      case ROW_WIFI:
        setup = true;
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
  if (name) editName();
  if (play) openGames();
  if (voices) openVoices();
  if (setup) {
    saveSettings();
    startSetup();
  }
}

// ----------------------------------------------------------------------------
//  Wi-Fi setup (own hotspot + setup page; the web flasher works any time)
// ----------------------------------------------------------------------------
static uint32_t setupSavedAt = 0;
static void drawFrame();

static void startSetup() {
  {
    ModelLock l;
    model.setupSaved = false;
    model.setupSsid = "\xE2\x80\xA6";
    model.setupPass = "";
    model.setupUrl = "";
    model.wifiOk = false;
    model.serverOk = false;
    setModeLocked(Mode::Setup);
  }
  drawFrame();
  provision::portalStart(settings.uiLang);  // scans for a few seconds
  ModelLock l;
  model.setupSsid = provision::portalSsid();
  model.setupPass = provision::portalPass();
  model.setupUrl = provision::portalUrl();
}

static void cancelSetup() {
  provision::portalStop();
  net::wifiBegin();  // back to the saved network (if any)
  ModelLock l;
  setModeLocked(Mode::Idle);
}

// Called every loop.
static void updateProvisioning(uint32_t now) {
  provision::improvPoll();
  if (provision::improvJustProvisioned()) {
    // Wi-Fi was set up from the web flasher over USB
    if (provision::portalActive()) provision::portalStop();
    ModelLock l;
    model.wifiOk = true;
    if (model.mode == Mode::Setup || model.mode == Mode::Error) setModeLocked(Mode::Idle);
    if (!workerBusy) sendCommand(CMD_FIND_SERVER);
  }
  if (!provision::portalActive()) return;
  provision::portalPoll();
  if (provision::portalSaved() && !setupSavedAt) {
    setupSavedAt = now;
    ModelLock l;
    model.setupSaved = true;
  }
  if (setupSavedAt && now - setupSavedAt > 2500) {  // let the phone get its answer
    Serial.println("[setup] restarting with the new Wi-Fi settings");
    delay(100);
    ESP.restart();
  }
}

// ----------------------------------------------------------------------------
//  Input handling
// ----------------------------------------------------------------------------
static bool pressStartedTurn = false;  // this press is recording (may become push-to-talk)
static uint32_t pressAt = 0;
static bool tapOnly = false;           // worker busy: a tap can still open the keyboard
static bool kbPressPending = false;    // keyboard: press not yet decided (key vs. talk)
static uint32_t kbPressAt = 0;
static uint32_t backDownAt = 0;  // back/power button press time, 0 = not pressed
static bool ignoreNextUp = false;

static void startTurn() {
  recStop = false;
  tapMode = false;
  turnVisible = false;
  uint32_t id = ++turnSeq;
  sendCommand(CMD_TURN, id);
}

static void cancelTurn() { cancelSeq = turnSeq; }

// ---------------------------------------------------------------- keyboard
static std::string draftStash;  // the unsent message while the name is being edited

// Settings -> AI name: the keyboard edits the name instead of a message.
static void editName() {
  ModelLock l;
  draftStash = model.kbText;
  model.kbText = settings.nickname;
  model.kbNameMode = true;
  model.kbShift = model.kbText.empty();
  model.kbPage = kb::LETTERS;
  if (model.kbIndex < 0 || model.kbIndex >= kb::keyCount()) model.kbIndex = 0;
  setModeLocked(Mode::Keyboard);
}

// Leaves name editing; save = keep the typed name. Call with the model locked.
static void finishNameLocked(bool save) {
  if (save) {
    std::string n = model.kbText;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(0, 1);
    while (n.size() > NICKNAME_MAX) {  // cut on a character boundary
      n.pop_back();
      while (!n.empty() && ((uint8_t)n.back() & 0xC0) == 0x80) n.pop_back();
      if (!n.empty() && ((uint8_t)n.back() & 0x80)) n.pop_back();
    }
    settings.nickname = n == "GeistAI" ? "" : n;
    model.assistantName = assistantName();
    net::setAssistant(settings.cloud, settings.nickname);
  }
  model.kbText = draftStash;
  draftStash.clear();
  model.kbNameMode = false;
  refreshSettingsLocked();
  model.settingsIndex = ROW_NAME;
  setModeLocked(Mode::Settings);
}

static void openKeyboard() {
  ModelLock l;
  if (model.kbText.empty() && model.kbPage == kb::LETTERS && settings.uiLang == UI_RU)
    model.kbPage = kb::CYRILLIC;  // Russian menu: start on the Cyrillic keys
  model.kbShift = model.kbText.empty();  // capital letter at the start
  if (model.kbIndex < 0 || model.kbIndex >= kb::keyCount()) model.kbIndex = 0;
  setModeLocked(Mode::Keyboard);
}

static void popLastChar(std::string& s) {
  while (!s.empty()) {
    uint8_t c = (uint8_t)s.back();
    s.pop_back();
    if ((c & 0xC0) != 0x80) break;  // stop at the first byte of the character
  }
}

static void keyboardPress() {
  bool toSettings = false, send = false;
  {
    ModelLock l;
    const kb::Key& k = kb::key(model.kbPage, model.kbIndex);
    std::string& t = model.kbText;
    switch (k.action) {
      case kb::CHAR:
        if (t.size() < (model.kbNameMode ? NICKNAME_MAX : (size_t)kb::TEXT_MAX))
          t += model.kbShift ? k.upper : k.lower;
        model.kbShift = false;
        break;
      case kb::SPACE:
        if (t.size() < (size_t)kb::TEXT_MAX && !t.empty() && t.back() != ' ') t += ' ';
        break;
      case kb::SHIFT:
        model.kbShift = !model.kbShift;
        break;
      case kb::PAGE:
        model.kbPage = (model.kbPage + 1) % kb::PAGE_COUNT;
        break;
      case kb::BACKSPACE:
        popLastChar(t);
        if (t.empty()) model.kbShift = true;
        break;
      case kb::SETTINGS:
        if (model.kbNameMode) finishNameLocked(false);
        else toSettings = true;
        break;
      case kb::CLOSE:
        if (model.kbNameMode) finishNameLocked(false);
        else setModeLocked(Mode::Idle);
        break;
      case kb::SEND: {
        if (model.kbNameMode) {
          finishNameLocked(true);
          break;
        }
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (t.empty()) break;
        pendingText = t;
        t.clear();
        model.kbPage = kb::LETTERS;
        send = true;
        break;
      }
    }
  }
  if (toSettings) openSettings();
  if (send) sendCommand(CMD_TEXT, ++turnSeq);
}

// ---------------------------------------------------------------- button
static void onButtonDown(const input::Event& ev) {
  Mode m = currentMode();
  switch (m) {
    case Mode::Boot:
    case Mode::Off:
    case Mode::Games:  // handled in gameInput()
      return;
    case Mode::Settings:
    case Mode::Setup:
    case Mode::Voices:
      return;  // handled on release
    case Mode::Keyboard:
      // decided later: a tap types the key, holding switches to talking
      kbPressPending = true;
      kbPressAt = ev.atMs;
      return;
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
      return;  // releasing the button sends
    case Mode::Speaking:
      playStop = true;
      break;
    case Mode::Idle:
    case Mode::Error:
      if (workerBusy) {  // still finishing a cancelled request
        tapOnly = true;
        return;
      }
      break;
  }
  // Start recording right away so the first word isn't cut off. If it turns
  // out to be a short tap, the recording is dropped and the keyboard opens.
  startTurn();
  pressAt = ev.atMs;
  pressStartedTurn = true;
}

static void onButtonUp(const input::Event& ev) {
  if (ignoreNextUp) {
    ignoreNextUp = false;
    return;
  }
  if (pressStartedTurn) {
    pressStartedTurn = false;
    if (turnVisible) {
      recStop = true;  // push-to-talk released: send
    } else {
      cancelTurn();  // just a tap: open the keyboard instead
      openKeyboard();
    }
    return;
  }
  if (tapOnly) {
    tapOnly = false;
    if (ev.heldMs < HOLD_THRESHOLD_MS) openKeyboard();
    return;
  }
  Mode m = currentMode();
  if (m == Mode::Setup) {
    if (!setupSavedAt) cancelSetup();
    return;
  }
  if (m == Mode::Voices) {
    if (ev.heldMs >= 700) closeVoices();
    else voicesClick();
    return;
  }
  if (m == Mode::Settings) {
    if (ev.heldMs >= 700)
      closeSettings();  // long press leaves the menu
    else
      settingsClick();
    return;
  }
  if (m == Mode::Keyboard && kbPressPending) {
    kbPressPending = false;
    keyboardPress();
  }
}

// ---------------------------------------------------------------- physical keyboard
// (Cardputer). Typing opens the message editor right away.
static void sendTypedText() {
  {
    ModelLock l;
    if (model.kbNameMode) {
      finishNameLocked(true);
      return;
    }
    std::string& t = model.kbText;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (t.empty()) return;
    pendingText = t;
    t.clear();
  }
  sendCommand(CMD_TEXT, ++turnSeq);
}

static void onChar(const char* text) {
  Mode m = currentMode();
  if (m == Mode::Speaking) {
    playStop = true;
  } else if (m != Mode::Idle && m != Mode::Error && m != Mode::Keyboard) {
    return;
  }
  if (m != Mode::Keyboard) openKeyboard();
  ModelLock l;
  if (model.kbText.size() + strlen(text) <= (size_t)kb::TEXT_MAX) model.kbText += text;
}

static void onEnter() {
  Mode m = currentMode();
  if (m == Mode::Keyboard) {
    sendTypedText();
  } else if (m == Mode::Settings) {
    settingsClick();
  } else if (m == Mode::Setup) {
    if (!setupSavedAt) cancelSetup();
  } else if (m == Mode::Voices) {
    voicesClick();
  } else if (m == Mode::Error) {
    setMode(Mode::Idle);
  }
}

static void onBackspace() {
  if (currentMode() != Mode::Keyboard) return;
  ModelLock l;
  popLastChar(model.kbText);
}

static void onEscape() {
  Mode m = currentMode();
  if (m == Mode::Settings)
    closeSettings();
  else if (m == Mode::Voices)
    closeVoices();
  else if (m == Mode::Setup && !setupSavedAt)
    cancelSetup();
  else if (m == Mode::Keyboard && model.kbNameMode) {
    ModelLock l;
    finishNameLocked(false);
  } else if (m == Mode::Keyboard || m == Mode::Error)
    setMode(Mode::Idle);
  else if (m == Mode::Speaking)
    playStop = true;
}

// Called every loop: turns a held press into push-to-talk.
static void updateHold(uint32_t now) {
  bool down = input::buttonIsDown();
  if (pressStartedTurn && !turnVisible && down && now - pressAt >= HOLD_THRESHOLD_MS)
    turnVisible = true;  // the worker now shows "listening"
  if (kbPressPending && down && now - kbPressAt >= KB_HOLD_TO_TALK_MS && !model.kbNameMode) {
    kbPressPending = false;  // held in the keyboard: talk instead (recording starts now)
    startTurn();
    turnVisible = true;
    pressAt = kbPressAt;
    pressStartedTurn = true;
  }
}

static void onRotate(int delta) {
  Mode m = currentMode();
  if (m == Mode::Settings) return settingsRotate(delta);
  if (m == Mode::Voices) return voicesRotate(delta);
  if (m == Mode::Boot || m == Mode::Listening || m == Mode::Setup) return;
  ModelLock l;
  if (m == Mode::Keyboard) {
    int n = kb::keyCount();
    model.kbIndex = ((model.kbIndex + delta) % n + n) % n;
    return;
  }
  int maxScroll = chatContentHeight(model) - chatViewportHeight();
  if (maxScroll < 0) maxScroll = 0;
  int s = model.scroll + delta * SCROLL_STEP_PX;  // clockwise scrolls back in time
  model.scroll = s < 0 ? 0 : (s > maxScroll ? maxScroll : s);
}

// ---------------------------------------------------------------- games
static bool gameDown = false;

static void gameKey(games::Key k) { games::key(k, millis()); }

static void leaveGamesIfDone(bool stay) {
  if (!stay) setMode(Mode::Idle);
}

// Returns true if the event was used by the games (only in Mode::Games).
static bool gameInput(const input::Event& ev) {
  if (currentMode() != Mode::Games) return false;
  using games::Key;
  switch (ev.type) {
    case input::EventType::Down:
    case input::EventType::SpaceDown:
      gameDown = true;
      gameKey(Key::PressDown);
      return true;
    case input::EventType::Up:
    case input::EventType::SpaceUp:
      if (gameDown) gameKey(ev.heldMs >= games::LONG_PRESS_MS ? Key::LongPress : Key::Press);
      gameDown = false;
      return true;
    case input::EventType::Rotate:
      for (int i = 0; i < (ev.delta < 0 ? -ev.delta : ev.delta); i++)
        gameKey(ev.delta > 0 ? Key::RotR : Key::RotL);
      return true;
    case input::EventType::Enter:
      gameKey(Key::Press);
      return true;
    case input::EventType::Escape:
      leaveGamesIfDone(games::backPressed(millis()));
      return true;
    case input::EventType::Char: {  // Cardputer: arrow keys ; . , /  or W A S D, F = flag
      char ch = ev.text[0] | 0x20;
      if (ch == ';' || ch == 'w') gameKey(Key::Up);
      else if (ch == '.' || ch == 's') gameKey(Key::Down);
      else if (ch == ',' || ch == 'a') gameKey(Key::Left);
      else if (ch == '/' || ch == 'd') gameKey(Key::Right);
      else if (ch == 'f') gameKey(Key::LongPress);
      return true;
    }
    default:
      return false;  // back / power button: handled below
  }
}

static void handleInput() {
  input::Event ev;
  while (input::poll(ev)) {
    if (gameInput(ev)) continue;
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
      case input::EventType::BackDown:
        backDownAt = ev.atMs;
        break;
      case input::EventType::Char:
        onChar(ev.text);
        break;
      case input::EventType::Enter:
        onEnter();
        break;
      case input::EventType::Backspace:
        onBackspace();
        break;
      case input::EventType::Escape:
        onEscape();
        break;
      case input::EventType::SpaceDown:  // types a space, otherwise hold-to-talk like G0
        if (currentMode() == Mode::Keyboard)
          onChar(" ");
        else
          onButtonDown(ev);
        break;
      case input::EventType::SpaceUp:
        if (pressStartedTurn || tapOnly) onButtonUp(ev);
        break;
      case input::EventType::BackUp: {
        bool wasPressed = backDownAt != 0;  // ignore the release after waking up
        backDownAt = 0;
        {
          ModelLock l;
          model.powerHold = 0.f;
        }
        if (!wasPressed || ev.heldMs >= POWER_HOLD_SHOW_MS) break;  // long hold cancelled
        if (currentMode() == Mode::Settings) {
          closeSettings();
        } else if (currentMode() == Mode::Setup) {
          cancelSetup();
        } else if (currentMode() == Mode::Keyboard && model.kbNameMode) {
          ModelLock l;
          finishNameLocked(false);
        } else if (currentMode() == Mode::Voices) {
          closeVoices();
        } else if (currentMode() == Mode::Games) {
          leaveGamesIfDone(games::backPressed(millis()));
        } else if (currentMode() != Mode::Boot) {
          openSettings();
        }
        break;
      }
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
  const bool configured = provision::hasWifi();
  enum { WIFI, SERVER, DONE } phase = configured ? WIFI : DONE;
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
    provision::improvPoll();  // the web flasher asks right after flashing
    handleInput();  // drain early presses
    if (now - lastFrame >= 33) {
      lastFrame = now;
      drawFrame();
    }
    delay(2);
  }

  if (!configured && !net::wifiConnected()) {
    // first start: no Wi-Fi known yet -> setup hotspot (or the web flasher)
    startSetup();
    return;
  }
  ModelLock l;
  model.wifiOk = net::wifiConnected();
  updateBadgeLocked();
  if (!model.wifiOk)
    showErrorLocked(wifiErrorText());
  else if (!model.serverOk)
    showErrorLocked(serverErrorText());
  else
    setModeLocked(Mode::Idle);
}

void setup() {
  power::handleWakeUp();  // goes back to sleep if the button was only bumped
#if PIN_POWER_ON >= 0
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
#endif
  Serial.begin(115200);
  // give the USB serial monitor a moment to attach so the boot log isn't lost
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 1500;) delay(10);
  Serial.printf("\nGeistAI %s on %s\n", GEIST_VERSION, BOARD_NAME);
  Serial.printf("PSRAM %u KB free, heap %u KB free\n", (unsigned)(ESP.getFreePsram() / 1024),
                (unsigned)(ESP.getFreeHeap() / 1024));

  modelMutex = xSemaphoreCreateMutex();
  cmdQueue = xQueueCreate(4, sizeof(Command));

  games::onNewBest = [](int g, int value) {
    char k[8];
    snprintf(k, sizeof(k), "hs%d", g);
    prefs.begin("geistai", false);
    prefs.putInt(k, value);
    prefs.end();
  };
  loadSettings();
  games::begin(esp_random());
  setTheme(settings.theme);
  net::setAssistant(settings.cloud, settings.nickname);
  model.assistantName = assistantName();
  model.uiLang = settings.uiLang;
  model.kbPhysical = BOARD_HAS_KEYBOARD;
#if defined(GEIST_BOARD_CARDPUTER)
  model.hintTalk = S_HINT_TALK_CARDPUTER;
  model.hintMenu = S_HINT_MENU_CARDPUTER;
#elif defined(GEIST_BOARD_STICKCPLUS2)
  model.hintTalk = S_HINT_TALK_STICK;
  model.hintMenu = S_HINT_MENU_STICK;
#else
  model.hintTalk = S_HOLD_TO_TALK;
  model.hintMenu = S_DOUBLE_FOR_MENU;
#endif

  // Each step is printed on the screen as well, so a hang is easy to locate.
  bool fbOk = display::begin();
  display::status("1/6 display ok");
  if (!fbOk) {
    display::status("ERROR: no memory for the screen buffer (PSRAM?)");
    for (;;) delay(1000);
  }
  setScreenSize(display::width(), display::height());
  canvas = new Canvas(display::framebuffer(), SCREEN_W, SCREEN_H);

  display::status("2/6 leds");
  leds::begin();
  display::status("3/6 input");
  input::begin();

  display::status("4/6 memory");
  // With PSRAM the whole recording is buffered and sent at once; without it
  // (Cardputer) the audio is streamed to the server while you speak.
  wavBuf = (uint8_t*)heap_caps_malloc(44 + kMaxSamples * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  recSamples = wavBuf ? (int16_t*)(wavBuf + 44) : nullptr;
  Serial.printf("[audio] %s\n", wavBuf ? "buffered recording (PSRAM)" : "streaming recording");

  display::status("5/6 audio");
  audio::begin();
  audio::setVolume(settings.volume);

  display::status("6/6 starting");
  // The worker runs on the other core so drawing never stutters.
  BaseType_t workerCore = xPortGetCoreID() == 0 ? 1 : 0;
  xTaskCreatePinnedToCore(workerTask, "worker", 12288, nullptr, 2, nullptr, workerCore);

  bootSequence();
}

// Back button held: show a filling ring, switch off after POWER_HOLD_OFF_MS.
static void updatePowerHold(uint32_t now) {
  if (!backDownAt || !input::backIsDown()) return;
  uint32_t held = now - backDownAt;
  float progress = held < POWER_HOLD_SHOW_MS
                       ? 0.f
                       : (float)(held - POWER_HOLD_SHOW_MS) / (POWER_HOLD_OFF_MS - POWER_HOLD_SHOW_MS);
  if (progress > 0.f && progress < 0.01f) progress = 0.01f;
  {
    ModelLock l;
    model.powerHold = progress > 1.f ? 1.f : progress;
  }
  if (held < POWER_HOLD_OFF_MS) return;

  Serial.println("[power] switching off");
  cancelTurn();
  playStop = true;
  {
    ModelLock l;
    model.powerHold = 0.f;
    setModeLocked(Mode::Off);
  }
  drawFrame();
  leds::update(Mode::Off, 0, now + 1000);
  delay(1500);  // let the goodbye screen be seen
  power::switchOff();
}

void loop() {
  static uint32_t lastFrame = 0, lastPoll = 0;
  uint32_t now = millis();

  handleInput();
  updateHold(now);
  updatePowerHold(now);
  updateProvisioning(now);

  // Leave the error message after a few seconds.
  {
    ModelLock l;
    if (model.mode == Mode::Error && now - model.modeSinceMs > 9000) setModeLocked(Mode::Idle);
  }

  // Background housekeeping every 10 s
  if (now - lastPoll > 10000) {
    lastPoll = now;
    bool wifi = net::wifiConnected();
    int bat = power::batteryPercent();
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

  bool playing = m == Mode::Games;
  if (playing) games::tick(now);
  if (now - lastFrame >= (playing ? 30u : 40u)) {  // ~33 fps in games, ~25 fps otherwise
    lastFrame = now;
    drawFrame();
  }
  delay(1);
}
