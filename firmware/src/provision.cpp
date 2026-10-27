#include "provision.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#if __has_include(<esp_mac.h>)
#include <esp_mac.h>
#endif

#include <vector>

#include "board.h"
#include "config.h"

// secrets.h is optional now: without it (or with the example values) the
// device asks for Wi-Fi via the web flasher or its own setup hotspot.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef GEIST_SERVER_URL
#define GEIST_SERVER_URL ""
#endif

namespace provision {

// ============================================================================
//  Stored settings
// ============================================================================
namespace {

constexpr const char* kNs = "geistwifi";
Creds g_creds;
bool g_loaded = false;

uint32_t fnv1a(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) h = (h ^ c) * 16777619u;
  return h;
}

std::string readString(Preferences& p, const char* key) {
  return p.isKey(key) ? std::string(p.getString(key, "").c_str()) : std::string();
}

void load() {
  Preferences p;
  p.begin(kNs, false);
#ifdef WIFI_SSID
  // Values compiled in from secrets.h win whenever they change, so editing
  // secrets.h and flashing again still works as before.
  if (WIFI_SSID[0] && strcmp(WIFI_SSID, "your-wifi-name") != 0) {
    const std::string ssid = WIFI_SSID, pass = WIFI_PASSWORD, server = GEIST_SERVER_URL;
    uint32_t h = fnv1a(ssid + "\n" + pass + "\n" + server);
    if (!p.isKey("sech") || p.getUInt("sech", 0) != h) {
      p.putString("ssid", ssid.c_str());
      p.putString("pass", pass.c_str());
      p.putString("server", server.c_str());
      p.putUInt("sech", h);
      Serial.println("[setup] Wi-Fi settings taken from secrets.h");
    }
  }
#endif
  g_creds.ssid = readString(p, "ssid");
  g_creds.pass = readString(p, "pass");
  g_creds.server = readString(p, "server");
  p.end();
  g_loaded = true;
  Serial.printf("[setup] Wi-Fi: %s, server: %s\n",
                g_creds.ssid.empty() ? "(not set up)" : g_creds.ssid.c_str(),
                g_creds.server.empty() ? "auto" : g_creds.server.c_str());
}

}  // namespace

const Creds& creds() {
  if (!g_loaded) load();
  return g_creds;
}

bool hasWifi() { return !creds().ssid.empty(); }

void save(const Creds& c) {
  Preferences p;
  p.begin(kNs, false);
  p.putString("ssid", c.ssid.c_str());
  p.putString("pass", c.pass.c_str());
  p.putString("server", c.server.c_str());
  p.end();
  g_creds = c;
  g_loaded = true;
  Serial.printf("[setup] saved Wi-Fi \"%s\"\n", c.ssid.c_str());
}

