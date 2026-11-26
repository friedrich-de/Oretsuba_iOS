// A card made of a word looked up in the game: what goes into each of its
// fields from the word, the line it stood in and the choices of the settings.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "mining/card_target.h"
#include "util/json.h"

namespace ore {

// What a card gets from a word looked up in the popup, as the popup's page gives it.
struct WordContent {
	std::string expression, reading;
	std::string furigana;          // in Anki's furigana: 食[た]べる
	std::string meaning;           // its definitions in its first dictionary, as HTML
	std::string frequency;         // as a number, "" without one
	std::string pitch;             // "0" or "2, 3", "" without one
	std::string pitch_categories;  // "heiban" or "atamadaka,nakadaka", "" without one
	std::string selection;         // what was selected in the popup when the card was made
	// Where the word is in the sentence, in the units a page counts text in (UTF-16).
	int start = -1, length = 0;

	static WordContent from_json(const json::Value &value);
};

// What a card gets from the line.
struct LineContent {
	std::string sentence;  // a line feed between its lines
	std::string source;    // the game's title
};

// Text for a field: escaped, with its line feeds as breaks.
std::string field_html(const std::string &text);
// The sentence for a field with the word in bold, as Yomitan writes it.
std::string sentence_with_word(const std::string &sentence, int start, int length);
// A field's HTML as plain text, to name a card: at most 40 characters.
std::string plain_text(const std::string &html);

// Why a card of a word cannot be made with these choices; empty when it can.
std::string card_problem(const std::string &deck, const std::string &note_type,
                         const std::vector<std::pair<std::string, std::string>> &roles);

// Makes the card: its fields of text filled in, and for each kind of file a
// field wants an entry in `note.media` without its bytes yet. False with the
// reason in `problem`.
bool compose_note(const std::string &deck, const std::string &note_type, const std::vector<std::pair<std::string, std::string>> &roles,
                  const std::vector<std::string> &tags, const LineContent &line, const WordContent &word, CardNote &note,
                  std::string &problem);

}  // namespace ore
