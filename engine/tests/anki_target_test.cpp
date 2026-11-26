// The Anki app on the same device as a place for the mining kit's cards
// (docs/MINING.md): AnkiMobile and AnkiDroid behind the kit's CardTarget, and
// the kit's calls as the settings and the popup's page make them. Both apps
// are made up here: an AnkiMobile from what its manual says it does
// (pretend_ankimobile.h) and an AnkiDroid that answers as AnkiDroid.kt does.
// Needs no game files, no Apple or Android system and no Anki:
//
//   c++ -std=c++17 -Isrc -Ithird_party tests/anki_target_test.cpp src/dict/*.cpp src/mining/anki_*.cpp
//       src/mining/card_note.cpp src/mining/word_audio.cpp src/mining/mining_kit.cpp src/util/json.cpp
//       src/util/log.cpp src/api/ore_kit.cpp host/linux/linux_kit_host.cpp -lsqlite3 -lz -lpthread
//       -o /tmp/anki_target_test && /tmp/anki_target_test
//
// It reads the rules (resources/mining/transforms.json) from the repository it
// is run in, or from the folder given as its first argument (the engine's).
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "dict/kit_config.h"
#include "mining/anki_app.h"
#include "mining/anki_app_target.h"
#include "pretend_ankimobile.h"
#include "util/json.h"

#if ORE_MINING_KIT
#include "api/ore_kit.h"
#include "mining/mining_kit.h"
#endif

using namespace ore;
using namespace ore_test;

