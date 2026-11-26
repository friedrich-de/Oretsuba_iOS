// The mining kit (docs/MINING.md): talking to an AnkiConnect, the Lapis
// defaults, how a word sounds, what goes into a card's fields, and the kit's
// calls as the launcher and the popup's page make them. Everything on the
// network is made up here: an AnkiConnect, an audio source and a place to
// download a dictionary from. Needs no game files and no Apple system:
//
//   c++ -std=c++17 -Isrc -Ithird_party -Itests tests/kit_test.cpp src/dict/*.cpp src/mining/anki_*.cpp
//       src/mining/card_note.cpp src/mining/word_audio.cpp src/mining/mining_kit.cpp src/util/json.cpp
//       src/util/log.cpp -lsqlite3 -lz -lpthread -o /tmp/kit_test && /tmp/kit_test
//
// It reads the rules (resources/mining/transforms.json) from the repository it
// is run in, or from the folder given as its first argument (the engine's).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "mining/anki_connect.h"
#include "mining/anki_fields.h"
#include "mining/card_note.h"
#include "mining/mining_kit.h"
#include "mining/word_audio.h"
#include "util/json.h"

#include "zip_writer.h"

using namespace ore;

namespace {

bool ok = true;
int checks = 0;

void check(bool value, const std::string &message) {
	++checks;
	if (!value) {
		fprintf(stderr, "FAIL: %s\n", message.c_str());
		ok = false;
	}
}

void check_eq(const std::string &got, const std::string &want, const std::string &message) {
	check(got == want, message + ": got \"" + got + "\", want \"" + want + "\"");
}

std::string read_file(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

json::Value parsed(const std::string &text) {
	json::Value value;
	json::parse(text, value);
	return value;
}

const std::vector<std::string> kLapisFields = {"Expression",    "ExpressionFurigana", "ExpressionReading", "ExpressionAudio", "SelectionText",
                                               "MainDefinition", "DefinitionPicture",  "Sentence",          "SentenceFurigana", "SentenceAudio",
                                               "Picture",        "Glossary",           "Hint",              "IsWordAndSentenceCard", "IsClickCard",
                                               "IsSentenceCard", "IsAudioCard",        "PitchPosition",     "PitchCategories",  "Frequency",
                                               "FreqSort",       "MiscInfo"};

// ---------------------------------------------------------------- a made-up AnkiConnect

struct FakeAnki {
	std::vector<std::string> decks{"Default", "Mining"};
	std::vector<std::pair<std::string, std::vector<std::string>>> models{{"Basic", {"Front", "Back"}}, {"Lapis", kLapisFields}};
	struct Note {
		double id;
		json::Value note;
	};
	std::vector<Note> notes;
	std::map<std::string, std::string> media;
	std::string key;        // the key it wants
	bool down = false;      // nothing answers
	int busy = 0;           // how many times it says it is busy first
	bool old = false;       // it does not know canAddNotesWithErrorDetail
	std::vector<std::string> actions;
	double next_id = 1700000000000;

	const std::vector<std::string> *fields_of(const std::string &model) const {
		for (const auto &known : models)
			if (known.first == model) return &known.second;
		return nullptr;
	}

	std::string first_field(const json::Value &note) const {
		const std::vector<std::string> *fields = fields_of(note.str("modelName"));
		return fields && !fields->empty() ? note["fields"].str((*fields)[0]) : std::string();
	}

	// Why a note cannot be added, as AnkiConnect says it; empty when it can.
	std::string refusal(const json::Value &note) const {
		if (!fields_of(note.str("modelName"))) return "model was not found: " + note.str("modelName");
		if (std::find(decks.begin(), decks.end(), note.str("deckName")) == decks.end()) return "deck was not found: " + note.str("deckName");
		std::string first = first_field(note);
		if (first.empty()) return "cannot create note because it is empty";
		for (const Note &known : notes)
			if (known.note.str("deckName") == note.str("deckName") && first_field(known.note) == first)
				return "cannot create note because it is a duplicate";
		return {};
	}

	void answer(const FetchRequest &request, FetchAnswer &answer) {
		if (down) {
			answer.status = 0;
			answer.error = "The connection was refused";
			return;
		}
		json::Value message = parsed(request.body);
		std::string action = message.str("action");
		actions.push_back(action);
		const json::Value &params = message["params"];
		json::Value body = json::Value::object();
		auto reply = [&](json::Value result, const std::string &error = "") {
			body.set("result", std::move(result));
			body.set("error", error.empty() ? json::Value() : json::Value(error));
		};
		answer.status = 200;
		answer.content_type = "application/json";
		if (busy > 0) {
			--busy;
			reply(json::Value(), "The collection is busy. Try again in a moment.");
		} else if (message.str("key") != key) {
			reply(json::Value(), "valid api key must be provided");
		} else if (message.number("version") != 6) {
			reply(json::Value(), "unsupported version");
		} else if (action == "version") {
			reply(6);
		} else if (action == "deckNames") {
			json::Value list = json::Value::array();
			for (const std::string &deck : decks) list.push(deck);
			reply(std::move(list));
		} else if (action == "modelNames") {
			json::Value list = json::Value::array();
			for (const auto &model : models) list.push(model.first);
			reply(std::move(list));
		} else if (action == "modelFieldNames") {
			const std::vector<std::string> *fields = fields_of(params.str("modelName"));
			if (!fields) {
				reply(json::Value(), "model was not found: " + params.str("modelName"));
			} else {
				json::Value list = json::Value::array();
				for (const std::string &field : *fields) list.push(field);
				reply(std::move(list));
			}
		} else if (action == "findNotes") {
			// note:"Lapis"
			std::string query = params.str("query");
			json::Value list = json::Value::array();
			for (const Note &known : notes)
				if (query == "note:\"" + known.note.str("modelName") + "\"") list.push(known.id);
			reply(std::move(list));
		} else if (action == "notesInfo") {
			json::Value list = json::Value::array();
			for (const json::Value &id : params["notes"].items()) {
				json::Value info = json::Value::object();
				info.set("noteId", id);
				info.set("cards", json::Value::array()).push(id.as_number() + 1);
				list.push(std::move(info));
			}
			reply(std::move(list));
		} else if (action == "cardsInfo") {
			json::Value list = json::Value::array();
			for (const json::Value &card : params["cards"].items())
				for (const Note &known : notes)
					if (known.id + 1 == card.as_number()) {
						json::Value info = json::Value::object();
						info.set("deckName", known.note.str("deckName"));
						list.push(std::move(info));
					}
			reply(std::move(list));
		} else if (action == "canAddNotesWithErrorDetail" && !old) {
			json::Value list = json::Value::array();
			for (const json::Value &note : params["notes"].items()) {
				std::string why = refusal(note);
				json::Value entry = json::Value::object();
				entry.set("canAdd", why.empty());
				if (!why.empty()) entry.set("error", why);
				list.push(std::move(entry));
			}
			reply(std::move(list));
		} else if (action == "storeMediaFile") {
			std::string data;
			unbase64(params.str("data"), data);
			media[params.str("filename")] = data;
			reply(params.str("filename"));
		} else if (action == "addNote") {
			std::string why = refusal(params["note"]);
			if (!why.empty()) {
				reply(json::Value(), why);
			} else {
				notes.push_back(Note{next_id, params["note"]});
				reply(next_id);
				next_id += 1000;
			}
		} else {
			reply(json::Value(), "unsupported action");
		}
		answer.body = json::write(body);
	}
};

// ---------------------------------------------------------------- the made-up network

struct Network {
	FakeAnki anki;
	std::vector<std::string> fetched;  // every address asked, in order
	std::string dictionary_zip;        // what https://dict.example.org/words.zip gives
	bool audio_down = false;

	HttpFetch fetch() {
		return [this](const FetchRequest &request, FetchAnswer &answer) { serve(request, answer); };
	}

	void serve(const FetchRequest &request, FetchAnswer &answer) {
		fetched.push_back(request.url);
		answer = FetchAnswer();
		answer.final_url = request.url;
		const std::string &url = request.url;
		auto starts = [&](const char *prefix) { return url.rfind(prefix, 0) == 0; };
		if (starts("http://anki.example.org:8765")) {
			anki.answer(request, answer);
		} else if (starts("http://audio.example.org/list?")) {
			if (audio_down) return;
			answer.status = 200;
			answer.content_type = "application/json";
			if (url.find("term=%E7%8A%AC") != std::string::npos) {  // 犬: the source has nothing
				answer.body = R"({"type":"audioSourceList","audioSources":[]})";
			} else if (url.find("term=%E7%8C%AB") != std::string::npos) {  // 猫: the first sounds are gone
				answer.body =
				    R"({"type":"audioSourceList","audioSources":[{"name":"a","url":"/gone/1.mp3"},{"name":"b","url":"/gone/2.mp3"},{"name":"c","url":"/gone/3.mp3"},{"name":"d","url":"/sounds/nhk.mp3"}]})";
			} else {
				answer.body = R"j({"type":"audioSourceList","audioSources":[
					{"name":"  NHK  ","url":"http://audio.example.org/sounds/nhk.mp3"},
					{"name":"Forvo","url":"sounds/forvo.ogg"},
					{"url":"//cdn.example.org/third"},
					{"name":"NHK","url":"/sounds/nhk-2.mp3?speaker=2"},
					{"name":"Script","url":"javascript:alert(1)"},
					{"name":"No address"}]})j";
			}
		} else if (starts("http://audio.example.org/sounds/nhk.mp3")) {
			answer.status = 200;
			answer.content_type = "audio/mpeg";
			answer.body = "ID3 nhk";
		} else if (starts("http://audio.example.org/sounds/forvo.ogg")) {
			answer.status = 200;
			answer.content_type = "application/octet-stream";
			answer.body = "OggS forvo";
		} else if (starts("http://cdn.example.org/third")) {
			answer.status = 200;
			answer.content_type = "audio/x-m4a";
			answer.body = "m4a third";
		} else if (starts("http://audio.example.org/sounds/nhk-2.mp3?speaker=2")) {
			answer.status = 200;
			answer.content_type = "audio/mp3";
			answer.body = "ID3 nhk two";
		} else if (starts("http://direct.example.org/")) {
			answer.status = 200;
			answer.final_url = "http://files.example.org/voices/word.OGG?token=1";
			answer.body = "OggS direct";
		} else if (starts("http://page.example.org/")) {
			answer.status = 200;
			answer.content_type = "text/html";
			answer.body = "<html>";
		} else if (starts("https://dict.example.org/words.zip")) {
			answer.status = 200;
			if (request.progress) request.progress(0.5);
			std::ofstream file(request.to_file, std::ios::binary | std::ios::trunc);
			file << dictionary_zip;
			if (request.progress) request.progress(1);
		} else if (starts("https://dict.example.org/missing.zip")) {
			answer.status = 404;
		}
	}
};

