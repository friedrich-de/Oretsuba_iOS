// The native side of the Kotlin class Native: the engine's C interface
// (api/ore_api.h) for the app, plus the two things the app leaves to native
// code, putting the picture on the screen and playing the sound.
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <cstring>
#include <mutex>
#include <string>

#include "android_platform.h"
#include "api/ore_api.h"
#include "audio_out.h"
#include "jni_bridge.h"
#include "util/log.h"

namespace {

using ore::jni_bytes;
using ore::jni_string;

jmethodID g_on_dialog = nullptr;
jmethodID g_on_text_input = nullptr;

// One game: the engine, the surface its picture goes to, its sound, and the
// Kotlin object that shows its message boxes.
struct Session {
	OreEngine *engine = nullptr;
	jobject host = nullptr;  // global reference to the EngineHost
	ore::AudioOut audio;
	// The surface comes and goes on the main thread while the engine's thread draws.
	std::mutex window_mu;
	ANativeWindow *window = nullptr;
	bool window_fresh = false;  // nothing was drawn into this surface yet
	uint64_t shown = 0;         // ore_frame_serial of the picture on the screen
};

Session *session_of(jlong handle) {
	return reinterpret_cast<Session *>(handle);
}

void log_to_logcat(ore::LogLevel level, const char *message) {
	static const int priority[] = {ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR};
	__android_log_write(priority[int(level)], "Oretsuba", message);
}

// The game asks for a message box or a line of text (on the engine's thread).
void on_dialog(void *ctx, const char *utf8_text, int kind) {
	auto *s = static_cast<Session *>(ctx);
	JNIEnv *env = ore::jni_env();
	if (!env || !s->host) return;
	jbyteArray text = jni_bytes(env, utf8_text ? utf8_text : "");
	env->CallVoidMethod(s->host, g_on_dialog, text, jint(kind));
	ore::jni_failed(env, "onDialog");
	env->DeleteLocalRef(text);
}

void on_text_input(void *ctx, const char *utf8_initial) {
	auto *s = static_cast<Session *>(ctx);
	JNIEnv *env = ore::jni_env();
	if (!env || !s->host) return;
	jbyteArray text = jni_bytes(env, utf8_initial ? utf8_initial : "");
	env->CallVoidMethod(s->host, g_on_text_input, text);
	ore::jni_failed(env, "onTextInput");
	env->DeleteLocalRef(text);
}

// ---------------------------------------------------------------- the game's life

jlong new_session(JNIEnv *env, jclass, jobject host) {
	auto *s = new Session();
	s->host = env->NewGlobalRef(host);
	return reinterpret_cast<jlong>(s);
}

// Starts the game. Null when it runs, otherwise the reason it does not.
jbyteArray boot(JNIEnv *env, jclass, jlong handle, jbyteArray data_dir, jbyteArray save_dir, jint autosave_lines,
                jboolean resume) {
	Session *s = session_of(handle);
	std::string data = jni_string(env, data_dir), save = jni_string(env, save_dir);
	OreConfig config{};
	config.data_dir = data.c_str();
	config.save_dir = save.c_str();
	config.autosave_lines = autosave_lines;
	config.resume = resume ? 1 : 0;
	char error[512] = "";
	OreEngine *engine = ore_create(&config, error, int(sizeof error));
	if (!engine) return jni_bytes(env, error[0] ? error : "The game could not be started.");
	ore_set_ui_callbacks(engine, s, on_dialog, on_text_input);
	{
		std::lock_guard<std::mutex> lock(s->window_mu);
		s->engine = engine;
		s->window_fresh = true;
	}
	s->audio.start(engine);
	return nullptr;
}

void destroy(JNIEnv *env, jclass, jlong handle) {
	Session *s = session_of(handle);
	s->audio.stop();
	OreEngine *engine;
	{
		std::lock_guard<std::mutex> lock(s->window_mu);
		engine = s->engine;
		s->engine = nullptr;
		if (s->window) ANativeWindow_release(s->window);
		s->window = nullptr;
	}
	if (engine) ore_destroy(engine);
	if (s->host) env->DeleteGlobalRef(s->host);
	delete s;
}

jint frame(JNIEnv *, jclass, jlong handle) {
	Session *s = session_of(handle);
	return s->engine ? ore_frame(s->engine, ore::android_now_ms()) : 0;
}

jbyteArray last_error(JNIEnv *env, jclass, jlong handle) {
	Session *s = session_of(handle);
	return jni_bytes(env, s->engine ? ore_last_error(s->engine) : "");
}

void set_paused(JNIEnv *, jclass, jlong handle, jboolean paused) {
	Session *s = session_of(handle);
	if (!s->engine) return;
	ore_set_paused(s->engine, paused ? 1 : 0);
	s->audio.set_paused(paused);
}

// ---------------------------------------------------------------- the picture

// The surface of the game's view, or null when it is gone (main thread).
void set_surface(JNIEnv *env, jclass, jlong handle, jobject surface) {
	Session *s = session_of(handle);
	std::lock_guard<std::mutex> lock(s->window_mu);
	if (s->window) ANativeWindow_release(s->window);
	s->window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
	s->window_fresh = true;
	if (s->window) {
		// The buffer keeps the game's own size; the system scales it to the view.
		int width = 800, height = 600;
		if (s->engine) ore_framebuffer(s->engine, &width, &height);
		ANativeWindow_setBuffersGeometry(s->window, width, height, WINDOW_FORMAT_RGBX_8888);
	}
}

// Draws the engine's picture when it changed since the last call. The engine
// holds it as B, G, R, A bytes; the surface wants R, G, B, X.
jboolean present(JNIEnv *, jclass, jlong handle) {
	Session *s = session_of(handle);
	std::lock_guard<std::mutex> lock(s->window_mu);
	if (!s->window || !s->engine) return JNI_FALSE;
	uint64_t serial = ore_frame_serial(s->engine);
	if (!s->window_fresh && serial == s->shown) return JNI_FALSE;
	int width = 0, height = 0;
	const uint8_t *pixels = ore_framebuffer(s->engine, &width, &height);
	if (!pixels || width <= 0 || height <= 0) return JNI_FALSE;
	ANativeWindow_Buffer buffer;
	if (ANativeWindow_lock(s->window, &buffer, nullptr) != 0) return JNI_FALSE;
	int rows = height < buffer.height ? height : buffer.height;
	int columns = width < buffer.width ? width : buffer.width;
	for (int y = 0; y < rows; ++y) {
		uint32_t *to = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(buffer.bits) + size_t(y) * buffer.stride * 4);
		const uint8_t *row = pixels + size_t(y) * width * 4;
		for (int x = 0; x < columns; ++x) {
			uint32_t bgra;
			memcpy(&bgra, row + size_t(x) * 4, 4);
			to[x] = 0xFF000000u | (bgra & 0x0000FF00u) | (bgra >> 16 & 0xFFu) | (bgra & 0xFFu) << 16;
		}
	}
	ANativeWindow_unlockAndPost(s->window);
	s->window_fresh = false;
	s->shown = serial;
	return JNI_TRUE;
}

