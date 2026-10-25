// LilyGO T-Embed implementation (see hal_m5.cpp for the M5Stack boards).
#include "board.h"
#if GEIST_HAL_LILYGO

#include "audio.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

#include "board.h"
#include "config.h"

#if MIC_ES7210
#include "es7210.h"
#endif

namespace audio {
namespace {

uint8_t g_volume = DEFAULT_VOLUME;
uint32_t g_spkRate = 0;

// DC blocker state
float dcX = 0, dcY = 0;

inline int16_t sat16(float v) {
  if (v > 32767.f) return 32767;
  if (v < -32768.f) return -32768;
  return (int16_t)v;
}

bool initMic() {
#if MIC_ES7210
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  audio_hal_codec_config_t cfg = {};
  cfg.adc_input = AUDIO_HAL_ADC_INPUT_ALL;
  cfg.codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE;
  cfg.i2s_iface.mode = AUDIO_HAL_MODE_SLAVE;
  cfg.i2s_iface.fmt = AUDIO_HAL_I2S_NORMAL;
  cfg.i2s_iface.samples = AUDIO_HAL_16K_SAMPLES;
  cfg.i2s_iface.bits = AUDIO_HAL_BIT_LENGTH_16BITS;
  esp_err_t err = ESP_OK;
  err |= es7210_adc_init(&Wire, &cfg);
  err |= es7210_adc_config_i2s(cfg.codec_mode, &cfg.i2s_iface);
  err |= es7210_mic_select((es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2));
  err |= es7210_adc_set_gain((es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2),
                             MIC_ES7210_GAIN);
  err |= es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START);
  if (err != ESP_OK) Serial.println("[audio] ES7210 init reported an error");

  i2s_config_t ic = {};
  ic.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  ic.sample_rate = MIC_SAMPLE_RATE;
  ic.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  ic.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  ic.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  ic.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  ic.dma_buf_count = 8;
  ic.dma_buf_len = 256;
  ic.use_apll = false;
  ic.tx_desc_auto_clear = false;
  ic.fixed_mclk = 0;
  ic.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  ic.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = PIN_MIC_MCLK;
  pins.bck_io_num = PIN_MIC_BCLK;
  pins.ws_io_num = PIN_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_MIC_DIN;
#elif MIC_PDM
  i2s_config_t ic = {};
  ic.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
  ic.sample_rate = MIC_SAMPLE_RATE;
  ic.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  ic.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  ic.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
  ic.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  ic.dma_buf_count = 8;
  ic.dma_buf_len = 256;
  ic.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_PIN_NO_CHANGE;
  pins.ws_io_num = PIN_MIC_CLK;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_MIC_DATA;
#endif
  if (i2s_driver_install(MIC_I2S_PORT, &ic, 0, nullptr) != ESP_OK) return false;
  if (i2s_set_pin(MIC_I2S_PORT, &pins) != ESP_OK) return false;
  i2s_stop(MIC_I2S_PORT);
  return true;
}

bool initSpeaker() {
  i2s_config_t ic = {};
  ic.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  ic.sample_rate = 22050;
  ic.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  ic.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  ic.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  ic.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  ic.dma_buf_count = 8;
  ic.dma_buf_len = 512;
  ic.use_apll = false;
  ic.tx_desc_auto_clear = true;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_SPK_BCLK;
  pins.ws_io_num = PIN_SPK_WS;
  pins.data_out_num = PIN_SPK_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_driver_install(SPK_I2S_PORT, &ic, 0, nullptr) != ESP_OK) return false;
  if (i2s_set_pin(SPK_I2S_PORT, &pins) != ESP_OK) return false;
  i2s_zero_dma_buffer(SPK_I2S_PORT);
  g_spkRate = 22050;
  return true;
}

}  // namespace

bool begin() {
  bool okMic = initMic();
  bool okSpk = initSpeaker();
  Serial.printf("[audio] mic %s, speaker %s\n", okMic ? "ok" : "FAILED", okSpk ? "ok" : "FAILED");
  return okMic && okSpk;
}

// ---------------------------------------------------------------- mic
void micStart() {
  dcX = dcY = 0;
  i2s_start(MIC_I2S_PORT);
  i2s_zero_dma_buffer(MIC_I2S_PORT);
}

void micStop() { i2s_stop(MIC_I2S_PORT); }

size_t micRead(int16_t* dst, size_t maxSamples, float* rms) {
#if MIC_ES7210
  // stereo frames in, mono out
  static int16_t raw[512 * 2];
  if (maxSamples > 512) maxSamples = 512;
  size_t bytes = 0;
  i2s_read(MIC_I2S_PORT, raw, maxSamples * 2 * sizeof(int16_t), &bytes, pdMS_TO_TICKS(200));
  size_t n = bytes / (2 * sizeof(int16_t));
#else
  static int16_t raw[512];
  if (maxSamples > 512) maxSamples = 512;
  size_t bytes = 0;
  i2s_read(MIC_I2S_PORT, raw, maxSamples * sizeof(int16_t), &bytes, pdMS_TO_TICKS(200));
  size_t n = bytes / sizeof(int16_t);
#endif
  double acc = 0;
  for (size_t i = 0; i < n; i++) {
#if MIC_ES7210
    float x = ((float)raw[2 * i] + (float)raw[2 * i + 1]) * 0.5f;
#else
    float x = (float)raw[i];
#endif
    // one-pole DC blocker, then gain
    float y = x - dcX + 0.995f * dcY;
    dcX = x;
    dcY = y;
    int16_t s = sat16(y * MIC_DIGITAL_GAIN);
    dst[i] = s;
    acc += (double)s * s;
  }
  if (rms) *rms = n ? sqrtf((float)(acc / n)) : 0.f;
  return n;
}

// ---------------------------------------------------------------- speaker
void speakerStart(uint32_t sampleRate) {
  if (sampleRate != g_spkRate) {
    i2s_set_clk(SPK_I2S_PORT, sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    g_spkRate = sampleRate;
  }
  i2s_zero_dma_buffer(SPK_I2S_PORT);
  i2s_start(SPK_I2S_PORT);
}

float speakerWrite(const int16_t* samples, size_t count) {
  static int16_t frame[256 * 2];
  // perceptual volume curve
  float v = g_volume / 100.f;
  float gain = v * v;
  double acc = 0;
  size_t done = 0;
  while (done < count) {
    size_t n = count - done;
    if (n > 256) n = 256;
    for (size_t i = 0; i < n; i++) {
      int16_t s = sat16(samples[done + i] * gain);
      frame[2 * i] = s;
      frame[2 * i + 1] = s;
      acc += (double)samples[done + i] * samples[done + i];
    }
    size_t written = 0;
    i2s_write(SPK_I2S_PORT, frame, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    done += n;
  }
  return count ? sqrtf((float)(acc / count)) / 32768.f : 0.f;
}

void speakerStop() {
  i2s_zero_dma_buffer(SPK_I2S_PORT);
  i2s_stop(SPK_I2S_PORT);
}

void setVolume(uint8_t percent) { g_volume = percent > 100 ? 100 : percent; }
uint8_t volume() { return g_volume; }

}  // namespace audio

#endif  // GEIST_HAL_LILYGO
