// Cards for the Anki app on the same device (docs/MINING.md): the links
// AnkiMobile takes, the lists it answers with, and the web server that hands
// a card's picture and sound over. Needs no game files, no Apple system and
// no Anki, so it also builds on its own:
//
//   c++ -std=c++17 -Isrc -Ithird_party tests/anki_app_test.cpp src/mining/anki_app.cpp src/util/json.cpp -lpthread
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mining/anki_app.h"
#include "pretend_ankimobile.h"

using namespace ore;
using namespace ore_test;

namespace {

bool ok = true;
int checks = 0;

void check(bool value, const char *message) {
	++checks;
	if (!value) {
		fprintf(stderr, "FAIL: %s\n", message);
		ok = false;
	}
}

const char U8_DECK[] = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e::\xe3\x83\x9e\xe3\x82\xa4\xe3\x83\x8b\xe3\x83\xb3\xe3\x82\xb0";  // 日本語::マイニング
const char U8_WORD[] = "\xe9\xa3\x9f\xe3\x81\xb9\xe3\x82\x8b";  // 食べる
const char U8_WORD_ESCAPED[] = "%E9%A3%9F%E3%81%B9%E3%82%8B";

// ---------------------------------------------------------------- links

void test_escape() {
	check(link_escape("AZaz09-._~") == "AZaz09-._~", "letters, digits and -._~ stay as they are");
	check(link_escape("a b&c=d?e#f+g/h:i%j") == "a%20b%26c%3Dd%3Fe%23f%2Bg%2Fh%3Ai%25j",
	      "everything that means something in a link is escaped, a blank as %20 and a plus as %2B");
	check(link_escape(U8_WORD) == U8_WORD_ESCAPED, "Japanese goes byte by byte");
	check(link_escape("<b>x</b><br>\"y\"\n") == "%3Cb%3Ex%3C%2Fb%3E%3Cbr%3E%22y%22%0A", "HTML and a line end are escaped");
	check(link_escape("") == "", "nothing stays nothing");

	std::string all;
	for (int c = 1; c < 256; ++c) all += char(c);
	check(link_unescape(link_escape(all)) == all, "every byte comes back as it went in");
	check(link_unescape("a+b%2Bc") == "a+b+c", "a plus stays a plus");
	check(link_unescape("100%") == "100%" && link_unescape("%4") == "%4" && link_unescape("%zz1") == "%zz1",
	      "a broken escape stays as written");
	check(link_unescape("%e9%a3%9f") == std::string(U8_WORD).substr(0, 3), "small hex letters are read too");
}

void test_query() {
	std::string v;
	check(link_query_value("oretsuba://anki/added?card=17&word=" + std::string(U8_WORD_ESCAPED), "card", v) && v == "17",
	      "the first value of a link is found");
	check(link_query_value("oretsuba://anki/added?card=17&word=" + std::string(U8_WORD_ESCAPED), "word", v) && v == U8_WORD,
	      "a later value is found and unescaped");
	check(!link_query_value("oretsuba://anki/added?card=17", "car", v) && !link_query_value("oretsuba://anki/added?card=17", "id", v),
	      "a name that is not there is not found, nor one that only begins the same");
	check(!link_query_value("oretsuba://anki/added", "card", v), "a link without a question mark has no values");
	check(link_query_value("x://y?flag&card=3", "flag", v) && v.empty(), "a name without a value is found, empty");
	check(link_query_value("x://y?a=1&b=2#b=9", "b", v) && v == "2", "what follows the hash sign is not a value");
	check(link_query_value("x://y?a%20b=c%26d", "a b", v) && v == "c&d", "escaped names are compared unescaped");
	check(link_query_value("x://y?a=1&a=2", "a", v) && v == "1", "of two the first counts");
}

void test_add_link() {
	AnkiLinkNote note;
	note.deck = "Mining";
	note.note_type = "Lapis";
	note.fields = {{"Expression", U8_WORD}, {"Sentence", "a <b>b</b> & c"}, {"Empty", ""}, {"Picture", "http://localhost:50123/abc/shot.jpg"}};
	note.tags = {"oretsuba", "two words", ""};
	std::string link = ankimobile_add_link(note, "oretsuba://anki/added?card=5");
	check(link == "anki://x-callback-url/addnote?type=Lapis&deck=Mining&fldExpression=" + std::string(U8_WORD_ESCAPED) +
	                  "&fldSentence=a%20%3Cb%3Eb%3C%2Fb%3E%20%26%20c"
	                  "&fldPicture=http%3A%2F%2Flocalhost%3A50123%2Fabc%2Fshot.jpg"
	                  "&tags=oretsuba%20two_words"
	                  "&x-success=oretsuba%3A%2F%2Fanki%2Fadded%3Fcard%3D5",
	      "the link is written as AnkiMobile's manual gives it");
	check(link.find("fldEmpty") == std::string::npos, "an empty field is left out");
	check(link.find("dupes") == std::string::npos && link.find("profile") == std::string::npos,
	      "no word of duplicates or a profile unless asked");

	std::string v;
	check(link_query_value(link, "fldPicture", v) && v == "http://localhost:50123/abc/shot.jpg",
	      "the picture's field holds the link alone, as AnkiMobile wants it to fetch the file");
	check(link_query_value(link, "x-success", v) && v == "oretsuba://anki/added?card=5", "the way back comes out whole");
	check(link_query_value(link, "tags", v) && v == "oretsuba two_words", "tags are parted by a blank, and a tag keeps none");

	note.deck = U8_DECK;
	note.note_type = "Basic (and reversed card)";
	note.profile = "User 1";
	note.allow_duplicate = true;
	note.tags.clear();
	note.fields = {{"Front & Back", "1+1=2?"}};
	link = ankimobile_add_link(note, "");
	check(link_query_value(link, "deck", v) && v == U8_DECK, "a deck inside a deck keeps its ::");
	check(link_query_value(link, "type", v) && v == "Basic (and reversed card)", "a note type with brackets and blanks comes out whole");
	check(link_query_value(link, "profile", v) && v == "User 1", "the profile is named when asked");
	check(link_query_value(link, "fldFront & Back", v) && v == "1+1=2?", "a field's name is escaped like its value");
	check(link_query_value(link, "dupes", v) && v == "1", "duplicates are allowed when asked");
	check(link.find("tags=") == std::string::npos && link.find("x-success") == std::string::npos, "no tags and no way back when there is none");
	check(link.find(' ') == std::string::npos && link.find('"') == std::string::npos && link.find('<') == std::string::npos,
	      "nothing in the link needs escaping again");

	check(ankimobile_info_link("oretsuba://anki/info") == "anki://x-callback-url/infoForAdding?x-success=oretsuba%3A%2F%2Fanki%2Finfo",
	      "the link that asks for decks and note types");
}

// ---------------------------------------------------------------- lists

void test_info() {
	// the shape AnkiMobile leaves on the clipboard
	const std::string answer =
	    "{\"profiles\":[{\"name\":\"User 1\"}],"
	    "\"decks\":[{\"name\":\"Default\"},{\"name\":\"" + std::string(U8_DECK) + "\"}],"
	    "\"notetypes\":[{\"name\":\"Basic\",\"kind\":\"normal\",\"fields\":[{\"name\":\"Front\"},{\"name\":\"Back\"}]},"
	    "{\"name\":\"Lapis\",\"fields\":[{\"name\":\"Expression\"},{\"name\":\"ExpressionFurigana\"},{\"name\":\"Sentence\"},"
	    "{\"name\":\"Picture\"},{\"name\":\"SentenceAudio\"}]},"
	    "{\"name\":\"Broken\",\"fields\":[]},{\"fields\":[{\"name\":\"x\"}]}]}";
	AnkiAppInfo info;
	std::string problem = "old";
	check(ankimobile_parse_info(answer, info, problem) && problem.empty(), "AnkiMobile's answer is read");
	check(info.profiles == std::vector<std::string>{"User 1"}, "the profile is read");
	check(info.decks == (std::vector<std::string>{"Default", U8_DECK}), "the decks come in AnkiMobile's order");
	check(info.note_types.size() == 2 && info.note_types[0].name == "Basic" && info.note_types[1].name == "Lapis",
	      "note types without a name or without fields are left out");
	check(info.note_types[1].fields == (std::vector<std::string>{"Expression", "ExpressionFurigana", "Sentence", "Picture", "SentenceAudio"}),
	      "a note type's fields come in order");
	check(info.note_type("Lapis") == &info.note_types[1] && info.note_type("lapis") == nullptr, "a note type is found by its exact name");

	AnkiAppInfo plain;
	check(ankimobile_parse_info("{\"decks\":[\"A\"],\"notetypes\":[{\"name\":\"N\",\"fields\":[\"F\"]}]}", plain, problem) &&
	          plain.decks.size() == 1 && plain.note_types[0].fields[0] == "F" && plain.profiles.empty(),
	      "names written as plain texts are read as well");

	AnkiAppInfo kept = info;
	check(!ankimobile_parse_info("", info, problem) && !problem.empty(), "an empty clipboard is no answer");
	check(!ankimobile_parse_info("[1,2]", info, problem), "a list is no answer");
	check(!ankimobile_parse_info("{\"decks\":[],\"notetypes\":[{\"name\":\"N\",\"fields\":[\"F\"]}]}", info, problem) &&
	          problem.find("deck") != std::string::npos,
	      "an answer without a deck is refused, and says so");
	check(!ankimobile_parse_info("{\"decks\":[\"A\"]}", info, problem) && problem.find("note type") != std::string::npos,
	      "an answer without a note type is refused, and says so");
	check(info.decks == kept.decks && info.note_types.size() == kept.note_types.size(), "a refused answer leaves the lists as they were");

	AnkiAppInfo back;
	check(anki_app_info_from_text(anki_app_info_text(info), back) && back.profiles == info.profiles && back.decks == info.decks &&
	          back.note_types.size() == 2 && back.note_types[1].name == "Lapis" && back.note_types[1].fields == info.note_types[1].fields,
	      "the lists come back from the settings as they went in");
	check(!anki_app_info_from_text("no json", back), "settings that are no JSON give no lists");
	AnkiAppInfo none;
	check(anki_app_info_from_text("{}", none) && none.decks.empty() && none.note_types.empty(), "empty settings give empty lists");
}

void test_media_names() {
	check(anki_media_name("shot.jpg") && anki_media_name("SHOT.JPEG") && anki_media_name("a.b.png") && anki_media_name("x.webp"),
	      "pictures are known by their ending, in any letter case");
	check(anki_media_name("TAK1004.ogg") && anki_media_name("word.mp3") && anki_media_name("v.opus") && anki_media_name("v.m4a") &&
	          anki_media_name("v.wav"),
	      "sound is known by its ending");
	check(!anki_media_name("notes.txt") && !anki_media_name("jpg") && !anki_media_name(".jpg") && !anki_media_name("shot.") &&
	          !anki_media_name("") && !anki_media_name("page.html"),
	      "a name without a media ending is none");
	check(std::string(anki_media_type("a.jpg")) == "image/jpeg" && std::string(anki_media_type("a.OGG")) == "audio/ogg" &&
	          std::string(anki_media_type("a.mp3")) == "audio/mpeg" && std::string(anki_media_type("a.bin")) == "application/octet-stream",
	      "each ending has its type");
	check(anki_media_safe_name("oretsuba_1a2b.jpg") == "oretsuba_1a2b.jpg", "a plain name stays");
	check(anki_media_safe_name("a b/c d\xe9\xa3\x9f.ogg") == "c_d___.ogg", "folders go, blanks and other letters become _");
	check(anki_media_safe_name("..") == "file" && anki_media_safe_name("") == "file" && anki_media_safe_name("...x") == "x",
	      "a name cannot climb out of its folder or be empty");
	check(anki_media_safe_name("C:\\dir\\voice.OGG") == "voice.OGG", "a Windows path loses its folders too");
}

// ---------------------------------------------------------------- the media server

bool has_v6() {
	int fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0) return false;
	sockaddr_in6 address;
	memset(&address, 0, sizeof address);
	address.sin6_family = AF_INET6;
	address.sin6_addr = in6addr_loopback;
	bool bound = bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0;
	close(fd);
	return bound;
}

