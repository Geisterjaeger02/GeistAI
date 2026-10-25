// GeistAI - pin maps for the supported LilyGO T-Embed variants.
// Select the variant with the PlatformIO environment (tembed / tembed_cc1101).
// Pins taken from LilyGO's official repos (Xinyuan-LilyGO/T-Embed and
// Xinyuan-LilyGO/T-Embed-CC1101).
#pragma once

#if defined(GEIST_BOARD_TEMBED)
// ---------------------------------------------------------- original T-Embed (ESP32-S3)
#define BOARD_NAME "T-Embed"
#define HOSTNAME "geistai-embed"
#define GEIST_HAL_LILYGO 1
#define BOARD_HAS_KEYBOARD 0
#define BOARD_HAS_SPEAKER 1

#define PIN_POWER_ON 46

// ST7789V 1.9" 170x320
#define PIN_LCD_BL 15
#define PIN_LCD_DC 13
#define PIN_LCD_CS 10
#define PIN_LCD_SCLK 12
#define PIN_LCD_MOSI 11
#define PIN_LCD_MISO -1
#define PIN_LCD_RST 9
#define LCD_BUS_SHARED 0
#define LCD_VENDOR_INIT 1  // LilyGO's tuned ST7789 init sequence

// Rotary encoder + push
#define PIN_ENC_A 2
#define PIN_ENC_B 1
#define PIN_ENC_BTN 0
#define PIN_BTN_BACK -1

// 7x APA102 LED ring
#define LED_APA102 1
#define PIN_LED_DATA 42
#define PIN_LED_CLK 45
#define LED_COUNT 7

// ES7210 microphone ADC (I2C control + I2S data)
#define MIC_ES7210 1
#define PIN_I2C_SDA 18
#define PIN_I2C_SCL 8
#define PIN_MIC_BCLK 47
#define PIN_MIC_WS 21
#define PIN_MIC_DIN 14
#define PIN_MIC_MCLK 48
#define MIC_I2S_PORT I2S_NUM_1

// I2S amplifier
#define PIN_SPK_BCLK 7
#define PIN_SPK_WS 5
#define PIN_SPK_DOUT 6
#define SPK_I2S_PORT I2S_NUM_0

#define PIN_BAT_ADC 4

#elif defined(GEIST_BOARD_TEMBED_CC1101)
// ---------------------------------------------------------- T-Embed CC1101 (ESP32-S3)
#define BOARD_NAME "T-Embed CC1101"
#define HOSTNAME "geistai-embed"
#define GEIST_HAL_LILYGO 1
#define BOARD_HAS_KEYBOARD 0
#define BOARD_HAS_SPEAKER 1

#define PIN_POWER_ON 15  // BOARD_PWR_EN

#define PIN_LCD_BL 21
#define PIN_LCD_DC 16
#define PIN_LCD_CS 41
#define PIN_LCD_SCLK 11
#define PIN_LCD_MOSI 9
#define PIN_LCD_MISO 10
#define PIN_LCD_RST -1
#define LCD_BUS_SHARED 1
#define LCD_VENDOR_INIT 0

// Other chip-selects on the shared SPI bus - kept high so they stay quiet.
#define PIN_SD_CS 13
#define PIN_RADIO_CS 12
#define PIN_NRF24_CS 44

#define PIN_ENC_A 4
#define PIN_ENC_B 5
#define PIN_ENC_BTN 0
#define PIN_BTN_BACK 6

// 8x WS2812
#define LED_WS2812 1
#define PIN_LED_DATA 14
#define LED_COUNT 8

// PDM microphone
#define MIC_PDM 1
#define PIN_MIC_CLK 39
#define PIN_MIC_DATA 42
#define MIC_I2S_PORT I2S_NUM_0  // PDM RX only works on I2S0

#define PIN_SPK_BCLK 46
#define PIN_SPK_WS 40
#define PIN_SPK_DOUT 7
#define SPK_I2S_PORT I2S_NUM_1

#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 18
#define PIN_BAT_ADC -1  // battery is read through the BQ27220 gauge (TODO)

#elif defined(GEIST_BOARD_CARDPUTER)
// ---------------------------------------------------------- M5Stack Cardputer (StampS3, no PSRAM)
// Display, keyboard, mic and speaker are handled by M5Unified / M5Cardputer.
#define BOARD_NAME "Cardputer"
#define HOSTNAME "geistai-cardputer"
#define GEIST_HAL_M5 1
#define BOARD_HAS_KEYBOARD 1
#define BOARD_HAS_SPEAKER 1
#define PIN_POWER_ON -1
#define PIN_BTN_BACK -1
#define LED_WS2812 1
#define PIN_LED_DATA 21
#define LED_COUNT 1

#elif defined(GEIST_BOARD_STICKCPLUS2)
// ---------------------------------------------------------- M5StickC Plus2 (ESP32-PICO-V3-02)
// Buttons A / B / PWR, PDM mic; only a buzzer, so voice replies default to off.
#define BOARD_NAME "StickC Plus2"
#define HOSTNAME "geistai-stick"
#define GEIST_HAL_M5 1
#define BOARD_HAS_KEYBOARD 0
#define BOARD_HAS_SPEAKER 0
#define PIN_POWER_ON -1
#define PIN_BTN_BACK -1
#define LED_COUNT 0

#else
#error "Select a board: build with one of the PlatformIO environments in platformio.ini"
#endif

#ifndef LED_COUNT
#define LED_COUNT 0
#endif
#ifndef GEIST_HAL_LILYGO
#define GEIST_HAL_LILYGO 0
#endif
#ifndef GEIST_HAL_M5
#define GEIST_HAL_M5 0
#endif