// ============================================================================
//  Improv Wi-Fi Serial  (protocol: https://www.improv-wifi.com/serial/)
// ============================================================================
namespace {

enum : uint8_t { TYPE_STATE = 0x01, TYPE_ERROR = 0x02, TYPE_RPC = 0x03, TYPE_RPC_RESULT = 0x04 };
enum : uint8_t { ST_AUTHORIZED = 0x02, ST_PROVISIONING = 0x03, ST_PROVISIONED = 0x04 };
enum : uint8_t {
  ERR_NONE = 0x00,
  ERR_INVALID_RPC = 0x01,
  ERR_UNKNOWN_RPC = 0x02,
  ERR_UNABLE_TO_CONNECT = 0x03,
};
enum : uint8_t {
  CMD_WIFI_SETTINGS = 0x01,
  CMD_GET_STATE = 0x02,
  CMD_GET_INFO = 0x03,
  CMD_GET_NETWORKS = 0x04,
};
constexpr uint32_t kImprovConnectTimeoutMs = 20000;

uint8_t rx[9 + 256 + 1];
size_t rxPos = 0;

bool connecting = false;
uint32_t connectStart = 0;
Creds pending;
bool justProvisioned = false;

void sendPacket(uint8_t type, const uint8_t* data, size_t len) {
  uint8_t out[9 + 256 + 2];
  if (len > 255) len = 255;
  memcpy(out, "IMPROV", 6);
  out[6] = 1;  // protocol version
  out[7] = type;
  out[8] = (uint8_t)len;
  memcpy(out + 9, data, len);
  uint8_t sum = 0;
  for (size_t i = 0; i < 9 + len; i++) sum += out[i];
  out[9 + len] = sum;
  out[10 + len] = '\n';
  Serial.write(out, 11 + len);  // one write call so log lines can't interleave
}

void sendState(uint8_t s) { sendPacket(TYPE_STATE, &s, 1); }
void sendError(uint8_t e) { sendPacket(TYPE_ERROR, &e, 1); }

void sendResult(uint8_t cmd, const std::vector<std::string>& items) {
  uint8_t d[256];
  size_t p = 2;
  for (const std::string& s : items) {
    if (p + 1 >= 250) break;
    size_t n = s.size();
    if (p + 1 + n > 250) n = 250 - p - 1;
    d[p++] = (uint8_t)n;
    memcpy(d + p, s.data(), n);
    p += n;
  }
  d[0] = cmd;
  d[1] = (uint8_t)(p - 2);
  sendPacket(TYPE_RPC_RESULT, d, p);
}

const char* chipName() {
#if CONFIG_IDF_TARGET_ESP32S3
  return "ESP32-S3";
#else
  return "ESP32";
#endif
}

void reconnectSaved() {
  WiFi.disconnect(false);
  if (hasWifi()) WiFi.begin(g_creds.ssid.c_str(), g_creds.pass.c_str());
}

void sendNetworks() {
  int n = WiFi.scanNetworks();
  std::vector<std::string> seen;
  for (int i = 0; i < n; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    bool dup = false;
    for (const std::string& s : seen) dup |= s == ssid;
    if (dup) continue;
    seen.push_back(ssid);
    sendResult(CMD_GET_NETWORKS, {ssid, std::to_string(WiFi.RSSI(i)),
                                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"});
  }
  WiFi.scanDelete();
  sendResult(CMD_GET_NETWORKS, {});  // end of list
}

void handleRpc(const uint8_t* d, size_t len) {
  if (len < 2 || d[1] != len - 2) {
    sendError(ERR_INVALID_RPC);
    return;
  }
  sendError(ERR_NONE);
  switch (d[0]) {
    case CMD_WIFI_SETTINGS: {
      size_t ssidLen = d[2];
      if (3 + ssidLen >= len) {
        sendError(ERR_INVALID_RPC);
        return;
      }
      size_t passLen = d[3 + ssidLen];
      if (4 + ssidLen + passLen > len) {
        sendError(ERR_INVALID_RPC);
        return;
      }
      pending.ssid.assign((const char*)d + 3, ssidLen);
      pending.pass.assign((const char*)d + 4 + ssidLen, passLen);
      pending.server = creds().server;
      Serial.printf("[setup] web flasher: connecting to \"%s\"\n", pending.ssid.c_str());
      sendState(ST_PROVISIONING);
      WiFi.disconnect(false);
      delay(100);
      if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_STA);
      WiFi.begin(pending.ssid.c_str(), pending.pass.c_str());
      connecting = true;
      connectStart = millis();
      break;
    }
    case CMD_GET_STATE:
      if (WiFi.status() == WL_CONNECTED) {
        sendState(ST_PROVISIONED);
        sendResult(CMD_GET_STATE, {});
      } else {
        sendState(ST_AUTHORIZED);
      }
      break;
    case CMD_GET_INFO:
      sendResult(CMD_GET_INFO, {"GeistAI", GEIST_VERSION, chipName(), BOARD_NAME});
      break;
    case CMD_GET_NETWORKS:
      sendNetworks();
      break;
    default:
      sendError(ERR_UNKNOWN_RPC);
      break;
  }
}

// Same checks as the reference implementation (improv-wifi/sdk-cpp).
bool acceptByte(size_t pos, uint8_t b) {
  static const char kHeader[] = "IMPROV";
  if (pos < 6) return b == (uint8_t)kHeader[pos];
  if (pos == 6) return b == 1;
  if (pos <= 8) return true;
  uint8_t len = rx[8];
  if (pos <= 8 + (size_t)len) return true;
  if (pos == 9 + (size_t)len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < pos; i++) sum += rx[i];
    if (sum != b) {
      sendError(ERR_INVALID_RPC);
      return false;
    }
    if (rx[7] == TYPE_RPC) handleRpc(rx + 9, len);
    return false;  // packet complete: start over
  }
  return false;
}

}  // namespace

