// The test host's platform on Linux, so that the engine can be run where no
// Mac is at hand (a container, a build server). Letters come from FreeType and
// a font file; they do not look like the original's, the rest does.
#pragma once

#include <map>
#include <mutex>
#include <string>

#include "platform/platform.h"

namespace ore {

class LinuxPlatform : public Platform {
public:
	explicit LinuxPlatform(std::string save_dir);
	~LinuxPlatform() override;

	uint32_t decode_char(uint16_t code, TextEncoding enc) override;
	std::string to_utf8(const std::string &text, TextEncoding enc) override;
	std::string from_utf8(const std::string &utf8, TextEncoding enc) override;
	bool render_glyph(uint32_t codepoint, const std::string &face, int size, int weight, GlyphBitmap &out) override;
	int64_t now_ms() override;
	std::string save_dir() override { return save_dir_; }
	LocalTime local_time() override;

private:
	std::string save_dir_;
	std::mutex mu_;
	void *library_ = nullptr;  // FT_Library
	void *face_ = nullptr;     // FT_Face, null when no font file was found
	std::map<uint32_t, uint32_t> char_cache_;
};

}  // namespace ore
