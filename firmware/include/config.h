// GeistAI - tunables. Wi-Fi + server are set up from the web flasher, the setup hotspot or secrets.h.
#pragma once

#define GEIST_VERSION "v0.2.0"

// ---------------------------------------------------------------- audio
#define MIC_SAMPLE_RATE 16000      // what the speech server expects
#define MAX_RECORD_SECONDS 15      // hard limit for one utterance
#define MIN_RECORD_MS 350          // shorter recordings are discarded
#define MIC_DIGITAL_GAIN 4.0f      // software gain after DC removal
#define MIC_ES7210_GAIN GAIN_24DB  // analog gain (original T-Embed only)

// Tap-to-talk voice activity detection (energy based)
#define VAD_SILENCE_END_MS 1200    // stop after this much silence once speech started
#define VAD_NO_SPEECH_MS 6000      // give up if nobody speaks
#define VAD_MIN_RMS 350.0f         // absolute speech threshold (int16 RMS)

#define DEFAULT_VOLUME 70          // percent
#define DEFAULT_UI_LANG 0          // menu language: 0 English, 1 Deutsch, 2 Español, 3 Français

// ---------------------------------------------------------------- input
#define HOLD_THRESHOLD_MS 350      // press longer than this = push-to-talk, shorter = keyboard
#define DOUBLE_CLICK_MS 600        // (unused since the keyboard took over the tap)
#define KB_HOLD_TO_TALK_MS 450     // holding the knob in the keyboard switches to talking
#define ENCODER_REVERSE 0          // flip if scrolling feels backwards
#define SCROLL_STEP_PX 18

// ---------------------------------------------------------------- power (back button)
#define POWER_HOLD_SHOW_MS 600     // ring appears after holding this long
#define POWER_HOLD_OFF_MS 3000     // switch off after holding this long
#define POWER_WAKE_HOLD_MS 800     // hold this long to switch back on
// 0 = deep sleep (wake with the back button, works on USB too)
// 1 = cut the battery via the BQ25896 charger ("ship mode"); battery only, and the
//     T-Embed then only turns on again via its charger button or by plugging in USB
#define POWER_OFF_SHIP_MODE 0

// ---------------------------------------------------------------- network
#define HTTP_TIMEOUT_MS 65000      // max the HTTP client allows (uint16 ms)
#define WIFI_CONNECT_TIMEOUT_MS 20000
#define SERVER_DEFAULT_PORT 8765
#define MDNS_SERVICE "geistai"     // server announces _geistai._tcp

// ---------------------------------------------------------------- display
#define LCD_ROTATION 3
#define LCD_BRIGHTNESS 210
#define CHAT_HISTORY_MAX 14        // bubbles kept on the device
