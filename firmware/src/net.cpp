#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "audio.h"
#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h missing - copy secrets.example.h to secrets.h and fill it in"
#include "secrets.example.h"
#endif

namespace net {
namespace {

std::string g_server;
std::string g_lastError;

void noteHttpError(int code) {
  g_lastError = code > 0 ? ("HTTP " + std::to_string(code)) : HTTPClient::errorToString(code).c_str();
  Serial.printf("[net] request to %s failed: %s\n", g_server.c_str(), g_lastError.c_str());
}

std::string urlEncode(const char* s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

bool readExact(WiFiClient* s, uint8_t* dst, size_t n, uint32_t timeoutMs = 5000) {
  size_t got = 0;
  uint32_t t0 = millis();
  while (got < n) {
    int avail = s->available();
    if (avail > 0) {
      int r = s->read(dst + got, n - got);
      if (r > 0) {
        got += r;
        t0 = millis();
      }
    } else {
      if (!s->connected()) return false;
      if (millis() - t0 > timeoutMs) return false;
      delay(2);
    }
  }
  return true;
}

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }

}  // namespace

// ---------------------------------------------------------------- wifi
void wifiBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("geistai-embed");
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // lower latency for audio streaming
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
int rssi() { return WiFi.RSSI(); }

std::string deviceId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  return buf;
}

// ---------------------------------------------------------------- server
bool serverKnown() { return !g_server.empty(); }
std::string lastErrorDetail() { return g_lastError; }
std::string serverHost() {
  if (g_server.empty()) return "-";
  size_t p = g_server.find("://");
  return p == std::string::npos ? g_server : g_server.substr(p + 3);
}
const std::string& serverUrl() { return g_server; }

bool findServer() {
  const char* configured = GEIST_SERVER_URL;
  if (configured && configured[0]) {
    g_server = configured;
    while (!g_server.empty() && g_server.back() == '/') g_server.pop_back();
    return healthCheck();
  }

  static bool mdnsStarted = false;
  if (!mdnsStarted) mdnsStarted = MDNS.begin("geistai-embed");
  int n = MDNS.queryService(MDNS_SERVICE, "tcp");
  for (int i = 0; i < n; i++) {
    char buf[64];
    IPAddress ip = MDNS.IP(i);
    snprintf(buf, sizeof(buf), "http://%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], MDNS.port(i));
    g_server = buf;
    if (healthCheck()) return true;
  }
  g_server.clear();
  return false;
}

bool healthCheck() {
  if (g_server.empty() || !wifiConnected()) return false;
  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin((g_server + "/health").c_str())) return false;
  int code = http.GET();
  http.end();
  if (code != 200)
    noteHttpError(code);
  else
    g_lastError.clear();
  return code == 200;
}

// ---------------------------------------------------------------- chat
ChatResult sendVoice(const uint8_t* wav, size_t len, const char* lang, bool wantVoice) {
  ChatResult res;
  if (!wifiConnected()) {
    res.error = "wifi";
    return res;
  }
  if (g_server.empty() && !findServer()) {
    res.error = "server";
    return res;
  }

  std::string url = g_server + "/api/chat?device=" + deviceId() + "&lang=" + urlEncode(lang) +
                    "&tts=" + (wantVoice ? "1" : "0");
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(4000);
  if (!http.begin(url.c_str())) {
    res.error = "server";
    return res;
  }
  http.addHeader("Content-Type", "audio/wav");
  int code = http.POST((uint8_t*)wav, len);
  res.httpCode = code;
  if (code <= 0) {
    noteHttpError(code);
    res.error = "server";
    http.end();
    return res;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body.c_str(), body.length());
  if (err) {
    res.error = "bad response";
    return res;
  }
  res.ok = code == 200 && (doc["ok"] | false);
  res.transcript = (const char*)(doc["transcript"] | "");
  res.reply = (const char*)(doc["reply"] | "");
  res.language = (const char*)(doc["language"] | "");
  res.audioPath = (const char*)(doc["audio"] | "");
  res.error = (const char*)(doc["error"] | "");
  return res;
}

bool resetConversation() {
  if (g_server.empty() || !wifiConnected()) return false;
  HTTPClient http;
  http.setTimeout(3000);
  std::string url = g_server + "/api/reset?device=" + deviceId();
  if (!http.begin(url.c_str())) return false;
  int code = http.POST((uint8_t*)nullptr, 0);
  http.end();
  return code == 200;
}

// ---------------------------------------------------------------- playback
bool playReply(const std::string& path, volatile bool* stop, volatile float* level) {
  if (path.empty() || g_server.empty()) return false;
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin((g_server + path).c_str())) return false;
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  WiFiClient* s = http.getStreamPtr();

  // --- parse the RIFF/WAVE header chunk by chunk
  uint8_t hdr[12];
  if (!readExact(s, hdr, 12) || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
    http.end();
    return false;
  }
  uint16_t channels = 1, bits = 16;
  uint32_t rate = 22050, dataLen = 0;
  for (;;) {
    uint8_t ch[8];
    if (!readExact(s, ch, 8)) {
      http.end();
      return false;
    }
    uint32_t sz = le32(ch + 4);
    if (!memcmp(ch, "fmt ", 4)) {
      uint8_t fmt[40];
      uint32_t take = sz < sizeof(fmt) ? sz : sizeof(fmt);
      if (!readExact(s, fmt, take)) break;
      for (uint32_t skip = take; skip < sz; skip++) s->read();
      channels = le16(fmt + 2);
      rate = le32(fmt + 4);
      bits = le16(fmt + 14);
    } else if (!memcmp(ch, "data", 4)) {
      dataLen = sz;
      break;
    } else {
      uint8_t tmp[64];
      uint32_t left = sz + (sz & 1);
      while (left) {
        uint32_t n = left > sizeof(tmp) ? sizeof(tmp) : left;
        if (!readExact(s, tmp, n)) break;
        left -= n;
      }
    }
  }
  if (bits != 16 || channels == 0 || channels > 2) {
    Serial.printf("[net] unsupported wav: %u ch, %u bit\n", channels, bits);
    http.end();
    return false;
  }

  audio::speakerStart(rate);
  const size_t frameBytes = 2 * channels;
  alignas(4) static uint8_t buf[2048];
  static int16_t mono[1024];
  size_t carry = 0;
  uint32_t played = 0;
  uint32_t lastData = millis();
  bool ok = true;
  while (!(stop && *stop) && (dataLen == 0 || played < dataLen)) {
    int avail = s->available();
    if (avail <= 0) {
      if (!s->connected()) break;
      if (millis() - lastData > 8000) {
        ok = false;
        break;
      }
      delay(2);
      continue;
    }
    size_t want = sizeof(buf) - carry;
    if (dataLen && want > dataLen - played) want = dataLen - played;
    int r = s->read(buf + carry, want);
    if (r <= 0) continue;
    lastData = millis();
    played += r;
    size_t total = carry + r;
    size_t frames = total / frameBytes;
    const int16_t* pcm = (const int16_t*)buf;
    for (size_t i = 0; i < frames; i++) {
      mono[i] = channels == 1 ? pcm[i] : (int16_t)(((int32_t)pcm[2 * i] + pcm[2 * i + 1]) / 2);
    }
    float l = audio::speakerWrite(mono, frames);
    if (level) *level = l * 3.f > 1.f ? 1.f : l * 3.f;
    carry = total - frames * frameBytes;
    if (carry) memmove(buf, buf + frames * frameBytes, carry);
  }
  // let the DMA drain
  delay(120);
  audio::speakerStop();
  if (level) *level = 0;
  http.end();
  return ok;
}

}  // namespace net