void improvPoll() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    uint8_t b = (uint8_t)c;
    rx[rxPos] = b;
    if (acceptByte(rxPos, b)) {
      rxPos++;
    } else {
      rxPos = 0;
      if (b == 'I') rx[rxPos++] = b;  // may be the start of the next packet
    }
  }

  if (connecting) {
    uint32_t elapsed = millis() - connectStart;
    if (elapsed > 500 && WiFi.status() == WL_CONNECTED) {
      connecting = false;
      save(pending);
      sendState(ST_PROVISIONED);
      sendResult(CMD_WIFI_SETTINGS, {});
      justProvisioned = true;
      Serial.printf("[setup] web flasher: connected, IP %s\n", WiFi.localIP().toString().c_str());
    } else if (elapsed > kImprovConnectTimeoutMs) {
      connecting = false;
      Serial.println("[setup] web flasher: could not connect");
      sendError(ERR_UNABLE_TO_CONNECT);
      sendState(ST_AUTHORIZED);
      reconnectSaved();
    }
  }
}

bool improvJustProvisioned() {
  bool v = justProvisioned;
  justProvisioned = false;
  return v;
}

// ============================================================================
//  Captive portal
// ============================================================================
namespace {

DNSServer* dns = nullptr;
WebServer* web = nullptr;
std::string apSsid, apPass;
bool saved = false;
uint8_t lang = 0;

struct Network {
  std::string ssid;
  int rssi;
  bool locked;
};
std::vector<Network> networks;

// ---- page texts
enum { P_TITLE, P_NETWORK, P_PASSWORD, P_SERVER, P_SERVER_HINT, P_SAVE, P_SAVED, P_RESCAN,
       P_ERR_SSID, P_ERR_PASS, P_COUNT };
const char* const kPage[][P_COUNT] = {
    {"Wi-Fi setup", "Wi-Fi network", "Password", "Server address (optional)",
     "Leave empty to find the GeistAI server on your network automatically.", "Save",
     "Saved! GeistAI restarts and connects to", "Scan again", "Please choose a network.",
     "The password must have 8-63 characters (or none for open networks)."},
    {"WLAN einrichten", "WLAN-Netzwerk", "Passwort", "Server-Adresse (optional)",
     "Leer lassen, dann sucht GeistAI den Server im Netzwerk automatisch.", "Speichern",
     "Gespeichert! GeistAI startet neu und verbindet sich mit", "Neu suchen",
     "Bitte wähle ein Netzwerk.",
     "Das Passwort muss 8-63 Zeichen haben (oder leer bei offenen Netzen)."},
    {"Configurar Wi-Fi", "Red Wi-Fi", "Contraseña", "Dirección del servidor (opcional)",
     "Déjalo vacío para encontrar el servidor GeistAI automáticamente.", "Guardar",
     "¡Guardado! GeistAI se reinicia y se conecta a", "Buscar de nuevo", "Elige una red.",
     "La contraseña debe tener 8-63 caracteres (o ninguna en redes abiertas)."},
    {"Configurer le Wi-Fi", "Réseau Wi-Fi", "Mot de passe", "Adresse du serveur (facultatif)",
     "Laisse vide pour trouver automatiquement le serveur GeistAI.", "Enregistrer",
     "Enregistré ! GeistAI redémarre et se connecte à", "Rechercher", "Choisis un réseau.",
     "Le mot de passe doit faire 8 à 63 caractères (ou rien si le réseau est ouvert)."},
    {"Configura Wi-Fi", "Rete Wi-Fi", "Password", "Indirizzo del server (facoltativo)",
     "Lascia vuoto per trovare il server GeistAI automaticamente.", "Salva",
     "Salvato! GeistAI si riavvia e si collega a", "Cerca di nuovo", "Scegli una rete.",
     "La password deve avere 8-63 caratteri (o nessuna per reti aperte)."},
    {"Configurar Wi-Fi", "Rede Wi-Fi", "Senha", "Endereço do servidor (opcional)",
     "Deixe vazio para encontrar o servidor GeistAI automaticamente.", "Salvar",
     "Salvo! O GeistAI reinicia e se conecta a", "Procurar de novo", "Escolha uma rede.",
     "A senha deve ter 8-63 caracteres (ou nenhuma em redes abertas)."},
    {"Wifi instellen", "Wifi-netwerk", "Wachtwoord", "Serveradres (optioneel)",
     "Laat leeg om de GeistAI-server automatisch te vinden.", "Opslaan",
     "Opgeslagen! GeistAI herstart en verbindt met", "Opnieuw zoeken", "Kies een netwerk.",
     "Het wachtwoord moet 8-63 tekens hebben (of geen bij open netwerken)."},
    {"Konfiguracja Wi-Fi", "Sieć Wi-Fi", "Hasło", "Adres serwera (opcjonalnie)",
     "Zostaw puste, aby automatycznie znaleźć serwer GeistAI.", "Zapisz",
     "Zapisano! GeistAI uruchamia się ponownie i łączy z", "Szukaj ponownie",
     "Wybierz sieć.", "Hasło musi mieć 8-63 znaki (lub żadnego w sieciach otwartych)."},
    {"Wi-Fi kurulumu", "Wi-Fi ağı", "Şifre", "Sunucu adresi (isteğe bağlı)",
     "GeistAI sunucusunu otomatik bulmak için boş bırak.", "Kaydet",
     "Kaydedildi! GeistAI yeniden başlıyor ve şuna bağlanıyor:", "Tekrar ara",
     "Bir ağ seç.", "Şifre 8-63 karakter olmalı (açık ağlarda boş)."},
    {"Настройка Wi-Fi", "Сеть Wi-Fi", "Пароль", "Адрес сервера (необязательно)",
     "Оставь пустым, чтобы найти сервер GeistAI автоматически.", "Сохранить",
     "Сохранено! GeistAI перезапускается и подключается к", "Искать снова",
     "Выбери сеть.", "Пароль должен быть от 8 до 63 символов (или пустым для открытых сетей)."},
};
constexpr int PAGE_LANGS = sizeof(kPage) / sizeof(kPage[0]);
const char* T(int id) { return kPage[lang < PAGE_LANGS ? lang : 0][id]; }

std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

void scan() {
  networks.clear();
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    bool dup = false;
    for (const Network& x : networks) dup |= x.ssid == ssid;
    if (!dup) networks.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  Serial.printf("[setup] found %u networks\n", (unsigned)networks.size());
}

const char kStyle[] =
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<style>body{margin:0;font-family:system-ui,sans-serif;background:linear-gradient(#28f6ff,"
    "#00cef0);color:#001824;min-height:100vh}main{max-width:420px;margin:auto;padding:24px 18px}"
    "h1{font:italic 700 34px Georgia,serif;margin:0}h2{font-size:17px;font-weight:600;margin:4px 0 "
    "18px;color:#00566c}label{display:block;font-weight:600;margin:14px 0 6px}input{width:100%;"
    "box-sizing:border-box;padding:12px;border:0;border-radius:12px;font-size:16px;background:"
    "#ffffffd0}button,.b{display:inline-block;padding:10px 14px;margin:4px 6px 0 0;border:0;"
    "border-radius:12px;background:#ffffff90;color:#001824;font-size:15px;text-decoration:none}"
    ".go{width:100%;margin-top:22px;padding:14px;background:#001824;color:#96faff;font-size:17px;"
    "font-weight:600}small{color:#00566c}.err{background:#c40046;color:#fff;padding:10px;"
    "border-radius:12px}</style>";

void sendForm(const char* error) {
  std::string h = "<!doctype html><html><head><meta charset=utf-8><title>GeistAI</title>";
  h += kStyle;
  h += "</head><body><main><h1>GeistAI</h1><h2>";
  h += T(P_TITLE);
  h += "</h2>";
  if (error) h += std::string("<p class=err>") + error + "</p>";
  h += "<form method=post action=/save><label>";
  h += T(P_NETWORK);
  h += "</label><div>";
  for (const Network& n : networks) {
    const char* bars = n.rssi > -60 ? "&#9679;&#9679;&#9679;" : n.rssi > -75 ? "&#9679;&#9679;&#9675;"
                                                                             : "&#9679;&#9675;&#9675;";
    h += "<button type=button onclick=\"s.value=this.dataset.s;p.focus()\" data-s=\"" + esc(n.ssid) +
         "\">" + esc(n.ssid) + " <small>" + bars + (n.locked ? " &#128274;" : "") +
         "</small></button>";
  }
  h += "<a class=b href=/scan>&#8635; ";
  h += T(P_RESCAN);
  h += "</a></div><input id=s name=ssid maxlength=32 required value=\"" + esc(g_creds.ssid) +
       "\"><label>";
  h += T(P_PASSWORD);
  h += "</label><input id=p name=pass type=password maxlength=63><label>";
  h += T(P_SERVER);
  h += "</label><input name=server placeholder='192.168.1.50' value=\"" + esc(g_creds.server) +
       "\"><small>";
  h += T(P_SERVER_HINT);
  h += "</small><button class=go>";
  h += T(P_SAVE);
  h += "</button></form></main></body></html>";
  web->send(200, "text/html; charset=utf-8", h.c_str());
}

void onRoot() { sendForm(nullptr); }

void onScan() {
  scan();
  web->sendHeader("Location", "/", true);
  web->send(302, "text/plain", "");
}

std::string trim(const String& s) {
  std::string t = s.c_str();
  while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
  size_t i = 0;
  while (i < t.size() && isspace((unsigned char)t[i])) i++;
  return t.substr(i);
}

void onSave() {
  Creds c;
  c.ssid = web->arg("ssid").c_str();  // SSIDs may legitimately have spaces at the ends
  c.pass = web->arg("pass").c_str();
  c.server = trim(web->arg("server"));
  if (c.ssid.empty() || c.ssid.size() > 32) return sendForm(T(P_ERR_SSID));
  if (!c.pass.empty() && (c.pass.size() < 8 || c.pass.size() > 63)) return sendForm(T(P_ERR_PASS));
  save(c);
  saved = true;
  std::string h = "<!doctype html><html><head><meta charset=utf-8><title>GeistAI</title>";
  h += kStyle;
  h += "</head><body><main><h1>GeistAI</h1><h2>";
  h += T(P_SAVED);
  h += " <b>" + esc(c.ssid) + "</b>.</h2></main></body></html>";
  web->send(200, "text/html; charset=utf-8", h.c_str());
}

// Phones probe a few well-known URLs to detect a sign-in page; answering them
// with a redirect makes the setup page pop up by itself.
void onOther() {
  web->sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  web->send(302, "text/plain", "");
}

}  // namespace

void portalStart(uint8_t uiLang) {
  if (web) return;
  lang = uiLang;
  saved = false;
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char name[20];
  snprintf(name, sizeof(name), "GeistAI-%02X%02X", mac[4], mac[5]);
  apSsid = name;
  apPass.clear();
  for (int i = 0; i < 8; i++) apPass += (char)('0' + esp_random() % 10);

  creds();  // make sure the stored values are loaded for the form
  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);
  delay(100);
  scan();  // before the hotspot is up, so phones don't get kicked off by channel hopping
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  delay(100);
  IPAddress ip = WiFi.softAPIP();

  dns = new DNSServer();
  dns->setErrorReplyCode(DNSReplyCode::NoError);
  dns->start(53, "*", ip);

  web = new WebServer(80);
  web->on("/", HTTP_GET, onRoot);
  web->on("/scan", HTTP_GET, onScan);
  web->on("/save", HTTP_POST, onSave);
  web->onNotFound(onOther);
  web->begin();
  Serial.printf("[setup] hotspot \"%s\" up, open http://%s\n", apSsid.c_str(),
                ip.toString().c_str());
}

void portalStop() {
  if (web) {
    web->stop();
    delete web;
    web = nullptr;
  }
  if (dns) {
    dns->stop();
    delete dns;
    dns = nullptr;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

bool portalActive() { return web != nullptr; }

void portalPoll() {
  if (dns) dns->processNextRequest();
  if (web) web->handleClient();
}

bool portalSaved() { return saved; }
const std::string& portalSsid() { return apSsid; }
const std::string& portalPass() { return apPass; }
std::string portalUrl() {
  return web ? std::string(WiFi.softAPIP().toString().c_str()) : std::string("192.168.4.1");
}

}  // namespace provision
