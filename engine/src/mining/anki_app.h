// Cards for the Anki app on the same device (docs/MINING.md): AnkiMobile on
// iPhone and iPad, AnkiDroid on Android. This file is the part that knows
// neither the engine nor the system: the links AnkiMobile takes, what it
// answers, and the small web server that hands a card's picture and sound
// over to it.
//
// AnkiMobile has no interface but links (docs.ankiweb.net, "URL Schemes"):
//   anki://x-callback-url/infoForAdding?x-success=LINK
//       leaves the decks and note types as JSON on the clipboard
//   anki://x-callback-url/addnote?type=..&deck=..&fldNAME=..&x-success=LINK
//       adds one note; a field whose whole value is a web link that ends in a
//       picture's or a sound's file ending is fetched and kept as that file
// Either one brings AnkiMobile to the front, and it opens LINK when it is
// done. Nothing comes back when it is not: no error, no "already there".
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ore {

// ---------------------------------------------------------------- what an Anki app holds

struct AnkiAppInfo {
	struct NoteType {
		std::string name;
		std::vector<std::string> fields;
	};
	std::vector<std::string> profiles;  // AnkiMobile only
	std::vector<std::string> decks;
	std::vector<NoteType> note_types;

	const NoteType *note_type(std::string_view name) const;
};

// What AnkiMobile leaves on the clipboard (type "net.ankimobile.json") after
// infoForAdding. False with the reason in `problem` when it cannot be read or
// names no deck or no note type.
bool ankimobile_parse_info(std::string_view json, AnkiAppInfo &out, std::string &problem);

// The same lists as the kit keeps them in its settings, and back.
std::string anki_app_info_text(const AnkiAppInfo &info);
bool anki_app_info_from_text(std::string_view json, AnkiAppInfo &out);

// ---------------------------------------------------------------- links

inline constexpr char kAnkiMobileClipboardType[] = "net.ankimobile.json";
// The link that tells whether AnkiMobile is on the device (it only has to open).
inline constexpr char kAnkiMobileProbeLink[] = "anki://";

// A text as one value of a link's query: everything but letters, digits and -._~ as %XX.
std::string link_escape(std::string_view text);
// And back; a + stays a +. Broken %XX stay as they are.
std::string link_unescape(std::string_view text);
// The value of `name` in the query of a link; false when the link has none.
bool link_query_value(std::string_view link, std::string_view name, std::string &out);

struct AnkiLinkNote {
	std::string deck, note_type;
	std::string profile;  // empty = whichever profile is open
	std::vector<std::pair<std::string, std::string>> fields;  // name, HTML; empty ones are left out
	std::vector<std::string> tags;
	bool allow_duplicate = false;
};

// The link that asks AnkiMobile for its decks and note types.
std::string ankimobile_info_link(std::string_view success_link);
// The link that adds `note`.
std::string ankimobile_add_link(const AnkiLinkNote &note, std::string_view success_link);

// Whether AnkiMobile fetches a file of this name from a link. It goes by the
// ending; these are the endings Anki itself counts as pictures and sound.
bool anki_media_name(std::string_view file_name);
// "image/jpeg" for "shot.jpg"; "application/octet-stream" for an ending it does not know.
const char *anki_media_type(std::string_view file_name);
// A file name that is safe in a link's path and in Anki's media folder:
// letters, digits, - _ and . stay, everything else becomes _.
std::string anki_media_safe_name(std::string_view file_name);

// ---------------------------------------------------------------- the files of a card, for AnkiMobile to fetch

// A web server for this device alone (127.0.0.1 and ::1, a port the system
// picks). It answers GET for the files it was given and nothing else, and
// every file sits behind a path nobody can guess. AnkiMobile fetches from it
// while this app is in the background, so the host must keep the app running
// for that moment (a background task on iOS).
class MediaServer {
public:
	MediaServer();
	~MediaServer();
	MediaServer(const MediaServer &) = delete;
	MediaServer &operator=(const MediaServer &) = delete;

	// Starts answering; a server that runs is stopped first and comes back on a
	// new port with nothing to offer. False with the reason in `problem`.
	bool start(std::string &problem);
	void stop();
	bool running() const;
	int port() const;

	// Offers a file and returns its link, "http://localhost:PORT/SECRET/NAME".
	// Empty when the server does not run.
	std::string offer(std::string_view file_name, std::vector<uint8_t> bytes, std::string_view content_type = {});
	// Nothing is offered any more; links handed out before answer 404.
	void clear();
	// How often the file behind `link` was fetched in full.
	int fetched(std::string_view link) const;
	// How many files are offered.
	int offered() const;

private:
	struct State;
	std::shared_ptr<State> state_;
};

// ---------------------------------------------------------------- one card on its way to AnkiMobile

// Takes a card to AnkiMobile and learns what became of it. AnkiMobile is
// handed one link; it fetches the card's files from the media server while it
// adds the note, then opens the way back. When it opens nothing (a word it
// has already and refuses, a note type that is gone, the player turning
// away), the card counts as not confirmed once this app is in front again.
class AnkiMobileHandover {
public:
	struct File {
		std::string name;                 // "oretsuba_3f2a.jpg"
		std::vector<uint8_t> bytes;
		std::vector<std::string> fields;  // the fields it belongs to
	};
	struct Prepared {
		std::string card;                   // names this card in the way back
		std::string link;                   // the link to open
		std::vector<std::string> left_out;  // names of the files AnkiMobile cannot take
	};
	struct Result {
		enum Kind { NotOurs, Added, Unconfirmed };
		Kind kind = NotOurs;
		std::string card;
		std::vector<std::string> not_fetched;  // names of the files AnkiMobile was offered and did not fetch
	};

	// `return_link` is this app's own link that AnkiMobile opens when it is
	// done, such as "oretsuba://anki/added".
	explicit AnkiMobileHandover(std::string return_link);
	~AnkiMobileHandover();

	// Offers the files and writes the link. AnkiMobile keeps a file only in a
	// field that holds nothing else, so a file goes into those of its fields
	// that are empty; one without such a field, or without a media ending, is
	// left out. A card still on its way is given up.
	bool prepare(AnkiLinkNote note, std::vector<File> files, Prepared &out, std::string &problem);
	// A link this app was opened with. Added when it is the way back of the
	// card on its way; the files are then offered no longer.
	Result returned(std::string_view link);
	// This app is in front again and no way back came. Unconfirmed when a card was on its way.
	Result gave_up();
	bool waiting() const;
	// The name of the card on its way; empty when none is.
	std::string card() const;

private:
	Result finish(Result::Kind kind);
	mutable std::mutex mutex_;
	MediaServer server_;
	std::string return_link_;
	std::string card_;
	std::vector<std::pair<std::string, std::string>> offered_;  // name, link
};

// ---------------------------------------------------------------- what the host does for an Anki app

// What the app around the engine can do with the Anki app on this device.
// A host has what its system has: any of these may be empty.
struct AnkiAppHost {
	// For AnkiMobile. Opens a link in another app and keeps this app running
	// while it is away; `done` hears whether an app took the link.
	std::function<void(const std::string &link, std::function<void(bool opened)> done)> open_link;
	// For AnkiMobile. Its answer, taken off the clipboard; empty when none is there.
	std::function<std::string()> take_answer;
	// For AnkiDroid. One request, answered in JSON (AnkiDroid.kt says which).
	std::function<std::string(const std::string &method, const std::string &json)> ankidroid;
};

// The links this app is opened with and its coming back to the front, on
// their way from the host to whoever waits for them. The host calls
// opened_with() and in_front() (ore_app_opened_with, ore_app_in_front).
class AppReturns {
public:
	static AppReturns &shared();
	// `link` is empty when the app is merely in front again.
	using Listener = std::function<void(const std::string &link)>;
	int listen(Listener listener);
	void forget(int id);
	void opened_with(const std::string &link);
	void in_front();

private:
	void tell(const std::string &link);
	std::mutex mutex_;
	int next_ = 1;
	std::vector<std::pair<int, std::shared_ptr<Listener>>> listeners_;
};

// ---------------------------------------------------------------- AnkiMobile, from asking to knowing

// Everything this app does with AnkiMobile: asking for its decks and note
// types, handing a card over, and learning how each ended. Both leave this
// app, so both answer later, through `tell`:
//
//   anki.lists        {"ok":true,"lists":{profiles,decks,noteTypes}}  or  {"ok":false,"problem":".."}
//   card.added        {"card":"..","notFetched":["file", ..]}
//   card.unconfirmed  {"card":"..","reason":".."}
//
// `tell` is called from a thread of this object's or from the host's.
class AnkiMobile {
public:
	using Tell = std::function<void(const std::string &event, const std::string &json)>;
	struct Timing {
		int confirm_ms = 2000;      // in front again this long without a way back: the card is not confirmed
		int answer_tries = 4;       // how often the clipboard is looked at for the lists
		int answer_wait_ms = 800;   // before each look: AnkiMobile's answer takes a moment to arrive
	};

	// `scheme` is this app's own link scheme, "oretsuba".
	AnkiMobile(AnkiAppHost host, std::string scheme, Tell tell);
	AnkiMobile(AnkiAppHost host, std::string scheme, Tell tell, Timing timing);
	~AnkiMobile();
	AnkiMobile(const AnkiMobile &) = delete;
	AnkiMobile &operator=(const AnkiMobile &) = delete;

	// Whether this host can open links and read the clipboard at all.
	bool possible() const;
	// Asks AnkiMobile for its decks and note types. False with the reason when it cannot even be asked.
	bool ask_lists(std::string &problem);
	// Hands a card over; `card` gets the name the later event carries, `left_out` the files AnkiMobile cannot take.
	bool add(AnkiLinkNote note, std::vector<AnkiMobileHandover::File> files, std::string &card,
	         std::vector<std::string> &left_out, std::string &problem);

private:
	struct State;
	std::shared_ptr<State> state_;
};

// ---------------------------------------------------------------- AnkiDroid

// A card as AnkiDroid.kt takes it ("add"). The files must wait in the folder
// "status" names, under the names given here.
struct AnkiDroidFile {
	std::string name;
	bool sound = false;
	std::vector<std::string> fields;
};
std::string ankidroid_add_request(const AnkiLinkNote &note, const std::vector<AnkiDroidFile> &files);
std::string ankidroid_known_request(std::string_view note_type, std::string_view field, std::string_view value);

// What AnkiDroid.kt answers. `problem` and `code` are set when it answered with an error or with no JSON.
struct AnkiDroidAnswer {
	bool ok = false;
	std::string problem, code;
	// status
	bool installed = false, allowed = false;
	std::string media_folder;
	// info
	AnkiAppInfo info;
	// known
	bool known = false;
	// add
	std::string id;
	std::vector<std::string> missing;
};
AnkiDroidAnswer ankidroid_answer(std::string_view json);

}  // namespace ore
