#include "win_platform.h"

#include <windows.h>

#include <direct.h>

#include <ctime>
#include <vector>

namespace ore {
namespace {

const UINT kCodePage = 932;  // the game's text is Shift-JIS

std::wstring widen(const std::string &text, UINT code_page) {
	if (text.empty()) return std::wstring();
	int n = MultiByteToWideChar(code_page, 0, text.data(), int(text.size()), nullptr, 0);
	std::wstring out(size_t(n), L'\0');
	MultiByteToWideChar(code_page, 0, text.data(), int(text.size()), &out[0], n);
	return out;
}

std::string narrow(const std::wstring &text, UINT code_page) {
	if (text.empty()) return std::string();
	// Characters the code page lacks become '?'.
	int n = WideCharToMultiByte(code_page, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(size_t(n), '\0');
	WideCharToMultiByte(code_page, 0, text.data(), int(text.size()), &out[0], n, nullptr, nullptr);
	return out;
}

}  // namespace

WinPlatform::WinPlatform(std::string save_dir) : save_dir_(std::move(save_dir)) {
	_mkdir(save_dir_.c_str());
	dc_ = CreateCompatibleDC(nullptr);
}

WinPlatform::~WinPlatform() {
	for (auto &kv : fonts_) DeleteObject(static_cast<HFONT>(kv.second));
	if (dc_) DeleteDC(static_cast<HDC>(dc_));
}

uint32_t WinPlatform::decode_char(uint16_t code, TextEncoding) {
	std::lock_guard<std::mutex> lock(mu_);
	auto it = char_cache_.find(code);
	if (it != char_cache_.end()) return it->second;
	char bytes[2];
	int n = 0;
	if (code > 0xFF) bytes[n++] = char(code >> 8);
	bytes[n++] = char(code);
	wchar_t w[2] = {0, 0};
	int len = MultiByteToWideChar(kCodePage, 0, bytes, n, w, 2);
	uint32_t cp = '?';
	if (len == 2 && w[0] >= 0xD800 && w[0] < 0xDC00)
		cp = 0x10000 + ((uint32_t(w[0]) - 0xD800) << 10) + (uint32_t(w[1]) - 0xDC00);
	else if (len >= 1)
		cp = w[0];
	char_cache_[code] = cp;
	return cp;
}

std::string WinPlatform::to_utf8(const std::string &text, TextEncoding) {
	return narrow(widen(text, kCodePage), CP_UTF8);
}

std::string WinPlatform::from_utf8(const std::string &utf8, TextEncoding) {
	return narrow(widen(utf8, CP_UTF8), kCodePage);
}

void *WinPlatform::font_for(const std::string &face, int size, int weight) {
	std::string key = face + ":" + std::to_string(size) + ":" + std::to_string(weight);
	auto it = fonts_.find(key);
	if (it != fonts_.end()) return it->second;
	// The original's LOGFONT: height -size, Shift-JIS character set, quality 3.
	LOGFONTW lf = {};
	lf.lfHeight = -size;
	lf.lfWeight = weight;
	lf.lfCharSet = SHIFTJIS_CHARSET;
	lf.lfQuality = NONANTIALIASED_QUALITY;
	std::wstring name = widen(face, kCodePage);
	wcsncpy(lf.lfFaceName, name.c_str(), LF_FACESIZE - 1);
	HFONT font = CreateFontIndirectW(&lf);
	fonts_[key] = font;
	return font;
}

bool WinPlatform::render_glyph(uint32_t codepoint, const std::string &face, int size, int weight, GlyphBitmap &out) {
	std::lock_guard<std::mutex> lock(mu_);
	if (codepoint > 0xFFFF) return false;  // nothing in the game's code page lies above
	HDC dc = static_cast<HDC>(dc_);
	HFONT font = static_cast<HFONT>(font_for(face, size, weight));
	if (!dc || !font) return false;
	HGDIOBJ old = SelectObject(dc, font);
	wchar_t ch = wchar_t(codepoint);
	SIZE extent = {0, 0};
	GetTextExtentPoint32W(dc, &ch, 1, &extent);
	out = GlyphBitmap();
	out.advance = extent.cx;

	UINT format = glyph_levels_ == 0 ? GGO_BITMAP : glyph_levels_ == 2 ? GGO_GRAY8_BITMAP : GGO_GRAY4_BITMAP;
	int scale = glyph_levels_ == 2 ? 4 : 16;  // 65 levels times 4, 17 levels times 16
	const MAT2 identity = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
	GLYPHMETRICS gm = {};
	DWORD bytes = GetGlyphOutlineW(dc, ch, format, &gm, 0, nullptr, &identity);
	bool ok = bytes != GDI_ERROR;
	if (ok && bytes > 0) {
		std::vector<uint8_t> raw(bytes);
		ok = GetGlyphOutlineW(dc, ch, format, &gm, bytes, raw.data(), &identity) != GDI_ERROR;
		if (ok) {
			out.width = int(gm.gmBlackBoxX);
			out.height = int(gm.gmBlackBoxY);
			out.left = gm.gmptGlyphOrigin.x;
			out.top = gm.gmptGlyphOrigin.y;  // rows above the baseline
			out.alpha.assign(size_t(out.width) * out.height, 0);
			if (format == GGO_BITMAP) {
				size_t pitch = ((size_t(out.width) + 31) / 32) * 4;  // rows of bits, padded to 32
				for (int y = 0; y < out.height && (size_t(y) + 1) * pitch <= raw.size(); ++y)
					for (int x = 0; x < out.width; ++x)
						if (raw[size_t(y) * pitch + size_t(x) / 8] & (0x80 >> (x % 8)))
							out.alpha[size_t(y) * out.width + x] = 255;
			} else {
				size_t pitch = (size_t(out.width) + 3) & ~size_t(3);  // rows of bytes, padded to 4
				for (int y = 0; y < out.height && (size_t(y) + 1) * pitch <= raw.size(); ++y)
					for (int x = 0; x < out.width; ++x) {
						int v = raw[size_t(y) * pitch + x] * scale;
						out.alpha[size_t(y) * out.width + x] = uint8_t(v > 255 ? 255 : v);
					}
			}
		}
	}
	SelectObject(dc, old);
	return ok;
}

int64_t WinPlatform::now_ms() {
	return int64_t(GetTickCount64());
}

Platform::LocalTime WinPlatform::local_time() {
	SYSTEMTIME t;
	GetLocalTime(&t);
	return LocalTime{t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wDayOfWeek};
}

}  // namespace ore
