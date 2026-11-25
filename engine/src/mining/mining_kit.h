// The mining kit: looking words up and making cards inside the game, without
// a server in between. It holds the player's choices, the dictionaries on the
// device, and the line a word was tapped in, and it answers the two things
// that ask it: the launcher's settings and the popup's page. Both speak to it
// in JSON by the name of a method, so that a host needs no more than a thin
// layer to carry those calls, whatever it is written in.
//
// A call may wait for the network: hosts call from a thread that may wait.
// Calls may come from several threads at once.
#pragma once

#include "dict/kit_config.h"
#if ORE_MINING_KIT

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "dict/library.h"
#include "mining/card_target.h"
#include "mining/http_fetch.h"
#include "util/json.h"

namespace ore {

// The line a word was tapped in, as it stood then.
struct KitLine {
	std::string sentence;      // UTF-8
	std::string speaker;
	std::string voice;         // the voice's file as the game has it; empty without one
	std::string voice_ending;  // "ogg"
	std::string picture;       // the screen as JPEG; empty without one
};

// Makes a voice's file one every Anki plays (the game's are Ogg, which Anki on
// an iPhone does not play). False to keep the file as it is.
using VoiceEncoder = std::function<bool(const std::string &file, const std::string &ending, std::string &out, std::string &type,
                                        std::string &out_ending)>;

// The title the cards name as their source.
inline constexpr char kKitSource[] = "\xE4\xBF\xBA\xE3\x81\x9F\xE3\x81\xA1\xE3\x81\xAB\xE7\xBF\xBC\xE3\x81\xAF\xE3\x81\xAA\xE3\x81\x84";  // 俺たちに翼はない

// The player's choices, kept in settings.json. The AnkiConnect's key and the
// audio source's address are not among them: an address like that often holds
// a key. The host keeps both where secrets belong and tells the kit at its
// start (secrets.set).
struct KitSettings {
	bool direct = false;             // a tap on the text looks the word up, instead of going on
	std::string target = "connect";  // where cards go (MiningKit::set_target)
	std::string address;             // of the AnkiConnect
	std::string deck, note_type;
	std::vector<std::pair<std::string, std::string>> fields;  // for each field of the note type, what goes in it
	std::vector<std::string> tags{"vn-miner"};
	int text_size = 100;  // of the popup, in percent
	bool hide_tags = false, compact = false;

	json::Value to_json() const;
	// Takes over what `patch` names and leaves the rest.
	void merge(const json::Value &patch);
};

// A method a host adds to the kit. False with the reason in `problem`.
using KitMethod = std::function<bool(const json::Value &params, json::Value &result, AnkiProblem &problem)>;

class MiningKit {
public:
	struct Options {
		std::string folder;     // where the kit keeps its settings and its dictionaries
		std::string resources;  // the folder with transforms.json
		HttpFetch fetch;        // may be empty: then nothing is reached
		VoiceEncoder encode_voice;
	};

	// Null with the reason in `problem` when the kit cannot start.
	static std::unique_ptr<MiningKit> open(Options options, std::string &problem);
	~MiningKit();
	MiningKit(const MiningKit &) = delete;
	MiningKit &operator=(const MiningKit &) = delete;

	// Does what `method` names with `params` (JSON, or empty for none). The
	// answer is {"ok":true,"result":…} or {"ok":false,"error":"a sentence for
	// the player","kind":"…"}. The methods are listed in docs/MINING.md.
	std::string call(const std::string &method, const std::string &params);
	// A file for the page: a dictionary's picture ("dict.media"), the line's
	// picture or voice ("line.picture", "line.voice"), how a word sounds
	// ("audio.word"). False when there is none.
	bool data(const std::string &method, const std::string &params, std::string &bytes, std::string &type);

	// Whether a tap on the text looks a word up.
	bool direct() const;
	// The line a word was tapped in. Returns the number it is kept under.
	int64_t set_line(KitLine line);

	// Another place cards can go, chosen by the setting anki.target.
	void set_target(const std::string &name, std::shared_ptr<CardTarget> target);
	// A method of the host's own: call() hands it what the kit does not know itself.
	void set_method(const std::string &name, KitMethod method);
	const std::string &folder() const { return options_.folder; }
	// Things the page is told without having asked (window.oreMining.event).
	void set_events(std::function<void(const std::string &name, const std::string &payload)> sink);
	void emit(const std::string &name, const json::Value &payload);

	dict::DictionaryLibrary &library() { return *library_; }

private:
	struct Failure {
		std::string message, kind;
	};
	MiningKit(Options options, std::shared_ptr<const dict::Deinflector> rules);
	json::Value dispatch(const std::string &method, const json::Value &params);

	KitSettings settings() const;
	void change(const std::function<void(KitSettings &)> &change);
	void save(const KitSettings &settings) const;
	// The place cards go by the settings, or by an address and key being tried out.
	std::shared_ptr<CardTarget> target(const KitSettings &settings, const std::string *address = nullptr, const std::string *key = nullptr) const;
	[[noreturn]] static void fail(const AnkiProblem &problem);
	std::shared_ptr<const KitLine> line(int64_t *id = nullptr) const;
	bool word_sound(const std::string &term, const std::string &reading, const std::string &source, std::string &bytes,
	                std::string &type, std::string &ending, bool &no_source) const;

	json::Value settings_view() const;
	json::Value anki_check(const json::Value &params);
	json::Value anki_fields(const json::Value &params);
	json::Value anki_choose(const json::Value &params);
	json::Value page_init();
	json::Value card_known(const json::Value &params);
	json::Value card_add(const json::Value &params);
	json::Value dict_list() const;

	Options options_;
	std::unique_ptr<dict::DictionaryLibrary> library_;
	mutable std::mutex gate_;
	KitSettings settings_;
	std::string anki_key_, audio_address_;
	std::shared_ptr<const KitLine> line_;
	int64_t line_id_ = 0;
	std::map<std::string, std::shared_ptr<CardTarget>> targets_;
	std::map<std::string, KitMethod> methods_;
	std::function<void(const std::string &, const std::string &)> events_;
};

}  // namespace ore

#endif  // ORE_MINING_KIT
