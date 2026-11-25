#include "jni_bridge.h"

#include <pthread.h>

#include <cstring>

#include "util/log.h"

namespace ore {
namespace {

JavaVM *g_vm = nullptr;
jclass g_native = nullptr;  // global reference: found once, valid on every thread
jmethodID g_render_glyph = nullptr;
jmethodID g_http_send = nullptr;
pthread_key_t g_detach_key;

void detach_thread(void *) {
	if (g_vm) g_vm->DetachCurrentThread();
}

uint32_t read_le32(const uint8_t *p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

}  // namespace

bool jni_bridge_init(JavaVM *vm, JNIEnv *env) {
	g_vm = vm;
	pthread_key_create(&g_detach_key, detach_thread);
	jclass local = env->FindClass(kNativeClass);
	if (!local) return false;
	g_native = static_cast<jclass>(env->NewGlobalRef(local));
	env->DeleteLocalRef(local);
	g_render_glyph = env->GetStaticMethodID(g_native, "renderGlyph", "(IZZI)[B");
	g_http_send = env->GetStaticMethodID(g_native, "httpSend", "([B[B[[B[BI)[B");
	return g_render_glyph && g_http_send;
}

JNIEnv *jni_env() {
	if (!g_vm) return nullptr;
	JNIEnv *env = nullptr;
	if (g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
	if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
	pthread_setspecific(g_detach_key, g_vm);  // any value: makes detach_thread run at the thread's end
	return env;
}

jbyteArray jni_bytes(JNIEnv *env, const std::string &text) {
	jbyteArray array = env->NewByteArray(jsize(text.size()));
	if (array && !text.empty())
		env->SetByteArrayRegion(array, 0, jsize(text.size()), reinterpret_cast<const jbyte *>(text.data()));
	return array;
}

std::string jni_string(JNIEnv *env, jbyteArray bytes) {
	if (!bytes) return std::string();
	jsize n = env->GetArrayLength(bytes);
	std::string out(size_t(n), '\0');
	if (n > 0) env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte *>(&out[0]));
	return out;
}

bool jni_failed(JNIEnv *env, const char *what) {
	if (!env->ExceptionCheck()) return false;
	log_error("%s: a Java exception", what);
	env->ExceptionDescribe();
	env->ExceptionClear();
	return true;
}

// The answer is five little-endian 32-bit numbers (width, height, left, top,
// advance) and then width * height bytes of coverage.
bool jni_render_glyph(uint32_t codepoint, bool serif, bool bold, int size, GlyphBitmap &out) {
	JNIEnv *env = jni_env();
	if (!env || env->PushLocalFrame(4) != 0) return false;
	bool ok = false;
	auto answer = static_cast<jbyteArray>(env->CallStaticObjectMethod(
	    g_native, g_render_glyph, jint(codepoint), jboolean(serif), jboolean(bold), jint(size)));
	if (!jni_failed(env, "renderGlyph") && answer) {
		jsize n = env->GetArrayLength(answer);
		if (n >= 20) {
			std::string bytes = jni_string(env, answer);
			const auto *p = reinterpret_cast<const uint8_t *>(bytes.data());
			GlyphBitmap g;
			g.width = int(read_le32(p));
			g.height = int(read_le32(p + 4));
			g.left = int32_t(read_le32(p + 8));
			g.top = int32_t(read_le32(p + 12));
			g.advance = int(read_le32(p + 16));
			if (g.width >= 0 && g.height >= 0 && size_t(g.width) * size_t(g.height) == size_t(n) - 20) {
				g.alpha.assign(p + 20, p + n);
				out = std::move(g);
				ok = true;
			}
		}
	}
	env->PopLocalFrame(nullptr);
	return ok;
}

// The answer is the status as a little-endian 32-bit number and then the
// answer's body, or, for status 0, the reason no answer came.
int jni_http_send(const HttpRequest &request, std::string &answer) {
	answer.clear();
	JNIEnv *env = jni_env();
	if (!env || env->PushLocalFrame(16 + jint(request.headers.size()) * 2) != 0) {
		answer = "The app cannot reach its network code.";
		return 0;
	}
	int status = 0;
	jclass bytes_class = env->FindClass("[B");
	jobjectArray headers = env->NewObjectArray(jsize(request.headers.size() * 2), bytes_class, nullptr);
	for (size_t i = 0; headers && i < request.headers.size(); ++i) {
		jbyteArray name = jni_bytes(env, request.headers[i].first);
		jbyteArray value = jni_bytes(env, request.headers[i].second);
		env->SetObjectArrayElement(headers, jsize(i * 2), name);
		env->SetObjectArrayElement(headers, jsize(i * 2 + 1), value);
		env->DeleteLocalRef(name);
		env->DeleteLocalRef(value);
	}
	jbyteArray body = request.body.empty() ? nullptr : jni_bytes(env, request.body);
	auto result = static_cast<jbyteArray>(
	    env->CallStaticObjectMethod(g_native, g_http_send, jni_bytes(env, request.method), jni_bytes(env, request.url),
	                                headers, body, jint(request.timeout_ms)));
	if (jni_failed(env, "httpSend") || !result) {
		answer = "The request failed inside the app.";
	} else {
		std::string bytes = jni_string(env, result);
		if (bytes.size() >= 4) {
			status = int(int32_t(read_le32(reinterpret_cast<const uint8_t *>(bytes.data()))));
			answer.assign(bytes, 4, std::string::npos);
		}
	}
	env->PopLocalFrame(nullptr);
	return status;
}

}  // namespace ore
