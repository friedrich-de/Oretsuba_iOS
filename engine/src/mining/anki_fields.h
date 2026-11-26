// What goes into each field of a card: the parts a mined card has, which
// field of a note type gets which by its name, and finding a Lapis note type
// in an Anki so that a new player has nothing to choose.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "mining/card_target.h"
#include "util/json.h"

namespace ore {

// A part of a card, by the name the settings keep it under.
struct FieldRole {
	const char *role;    // "sentence"
	const char *label;   // as Lapis names its field: "Sentence"
	const char *detail;  // what it holds, with an example
};

// The choices for a field, "none" first.
const std::vector<FieldRole> &field_roles();
bool is_field_role(const std::string &role);
// The parts a card gets as files in Anki's media folder.
bool is_media_role(const std::string &role);

// What goes into each field of a note type as it is now: the choices made
// before (`chosen`, an object of field name to role) for the fields still
// there, and guesses by name for new fields, for parts no field has yet: the
// sentence into "Sentence", the voice into "SentenceAudio", the word into
// "Expression", and so on, as Lapis and most mining note types name them. Each
// part goes into one field at most, but for the frequency, which Lapis also
// sorts by ("FreqSort"). The result is in the order of the fields.
std::vector<std::pair<std::string, std::string>> roles_for(const std::vector<std::string> &field_names, const json::Value &chosen);

// Whether the fields are those of Lapis, whatever the note type is called:
// Lapis itself, Kiku, which is built on it, and the copies people make.
bool is_lapis(const std::vector<std::string> &field_names);

// A note type, deck and fields chosen for someone who has not chosen any.
struct FoundNoteType {
	std::string note_type;
	std::string deck;  // of the newest card of that note type, or the one deck there is; "" when unsure
	std::vector<std::string> field_names;
};

// A Lapis note type in this Anki. False when there is none; `problem` then
// says so only when Anki could not be asked (problem.unreachable).
bool find_lapis(CardTarget &anki, const CardPlaces &places, FoundNoteType &found, AnkiProblem &problem);

}  // namespace ore