std::string scratch;

// A small dictionary: 食べる with a frequency and a pitch, 話 and 昨日.
std::string dictionary_zip() {
	std::vector<std::pair<std::string, std::string>> files = {
	    {"index.json", R"({"title":"Words","revision":"1","format":3,"frequencyMode":"rank-based"})"},
	    {"term_bank_1.json",
	     R"([["食べる","たべる","","v1",10,["to eat"],1,""],["話","はなし","","n",5,["talk","story"],2,""],["昨日","きのう","","n",5,["yesterday"],3,""]])"},
	    {"term_meta_bank_1.json", R"([["食べる","freq",{"reading":"たべる","frequency":512}],["食べる","pitch",{"reading":"たべる","pitches":[{"position":2}]}]])"},
	    {"styles.css", ".gloss { color: red; }"},
	    {"pictures/bowl.png", std::string("\x89PNG", 4)},
	};
	std::string path = scratch + "/words.zip";
	testing_zip::write_zip(path, files);
	return path;
}

// ---------------------------------------------------------------- AnkiConnect

void test_anki_connect() {
	std::string address;
	check(read_anki_address("  anki.example.org:8765\n", address) && address == "http://anki.example.org:8765", "a bare host gets http before it");
	check(read_anki_address("HTTPS://anki.example.org/connect", address) && address == "HTTPS://anki.example.org/connect", "an https address is kept");
	check(!read_anki_address("", address) && !read_anki_address("ftp://anki.example.org", address) && !read_anki_address("http://", address) &&
	          !read_anki_address("http://an ki", address),
	      "what is no http or https address is refused");
	check(!read_anki_address("http://name:word@anki.example.org", address), "a name and password in the address are refused");

	std::string back;
	check_eq(base64("any carnal pleas"), "YW55IGNhcm5hbCBwbGVhcw==", "base64 with two fill characters");
	check_eq(base64("any carnal pleasu"), "YW55IGNhcm5hbCBwbGVhc3U=", "with one");
	check_eq(base64(std::string("\x00\xff\x80", 3)), "AP+A", "with none, of any bytes");
	check(unbase64("YW55IGNhcm5hbCBwbGVhc3U=", back) && back == "any carnal pleasu" && unbase64("AP+A", back) && back == std::string("\x00\xff\x80", 3) &&
	          !unbase64("a*b", back),
	      "base64 is read back");
	check_eq(mark_of_text("abc"), "1a47e90b", "the mark of a text");
	check_eq(mark_of_text("食べる|たべる"), "a697fa53", "is the one the mining page gives");
	check_eq(mark_of_text("𠮷野家|よしのや"), "cdbe4a31", "also past the first plane");
	check_eq(mark_of_bytes("OggS voice"), "b3c5f543", "the mark of a file's bytes");

	Network net;
	net.anki.key = "secret";
	AnkiClient anki("http://anki.example.org:8765", "secret", net.fetch());
	json::Value result;
	AnkiProblem problem;
	check(anki.invoke("version", result, problem) && result.as_number() == 6, "AnkiConnect is asked with the key");
	net.anki.busy = 1;
	check(anki.invoke("deckNames", result, problem) && result.size() == 2, "a busy collection is asked once more");
	net.anki.busy = 2;
	check(!anki.invoke("deckNames", result, problem) && problem.message == "Anki said: The collection is busy. Try again in a moment." &&
	          !problem.unreachable,
	      "but not for ever");
	net.anki.busy = 0;
	AnkiClient wrong("http://anki.example.org:8765", "other", net.fetch());
	check(!wrong.invoke("version", result, problem) && problem.message == "The AnkiConnect did not take the key. Check it in the settings.",
	      "a key that is not taken is said so");
	net.anki.down = true;
	check(!anki.invoke("version", result, problem) && problem.unreachable &&
	          problem.message == "Anki could not be reached at http://anki.example.org:8765 (The connection was refused).",
	      "an Anki that does not answer is told from one that says no");
	net.anki.down = false;
	AnkiClient elsewhere("http://page.example.org/", "", net.fetch());
	check(!elsewhere.invoke("version", result, problem) &&
	          problem.message == "What answers at this address is not AnkiConnect. Check it in the settings.",
	      "what is no AnkiConnect is said so");
	AnkiClient none("", "", net.fetch());
	check(!none.usable() && !none.invoke("version", result, problem) && problem.unreachable, "no address, nothing asked");
	std::string stored;
	check(anki.store_media("a.jpg", std::string("\xff\xd8\x00", 3), stored, problem) && stored == "a.jpg" &&
	          net.anki.media["a.jpg"] == std::string("\xff\xd8\x00", 3),
	      "a file goes into Anki's media as it is");
}