// ---------------------------------------------------------------- input and answers

void mouse_move(JNIEnv *, jclass, jlong handle, jint x, jint y) {
	if (Session *s = session_of(handle); s->engine) ore_mouse_move(s->engine, x, y);
}

void mouse_button(JNIEnv *, jclass, jlong handle, jint button, jboolean down) {
	if (Session *s = session_of(handle); s->engine) ore_mouse_button(s->engine, button, down ? 1 : 0);
}

void mouse_wheel(JNIEnv *, jclass, jlong handle, jint delta) {
	if (Session *s = session_of(handle); s->engine) ore_mouse_wheel(s->engine, delta);
}

void key(JNIEnv *, jclass, jlong handle, jint vk, jboolean down) {
	if (Session *s = session_of(handle); s->engine) ore_key(s->engine, vk, down ? 1 : 0);
}

void game_command(JNIEnv *, jclass, jlong handle, jint command) {
	if (Session *s = session_of(handle); s->engine) ore_game_command(s->engine, command);
}

jboolean movie_playing(JNIEnv *, jclass, jlong handle) {
	Session *s = session_of(handle);
	return s->engine && ore_movie_playing(s->engine) ? JNI_TRUE : JNI_FALSE;
}

void answer_dialog(JNIEnv *, jclass, jlong handle, jint result) {
	if (Session *s = session_of(handle); s->engine) ore_answer_dialog(s->engine, result);
}

