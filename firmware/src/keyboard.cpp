#include "keyboard.h"

#include <string.h>

namespace geist {
namespace kb {
namespace {

constexpr int ROW_KEYS = 11;
constexpr int CHAR_ROWS = 3;
constexpr int CHAR_KEYS = ROW_KEYS * CHAR_ROWS;  // 33
constexpr int BOTTOM_KEYS = 7;

// lower / upper pairs, row by row (QWERTZ, since umlauts matter)
const char* const kLetters[CHAR_KEYS][2] = {
    {"q", "Q"}, {"w", "W"}, {"e", "E"}, {"r", "R"}, {"t", "T"}, {"z", "Z"},
    {"u", "U"}, {"i", "I"}, {"o", "O"}, {"p", "P"}, {"\xC3\xBC", "\xC3\x9C"},  // ü
    {"a", "A"}, {"s", "S"}, {"d", "D"}, {"f", "F"}, {"g", "G"}, {"h", "H"},
    {"j", "J"}, {"k", "K"}, {"l", "L"}, {"\xC3\xB6", "\xC3\x96"}, {"\xC3\xA4", "\xC3\x84"},  // ö ä
    {"y", "Y"}, {"x", "X"}, {"c", "C"}, {"v", "V"}, {"b", "B"}, {"n", "N"},
    {"m", "M"}, {"\xC3\x9F", "\xC3\x9F"}, {",", ";"}, {".", ":"}, {"?", "!"},  // ß
};

const char* const kNumbers[CHAR_KEYS] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\xE2\x82\xAC",  // €
    "-", "/", ":", ";", "(", ")", "&", "@", "\"", "'", "!",
    "+", "=", "*", "#", "%", "_", "<", ">", "\xC2\xBF", "\xC2\xA1", "?",  // ¿ ¡
};

const char* const kAccents[CHAR_KEYS][2] = {
    {"\xC3\xA1", "\xC3\x81"}, {"\xC3\xA9", "\xC3\x89"}, {"\xC3\xAD", "\xC3\x8D"},  // á é í
    {"\xC3\xB3", "\xC3\x93"}, {"\xC3\xBA", "\xC3\x9A"}, {"\xC3\xA0", "\xC3\x80"},  // ó ú à
    {"\xC3\xA8", "\xC3\x88"}, {"\xC3\xAC", "\xC3\x8C"}, {"\xC3\xB2", "\xC3\x92"},  // è ì ò
    {"\xC3\xB9", "\xC3\x99"}, {"\xC3\xB1", "\xC3\x91"},                            // ù ñ
    {"\xC3\xA2", "\xC3\x82"}, {"\xC3\xAA", "\xC3\x8A"}, {"\xC3\xAE", "\xC3\x8E"},  // â ê î
    {"\xC3\xB4", "\xC3\x94"}, {"\xC3\xBB", "\xC3\x9B"}, {"\xC3\xA7", "\xC3\x87"},  // ô û ç
    {"\xC3\xAB", "\xC3\x8B"}, {"\xC3\xAF", "\xC3\x8F"}, {"\xC3\xA3", "\xC3\x83"},  // ë ï ã
    {"\xC3\xB5", "\xC3\x95"}, {"\xC5\x93", "\xC5\x92"},                            // õ œ
    {"\xC2\xAB", "\xC2\xAB"}, {"\xC2\xBB", "\xC2\xBB"}, {"\xE2\x80\xA6", "\xE2\x80\xA6"},  // « » …
    {"\xE2\x80\x93", "\xE2\x80\x93"}, {"\xC2\xB0", "\xC2\xB0"}, {"~", "~"},              // – ° ~
    {"^", "^"}, {"|", "|"}, {"$", "$"}, {"\xC5\x82", "\xC5\x81"}, {"\xC5\x9F", "\xC5\x9E"},  // ł ş
};

const char* const kCyrillic[CHAR_KEYS][2] = {  // ЙЦУКЕН
    {"\xD0\xB9", "\xD0\x99"}, {"\xD1\x86", "\xD0\xA6"}, {"\xD1\x83", "\xD0\xA3"}, {"\xD0\xBA", "\xD0\x9A"}, {"\xD0\xB5", "\xD0\x95"}, {"\xD0\xBD", "\xD0\x9D"}, {"\xD0\xB3", "\xD0\x93"}, {"\xD1\x88", "\xD0\xA8"}, {"\xD1\x89", "\xD0\xA9"}, {"\xD0\xB7", "\xD0\x97"}, {"\xD1\x85", "\xD0\xA5"},
    {"\xD1\x84", "\xD0\xA4"}, {"\xD1\x8B", "\xD0\xAB"}, {"\xD0\xB2", "\xD0\x92"}, {"\xD0\xB0", "\xD0\x90"}, {"\xD0\xBF", "\xD0\x9F"}, {"\xD1\x80", "\xD0\xA0"}, {"\xD0\xBE", "\xD0\x9E"}, {"\xD0\xBB", "\xD0\x9B"}, {"\xD0\xB4", "\xD0\x94"}, {"\xD0\xB6", "\xD0\x96"}, {"\xD1\x8D", "\xD0\xAD"},
    {"\xD1\x8F", "\xD0\xAF"}, {"\xD1\x87", "\xD0\xA7"}, {"\xD1\x81", "\xD0\xA1"}, {"\xD0\xBC", "\xD0\x9C"}, {"\xD0\xB8", "\xD0\x98"}, {"\xD1\x82", "\xD0\xA2"}, {"\xD1\x8C", "\xD0\xAC"}, {"\xD0\xB1", "\xD0\x91"}, {"\xD1\x8E", "\xD0\xAE"}, {"\xD1\x8A", "\xD0\xAA"}, {"\xD1\x91", "\xD0\x81"},
};

Key g_keys[PAGE_COUNT][CHAR_KEYS + BOTTOM_KEYS];
bool g_built = false;

void build() {
  for (int p = 0; p < PAGE_COUNT; p++) {
    for (int i = 0; i < CHAR_KEYS; i++) {
      const char* lo;
      const char* up;
      if (p == LETTERS) {
        lo = kLetters[i][0];
        up = kLetters[i][1];
      } else if (p == NUMBERS) {
        lo = up = kNumbers[i];
      } else if (p == ACCENTS) {
        lo = kAccents[i][0];
        up = kAccents[i][1];
      } else {
        lo = kCyrillic[i][0];
        up = kCyrillic[i][1];
      }
      g_keys[p][i] = Key{CHAR, lo, up, 2};
    }
    Key* b = &g_keys[p][CHAR_KEYS];
    b[0] = Key{SHIFT, "", "", 3};
    b[1] = Key{PAGE, "", "", 3};
    b[2] = Key{SPACE, " ", " ", 6};
    b[3] = Key{BACKSPACE, "", "", 3};
    b[4] = Key{SETTINGS, "", "", 2};
    b[5] = Key{CLOSE, "", "", 2};
    b[6] = Key{SEND, "", "", 3};
  }
  g_built = true;
}

}  // namespace

int keyCount() { return CHAR_KEYS + BOTTOM_KEYS; }

const Key& key(int page, int index) {
  if (!g_built) build();
  if (page < 0 || page >= PAGE_COUNT) page = 0;
  if (index < 0 || index >= keyCount()) index = 0;
  return g_keys[page][index];
}

int g_left = 4, g_width = 312, g_top = 46, g_rowH = 31, g_gap = 3;

void setLayout(int w, int h) {
  g_left = w < 300 ? 2 : 4;
  g_width = w - 2 * g_left;
  g_top = h < 160 ? 30 : 46;
  g_rowH = (h - g_top - 1) / 4;
  g_gap = w < 300 ? 2 : 3;
}

Rect keyRect(int index) {
  const int left = g_left, width = g_width, top = g_top, rowH = g_rowH, gap = g_gap;
  int row = index < CHAR_KEYS ? index / ROW_KEYS : CHAR_ROWS;
  int first = row < CHAR_ROWS ? row * ROW_KEYS : CHAR_KEYS;
  int units = 0;
  for (int i = first; i < index; i++) units += key(0, i).halfUnits;
  int u = key(0, index).halfUnits;
  int x0 = left + units * width / 22;
  int x1 = left + (units + u) * width / 22;
  return Rect{x0 + 1, top + row * rowH, x1 - x0 - gap, rowH - gap};
}

const char* pageKeyLabel(int page) {
  switch (page) {
    case LETTERS: return "123";
    case NUMBERS: return "\xC3\xA1\xC3\xA9";  // áé
    case ACCENTS: return "\xD0\xB0\xD0\xB1\xD0\xB2";  // абв
    default: return "abc";
  }
}

void popUtf8(char* s) {
  size_t n = strlen(s);
  if (!n) return;
  n--;
  while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
  s[n] = 0;
}

}  // namespace kb
}  // namespace geist