// ---------------------------------------------------------------- fields

std::string role_of(const std::vector<std::pair<std::string, std::string>> &roles, const std::string &field) {
	for (const auto &role : roles)
		if (role.first == field) return role.second;
	return "?";
}

void test_fields() {
	auto lapis = roles_for(kLapisFields, json::Value::object());
	const char *expected[][2] = {
	    {"Expression", "word"},         {"ExpressionFurigana", "furigana"}, {"ExpressionReading", "reading"},
	    {"ExpressionAudio", "wordAudio"}, {"SelectionText", "selectionText"}, {"MainDefinition", "meaning"},
	    {"Sentence", "sentence"},       {"SentenceAudio", "sound"},         {"Picture", "picture"},
	    {"PitchPosition", "pitch"},     {"PitchCategories", "pitchCategories"}, {"Frequency", "frequency"},
	    {"FreqSort", "frequency"},      {"MiscInfo", "source"},             {"Glossary", "none"},
	    {"DefinitionPicture", "none"},  {"SentenceFurigana", "none"},       {"Hint", "none"},
	    {"IsSentenceCard", "none"},
	};
	for (auto &row : expected) check_eq(role_of(lapis, row[0]), row[1], std::string("Lapis's ") + row[0]);
	check(lapis.size() == kLapisFields.size() && lapis[0].first == "Expression" && lapis.back().first == "MiscInfo", "the fields keep their order");

	auto other = roles_for({"Word", "Word Reading", "Meaning", "sentence", "Audio", "Image", "Sentence Audio", " Source ", "Notes"}, json::Value::object());
	check(role_of(other, "Word") == "word" && role_of(other, "Word Reading") == "reading" && role_of(other, "Meaning") == "meaning" &&
	          role_of(other, "sentence") == "sentence" && role_of(other, "Audio") == "sound" && role_of(other, "Image") == "picture" &&
	          role_of(other, " Source ") == "source" && role_of(other, "Notes") == "none",
	      "other note types are guessed by their names, in any case");
	check_eq(role_of(other, "Sentence Audio"), "none", "a part goes into one field only");

	json::Value chosen = parsed(R"({"Front":"sentence","Back":"word","Gone":"picture","Extra":"nonsense"})");
	auto kept = roles_for({"Front", "Back", "Sentence", "Extra", "Picture"}, chosen);
	check(role_of(kept, "Front") == "sentence" && role_of(kept, "Back") == "word", "choices made before are kept");
	check(role_of(kept, "Sentence") == "none" && role_of(kept, "Picture") == "picture" && role_of(kept, "Extra") == "none",
	      "and new fields are guessed for the parts no field has yet");

	check(is_lapis(kLapisFields), "Lapis is known by its fields");
	check(!is_lapis({"Expression", "Sentence", "Picture"}), "another note type is not");
	check(field_roles().size() == 14 && std::string(field_roles()[0].role) == "none" && is_field_role("pitchCategories") &&
	          !is_field_role("nonsense") && is_media_role("wordAudio") && !is_media_role("word"),
	      "the parts of a card");

	Network net;
	AnkiConnectTarget target(AnkiClient("http://anki.example.org:8765", "", net.fetch()));
	CardPlaces places;
	AnkiProblem problem;
	check(target.places(places, problem) && places.version == "6" && places.decks.size() == 2 && places.note_types.size() == 2,
	      "the decks and note types of an Anki");
	FoundNoteType found;
	check(find_lapis(target, places, found, problem) && found.note_type == "Lapis" && found.deck == "Mining" && found.field_names == kLapisFields,
	      "Lapis is found, with the one deck made besides Default");
	net.anki.decks = {"Default", "Mining", "Other"};
	net.anki.models.push_back({"Kiku copy", kLapisFields});
	net.anki.models.push_back({"my lapis", kLapisFields});
	std::swap(net.anki.models[1], net.anki.models[3]);  // Basic, my lapis, Kiku copy, Lapis
	target.places(places, problem);
	found = FoundNoteType();
	check(find_lapis(target, places, found, problem) && found.note_type == "Lapis" && found.deck.empty(),
	      "Lapis by name comes first; with several decks and no card the deck is left open");
	json::Value note = parsed(R"({"deckName":"Other","modelName":"Lapis","fields":{"Expression":"x"}})");
	net.anki.notes.push_back({1700000000000, note});
	found = FoundNoteType();
	check(find_lapis(target, places, found, problem) && found.deck == "Other", "the deck of the newest card of that note type is taken");
	net.anki.models = {{"Basic", {"Front", "Back"}}};
	target.places(places, problem);
	check(!find_lapis(target, places, found, problem) && !problem.unreachable, "an Anki without Lapis has none");
	net.anki.models.push_back({"Lapis", kLapisFields});
	target.places(places, problem);
	net.anki.down = true;
	check(!find_lapis(target, places, found, problem) && problem.unreachable, "an Anki that stops answering is said so");
}

// ---------------------------------------------------------------- how a word sounds

