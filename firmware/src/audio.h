// GeistAI - microphone capture and speaker playback (legacy ESP-IDF I2S driver).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace audio {

bool begin();

// ---- microphone: 16 kHz, mono, int16
void micStart();
// Blocks until `maxSamples` samples are read (~maxSamples/16 ms). Returns the
// number of samples written to dst and the frame RMS (int16 scale).
size_t micRead(int16_t* dst, size_t maxSamples, float* rms);
void micStop();

// ---- speaker: mono int16 at any sample rate
void speakerStart(uint32_t sampleRate);
// Writes mono samples (volume applied); returns the chunk's RMS 0..1.
float speakerWrite(const int16_t* samples, size_t count);
void speakerStop();

void setVolume(uint8_t percent);
uint8_t volume();

}  // namespace audio
