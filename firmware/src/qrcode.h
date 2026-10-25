/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2017 Richard Moore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 *  Special thanks to Nayuki (https://www.nayuki.io/) from which this library was
 *  heavily inspired and compared against.
 *
 *  See: https://github.com/nayuki/QR-Code-generator/tree/master/cpp
 */

/**
 * original source is here : https://github.com/ricmoo/QRCode/
 * Modified for LGFX  by lovyan03, 2020
 */


// Minimal QR code generator (ricmoo/QRCode via LovyanGFX), namespaced for GeistAI.
#pragma once
#include <stdint.h>

namespace geistqr {

#define QR_MODE_NUMERIC 0
#define QR_MODE_ALPHANUMERIC 1
#define QR_MODE_BYTE 2
#define QR_ECC_LOW 0
#define QR_ECC_MEDIUM 1
#define QR_ECC_QUARTILE 2
#define QR_ECC_HIGH 3

struct QRCode {
  uint8_t version;
  uint8_t size;
  uint8_t ecc;
  uint8_t mode;
  uint8_t mask;
  uint8_t* modules;
};

uint16_t qr_getBufferSize(uint8_t version);
int8_t qr_initText(QRCode* qrcode, uint8_t* modules, uint8_t version, uint8_t ecc, const char* data);
int8_t qr_initBytes(QRCode* qrcode, uint8_t* modules, uint8_t version, uint8_t ecc, uint8_t* data, uint16_t length);
bool qr_getModule(QRCode* qrcode, uint_fast8_t x, uint_fast8_t y);

}  // namespace geistqr
