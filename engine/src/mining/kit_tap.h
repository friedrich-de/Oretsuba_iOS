// Mining in the game (docs/MINING.md): the part of the mining kit that looks
// at the engine. A tap on the text that stands in the message window is not
// a click for the game: it names a character of the line, and the popup looks
// up the word that starts there.
#pragma once

#include <cstdint>
#include <string>

#include "dict/kit_config.h"

namespace ore {

class Engine;
class MessageWindow;
class MiningKit;

#if ORE_MINING_KIT

class KitTap {
public:
	KitTap(Engine &engine, MiningKit &kit) : engine_(engine), kit_(kit) {}

	// What a tap at a place of the screen is, while the kit mines in the game.
	// 0: none of the text's business, the game gets it as a click.
	// 1: on a character of the line that stands in the message window. The
	//    line is handed to the kit with its voice and the screen, and `json`
	//    gets what the popup's page needs: {"line", "text", "index", "rects",
	//    "sentence", "speaker", "box", "screen"}. `text` is the page of text
	//    as it stands, `index` the tapped character in it, `rects` the cell
	//    [x, y, w, h] of every character on the screen.
	// 2: in the text box, but on no character: nothing happens.
	int tap(int x, int y, std::string &json);
	// The same JSON for the line that stands, without a tap ("index" is -1 and
	// "line" 0: nothing is handed to the kit). False when no line stands. For
	// tests and for a host that wants to know what can be tapped.
	bool standing_text(std::string &json);

	void line_shown(MessageWindow &window);  // native 0x2fa began a line
	void window_gone(MessageWindow &window);

private:
	// The window whose line stands complete in front of the reader.
	MessageWindow *standing() const;
	int64_t capture(MessageWindow &window);
	std::string describe(MessageWindow &window, int64_t line, int index) const;

	Engine &engine_;
	MiningKit &kit_;
	MessageWindow *window_ = nullptr;
	std::string shown_in_;  // the script that showed the line
	std::string captured_;  // the line the kit holds
	int64_t captured_id_ = 0;
};

#else

class KitTap {};

#endif  // ORE_MINING_KIT

}  // namespace ore
