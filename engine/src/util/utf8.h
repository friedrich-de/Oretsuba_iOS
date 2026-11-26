// UTF-8 by the character, for the mining kit: its texts are UTF-8 throughout.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ore::utf8 {

inline void append(std::string &out, uint32_t cp) {
	if (cp < 0x80) {
		out += char(cp);
	} else if (cp < 0x800) {
		out += char(0xC0 | (cp >> 6));
		out += char(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += char(0xE0 | (cp >> 12));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	} else {
		out += char(0xF0 | (cp >> 18));
		out += char(0x80 | ((cp >> 12) & 0x3F));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	}
}

// The character at p, which moves past it. Bytes that are no UTF-8 give U+FFFD one at a time.
inline uint32_t next(const char *&p, const char *end) {
	unsigned char c = (unsigned char)*p++;
	if (c < 0x80) return c;
	int more = c >= 0xF0 && c < 0xF8 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 && c < 0xE0 ? 1 : -1;
	if (more < 0 || c >= 0xF8 || end - p < more) return 0xFFFD;
	uint32_t cp = more == 3 ? c & 0x07u : more == 2 ? c & 0x0Fu : c & 0x1Fu;
	for (int i = 0; i < more; ++i) {
		unsigned char t = (unsigned char)p[i];
		if ((t & 0xC0) != 0x80) return 0xFFFD;
		cp = cp << 6 | (t & 0x3Fu);
	}
	p += more;
	return cp;
}

inline std::u32string decode(std::string_view text) {
	std::u32string out;
	out.reserve(text.size());
	const char *p = text.data(), *end = p + text.size();
	while (p < end) out += char32_t(next(p, end));
	return out;
}

inline std::string encode(std::u32string_view text) {
	std::string out;
	out.reserve(text.size() * 3);
	for (char32_t c : text) append(out, uint32_t(c));
	return out;
}

// How many characters a text has.
inline size_t length(std::string_view text) {
	size_t n = 0;
	for (unsigned char c : text)
		if ((c & 0xC0) != 0x80) ++n;
	return n;
}

// How many UTF-16 units a text takes, which is how JavaScript counts it.
inline size_t length16(std::string_view text) {
	size_t n = 0;
	for (unsigned char c : text)
		if ((c & 0xC0) != 0x80) n += c >= 0xF0 ? 2 : 1;
	return n;
}

}  // namespace ore::utf8
