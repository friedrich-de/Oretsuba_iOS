// The Anki app on the same device as a place the mining kit's cards go
// (mining/card_target.h): AnkiMobile on iPhone and iPad, AnkiDroid on Android.
// What each app can and cannot do is told in mining/anki_app.h; this file
// fits them to the kit.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "dict/kit_config.h"
#include "mining/anki_app.h"
#include "mining/card_target.h"

namespace ore {

// The names the setting anki.target knows these places by.
inline constexpr char kAnkiMobileTarget[] = "ankimobile";
inline constexpr char kAnkiDroidTarget[] = "ankidroid";

// Cards to AnkiMobile. It is reached by links alone, and each link brings it
// to the front, so:
//   - its decks and note types are fetched when the player asks (fetch) and
//     kept in a file; places() and field_names() answer from that file
//   - it cannot be asked whether it has a word: known() cannot say
//   - add() hands the card over; whether AnkiMobile took it is told later
// What is told later goes to `tell`, each time with a sentence for the player:
//   anki.lists        {"ok":true|false,"message":".."}
//   card.added        {"card":"..","message":"..","missing":["picture"|"sound"|"wordAudio", ..]}
//   card.unconfirmed  {"card":"..","message":".."}
// `missing` names the files the card went without. One card is on its way at
// a time: a new one gives the one before up.
class AnkiMobileTarget : public CardTarget {
public:
	using Tell = std::function<void(const std::string &event, const std::string &json)>;

	// `scheme` is this app's own link scheme ("oretsuba"); `lists_file` keeps
	// what AnkiMobile last said it has.
	AnkiMobileTarget(AnkiAppHost host, std::string scheme, std::string lists_file, Tell tell,
	                 AnkiMobile::Timing timing = AnkiMobile::Timing());
	~AnkiMobileTarget() override;

	// Whether this host can reach AnkiMobile at all.
	bool possible() const;
	// Opens AnkiMobile for its decks and note types; anki.lists tells how it went.
	bool fetch(AnkiProblem &problem);

	bool places(CardPlaces &places, AnkiProblem &problem) override;
	bool field_names(const std::string &note_type, std::vector<std::string> &names, AnkiProblem &problem) override;
	bool known(const std::vector<CardNote> &notes, std::vector<Known> &answers, AnkiProblem &problem) override;
	bool add(const CardNote &note, AddOutcome &outcome, AnkiProblem &problem) override;

private:
	struct State;
	std::shared_ptr<State> state_;
	std::unique_ptr<AnkiMobile> mobile_;  // after state_: it goes first, and with it the last thing told
};

// Cards to AnkiDroid, which answers at once (AnkiDroid.kt): every call here
// is one or two requests to it.
class AnkiDroidTarget : public CardTarget {
public:
	explicit AnkiDroidTarget(AnkiAppHost host);

	bool possible() const;
	// Whether AnkiDroid is on the device, and whether the player has let this app at its cards.
	bool status(bool &installed, bool &allowed, AnkiProblem &problem);

	bool places(CardPlaces &places, AnkiProblem &problem) override;
	bool field_names(const std::string &note_type, std::vector<std::string> &names, AnkiProblem &problem) override;
	bool known(const std::vector<CardNote> &notes, std::vector<Known> &answers, AnkiProblem &problem) override;
	bool add(const CardNote &note, AddOutcome &outcome, AnkiProblem &problem) override;

private:
	bool ask(const std::string &method, const std::string &json, AnkiDroidAnswer &answer, AnkiProblem &problem);
	AnkiAppHost host_;
};

#if ORE_MINING_KIT
class MiningKit;

// Gives the kit the Anki apps this host can reach, as places for its cards
// (kAnkiMobileTarget, kAnkiDroidTarget), and the calls the settings make for them:
//
//   anki.targets      -> [{"name":"connect","label":"AnkiConnect","detail":".."}, ..]
//                        the places this device has, for the choice "Send cards to"
//   ankimobile.fetch  -> {"asked":true}; the event anki.lists tells how it went
//   ankidroid.status  -> {"installed":bool,"allowed":bool}
//
// What AnkiMobile tells later goes out through MiningKit::emit. `scheme` is
// this app's own link scheme. Call it once, right after the kit is opened.
void add_anki_apps(MiningKit &kit, const AnkiAppHost &host, const std::string &scheme,
                   AnkiMobile::Timing timing = AnkiMobile::Timing());
#endif

}  // namespace ore
