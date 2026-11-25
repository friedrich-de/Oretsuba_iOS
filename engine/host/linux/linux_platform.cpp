#include "linux_platform.h"

#include <iconv.h>
#include <sys/stat.h>

#include <cstdlib>
#include <ctime>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "util/log.h"

namespace ore {
namespace {

// The game's text is Shift-JIS as Windows reads it.
std::string convert(const std::string &text, const char *to, const char *from) {
	if (text.empty()) return std::string();
	iconv_t cd = iconv_open(to, from);
	if (cd == iconv_t(-1)) return text;
	std::string out(text.size() * 4 + 4, '\0');
	char *in = const_cast<char *>(text.data());
	size_t in_left = text.size();
	char *dst = &out[0];
	size_t out_left = out.size();
	while (in_left > 0) {
		if (iconv(cd, &in, &in_left, &dst, &out_left) != size_t(-1)) break;
		if (out_left < 4) break;
		// A byte the code page lacks becomes '?'.
		*dst++ = '?', --out_left;
		++in, --in_left;
	}
	iconv_close(cd);
	out.resize(out.size() - out_left);
	return out;
}

// ORE_FONT names the font file; otherwise the first of a few usual places.
std::string font_file() {
	if (const char *env = getenv("ORE_FONT")) return env;
	const char *usual[] = {"/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
	                       "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
	                       "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
	                       "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"};
	struct stat st;
	for (const char *path : usual)
		if (stat(path, &st) == 0) return path;
	return std::string();
}

}  // namespace

LinuxPlatform::LinuxPlatform(std::string save_dir) : save_dir_(std::move(save_dir)) {
	mkdir(save_dir_.c_str(), 0755);
	FT_Library library = nullptr;
	if (FT_Init_FreeType(&library) != 0) return;
	library_ = library;
	std::string path = font_file();
	FT_Face face = nullptr;
	if (path.empty() || FT_New_Face(library, path.c_str(), 0, &face) != 0) {
		log_warn("no font file for the letters (set ORE_FONT); text stays empty");
		return;
	}
	face_ = face;
}

LinuxPlatform::~LinuxPlatform() {
	if (face_) FT_Done_Face(static_cast<FT_Face>(face_));
	if (library_) FT_Done_FreeType(static_cast<FT_Library>(library_));
}

uint32_t LinuxPlatform::decode_char(uint16_t code, TextEncoding) {
	std::lock_guard<std::mutex> lock(mu_);
	auto it = char_cache_.find(code);
	if (it != char_cache_.end()) return it->second;
	std::string bytes;
	if (code > 0xFF) bytes += char(code >> 8);
	bytes += char(code);
	std::string wide = convert(bytes, "UTF-32LE", "CP932");
	uint32_t cp = '?';
	if (wide.size() >= 4)
		cp = uint32_t(uint8_t(wide[0])) | uint32_t(uint8_t(wide[1])) << 8 | uint32_t(uint8_t(wide[2])) << 16 |
		     uint32_t(uint8_t(wide[3])) << 24;
	char_cache_[code] = cp;
	return cp;
}

std::string LinuxPlatform::to_utf8(const std::string &text, TextEncoding) {
	return convert(text, "UTF-8", "CP932");
}

std::string LinuxPlatform::from_utf8(const std::string &utf8, TextEncoding) {
	return convert(utf8, "CP932", "UTF-8");
}

bool LinuxPlatform::render_glyph(uint32_t codepoint, const std::string &, int size, int weight, GlyphBitmap &out) {
	std::lock_guard<std::mutex> lock(mu_);
	FT_Face face = static_cast<FT_Face>(face_);
	if (!face) return false;
	if (FT_Set_Pixel_Sizes(face, 0, FT_UInt(size)) != 0) return false;
	if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP) != 0) return false;
	const FT_GlyphSlot slot = face->glyph;
	out = GlyphBitmap();
	out.width = int(slot->bitmap.width);
	out.height = int(slot->bitmap.rows);
	out.left = slot->bitmap_left;
	out.top = slot->bitmap_top;
	out.advance = int((slot->advance.x + 32) >> 6);
	out.alpha.assign(size_t(out.width) * out.height, 0);
	for (int y = 0; y < out.height; ++y)
		for (int x = 0; x < out.width; ++x) {
			int v = slot->bitmap.buffer[y * slot->bitmap.pitch + x];
			// Bold as a one pixel smear to the right, as GDI embolds a face without a bold cut.
			if (weight >= 700 && x > 0) v = std::max(v, int(slot->bitmap.buffer[y * slot->bitmap.pitch + x - 1]));
			out.alpha[size_t(y) * out.width + x] = uint8_t(v);
		}
	return true;
}

int64_t LinuxPlatform::now_ms() {
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

Platform::LocalTime LinuxPlatform::local_time() {
	time_t t = time(nullptr);
	tm l;
	localtime_r(&t, &l);
	return LocalTime{l.tm_year + 1900, l.tm_mon + 1, l.tm_mday, l.tm_hour, l.tm_min, l.tm_sec, l.tm_wday};
}

}  // namespace ore
