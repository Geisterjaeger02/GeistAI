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
// Last HTTP result of the health check / chat request, for error messages.
std::string lastErrorDetail();
// "192.168.1.5:8765" (no scheme), or "-".
std::string serverHost();

std::string deviceId();

// Settings sent with every question: use ChatGPT instead of the local model,
// and the assistant's name ("" = GeistAI).
void setAssistant(bool useCloud, const std::string& name);
// The server has an OpenAI key (known after the first health check).
bool cloudAvailable();

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

// Streaming upload for boards without PSRAM: raw 16 kHz PCM is sent with
// chunked transfer encoding while recording (POST /api/chat, audio/L16).
bool streamBegin(const char* lang, bool wantVoice);
bool streamWrite(const int16_t* pcm, size_t samples);
ChatResult streamFinish();  // ends the upload and waits for the answer
void streamAbort();

// Sends a typed message (POST /api/text).
ChatResult sendText(const std::string& text, const char* lang, bool wantVoice);

// Streams a WAV reply to the speaker. Returns false on network error.
// `stop` aborts playback, `level` receives the current loudness 0..1.
bool playReply(const std::string& path, volatile bool* stop, volatile float* level);

bool resetConversation();

}  // namespace net
