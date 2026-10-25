// GeistAI - Wi-Fi setup without editing secrets.h.
//
// Credentials and the server address are kept in flash (NVS). They can be set
//  * by the web flasher over USB (Improv Wi-Fi Serial, https://www.improv-wifi.com),
//  * with a phone: the device opens its own Wi-Fi "GeistAI-XXXX" and a setup
//    page (captive portal),
//  * or at build time with include/secrets.h (applied whenever it changes).
#pragma once
#include <stdint.h>

#include <string>

namespace provision {

struct Creds {
  std::string ssid;
  std::string pass;
  std::string server;  // empty = find the server automatically (mDNS)
};

// Stored settings (loaded on first use).
const Creds& creds();
bool hasWifi();  // an SSID is configured
void save(const Creds& c);

// ---- Improv Wi-Fi Serial (web flasher). Call improvPoll() often.
void improvPoll();
// true once after the web flasher set up Wi-Fi successfully
bool improvJustProvisioned();

// ---- captive portal (phone setup)
// Blocks for a few seconds while it scans for networks.
void portalStart(uint8_t uiLang);
void portalStop();
bool portalActive();
void portalPoll();
bool portalSaved();  // the form was submitted - restart to apply
const std::string& portalSsid();
const std::string& portalPass();
std::string portalUrl();  // "192.168.4.1"

}  // namespace provision
