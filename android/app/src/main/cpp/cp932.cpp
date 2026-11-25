#include "cp932.h"

#include <mutex>
#include <unordered_map>

namespace ore {
namespace {

constexpr int kTrailFirst = 0x40, kTrailLast = 0xFC;
constexpr int kTrails = kTrailLast - kTrailFirst + 1;

const uint16_t kTable[60][kTrails] = {
#include "cp932_table.inc"
};

// Row of a lead byte in the table, or -1.
int lead_row(int lead) {
	if (lead >= 0x81 && lead <= 0x9F) return lead - 0x81;
	if (lead >= 0xE0 && lead <= 0xFC) return lead - 0xE0 + 31;
	return -1;
}

int row_lead(int row) {
	return row < 31 ? row + 0x81 : row - 31 + 0xE0;
}

void append_utf8(std::string &out, uint32_t cp) {
	if (cp < 0x80) {
		out += char(cp);
	} else if (cp < 0x800) {
		out += char(0xC0 | cp >> 6);
		out += char(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += char(0xE0 | cp >> 12);
		out += char(0x80 | (cp >> 6 & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	} else {
		out += char(0xF0 | cp >> 18);
		out += char(0x80 | (cp >> 12 & 0x3F));
		out += char(0x80 | (cp >> 6 & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	}
}

// Unicode to the two-byte code Windows writes for it. The code page holds some
// characters several times: the lowest code wins, but the block 0xED00..0xEEFF
// (NEC's copy of IBM's extension) loses to IBM's own block at 0xFA00 and after.
const std::unordered_map<uint16_t, uint16_t> &encode_map() {
	static std::unordered_map<uint16_t, uint16_t> map;
	static std::once_flag once;
	std::call_once(once, [] {
		for (int pass = 0; pass < 2; ++pass)
			for (int row = 0; row < 60; ++row) {
				int lead = row_lead(row);
				bool nec_copy = lead == 0xED || lead == 0xEE;
				if (nec_copy != (pass == 1)) continue;
				for (int i = 0; i < kTrails; ++i)
					if (kTable[row][i]) map.emplace(kTable[row][i], uint16_t(lead << 8 | (i + kTrailFirst)));
			}
	});
	return map;
}

}  // namespace

uint32_t cp932_decode(uint16_t code) {
	if (code < 0x80) return code;
	if (code >= 0xA1 && code <= 0xDF) return 0xFF61 + (code - 0xA1);  // half-width katakana
	if (code <= 0xFF) return 0;
	int row = lead_row(code >> 8), trail = code & 0xFF;
	if (row < 0 || trail < kTrailFirst || trail > kTrailLast) return 0;
	return kTable[row][trail - kTrailFirst];
}

std::string cp932_to_utf8(const std::string &text) {
	std::string out;
	out.reserve(text.size() * 3 / 2 + 1);
	for (size_t i = 0; i < text.size();) {
		uint8_t c = uint8_t(text[i]);
		uint16_t code = c;
		size_t used = 1;
		if (lead_row(c) >= 0 && i + 1 < text.size()) {
			code = uint16_t(c << 8 | uint8_t(text[i + 1]));
			used = 2;
		}
		uint32_t cp = cp932_decode(code);
		if (!cp && used == 2) {
			// Not a character: the lead byte alone becomes '?', the next byte is read again.
			used = 1;
		}
		append_utf8(out, cp ? cp : (code == 0 ? 0 : '?'));
		i += used;
	}
	return out;
}

std::string utf8_to_cp932(const std::string &utf8) {
	const auto &map = encode_map();
	std::string out;
	out.reserve(utf8.size());
	for (size_t i = 0; i < utf8.size();) {
		uint8_t c = uint8_t(utf8[i]);
		uint32_t cp = c;
		size_t len = 1;
		if (c >= 0xF0 && c < 0xF8)
			cp = c & 0x07, len = 4;
		else if (c >= 0xE0)
			cp = c & 0x0F, len = 3;
		else if (c >= 0xC0)
			cp = c & 0x1F, len = 2;
		bool whole = len == 1 ? c < 0x80 : i + len <= utf8.size();
		for (size_t k = 1; whole && k < len; ++k) {
			uint8_t next = uint8_t(utf8[i + k]);
			if ((next & 0xC0) != 0x80) whole = false;
			cp = cp << 6 | (next & 0x3F);
		}
		if (!whole) {
			out += '?';
			++i;
			continue;
		}
		i += len;
		if (cp < 0x80) {
			out += char(cp);
		} else if (cp >= 0xFF61 && cp <= 0xFF9F) {
			out += char(0xA1 + (cp - 0xFF61));
		} else {
			auto it = cp < 0x10000 ? map.find(uint16_t(cp)) : map.end();
			if (it == map.end()) {
				out += '?';
			} else {
				out += char(it->second >> 8);
				out += char(it->second & 0xFF);
			}
		}
	}
	return out;
}

}  // namespace ore
