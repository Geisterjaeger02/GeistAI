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

// Accepts "192.168.1.5", "192.168.1.5:8765", "http://192.168.1.5:8765/" ... and
// turns it into "http://192.168.1.5:8765".
static std::string normalizeUrl(std::string u) {
  std::string t;
  for (char ch : u)
    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') t += ch;
  while (!t.empty() && t.back() == '/') t.pop_back();
  if (t.empty()) return t;
  if (t.rfind("http://", 0) != 0 && t.rfind("https://", 0) != 0) {
    size_t p = t.find("://");
    if (p != std::string::npos) t = t.substr(p + 3);   // e.g. "htp://"
    else if (t.rfind("http//", 0) == 0) t = t.substr(6);
    t = "http://" + t;
  }
  size_t hostStart = t.find("://") + 3;
  size_t slash = t.find('/', hostStart);
  std::string host = t.substr(hostStart, slash == std::string::npos ? std::string::npos : slash - hostStart);
  if (host.find(':') == std::string::npos) {
    std::string port = ":" + std::to_string(SERVER_DEFAULT_PORT);
    if (slash == std::string::npos) t += port;
    else t.insert(slash, port);
  }
  return t;
}

bool findServer() {
  const char* configured = GEIST_SERVER_URL;
  if (configured && configured[0]) {
    g_server = normalizeUrl(configured);
    Serial.printf("[net] server from secrets.h: %s\n", g_server.c_str());
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
  if (!http.begin((g_server + "/health").c_str())) {
    g_lastError = "invalid address";
    Serial.printf("[net] invalid server address: %s\n", g_server.c_str());
    return false;
  }
  int code = http.GET();
  http.end();
  if (code != 200)
    noteHttpError(code);
  else
    g_lastError.clear();
  return code == 200;
}

// ---------------------------------------------------------------- chat
static void parseChatJson(int code, const char* body, size_t len, ChatResult& res) {
  res.httpCode = code;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, len);
  if (err) {
    res.error = "bad response";
    return;
  }
  res.ok = code == 200 && (doc["ok"] | false);
  res.transcript = (const char*)(doc["transcript"] | "");
  res.reply = (const char*)(doc["reply"] | "");
  res.language = (const char*)(doc["language"] | "");
  res.audioPath = (const char*)(doc["audio"] | "");
  res.error = (const char*)(doc["error"] | "");
}

// ---------------------------------------------------------------- streaming upload
namespace {
WiFiClient g_stream;

bool splitServer(std::string& host, uint16_t& port) {
  std::string rest = g_server;
  size_t p = rest.find("://");
  if (p != std::string::npos) rest = rest.substr(p + 3);
  size_t slash = rest.find('/');
  if (slash != std::string::npos) rest = rest.substr(0, slash);
  size_t colon = rest.rfind(':');
  host = rest.substr(0, colon);
  port = colon == std::string::npos ? 80 : (uint16_t)atoi(rest.c_str() + colon + 1);
  return !host.empty();
}

bool writeAll(const void* data, size_t n) {
  const uint8_t* d = (const uint8_t*)data;
  size_t off = 0;
  uint32_t t0 = millis();
  while (off < n) {
    size_t w = g_stream.write(d + off, n - off);
    if (w > 0) {
      off += w;
      t0 = millis();
    } else {
      if (!g_stream.connected() || millis() - t0 > 5000) return false;
      delay(1);
    }
  }
  return true;
}

bool readLine(std::string& line, uint32_t timeoutMs) {
  line.clear();
  uint32_t t0 = millis();
  for (;;) {
    while (g_stream.available()) {
      char c = (char)g_stream.read();
      if (c == '\n') {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
      }
      if (line.size() < 512) line += c;
      t0 = millis();
    }
    if (!g_stream.connected() && !g_stream.available()) return !line.empty();
    if (millis() - t0 > timeoutMs) return false;
    delay(2);
  }
}
}  // namespace