void test_word_audio() {
	std::string address, problem;
	check(read_audio_address("  http://audio.example.org/list?term={term}&reading={reading}  ", address, problem) &&
	          address == "http://audio.example.org/list?term={term}&reading={reading}",
	      "an audio source's address is read");
	check(!read_audio_address("http://audio.example.org/list", address, problem) &&
	          problem == "The address needs {term} where the word goes, as in Yomitan's custom audio sources.",
	      "one without {term} is refused");
	check(!read_audio_address("audio.example.org/{term}", address, problem) && problem == "That is not an http or https address." &&
	          !read_audio_address("http://name:word@audio.example.org/{term}", address, problem) &&
	          !read_audio_address("file:///{term}", address, problem),
	      "so is one that is no web address, or has a name and password");
	check_eq(fill_audio_address("http://a.example.org/?term={term}&reading={reading}&lang={language}", "食べる", "た b&c"),
	         "http://a.example.org/?term=%E9%A3%9F%E3%81%B9%E3%82%8B&reading=%E3%81%9F%20b%26c&lang=ja", "the word and its reading go into the address");

	Network net;
	const std::string source = "http://audio.example.org/list?term={term}&reading={reading}";
	std::vector<std::string> names;
	check(word_audio_choices(net.fetch(), source, " 食べる ", "", names) == WordAudioOutcome::Found, "a list of sounds is read");
	check(names == std::vector<std::string>({"NHK", "Forvo", "Sound 3", "NHK (2)"}), "each sound has a name of its own, and only web addresses count");
	check_eq(net.fetched[0], "http://audio.example.org/list?term=%E9%A3%9F%E3%81%B9%E3%82%8B&reading=%E9%A3%9F%E3%81%B9%E3%82%8B",
	         "a word without a reading is asked for as it is written");

	WordSound sound;
	check(find_word_audio(net.fetch(), source, "食べる", "たべる", "", sound) == WordAudioOutcome::Found && sound.data == "ID3 nhk" &&
	          sound.type == "audio/mpeg" && sound.extension == "mp3",
	      "the first sound of the list is taken");
	check(find_word_audio(net.fetch(), source, "食べる", "たべる", "Forvo", sound) == WordAudioOutcome::Found && sound.data == "OggS forvo" &&
	          sound.type == "audio/ogg" && sound.extension == "ogg",
	      "or the one chosen by name; a file without a type is known by its ending");
	check_eq(net.fetched.back(), "http://audio.example.org/sounds/forvo.ogg", "an address given from the list's own is followed");
	check(find_word_audio(net.fetch(), source, "食べる", "たべる", "Sound 3", sound) == WordAudioOutcome::Found && sound.type == "audio/mp4" &&
	          sound.extension == "m4a" && net.fetched.back() == "http://cdn.example.org/third",
	      "also one that names only its host");
	check(find_word_audio(net.fetch(), source, "食べる", "たべる", "NHK (2)", sound) == WordAudioOutcome::Found && sound.data == "ID3 nhk two",
	      "and the second of a name");
	check(find_word_audio(net.fetch(), source, "食べる", "たべる", "Nobody", sound) == WordAudioOutcome::NotFound, "a name the list has not");
	check(find_word_audio(net.fetch(), source, "犬", "いぬ", "", sound) == WordAudioOutcome::NotFound &&
	          word_audio_choices(net.fetch(), source, "犬", "いぬ", names) == WordAudioOutcome::Found && names.empty(),
	      "a word the source has no sound for");
	size_t before = net.fetched.size();
	check(find_word_audio(net.fetch(), source, "猫", "ねこ", "", sound) == WordAudioOutcome::NotFound && net.fetched.size() == before + 4,
	      "three sounds are tried, no more");
	check(find_word_audio(net.fetch(), "", "食べる", "たべる", "", sound) == WordAudioOutcome::NoSource &&
	          find_word_audio(nullptr, source, "食べる", "たべる", "", sound) == WordAudioOutcome::NoSource,
	      "no source, no sound");
	check(find_word_audio(net.fetch(), source, "  ", "", "", sound) == WordAudioOutcome::NotFound, "no word, no sound");
	check(find_word_audio(net.fetch(), "http://page.example.org/{term}", "食べる", "", "", sound) == WordAudioOutcome::Failed,
	      "a source that answers with something else has failed");
	net.audio_down = true;
	check(find_word_audio(net.fetch(), source, "食べる", "たべる", "", sound) == WordAudioOutcome::Failed &&
	          word_audio_choices(net.fetch(), source, "食べる", "たべる", names) == WordAudioOutcome::Failed,
	      "so has one that does not answer");

	const std::string direct = "http://direct.example.org/{term}.mp3";
	check(find_word_audio(net.fetch(), direct, "食べる", "", "", sound) == WordAudioOutcome::Found && sound.data == "OggS direct" &&
	          sound.type == "audio/ogg",
	      "a source that answers with the sound itself, named by where it came from in the end");
	check(word_audio_choices(net.fetch(), direct, "食べる", "", names) == WordAudioOutcome::Found && names == std::vector<std::string>({"Sound"}) &&
	          find_word_audio(net.fetch(), direct, "食べる", "", "Sound", sound) == WordAudioOutcome::Found &&
	          find_word_audio(net.fetch(), direct, "食べる", "", "NHK", sound) == WordAudioOutcome::NotFound,
	      "has one sound, called Sound");
}

// ---------------------------------------------------------------- a card's fields

void test_card_note() {
	check_eq(field_html("  a < b & \"c\"\r\nd\ne  "), "a &lt; b &amp; &quot;c&quot;<br>d<br>e", "text for a field is escaped, its line feeds breaks");
	check_eq(sentence_with_word("昨日の話をしよう", 3, 1), "昨日の<b>話</b>をしよう", "the word is bold in the sentence");
	check_eq(sentence_with_word("　「𠮷野家で食べた」\n", 7, 3), "「𠮷野家で<b>食べた</b>」", "places are counted as a page counts them, the ends trimmed");
	check_eq(sentence_with_word("a<b", 0, 1), "<b>a</b>&lt;b", "the sentence is escaped around the word");
	check_eq(sentence_with_word("昨日の話", -1, 0), "昨日の話", "a word that is not in the line leaves it plain");
	check_eq(sentence_with_word("昨日の話", 9, 2), "昨日の話", "so does a place past its end");
	check_eq(sentence_with_word("昨日の話", 3, 9), "昨日の<b>話</b>", "a word longer than the rest ends with it");
	check_eq(plain_text("<div>to <b>eat</b><br>a &amp; b&nbsp;&lt;c&gt;</div>"), "to eat a & b <c>", "a field as plain text");
	check_eq(plain_text("<img src=\"a.jpg\">"), "", "a picture alone is no text");
	check_eq(plain_text("一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二"),
	         "一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十…", "a long one is cut");

	std::vector<std::pair<std::string, std::string>> roles = roles_for(kLapisFields, json::Value::object());
	check_eq(card_problem("", "Lapis", roles), "Choose a deck and a note type in the settings first.", "a card needs a deck");
	check_eq(card_problem("Mining", "Basic", {{"Front", "sentence"}}), "Give the word a field in the settings first.", "and a field for the word");
	check_eq(card_problem("Mining", "Lapis", roles), "", "Lapis has both");

	WordContent word = WordContent::from_json(parsed(
	    R"({"expression":"話","reading":"はなし","furigana":"話[はなし]","meaning":"<ol><li>talk</li></ol>","frequency":"812","pitch":"3","pitchCategories":"odaka","selection":"talk\nstory","at":{"start":3,"length":1}})"));
	LineContent line{"昨日の話をしよう", "俺たちに翼はない"};
	CardNote note;
	std::string problem;
	check(compose_note("Mining", "Lapis", roles, {"vn-miner", "oretsuba"}, line, word, note, problem), "a card is made of a word and its line");
	auto value = [&](const std::string &field) {
		for (const auto &known : note.fields)
			if (known.first == field) return known.second;
		return std::string("?");
	};
	check(note.deck == "Mining" && note.note_type == "Lapis" && note.tags.size() == 2 && !note.allow_duplicate &&
	          note.fields.size() == kLapisFields.size() && note.fields[0].first == "Expression",
	      "with its deck, note type, tags and every field in order");
	check_eq(value("Expression"), "話", "the word");
	check_eq(value("ExpressionReading"), "はなし", "its reading");
	check_eq(value("ExpressionFurigana"), "話[はなし]", "its furigana");
	check_eq(value("MainDefinition"), "<ol><li>talk</li></ol>", "its definitions as they are");
	check_eq(value("SelectionText"), "talk<br>story", "what was selected");
	check_eq(value("Sentence"), "昨日の<b>話</b>をしよう", "the sentence");
	check_eq(value("Frequency"), "812", "its frequency");
	check_eq(value("FreqSort"), "812", "twice, for Lapis sorts by it");
	check_eq(value("PitchPosition"), "3", "its pitch");
	check_eq(value("PitchCategories"), "odaka", "and the pattern");
	check_eq(value("MiscInfo"), "俺たちに翼はない", "the game's title");
	check(value("Picture").empty() && value("SentenceAudio").empty() && value("ExpressionAudio").empty() && value("Glossary").empty(),
	      "the fields of files and those with no part are empty");
	check(note.media.size() == 3 && note.media[0].role == "wordAudio" && note.media[0].fields == std::vector<std::string>({"ExpressionAudio"}) &&
	          note.media[1].role == "sound" && note.media[2].role == "picture" && note.media[2].data.empty(),
	      "and each file a field wants is named, without its bytes yet");

	check(!compose_note("Mining", "Basic", {{"Front", "furigana"}, {"Back", "word"}}, {}, line, WordContent::from_json(parsed(R"({"expression":"話"})")),
	                    note, problem) &&
	          problem == "A new \"Basic\" card needs something in its first field. Give it the word in the settings.",
	      "a card whose first field would be empty is not made");
	check(compose_note("Mining", "Basic", {{"Front", "picture"}, {"Back", "word"}, {"More", "picture"}}, {}, line, word, note, problem) &&
	          note.media.size() == 1 && note.media[0].fields.size() == 2,
	      "unless a file goes there; a file may go into two fields");
}

