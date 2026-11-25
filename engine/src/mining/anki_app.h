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
#include <memory>
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

}  // namespace ore