void test_server() {
	MediaServer server;
	std::string problem;
	check(!server.running() && server.port() == 0, "a new server does not run");
	check(server.offer("a.jpg", {1, 2, 3}).empty(), "a server that does not run offers nothing");
	check(server.start(problem) && problem.empty() && server.running() && server.port() > 0, "the server starts on a port of the system's choice");
	int port = server.port();

	// a picture with every byte value, larger than one packet, and a sound
	std::vector<uint8_t> picture(300000);
	for (size_t i = 0; i < picture.size(); ++i) picture[i] = uint8_t(i * 7 + i / 251);
	std::vector<uint8_t> sound = {'O', 'g', 'g', 'S', 0, 2, 0, 0};
	std::string picture_link = server.offer("oretsuba shot.jpg", picture);
	std::string sound_link = server.offer("TAK1004.ogg", sound);
	std::string start = "http://localhost:" + std::to_string(port) + "/";
	check(picture_link.compare(0, start.size(), start) == 0 && sound_link.compare(0, start.size(), start) == 0,
	      "links name localhost and the port");
	check(picture_link.size() > 13 && picture_link.compare(picture_link.size() - 18, 18, "/oretsuba_shot.jpg") == 0 &&
	          sound_link.compare(sound_link.size() - 12, 12, "/TAK1004.ogg") == 0,
	      "links end in the file's name, made safe, so AnkiMobile sees the ending");
	std::string secret = picture_link.substr(start.size(), picture_link.size() - start.size() - 18);
	check(secret.size() == 32 && secret.find_first_not_of("0123456789abcdef") == std::string::npos, "the path begins with a secret of 32 hex digits");
	check(anki_media_name(picture_link) && anki_media_name(sound_link), "both links end in a media ending");
	check(server.offered() == 2 && server.fetched(picture_link) == 0, "two files are offered, none fetched yet");

	Answer a = get(false, port, path_of(picture_link));
	check(a.connected && a.status == 200, "the picture is answered with 200");
	check(a.body.size() == picture.size() && memcmp(a.body.data(), picture.data(), picture.size()) == 0, "the picture arrives byte for byte");
	check(a.head.find("Content-Type: image/jpeg") != std::string::npos &&
	          a.head.find("Content-Length: 300000") != std::string::npos && a.head.find("Connection: close") != std::string::npos,
	      "type and length are named and the connection ends");
	a = get(false, port, path_of(sound_link));
	check(a.status == 200 && a.body == std::string(sound.begin(), sound.end()) && a.head.find("Content-Type: audio/ogg") != std::string::npos,
	      "the sound arrives with its type");
	check(server.fetched(picture_link) == 1 && server.fetched(sound_link) == 1, "each fetch is counted");

	if (has_v6()) {
		a = get(true, port, path_of(sound_link));
		check(a.connected && a.status == 200 && a.body.size() == sound.size(), "the same port answers on ::1, for a system that reads localhost so");
		check(server.fetched(sound_link) == 2, "and that fetch is counted too");
	} else {
		printf("  (this machine has no ::1, the IPv6 check is left out)\n");
	}

	a = get(false, port, path_of(sound_link) + "?fakePath=x.ogg");
	check(a.status == 200 && a.body.size() == sound.size(), "a query after the name does no harm");
	a = get(false, port, path_of(sound_link), "HEAD");
	check(a.status == 200 && a.body.empty() && a.head.find("Content-Length: 8") != std::string::npos, "HEAD gets the length without the file");
	int counted = server.fetched(sound_link);

	check(get(false, port, "/").status == 404 && get(false, port, "/TAK1004.ogg").status == 404, "without the secret there is nothing");
	check(get(false, port, "/" + std::string(32, '0') + "/TAK1004.ogg").status == 404, "a wrong secret finds nothing");
	check(get(false, port, "/" + secret + "/").status == 404 && get(false, port, "/" + secret).status == 404, "the secret alone lists nothing");
	check(get(false, port, "/" + secret + "/../" + secret + "/TAK1004.ogg").status == 404, "paths are not walked");
	check(get(false, port, path_of(sound_link), "POST").status == 405 && get(false, port, path_of(sound_link), "DELETE").status == 405,
	      "nothing but GET and HEAD is taken");
	check(ask(false, port, "nonsense\r\n\r\n").status == 400, "a request that is none is refused");
	check(ask(false, port, "GET " + std::string(9000, 'a') + " HTTP/1.1\r\n").status == 400, "a request head that never ends is refused");
	check(server.fetched(sound_link) == counted, "none of these counts as a fetch");

	// several at once: they are answered one after the other
	std::vector<std::thread> clients;
	std::vector<int> sizes(6, 0);
	for (int i = 0; i < 6; ++i)
		clients.emplace_back([&, i] { sizes[size_t(i)] = int(get(false, port, path_of(picture_link)).body.size()); });
	for (std::thread &t : clients) t.join();
	bool all = true;
	for (int size : sizes) all = all && size == int(picture.size());
	check(all && server.fetched(picture_link) == 7, "six fetches at once all arrive whole");

	// the same name again replaces the file
	std::string again = server.offer("TAK1004.ogg", {9, 9});
	check(again == sound_link && server.offered() == 2, "a name offered twice keeps its link");
	a = get(false, port, path_of(sound_link));
	check(a.status == 200 && a.body.size() == 2 && server.fetched(sound_link) == 1, "and answers with the newer file, counted anew");

	server.clear();
	check(server.offered() == 0 && server.running(), "after clearing nothing is offered, the server still runs");
	check(get(false, port, path_of(picture_link)).status == 404, "a link handed out before clearing is dead");
	check(server.fetched(picture_link) == 0, "and its count is gone");

	// a restart: new port or not, a new secret, the old link dead
	std::string before = server.offer("x.png", {1});
	check(server.start(problem) && server.running(), "the server starts again");
	std::string after = server.offer("x.png", {1});
	check(!after.empty() && after != before, "a restart gives new links");
	check(server.offered() == 1, "and begins with nothing offered");
	check(get(false, server.port(), path_of(before)).status != 200, "the link from before the restart finds nothing");
	check(get(false, server.port(), path_of(after)).status == 200, "the new link works");

	int last = server.port();
	server.stop();
	check(!server.running() && server.port() == 0 && server.offer("y.png", {1}).empty(), "a stopped server offers nothing");
	check(!ask(false, last, "GET / HTTP/1.1\r\n\r\n").connected, "and its port is closed");
	server.stop();
	check(true, "stopping twice does no harm");

	// a client that says nothing does not hold the server for long, and others get through after it
	MediaServer patient;
	check(patient.start(problem), "another server starts");
	std::string link = patient.offer("z.jpg", {5, 6, 7});
	auto began = std::chrono::steady_clock::now();
	std::thread silent([&] { ask(false, patient.port(), ""); });
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	a = get(false, patient.port(), path_of(link));
	silent.join();
	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
	check(a.status == 200 && a.body.size() == 3, "after a silent visitor the next one is still served");
	check(seconds < 9.0, "and the silent one is dropped within seconds");

	// two servers side by side do not share files
	MediaServer other;
	check(other.start(problem) && other.port() != patient.port(), "two servers get two ports");
	check(get(false, other.port(), path_of(link)).status == 404, "one server does not hand out the other's file");
	// leaving scope stops both
}