// ---------------------------------------------------------------- the kit

struct Call {
	bool ok = false;
	json::Value result;
	std::string error, kind;
};

Call ask(MiningKit &kit, const std::string &method, const std::string &params = "") {
	json::Value answer = parsed(kit.call(method, params));
	Call call;
	call.ok = answer.boolean("ok");
	call.result = answer["result"];
	call.error = answer.str("error");
	call.kind = answer.str("kind");
	return call;
}

const char kTaberu[] =
    R"({"word":{"expression":"食べる","reading":"たべる","furigana":"食[た]べる","meaning":"<ol><li>to eat</li></ol>","frequency":"512","pitch":"2","pitchCategories":"kifuku","selection":"","at":{"start":0,"length":3}}})";

void test_kit(const std::string &root) {
	Network net;
	net.anki.key = "secret";
	std::string folder = scratch + "/kit", problem;
	std::vector<std::string> encoded;
	MiningKit::Options options;
	options.folder = folder;
	options.resources = root + "/resources/mining";
	options.fetch = net.fetch();
	options.encode_voice = [&](const std::string &file, const std::string &ending, std::string &out, std::string &type, std::string &out_ending) {
		encoded.push_back(ending);
		if (file == "OggS keep") return false;
		out = "m4a of " + file;
		type = "audio/mp4";
		out_ending = "m4a";
		return true;
	};

	MiningKit::Options nowhere = options;
	nowhere.resources = scratch + "/none";
	check(!MiningKit::open(nowhere, problem) && problem == "The rules for undoing inflections (transforms.json) are missing from the app.",
	      "a kit without its rules does not start");

	auto kit = MiningKit::open(options, problem);
	check(kit != nullptr, "the kit starts: " + problem);
	if (!kit) return;

	// ---- settings
	Call call = ask(*kit, "settings.get");
	check(call.ok && !call.result.boolean("direct", true) && call.result["anki"].str("target") == "connect" &&
	          call.result["anki"].str("address").empty() && call.result["anki"]["tags"].size() == 1 &&
	          call.result["anki"]["tags"][0].as_string() == "vn-miner" && call.result["popup"].number("textSize") == 100 &&
	          !call.result.boolean("ankiKeySet", true) && !call.result.boolean("audioSet", true),
	      "a new kit has the usual choices and mines nothing");
	check(!kit->direct(), "and a tap on the text goes on as ever");
	call = ask(*kit, "page.init");
	check(call.ok && !call.result["cards"].boolean("possible", true) &&
	          call.result["cards"].str("problem") == "Set Anki up in the settings before the game starts." && !call.result.boolean("wordAudio", true) &&
	          call.result["dictionaries"].size() == 0,
	      "the page is told that no card can be made yet, and why");
	call = ask(*kit, "anki.check");
	check(!call.ok && call.kind == "settings" && call.error == "Enter the address of your AnkiConnect first.", "Anki is not asked without an address");
	call = ask(*kit, "anki.check", R"({"address":"ftp://anki.example.org"})");
	check(!call.ok && call.kind == "address", "nor at one that is no web address");

	call = ask(*kit, "anki.check", R"({"address":"anki.example.org:8765","key":"wrong"})");
	check(!call.ok && call.kind == "anki" && call.error == "The AnkiConnect did not take the key. Check it in the settings.", "a wrong key is said so");
	net.anki.down = true;
	call = ask(*kit, "anki.check", R"({"address":"anki.example.org:8765","key":"secret"})");
	check(!call.ok && call.kind == "unreachable", "an Anki that does not answer is said so");
	net.anki.down = false;
	call = ask(*kit, "anki.check", R"({"address":"anki.example.org:8765","key":"secret"})");
	check(call.ok && call.result.str("version") == "6" && call.result["decks"].size() == 2 && call.result["noteTypes"].size() == 2 &&
	          call.result["lapis"].str("noteType") == "Lapis" && call.result["lapis"].str("deck") == "Mining",
	      "trying an address out tells what is there, and that Lapis is");
	check(call.result["settings"]["anki"].str("address").empty() && !call.result["settings"].boolean("ankiKeySet", true),
	      "and keeps nothing yet");
	call = ask(*kit, "anki.check", R"({"address":"anki.example.org:8765","key":"secret","adopt":true})");
	const json::Value &anki = call.result["settings"]["anki"];
	check(call.ok && anki.str("address") == "http://anki.example.org:8765" && anki.str("noteType") == "Lapis" && anki.str("deck") == "Mining" &&
	          anki["fields"].size() == kLapisFields.size() && anki["fields"][0].str("field") == "Expression" &&
	          anki["fields"][0].str("role") == "word" && call.result["settings"].boolean("ankiKeySet"),
	      "taking it over keeps the address and the key, and with nothing chosen, Lapis, its deck and its fields");

	call = ask(*kit, "settings.set", R"({"direct":true,"anki":{"tags":["vn-miner oretsuba", "", "oretsuba"]},"popup":{"textSize":120,"hideTags":true}})");
	check(call.ok && call.result.boolean("direct") && call.result["anki"]["tags"].size() == 2 && call.result["anki"].str("noteType") == "Lapis" &&
	          call.result["popup"].number("textSize") == 115 && call.result["popup"].boolean("hideTags") && !call.result["popup"].boolean("compact", true),
	      "settings are changed part by part, the rest left as it is");
	check(kit->direct(), "now a tap on the text looks a word up");
	call = ask(*kit, "settings.set", R"({"anki":{"address":"not an address"}})");
	check(!call.ok && call.kind == "address", "an address that is none is not kept");
	call = ask(*kit, "anki.setRole", R"({"field":"Glossary","role":"meaning"})");
	check(call.ok && call.result["anki"]["fields"][11].str("field") == "Glossary" && call.result["anki"]["fields"][11].str("role") == "meaning",
	      "a field is given a part");
	check(!ask(*kit, "anki.setRole", R"({"field":"Glossary","role":"nonsense"})").ok, "but not a part there is none of");
	ask(*kit, "anki.setRole", R"({"field":"Glossary","role":"none"})");
	call = ask(*kit, "anki.roleChoices");
	check(call.ok && call.result.size() == 14 && call.result[1].str("role") == "sentence" && call.result[1].str("label") == "Sentence",
	      "the parts to choose from are listed");

	call = ask(*kit, "anki.choose", R"({"deck":"Default","noteType":"Basic"})");
	check(call.ok && call.result["anki"].str("deck") == "Default" && call.result["anki"].str("noteType") == "Basic" &&
	          call.result["anki"]["fields"].size() == 2 && call.result["anki"]["fields"][0].str("role") == "none",
	      "choosing another note type takes its fields");
	call = ask(*kit, "page.init");
	check(call.ok && call.result["cards"].str("problem") == "Give the word a field in the settings first.", "which may leave the word without a field");
	call = ask(*kit, "anki.choose", R"({"deck":"Mining","noteType":"Lapis"})");
	check(call.ok && call.result["anki"]["fields"].size() == kLapisFields.size(), "and back");
	net.anki.models[1].second.push_back("Extra");
	call = ask(*kit, "anki.fields");
	check(call.ok && call.result["fields"].size() == kLapisFields.size() + 1 && call.result["fields"][kLapisFields.size()].str("role") == "none" &&
	          ask(*kit, "settings.get").result["anki"]["fields"].size() == kLapisFields.size() + 1,
	      "a field added in Anki since is taken in");
	net.anki.models[1].second.pop_back();
	ask(*kit, "anki.fields");
	call = ask(*kit, "anki.fields", R"({"noteType":"Basic"})");
	check(call.ok && call.result["fields"].size() == 2 && ask(*kit, "settings.get").result["anki"].str("noteType") == "Lapis",
	      "looking at another note type's fields chooses nothing");

	// ---- secrets
	call = ask(*kit, "secrets.set", R"({"audioAddress":"audio.example.org/{term}"})");
	check(!call.ok && call.kind == "address", "an audio source that is no address is refused");
	call = ask(*kit, "audio.check");
	check(!call.ok && call.kind == "settings", "and without one there is nothing to try out");
	call = ask(*kit, "audio.check", R"({"address":"http://audio.example.org/list?term={term}&reading={reading}"})");
	check(call.ok && call.result["names"].size() == 4, "an audio source is tried out before it is kept");
	call = ask(*kit, "audio.check", R"({"address":"http://page.example.org/{term}"})");
	check(!call.ok && call.kind == "unreachable", "one that answers with something else is said so");
	call = ask(*kit, "secrets.set", R"({"audioAddress":"http://audio.example.org/list?term={term}&reading={reading}"})");
	check(call.ok && call.result.boolean("audioSet") && call.result.boolean("ankiKeySet"), "the audio source is kept beside the key");
	check(read_file(folder + "/settings.json").find("audio.example.org") == std::string::npos &&
	          read_file(folder + "/settings.json").find("secret") == std::string::npos,
	      "neither is written into the settings file");

	// ---- dictionaries
	call = ask(*kit, "dict.list");
	check(call.ok && call.result["dictionaries"].size() == 0 && call.result["import"].str("state") == "idle", "no dictionary yet");
	check(call.result["suggested"].size() == 2 && call.result["suggested"][0].str("name") == "Jitendex" &&
	          call.result["suggested"][0].str("url").rfind("https://", 0) == 0 && !call.result["suggested"][0].boolean("installed", true) &&
	          call.result["suggested"][1].str("name") == "JMnedict" && !call.result["suggested"][1].boolean("installed", true),
	      "two dictionaries are suggested, neither is there");
	call = ask(*kit, "lookup", R"({"text":"食べた"})");
	check(call.ok && call.result["results"].size() == 0, "so nothing is found");
	std::string zip = dictionary_zip();
	call = ask(*kit, "dict.import", "{\"path\":" + json::quote(zip) + ",\"name\":\"words.zip\"}");
	check(call.ok, "a dictionary's file is imported");
	kit->library().wait_import();
	call = ask(*kit, "dict.list");
	check(call.ok && call.result["dictionaries"].size() == 1 && call.result["dictionaries"][0].str("title") == "Words" &&
	          call.result["dictionaries"][0].boolean("enabled") && call.result["dictionaries"][0].number("terms") == 3 &&
	          call.result["dictionaries"][0].number("fileSize") > 0 && call.result["import"].str("state") == "done" &&
	          call.result["import"].str("title") == "Words",
	      "and listed");
	check(access(zip.c_str(), F_OK) == 0, "a file the player chose is left where it is");
	std::string id = call.result["dictionaries"][0].str("id");
	call = ask(*kit, "lookup", R"({"text":"食べた"})");
	check(call.ok && call.result["results"].size() == 1 && call.result["results"][0].str("matched") == "食べた" &&
	          call.result["results"][0]["term"].str("expression") == "食べる",
	      "a word is looked up, its inflection undone");
	call = ask(*kit, "page.init");
	check(call.ok && call.result.boolean("direct") && call.result["cards"].boolean("possible") && call.result.boolean("wordAudio") &&
	          call.result["dictionaries"].size() == 1 && call.result["dictionaries"][0].str("title") == "Words" &&
	          call.result["dictionaries"][0].str("frequencyMode") == "rank-based" && call.result["styles"].size() == 1 &&
	          call.result["styles"][0].str("styles") == ".gloss { color: red; }" && call.result["popup"].number("textSize") == 115 &&
	          call.result.str("source") == kKitSource,
	      "the page is told how the popup looks, the dictionaries and that cards can be made");
	std::string bytes, type;
	check(kit->data("dict.media", R"({"dictionary":"Words","path":"pictures/bowl.png"})", bytes, type) && bytes == std::string("\x89PNG", 4) &&
	          type == "image/png" && !kit->data("dict.media", R"({"dictionary":"Words","path":"none.png"})", bytes, type),
	      "a dictionary's picture is given to the page");
	call = ask(*kit, "dict.enable", "{\"id\":" + json::quote(id) + ",\"enabled\":false}");
	check(call.ok && !call.result["dictionaries"][0].boolean("enabled", true) &&
	          ask(*kit, "lookup", R"({"text":"食べた"})").result["results"].size() == 0 && ask(*kit, "page.init").result["dictionaries"].size() == 0,
	      "a dictionary turned off is not looked into");
	ask(*kit, "dict.enable", "{\"id\":" + json::quote(id) + ",\"enabled\":true}");
	check(!ask(*kit, "dict.arrange", R"({"order":["nonsense"],"disabled":[]})").ok, "an order that names another dictionary is refused");

	net.dictionary_zip = read_file(zip);
	call = ask(*kit, "dict.download", R"({"url":"ftp://dict.example.org/words.zip"})");
	check(!call.ok && call.kind == "address", "a download needs a web address");
	call = ask(*kit, "dict.download", R"({"url":"https://dict.example.org/missing.zip"})");
	kit->library().wait_import();
	call = ask(*kit, "dict.list");
	check(call.result["import"].str("state") == "failed" && call.result["import"].str("error") == "The address answered 404." &&
	          access((folder + "/dictionaries/download.zip").c_str(), F_OK) != 0,
	      "a download that fails says why and leaves nothing");
	call = ask(*kit, "dict.download", R"({"url":" https://dict.example.org/words.zip "})");
	check(call.ok && call.result["import"].str("source") == "https://dict.example.org/words.zip", "a dictionary is downloaded");
	kit->library().wait_import();
	call = ask(*kit, "dict.list");
	check(call.result["import"].str("state") == "done" && call.result["dictionaries"].size() == 1 && call.result["dictionaries"][0].str("id") != id &&
	          access((folder + "/dictionaries/download.zip").c_str(), F_OK) != 0,
	      "and takes the place of the one of the same title; the downloaded file goes");

	// ---- the line and the card
	call = ask(*kit, "line.get");
	check(call.ok && call.result.str("sentence").empty() && !call.result.boolean("hasVoice", true), "no line yet");
	KitLine line;
	line.sentence = "食べるのが好き";
	line.speaker = "鷹志";
	line.voice = "OggS voice";
	line.voice_ending = "OGG";
	line.picture = std::string("\xff\xd8 picture", 10);
	int64_t line_id = kit->set_line(line);
	call = ask(*kit, "line.get");
	check(call.ok && call.result.number("id") == double(line_id) && call.result.str("sentence") == "食べるのが好き" &&
	          call.result.str("speaker") == "鷹志" && call.result.boolean("hasVoice") && call.result.boolean("hasPicture"),
	      "the line a word was tapped in is kept");
	check(kit->data("line.picture", "", bytes, type) && bytes == line.picture && type == "image/jpeg" && kit->data("line.voice", "", bytes, type) &&
	          bytes == "OggS voice" && type == "audio/ogg",
	      "with its picture and its voice");
	check(kit->data("audio.word", R"({"term":"食べる","reading":"たべる"})", bytes, type) && bytes == "ID3 nhk" && type == "audio/mpeg" &&
	          kit->data("audio.word", R"({"term":"食べる","reading":"たべる","source":"Forvo"})", bytes, type) && bytes == "OggS forvo" &&
	          !kit->data("audio.word", R"({"term":"犬","reading":"いぬ"})", bytes, type),
	      "how a word sounds is given to the page");
	call = ask(*kit, "audio.sources", R"({"term":"食べる","reading":"たべる"})");
	check(call.ok && call.result["names"].size() == 4 && call.result["names"][3].as_string() == "NHK (2)", "and the sounds to choose from");

	std::string taberu = json::write(parsed(kTaberu)["word"]);
	call = ask(*kit, "card.known", "{\"words\":[" + taberu + ",{\"expression\":\"話\",\"reading\":\"はなし\"}]}");
	check(call.ok && call.result["known"].size() == 2 && !call.result["known"][0].as_bool(true) && !call.result["known"][1].as_bool(true),
	      "Anki has neither word yet");

	net.anki.actions.clear();
	call = ask(*kit, "card.add", kTaberu);
	check(call.ok && !call.result.boolean("handedOver", true) && call.result.number("noteId") == 1700000000000 &&
	          call.result.str("label") == "食べる" && !call.result.boolean("noWordAudio", true) &&
	          call.result.str("message") == "Made a card of 食べる.",
	      "a card is made: " + call.error);
	check(net.anki.actions == std::vector<std::string>({"canAddNotesWithErrorDetail", "storeMediaFile", "storeMediaFile", "storeMediaFile", "addNote"}),
	      "Anki is asked first whether it has the card, then given the files, then the card");
	check(net.anki.notes.size() == 1, "one card is in Anki");
	if (net.anki.notes.size() == 1) {
		const json::Value &note = net.anki.notes[0].note;
		const json::Value &fields = note["fields"];
		check(note.str("deckName") == "Mining" && note.str("modelName") == "Lapis" && note["tags"].size() == 2 &&
		          note["tags"][1].as_string() == "oretsuba" && !note["options"].boolean("allowDuplicate", true) &&
		          note["options"].str("duplicateScope") == "deck",
		      "in the deck and note type of the settings, with their tags");
		check_eq(fields.str("Expression"), "食べる", "its word");
		check_eq(fields.str("ExpressionFurigana"), "食[た]べる", "its furigana");
		check_eq(fields.str("Sentence"), "<b>食べる</b>のが好き", "its sentence");
		check_eq(fields.str("MainDefinition"), "<ol><li>to eat</li></ol>", "its definitions");
		check_eq(fields.str("Frequency"), "512", "its frequency");
		check_eq(fields.str("FreqSort"), "512", "its sorting");
		check_eq(fields.str("PitchCategories"), "kifuku", "its pitch pattern");
		check_eq(fields.str("MiscInfo"), kKitSource, "its source");
		const std::string of_line = mark_of_text("食べるのが好き");
		std::string picture = "oretsuba_" + of_line + "_" + mark_of_bytes(line.picture) + ".jpg";
		std::string voice = "oretsuba_" + of_line + "_" + mark_of_bytes("m4a of OggS voice") + ".m4a";
		check_eq(fields.str("Picture"), "<img src=\"" + picture + "\">", "its picture");
		check_eq(fields.str("SentenceAudio"), "[sound:" + voice + "]", "its voice");
		check_eq(fields.str("ExpressionAudio"), "[sound:vn-miner_word_a697fa53.mp3]", "how its word sounds, named as the mining page names it");
		check(net.anki.media[picture] == line.picture && net.anki.media[voice] == "m4a of OggS voice" &&
		          net.anki.media["vn-miner_word_a697fa53.mp3"] == "ID3 nhk" && encoded == std::vector<std::string>({"OGG"}),
		      "the files are in Anki's media, the voice made one every Anki plays");
	}

	call = ask(*kit, "card.known", "{\"words\":[" + taberu + ",{\"expression\":\"話\",\"reading\":\"はなし\"}]}");
	check(call.ok && call.result["known"][0].as_bool() && !call.result["known"][1].as_bool(true), "now Anki has the first word");
	size_t stored = net.anki.media.size();
	call = ask(*kit, "card.add", kTaberu);
	check(!call.ok && call.kind == "duplicate" && call.error == "This deck has a card like this already." && net.anki.media.size() == stored &&
	          net.anki.notes.size() == 1,
	      "a second card of it is refused before any file is made");
	net.anki.old = true;
	call = ask(*kit, "card.add", kTaberu);
	check(!call.ok && call.kind == "duplicate" && net.anki.notes.size() == 1, "also by an AnkiConnect that only says so when the card is added");
	check(ask(*kit, "card.known", "{\"words\":[" + taberu + "]}").ok, "which cannot say beforehand, and is not held to it");
	net.anki.old = false;

	line.sentence = "犬の話";
	line.voice = "OggS keep";
	line.voice_ending = "ogg";
	line.picture.clear();
	kit->set_line(line);
	call = ask(*kit, "card.add",
	           R"({"word":{"expression":"犬","reading":"いぬ","furigana":"犬[いぬ]","meaning":"dog","at":{"start":0,"length":1}},"audioChoice":""})");
	check(call.ok && call.result.boolean("noWordAudio") && call.result.str("message") == "Made a card of 犬. The audio source had no sound for it.",
	      "a word the source has no sound for still gets its card");
	if (net.anki.notes.size() == 2) {
		const json::Value &fields = net.anki.notes[1].note["fields"];
		check(fields.str("ExpressionAudio").empty() && fields.str("Picture").empty() &&
		          fields.str("SentenceAudio") == "[sound:oretsuba_" + mark_of_text("犬の話") + "_" + mark_of_bytes("OggS keep") + ".ogg]" &&
		          fields.str("Frequency").empty(),
		      "without its sound and without a picture the line has none of; a voice that is not converted stays as it is");
	}
	line.sentence = "猫の話";
	line.voice.clear();
	kit->set_line(line);
	call = ask(*kit, "card.add",
	           R"({"word":{"expression":"話","reading":"はなし","furigana":"話[はなし]","meaning":"talk","at":{"start":2,"length":1}},"audioChoice":"Forvo"})");
	check(call.ok && net.anki.notes.size() == 3 &&
	          net.anki.notes[2].note["fields"].str("ExpressionAudio") == "[sound:vn-miner_word_" + mark_of_text("話|はなし|Forvo") + ".ogg]" &&
	          net.anki.notes[2].note["fields"].str("SentenceAudio").empty(),
	      "the sound chosen in the popup goes onto the card; a line without a voice has none");

	check(!ask(*kit, "card.add", R"({"word":{"reading":"はなし"}})").ok, "no word, no card");
	net.anki.down = true;
	call = ask(*kit, "card.add", R"({"word":{"expression":"昨日","reading":"きのう","meaning":"yesterday"}})");
	check(!call.ok && call.kind == "unreachable" && call.error.find("Anki could not be reached at http://anki.example.org:8765") == 0,
	      "an Anki that cannot be reached is said so");
	net.anki.down = false;
	ask(*kit, "anki.choose", R"({"noteType":"Basic"})");
	call = ask(*kit, "card.add", kTaberu);
	check(!call.ok && call.kind == "settings" && call.error == "Give the word a field in the settings first.", "settings that make no card say what is missing");
	ask(*kit, "anki.choose", R"({"noteType":"Lapis"})");

	// ---- another place for cards, and what the page is told later
	struct Other : CardTarget {
		std::vector<CardNote> added;
		bool places(CardPlaces &places, AnkiProblem &) override {
			places.decks = {"On the phone"};
			places.note_types = {"Lapis"};
			return true;
		}
		bool field_names(const std::string &, std::vector<std::string> &names, AnkiProblem &) override {
			names = kLapisFields;
			return true;
		}
		bool known(const std::vector<CardNote> &notes, std::vector<Known> &answers, AnkiProblem &) override {
			answers.assign(notes.size(), Known::CannotSay);
			return true;
		}
		bool add(const CardNote &note, AddOutcome &outcome, AnkiProblem &) override {
			added.push_back(note);
			outcome.handed_over = true;
			return true;
		}
	};
	auto other = std::make_shared<Other>();
	kit->set_target("app", other);
	std::vector<std::string> events;
	kit->set_events([&](const std::string &name, const std::string &payload) { events.push_back(name + " " + payload); });
	ask(*kit, "settings.set", R"({"anki":{"target":"app"}})");
	call = ask(*kit, "anki.check", R"({"adopt":true})");
	check(call.ok && call.result["decks"].size() == 1 && call.result["lapis"].str("deck") == "On the phone", "another place for cards is asked the same way");
	ask(*kit, "anki.choose", R"({"deck":"On the phone"})");
	line.sentence = "昨日の話";
	line.voice = "OggS voice";
	line.picture = "jpeg";
	kit->set_line(line);
	call = ask(*kit, "card.add", R"({"word":{"expression":"昨日","reading":"きのう","meaning":"yesterday","at":{"start":0,"length":2}}})");
	check(call.ok && call.result.boolean("handedOver") && call.result.str("message") == "Handed 昨日 to Anki." && other->added.size() == 1,
	      "a card handed to another app is said to be handed over");
	if (other->added.size() == 1) {
		const CardNote &note = other->added[0];
		bool voice = false, picture = false;
		for (const CardMedia &media : note.media) {
			if (media.role == "sound") voice = media.data == "m4a of OggS voice" && media.type == "audio/mp4" && media.fields == std::vector<std::string>({"SentenceAudio"});
			if (media.role == "picture") picture = media.data == "jpeg" && media.type == "image/jpeg" && !media.name.empty();
		}
		check(voice && picture && note.deck == "On the phone", "its files come as bytes, for the target to put where its Anki wants them");
	}
	kit->emit("card.added", parsed(R"({"word":"昨日"})"));
	check(events == std::vector<std::string>({"card.added {\"word\":\"昨日\"}"}), "what happens later is told to the page");
	ask(*kit, "settings.set", R"({"anki":{"target":"gone"}})");
	check(!ask(*kit, "card.add", kTaberu).ok && !ask(*kit, "page.init").result["cards"].boolean("possible", true), "a place that is not there makes no cards");
	ask(*kit, "settings.set", R"({"anki":{"target":"connect"}})");

	check(!ask(*kit, "nonsense").ok && !ask(*kit, "settings.get", "{not json").ok, "a call that is not understood says so");
	kit->set_method("host.double", [](const json::Value &params, json::Value &result, AnkiProblem &problem) {
		if (!params["n"].is_number()) {
			problem.message = "No number to double.";
			return false;
		}
		result = json::Value(params["n"].as_number() * 2);
		return true;
	});
	call = ask(*kit, "host.double", R"({"n":21})");
	check(call.ok && call.result.as_number() == 42, "a host adds a method of its own");
	call = ask(*kit, "host.double");
	check(!call.ok && call.error == "No number to double." && call.kind == "anki", "which may fail with its reason");
	kit->set_method("host.double", nullptr);
	check(!ask(*kit, "host.double", R"({"n":21})").ok, "and is taken away again");

	// ---- what is kept
	kit.reset();
	kit = MiningKit::open(options, problem);
	check(kit != nullptr, "the kit starts again");
	if (!kit) return;
	call = ask(*kit, "settings.get");
	check(call.ok && call.result.boolean("direct") && call.result["anki"].str("address") == "http://anki.example.org:8765" &&
	          call.result["anki"].str("noteType") == "Lapis" && call.result["anki"]["fields"].size() == kLapisFields.size() &&
	          call.result["anki"]["fields"][7].str("field") == "Sentence" && call.result["anki"]["fields"][7].str("role") == "sentence" &&
	          call.result["anki"]["tags"].size() == 2 && call.result["popup"].number("textSize") == 115,
	      "the choices are kept, the fields in their order");
	check(!call.result.boolean("ankiKeySet", true) && !call.result.boolean("audioSet", true), "the key and the audio source are the host's to keep");
	check(ask(*kit, "dict.list").result["dictionaries"].size() == 1 && ask(*kit, "lookup", R"({"text":"話をしよう"})").result["results"].size() == 1,
	      "and so are the dictionaries");
	call = ask(*kit, "dict.remove", "{\"id\":" + json::quote(ask(*kit, "dict.list").result["dictionaries"][0].str("id")) + "}");
	check(call.ok && call.result["dictionaries"].size() == 0 && !ask(*kit, "dict.remove", R"({"id":"gone"})").ok, "a dictionary is removed");

	// A suggested dictionary is known by its title, whatever its revision.
	std::string suggested_zip = scratch + "/suggested.zip";
	testing_zip::write_zip(suggested_zip, {{"index.json", R"({"title":"Jitendex.org [2026-11-23]","revision":"2026.11.23.0","format":3})"},
	                                       {"term_bank_1.json", R"([["話","はなし","","n",5,["talk"],1,""]])"}});
	call = ask(*kit, "dict.import", "{\"path\":" + json::quote(suggested_zip) + ",\"name\":\"suggested.zip\"}");
	kit->library().wait_import();
	call = ask(*kit, "dict.list");
	check(call.ok && call.result["dictionaries"].size() == 1 && call.result["suggested"][0].boolean("installed") &&
	          !call.result["suggested"][1].boolean("installed", true) && call.result["import"].str("name").empty(),
	      "a suggested dictionary that is there is marked so");
	ask(*kit, "dict.remove", "{\"id\":" + json::quote(call.result["dictionaries"][0].str("id")) + "}");
}

}  // namespace

int main(int argc, char **argv) {
	std::string root = argc > 1 ? argv[1] : ".";
	char name[] = "/tmp/kit_test_XXXXXX";
	if (!mkdtemp(name)) {
		fprintf(stderr, "no folder for the test\n");
		return 2;
	}
	scratch = name;
	test_anki_connect();
	test_fields();
	test_word_audio();
	test_card_note();
	test_kit(root);
	std::string remove = "rm -rf '" + scratch + "'";
	if (system(remove.c_str()) != 0) fprintf(stderr, "could not remove %s\n", scratch.c_str());
	printf("%s: %d checks\n", ok ? "ok" : "FAILED", checks);
	return ok ? 0 : 1;
}
