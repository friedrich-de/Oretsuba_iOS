// The Anki app on the same device as a place the mining kit's cards go.
#include "mining/anki_app_target.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "util/json.h"
#include "util/log.h"

#if ORE_MINING_KIT
#include "mining/mining_kit.h"
#endif

namespace ore {

namespace {

bool read_file(const std::string &path, std::string &out) {
	std::ifstream file(path, std::ios::binary);
	if (!file) return false;
	std::stringstream text;
	text << file.rdbuf();
	out = text.str();
	return true;
}

// The whole file or none of it: written beside it first, then put in its place.
bool write_file(const std::string &path, const std::string &bytes) {
	std::string unfinished = path + ".new";
	{
		std::ofstream file(unfinished, std::ios::binary | std::ios::trunc);
		file.write(bytes.data(), std::streamsize(bytes.size()));
		if (!file) {
			::remove(unfinished.c_str());
			return false;
		}
	}
	if (::rename(unfinished.c_str(), path.c_str()) == 0) return true;
	::remove(unfinished.c_str());
	return false;
}

std::string counted(size_t count, const char *one, const char *many) {
	return std::to_string(count) + " " + (count == 1 ? one : many);
}

// A file of a card in words, by what it is.
const char *file_in_words(const std::string &role) {
	if (role == "picture") return "the picture";
	if (role == "sound") return "the line's voice";
	if (role == "wordAudio") return "the word's sound";
	return "a file";
}

// "the picture", "the picture and the line's voice", "a, b and c".
std::string listed(const std::vector<std::string> &roles) {
	std::string out;
	for (size_t i = 0; i < roles.size(); ++i) {
		if (i > 0) out += i + 1 == roles.size() ? " and " : ", ";
		out += file_in_words(roles[i]);
	}
	return out;
}

void add_once(std::vector<std::string> &list, const std::string &item) {
	if (std::find(list.begin(), list.end(), item) == list.end()) list.push_back(item);
}

AnkiLinkNote link_note(const CardNote &note) {
	AnkiLinkNote link;
	link.deck = note.deck;
	link.note_type = note.note_type;
	link.fields = note.fields;
	link.tags = note.tags;
	link.allow_duplicate = note.allow_duplicate;
	return link;
}

}  // namespace

// ---------------------------------------------------------------- AnkiMobile

struct AnkiMobileTarget::State {
	std::string lists_file;
	Tell tell;

	std::mutex mutex;
	bool read = false;  // the file was looked at
	bool have = false;  // and held lists, or lists have arrived since
	AnkiAppInfo info;

	// What each file of the card on its way is, by its name. Names are made of
	// what the files are, so a name that stays from the card before does no harm.
	std::map<std::string, std::string> role_of;

	// The lists as they are known now; false when AnkiMobile was never asked.
	bool lists(AnkiAppInfo &out) {
		std::lock_guard<std::mutex> lock(mutex);
		if (!read) {
			read = true;
			std::string text;
			AnkiAppInfo kept;
			if (read_file(lists_file, text) && anki_app_info_from_text(text, kept) && !kept.decks.empty() && !kept.note_types.empty()) {
				info = std::move(kept);
				have = true;
			}
		}
		if (have) out = info;
		return have;
	}

	void say(const std::string &event, const json::Value &payload) {
		if (tell) tell(event, json::write(payload));
	}

