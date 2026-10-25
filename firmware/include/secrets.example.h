// OPTIONAL. Wi-Fi can also be set up from the web flasher or with the device's
// setup hotspot (it opens by itself when no Wi-Fi is known).
//
// To build it in instead: copy this file to secrets.h (same folder) and fill in
// your details. They are applied whenever they change. secrets.h is git-ignored
// so your Wi-Fi password never ends up on GitHub.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// URL of the GeistAI server running on your PC / Raspberry Pi / home server.
// Leave empty ("") to find it automatically via mDNS (_geistai._tcp).
#define GEIST_SERVER_URL ""
// #define GEIST_SERVER_URL "http://192.168.1.50:8765"
