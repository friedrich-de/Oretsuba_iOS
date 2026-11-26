// The part of the mining kit that talks to an AnkiConnect.
#include "mining/anki_connect.h"

#include <algorithm>
#include <cstdio>

#include "util/utf8.h"

namespace ore {

namespace {

std::string lower(std::string s) {
	for (char &c : s)
		if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return s;
}

std::string hex8(uint32_t hash) {
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%08x", hash);
	return buffer;
}

}  // namespace

bool AnkiClient::invoke(const std::string &action, const json::Value &params, json::Value &result, AnkiProblem &problem,
                        int timeout_ms) const {
	problem = AnkiProblem();
	if (!usable()) {
		problem.message = "No AnkiConnect address is set.";
		problem.unreachable = true;
		return false;
	}
	json::Value message = json::Value::object();
	message.set("action", action);
	message.set("version", kAnkiVersion);
	if (!key_.empty()) message.set("key", key_);
	if (!params.is_null()) message.set("params", params);

	FetchRequest request;
	request.method = "POST";
	request.url = address_;
	request.headers.emplace_back("Content-Type", "application/json");
	request.body = json::write(message);
	request.timeout_ms = timeout_ms;
	request.max_bytes = 64u << 20;

	// While it syncs, a server says that the collection is busy: once more then.
	for (int attempt = 0;; ++attempt) {
		FetchAnswer answer;
		fetch_(request, answer);
		if (answer.status <= 0) {
			problem.message = "Anki could not be reached at " + address_ + (answer.error.empty() ? "." : " (" + answer.error + ").");
			problem.unreachable = true;
			return false;
		}
		if (answer.status < 200 || answer.status >= 300) {
			problem.message = "The AnkiConnect at " + address_ + " answered " + std::to_string(answer.status) + ".";
			problem.unreachable = answer.status >= 500;
			return false;
		}
		json::Value body;
		if (!json::parse(answer.body, body) || !body.is_object() || !body.has("result") || !body.has("error")) {
			problem.message = "What answers at this address is not AnkiConnect. Check it in the settings.";
			return false;
		}
		const std::string &error = body["error"].as_string();
		if (error.empty()) {
			result = body["result"];
			return true;
		}
		std::string said = lower(error);
		if (attempt == 0 && said.find("collection is busy") != std::string::npos) continue;
		problem.said = error;
		problem.message = said.find("api key") != std::string::npos ? "The AnkiConnect did not take the key. Check it in the settings."
		                                                             : "Anki said: " + error;
		return false;
	}
}

bool AnkiClient::store_media(const std::string &name, const std::string &data, std::string &stored, AnkiProblem &problem) const {
	json::Value params = json::Value::object();
	params.set("filename", name);
	params.set("data", base64(data));
	json::Value result;
	if (!invoke("storeMediaFile", params, result, problem, 120000)) return false;
	stored = result.is_string() && !result.as_string().empty() ? result.as_string() : name;
	return true;
}

// ---------------------------------------------------------------- cards

namespace {

std::string escaped_query(const std::string &text) {
	std::string out;
	for (char c : text) {
		if (c == '\\' || c == '"') out += '\\';
		out += c;
	}
	return out;
}

std::string escaped_html(const std::string &text) {
	std::string out;
	for (char c : text) {
		if (c == '&')
			out += "&amp;";
		else if (c == '<')
			out += "&lt;";
		else if (c == '>')
			out += "&gt;";
		else if (c == '"')
			out += "&quot;";
		else
			out += c;
	}
	return out;
}

json::Value note_json(const CardNote &note, const std::vector<std::pair<std::string, std::string>> &fields) {
	json::Value value = json::Value::object();
	value.set("deckName", note.deck);
	value.set("modelName", note.note_type);
	json::Value &names = value.set("fields", json::Value::object());
	for (const auto &field : fields) names.set(field.first, field.second);
	json::Value &tags = value.set("tags", json::Value::array());
	for (const std::string &tag : note.tags) tags.push(tag);
	json::Value &options = value.set("options", json::Value::object());
	options.set("allowDuplicate", note.allow_duplicate);
	options.set("duplicateScope", "deck");
	return value;
}

bool names_of(const json::Value &list, std::vector<std::string> &names) {
	names.clear();
	if (!list.is_array()) return false;
	for (const json::Value &name : list.items())
		if (name.is_string()) names.push_back(name.as_string());
	return true;
}

}  // namespace

std::string picture_field(const std::string &name) {
	return "<img src=\"" + escaped_html(name) + "\">";
}

std::string sound_field(const std::string &name) {
	return "[sound:" + name + "]";
}

bool AnkiConnectTarget::places(CardPlaces &places, AnkiProblem &problem) {
	json::Value result;
	if (!client_.invoke("version", result, problem)) return false;
	places.version = result.is_number() ? json::number_text(result.as_number()) : std::string();
	if (!client_.invoke("deckNames", result, problem)) return false;
	names_of(result, places.decks);
	if (!client_.invoke("modelNames", result, problem)) return false;
	names_of(result, places.note_types);
	return true;
}

bool AnkiConnectTarget::field_names(const std::string &note_type, std::vector<std::string> &names, AnkiProblem &problem) {
	json::Value params = json::Value::object(), result;
	params.set("modelName", note_type);
	if (!client_.invoke("modelFieldNames", params, result, problem)) return false;
	names_of(result, names);
	return true;
}

bool AnkiConnectTarget::deck_of_newest(const std::string &note_type, const std::vector<std::string> &decks, std::string &deck,
                                       AnkiProblem &problem) {
	deck.clear();
	json::Value params = json::Value::object(), result;
	params.set("query", "note:\"" + escaped_query(note_type) + "\"");
	// An AnkiConnect that does not answer one of these leaves the deck to be chosen.
	if (!client_.invoke("findNotes", params, result, problem)) return !problem.unreachable;
	double newest = -1;
	for (const json::Value &id : result.items()) newest = std::max(newest, id.as_number(-1));
	if (newest < 0) return true;
	params = json::Value::object();
	params.set("notes", json::Value::array()).push(newest);
	if (!client_.invoke("notesInfo", params, result, problem)) return !problem.unreachable;
	const json::Value &card = result[0]["cards"][0];
	if (!card.is_number()) return true;
	params = json::Value::object();
	params.set("cards", json::Value::array()).push(card);
	if (!client_.invoke("cardsInfo", params, result, problem)) return !problem.unreachable;
	std::string name = result[0].str("deckName");
	if (std::find(decks.begin(), decks.end(), name) != decks.end()) deck = name;
	problem = AnkiProblem();
	return true;
}

bool AnkiConnectTarget::known(const std::vector<CardNote> &notes, std::vector<Known> &answers, AnkiProblem &problem) {
	answers.assign(notes.size(), Known::CannotSay);
	problem = AnkiProblem();
	if (notes.empty()) return true;
	json::Value params = json::Value::object(), result;
	json::Value &list = params.set("notes", json::Value::array());
	for (const CardNote &note : notes) list.push(note_json(note, note.fields));
	if (!client_.invoke("canAddNotesWithErrorDetail", params, result, problem)) {
		// An AnkiConnect that does not know the question says so when the card is added.
		if (problem.unreachable) return false;
		problem = AnkiProblem();
		return true;
	}
	for (size_t i = 0; i < notes.size() && i < result.size(); ++i) {
		const json::Value &answer = result[i];
		if (!answer.is_object() || !answer["canAdd"].is_bool()) continue;
		answers[i] = !answer["canAdd"].as_bool() && lower(answer.str("error")).find("duplicate") != std::string::npos ? Known::Yes : Known::No;
	}
	return true;
}

bool AnkiConnectTarget::add(const CardNote &note, AddOutcome &outcome, AnkiProblem &problem) {
	std::vector<std::pair<std::string, std::string>> fields = note.fields;
	for (const CardMedia &media : note.media) {
		std::string stored;
		if (!client_.store_media(media.name, media.data, stored, problem)) return false;
		std::string value = media.role == "picture" ? picture_field(stored) : sound_field(stored);
		for (auto &field : fields)
			if (std::find(media.fields.begin(), media.fields.end(), field.first) != media.fields.end()) field.second = value;
	}
	json::Value params = json::Value::object(), result;
	params.set("note", note_json(note, fields));
	if (!client_.invoke("addNote", params, result, problem)) return false;
	outcome.handed_over = false;
	outcome.note_id = result.as_number(0);
	return true;
}

// ---------------------------------------------------------------- addresses and marks

bool read_anki_address(const std::string &typed, std::string &address) {
	size_t a = 0, b = typed.size();
	while (a < b && (typed[a] == ' ' || typed[a] == '\t' || typed[a] == '\n' || typed[a] == '\r')) ++a;
	while (b > a && (typed[b - 1] == ' ' || typed[b - 1] == '\t' || typed[b - 1] == '\n' || typed[b - 1] == '\r')) --b;
	address = typed.substr(a, b - a);
	if (address.empty()) return false;
	std::string start = lower(address.substr(0, 8));
	if (start.rfind("http://", 0) != 0 && start.rfind("https://", 0) != 0) {
		if (address.find("://") != std::string::npos) return false;
		address = "http://" + address;
	}
	size_t host = address.find("://") + 3;
	if (host >= address.size() || address[host] == '/' || address.find(' ') != std::string::npos) return false;
	// A name and password in the address would go into messages.
	size_t path = address.find('/', host);
	if (address.substr(host, path == std::string::npos ? std::string::npos : path - host).find('@') != std::string::npos) return false;
	return true;
}

std::string base64(const std::string &data) {
	static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < data.size(); i += 3) {
		uint32_t n = uint32_t(uint8_t(data[i])) << 16 | uint32_t(uint8_t(data[i + 1])) << 8 | uint8_t(data[i + 2]);
		out += table[n >> 18];
		out += table[(n >> 12) & 63];
		out += table[(n >> 6) & 63];
		out += table[n & 63];
	}
	if (i + 1 == data.size()) {
		uint32_t n = uint32_t(uint8_t(data[i])) << 16;
		out += table[n >> 18];
		out += table[(n >> 12) & 63];
		out += "==";
	} else if (i + 2 == data.size()) {
		uint32_t n = uint32_t(uint8_t(data[i])) << 16 | uint32_t(uint8_t(data[i + 1])) << 8;
		out += table[n >> 18];
		out += table[(n >> 12) & 63];
		out += table[(n >> 6) & 63];
		out += '=';
	}
	return out;
}

