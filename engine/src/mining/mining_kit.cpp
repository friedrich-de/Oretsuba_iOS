// The mining kit: looking words up and making cards inside the game.
#include "mining/mining_kit.h"
#if ORE_MINING_KIT

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "dict/yomitan_import.h"
#include "mining/anki_connect.h"
#include "mining/anki_fields.h"
#include "mining/card_note.h"
#include "mining/word_audio.h"
#include "util/log.h"

namespace ore {

namespace {

const int kTextSizes[] = {70, 80, 90, 100, 115, 130};
const size_t kMaxName = 300, kMaxFields = 200, kMaxTags = 20;
const uint64_t kMaxDictionaryDownload = 4ull * 1024 * 1024 * 1024;

// Dictionaries a player adds with one tap, the same as VN Mining Server's page offers: free, and
// the ones most people use. Each address always gives the newest release.
struct Suggestion {
	const char *name, *text, *url;
};
const Suggestion kSuggested[] = {
    {"Jitendex", "Japanese to English", "https://github.com/stephenmk/stephenmk.github.io/releases/latest/download/jitendex-yomitan.zip"},
    {"JMnedict", "Names of people and places", "https://github.com/yomidevs/jmdict-yomitan/releases/latest/download/JMnedict.zip"},
};

std::string lower(std::string s) {
	for (char &c : s)
		if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return s;
}

std::string trimmed(const std::string &s) {
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) --b;
	return s.substr(a, b - a);
}

bool read_file(const std::string &path, std::string &out) {
	std::ifstream file(path, std::ios::binary);
	if (!file) return false;
	std::stringstream text;
	text << file.rdbuf();
	out = text.str();
	return true;
}

json::Value names_json(const std::vector<std::string> &names) {
	json::Value list = json::Value::array();
	for (const std::string &name : names) list.push(name);
	return list;
}

json::Value roles_json(const std::vector<std::pair<std::string, std::string>> &roles) {
	json::Value list = json::Value::array();
	for (const auto &role : roles) {
		json::Value entry = json::Value::object();
		entry.set("field", role.first);
		entry.set("role", role.second);
		list.push(std::move(entry));
	}
	return list;
}

// The roles as the object roles_for reads.
json::Value chosen_json(const std::vector<std::pair<std::string, std::string>> &roles) {
	json::Value chosen = json::Value::object();
	for (const auto &role : roles) chosen.set(role.first, role.second);
	return chosen;
}

std::vector<std::string> strings_of(const json::Value &list) {
	std::vector<std::string> out;
	for (const json::Value &item : list.items())
		if (item.is_string()) out.push_back(item.as_string());
	return out;
}

// The media type of a voice's file by its ending.
std::string voice_type(const std::string &ending) {
	std::string e = lower(ending);
	if (e == "ogg" || e == "oga") return "audio/ogg";
	if (e == "wav") return "audio/wav";
	if (e == "mp3") return "audio/mpeg";
	if (e == "m4a" || e == "mp4") return "audio/mp4";
	return "application/octet-stream";
}

}  // namespace

// ---------------------------------------------------------------- settings

json::Value KitSettings::to_json() const {
	json::Value value = json::Value::object();
	value.set("direct", direct);
	json::Value &anki = value.set("anki", json::Value::object());
	anki.set("target", target);
	anki.set("address", address);
	anki.set("deck", deck);
	anki.set("noteType", note_type);
	// A list, not an object: what reads this may not keep an object's order.
	anki.set("fields", roles_json(fields));
	anki.set("tags", names_json(tags));
	json::Value &popup = value.set("popup", json::Value::object());
	popup.set("textSize", text_size);
	popup.set("hideTags", hide_tags);
	popup.set("compact", compact);
	return value;
}