	// What AnkiMobile (mining/anki_app.h) tells, made what the kit's page and settings are told.
	void heard(const std::string &event, const std::string &text) {
		json::Value said;
		if (!json::parse(text, said) || !said.is_object()) return;
		json::Value payload = json::Value::object();
		if (event == "anki.lists") {
			AnkiAppInfo arrived;
			bool ok = said.boolean("ok") && anki_app_info_from_text(json::write(said["lists"]), arrived) && !arrived.decks.empty() &&
			          !arrived.note_types.empty();
			std::string message;
			if (ok) {
				message = "AnkiMobile has " + counted(arrived.decks.size(), "deck", "decks") + " and " +
				          counted(arrived.note_types.size(), "note type", "note types") + ".";
				if (!write_file(lists_file, anki_app_info_text(arrived))) log_warn("mining: AnkiMobile's lists could not be kept");
				std::lock_guard<std::mutex> lock(mutex);
				info = std::move(arrived);
				read = have = true;
			} else {
				message = said.str("problem");
				if (message.empty()) message = "AnkiMobile named no deck or no note type.";
			}
			payload.set("ok", ok);
			payload.set("message", message);
		} else if (event == "card.added" || event == "card.unconfirmed") {
			std::string name = said.str("card");
			std::vector<std::string> missing;
			{
				std::lock_guard<std::mutex> lock(mutex);
				for (const char *list : {"leftOut", "notFetched"})
					for (const json::Value &file : said[list].items()) {
						auto role = role_of.find(file.as_string());
						add_once(missing, role == role_of.end() ? std::string("file") : role->second);
					}
			}
			payload.set("card", name);
			if (event == "card.added") {
				payload.set("message", missing.empty() ? std::string("AnkiMobile added the card.")
				                                       : "AnkiMobile added the card, but without " + listed(missing) + ".");
				json::Value &list = payload.set("missing", json::Value::array());
				for (const std::string &role : missing) list.push(role);
			} else {
				payload.set("message", said.str("reason"));
			}
		} else {
			return;
		}
		say(event, payload);
	}
};

AnkiMobileTarget::AnkiMobileTarget(AnkiAppHost host, std::string scheme, std::string lists_file, Tell tell, AnkiMobile::Timing timing)
    : state_(std::make_shared<State>()) {
	state_->lists_file = std::move(lists_file);
	state_->tell = std::move(tell);
	std::shared_ptr<State> state = state_;
	mobile_ = std::make_unique<AnkiMobile>(
	    std::move(host), std::move(scheme), [state](const std::string &event, const std::string &json) { state->heard(event, json); }, timing);
}

AnkiMobileTarget::~AnkiMobileTarget() = default;

bool AnkiMobileTarget::possible() const { return mobile_->possible(); }

bool AnkiMobileTarget::fetch(AnkiProblem &problem) {
	problem = AnkiProblem();
	std::string why;
	if (mobile_->ask_lists(why)) return true;
	problem.message = why;
	problem.unreachable = true;
	return false;
}

bool AnkiMobileTarget::places(CardPlaces &places, AnkiProblem &problem) {
	problem = AnkiProblem();
	AnkiAppInfo info;
	if (!state_->lists(info)) {
		problem.message = "Fetch the decks and note types from AnkiMobile first.";
		return false;
	}
	places.version.clear();
	places.decks = info.decks;
	places.note_types.clear();
	for (const AnkiAppInfo::NoteType &type : info.note_types) places.note_types.push_back(type.name);
	return true;
}

bool AnkiMobileTarget::field_names(const std::string &note_type, std::vector<std::string> &names, AnkiProblem &problem) {
	problem = AnkiProblem();
	AnkiAppInfo info;
	if (!state_->lists(info)) {
		problem.message = "Fetch the decks and note types from AnkiMobile first.";
		return false;
	}
	const AnkiAppInfo::NoteType *type = info.note_type(note_type);
	if (!type) {
		problem.message = "AnkiMobile had no note type \"" + note_type + "\" when it was last asked. Fetch its decks and note types again.";
		return false;
	}
	names = type->fields;
	return true;
}

bool AnkiMobileTarget::known(const std::vector<CardNote> &notes, std::vector<Known> &answers, AnkiProblem &problem) {
	problem = AnkiProblem();
	answers.assign(notes.size(), Known::CannotSay);
	return true;
}

bool AnkiMobileTarget::add(const CardNote &note, AddOutcome &outcome, AnkiProblem &problem) {
	problem = AnkiProblem();
	// AnkiMobile says nothing when it refuses a note, so what can be known before is said here:
	// the note type may be one that was chosen for another Anki.
	AnkiAppInfo info;
	if (possible() && !state_->lists(info)) {
		problem.message = "Fetch the decks and note types from AnkiMobile in the settings first.";
		return false;
	}
	if (possible() && !info.note_type(note.note_type)) {
		problem.message = "AnkiMobile had no note type \"" + note.note_type + "\" when it was last asked. Choose one of its note types in the settings.";
		return false;
	}
	std::vector<AnkiMobileHandover::File> files;
	std::map<std::string, std::string> role_of;
	for (const CardMedia &media : note.media) {
		AnkiMobileHandover::File file;
		file.name = anki_media_safe_name(media.name);
		file.bytes.assign(media.data.begin(), media.data.end());
		file.fields = media.fields;
		role_of[file.name] = media.role;
		files.push_back(std::move(file));
	}
	// Before the card is on its way: AnkiMobile's answer may come the moment it is.
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		state_->role_of = std::move(role_of);
	}
	std::string card, why;
	std::vector<std::string> left_out;
	if (!mobile_->add(link_note(note), std::move(files), card, left_out, why)) {
		problem.message = why;
		problem.unreachable = true;
		return false;
	}
	outcome.handed_over = true;
	outcome.note_id = 0;
	return true;
}

