// The game's sound: an AAudio stream that asks the engine's mixer for
// samples on the system's audio thread.
#pragma once

#include <memory>

#include "api/ore_api.h"

namespace ore {

class AudioOut {
public:
	AudioOut();
	~AudioOut();

	// Opens the stream and starts it. False when the device gives no sound;
	// the game then runs silent.
	bool start(OreEngine *engine);
	// The app left the screen or came back.
	void set_paused(bool paused);
	// No sample is asked for after this returns.
	void stop();

private:
	struct State;
	std::shared_ptr<State> state_;
};

}  // namespace ore