// ---------------------------------------------------------------- one card on its way to AnkiMobile

AnkiLinkNote lapis_note() {
	AnkiLinkNote note;
	note.deck = "Mining";
	note.note_type = "Lapis";
	note.fields = {{"Expression", U8_WORD}, {"Sentence", "a <b>b</b> c"}, {"Picture", ""}, {"SentenceAudio", ""}, {"ExpressionAudio", ""}, {"Glossary", "<ol><li>to eat</li></ol>"}};
	note.tags = {"oretsuba"};
	return note;
}

void test_handover() {
	AnkiMobileHandover handover("oretsuba://anki/added");
	check(!handover.waiting(), "no card is on its way at first");
	check(handover.gave_up().kind == AnkiMobileHandover::Result::NotOurs, "giving up without a card is nothing");
	check(handover.returned("oretsuba://anki/added?card=1").kind == AnkiMobileHandover::Result::NotOurs, "a way back without a card is not ours");

	std::vector<uint8_t> picture(70000, 0x5a), voice(20000, 0x33), word(9000, 0x11);
	AnkiMobileHandover::Prepared p;
	std::string problem = "old";
	bool made = handover.prepare(lapis_note(),
	                             {{"oretsuba_shot.jpg", picture, {"Picture"}},
	                              {"TAK1004.ogg", voice, {"SentenceAudio"}},
	                              {"word.mp3", word, {"ExpressionAudio"}}},
	                             p, problem);
	check(made && problem.empty() && handover.waiting(), "a card with picture, voice and word sound is prepared");
	check(p.left_out.empty() && p.card.size() == 16, "nothing is left out and the card has a name");
	std::string v, picture_link, voice_link, word_link;
	check(p.link.compare(0, 37, "anki://x-callback-url/addnote?type=La") == 0, "the link goes to AnkiMobile's addnote");
	check(link_query_value(p.link, "fldPicture", picture_link) && picture_link.compare(0, 17, "http://localhost:") == 0 &&
	          picture_link.compare(picture_link.size() - 18, 18, "/oretsuba_shot.jpg") == 0,
	      "the picture's field holds a link on this device that ends in the file's name");
	check(link_query_value(p.link, "fldSentenceAudio", voice_link) && voice_link.compare(voice_link.size() - 12, 12, "/TAK1004.ogg") == 0 &&
	          link_query_value(p.link, "fldExpressionAudio", word_link) && word_link.compare(word_link.size() - 9, 9, "/word.mp3") == 0,
	      "voice and word sound each have their link");
	check(link_query_value(p.link, "fldExpression", v) && v == U8_WORD && link_query_value(p.link, "fldGlossary", v) &&
	          v == "<ol><li>to eat</li></ol>",
	      "the text fields are as they were");
	check(link_query_value(p.link, "x-success", v) && v == "oretsuba://anki/added?card=" + p.card, "the way back names the card");

	int port = atoi(picture_link.c_str() + 17);
	Answer a = get(false, port, path_of(picture_link));
	check(a.status == 200 && a.body.size() == picture.size(), "the picture can be fetched as AnkiMobile will");
	a = get(false, port, path_of(voice_link));
	check(a.status == 200 && a.body.size() == voice.size(), "and the voice");

	check(handover.returned("oretsuba://anki/info").kind == AnkiMobileHandover::Result::NotOurs && handover.waiting(),
	      "another link of this app is not the way back");
	check(handover.returned("oretsuba://anki/added?card=0000000000000000").kind == AnkiMobileHandover::Result::NotOurs && handover.waiting(),
	      "the way back of another card is not this one's");
	check(handover.returned("other://anki/added?card=" + p.card).kind == AnkiMobileHandover::Result::NotOurs && handover.waiting(),
	      "nor is another app's link with this card's name");
	AnkiMobileHandover::Result r = handover.returned("oretsuba://anki/added?card=" + p.card);
	check(r.kind == AnkiMobileHandover::Result::Added && r.card == p.card, "the way back says the card was added");
	check(r.not_fetched == std::vector<std::string>{"word.mp3"}, "and names the file AnkiMobile did not fetch");
	check(!handover.waiting(), "no card is on its way any more");
	check(!ask(false, port, "GET / HTTP/1.1\r\n\r\n").connected, "the files are offered no longer: the server is gone");
	check(handover.returned("oretsuba://anki/added?card=" + p.card).kind == AnkiMobileHandover::Result::NotOurs, "the same way back twice counts once");

	// AnkiMobile never comes back
	check(handover.prepare(lapis_note(), {{"oretsuba_shot.jpg", picture, {"Picture"}}}, p, problem) && handover.waiting(), "a second card is prepared");
	std::string second = p.card;
	check(link_query_value(p.link, "fldPicture", picture_link), "with its picture");
	port = atoi(picture_link.c_str() + 17);
	r = handover.gave_up();
	check(r.kind == AnkiMobileHandover::Result::Unconfirmed && r.card == second && r.not_fetched.empty(), "giving up says the card is not confirmed");
	check(!handover.waiting() && !ask(false, port, "GET / HTTP/1.1\r\n\r\n").connected, "and ends the offer");
	check(handover.returned("oretsuba://anki/added?card=" + second).kind == AnkiMobileHandover::Result::NotOurs, "a way back that comes too late is not counted");

	// what AnkiMobile cannot take
	AnkiLinkNote note = lapis_note();
	note.fields[2].second = "<i>already here</i>";  // Picture holds text
	check(handover.prepare(note,
	                       {{"shot.jpg", picture, {"Picture"}},          // its field is taken
	                        {"voice.ogg", voice, {"SentenceAudio", "ExpressionAudio"}},  // two fields, both empty
	                        {"word.mp3", word, {"SentenceAudio"}},        // its field went to the voice
	                        {"notes.txt", word, {"ExpressionAudio"}},     // no media ending
	                        {"gone.jpg", {}, {"Picture"}},                // no bytes
	                        {"else.png", word, {"NoSuchField"}}},         // a field the note type lacks
	                       p, problem),
	      "a card with files that cannot all be taken is still prepared");
	check(p.left_out == (std::vector<std::string>{"shot.jpg", "word.mp3", "notes.txt", "gone.jpg", "else.png"}), "the files left out are named");
	check(link_query_value(p.link, "fldPicture", v) && v == "<i>already here</i>", "a field that holds text keeps it");
	std::string v2;
	check(link_query_value(p.link, "fldSentenceAudio", v) && link_query_value(p.link, "fldExpressionAudio", v2) && v == v2 &&
	          v.compare(v.size() - 10, 10, "/voice.ogg") == 0,
	      "a file for two empty fields is linked in both");
	std::string third = p.card;
	check(third != second, "every card has its own name");

	// preparing again gives the card before up without a word
	check(handover.prepare(lapis_note(), {}, p, problem) && p.card != third, "a card without files is prepared over the one on its way");
	check(p.link.find("localhost") == std::string::npos && p.left_out.empty(), "it names no file");
	check(handover.returned("oretsuba://anki/added?card=" + third).kind == AnkiMobileHandover::Result::NotOurs, "the card before is forgotten");
	r = handover.returned("oretsuba://anki/added?card=" + p.card + "&extra=1");
	check(r.kind == AnkiMobileHandover::Result::Added && r.not_fetched.empty(), "the newer one comes back, whatever else the link carries");

	AnkiMobileHandover with_query("oretsuba://anki/added?from=game");
	check(with_query.prepare(lapis_note(), {}, p, problem) && link_query_value(p.link, "x-success", v) &&
	          v == "oretsuba://anki/added?from=game&card=" + p.card,
	      "a way back that has a query already gets the card added to it");
	check(with_query.returned(v).kind == AnkiMobileHandover::Result::Added, "and is recognised");
}

