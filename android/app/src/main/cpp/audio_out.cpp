#include "audio_out.h"

#include <aaudio/AAudio.h>

#include <cstring>
#include <mutex>
#include <thread>

#include "util/log.h"

namespace ore {

// Shared with the thread that reopens the stream after the output device
// changed (headphones pulled, a Bluetooth speaker gone), which may outlive
// the AudioOut object.
struct AudioOut::State : std::enable_shared_from_this<AudioOut::State> {
	std::mutex mu;
	AAudioStream *stream = nullptr;
	OreEngine *engine = nullptr;
	bool paused = false;
	bool closed = false;

	static aaudio_data_callback_result_t on_data(AAudioStream *, void *user, void *audio, int32_t frames) {
		auto *self = static_cast<State *>(user);
		ore_audio_mix(self->engine, static_cast<float *>(audio), frames);
		return AAUDIO_CALLBACK_RESULT_CONTINUE;
	}

	static void on_error(AAudioStream *stream, void *user, aaudio_result_t error) {
		auto *self = static_cast<State *>(user);
		log_warn("audio: the stream stopped (%s)", AAudio_convertResultToText(error));
		// A stream must not be closed from its own callback: do it from a thread.
		std::shared_ptr<State> keep = self->weak_from_this().lock();
		if (!keep) return;
		std::thread([keep, stream] {
			std::lock_guard<std::mutex> lock(keep->mu);
			if (keep->closed || keep->stream != stream) return;
			keep->close_locked();
			if (keep->open_locked() && !keep->paused) AAudioStream_requestStart(keep->stream);
		}).detach();
	}

	bool open_locked() {
		AAudioStreamBuilder *builder = nullptr;
		if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return false;
		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
		AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
		// The mixer's own format; the system converts to what the device plays.
		AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
		AAudioStreamBuilder_setChannelCount(builder, 2);
		AAudioStreamBuilder_setSampleRate(builder, ore_audio_rate());
		AAudioStreamBuilder_setDataCallback(builder, on_data, this);
		AAudioStreamBuilder_setErrorCallback(builder, on_error, this);
		aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream);
		AAudioStreamBuilder_delete(builder);
		if (result != AAUDIO_OK) {
			log_warn("audio: no output stream (%s)", AAudio_convertResultToText(result));
			stream = nullptr;
			return false;
		}
		return true;
	}

	void close_locked() {
		if (!stream) return;
		AAudioStream_requestStop(stream);
		AAudioStream_close(stream);
		stream = nullptr;
	}
};

AudioOut::AudioOut() : state_(std::make_shared<State>()) {}

AudioOut::~AudioOut() {
	stop();
}

bool AudioOut::start(OreEngine *engine) {
	std::lock_guard<std::mutex> lock(state_->mu);
	state_->engine = engine;
	state_->closed = false;
	if (!state_->open_locked()) return false;
	if (!state_->paused) AAudioStream_requestStart(state_->stream);
	return true;
}

void AudioOut::set_paused(bool paused) {
	std::lock_guard<std::mutex> lock(state_->mu);
	// Asking a stream that is already starting to start again is an error.
	if (state_->paused == paused) return;
	state_->paused = paused;
	if (!state_->stream) return;
	if (paused)
		AAudioStream_requestPause(state_->stream);
	else
		AAudioStream_requestStart(state_->stream);
}

void AudioOut::stop() {
	std::lock_guard<std::mutex> lock(state_->mu);
	state_->closed = true;
	state_->close_locked();
}

}  // namespace ore
