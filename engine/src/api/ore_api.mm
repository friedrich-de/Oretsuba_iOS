// C interface of the engine for the apps: Swift on Apple devices, Kotlin
// (through JNI) on Android. The file is Objective-C++ by its name only; the
// Android build compiles it as C++.
#include "api/ore_api.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "api/ore_kit_internal.h"
#include "audio/app_sound.h"
#include "core/engine.h"
#include "mining/anki_app.h"
#include "mining/kit_tap.h"
#include "mining/mining.h"
#include "util/log.h"

// The platform of the app this is built into, and its HTTP for mining.
#if defined(__ANDROID__)
#include "android_platform.h"  // android/app/src/main/cpp
namespace {
using HostPlatform = ore::AndroidPlatform;
ore::HttpSend host_http_sender() { return ore::android_http_sender(); }
}  // namespace
#else
#include "platform/apple/apple_http.h"
#include "platform/apple/apple_platform.h"
namespace {
using HostPlatform = ore::ApplePlatform;
ore::HttpSend host_http_sender() { return ore::apple_http_sender(); }
}  // namespace
#endif

struct OreEngine {
	std::unique_ptr<HostPlatform> platform;
	std::unique_ptr<ore::Engine> engine;
	// Declared after the engine, so it ends first: it plays on the engine's mixer.
	std::unique_ptr<ore::AppSound> app_sound;
	std::string error;
};

extern "C" {

OreEngine *ore_create(const OreConfig *config, char *err, int err_len) {
	auto e = std::make_unique<OreEngine>();
	e->platform = std::make_unique<HostPlatform>(config->save_dir ? config->save_dir : "");
	ore::EngineConfig cfg;
	cfg.data_dir = config->data_dir ? config->data_dir : "";
	cfg.autosave_lines = config->autosave_lines;
	cfg.resume = config->resume != 0;
	e->engine = std::make_unique<ore::Engine>(*e->platform, cfg);
	std::string error;
	if (!e->engine->boot(&error)) {
		if (err && err_len > 0) {
			strncpy(err, error.c_str(), size_t(err_len) - 1);
			err[err_len - 1] = 0;
		}
		return nullptr;
	}
	e->app_sound = std::make_unique<ore::AppSound>(e->engine->mixer);
	return e.release();
}

void ore_destroy(OreEngine *e) {
	delete e;
}

int ore_frame(OreEngine *e, int64_t now_ms) {
	int r = e->engine->frame(now_ms);
	if (r < 0) e->error = e->engine->error();
	return r;
}

const char *ore_last_error(OreEngine *e) {
	return e->error.c_str();
}

const uint8_t *ore_framebuffer(OreEngine *e, int *width, int *height) {
	const ore::Image &img = e->engine->framebuffer();
	*width = img.width();
	*height = img.height();
	return img.bits();
}

uint64_t ore_frame_serial(OreEngine *e) {
	return e->engine->frame_serial();
}

void ore_mouse_move(OreEngine *e, int x, int y) {
	e->engine->mouse_move(x, y);
}

void ore_mouse_button(OreEngine *e, int button, int down) {
	e->engine->mouse_button(button, down != 0);
}

void ore_mouse_wheel(OreEngine *e, int delta) {
	e->engine->input.on_wheel(delta);
}

void ore_key(OreEngine *e, int vk, int down) {
	e->engine->input.on_key(vk, down != 0);
}

void ore_game_command(OreEngine *e, int command) {
	e->engine->game_command(command);
}

int ore_movie_playing(OreEngine *e) {
	return e->engine->movie ? 1 : 0;
}

int ore_audio_rate(void) {
	return ore::AudioMixer::kOutputRate;
}

void ore_audio_mix(OreEngine *e, float *out, int frames) {
	e->engine->mixer.mix(out, frames);
}

int ore_play_app_sound(OreEngine *e, const int16_t *samples, int frames, int channels, int rate) {
	if (frames <= 0) {
		e->app_sound->stop();
		return 0;
	}
	bool playing = e->app_sound->play(samples, size_t(frames), channels, rate);
	log_info("app sound: %d frames, %d channel(s), %d Hz%s", frames, channels, rate, playing ? "" : ", not a sound");
	return playing ? 1 : 0;
}

void ore_set_ui_callbacks(OreEngine *e, void *ctx, OreDialogCallback dialog, OreTextInputCallback text_input) {
	e->platform->set_dialog_handler([ctx, dialog](const std::string &text, int kind) {
		if (dialog) dialog(ctx, text.c_str(), kind);
	});
	e->platform->set_text_input_handler([ctx, text_input](const std::string &initial) {
		if (text_input) text_input(ctx, initial.c_str());
	});
}

void ore_answer_dialog(OreEngine *e, int result) {
	e->platform->answer_dialog(result);
}

void ore_answer_text_input(OreEngine *e, int ok, const char *utf8) {
	e->platform->answer_text_input(ok != 0, utf8 ? utf8 : "");
}

void ore_set_paused(OreEngine *e, int paused) {
	e->engine->set_paused(paused != 0);
}

// Mining in the game (api/ore_kit.h): the functions that take the engine.
void ore_set_kit(OreEngine *e, OreKit *kit) {
#if ORE_MINING_KIT
	e->engine->set_kit(kit ? kit->kit.get() : nullptr);
#else
	(void)e, (void)kit;
#endif
}

int ore_text_tap(OreEngine *e, int x, int y, char **json) {
	*json = nullptr;
#if ORE_MINING_KIT
	if (!e->engine->kit_tap) return 0;
	std::string text;
	int what = e->engine->kit_tap->tap(x, y, text);
	if (what == 1) {
		*json = static_cast<char *>(malloc(text.size() + 1));
		if (!*json) return 2;
		memcpy(*json, text.c_str(), text.size() + 1);
	}
	return what;
#else
	(void)e, (void)x, (void)y;
	return 0;
#endif
}

void ore_set_reading_hold(OreEngine *e, int hold) {
	e->engine->set_reading_hold(hold != 0);
}

int ore_mining_check(const char *data_dir, char *text, int text_len) {
	ore::MiningSettings settings;
	std::string message;
	int result = ore::load_mining_settings(data_dir ? data_dir : "", settings, message);
	if (result > 0) result = ore::mining_check(settings, host_http_sender(), message) ? 1 : -1;
	if (text && text_len > 0) {
		strncpy(text, message.c_str(), size_t(text_len) - 1);
		text[text_len - 1] = 0;
	}
	return result;
}

void ore_app_opened_with(const char *link) {
	if (link && *link) ore::AppReturns::shared().opened_with(link);
}

#if DEBUG
int ore_debug_start_movie(OreEngine *e, int index) {
	if (index < 0 || index > 1) return 0;
	e->engine->vm.mem().wr32(e->engine->vm.global_addr(912), uint32_t(index));
	if (!e->engine->vm.start("MOVIEPLAY", &e->error)) return 0;
	log_info("movie diagnostic: index %d, isolated saves", index);
	return 1;
}
#endif

}  // extern "C"