namespace {

bool ok = true;
int checks = 0;
std::string scratch;

void check(bool value, const std::string &message) {
	++checks;
	if (!value) {
		fprintf(stderr, "FAIL: %s\n", message.c_str());
		ok = false;
	}
}

bool has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

std::string read_file(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

bool exists(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

int files_in(const std::string &folder) {
	int count = 0;
	if (DIR *dir = opendir(folder.c_str())) {
		while (dirent *entry = readdir(dir))
			if (entry->d_name[0] != '.') ++count;
		closedir(dir);
	}
	return count;
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

// What AnkiMobile leaves on the clipboard when it has the decks Default and
// Mining and the note types Basic and Lapis.
std::string lapis_lists() {
	std::string fields;
	for (const std::string &name : kLapisFields) fields += std::string(fields.empty() ? "" : ",") + "{\"name\":\"" + name + "\"}";
	return "{\"profiles\":[{\"name\":\"User 1\"}],\"decks\":[{\"name\":\"Default\"},{\"name\":\"Mining\"}],"
	       "\"notetypes\":[{\"name\":\"Basic\",\"fields\":[{\"name\":\"Front\"},{\"name\":\"Back\"}]},{\"name\":\"Lapis\",\"fields\":[" +
	       fields + "]}]}";
}

// ---------------------------------------------------------------- a made-up AnkiDroid

// Answers as AnkiDroid.kt does: the same requests, the same answers, the same
// errors and codes. Its files wait in a folder and are gone once it took them.
struct PretendAnkiDroid {
	struct Note {
		std::string id, note_type, deck, tags;
		std::vector<std::string> values;
	};
	std::mutex mutex;
	bool installed = true, allowed = true;
	bool takes_files = true;
	bool stumbles_over_newest = false;
	std::string media_folder;
	std::vector<std::string> decks{"Default", "Mining"};
	std::vector<std::pair<std::string, std::vector<std::string>>> types{{"Basic", {"Front", "Back"}}, {"Lapis", kLapisFields}};
	std::vector<Note> notes;
	std::vector<std::string> asked;  // the methods, in the order they came

	static std::string problem(const std::string &text, const std::string &code) {
		json::Value answer = json::Value::object();
		answer.set("error", text);
		answer.set("code", code);
		return json::write(answer);
	}

	const std::vector<std::string> *fields_of(const std::string &type) const {
		for (const auto &entry : types)
			if (entry.first == type) return &entry.second;
		return nullptr;
	}

	bool is_known(const std::string &type, const std::string &field, const std::string &value) const {
		const std::vector<std::string> *fields = fields_of(type);
		if (!fields || value.empty()) return false;
		for (size_t i = 0; i < fields->size(); ++i)
			if ((*fields)[i] == field)
				for (const Note &note : notes)
					if (note.note_type == type && note.values[i] == value) return true;
		return false;
	}

	std::string call(const std::string &method, const std::string &text) {
		std::lock_guard<std::mutex> lock(mutex);
		asked.push_back(method);
		json::Value ask = parsed(text);
		if (method == "status") {
			json::Value answer = json::Value::object();
			answer.set("installed", installed);
			answer.set("allowed", installed && allowed);
			answer.set("mediaFolder", media_folder);
			return json::write(answer);
		}
		if (!installed) return problem("AnkiDroid is not on this device.", "missing");
		if (!allowed) return problem("AnkiDroid has not allowed this app to add cards yet. Allow it in the launcher.", "permission");
		if (method == "info") {
			json::Value answer = json::Value::object();
			json::Value &deck_names = answer.set("decks", json::Value::array());
			for (const std::string &deck : decks) deck_names.push(deck);
			json::Value &type_list = answer.set("noteTypes", json::Value::array());
			for (const auto &type : types) {
				json::Value entry = json::Value::object();
				entry.set("name", type.first);
				json::Value &names = entry.set("fields", json::Value::array());
				for (const std::string &field : type.second) names.push(field);
				type_list.push(std::move(entry));
			}
			return json::write(answer);
		}
		if (method == "known") {
			json::Value answer = json::Value::object();
			answer.set("known", is_known(ask.str("noteType"), ask.str("field"), ask.str("value")));
			return json::write(answer);
		}
		if (method == "deckOfNewest") {
			if (stumbles_over_newest) return problem("AnkiDroid did not answer. Open AnkiDroid once, then try again.", "failed");
			// The newest note of the type; a deck that is not in the list is a filtered one.
			std::string deck;
			for (const Note &note : notes)
				if (note.note_type == ask.str("noteType")) deck = note.deck;
			json::Value answer = json::Value::object();
			answer.set("deck", std::find(decks.begin(), decks.end(), deck) != decks.end() ? deck : std::string());
			return json::write(answer);
		}
		if (method == "add") {
			std::string type = ask.str("noteType"), deck = ask.str("deck");
			const std::vector<std::string> *fields = fields_of(type);
			if (!fields) return problem("AnkiDroid has no note type \"" + type + "\" any more. Fetch the lists again in the launcher.", "noteType");
			if (std::find(decks.begin(), decks.end(), deck) == decks.end())
				return problem("AnkiDroid has no deck \"" + deck + "\" any more. Fetch the lists again in the launcher.", "deck");
			Note note;
			note.note_type = type;
			note.deck = deck;
			for (const std::string &field : *fields) note.values.push_back(ask["fields"].str(field));
			if (note.values[0].empty()) return problem("The card's first field, \"" + (*fields)[0] + "\", is empty.", "empty");
			if (!ask.boolean("allowDuplicate") && is_known(type, (*fields)[0], note.values[0]))
				return problem("This word is in AnkiDroid already.", "duplicate");
			json::Value missing = json::Value::array();
			for (const json::Value &file : ask["media"].items()) {
				std::string name = file.str("name"), path = media_folder + "/" + name;
				bool there = exists(path);
				::remove(path.c_str());
				if (!there || !takes_files) {
					missing.push(name);
					continue;
				}
				size_t dot = name.rfind('.');
				std::string stored = name.substr(0, dot) + "_1700000000" + name.substr(dot);
				std::string reference = file.boolean("sound") ? "[sound:" + stored + "]" : "<img src=\"" + stored + "\">";
				for (const json::Value &into : file["fields"].items())
					for (size_t i = 0; i < fields->size(); ++i)
						if ((*fields)[i] == into.as_string()) note.values[i] += reference;
			}
			for (const json::Value &tag : ask["tags"].items()) note.tags += (note.tags.empty() ? "" : " ") + tag.as_string();
			note.id = std::to_string(1700000000000 + int64_t(notes.size()) + 1);
			notes.push_back(note);
			json::Value answer = json::Value::object();
			answer.set("id", note.id);
			answer.set("missing", std::move(missing));
			return json::write(answer);
		}
		return problem("AnkiDroid cannot do \"" + method + "\".", "failed");
	}

	AnkiAppHost host() {
		AnkiAppHost h;
		h.ankidroid = [this](const std::string &method, const std::string &json) { return call(method, json); };
		return h;
	}

	// The value of a field of a note, by the field's name.
	std::string value(size_t note, const std::string &field) {
		std::lock_guard<std::mutex> lock(mutex);
		if (note >= notes.size()) return std::string();
		const std::vector<std::string> *fields = fields_of(notes[note].note_type);
		for (size_t i = 0; fields && i < fields->size(); ++i)
			if ((*fields)[i] == field) return notes[note].values[i];
		return std::string();
	}
};

// A card of 食べる as the kit hands it over: the text in its fields, the fields of the files empty.
CardNote taberu_note() {
	CardNote note;
	note.deck = "Mining";
	note.note_type = "Lapis";
	for (const std::string &field : kLapisFields) note.fields.emplace_back(field, std::string());
	note.fields[0].second = "食べる";
	note.fields[7].second = "ご飯を<b>食べる</b>。";
	note.tags = {"vn-miner", "oretsuba"};
	note.media.push_back({"picture", "oretsuba_1a2b_3c4d.jpg", std::string(50000, 'p'), "image/jpeg", {"Picture"}});
	note.media.push_back({"sound", "oretsuba_1a2b_5e6f.m4a", std::string(30000, 's'), "audio/mp4", {"SentenceAudio"}});
	note.media.push_back({"wordAudio", "vn-miner_word_77aa.mp3", std::string(9000, 'w'), "audio/mpeg", {"ExpressionAudio"}});
	return note;
}

void test_ankidroid() {
	AnkiProblem problem;
	CardPlaces places;
	std::vector<std::string> names;
	std::vector<Known> answers;
	AddOutcome outcome;

	{
		AnkiDroidTarget none{AnkiAppHost()};
		check(!none.possible() && !none.places(places, problem) && problem.unreachable && !problem.message.empty(),
		      "a host without AnkiDroid says that it cannot be reached");
	}

	PretendAnkiDroid droid;
	droid.media_folder = scratch + "/anki-media";
	mkdir(droid.media_folder.c_str(), 0700);
	AnkiDroidTarget target(droid.host());
	check(target.possible(), "a host with AnkiDroid's requests can try");

	droid.installed = false;
	bool installed = true, allowed = true;
	check(target.status(installed, allowed, problem) && !installed && !allowed, "an AnkiDroid that is not on the device is told so");
	check(!target.places(places, problem) && problem.unreachable && has(problem.message, "not on this device"), "and its lists fail with those words");
	droid.installed = true;
	droid.allowed = false;
	check(target.status(installed, allowed, problem) && installed && !allowed, "one that has not let this app in is told so");
	check(!target.places(places, problem) && problem.unreachable && has(problem.message, "not allowed"), "and its lists fail with those words");
	check(!target.known({taberu_note()}, answers, problem) && problem.unreachable, "nor does it say what it has");
	check(!target.add(taberu_note(), outcome, problem) && problem.unreachable && droid.notes.empty(), "nor take a card");
	check(files_in(droid.media_folder) == 0, "and a card it did not take leaves no file behind");
	droid.allowed = true;

	check(target.places(places, problem) && places.decks == std::vector<std::string>({"Default", "Mining"}) &&
	          places.note_types == std::vector<std::string>({"Basic", "Lapis"}),
	      "the decks and note types are AnkiDroid's");
	check(target.field_names("Lapis", names, problem) && names == kLapisFields, "and so are a note type's fields, in their order");
	check(!target.field_names("Gone", names, problem) && !problem.unreachable && has(problem.message, "Gone"), "a note type it does not have is said so");
	std::string deck = "x";
	check(target.deck_of_newest("Lapis", places.decks, deck, problem) && deck.empty(), "a note type without a note names no deck");

	check(target.known({taberu_note()}, answers, problem) && answers == std::vector<Known>({Known::No}), "a new word is not known");
	droid.asked.clear();
	check(target.add(taberu_note(), outcome, problem) && !outcome.handed_over && outcome.note_id == 1700000000001.0, "a card is added at once, and its number comes back");
	check(droid.asked == std::vector<std::string>({"status", "add"}), "with one question for the folder and one request for the card");
	check(droid.notes.size() == 1 && droid.notes[0].deck == "Mining" && droid.notes[0].tags == "vn-miner oretsuba" &&
	          droid.value(0, "Expression") == "食べる" && droid.value(0, "Sentence") == "ご飯を<b>食べる</b>。",
	      "deck, tags and the fields of text arrive as they are");
	check(droid.value(0, "Picture") == "<img src=\"oretsuba_1a2b_3c4d_1700000000.jpg\">" &&
	          droid.value(0, "SentenceAudio") == "[sound:oretsuba_1a2b_5e6f_1700000000.m4a]" &&
	          droid.value(0, "ExpressionAudio") == "[sound:vn-miner_word_77aa_1700000000.mp3]",
	      "each file is in its field under the name AnkiDroid stored it by");
	check(files_in(droid.media_folder) == 0, "no file is left waiting");
	check(target.known({taberu_note(), taberu_note()}, answers, problem) && answers == std::vector<Known>({Known::Yes, Known::Yes}), "the word is known now");

	check(!target.add(taberu_note(), outcome, problem) && !problem.unreachable && problem.said == "duplicate" && droid.notes.size() == 1,
	      "a second card of the word is refused, in the word the kit knows a duplicate by");
	CardNote again = taberu_note();
	again.allow_duplicate = true;
	check(target.add(again, outcome, problem) && droid.notes.size() == 2, "unless duplicates are asked for");

	deck = "x";
	check(target.deck_of_newest("Lapis", places.decks, deck, problem) && deck == "Mining", "a note type with cards names the deck its newest is in");
	check(target.deck_of_newest("Lapis", {"Default"}, deck, problem) && deck.empty(), "unless that deck is not among the decks to choose from");
	droid.stumbles_over_newest = true;
	deck = "x";
	check(target.deck_of_newest("Lapis", places.decks, deck, problem) && deck.empty() && problem.message.empty(),
	      "an AnkiDroid that stumbles over this question leaves the deck to be chosen");
	droid.stumbles_over_newest = false;
	droid.allowed = false;
	check(!target.deck_of_newest("Lapis", places.decks, deck, problem) && problem.unreachable, "one that does not let the app in fails here too");
	droid.allowed = true;

	CardNote empty = taberu_note();
	empty.fields[0].second.clear();
	check(!target.add(empty, outcome, problem) && problem.said == "empty" && !problem.unreachable, "a card without its first field is refused as empty");
	check(target.known({empty}, answers, problem) && answers == std::vector<Known>({Known::No}), "and is not known");
	CardNote nowhere = taberu_note();
	nowhere.deck = "Gone";
	nowhere.fields[0].second = "飲む";
	check(!target.add(nowhere, outcome, problem) && !problem.unreachable && has(problem.message, "\"Gone\""), "a deck AnkiDroid does not have is said so");

	CardNote plain = taberu_note();
	plain.fields[0].second = "見る";
	plain.media.clear();
	droid.asked.clear();
	check(target.add(plain, outcome, problem) && droid.asked == std::vector<std::string>({"add"}) && droid.value(droid.notes.size() - 1, "Picture").empty(),
	      "a card without files asks for no folder");

	CardNote odd = taberu_note();
	odd.fields[0].second = "聞く";
	odd.media.resize(1);
	odd.media[0].name = "../up/shot one.jpg";
	check(target.add(odd, outcome, problem) && droid.value(droid.notes.size() - 1, "Picture") == "<img src=\"shot_one_1700000000.jpg\">" &&
	          !exists(scratch + "/up"),
	      "a file's name cannot leave the folder");

	droid.takes_files = false;
	CardNote refused = taberu_note();
	refused.fields[0].second = "話す";
	check(target.add(refused, outcome, problem) && droid.value(droid.notes.size() - 1, "Picture").empty() && files_in(droid.media_folder) == 0,
	      "a card whose files AnkiDroid does not take is still added, without them");
}

// ---------------------------------------------------------------- AnkiMobile

AnkiMobile::Timing quick() {
	AnkiMobile::Timing timing;
	timing.confirm_ms = 150;
	timing.answer_tries = 4;
	timing.answer_wait_ms = 20;
	return timing;
}

// The value a link gives a field.
std::string field_of(const std::string &link, const std::string &field) {
	std::string value;
	link_query_value(link, "fld" + field, value);
	return value;
}

void test_ankimobile() {
	AnkiProblem problem;
	CardPlaces places;
	std::vector<std::string> names;
	std::vector<Known> answers;
	AddOutcome outcome;
	std::string lists_file = scratch + "/ankimobile.json";

	{
		Told told;
		AnkiMobileTarget none(AnkiAppHost(), "oretsuba", scratch + "/none.json", told.tell(), quick());
		check(!none.possible() && !none.fetch(problem) && problem.unreachable, "a host that opens no links cannot fetch AnkiMobile's lists");
		check(!none.add(taberu_note(), outcome, problem) && problem.unreachable && !problem.message.empty(), "nor hand a card over");
	}

	PretendAnkiMobile anki;
	anki.lists = lapis_lists();
	Told told;
	{
		AnkiMobileTarget target(anki.host(), "oretsuba", lists_file, told.tell(), quick());
		check(target.possible(), "a host that opens links can try");
		check(!target.places(places, problem) && !problem.unreachable && problem.message == "Fetch the decks and note types from AnkiMobile first.",
		      "before AnkiMobile was asked, its lists are asked for");
		check(!target.field_names("Lapis", names, problem) && !problem.message.empty(), "and so are a note type's fields");
		check(!target.add(taberu_note(), outcome, problem) && !problem.unreachable && has(problem.message, "Fetch") && anki.links.empty(),
		      "and no card is handed over, which AnkiMobile would refuse without a word");

		check(target.fetch(problem) && told.wait(1), "fetching opens AnkiMobile, and an answer comes");
		json::Value event = parsed(told.at(0).second);
		check(told.at(0).first == "anki.lists" && event.boolean("ok") && event.str("message") == "AnkiMobile has 2 decks and 2 note types.",
		      "the answer says in a sentence what AnkiMobile has");
		check(target.places(places, problem) && places.decks == std::vector<std::string>({"Default", "Mining"}) &&
		          places.note_types == std::vector<std::string>({"Basic", "Lapis"}),
		      "the decks and note types are AnkiMobile's from then on");
		check(target.field_names("Lapis", names, problem) && names == kLapisFields, "and a note type's fields");
		check(!target.field_names("Gone", names, problem) && has(problem.message, "Gone") && has(problem.message, "again"),
		      "a note type it did not name is said so, with what to do");
		check(target.known({taberu_note()}, answers, problem) && answers == std::vector<Known>({Known::CannotSay}),
		      "whether AnkiMobile has a word cannot be asked");
		anki.finish();
	}
	check(has(read_file(lists_file), "\"Mining\""), "the lists are kept in a file");

	AnkiMobileTarget target(anki.host(), "oretsuba", lists_file, told.tell(), quick());
	check(target.places(places, problem) && places.decks.size() == 2 && anki.links.size() == 1,
	      "the next time the app runs the lists are there without asking AnkiMobile again");
	CardNote other = taberu_note();
	other.note_type = "Chosen for another Anki";
	check(!target.add(other, outcome, problem) && !problem.unreachable && has(problem.message, "Chosen for another Anki") && anki.links.size() == 1,
	      "nor is a card of a note type AnkiMobile did not name");

	// ---- a card that arrives whole
	size_t before = told.count();
	check(target.add(taberu_note(), outcome, problem) && outcome.handed_over && outcome.note_id == 0, "a card is handed over");
	check(told.wait(before + 1), "and AnkiMobile's answer is told later");
	json::Value event = parsed(told.at(before).second);
	check(told.at(before).first == "card.added" && event.str("message") == "AnkiMobile added the card." && event["missing"].is_array() &&
	          event["missing"].size() == 0 && event.str("card").size() == 16,
	      "a card that arrived whole is said so");
	anki.finish();
	std::string link = anki.links.back();
	std::string picture = field_of(link, "Picture"), voice = field_of(link, "SentenceAudio"), word = field_of(link, "ExpressionAudio");
	check(has(link, "anki://x-callback-url/addnote?type=Lapis&deck=Mining&") && field_of(link, "Expression") == "食べる" &&
	          field_of(link, "Sentence") == "ご飯を<b>食べる</b>。" && has(link, "&tags=vn-miner%20oretsuba&"),
	      "the link names note type, deck, tags and the fields of text");
	check(picture.compare(0, 17, "http://localhost:") == 0 && has(picture, "/oretsuba_1a2b_3c4d.jpg") && has(voice, "/oretsuba_1a2b_5e6f.m4a") &&
	          has(word, "/vn-miner_word_77aa.mp3"),
	      "each file's field holds the link AnkiMobile fetches it from");
	check(anki.fetched.size() == 3 && anki.fetched[0].second + anki.fetched[1].second + anki.fetched[2].second == 89000, "AnkiMobile fetched all three, every byte");
	check(!has(link, "dupes=1"), "duplicates are not asked for");

	// ---- a card that goes without some of its files
	anki.fetches = false;
	before = told.count();
	check(target.add(taberu_note(), outcome, problem) && told.wait(before + 1), "a card whose files AnkiMobile does not fetch still comes back");
	event = parsed(told.at(before).second);
	check(told.at(before).first == "card.added" &&
	          event.str("message") == "AnkiMobile added the card, but without the picture, the line's voice and the word's sound." &&
	          event["missing"].size() == 3 && event["missing"][0].as_string() == "picture" && event["missing"][2].as_string() == "wordAudio",
	      "and the page is told what it went without, by what the files are");
	anki.finish();
	anki.fetches = true;

	CardNote taken = taberu_note();
	taken.fields[10].second = "something written";  // Picture
	taken.media[1].name = "voice.xyz";               // an ending AnkiMobile does not fetch
	taken.media.resize(2);
	before = told.count();
	check(target.add(taken, outcome, problem) && told.wait(before + 1), "a file whose field is taken, or whose ending AnkiMobile does not know, is left out");
	event = parsed(told.at(before).second);
	check(event.str("message") == "AnkiMobile added the card, but without the picture and the line's voice." && event["missing"].size() == 2,
	      "and that too is told once the card is there");
	anki.finish();
	check(field_of(anki.links.back(), "Picture") == "something written" && field_of(anki.links.back(), "SentenceAudio").empty(),
	      "the fields stay as the kit made them");

	// ---- cards that do not come back
	anki.comes_back = false;
	before = told.count();
	check(target.add(taberu_note(), outcome, problem) && outcome.handed_over && told.wait(before + 1), "a card AnkiMobile does not confirm is told too");
	event = parsed(told.at(before).second);
	check(told.at(before).first == "card.unconfirmed" && has(event.str("message"), "did not say that it added the card") && event.str("card").size() == 16,
	      "as not confirmed, with what the player can do");
	anki.finish();
	anki.comes_back = true;
	anki.installed = false;
	before = told.count();
	check(target.add(taberu_note(), outcome, problem) && told.wait(before + 1) && told.at(before).first == "card.unconfirmed" &&
	          has(parsed(told.at(before).second).str("message"), "did not open"),
	      "without AnkiMobile on the device the card is given up at once, and says why");
	anki.installed = true;

	// ---- lists that do not arrive
	anki.lists = "{\"decks\":[]}";
	before = told.count();
	check(target.fetch(problem) && told.wait(before + 1), "a fetch that brings no decks answers too");
	event = parsed(told.at(before).second);
	check(told.at(before).first == "anki.lists" && !event.boolean("ok", true) && !event.str("message").empty(), "with the reason");
	check(target.places(places, problem) && places.decks.size() == 2, "and the lists from before stay");
	anki.finish();
}

// ---------------------------------------------------------------- the kit

#if ORE_MINING_KIT

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
    R"({"word":{"expression":"食べる","reading":"たべる","furigana":"食[た]べる","meaning":"<ol><li>to eat</li></ol>","frequency":"512","pitch":"2","pitchCategories":"kifuku","selection":"","at":{"start":3,"length":3}}})";

void test_kit(const std::string &root) {
	std::string folder = scratch + "/kit", problem;
	MiningKit::Options options;
	options.folder = folder;
	options.resources = root + "/resources/mining";
	options.encode_voice = [](const std::string &file, const std::string &, std::string &out, std::string &type, std::string &out_ending) {
		out = "m4a of " + file;
		type = "audio/mp4";
		out_ending = "m4a";
		return true;
	};
	KitLine line;
	line.sentence = "ご飯を食べる。";
	line.voice = "OggS voice";
	line.voice_ending = "ogg";
	line.picture = std::string(4000, 'j');

	{
		auto kit = MiningKit::open(options, problem);
		check(kit != nullptr, "the kit starts: " + problem);
		if (!kit) return;
		add_anki_apps(*kit, AnkiAppHost(), "oretsuba", quick());
		Call call = ask(*kit, "anki.targets");
		check(call.ok && call.result.size() == 1 && call.result[0].str("name") == "connect" && call.result[0].str("label") == "AnkiConnect",
		      "a device without an Anki app offers AnkiConnect alone");
		check(!ask(*kit, "ankimobile.fetch").ok && !ask(*kit, "ankidroid.status").ok, "and has no calls for the apps");
		ask(*kit, "settings.set", R"({"anki":{"target":"ankimobile"}})");
		check(!ask(*kit, "anki.check").ok && !ask(*kit, "page.init").result["cards"].boolean("possible", true), "a place that is not there makes no cards");
	}

	PretendAnkiMobile anki;
	anki.lists = lapis_lists();
	PretendAnkiDroid droid;
	droid.media_folder = scratch + "/kit-media";
	mkdir(droid.media_folder.c_str(), 0700);
	AnkiAppHost host = anki.host();
	host.ankidroid = droid.host().ankidroid;

	auto kit = MiningKit::open(options, problem);
	check(kit != nullptr, "the kit starts again");
	if (!kit) return;
	Told told;
	kit->set_events(told.tell());
	add_anki_apps(*kit, host, "oretsuba", quick());

	Call call = ask(*kit, "anki.targets");
	check(call.ok && call.result.size() == 3 && call.result[1].str("name") == "ankimobile" && call.result[1].str("label") == "AnkiMobile" &&
	          call.result[2].str("name") == "ankidroid" && !call.result[2].str("detail").empty(),
	      "the places a device has are listed for the settings, AnkiConnect first");
	check(ask(*kit, "settings.get").result["anki"].str("target") == "ankimobile", "the place chosen before is kept");

	// ---- AnkiMobile
	call = ask(*kit, "anki.check");
	check(!call.ok && call.kind == "anki" && call.error == "Fetch the decks and note types from AnkiMobile first.", "before the first fetch the settings are told to fetch");
	call = ask(*kit, "ankimobile.fetch");
	check(call.ok && call.result.boolean("asked") && told.wait(1), "fetching answers at once, and the lists come as an event");
	check(told.at(0).first == "anki.lists" && parsed(told.at(0).second).boolean("ok") && exists(folder + "/ankimobile.json"),
	      "which says that they arrived; they are kept beside the settings");
	anki.finish();
	call = ask(*kit, "anki.check", R"({"adopt":true})");
	check(call.ok && call.result["decks"].size() == 2 && call.result["noteTypes"].size() == 2 && call.result["lapis"].str("noteType") == "Lapis",
	      "then AnkiMobile is asked like any Anki, and Lapis is found in it");
	check(call.result["settings"]["anki"].str("noteType") == "Lapis" && call.result["settings"]["anki"]["fields"].size() == kLapisFields.size(),
	      "and taken with its fields, as with an AnkiConnect");
	check(ask(*kit, "anki.choose", R"({"deck":"Mining"})").ok && ask(*kit, "page.init").result["cards"].boolean("possible"), "with a deck chosen, cards can be made");

	kit->set_line(line);
	call = ask(*kit, "card.known", "{\"words\":[" + json::write(parsed(kTaberu)["word"]) + "]}");
	check(call.ok && call.result["known"].size() == 1 && !call.result["known"][0].as_bool(true), "AnkiMobile cannot say that it has a word, so none is marked");
	size_t before = told.count();
	call = ask(*kit, "card.add", kTaberu);
	check(call.ok && call.result.boolean("handedOver") && has(call.result.str("message"), "Handed"), "a card goes to AnkiMobile as handed over");
	check(told.wait(before + 1) && told.at(before).first == "card.added" && parsed(told.at(before).second).str("message") == "AnkiMobile added the card.",
	      "and the page is told when AnkiMobile has added it");
	anki.finish();
	std::string link = anki.links.back();
	check(has(link, "addnote?type=Lapis&deck=Mining&") && field_of(link, "Expression") == "食べる" && has(field_of(link, "Sentence"), "<b>食べる</b>"),
	      "the card is the kit's: the word and its sentence");
	check(has(field_of(link, "Picture"), ".jpg") && has(field_of(link, "SentenceAudio"), ".m4a") && anki.fetched.size() == 2 &&
	          anki.fetched[0].second + anki.fetched[1].second == 4000 + std::string("m4a of OggS voice").size(),
	      "its picture and its voice, made a file AnkiMobile plays, were fetched");

	// ---- AnkiDroid
	check(ask(*kit, "settings.set", R"({"anki":{"target":"ankidroid"}})").ok, "the other app is chosen");
	call = ask(*kit, "ankidroid.status");
	check(call.ok && call.result.boolean("installed") && call.result.boolean("allowed"), "the settings can ask whether AnkiDroid lets the app in");
	droid.allowed = false;
	call = ask(*kit, "ankidroid.status");
	check(call.ok && call.result.boolean("installed") && !call.result.boolean("allowed", true), "and whether it does not");
	call = ask(*kit, "anki.check");
	check(!call.ok && call.kind == "unreachable" && has(call.error, "not allowed"), "an AnkiDroid that does not let the app in is said so");
	droid.allowed = true;
	call = ask(*kit, "anki.check", R"({"adopt":true})");
	check(call.ok && call.result["decks"].size() == 2 && call.result["lapis"].str("noteType") == "Lapis", "AnkiDroid is asked like any Anki");
	call = ask(*kit, "card.known", "{\"words\":[" + json::write(parsed(kTaberu)["word"]) + "]}");
	check(call.ok && !call.result["known"][0].as_bool(true), "a new word is not known to it");
	call = ask(*kit, "card.add", kTaberu);
	check(call.ok && !call.result.boolean("handedOver", true) && call.result.number("noteId") == 1700000000001.0 && has(call.result.str("message"), "Made a card"),
	      "a card goes to AnkiDroid at once");
	check(droid.notes.size() == 1 && droid.value(0, "Expression") == "食べる" && has(droid.value(0, "Picture"), "<img src=\"oretsuba_") &&
	          has(droid.value(0, "SentenceAudio"), "[sound:oretsuba_") && has(droid.value(0, "SentenceAudio"), ".m4a]") &&
	          files_in(droid.media_folder) == 0,
	      "with its picture and its voice in their fields");
	call = ask(*kit, "card.known", "{\"words\":[" + json::write(parsed(kTaberu)["word"]) + "]}");
	check(call.ok && call.result["known"][0].as_bool(), "the word is known from then on");
	call = ask(*kit, "card.add", kTaberu);
	check(!call.ok && call.kind == "duplicate" && droid.notes.size() == 1, "and a second card of it is refused as a duplicate");
	// A player's AnkiDroid has more decks than one: Lapis then comes with the deck its newest card is in.
	droid.decks = {"Default", "Kaishi 1.5k", "Mining"};
	check(ask(*kit, "anki.choose", R"({"deck":"","noteType":""})").ok, "with neither deck nor note type chosen");
	call = ask(*kit, "anki.check", R"({"adopt":true})");
	check(call.ok && call.result["settings"]["anki"].str("noteType") == "Lapis" && call.result["settings"]["anki"].str("deck") == "Mining" &&
	          ask(*kit, "page.init").result["cards"].boolean("possible"),
	      "Lapis is taken together with the deck its newest card is in, and cards can be made");
	droid.notes[0].deck = "A filtered deck";
	ask(*kit, "anki.choose", R"({"deck":"","noteType":""})");
	call = ask(*kit, "anki.check", R"({"adopt":true})");
	check(call.ok && call.result["settings"]["anki"].str("noteType") == "Lapis" && call.result["settings"]["anki"].str("deck").empty(),
	      "with that card in no deck of the list, the deck is left to the player");
	droid.notes[0].deck = "Mining";
	ask(*kit, "anki.choose", R"({"deck":"Mining","noteType":"Lapis"})");

	// ---- the kit goes while a card is on its way
	ask(*kit, "settings.set", R"({"anki":{"target":"ankimobile"}})");
	anki.comes_back = false;
	before = told.count();
	check(ask(*kit, "card.add", kTaberu).ok, "a card is on its way");
	kit.reset();
	anki.finish();
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	check(told.count() == before, "nothing is told once the kit is gone");
	AppReturns::shared().in_front();
	AppReturns::shared().opened_with("oretsuba://anki/added?card=0123456789abcdef");
}

// The kit as the apps open it (api/ore_kit.h): that is where it gets the Anki
// apps of its host. A test host has none, so AnkiConnect stands alone.
void test_opened_by_the_app(const std::string &root) {
	char problem[256] = "";
	OreKit *kit = ore_kit_open((scratch + "/app-kit").c_str(), (root + "/resources/mining").c_str(), problem, sizeof problem);
	check(kit != nullptr, std::string("the kit opens as the app opens it: ") + problem);
	if (!kit) return;
	char *answer = ore_kit_call(kit, "anki.targets", nullptr);
	json::Value read = parsed(answer ? answer : "");
	ore_kit_free(answer);
	check(read.boolean("ok") && read["result"].size() >= 1 && read["result"][0].str("name") == "connect",
	      "and answers which places for cards this device has");
	ore_kit_close(kit);
}

#endif  // ORE_MINING_KIT

}  // namespace

int main(int argc, char **argv) {
	std::string root = argc > 1 ? argv[1] : ".";
	char name[] = "/tmp/anki_target_test_XXXXXX";
	if (!mkdtemp(name)) {
		fprintf(stderr, "no folder for the test\n");
		return 2;
	}
	scratch = name;
	test_ankidroid();
	test_ankimobile();
#if ORE_MINING_KIT
	test_kit(root);
	test_opened_by_the_app(root);
#else
	printf("  (this build has no mining kit, its part is left out)\n");
#endif
	std::string remove = "rm -rf '" + scratch + "'";
	if (system(remove.c_str()) != 0) fprintf(stderr, "the test's folder %s stays\n", scratch.c_str());
	if (!ok) {
		fprintf(stderr, "anki_target_test FAILED\n");
		return 1;
	}
	printf("anki_target_test passed: %d checks\n", checks);
	return 0;
}
