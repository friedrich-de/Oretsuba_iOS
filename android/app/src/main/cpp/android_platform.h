// The engine's platform on Android. Letters are drawn by the system
// (android.graphics, the device's Japanese fonts) and HTTP goes through
// Java's HttpURLConnection, both reached over JNI (jni_bridge); the game's
// Shift-JIS text is converted with a table of the app's own (cp932).
#pragma once

#include <functional>
#include <string>

#include "platform/platform.h"

namespace ore {

class AndroidPlatform : public Platform {
public:
	explicit AndroidPlatform(std::string save_dir);

	uint32_t decode_char(uint16_t code, TextEncoding enc) override;
	std::string to_utf8(const std::string &text, TextEncoding enc) override;
	std::string from_utf8(const std::string &utf8, TextEncoding enc) override;
	bool render_glyph(uint32_t codepoint, const std::string &face, int size, int weight, GlyphBitmap &out) override;
	int64_t now_ms() override;
	std::string save_dir() override { return save_dir_; }
	LocalTime local_time() override;

	// Message boxes and text entry are shown by the app: the engine asks
	// through these handlers and the app answers with answer_*().
	using DialogHandler = std::function<void(const std::string &utf8, int kind)>;
	using TextInputHandler = std::function<void(const std::string &utf8_initial)>;
	void set_dialog_handler(DialogHandler h) { dialog_handler_ = std::move(h); }
	void set_text_input_handler(TextInputHandler h) { text_handler_ = std::move(h); }
	void answer_dialog(int result) { dialog_result_ = result; }
	void answer_text_input(bool ok, const std::string &utf8) {
		text_result_ = ok ? 1 : 0;
		text_value_ = utf8;
	}
	void begin_dialog(const std::string &utf8_text, int kind) override;
	int poll_dialog() override { return dialog_result_; }
	void begin_text_input(const std::string &utf8_initial) override;
	int poll_text_input(std::string &utf8_result) override;
	HttpSend http_sender() override;
	AnkiAppHost anki_app() override;

private:
	std::string save_dir_;
	DialogHandler dialog_handler_;
	TextInputHandler text_handler_;
	int dialog_result_ = 1;
	int text_result_ = 0;
	std::string text_value_;
};

// Sends one request and waits for its answer. Needs nothing but the Java
// machine, so it may be called from any thread for as long as the app runs.
HttpSend android_http_sender();

// AnkiDroid on this device, asked through the app's Kotlin (AnkiDroid.kt). Like
// the sender above it needs nothing but the Java machine.
AnkiAppHost android_anki_host();

// Monotonic milliseconds, the clock of now_ms() and of the frames.
int64_t android_now_ms();

}  // namespace ore