void KitSettings::merge(const json::Value &patch) {
	auto text = [](const json::Value &value, std::string &out) {
		if (value.is_string() && value.as_string().size() <= kMaxName) out = value.as_string();
	};
	if (patch["direct"].is_bool()) direct = patch["direct"].as_bool();
	const json::Value &anki = patch["anki"];
	if (anki["target"].is_string() && !anki["target"].as_string().empty()) text(anki["target"], target);
	text(anki["address"], address);
	text(anki["deck"], deck);
	text(anki["noteType"], note_type);
	if (anki["fields"].is_array() || anki["fields"].is_object()) {
		fields.clear();
		auto add = [this](const std::string &field, const json::Value &role) {
			if (field.empty() || field.size() > kMaxName || fields.size() >= kMaxFields) return;
			for (const auto &known : fields)
				if (known.first == field) return;
			fields.emplace_back(field, role.is_string() && is_field_role(role.as_string()) ? role.as_string() : "none");
		};
		for (const json::Value &entry : anki["fields"].items()) add(entry.str("field"), entry["role"]);
		for (const auto &member : anki["fields"].members()) add(member.first, member.second);
	}
	if (anki["tags"].is_array()) {
		tags.clear();
		for (const json::Value &tag : anki["tags"].items()) {
			// Anki parts tags at blanks.
			std::stringstream words(tag.as_string());
			for (std::string word; words >> word;)
				if (word.size() <= kMaxName && tags.size() < kMaxTags && std::find(tags.begin(), tags.end(), word) == tags.end()) tags.push_back(word);
		}
	}
	const json::Value &popup = patch["popup"];
	if (popup["textSize"].is_number()) {
		double wanted = popup["textSize"].as_number();
		int best = 100;
		for (int size : kTextSizes)
			if (std::abs(size - wanted) < std::abs(best - wanted)) best = size;
		text_size = best;
	}
	if (popup["hideTags"].is_bool()) hide_tags = popup["hideTags"].as_bool();
	if (popup["compact"].is_bool()) compact = popup["compact"].as_bool();
}

// ---------------------------------------------------------------- start

