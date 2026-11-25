// What goes into each field of a card.
#include "mining/anki_fields.h"

#include <algorithm>
#include <regex>
#include <set>

namespace ore {

namespace {

// How many note types are looked into at most, so that a large collection is not asked for long.
const size_t kMostLookedInto = 30;

const char *const kLapisSignature[] = {"Expression", "ExpressionFurigana", "ExpressionReading", "ExpressionAudio",
                                       "SelectionText", "MainDefinition",     "Sentence",          "SentenceAudio",
                                       "Picture",       "IsSentenceCard",     "MiscInfo"};

struct Guess {
	std::regex pattern;
	const char *role;
	bool also;  // a part that also goes into a second field
};

const std::vector<Guess> &guesses() {
	static const std::vector<Guess> list = [] {
		const std::string word = "(expression|word|vocab(ulary)?|term)";
		auto make = [](const std::string &pattern, const char *role, bool also = false) {
			return Guess{std::regex(pattern, std::regex::ECMAScript | std::regex::icase), role, also};
		};
		return std::vector<Guess>{
		    make("^sentence([ _-]?(kanji|text|jp|ja|japanese))?$", "sentence"),
		    make("^((sentence[ _-]?)?audio|sentence[ _-]?sound|sound)$", "sound"),
		    make("^(picture|image|screenshot|snapshot)$", "picture"),
		    make("^(misc[ _-]?info|source|sentence[ _-]?source)$", "source"),
		    make("^(" + word + "|key)$", "word"),
		    make("^(" + word + "[ _-]?reading|reading|kana)$", "reading"),
		    make("^(" + word + "[ _-]?furigana|furigana)$", "furigana"),
		    make("^(" + word + "[ _-]?(audio|sound)|word[ _-]?pronunciation)$", "wordAudio"),
		    make("^(main[ _-]?definition|definition|meaning|glossary|" + word + "[ _-]?(meaning|definition|english))$", "meaning"),
		    make("^(selection[ _-]?text|selected[ _-]?(text|definition))$", "selectionText"),
		    make("^(freq(uency)?|frequencies)$", "frequency"),
		    // Lapis sorts new cards by it: the same number as the frequency.
		    make("^freq(uency)?[ _-]?sort$", "frequency", true),
		    make("^(pitch([ _-]?(position|accent))?|accent)$", "pitch"),
		    make("^pitch[ _-]?(categories|category|pattern)$", "pitchCategories"),
		};
	}();
	return list;
}

std::string trimmed(const std::string &s) {
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
	return s.substr(a, b - a);
}

bool lower_contains(const std::string &name, const char *part) {
	std::string lower = name;
	for (char &c : lower)
		if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return lower.find(part) != std::string::npos;
}

// Where cards go when no card says: the one deck made besides Default, or Default alone.
std::string only_deck(const std::vector<std::string> &decks) {
	std::vector<std::string> made;
	bool has_default = false;
	for (const std::string &deck : decks) {
		if (deck == "Default")
			has_default = true;
		else
			made.push_back(deck);
	}
	if (made.size() == 1) return made[0];
	return made.empty() && has_default ? "Default" : "";
}

}  // namespace

const std::vector<FieldRole> &field_roles() {
	static const std::vector<FieldRole> list = {
	    {"none", "Nothing", "left empty"},
	    {"sentence", "Sentence", "the line, with the word in bold (昨日の話をしよう)"},
	    {"sound", "SentenceAudio", "the voice of the line"},
	    {"picture", "Picture", "the screen as the line stood on it"},
	    {"source", "MiscInfo", "the game's title"},
	    {"word", "Expression", "the word looked up (食べる)"},
	    {"reading", "ExpressionReading", "its reading (たべる)"},
	    {"furigana", "ExpressionFurigana", "the word with its reading (食[た]べる)"},
	    {"wordAudio", "ExpressionAudio", "how the word sounds"},
	    {"meaning", "MainDefinition", "its meanings in your first dictionary"},
	    {"selectionText", "SelectionText", "what you select in the popup"},
	    {"frequency", "Frequency", "how common it is (512)"},
	    {"pitch", "PitchPosition", "its pitch accent (2)"},
	    {"pitchCategories", "PitchCategories", "its pitch pattern (nakadaka)"},
	};
	return list;
}

bool is_field_role(const std::string &role) {
	for (const FieldRole &choice : field_roles())
		if (role == choice.role) return true;
	return false;
}

bool is_media_role(const std::string &role) {
	return role == "picture" || role == "sound" || role == "wordAudio";
}

std::vector<std::pair<std::string, std::string>> roles_for(const std::vector<std::string> &field_names, const json::Value &chosen) {
	auto kept = [&](const std::string &name) -> const json::Value * {
		const json::Value *role = chosen.find(name);
		return role && role->is_string() && is_field_role(role->as_string()) ? role : nullptr;
	};
	std::set<std::string> taken;
	for (const std::string &name : field_names)
		if (const json::Value *role = kept(name)) taken.insert(role->as_string());
	std::vector<std::pair<std::string, std::string>> roles;
	for (const std::string &name : field_names) {
		if (const json::Value *role = kept(name)) {
			roles.emplace_back(name, role->as_string());
			continue;
		}
		const Guess *guess = nullptr;
		std::string plain = trimmed(name);
		for (const Guess &candidate : guesses())
			if (std::regex_match(plain, candidate.pattern)) {
				guess = &candidate;
				break;
			}
		if (!guess) {
			roles.emplace_back(name, "none");
		} else if (guess->also) {
			roles.emplace_back(name, guess->role);
		} else {
			roles.emplace_back(name, taken.count(guess->role) ? "none" : guess->role);
			taken.insert(guess->role);
		}
	}
	return roles;
}

bool is_lapis(const std::vector<std::string> &field_names) {
	for (const char *wanted : kLapisSignature)
		if (std::find(field_names.begin(), field_names.end(), wanted) == field_names.end()) return false;
	return true;
}

bool find_lapis(CardTarget &anki, const CardPlaces &places, FoundNoteType &found, AnkiProblem &problem) {
	problem = AnkiProblem();
	// Lapis first by name, then Kiku and the rest, each in the order Anki gives them.
	auto rank = [](const std::string &name) { return name == "Lapis" ? 0 : lower_contains(name, "lapis") ? 1 : lower_contains(name, "kiku") ? 2 : 3; };
	std::vector<std::string> ordered = places.note_types;
	std::stable_sort(ordered.begin(), ordered.end(), [&](const std::string &a, const std::string &b) { return rank(a) < rank(b); });
	if (ordered.size() > kMostLookedInto) ordered.resize(kMostLookedInto);

	for (const std::string &note_type : ordered) {
		std::vector<std::string> names;
		if (!anki.field_names(note_type, names, problem)) {
			if (problem.unreachable) return false;
			continue;
		}
		if (!is_lapis(names)) continue;
		std::string deck;
		if (!anki.deck_of_newest(note_type, places.decks, deck, problem)) return false;
		problem = AnkiProblem();
		found.note_type = note_type;
		found.deck = deck.empty() ? only_deck(places.decks) : deck;
		found.field_names = std::move(names);
		return true;
	}
	problem = AnkiProblem();
	return false;
}

}  // namespace ore
