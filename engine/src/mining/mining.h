// Mining: the lines the game shows go to a VN Mining Server, each with who
// speaks, its voice and a screenshot (docs/MINING.md). This file is the part
// that knows nothing of the engine: the settings file, the text of a record,
// which lines count as read, and the queue that sends them.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "util/text.h"

namespace ore {

// ---------------------------------------------------------------- mining.txt

struct MiningSettings {
	std::string server;      // "https://vn.example.org", without a slash at the end
	std::string key;         // the server's API key
	std::string game;        // the title the server puts on cards; empty = the engine's own
	bool voice = true;       // send the voice of a line
	bool screenshot = true;  // send the screen of a line
};

inline constexpr char kMiningFileName[] = "mining.txt";

// Reads the text of a mining.txt: one "name = value" per line, '#' starts a
// comment. False with the reason in `problem` when the file cannot be used.
bool parse_mining_settings(const std::string &text, MiningSettings &out, std::string &problem);

// The mining.txt of a game: in the game's folder or in one of the two folders
// above it, in any letter case. Empty when there is none.
std::string find_mining_file(const std::string &game_dir);

// find + read + parse. 0 = no file (problem stays empty), 1 = usable, -1 = unusable.
int load_mining_settings(const std::string &game_dir, MiningSettings &out, std::string &problem);

// ---------------------------------------------------------------- the text of a line

// The whole text of a record's body as the reader sees it, still in the game's
// encoding: tags, waits, page and line breaks dropped.
std::string mining_plain_text(const std::string &body, TextEncoding enc);

// UTF-8 text without the blanks (also full-width ones) at both ends.
std::string mining_trim(const std::string &utf8);

// ---------------------------------------------------------------- which lines are sent

// Picks the lines the reader got to see: a line counts once its text stands
// complete, a moment after it began. That is when it waits for the click, or,
// for the question before a choice, when it has ended without waiting. Lines
// that are clicked or skipped through faster are left out.
class LineGate {
public:
	static constexpr int64_t kDwellMs = 300;

	// One record of text, as the message manager holds it.
	struct Record {
		int type = 0, line = 0;
		const std::string *name = nullptr, *body = nullptr;
	};

	// The message window began to show `record` (native 0x2fa).
	void shown(const Record &record, int64_t now);
	// Once per frame while a window is known. `state` is MessageWindow::state(),
	// `current` the record the message manager holds now. True = send it now.
	bool frame(int state, const Record &current, int64_t now);
	// The player leaves the app: the line on screen is the one they want.
	bool leaving(int state, const Record &current);
	// The window is gone.
	void reset() { armed_ = false; }
	// The game starts at the line it was left at (docs/AUTOSAVE.md): the next line
	// shown is that one, and the reader had it before.
	void next_is_known() { next_known_ = true; }
	bool armed() const { return armed_; }

private:
	static std::string key_of(const Record &record);
	// Whether the line the gate holds may still be sent in this state.
	bool alive(int state, const Record &current);
	bool armed_ = false;
	bool waited_ = false;  // the line was seen waiting for the click
	bool next_known_ = false;
	int64_t at_ = 0;
	std::string key_, sent_;
};

// ---------------------------------------------------------------- sending

struct HttpRequest {
	std::string method = "POST";
	std::string url;
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
	int timeout_ms = 15000;
};

// Sends one request and waits for the answer. Returns the HTTP status and the
// answer's body in `answer`, or 0 or less when no answer came, then with the
// reason in `answer`. Called from the queue's own thread; it must not depend
// on anything that can go away while the app runs.
using HttpSend = std::function<int(const HttpRequest &request, std::string &answer)>;

// One line on its way to the server. Texts are UTF-8.
struct MiningLine {
	std::string text, name, game;
	std::string voice;                // the voice file's name in the game, without ending
	std::string audio_name;           // "TAK1004.ogg"
	std::vector<uint8_t> audio;       // the voice file as it is
	std::vector<uint32_t> pixels;     // the screen, one 0x00RRGGBB per pixel, rows top down
	int width = 0, height = 0;
	std::vector<uint8_t> picture;     // the screen as JPEG, made from `pixels` before sending
};

// JPEG of a screen held as 0x00RRGGBB pixels. Empty when it cannot be made.
std::vector<uint8_t> mining_jpeg(const uint32_t *pixels, int width, int height, int quality);

// The request that hands `line` to the server (POST Api/Lines, multipart).
HttpRequest mining_line_request(const MiningSettings &settings, const MiningLine &line, const std::string &boundary);

// Asks the server whether it takes the key (GET Api/Status). `message` gets a
// sentence for the player either way. True when lines will arrive.
bool mining_check(const MiningSettings &settings, const HttpSend &send, std::string &message);

// Sends lines in the order they came, on a thread of its own, so the game
// never waits for the network. Lines the network cannot take at once are kept
// and tried again; when too many wait, the oldest go.
class MiningQueue {
public:
	struct Options {
		int max_pending = 64;       // lines kept while the server cannot be reached
		int max_raw = 6;            // of those, how many may still hold their raw screen
		int retry_ms = 1000;        // wait after the first failure; doubles up to retry_max_ms
		int retry_max_ms = 30000;
		int attempts = 5;           // a line the server answers with an error this often is dropped
		int jpeg_quality = 85;
		int leave_ms = 1000;        // how long the destructor waits for lines on their way
	};
	struct Stats {
		int sent = 0, dropped = 0, pending = 0;
		int last_status = 0;        // of the last request; 0 or less = no answer
		bool refused = false;       // the server did not take the key: nothing more is sent
	};

	MiningQueue(MiningSettings settings, HttpSend send);
	MiningQueue(MiningSettings settings, HttpSend send, Options options);
	~MiningQueue();
	MiningQueue(const MiningQueue &) = delete;
	MiningQueue &operator=(const MiningQueue &) = delete;

	void push(MiningLine line);
	// Waits until no line is left to send, at most `timeout_ms`. True when none is left.
	bool flush(int timeout_ms);
	Stats stats() const;

private:
	struct State;
	static void run(std::shared_ptr<State> state);
	std::shared_ptr<State> state_;
};

}  // namespace ore
