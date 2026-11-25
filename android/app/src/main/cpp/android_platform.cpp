#include "android_platform.h"

#include <sys/stat.h>

#include <ctime>

#include "cp932.h"
#include "jni_bridge.h"

namespace ore {
namespace {

bool face_is_serif(const std::string &face) {
	// "ＭＳ 明朝", "MS Mincho"
	return face.find("\x96\xbe\x92\xa9") != std::string::npos ||  // 明朝 (cp932)
	       face.find("Mincho") != std::string::npos || face.find("MINCHO") != std::string::npos;
}

}  // namespace

AndroidPlatform::AndroidPlatform(std::string save_dir) : save_dir_(std::move(save_dir)) {
	mkdir(save_dir_.c_str(), 0755);
}

uint32_t AndroidPlatform::decode_char(uint16_t code, TextEncoding) {
	uint32_t cp = cp932_decode(code);
	return cp ? cp : '?';
}

std::string AndroidPlatform::to_utf8(const std::string &text, TextEncoding) {
	return cp932_to_utf8(text);
}

std::string AndroidPlatform::from_utf8(const std::string &utf8, TextEncoding) {
	return utf8_to_cp932(utf8);
}

bool AndroidPlatform::render_glyph(uint32_t codepoint, const std::string &face, int size, int weight,
                                   GlyphBitmap &out) {
	return jni_render_glyph(codepoint, face_is_serif(face), weight >= 600, size, out);
}

void AndroidPlatform::begin_dialog(const std::string &utf8_text, int kind) {
	if (!dialog_handler_) {
		dialog_result_ = 1;
		return;
	}
	dialog_result_ = -1;
	dialog_handler_(utf8_text, kind);
}

void AndroidPlatform::begin_text_input(const std::string &utf8_initial) {
	if (!text_handler_) {
		text_result_ = 0;
		return;
	}
	text_result_ = -1;
	text_handler_(utf8_initial);
}

int AndroidPlatform::poll_text_input(std::string &utf8_result) {
	if (text_result_ == 1) utf8_result = text_value_;
	return text_result_;
}

int64_t android_now_ms() {
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

int64_t AndroidPlatform::now_ms() {
	return android_now_ms();
}

Platform::LocalTime AndroidPlatform::local_time() {
	time_t t = time(nullptr);
	tm l;
	localtime_r(&t, &l);
	return LocalTime{l.tm_year + 1900, l.tm_mon + 1, l.tm_mday, l.tm_hour, l.tm_min, l.tm_sec, l.tm_wday};
}

HttpSend android_http_sender() {
	return jni_http_send;
}

HttpSend AndroidPlatform::http_sender() {
	return android_http_sender();
}

AnkiAppHost android_anki_host() {
	AnkiAppHost host;
	host.ankidroid = jni_ankidroid;
	return host;
}

AnkiAppHost AndroidPlatform::anki_app() {
	return android_anki_host();
}

}  // namespace ore
