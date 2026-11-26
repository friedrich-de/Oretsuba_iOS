// What the mining kit needs of the system (mining/kit_host.h) on Android:
// HTTP through HttpURLConnection (KitHttp.kt), a line's voice from Ogg to AAC,
// which every Anki plays, through the system's encoder (VoiceEncoder.kt), and
// AnkiDroid on the same device (AnkiDroid.kt). All three are reached over JNI
// (jni_bridge).
#include "mining/kit_host.h"
#if ORE_MINING_KIT

#include <cstring>
#include <vector>

#include "audio/mixer.h"
#include "jni_bridge.h"

namespace ore {
namespace {

// The game's voices are Ogg Vorbis: decoded with the engine's own decoder and
// written as AAC in an MPEG-4 file by the system.
bool android_encode_voice(const std::string &file, const std::string &ending, std::string &out, std::string &type,
                          std::string &out_ending) {
	if (ending != "ogg") return false;
	std::shared_ptr<PcmBuffer> pcm = decode_ogg(std::vector<uint8_t>(file.begin(), file.end()));
	if (!pcm || pcm->frames() == 0) return false;
	// Android runs on little-endian machines only, so the samples are the bytes the encoder wants.
	std::string samples(reinterpret_cast<const char *>(pcm->samples.data()), pcm->samples.size() * sizeof(int16_t));
	std::string encoded = jni_encode_voice(samples, pcm->rate);
	if (encoded.empty()) return false;
	out = std::move(encoded);
	type = "audio/mp4";
	out_ending = "m4a";
	return true;
}

}  // namespace

HttpFetch host_http_fetcher() {
	return jni_kit_fetch;
}

VoiceEncoder host_voice_encoder() {
	return android_encode_voice;
}

AnkiAppHost host_anki_app() {
	AnkiAppHost host;
	host.ankidroid = jni_ankidroid;
	return host;
}

}  // namespace ore

#endif  // ORE_MINING_KIT
