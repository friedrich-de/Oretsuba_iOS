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
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "mining/anki_app.h"

#if !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0  // Apple has none; the test does not write to a closed socket
#endif

using namespace ore;

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

struct Answer {
	bool connected = false;
	int status = 0;
	std::string head, body;
};

// One request to this device's own address, v4 or v6, read to the end.
Answer ask(bool v6, int port, const std::string &request) {
	Answer a;
	int fd = socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return a;
	timeval patience = {10, 0};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof patience);
	int connected;
	if (v6) {
		sockaddr_in6 address;
		memset(&address, 0, sizeof address);
		address.sin6_family = AF_INET6;
		address.sin6_addr = in6addr_loopback;
		address.sin6_port = htons(uint16_t(port));
		connected = connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address);
	} else {
		sockaddr_in address;
		memset(&address, 0, sizeof address);
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(uint16_t(port));
		connected = connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address);
	}
	if (connected != 0) {
		close(fd);
		return a;
	}
	a.connected = true;
	if (!request.empty()) send(fd, request.data(), request.size(), MSG_NOSIGNAL);
	std::string all;
	char buffer[4096];
	for (;;) {
		ssize_t n = recv(fd, buffer, sizeof buffer, 0);
		if (n <= 0) break;
		all.append(buffer, size_t(n));
	}
	close(fd);
	size_t split = all.find("\r\n\r\n");
	a.head = all.substr(0, split);
	if (split != std::string::npos) a.body = all.substr(split + 4);
	if (a.head.compare(0, 9, "HTTP/1.1 ") == 0) a.status = atoi(a.head.c_str() + 9);
	return a;
}

std::string path_of(const std::string &link) { return link.substr(link.find('/', link.find("://") + 3)); }

Answer get(bool v6, int port, const std::string &path, const char *method = "GET") {
	return ask(v6, port, std::string(method) + " " + path + " HTTP/1.1\r\nHost: localhost:" + std::to_string(port) + "\r\nAccept: */*\r\n\r\n");
}

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

}  // namespace

int main() {
	test_escape();
	test_query();
	test_add_link();
	test_info();
	test_media_names();
	test_server();
	printf("%s: %d checks\n", ok ? "anki_app_test passed" : "anki_app_test FAILED", checks);
	return ok ? 0 : 1;
}