bool unbase64(const std::string &text, std::string &data) {
	data.clear();
	uint32_t n = 0;
	int bits = 0;
	for (char c : text) {
		int v;
		if (c >= 'A' && c <= 'Z')
			v = c - 'A';
		else if (c >= 'a' && c <= 'z')
			v = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			v = c - '0' + 52;
		else if (c == '+' || c == '-')
			v = 62;
		else if (c == '/' || c == '_')
			v = 63;
		else if (c == '=' || c == '\n' || c == '\r' || c == ' ')
			continue;
		else
			return false;
		n = n << 6 | uint32_t(v);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			data += char((n >> bits) & 0xFF);
		}
	}
	return true;
}

std::string mark_of_text(const std::string &text) {
	uint32_t hash = 0x811c9dc5;
	auto mix = [&hash](uint32_t unit) {
		hash ^= unit;
		hash *= 0x01000193;
	};
	const char *p = text.data(), *end = p + text.size();
	while (p < end) {
		uint32_t c = utf8::next(p, end);
		if (c >= 0x10000) {
			c -= 0x10000;
			mix(0xD800 + (c >> 10));
			mix(0xDC00 + (c & 0x3FF));
		} else {
			mix(c);
		}
	}
	return hex8(hash);
}

std::string mark_of_bytes(const std::string &bytes) {
	uint32_t hash = 0x811c9dc5;
	for (unsigned char c : bytes) {
		hash ^= c;
		hash *= 0x01000193;
	}
	return hex8(hash);
}

}  // namespace ore