// ---------------------------------------------------------------- AnkiMobile, from asking to knowing

bool has(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

std::vector<AnkiMobileHandover::File> two_files() {
	return {{"oretsuba_shot.jpg", std::vector<uint8_t>(70000, 0x5a), {"Picture"}}, {"TAK1004.ogg", std::vector<uint8_t>(20000, 0x33), {"SentenceAudio"}}};
}

void test_returns() {
	std::vector<std::string> heard_a, heard_b;
	int a = AppReturns::shared().listen([&](const std::string &link) { heard_a.push_back(link); });
	int b = AppReturns::shared().listen([&](const std::string &link) { heard_b.push_back(link); });
	AppReturns::shared().opened_with("oretsuba://anki/added?card=1");
	AppReturns::shared().in_front();
	AppReturns::shared().opened_with("");
	check(heard_a == (std::vector<std::string>{"oretsuba://anki/added?card=1", ""}) && heard_b == heard_a,
	      "every listener hears a link and the coming to the front, and an empty link is no link");
	AppReturns::shared().forget(a);
	AppReturns::shared().opened_with("x://y");
	check(heard_a.size() == 2 && heard_b.size() == 3 && a != b, "a listener that was forgotten hears no more");
	AppReturns::shared().forget(b);
	AppReturns::shared().forget(b);
	AppReturns::shared().in_front();
	check(heard_b.size() == 3, "forgetting twice does no harm");
}

void test_ankimobile() {
	AnkiMobile::Timing quick;
	quick.confirm_ms = 150;
	quick.answer_tries = 4;
	quick.answer_wait_ms = 20;
	std::string problem, card;
	std::vector<std::string> left_out;

	{
		Told told;
		AnkiMobile none(AnkiAppHost(), "oretsuba", told.tell(), quick);
		check(!none.possible(), "a host that can open no link has no AnkiMobile");
		check(!none.ask_lists(problem) && !problem.empty(), "its lists cannot be asked for, and it says so");
		check(!none.add(lapis_note(), two_files(), card, left_out, problem) && !problem.empty(), "nor can a card be handed over");
		AppReturns::shared().in_front();
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		check(told.count() == 0, "coming to the front with nothing on its way says nothing");
	}

	// ---- the lists
	{
		PretendAnkiMobile anki;
		anki.clipboard_late = 2;
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.possible() && mobile.ask_lists(problem), "the lists are asked for");
		check(told.wait(1) && told.at(0).first == "anki.lists", "an answer comes");
		std::string json = told.at(0).second;
		check(has(json, "\"ok\":true") && has(json, "\"decks\":[\"Default\",\"Mining\"]") &&
		          has(json, "\"noteTypes\":[{\"name\":\"Lapis\",\"fields\":[\"Expression\",\"Picture\"]}]") && has(json, "\"profiles\":[\"User 1\"]"),
		      "it carries decks, note types and fields, even when the clipboard took a moment");
		anki.finish();
		check(anki.links.size() == 1 && anki.links[0] == "anki://x-callback-url/infoForAdding?x-success=oretsuba%3A%2F%2Fanki%2Flists",
		      "AnkiMobile was opened with the link its manual gives");
		check(anki.clipboard.empty(), "the answer was taken off the clipboard");
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		check(told.count() == 1, "one question, one answer");
		AppReturns::shared().opened_with("oretsuba://anki/lists");
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		check(told.count() == 1, "the way back without a question is not an answer");
	}
	{
		PretendAnkiMobile anki;
		anki.installed = false;
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.ask_lists(problem) && told.wait(1) && has(told.at(0).second, "\"ok\":false") && has(told.at(0).second, "did not open"),
		      "without AnkiMobile on the device the question fails at once, and says why");
	}
	{
		PretendAnkiMobile anki;
		anki.comes_back = false;  // the player says no in AnkiMobile and returns by hand
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.ask_lists(problem) && told.wait(1) && has(told.at(0).second, "\"ok\":false") && has(told.at(0).second, "left no answer"),
		      "a question AnkiMobile never answers ends once this app is in front again");
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		check(told.count() == 1, "and ends once");
	}
	{
		PretendAnkiMobile anki;
		anki.lists = "";  // comes back with nothing on the clipboard
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.ask_lists(problem) && told.wait(1) && has(told.at(0).second, "\"ok\":false") && has(told.at(0).second, "left no answer"),
		      "a way back with an empty clipboard is no answer");
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		check(told.count() == 1, "and is told once, not again when the app is in front");
	}
	{
		PretendAnkiMobile anki;
		anki.lists = "{\"decks\":[],\"notetypes\":[]}";
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.ask_lists(problem) && told.wait(1) && has(told.at(0).second, "\"ok\":false") && has(told.at(0).second, "no deck"),
		      "an answer without a deck is passed on as a problem");
	}

	// ---- a card
	{
		PretendAnkiMobile anki;
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.add(lapis_note(), two_files(), card, left_out, problem) && card.size() == 16 && left_out.empty(), "a card is handed over");
		check(told.wait(1) && told.at(0).first == "card.added", "AnkiMobile's way back says it was added");
		check(has(told.at(0).second, ("\"card\":\"" + card + "\"").c_str()) && has(told.at(0).second, "\"notFetched\":[]"),
		      "the event names the card, and no file is missing");
		anki.finish();
		check(anki.fetched.size() == 2 && anki.fetched[0] == std::make_pair(std::string("Picture"), size_t(70000)) &&
		          anki.fetched[1] == std::make_pair(std::string("SentenceAudio"), size_t(20000)),
		      "AnkiMobile fetched picture and voice whole, from the links in their fields");
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		check(told.count() == 1, "a card that was added is not called unconfirmed afterwards");
	}
	{
		PretendAnkiMobile anki;
		anki.front_first = true;  // the system says "in front" before it delivers the way back
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.add(lapis_note(), two_files(), card, left_out, problem) && told.wait(1) && told.at(0).first == "card.added",
		      "the way back may come after the app is in front");
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		check(told.count() == 1, "and still counts once");
	}
	{
		PretendAnkiMobile anki;
		anki.fetches = false;
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.add(lapis_note(), two_files(), card, left_out, problem) && told.wait(1) && told.at(0).first == "card.added" &&
		          has(told.at(0).second, "\"notFetched\":[\"oretsuba_shot.jpg\",\"TAK1004.ogg\"]"),
		      "files AnkiMobile did not fetch are named with the card");
	}
	{
		PretendAnkiMobile anki;
		anki.comes_back = false;  // AnkiMobile refuses the note and the player returns by hand
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.add(lapis_note(), two_files(), card, left_out, problem) && told.wait(1) && told.at(0).first == "card.unconfirmed",
		      "a card AnkiMobile never confirms is called unconfirmed once this app is in front again");
		check(has(told.at(0).second, ("\"card\":\"" + card + "\"").c_str()) && has(told.at(0).second, "\"reason\":\"AnkiMobile did not say"),
		      "with the card's name and a reason");
		AppReturns::shared().opened_with("oretsuba://anki/added?card=" + card);
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		check(told.count() == 1, "a way back after that is too late");
	}
	{
		PretendAnkiMobile anki;
		anki.installed = false;
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		check(mobile.add(lapis_note(), two_files(), card, left_out, problem) && told.wait(1) && told.at(0).first == "card.unconfirmed" &&
		          has(told.at(0).second, "did not open"),
		      "without AnkiMobile on the device the card is unconfirmed at once");
	}
	{
		// two cards one after the other, the first never confirmed
		PretendAnkiMobile anki;
		anki.comes_back = false;
		Told told;
		AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
		std::string first, second;
		check(mobile.add(lapis_note(), two_files(), first, left_out, problem), "a first card goes out");
		anki.finish();
		{
			std::lock_guard<std::mutex> lock(anki.mutex);
			anki.comes_back = true;
		}
		check(mobile.add(lapis_note(), two_files(), second, left_out, problem) && second != first, "a second card goes out before the first is settled");
		check(told.wait(1) && told.at(0).first == "card.added" && has(told.at(0).second, second.c_str()), "the second is added");
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		check(told.count() == 1, "and the first, given up for it, says nothing more");
	}
	{
		// the owner lets go while a card is on its way and timers run
		PretendAnkiMobile anki;
		anki.comes_back = false;
		Told told;
		{
			AnkiMobile mobile(anki.host(), "oretsuba", told.tell(), quick);
			check(mobile.add(lapis_note(), two_files(), card, left_out, problem), "a card goes out");
			anki.finish();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		check(told.count() == 0, "an AnkiMobile that is gone tells nothing");
		AppReturns::shared().opened_with("oretsuba://anki/added?card=" + card);
		AppReturns::shared().in_front();
		check(told.count() == 0, "and hears nothing");
	}
}

