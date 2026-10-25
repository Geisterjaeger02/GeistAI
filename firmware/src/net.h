// GeistAI - Wi-Fi, server discovery and the HTTP protocol to the GeistAI server.
// See docs/protocol.md for the API.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <string>

namespace net {

void wifiBegin();
bool wifiConnected();
int rssi();

// Resolves the server URL (secrets.h or mDNS) and checks /health.
bool findServer();
bool serverKnown();
const std::string& serverUrl();
bool healthCheck();

std::string deviceId();

struct ChatResult {
  bool ok = false;
  int httpCode = 0;
  std::string transcript;
  std::string reply;
  std::string language;  // detected / used language (ISO 639-1)
  std::string audioPath; // e.g. /api/audio/abc.wav, empty if no voice
  std::string error;
};

// Uploads a 16 kHz mono WAV (header included) and waits for the reply.
ChatResult sendVoice(const uint8_t* wav, size_t len, const char* lang, bool wantVoice);

// Streams a WAV reply to the speaker. Returns false on network error.
// `stop` aborts playback, `level` receives the current loudness 0..1.
bool playReply(const std::string& path, volatile bool* stop, volatile float* level);

bool resetConversation();

}  // namespace net
