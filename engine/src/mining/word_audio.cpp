// How a word sounds, from the audio source the player set.
#include "mining/word_audio.h"

#include <algorithm>
#include <functional>
#include <set>

#include "util/json.h"
#include "util/utf8.h"

namespace ore {

namespace {

struct Fetched {
	std::string address;  // where it came from in the end
	std::string type;
	std::string body;
};

struct NamedSound {
	std::string name, address;
};

std::string lower(std::string s) {
	for (char &c : s)
		if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return s;
}

bool starts_with(const std::string &s, const char *prefix) {
	return s.rfind(prefix, 0) == 0;
}

// Without the blanks at both ends, the full-width one among them.
std::string trimmed(const std::string &s) {
	size_t a = 0, b = s.size();
	for (;;) {
		if (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r'))
			++a;
		else if (b - a >= 3 && s.compare(a, 3, "\xE3\x80\x80") == 0)
			a += 3;
		else
			break;
	}
	for (;;) {
		if (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r'))
			--b;
		else if (b - a >= 3 && s.compare(b - 3, 3, "\xE3\x80\x80") == 0)
			b -= 3;
		else
			break;
	}
	return s.substr(a, b - a);
}

// A text as it goes into an address: all but letters, digits and - . _ ~ as %XX.
std::string percent(const std::string &text) {
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : text) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
			out += char(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

void replace_all(std::string &text, const std::string &what, const std::string &with) {
	for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + with.size())) text.replace(at, what.size(), with);
}

// Whether it is an http or https address with a host and without a name and password.
bool is_web_address(const std::string &address) {
	std::string start = lower(address.substr(0, 8));
	size_t host = starts_with(start, "https://") ? 8 : starts_with(start, "http://") ? 7 : 0;
	if (host == 0 || host >= address.size()) return false;
	size_t end = address.find_first_of("/?#", host);
	std::string authority = address.substr(host, end == std::string::npos ? std::string::npos : end - host);
	if (authority.empty() || authority.find('@') != std::string::npos) return false;
	for (unsigned char c : address)
		if (c <= ' ' || c == 0x7f) return false;
	return true;
}

// An address a list names, which may be given from the list's own; empty when it is no web address.
std::string resolved(const std::string &base, const std::string &reference) {
	if (reference.empty()) return {};
	std::string start = lower(reference.substr(0, 8));
	std::string address;
	if (starts_with(start, "http://") || starts_with(start, "https://")) {
		address = reference;
	} else {
		// Any other scheme is not followed.
		for (size_t i = 0; i < reference.size(); ++i) {
			char c = reference[i];
			if (c == ':') return {};
			bool scheme = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (i > 0 && ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'));
			if (!scheme) break;
		}
		size_t host = base.find("://");
		if (host == std::string::npos) return {};
		size_t path = base.find('/', host + 3);
		size_t query = base.find_first_of("?#");
		std::string origin = base.substr(0, std::min(path, query));
		if (starts_with(reference, "//")) {
			address = base.substr(0, host + 1) + reference;
		} else if (reference[0] == '/') {
			address = origin + reference;
		} else if (reference[0] == '?') {
			address = base.substr(0, base.find_first_of("?#")) + reference;
		} else if (reference[0] == '#') {
			address = base.substr(0, base.find('#')) + reference;
		} else {
			std::string folder = base.substr(0, query);
			size_t slash = folder.rfind('/');
			folder = slash == std::string::npos || slash < host + 3 ? origin + "/" : folder.substr(0, slash + 1);
			address = folder + reference;
		}
	}
	return is_web_address(address) ? address : std::string();
}

bool get(const HttpFetch &fetch, const std::string &address, Fetched &fetched) {
	FetchRequest request;
	request.url = address;
	request.timeout_ms = 15000;
	request.max_bytes = kMaxSoundBytes;
	FetchAnswer answer;
	fetch(request, answer);
	if (answer.status < 200 || answer.status >= 300 || answer.too_large) return false;
	fetched.address = answer.final_url.empty() ? address : answer.final_url;
	fetched.type = lower(answer.content_type);
	fetched.body = std::move(answer.body);
	return true;
}

struct Type {
	const char *name, *type, *extension;
};

const Type kTypes[] = {
    {"audio/mpeg", "audio/mpeg", "mp3"}, {"audio/mp3", "audio/mpeg", "mp3"},  {"audio/ogg", "audio/ogg", "ogg"},
    {"audio/opus", "audio/ogg", "opus"}, {"audio/webm", "audio/webm", "webm"}, {"audio/mp4", "audio/mp4", "m4a"},
    {"audio/x-m4a", "audio/mp4", "m4a"}, {"audio/aac", "audio/aac", "aac"},    {"audio/wav", "audio/wav", "wav"},
    {"audio/x-wav", "audio/wav", "wav"}, {"audio/wave", "audio/wav", "wav"},   {"audio/flac", "audio/flac", "flac"},
};

const char *const kTypeOfExtension[][2] = {
    {".mp3", "audio/mpeg"},  {".ogg", "audio/ogg"}, {".oga", "audio/ogg"}, {".opus", "audio/opus"}, {".webm", "audio/webm"},
    {".m4a", "audio/mp4"},   {".aac", "audio/aac"}, {".wav", "audio/wav"}, {".flac", "audio/flac"},
};

// The extension of an address's path, in small letters: ".mp3".
std::string extension_of(const std::string &address) {
	size_t host = address.find("://");
	size_t path = host == std::string::npos ? std::string::npos : address.find('/', host + 3);
	if (path == std::string::npos) return {};
	std::string file = address.substr(path, address.find_first_of("?#", path) - path);
	size_t dot = file.rfind('.'), slash = file.rfind('/');
	return dot == std::string::npos || dot < slash ? std::string() : lower(file.substr(dot));
}

// The sound a fetch brought, when it brought one.
bool audio_of(const Fetched &fetched, WordSound &sound) {
	if (fetched.body.empty()) return false;
	std::string type = fetched.type;
	if (type.empty() || type == "application/octet-stream") {
		// A file server that names no type: the address's extension says what it is.
		std::string extension = extension_of(fetched.address);
		type.clear();
		for (const auto &known : kTypeOfExtension)
			if (extension == known[0]) type = known[1];
		if (type.empty()) return false;
	}
	for (const Type &known : kTypes) {
		if (type != known.name) continue;
		sound = WordSound{fetched.body, known.type, known.extension};
		return true;
	}
	if (!starts_with(type, "audio/")) return false;
	sound = WordSound{fetched.body, type, "audio"};
	return true;
}

// A name cut to its first kMaxSoundName characters.
std::string cut(const std::string &name) {
	std::u32string text = utf8::decode(name);
	return text.size() > kMaxSoundName ? utf8::encode(text.substr(0, kMaxSoundName)) : name;
}

// The sounds of Yomitan's list, in its order, each with a name of its own:
// the list's name, "Sound 2" where it has none, and "nhk16 (2)" for a name
// said before. False when the answer is not such a list.
bool sounds_of(const Fetched &fetched, std::vector<NamedSound> &sounds) {
	sounds.clear();
	json::Value root;
	if (fetched.body.size() > kMaxSoundListBytes || !json::parse(fetched.body, root) || !root.is_object() ||
	    root.str("type") != "audioSourceList" || !root["audioSources"].is_array())
		return false;
	std::set<std::string> names;
	for (const json::Value &item : root["audioSources"].items()) {
		if (!item.is_object() || !item["url"].is_string()) continue;
		std::string address = resolved(fetched.address, item["url"].as_string());
		if (address.empty()) continue;
		std::string given = cut(trimmed(item.str("name")));
		std::string plain = given.empty() ? "Sound " + std::to_string(sounds.size() + 1) : given;
		std::string unique = plain;
		for (int count = 2; !names.insert(unique).second; ++count) unique = plain + " (" + std::to_string(count) + ")";
		sounds.push_back(NamedSound{unique, address});
	}
	return true;
}

// Asks the source about a word and reads its first answer.
WordAudioOutcome ask(const HttpFetch &fetch, const std::string &address, const std::string &term_typed, const std::string &reading_typed,
                     WordAudioOutcome nothing, const std::function<WordAudioOutcome(const Fetched &)> &read) {
	if (address.empty() || !fetch) return WordAudioOutcome::NoSource;
	std::string term = trimmed(term_typed), reading = trimmed(reading_typed);
	if (reading.empty()) reading = term;
	std::string filled = term.empty() ? std::string() : fill_audio_address(address, term, reading);
	if (filled.empty()) return nothing;
	Fetched first;
	return get(fetch, filled, first) ? read(first) : WordAudioOutcome::Failed;
}

}  // namespace

bool read_audio_address(const std::string &typed, std::string &address, std::string &problem) {
	address = trimmed(typed);
	problem.clear();
	if (address.find("{term}") == std::string::npos) {
		problem = "The address needs {term} where the word goes, as in Yomitan's custom audio sources.";
		return false;
	}
	if (fill_audio_address(address, "x", "x").empty()) {
		problem = "That is not an http or https address.";
		return false;
	}
	return true;
}

std::string fill_audio_address(const std::string &address, const std::string &term, const std::string &reading) {
	std::string filled = address;
	replace_all(filled, "{term}", percent(term));
	replace_all(filled, "{reading}", percent(reading));
	replace_all(filled, "{language}", "ja");
	return is_web_address(filled) ? filled : std::string();
}

WordAudioOutcome find_word_audio(const HttpFetch &fetch, const std::string &address, const std::string &term,
                                 const std::string &reading, const std::string &source, WordSound &sound) {
	return ask(fetch, address, term, reading, WordAudioOutcome::NotFound, [&](const Fetched &first) {
		// A "Custom URL" source answers with the sound itself.
		if (audio_of(first, sound)) return source.empty() || source == kDirectSoundName ? WordAudioOutcome::Found : WordAudioOutcome::NotFound;
		std::vector<NamedSound> sounds;
		if (!sounds_of(first, sounds)) return WordAudioOutcome::Failed;
		int tried = 0;
		for (const NamedSound &named : sounds) {
			if (!source.empty() && named.name != source) continue;
			if (tried++ == kMaxSoundTries) break;
			Fetched fetched;
			if (get(fetch, named.address, fetched) && audio_of(fetched, sound)) return WordAudioOutcome::Found;
			if (!source.empty()) break;
		}
		return WordAudioOutcome::NotFound;
	});
}

WordAudioOutcome word_audio_choices(const HttpFetch &fetch, const std::string &address, const std::string &term,
                                    const std::string &reading, std::vector<std::string> &names) {
	names.clear();
	return ask(fetch, address, term, reading, WordAudioOutcome::Found, [&](const Fetched &first) {
		WordSound sound;
		if (audio_of(first, sound)) {
			names.push_back(kDirectSoundName);
			return WordAudioOutcome::Found;
		}
		std::vector<NamedSound> sounds;
		if (!sounds_of(first, sounds)) return WordAudioOutcome::Failed;
		for (const NamedSound &named : sounds) names.push_back(named.name);
		return WordAudioOutcome::Found;
	});
}

}  // namespace ore