// ---------------------------------------------------------------- AnkiDroid

void test_ankidroid() {
	AnkiLinkNote note = lapis_note();
	note.allow_duplicate = true;
	std::string request = ankidroid_add_request(note, {{"oretsuba_shot.jpg", false, {"Picture"}}, {"TAK1004.ogg", true, {"SentenceAudio", "ExpressionAudio"}}});
	check(request == std::string("{\"deck\":\"Mining\",\"noteType\":\"Lapis\",\"fields\":{\"Expression\":\"") + U8_WORD +
	                     "\",\"Sentence\":\"a <b>b</b> c\",\"Picture\":\"\",\"SentenceAudio\":\"\",\"ExpressionAudio\":\"\","
	                     "\"Glossary\":\"<ol><li>to eat</li></ol>\"},\"tags\":[\"oretsuba\"],\"allowDuplicate\":true,"
	                     "\"media\":[{\"name\":\"oretsuba_shot.jpg\",\"sound\":false,\"fields\":[\"Picture\"]},"
	                     "{\"name\":\"TAK1004.ogg\",\"sound\":true,\"fields\":[\"SentenceAudio\",\"ExpressionAudio\"]}]}",
	      "a card is written for AnkiDroid.kt as it reads it");
	check(ankidroid_known_request("Lapis", "Expression", "a\"b") == "{\"noteType\":\"Lapis\",\"field\":\"Expression\",\"value\":\"a\\\"b\"}",
	      "the question whether a word is there is written with its quotes escaped");

	AnkiDroidAnswer a = ankidroid_answer("{\"installed\":true,\"allowed\":false,\"mediaFolder\":\"/data/user/0/app/cache/anki-media\"}");
	check(a.ok && a.installed && !a.allowed && a.media_folder == "/data/user/0/app/cache/anki-media", "status is read");
	a = ankidroid_answer("{\"decks\":[\"Default\",\"Mining\"],\"noteTypes\":[{\"name\":\"Lapis\",\"fields\":[\"Expression\",\"Picture\"]}]}");
	check(a.ok && a.info.decks.size() == 2 && a.info.note_types.size() == 1 && a.info.note_types[0].fields.size() == 2, "the lists are read");
	a = ankidroid_answer("{\"known\":true}");
	check(a.ok && a.known, "a known word is read");
	a = ankidroid_answer("{\"id\":\"1795645000123\",\"missing\":[\"TAK1004.ogg\"]}");
	check(a.ok && a.id == "1795645000123" && a.missing == std::vector<std::string>{"TAK1004.ogg"}, "a new card's number and its missing file are read");
	a = ankidroid_answer("{\"error\":\"This word is in AnkiDroid already.\",\"code\":\"duplicate\"}");
	check(!a.ok && a.problem == "This word is in AnkiDroid already." && a.code == "duplicate", "a refusal is read with its reason and kind");
	a = ankidroid_answer("{\"error\":\"\"}");
	check(!a.ok && !a.problem.empty() && a.code == "failed", "a refusal without words still has words");
	a = ankidroid_answer("");
	check(!a.ok && a.code == "failed" && !a.problem.empty(), "no answer is a failure");
	a = ankidroid_answer("[]");
	check(!a.ok, "a list is no answer");
}

}  // namespace

int main() {
	test_escape();
	test_query();
	test_add_link();
	test_info();
	test_media_names();
	test_server();
	test_handover();
	test_returns();
	test_ankimobile();
	test_ankidroid();
	printf("%s: %d checks\n", ok ? "anki_app_test passed" : "anki_app_test FAILED", checks);
	return ok ? 0 : 1;
}
