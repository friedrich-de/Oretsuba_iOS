// Text & message system (docs/re/40_text.md): message manager (current
// record, backlog, choices, read flags), the ADV windows (types 5, 0x6f, 6)
// and the text natives.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ore {

class Engine;
class MessageManager;
class ByteReader;
class ByteWriter;

class TextSystem {
public:
	explicit TextSystem(Engine &e);
	~TextSystem();

	Engine &engine;

	// Save support (save.cpp)
	void save_backlog(ByteWriter &w);
	bool load_backlog(ByteReader &r, bool keep);  // keep false: read past it
	void load_backlog_earlier(const std::vector<uint8_t> &data);  // as this port wrote it at first
	void clear_message_state();  // FUN_00432330
	// "【name】\n" + plain message text, cut to max bytes on a character boundary.
	std::string preview_text(size_t max);
	// Wraps game text at `cols` bytes per line, at most `lines` lines.
	std::string reflow(const std::string &text, int cols, int lines);

	MessageManager &msg();
	uint32_t active_select = 0;  // DAT_0050325c (N_30b)
	// "options changed" flag of N_c (app+0x1ac9): the option block differs
	// from the snapshot taken at the last clear.
	bool options_dirty();
	void clear_options_dirty();

	struct Impl;
	std::unique_ptr<Impl> impl;
};

}  // namespace ore