// ---------------------------------------------------------------- AnkiDroid

AnkiDroidTarget::AnkiDroidTarget(AnkiAppHost host) : host_(std::move(host)) {}

bool AnkiDroidTarget::possible() const { return bool(host_.ankidroid); }

bool AnkiDroidTarget::ask(const std::string &method, const std::string &json, AnkiDroidAnswer &answer, AnkiProblem &problem) {
	problem = AnkiProblem();
	if (!host_.ankidroid) {
		problem.message = "This device cannot reach AnkiDroid.";
		problem.unreachable = true;
		return false;
	}
	answer = ankidroid_answer(host_.ankidroid(method, json));
	if (answer.ok) return true;
	problem.message = answer.problem;
	// AnkiDroid saying no to this card, as opposed to not being there or not letting this app in.
	bool refused = answer.code == "duplicate" || answer.code == "empty" || answer.code == "noteType" || answer.code == "deck";
	problem.unreachable = !refused;
	// The words the kit knows these two refusals by (MiningKit::card_add).
	if (answer.code == "duplicate" || answer.code == "empty") problem.said = answer.code;
	return false;
}

bool AnkiDroidTarget::status(bool &installed, bool &allowed, AnkiProblem &problem) {
	AnkiDroidAnswer answer;
	if (!ask("status", "{}", answer, problem)) return false;
	installed = answer.installed;
	allowed = answer.allowed;
	return true;
}

bool AnkiDroidTarget::places(CardPlaces &places, AnkiProblem &problem) {
	AnkiDroidAnswer answer;
	if (!ask("info", "{}", answer, problem)) return false;
	places.version.clear();
	places.decks = answer.info.decks;
	places.note_types.clear();
	for (const AnkiAppInfo::NoteType &type : answer.info.note_types) places.note_types.push_back(type.name);
	return true;
}

bool AnkiDroidTarget::field_names(const std::string &note_type, std::vector<std::string> &names, AnkiProblem &problem) {
	AnkiDroidAnswer answer;
	if (!ask("info", "{}", answer, problem)) return false;
	const AnkiAppInfo::NoteType *type = answer.info.note_type(note_type);
	if (!type) {
		problem.message = "AnkiDroid has no note type \"" + note_type + "\".";
		return false;
	}
	names = type->fields;
	return true;
}

bool AnkiDroidTarget::known(const std::vector<CardNote> &notes, std::vector<Known> &answers, AnkiProblem &problem) {
	problem = AnkiProblem();
	answers.assign(notes.size(), Known::CannotSay);
	for (size_t i = 0; i < notes.size(); ++i) {
		const CardNote &note = notes[i];
		// Anki tells one note from another by its first field.
		if (note.fields.empty()) continue;
		AnkiDroidAnswer answer;
		if (!ask("known", ankidroid_known_request(note.note_type, note.fields[0].first, note.fields[0].second), answer, problem)) return false;
		answers[i] = answer.known ? Known::Yes : Known::No;
	}
	return true;
}

