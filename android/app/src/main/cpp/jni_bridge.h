// The way from native code into the app's Kotlin: the calls any native thread
// may make (letters, HTTP) and the helpers the JNI functions share.
#pragma once

#include <jni.h>

#include <string>

#include "mining/http_fetch.h"
#include "mining/mining.h"
#include "platform/platform.h"

namespace ore {

// The app's Kotlin classes, by their JNI names.
inline constexpr char kNativeClass[] = "io/github/friedrich_de/oretsuba/Native";
inline constexpr char kHostClass[] = "io/github/friedrich_de/oretsuba/EngineHost";

// Once, from JNI_OnLoad: remembers the Java machine and the Native class.
bool jni_bridge_init(JavaVM *vm, JNIEnv *env);

// The calling thread's JNI environment. A thread Java does not know yet (the
// mining queue's, the audio restart's) is attached and detached when it ends.
JNIEnv *jni_env();

// Texts cross as UTF-8 bytes: JNI's own strings are not UTF-8 for every character.
jbyteArray jni_bytes(JNIEnv *env, const std::string &text);
std::string jni_string(JNIEnv *env, jbyteArray bytes);
// True when a Java exception was pending; it is logged and cleared.
bool jni_failed(JNIEnv *env, const char *what);

// Native.renderGlyph: one letter drawn by android.graphics.
bool jni_render_glyph(uint32_t codepoint, bool serif, bool bold, int size, GlyphBitmap &out);
// Native.httpSend: one request through HttpURLConnection (the HttpSend of mining/mining.h).
int jni_http_send(const HttpRequest &request, std::string &answer);
// Native.ankiDroid: one request to AnkiDroid on this device, answered in JSON
// (AnkiDroid.kt, the AnkiAppHost::ankidroid of mining/anki_app.h).
std::string jni_ankidroid(const std::string &method, const std::string &json);
// Native.kitFetch: one request of the mining kit (the HttpFetch of mining/http_fetch.h), with the
// whole answer, its type and where it came from, into a file when the request names one.
void jni_kit_fetch(const FetchRequest &request, FetchAnswer &answer);
// What Native.fetchStep is given while such a request runs: tells the request's progress on, and
// answers whether the request is to go on.
bool jni_fetch_step(jlong handle, jlong taken, jlong expected);
// Native.encodeVoice: 16-bit stereo samples as AAC in an MPEG-4 file. Empty when the device cannot.
std::string jni_encode_voice(const std::string &samples, int rate);
// Native.kitEvent: what the mining kit tells without having been asked, from whatever thread it is on.
void jni_kit_event(const char *name, const char *payload_json);

}  // namespace ore
