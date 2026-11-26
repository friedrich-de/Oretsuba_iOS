#include "audio/app_sound.h"

namespace ore {

std::shared_ptr<PcmBuffer> pcm_of(const int16_t *samples, size_t frames, int channels, int rate) {
	if (!samples || frames == 0 || (channels != 1 && channels != 2) || rate < 8000 || rate > 192000) return nullptr;
	auto pcm = std::make_shared<PcmBuffer>();
	pcm->rate = rate;
	if (channels == 2) {
		pcm->samples.assign(samples, samples + frames * 2);
	} else {
		pcm->samples.resize(frames * 2);
		for (size_t i = 0; i < frames; ++i) pcm->samples[i * 2] = pcm->samples[i * 2 + 1] = samples[i];
	}
	return pcm;
}

bool AppSound::play(const int16_t *samples, size_t frames, int channels, int rate) {
	std::shared_ptr<PcmBuffer> pcm = pcm_of(samples, frames, channels, rate);
	std::lock_guard<std::mutex> lock(mu_);
	if (voice_) mixer_.stop(voice_);
	voice_ = pcm ? mixer_.play_pcm(pcm, AudioMixer::PlayParams{}) : 0;
	return voice_ != 0;
}

void AppSound::stop() {
	std::lock_guard<std::mutex> lock(mu_);
	if (voice_) mixer_.stop(voice_);
	voice_ = 0;
}

bool AppSound::playing() {
	std::lock_guard<std::mutex> lock(mu_);
	return voice_ != 0 && mixer_.playing(voice_);
}

}  // namespace ore
