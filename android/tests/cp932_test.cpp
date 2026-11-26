// Checks the app's Shift-JIS table (app/src/main/cpp/cp932) against the
// computer's own converter, for every two-byte code and both directions.
// Runs on a PC, not on a device:
//
//   cd android && g++ -std=c++17 -Iapp/src/main/cpp tests/cp932_test.cpp app/src/main/cpp/cp932.cpp -o cp932_test
//   ./cp932_test
//
// (on a Mac add -liconv)
#include <iconv.h>

#include <cstdio>
#include <string>

#include "cp932.h"

namespace {

// False when the computer's converter has no such character.
bool convert(const std::string &text, const char *to, const char *from, std::string &out) {
	iconv_t cd = iconv_open(to, from);
	if (cd == iconv_t(-1)) {
		fprintf(stderr, "this computer converts no %s\n", from);
		exit(2);
	}
	out.assign(text.size() * 4 + 8, '\0');
	char *in = const_cast<char *>(text.data());
	size_t in_left = text.size();
	char *dst = &out[0];
	size_t out_left = out.size();
	bool ok = iconv(cd, &in, &in_left, &dst, &out_left) != size_t(-1) && in_left == 0;
	iconv_close(cd);
	out.resize(out.size() - out_left);
	return ok;
}

bool is_lead(int c) {
	return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

}  // namespace

int main() {
	int decoded = 0, encoded = 0, wrong = 0;
	for (int code = 0x8140; code <= 0xFCFC; ++code) {
		int lead = code >> 8, trail = code & 0xFF;
		if (!is_lead(lead) || trail < 0x40 || trail == 0x7F || trail > 0xFC) continue;
		std::string bytes{char(lead), char(trail)};
		uint32_t cp = ore::cp932_decode(uint16_t(code));
		std::string want;
		if (!convert(bytes, "UTF-8", "CP932", want)) {
			if (cp != 0 && ++wrong < 20) printf("%04X: U+%04X here, no character there\n", code, cp);
			continue;
		}
		// Windows keeps 0xF040..0xF9FC for characters of the user's own; the table leaves them out.
		if (cp == 0 && lead >= 0xF0 && lead <= 0xF9) continue;
		++decoded;
		std::string got = ore::cp932_to_utf8(bytes);
		if (got != want) {
			if (++wrong < 20) printf("%04X: \"%s\" here, \"%s\" there\n", code, got.c_str(), want.c_str());
			continue;
		}
		// Back again. Some characters are in the code page more than once; both sides must pick the same code.
		std::string back_want, back = ore::utf8_to_cp932(got);
		convert(want, "CP932", "UTF-8", back_want);
		++encoded;
		if (back != back_want && ++wrong < 20)
			printf("U+%04X: %02X%02X here, %02X%02X there\n", cp, uint8_t(back[0]), uint8_t(back[1]), uint8_t(back_want[0]),
			       uint8_t(back_want[1]));
	}

	// A line with a kanji, kana, ASCII, half-width katakana, the wave dash and "ソ",
	// whose second byte is the backslash's.
	const std::string line = "\x89\xb4\x82\xbd\x82\xbf ABC \xb1\xb2 \x81\x60 \x83\x5c";
	const std::string utf8 = "\xe4\xbf\xba\xe3\x81\x9f\xe3\x81\xa1 ABC \xef\xbd\xb1\xef\xbd\xb2 \xef\xbd\x9e \xe3\x82\xbd";
	if (ore::cp932_to_utf8(line) != utf8 || ore::utf8_to_cp932(utf8) != line) {
		printf("the sample line does not survive the round trip\n");
		++wrong;
	}
	// What the code page lacks becomes a question mark, and broken input does not derail the rest.
	if (ore::utf8_to_cp932("a\xf0\x9f\x98\x80z") != "a?z" || ore::cp932_to_utf8("\x81") != "?" ||
	    ore::cp932_to_utf8("\x81\xfd" "A") != "??A") {
		printf("characters without a counterpart are not handled as documented\n");
		++wrong;
	}

	printf("cp932: %d codes decoded, %d encoded back, %d wrong\n", decoded, encoded, wrong);
	return wrong ? 1 : 0;
}
