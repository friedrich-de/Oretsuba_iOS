// Where a mined card goes. The kit makes a card of the word, the line, its
// voice and its picture; a target puts it into an Anki: an AnkiConnect over
// HTTP (mining/anki_connect.h), or whatever else a host gives the kit.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ore {

// Why Anki did not do what was asked, in words for the player.
struct AnkiProblem {
	std::string message;
	bool unreachable = false;  // Anki could not be reached at all, as opposed to saying no
	std::string said;          // Anki's own words, when it answered with an error
};

// A file of a card: the line's picture, its voice, or how the word sounds.
struct CardMedia {
	std::string role;                 // "picture", "sound" or "wordAudio"
	std::string name;                 // the name it should get among Anki's media
	std::string data;                 // its bytes
	std::string type;                 // "image/jpeg", "audio/mp4"
	std::vector<std::string> fields;  // the fields it goes into
};

// A card as the kit hands it over. The fields of text are filled in; those of
// the media are empty, and the media come as bytes: the target puts them where
// its Anki wants them and writes into their fields what refers to them.
struct CardNote {
	std::string deck, note_type;
	std::vector<std::pair<std::string, std::string>> fields;  // in the order of the note type
	std::vector<std::string> tags;
	bool allow_duplicate = false;
	std::vector<CardMedia> media;
};

// The decks and note types of an Anki.
struct CardPlaces {
	std::string version;  // of what answers, for the settings to show; may be empty
	std::vector<std::string> decks, note_types;
};

// Whether an Anki has a card already.
enum class Known { No, Yes, CannotSay };

// What adding a card came to.
struct AddOutcome {
	// The card was handed to another app, which adds it on its own: whether it
	// did is told later, if at all (MiningKit::emit).
	bool handed_over = false;
	double note_id = 0;  // when the target knows it
};

// Each call waits for its answer and may come from any thread. False with the
// reason in `problem`.
class CardTarget {
public:
	virtual ~CardTarget() = default;

	virtual bool places(CardPlaces &places, AnkiProblem &problem) = 0;
	virtual bool field_names(const std::string &note_type, std::vector<std::string> &names, AnkiProblem &problem) = 0;
	// The deck the newest card of a note type is in, for a first choice of deck;
	// empty when the target cannot say.
	virtual bool deck_of_newest(const std::string &note_type, const std::vector<std::string> &decks, std::string &deck,
	                            AnkiProblem &problem) {
		deck.clear();
		problem = AnkiProblem();
		return true;
	}
	// For each note, whether its deck has a card like it already. The media are not looked at.
	virtual bool known(const std::vector<CardNote> &notes, std::vector<Known> &answers, AnkiProblem &problem) = 0;
	virtual bool add(const CardNote &note, AddOutcome &outcome, AnkiProblem &problem) = 0;
};

// What a field of a picture or of a sound holds once the file is in Anki's media under `name`.
std::string picture_field(const std::string &name);
std::string sound_field(const std::string &name);

}  // namespace ore