bool AnkiDroidTarget::add(const CardNote &note, AddOutcome &outcome, AnkiProblem &problem) {
	// The files wait in a folder of this app's; AnkiDroid.kt hands each to AnkiDroid and removes it.
	std::vector<AnkiDroidFile> files;
	std::vector<std::string> waiting;
	if (!note.media.empty()) {
		AnkiDroidAnswer where;
		if (!ask("status", "{}", where, problem)) return false;
		for (const CardMedia &media : note.media) {
			AnkiDroidFile file;
			file.name = anki_media_safe_name(media.name);
			file.sound = media.role != "picture";
			file.fields = media.fields;
			std::string path = where.media_folder + "/" + file.name;
			if (!where.media_folder.empty() && write_file(path, media.data))
				waiting.push_back(path);
			else
				log_warn("mining: %s could not be put down for AnkiDroid", file.name.c_str());
			files.push_back(std::move(file));
		}
	}
	AnkiDroidAnswer answer;
	if (!ask("add", ankidroid_add_request(link_note(note), files), answer, problem)) {
		// A card AnkiDroid did not take leaves nothing behind.
		for (const std::string &path : waiting) ::remove(path.c_str());
		return false;
	}
	for (const std::string &name : answer.missing) log_warn("mining: AnkiDroid did not take %s", name.c_str());
	outcome.handed_over = false;
	outcome.note_id = std::strtod(answer.id.c_str(), nullptr);
	return true;
}

// ---------------------------------------------------------------- the kit

#if ORE_MINING_KIT

void add_anki_apps(MiningKit &kit, const AnkiAppHost &host, const std::string &scheme, AnkiMobile::Timing timing) {
	std::shared_ptr<AnkiMobileTarget> mobile;
	std::shared_ptr<AnkiDroidTarget> droid;
	if (host.open_link && host.take_answer) {
		// The kit keeps the target and outlives it, so the target may speak through the kit.
		MiningKit *out = &kit;
		mobile = std::make_shared<AnkiMobileTarget>(
		    host, scheme, kit.folder() + "/ankimobile.json",
		    [out](const std::string &event, const std::string &text) {
			    json::Value payload;
			    if (json::parse(text, payload)) out->emit(event, payload);
		    },
		    timing);
		kit.set_target(kAnkiMobileTarget, mobile);
		kit.set_method("ankimobile.fetch", [mobile](const json::Value &, json::Value &result, AnkiProblem &problem) {
			if (!mobile->fetch(problem)) return false;
			result = json::Value::object();
			result.set("asked", true);
			return true;
		});
	}
	if (host.ankidroid) {
		droid = std::make_shared<AnkiDroidTarget>(host);
		kit.set_target(kAnkiDroidTarget, droid);
		kit.set_method("ankidroid.status", [droid](const json::Value &, json::Value &result, AnkiProblem &problem) {
			bool installed = false, allowed = false;
			if (!droid->status(installed, allowed, problem)) return false;
			result = json::Value::object();
			result.set("installed", installed);
			result.set("allowed", allowed);
			return true;
		});
	}
	bool has_mobile = bool(mobile), has_droid = bool(droid);
	kit.set_method("anki.targets", [has_mobile, has_droid](const json::Value &, json::Value &result, AnkiProblem &) {
		result = json::Value::array();
		auto place = [&result](const char *name, const char *label, const char *detail) {
			json::Value entry = json::Value::object();
			entry.set("name", name);
			entry.set("label", label);
			entry.set("detail", detail);
			result.push(std::move(entry));
		};
		place("connect", "AnkiConnect", "An Anki on a computer or a server, reached over the network.");
		if (has_mobile) place(kAnkiMobileTarget, "AnkiMobile", "The Anki app on this device. It comes to the front for a moment with every card.");
		if (has_droid) place(kAnkiDroidTarget, "AnkiDroid", "The Anki app on this device.");
		return true;
	});
}

#endif  // ORE_MINING_KIT

}  // namespace ore