std::unique_ptr<MiningKit> MiningKit::open(Options options, std::string &problem) {
	std::string rules_text;
	if (!read_file(options.resources + "/transforms.json", rules_text)) {
		problem = "The rules for undoing inflections (transforms.json) are missing from the app.";
		return nullptr;
	}
	std::shared_ptr<const dict::Deinflector> rules = dict::Deinflector::load(rules_text, problem);
	if (!rules) return nullptr;
	mkdir(options.folder.c_str(), 0700);
	struct stat st;
	if (stat(options.folder.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
		problem = "The folder for dictionaries and settings could not be made.";
		return nullptr;
	}
	return std::unique_ptr<MiningKit>(new MiningKit(std::move(options), std::move(rules)));
}

MiningKit::MiningKit(Options options, std::shared_ptr<const dict::Deinflector> rules) : options_(std::move(options)) {
	library_ = std::make_unique<dict::DictionaryLibrary>(options_.folder + "/dictionaries", std::move(rules));
	std::string text;
	json::Value saved;
	if (read_file(options_.folder + "/settings.json", text) && json::parse(text, saved) && saved.is_object()) settings_.merge(saved);
}

MiningKit::~MiningKit() {
	// What a host added goes first, while the kit is whole: a target's last word still finds its listener.
	std::map<std::string, std::shared_ptr<CardTarget>> targets;
	std::map<std::string, KitMethod> methods;
	{
		std::lock_guard<std::mutex> lock(gate_);
		targets.swap(targets_);
		methods.swap(methods_);
	}
	targets.clear();
	methods.clear();
}

KitSettings MiningKit::settings() const {
	std::lock_guard<std::mutex> lock(gate_);
	return settings_;
}

void MiningKit::save(const KitSettings &settings) const {
	std::string path = options_.folder + "/settings.json", unfinished = path + ".new";
	{
		std::ofstream file(unfinished, std::ios::binary | std::ios::trunc);
		file << json::write(settings.to_json());
		if (!file) {
			log_warn("mining: the settings could not be kept");
			return;
		}
	}
	if (rename(unfinished.c_str(), path.c_str()) != 0) log_warn("mining: the settings could not be kept");
}

void MiningKit::change(const std::function<void(KitSettings &)> &change) {
	std::lock_guard<std::mutex> lock(gate_);
	change(settings_);
	save(settings_);
}

bool MiningKit::direct() const {
	std::lock_guard<std::mutex> lock(gate_);
	return settings_.direct;
}

int64_t MiningKit::set_line(KitLine line) {
	auto kept = std::make_shared<const KitLine>(std::move(line));
	std::lock_guard<std::mutex> lock(gate_);
	line_ = std::move(kept);
	return ++line_id_;
}

std::shared_ptr<const KitLine> MiningKit::line(int64_t *id) const {
	std::lock_guard<std::mutex> lock(gate_);
	if (id) *id = line_id_;
	return line_;
}

void MiningKit::set_target(const std::string &name, std::shared_ptr<CardTarget> target) {
	std::lock_guard<std::mutex> lock(gate_);
	targets_[name] = std::move(target);
}

void MiningKit::set_method(const std::string &name, KitMethod method) {
	std::lock_guard<std::mutex> lock(gate_);
	if (method)
		methods_[name] = std::move(method);
	else
		methods_.erase(name);
}

void MiningKit::set_events(std::function<void(const std::string &, const std::string &)> sink) {
	std::lock_guard<std::mutex> lock(gate_);
	events_ = std::move(sink);
}

void MiningKit::emit(const std::string &name, const json::Value &payload) {
	std::function<void(const std::string &, const std::string &)> sink;
	{
		std::lock_guard<std::mutex> lock(gate_);
		sink = events_;
	}
	if (sink) sink(name, json::write(payload));
}

std::shared_ptr<CardTarget> MiningKit::target(const KitSettings &settings, const std::string *address, const std::string *key) const {
	std::lock_guard<std::mutex> lock(gate_);
	if (settings.target != "connect") {
		auto found = targets_.find(settings.target);
		return found == targets_.end() ? nullptr : found->second;
	}
	const std::string &where = address ? *address : settings.address;
	if (where.empty() || !options_.fetch) return nullptr;
	return std::make_shared<AnkiConnectTarget>(AnkiClient(where, key ? *key : anki_key_, options_.fetch));
}

void MiningKit::fail(const AnkiProblem &problem) {
	throw Failure{problem.message, problem.unreachable ? "unreachable" : "anki"};
}

// ---------------------------------------------------------------- calls

std::string MiningKit::call(const std::string &method, const std::string &params) {
	json::Value answer = json::Value::object();
	auto failed = [&](const std::string &message, const std::string &kind) {
		answer = json::Value::object();
		answer.set("ok", false);
		answer.set("error", message);
		answer.set("kind", kind);
	};
	json::Value read = json::Value::object();
	if (!trimmed(params).empty() && !json::parse(params, read)) {
		failed("That call was not understood.", "internal");
		return json::write(answer);
	}
	try {
		json::Value result = dispatch(method, read);
		answer.set("ok", true);
		answer.set("result", std::move(result));
	} catch (const Failure &failure) {
		failed(failure.message, failure.kind);
	} catch (const dict::DictError &error) {
		failed(error.what(), "dictionary");
	} catch (const std::exception &error) {
		log_warn("mining: %s failed: %s", method.c_str(), error.what());
		failed("Something went wrong in the app.", "internal");
	}
	return json::write(answer);
}

json::Value MiningKit::settings_view() const {
	std::lock_guard<std::mutex> lock(gate_);
	json::Value view = settings_.to_json();
	view.set("ankiKeySet", !anki_key_.empty());
	view.set("audioSet", !audio_address_.empty());
	return view;
}

json::Value MiningKit::dispatch(const std::string &method, const json::Value &params) {
	// ---- the launcher's settings
	if (method == "settings.get") return settings_view();
	if (method == "settings.set") {
		json::Value patch = params;
		if (params["anki"]["address"].is_string()) {
			std::string address;
			if (!trimmed(params["anki"]["address"].as_string()).empty() && !read_anki_address(params["anki"]["address"].as_string(), address))
				throw Failure{"That is not an http or https address.", "address"};
			json::Value anki = params["anki"];
			anki.set("address", address);
			patch.set("anki", std::move(anki));
		}
		change([&](KitSettings &settings) { settings.merge(patch); });
		return settings_view();
	}
	if (method == "secrets.set") {
		std::string audio;
		if (params["audioAddress"].is_string() && !trimmed(params["audioAddress"].as_string()).empty()) {
			std::string problem;
			if (!read_audio_address(params["audioAddress"].as_string(), audio, problem)) throw Failure{problem, "address"};
		}
		{
			std::lock_guard<std::mutex> lock(gate_);
			if (params["ankiKey"].is_string()) anki_key_ = params["ankiKey"].as_string();
			if (params["audioAddress"].is_string()) audio_address_ = audio;
		}
		return settings_view();
	}
	if (method == "anki.check") return anki_check(params);
	if (method == "anki.fields") return anki_fields(params);
	if (method == "anki.choose") return anki_choose(params);
	if (method == "anki.setRole") {
		std::string field = params.str("field"), role = params.str("role");
		if (!is_field_role(role)) throw Failure{"There is no such part of a card.", "internal"};
		change([&](KitSettings &settings) {
			for (auto &known : settings.fields)
				if (known.first == field) known.second = role;
		});
		return settings_view();
	}
	if (method == "anki.roleChoices") {
		json::Value list = json::Value::array();
		for (const FieldRole &choice : field_roles()) {
			json::Value entry = json::Value::object();
			entry.set("role", choice.role);
			entry.set("label", choice.label);
			entry.set("detail", choice.detail);
			list.push(std::move(entry));
		}
		return list;
	}
	if (method == "audio.check") {
		std::string address, problem;
		if (params["address"].is_string()) {
			if (!read_audio_address(params["address"].as_string(), address, problem)) throw Failure{problem, "address"};
		} else {
			std::lock_guard<std::mutex> lock(gate_);
			address = audio_address_;
		}
		if (address.empty()) throw Failure{"Enter the address of an audio source first.", "settings"};
		std::vector<std::string> names;
		// 食べる, たべる: a word every source has.
		WordAudioOutcome outcome = word_audio_choices(options_.fetch, address, "\xE9\xA3\x9F\xE3\x81\xB9\xE3\x82\x8B", "\xE3\x81\x9F\xE3\x81\xB9\xE3\x82\x8B", names);
		if (outcome != WordAudioOutcome::Found)
			throw Failure{"The audio source could not be reached, or answered with something that is neither a sound nor Yomitan's list of sounds.",
			              "unreachable"};
		if (names.empty()) throw Failure{"The audio source answered, but has no sound for \xE9\xA3\x9F\xE3\x81\xB9\xE3\x82\x8B.", "audio"};
		json::Value result = json::Value::object();
		result.set("names", names_json(names));
		return result;
	}

	// ---- dictionaries
	if (method == "dict.list") return dict_list();
	if (method == "dict.import") {
		std::string path = params.str("path");
		if (path.empty()) throw Failure{"No file was chosen.", "internal"};
		std::string name = params.str("name");
		if (name.empty()) name = path.substr(path.find_last_of('/') + 1);
		bool remove_zip = params.boolean("remove");
		if (!library_->start_import(name, [path](const std::function<void(double)> &, const std::atomic<bool> &) { return path; }, remove_zip))
			throw Failure{"A dictionary is being imported already. Wait for it, or stop it.", "busy"};
		return dict_list();
	}
	if (method == "dict.download") {
		std::string url = trimmed(params.str("url"));
		std::string start = lower(url.substr(0, 8));
		if (start.rfind("http://", 0) != 0 && start.rfind("https://", 0) != 0) throw Failure{"That is not an http or https address.", "address"};
		HttpFetch fetch = options_.fetch;
		std::string to = options_.folder + "/dictionaries/download.zip";
		auto download = [fetch, url, to](const std::function<void(double)> &progress, const std::atomic<bool> &stop) {
			if (!fetch) throw dict::DictError("This build cannot download.");
			FetchRequest request;
			request.url = url;
			request.to_file = to;
			request.max_bytes = kMaxDictionaryDownload;
			request.timeout_ms = 30000;
			request.progress = progress;
			request.stop = &stop;
			FetchAnswer answer;
			fetch(request, answer);
			bool taken = answer.status >= 200 && answer.status < 300 && !answer.too_large && !stop.load();
			if (!taken) ::remove(to.c_str());
			if (stop.load()) throw dict::ImportStopped();
			if (answer.too_large) throw dict::DictError("That file is too large to be a dictionary.");
			if (answer.status <= 0) throw dict::DictError(answer.error.empty() ? "The address did not answer." : "The address did not answer (" + answer.error + ").");
			if (!taken) throw dict::DictError("The address answered " + std::to_string(answer.status) + ".");
			return to;
		};
		if (!library_->start_import(url, download, true)) throw Failure{"A dictionary is being imported already. Wait for it, or stop it.", "busy"};
		return dict_list();
	}
	if (method == "dict.cancel") {
		library_->cancel_import();
		return dict_list();
	}
	if (method == "dict.arrange") {
		if (!library_->arrange(strings_of(params["order"]), strings_of(params["disabled"])))
			throw Failure{"The dictionaries have changed. Look at the list again.", "stale"};
		return dict_list();
	}
	if (method == "dict.enable") {
		std::string id = params.str("id");
		std::vector<std::string> order, disabled;
		for (const auto &item : library_->installed()) {
			const std::string &name = item.reader->info().id;
			order.push_back(name);
			if (name == id ? !params.boolean("enabled", true) : !item.enabled) disabled.push_back(name);
		}
		if (!library_->arrange(order, disabled)) throw Failure{"The dictionaries have changed. Look at the list again.", "stale"};
		return dict_list();
	}
	if (method == "dict.remove") {
		if (!library_->remove(params.str("id"))) throw Failure{"That dictionary is gone already.", "stale"};
		return dict_list();
	}

	// ---- the popup's page
	if (method == "page.init") return page_init();
	if (method == "lookup") return library_->lookup(params.str("text"));
	if (method == "card.known") return card_known(params);
	if (method == "card.add") return card_add(params);
	if (method == "audio.sources") {
		std::string address;
		{
			std::lock_guard<std::mutex> lock(gate_);
			address = audio_address_;
		}
		std::vector<std::string> names;
		if (word_audio_choices(options_.fetch, address, params.str("term"), params.str("reading"), names) == WordAudioOutcome::Failed)
			throw Failure{"The audio source could not be reached.", "unreachable"};
		json::Value result = json::Value::object();
		result.set("names", names_json(names));
		return result;
	}
	if (method == "line.get") {
		int64_t id = 0;
		auto now = line(&id);
		json::Value result = json::Value::object();
		result.set("id", id);
		result.set("sentence", now ? now->sentence : std::string());
		result.set("speaker", now ? now->speaker : std::string());
		result.set("hasVoice", now && !now->voice.empty());
		result.set("hasPicture", now && !now->picture.empty());
		return result;
	}
	if (method == "line.set") {
		// For a host without the game, and for tests.
		KitLine made;
		made.sentence = params.str("sentence");
		made.speaker = params.str("speaker");
		json::Value result = json::Value::object();
		result.set("id", set_line(std::move(made)));
		return result;
	}
	KitMethod added;
	{
		std::lock_guard<std::mutex> lock(gate_);
		auto found = methods_.find(method);
		if (found != methods_.end()) added = found->second;
	}
	if (added) {
		json::Value result;
		AnkiProblem problem;
		if (!added(params, result, problem)) fail(problem);
		return result;
	}
	throw Failure{"The app does not know what \"" + method + "\" asks for.", "internal"};
}

json::Value MiningKit::anki_check(const json::Value &params) {
	KitSettings now = settings();
	std::string address = now.address, key;
	{
		std::lock_guard<std::mutex> lock(gate_);
		key = anki_key_;
	}
	if (params["address"].is_string() && !read_anki_address(params["address"].as_string(), address))
		throw Failure{"That is not an http or https address.", "address"};
	if (params["key"].is_string()) key = params["key"].as_string();
	if (now.target == "connect" && address.empty()) throw Failure{"Enter the address of your AnkiConnect first.", "settings"};
	std::shared_ptr<CardTarget> anki = target(now, &address, &key);
	if (!anki) throw Failure{"This build cannot reach Anki that way.", "settings"};

	CardPlaces places;
	AnkiProblem problem;
	if (!anki->places(places, problem)) fail(problem);
	FoundNoteType found;
	bool lapis = find_lapis(*anki, places, found, problem);
	if (!lapis && problem.unreachable) fail(problem);

	if (params.boolean("adopt")) {
		// These are the player's from now on; with nothing chosen yet, Lapis and its deck are.
		{
			std::lock_guard<std::mutex> lock(gate_);
			anki_key_ = key;
		}
		change([&](KitSettings &settings) {
			settings.address = address;
			bool gone = std::find(places.note_types.begin(), places.note_types.end(), settings.note_type) == places.note_types.end();
			if ((settings.note_type.empty() || gone) && lapis) {
				settings.note_type = found.note_type;
				settings.fields = roles_for(found.field_names, json::Value::object());
				if (settings.deck.empty() || std::find(places.decks.begin(), places.decks.end(), settings.deck) == places.decks.end())
					settings.deck = found.deck;
			}
		});
	}

	json::Value result = json::Value::object();
	result.set("version", places.version);
	result.set("decks", names_json(places.decks));
	result.set("noteTypes", names_json(places.note_types));
	if (lapis) {
		json::Value &suggestion = result.set("lapis", json::Value::object());
		suggestion.set("noteType", found.note_type);
		suggestion.set("deck", found.deck);
	} else {
		result.set("lapis", json::Value());
	}
	result.set("settings", settings_view());
	return result;
}

json::Value MiningKit::anki_fields(const json::Value &params) {
	KitSettings now = settings();
	std::string note_type = params["noteType"].is_string() ? params["noteType"].as_string() : now.note_type;
	if (note_type.empty()) throw Failure{"Choose a note type first.", "settings"};
	std::shared_ptr<CardTarget> anki = target(now);
	if (!anki) throw Failure{"Enter the address of your AnkiConnect first.", "settings"};
	std::vector<std::string> names;
	AnkiProblem problem;
	if (!anki->field_names(note_type, names, problem)) fail(problem);
	bool chosen = note_type == now.note_type;
	auto roles = roles_for(names, chosen ? chosen_json(now.fields) : json::Value::object());
	// The note type's fields may have changed in Anki since they were chosen.
	if (chosen && roles != now.fields)
		change([&](KitSettings &settings) {
			if (settings.note_type == note_type) settings.fields = roles;
		});
	json::Value result = json::Value::object();
	result.set("noteType", note_type);
	result.set("fields", roles_json(roles));
	return result;
}

json::Value MiningKit::anki_choose(const json::Value &params) {
	KitSettings now = settings();
	std::string note_type = params["noteType"].is_string() ? params["noteType"].as_string() : now.note_type;
	std::vector<std::pair<std::string, std::string>> roles = now.fields;
	if (note_type != now.note_type && !note_type.empty()) {
		std::shared_ptr<CardTarget> anki = target(now);
		if (!anki) throw Failure{"Enter the address of your AnkiConnect first.", "settings"};
		std::vector<std::string> names;
		AnkiProblem problem;
		if (!anki->field_names(note_type, names, problem)) fail(problem);
		roles = roles_for(names, json::Value::object());
	}
	change([&](KitSettings &settings) {
		if (params["deck"].is_string()) settings.deck = params["deck"].as_string();
		settings.note_type = note_type;
		settings.fields = note_type.empty() ? std::vector<std::pair<std::string, std::string>>() : roles;
	});
	return settings_view();
}

// Whether a dictionary is the suggested one: by the address it updates from, or by its title,
// which carries its revision ("Jitendex.org [2026-11-23]").
static bool is_suggestion(const Suggestion &suggestion, const dict::DictionaryInfo &info) {
	if (info.download_url == suggestion.url) return true;
	std::string name = lower(suggestion.name), title = lower(info.title);
	if (title.compare(0, name.size(), name) != 0) return false;
	return title.size() == name.size() || title[name.size()] < 'a' || title[name.size()] > 'z';
}

json::Value MiningKit::dict_list() const {
	json::Value result = json::Value::object();
	json::Value &list = result.set("dictionaries", json::Value::array());
	for (const auto &item : library_->installed()) {
		json::Value entry = item.reader->info().to_json();
		entry.set("enabled", item.enabled);
		entry.set("fileSize", item.reader->file_size());
		list.push(std::move(entry));
	}
	dict::ImportState state = library_->import_state();
	json::Value &import = result.set("import", json::Value::object());
	import.set("state", state.state);
	import.set("source", state.source);
	import.set("progress", state.progress);
	import.set("title", state.title);
	import.set("error", state.error);
	// What is imported by a name a player knows, when it is a suggested dictionary.
	std::string name;
	for (const Suggestion &suggestion : kSuggested)
		if (state.source == suggestion.url) name = suggestion.name;
	import.set("name", name);
	json::Value &suggested = result.set("suggested", json::Value::array());
	for (const Suggestion &suggestion : kSuggested) {
		bool installed = false;
		for (const auto &item : library_->installed()) installed = installed || is_suggestion(suggestion, item.reader->info());
		json::Value entry = json::Value::object();
		entry.set("name", suggestion.name);
		entry.set("text", suggestion.text);
		entry.set("url", suggestion.url);
		entry.set("installed", installed);
		suggested.push(std::move(entry));
	}
	return result;
}

json::Value MiningKit::page_init() {
	KitSettings now = settings();
	json::Value result = json::Value::object();
	result.set("direct", now.direct);
	json::Value &popup = result.set("popup", json::Value::object());
	popup.set("textSize", now.text_size);
	popup.set("hideTags", now.hide_tags);
	popup.set("compact", now.compact);
	json::Value &dictionaries = result.set("dictionaries", json::Value::array());
	for (const auto &item : library_->installed()) {
		if (!item.enabled) continue;
		const dict::DictionaryInfo &info = item.reader->info();
		json::Value entry = json::Value::object();
		entry.set("title", info.title);
		entry.set("frequencyMode", info.frequency_mode.empty() ? json::Value() : json::Value(info.frequency_mode));
		dictionaries.push(std::move(entry));
	}
	result.set("styles", library_->styles());
	std::string problem = target(now) ? card_problem(now.deck, now.note_type, now.fields) : "Set Anki up in the settings before the game starts.";
	json::Value &cards = result.set("cards", json::Value::object());
	cards.set("possible", problem.empty());
	cards.set("problem", problem);
	{
		std::lock_guard<std::mutex> lock(gate_);
		result.set("wordAudio", !audio_address_.empty() && bool(options_.fetch));
	}
	result.set("source", kKitSource);
	return result;
}

json::Value MiningKit::card_known(const json::Value &params) {
	KitSettings now = settings();
	std::shared_ptr<CardTarget> anki = target(now);
	auto current = line();
	LineContent content{current ? current->sentence : std::string(), kKitSource};
	// A word that makes no card is not asked about.
	std::vector<CardNote> notes;
	std::vector<size_t> asked;
	const auto &words = params["words"].items();
	for (size_t i = 0; anki && i < words.size(); ++i) {
		CardNote note;
		std::string problem;
		if (!compose_note(now.deck, now.note_type, now.fields, now.tags, content, WordContent::from_json(words[i]), note, problem)) continue;
		note.media.clear();
		notes.push_back(std::move(note));
		asked.push_back(i);
	}
	std::vector<Known> answers;
	AnkiProblem problem;
	if (anki && !notes.empty() && !anki->known(notes, answers, problem)) fail(problem);
	std::vector<bool> has(words.size(), false);
	for (size_t i = 0; i < asked.size() && i < answers.size(); ++i) has[asked[i]] = answers[i] == Known::Yes;
	json::Value known = json::Value::array();
	for (bool one : has) known.push(one);
	json::Value result = json::Value::object();
	result.set("known", std::move(known));
	return result;
}

bool MiningKit::word_sound(const std::string &term, const std::string &reading, const std::string &source, std::string &bytes,
                           std::string &type, std::string &ending, bool &no_source) const {
	std::string address;
	{
		std::lock_guard<std::mutex> lock(gate_);
		address = audio_address_;
	}
	WordSound sound;
	WordAudioOutcome outcome = find_word_audio(options_.fetch, address, term, reading, source, sound);
	no_source = outcome == WordAudioOutcome::NoSource;
	if (outcome != WordAudioOutcome::Found) return false;
	bytes = std::move(sound.data);
	type = sound.type;
	ending = sound.extension;
	return true;
}

json::Value MiningKit::card_add(const json::Value &params) {
	KitSettings now = settings();
	std::shared_ptr<CardTarget> anki = target(now);
	if (!anki) throw Failure{"Set Anki up in the settings before the game starts.", "settings"};
	WordContent word = WordContent::from_json(params["word"]);
	if (word.expression.empty()) throw Failure{"There is no word to make a card of.", "internal"};
	auto current = line();
	LineContent content{current ? current->sentence : std::string(), kKitSource};
	CardNote note;
	std::string problem_text;
	if (!compose_note(now.deck, now.note_type, now.fields, now.tags, content, word, note, problem_text)) throw Failure{problem_text, "settings"};
	const std::string duplicate = "This deck has a card like this already.";

	// The words first: a card Anki would refuse gets no picture or sound made for nothing.
	AnkiProblem problem;
	std::vector<Known> answers;
	if (!anki->known({note}, answers, problem)) fail(problem);
	if (!answers.empty() && answers[0] == Known::Yes) throw Failure{duplicate, "duplicate"};

	bool no_word_audio = false;
	const std::string of_line = current ? mark_of_text(current->sentence) : std::string();
	for (CardMedia &media : note.media) {
		if (media.role == "picture" && current && !current->picture.empty()) {
			media.data = current->picture;
			media.type = "image/jpeg";
			media.name = "oretsuba_" + of_line + "_" + mark_of_bytes(media.data) + ".jpg";
		} else if (media.role == "sound" && current && !current->voice.empty()) {
			std::string ending = lower(current->voice_ending);
			if (!options_.encode_voice || !options_.encode_voice(current->voice, current->voice_ending, media.data, media.type, ending)) {
				media.data = current->voice;
				media.type = voice_type(ending);
			}
			media.name = "oretsuba_" + of_line + "_" + mark_of_bytes(media.data) + "." + (ending.empty() ? "bin" : ending);
		} else if (media.role == "wordAudio") {
			std::string choice = params.str("audioChoice"), ending;
			bool no_source = false;
			if (word_sound(word.expression, word.reading, choice, media.data, media.type, ending, no_source)) {
				// The same word from the same sound gets the same name, so its sound is in Anki once.
				std::string term = trimmed(word.expression), reading = trimmed(word.reading);
				media.name = "vn-miner_word_" + mark_of_text(term + "|" + (reading.empty() ? term : reading) + (choice.empty() ? "" : "|" + choice)) +
				             "." + ending;
			} else {
				// A word the source has no sound for still gets its card.
				no_word_audio = !no_source;
			}
		}
	}
	note.media.erase(std::remove_if(note.media.begin(), note.media.end(), [](const CardMedia &media) { return media.data.empty(); }),
	                 note.media.end());

	AddOutcome outcome;
	if (!anki->add(note, outcome, problem)) {
		std::string said = lower(problem.said);
		if (said.find("duplicate") != std::string::npos) throw Failure{duplicate, "duplicate"};
		if (said.find("empty") != std::string::npos)
			throw Failure{"A new \"" + now.note_type + "\" card needs something in its first field. Give it the word in the settings.", "settings"};
		fail(problem);
	}

	json::Value result = json::Value::object();
	result.set("handedOver", outcome.handed_over);
	result.set("noteId", outcome.note_id);
	result.set("label", plain_text(note.fields[0].second));
	result.set("noWordAudio", no_word_audio);
	std::string message = outcome.handed_over ? "Handed " + word.expression + " to Anki." : "Made a card of " + word.expression + ".";
	if (no_word_audio) message += " The audio source had no sound for it.";
	result.set("message", message);
	return result;
}

// ---------------------------------------------------------------- files

bool MiningKit::data(const std::string &method, const std::string &params, std::string &bytes, std::string &type) {
	json::Value read = json::Value::object();
	if (!trimmed(params).empty() && !json::parse(params, read)) return false;
	if (method == "dict.media") return library_->media(read.str("dictionary"), read.str("path"), bytes, type);
	if (method == "audio.word") {
		std::string ending;
		bool no_source = false;
		return word_sound(read.str("term"), read.str("reading"), read.str("source"), bytes, type, ending, no_source);
	}
	auto current = line();
	if (method == "line.picture" && current && !current->picture.empty()) {
		bytes = current->picture;
		type = "image/jpeg";
		return true;
	}
	if (method == "line.voice" && current && !current->voice.empty()) {
		bytes = current->voice;
		type = voice_type(current->voice_ending);
		return true;
	}
	return false;
}

}  // namespace ore

#endif  // ORE_MINING_KIT