void answer_text_input(JNIEnv *env, jclass, jlong handle, jboolean ok, jbyteArray text) {
	Session *s = session_of(handle);
	if (!s->engine) return;
	std::string utf8 = jni_string(env, text);
	ore_answer_text_input(s->engine, ok ? 1 : 0, utf8.c_str());
}

// ---------------------------------------------------------------- mining

// Asks the server of the game folder's mining.txt whether it takes the key
// (ore_mining_check). The state goes to state[0], the sentence is returned.
jbyteArray mining_check(JNIEnv *env, jclass, jbyteArray data_dir, jintArray state) {
	std::string data = jni_string(env, data_dir);
	char text[512] = "";
	jint result = ore_mining_check(data.c_str(), text, int(sizeof text));
	env->SetIntArrayRegion(state, 0, 1, &result);
	return jni_bytes(env, text);
}

const JNINativeMethod kMethods[] = {
    {"newSession", "(Lio/github/friedrich_de/oretsuba/EngineHost;)J", reinterpret_cast<void *>(new_session)},
    {"boot", "(J[B[BIZ)[B", reinterpret_cast<void *>(boot)},
    {"destroy", "(J)V", reinterpret_cast<void *>(destroy)},
    {"frame", "(J)I", reinterpret_cast<void *>(frame)},
    {"lastError", "(J)[B", reinterpret_cast<void *>(last_error)},
    {"setPaused", "(JZ)V", reinterpret_cast<void *>(set_paused)},
    {"setSurface", "(JLandroid/view/Surface;)V", reinterpret_cast<void *>(set_surface)},
    {"present", "(J)Z", reinterpret_cast<void *>(present)},
    {"mouseMove", "(JII)V", reinterpret_cast<void *>(mouse_move)},
    {"mouseButton", "(JIZ)V", reinterpret_cast<void *>(mouse_button)},
    {"mouseWheel", "(JI)V", reinterpret_cast<void *>(mouse_wheel)},
    {"key", "(JIZ)V", reinterpret_cast<void *>(key)},
    {"gameCommand", "(JI)V", reinterpret_cast<void *>(game_command)},
    {"moviePlaying", "(J)Z", reinterpret_cast<void *>(movie_playing)},
    {"answerDialog", "(JI)V", reinterpret_cast<void *>(answer_dialog)},
    {"answerTextInput", "(JZ[B)V", reinterpret_cast<void *>(answer_text_input)},
    {"miningCheck", "([B[I)[B", reinterpret_cast<void *>(mining_check)},
};

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
	JNIEnv *env = nullptr;
	if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
	ore::set_log_sink(log_to_logcat);
	if (!ore::jni_bridge_init(vm, env)) return JNI_ERR;
	jclass native = env->FindClass(ore::kNativeClass);
	jclass host = env->FindClass(ore::kHostClass);
	if (!native || !host) return JNI_ERR;
	g_on_dialog = env->GetMethodID(host, "onDialog", "([BI)V");
	g_on_text_input = env->GetMethodID(host, "onTextInput", "([B)V");
	if (!g_on_dialog || !g_on_text_input) return JNI_ERR;
	if (env->RegisterNatives(native, kMethods, jint(sizeof kMethods / sizeof kMethods[0])) != JNI_OK) return JNI_ERR;
	return JNI_VERSION_1_6;
}