bool streamBegin(const char* lang, bool wantVoice) {
  if (!wifiConnected()) return false;
  if (g_server.empty() && !findServer()) return false;
  std::string host;
  uint16_t port;
  if (!splitServer(host, port)) return false;
  g_stream.stop();
  if (!g_stream.connect(host.c_str(), port, 4000)) {
    noteHttpError(HTTPC_ERROR_CONNECTION_REFUSED);
    return false;
  }
  g_stream.setNoDelay(true);
  std::string req = "POST /api/chat?device=" + deviceId() + "&lang=" + urlEncode(lang) +
                    "&tts=" + (wantVoice ? "1" : "0") + " HTTP/1.1\r\nHost: " + host + ":" +
                    std::to_string(port) +
                    "\r\nContent-Type: audio/L16; rate=16000; channels=1\r\n"
                    "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
  if (!writeAll(req.data(), req.size())) {
    noteHttpError(HTTPC_ERROR_SEND_HEADER_FAILED);
    g_stream.stop();
    return false;
  }
  return true;
}

bool streamWrite(const int16_t* pcm, size_t samples) {
  if (!samples) return true;
  char hdr[12];
  int hl = snprintf(hdr, sizeof(hdr), "%X\r\n", (unsigned)(samples * 2));
  bool ok = writeAll(hdr, hl) && writeAll(pcm, samples * 2) && writeAll("\r\n", 2);
  if (!ok) noteHttpError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
  return ok;
}

void streamAbort() { g_stream.stop(); }

ChatResult streamFinish() {
  ChatResult res;
  if (!writeAll("0\r\n\r\n", 5)) {
    noteHttpError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
    res.error = "server";
    g_stream.stop();
    return res;
  }
  // status line, e.g. "HTTP/1.1 200 OK" (the server may need a while to think)
  std::string line;
  if (!readLine(line, 120000) || line.rfind("HTTP/", 0) != 0) {
    noteHttpError(HTTPC_ERROR_READ_TIMEOUT);
    res.error = "server";
    g_stream.stop();
    return res;
  }
  size_t sp = line.find(' ');
  int code = sp == std::string::npos ? 0 : atoi(line.c_str() + sp + 1);
  long contentLength = -1;
  while (readLine(line, 10000) && !line.empty()) {
    std::string lower = line;
    for (auto& ch : lower) ch = tolower((unsigned char)ch);
    if (lower.rfind("content-length:", 0) == 0) contentLength = atol(lower.c_str() + 15);
  }
  std::string body;
  uint32_t t0 = millis();
  while ((contentLength < 0 || (long)body.size() < contentLength) && millis() - t0 < 10000) {
    while (g_stream.available()) {
      body += (char)g_stream.read();
      t0 = millis();
    }
    if (!g_stream.connected() && !g_stream.available()) break;
    delay(2);
  }
  g_stream.stop();
  parseChatJson(code, body.data(), body.size(), res);
  if (code <= 0) res.error = "server";
  return res;
}

// Shared by voice and text: POSTs `body` to `path` and parses the JSON reply.
static ChatResult postChat(const char* path, const char* contentType, const uint8_t* body,
                           size_t len, const char* lang, bool wantVoice) {
  ChatResult res;
  if (!wifiConnected()) {
    res.error = "wifi";
    return res;
  }
  if (g_server.empty() && !findServer()) {
    res.error = "server";
    return res;
  }

  std::string url = g_server + path + "?device=" + deviceId() + "&lang=" + urlEncode(lang) +
                    "&tts=" + (wantVoice ? "1" : "0");
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(4000);
  if (!http.begin(url.c_str())) {
    g_lastError = "invalid address";
    res.error = "server";
    return res;
  }
  http.addHeader("Content-Type", contentType);
  int code = http.POST((uint8_t*)body, len);
  res.httpCode = code;
  if (code <= 0) {
    noteHttpError(code);
    res.error = "server";
    http.end();
    return res;
  }
  String reply = http.getString();
  http.end();
  parseChatJson(code, reply.c_str(), reply.length(), res);
  return res;
}

ChatResult sendVoice(const uint8_t* wav, size_t len, const char* lang, bool wantVoice) {
  return postChat("/api/chat", "audio/wav", wav, len, lang, wantVoice);
}

ChatResult sendText(const std::string& text, const char* lang, bool wantVoice) {
  JsonDocument body;
  body["text"] = text.c_str();
  std::string payload;
  serializeJson(body, payload);
  return postChat("/api/text", "application/json", (const uint8_t*)payload.data(), payload.size(),
                  lang, wantVoice);
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
