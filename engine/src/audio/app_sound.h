// A sound of the app's own over the game's: the word's sound the dictionary
// popup plays (docs/MINING.md). It goes through the game's own mixer and audio
// output, so the app needs no player of its own. On an iPhone a sound played by
// the popup's web view took the app's audio session from the game, and the
// game stayed silent after it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "audio/mixer.h"

namespace ore {

// 16-bit samples, `channels` (1 or 2) of them a frame, interleaved, at `rate`
// Hz, as the mixer's stereo buffer (one channel is heard on both sides). Null
// when they are not a sound: no frames, another number of channels, a rate
// outside 8,000 to 192,000 Hz.
std::shared_ptr<PcmBuffer> pcm_of(const int16_t *samples, size_t frames, int channels, int rate);

class AppSound {
public:
	explicit AppSound(AudioMixer &mixer) : mixer_(mixer) {}

	// Plays a sound (as pcm_of takes it) in place of the one before, at full
	// volume. False when it is not a sound; the one before has stopped then too.
	// Any thread.
	bool play(const int16_t *samples, size_t frames, int channels, int rate);
	void stop();
	bool playing();

private:
	AudioMixer &mixer_;
	std::mutex mu_;
	int voice_ = 0;
};

}  // namespace ore
