// The test host's platform on Windows. It draws letters with GDI, the way the
// original game does (docs/re/40_text.md §9), so that on a Windows machine the
// port's pictures can be put next to the original's.
#pragma once

#include <map>
#include <mutex>
#include <string>

#include "platform/platform.h"

namespace ore {

class WinPlatform : public Platform {
public:
	explicit WinPlatform(std::string save_dir);
	~WinPlatform() override;

	uint32_t decode_char(uint16_t code, TextEncoding enc) override;
	std::string to_utf8(const std::string &text, TextEncoding enc) override;
	std::string from_utf8(const std::string &utf8, TextEncoding enc) override;
	bool render_glyph(uint32_t codepoint, const std::string &face, int size, int weight, GlyphBitmap &out) override {
		return render_glyph_aa(codepoint, face, size, weight, 1, out);
	}
	bool render_glyph_aa(uint32_t codepoint, const std::string &face, int size, int weight, int antialias,
	                     GlyphBitmap &out) override;
	int64_t now_ms() override;
	std::string save_dir() override { return save_dir_; }
	LocalTime local_time() override;

	// What GDI answers for a face (in the game's encoding) at sizes `from` to `to`:
	// the face it really chose, its metrics next to the port's own figure for the
	// ascent, and for a few letters the box, the advance and how many grey levels
	// the 17-level picture uses (two means a bitmap stored in the font). One line
	// per fact, tab separated, in UTF-8.
	std::string font_report(const std::string &face, int from, int to);

private:
	void *font_for(const std::string &face, int size, int weight);
	std::string save_dir_;
	std::mutex mu_;
	void *dc_ = nullptr;                   // memory HDC
	std::map<std::string, void *> fonts_;  // HFONT cache
	std::map<uint32_t, uint32_t> char_cache_;
};

}  // namespace ore
