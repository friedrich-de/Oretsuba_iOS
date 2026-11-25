// Mining (docs/MINING.md): the part that listens to the engine. It hears of
// every line the message window begins, waits until the reader has it in
// front of them, and hands it to the queue with its voice and the screen.
#pragma once

#include <memory>
#include <string>

#include "mining/mining.h"

namespace ore {

class Engine;
class MessageWindow;

class MiningTap {
public:
	// The tap of a game whose folder holds a usable mining.txt; null otherwise.
	static std::unique_ptr<MiningTap> open(Engine &engine);

	void line_shown(MessageWindow &window);   // native 0x2fa began a line
	void window_gone(MessageWindow &window);
	void frame_drawn();                       // once per frame, when the screen is drawn
	void leaving();                           // the app goes to the background
	MiningQueue &queue() { return queue_; }

private:
	MiningTap(Engine &engine, MiningSettings settings, HttpSend send);
	LineGate::Record record() const;
	void send();

	Engine &engine_;
	MiningSettings settings_;
	MiningQueue queue_;
	LineGate gate_;
	MessageWindow *window_ = nullptr;
};

}  // namespace ore
