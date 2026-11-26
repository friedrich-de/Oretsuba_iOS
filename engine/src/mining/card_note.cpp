// A card made of a word looked up in the game.
#include "mining/card_note.h"

#include <algorithm>

#include "mining/anki_fields.h"
#include "util/utf8.h"

namespace ore {

namespace {

std::string escaped(const std::string &text, bool breaks) {
	std::string out;
	for (size_t i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c == '&') {
			out += "&amp;";
		} else if (c == '<') {
			out += "&lt;";
		} else if (c == '>') {
			out += "&gt;";
		} else if (c == '"') {
			out += "&quot;";
		} else if (breaks && c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
			// The line feed after it is the break.
		} else if (breaks && c == '\n') {
			out += "<br>";
		} else {
			out += c;
		}
	}
	return out;
}

// How many bytes of blanks a text starts with, the full-width and the no-break one among them.
size_t blank_at(const std::string &s, size_t at) {
	unsigned char c = static_cast<unsigned char>(s[at]);
	if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') return 1;
	if (s.compare(at, 3, "\xE3\x80\x80") == 0) return 3;
	if (s.compare(at, 2, "\xC2\xA0") == 0) return 2;
	return 0;
}

std::string trim_start(const std::string &s) {
	size_t a = 0;
	while (a < s.size()) {
		size_t n = blank_at(s, a);
		if (n == 0) break;
		a += n;
	}
	return s.substr(a);
}

std::string trim_end(const std::string &s) {
	size_t b = s.size();
	for (;;) {
		if (b >= 1 && blank_at(s, b - 1) == 1)
			b -= 1;
		else if (b >= 3 && blank_at(s, b - 3) == 3)
			b -= 3;
		else if (b >= 2 && blank_at(s, b - 2) == 2)
			b -= 2;
		else
			break;
	}
	return s.substr(0, b);
}

std::string trim(const std::string &s) {
	return trim_end(trim_start(s));
}

// The place in bytes of a place counted in UTF-16 units; the end of the text when it is past it.
size_t byte_offset(const std::string &text, int units) {
	const char *p = text.data(), *end = p + text.size();
	int at = 0;
	while (p < end && at < units) {
		uint32_t c = utf8::next(p, end);
		at += c >= 0x10000 ? 2 : 1;
	}
	return size_t(p - text.data());
}

void replace_all(std::string &text, const char *what, const char *with) {
	size_t n = std::string(what).size(), m = std::string(with).size();
	for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + m)) text.replace(at, n, with);
}

bool is_media(const std::string &role) {
	return is_media_role(role);
}

// What a field of text gets; false when the word has nothing for it.
bool value_for(const std::string &role, const LineContent &line, const WordContent &word, std::string &value) {
	if (role == "sentence")
		value = sentence_with_word(line.sentence, word.start, word.length);
	else if (role == "word")
		value = escaped(word.expression, false);
	else if (role == "reading")
		value = escaped(word.reading, false);
	else if (role == "furigana")
		value = escaped(word.furigana, false);
	else if (role == "meaning")
		value = word.meaning;
	else if (role == "selectionText")
		value = field_html(word.selection);
	else if (role == "frequency")
		value = word.frequency;
	else if (role == "pitch")
		value = word.pitch;
	else if (role == "pitchCategories")
		value = word.pitch_categories;
	else if (role == "source" && !trim(line.source).empty())
		value = field_html(line.source);
	else
		return false;
	return true;
}

}  // namespace

WordContent WordContent::from_json(const json::Value &value) {
	WordContent word;
	word.expression = value.str("expression");
	word.reading = value.str("reading");
	word.furigana = value.str("furigana");
	word.meaning = value.str("meaning");
	word.frequency = value.str("frequency");
	word.pitch = value.str("pitch");
	word.pitch_categories = value.str("pitchCategories");
	word.selection = value.str("selection");
	const json::Value &at = value["at"];
	int n = 0;
	if (at["start"].as_int(n)) word.start = n;
	if (at["length"].as_int(n)) word.length = n;
	return word;
}

std::string field_html(const std::string &text) {
	return escaped(trim(text), true);
}

std::string sentence_with_word(const std::string &sentence, int start, int length) {
	if (start < 0 || length <= 0) return field_html(sentence);
	size_t from = byte_offset(sentence, start), to = byte_offset(sentence, start + length);
	if (from >= to) return field_html(sentence);
	return escaped(trim_start(sentence.substr(0, from)), true) + "<b>" + escaped(sentence.substr(from, to - from), true) + "</b>" +
	       escaped(trim_end(sentence.substr(to)), true);
}

std::string plain_text(const std::string &html) {
	std::string text;
	for (size_t i = 0; i < html.size();) {
		if (html[i] != '<') {
			text += html[i++];
			continue;
		}
		size_t end = html.find('>', i);
		if (end == std::string::npos) {
			text += html[i++];
			continue;
		}
		// A break is a blank; every other tag is nothing.
		if (end - i >= 3 && (html[i + 1] == 'b' || html[i + 1] == 'B') && (html[i + 2] == 'r' || html[i + 2] == 'R')) text += ' ';
		i = end + 1;
	}
	replace_all(text, "&nbsp;", " ");
	replace_all(text, "&lt;", "<");
	replace_all(text, "&gt;", ">");
	replace_all(text, "&quot;", "\"");
	replace_all(text, "&amp;", "&");
	// Runs of blanks as one.
	std::string joined;
	bool blank = false;
	for (size_t i = 0; i < text.size();) {
		size_t n = blank_at(text, i);
		if (n) {
			blank = true;
			i += n;
			continue;
		}
		if (blank && !joined.empty()) joined += ' ';
		blank = false;
		joined += text[i++];
	}
	std::u32string characters = utf8::decode(joined);
	if (characters.size() > 40) return utf8::encode(characters.substr(0, 40)) + "\xE2\x80\xA6";
	return joined;
}

std::string card_problem(const std::string &deck, const std::string &note_type,
                         const std::vector<std::pair<std::string, std::string>> &roles) {
	if (deck.empty() || note_type.empty()) return "Choose a deck and a note type in the settings first.";
	for (const auto &role : roles)
		if (role.second == "word") return {};
	return "Give the word a field in the settings first.";
}

bool compose_note(const std::string &deck, const std::string &note_type, const std::vector<std::pair<std::string, std::string>> &roles,
                  const std::vector<std::string> &tags, const LineContent &line, const WordContent &word, CardNote &note,
                  std::string &problem) {
	problem = card_problem(deck, note_type, roles);
	if (!problem.empty()) return false;
	note = CardNote();
	note.deck = deck;
	note.note_type = note_type;
	note.tags = tags;
	for (const auto &role : roles) {
		std::string value;
		if (is_media(role.second)) {
			auto known = std::find_if(note.media.begin(), note.media.end(), [&](const CardMedia &m) { return m.role == role.second; });
			if (known == note.media.end()) {
				note.media.push_back(CardMedia{role.second, {}, {}, {}, {}});
				known = note.media.end() - 1;
			}
			known->fields.push_back(role.first);
		} else {
			value_for(role.second, line, word, value);
		}
		note.fields.emplace_back(role.first, std::move(value));
	}
	// Anki needs the first field, unless a file goes there.
	if (note.fields.empty() || (!is_media(roles[0].second) && plain_text(note.fields[0].second).empty())) {
		problem = "A new \"" + note_type + "\" card needs something in its first field. Give it the word in the settings.";
		return false;
	}
	return true;
}

}  // namespace ore
