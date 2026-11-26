// Shift-JIS as Windows reads it (code page 932), the encoding of the game's
// text. Android's C library converts no Japanese encodings, so the app carries
// the table (cp932_table.inc, written by tools/android/make_cp932.py).
#pragma once

#include <cstdint>
#include <string>

namespace ore {

// One character of game text (one byte, or lead byte << 8 | trail byte) as a
// Unicode code point; 0 when the code page has no such character.
uint32_t cp932_decode(uint16_t code);

// Whole strings. A byte or character the other side lacks becomes '?'.
std::string cp932_to_utf8(const std::string &text);
std::string utf8_to_cp932(const std::string &utf8);

}  // namespace ore
