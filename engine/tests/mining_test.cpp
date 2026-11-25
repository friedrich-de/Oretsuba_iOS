// Mining (docs/MINING.md): the settings file, the text of a record, which
// lines count as read, the request and the queue. Needs no game files and no
// Apple system, so it also builds on its own:
//
//   c++ -std=c++17 -Isrc -Ithird_party tests/mining_test.cpp src/mining/mining.cpp src/util/log.cpp -lpthread
//
// With --server URL --key KEY [--voice FILE] it then sends three lines to a
// running VN Mining Server and reads them back.
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mining/mining.h"
#include "util/log.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
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

// Shift-JIS: <-2>\「ソファで、\表を見てた」$   (ソ and 表 end in the byte of the backslash)
const char SJ_BODY[] = "\x3c\x2d\x32\x3e\x5c\x81\x75\x83\x5c\x83\x74\x83\x40\x82\xc5\x81\x41\x5c\x95\x5c\x82\xf0\x8c\xa9\x82\xc4\x82\xbd\x81\x76\x24";
const char SJ_PLAIN[] = "\x81\x75\x83\x5c\x83\x74\x83\x40\x82\xc5\x81\x41\x95\x5c\x82\xf0\x8c\xa9\x82\xc4\x82\xbd\x81\x76";
// 　ああ、$そうか。$&次のページ$   (waits in the middle and a new page)
const char SJ_WAITS[] = "\x81\x40\x82\xa0\x82\xa0\x81\x41\x24\x82\xbb\x82\xa4\x82\xa9\x81\x42\x24\x26\x8e\x9f\x82\xcc\x83\x79\x81\x5b\x83\x57\x24";
const char SJ_WAITS_PLAIN[] = "\x81\x40\x82\xa0\x82\xa0\x81\x41\x82\xbb\x82\xa4\x82\xa9\x81\x42\x8e\x9f\x82\xcc\x83\x79\x81\x5b\x83\x57";
// a<<b@@c@12d<"外字">e<x
const char SJ_MARKS[] = "\x61\x3c\x3c\x62\x40\x40\x63\x40\x31\x32\x64\x3c\x22\x8a\x4f\x8e\x9a\x22\x3e\x65\x3c\x78";
const char SJ_MARKS_PLAIN[] = "\x61\x3c\x62\x40\x63\x64\x8a\x4f\x8e\x9a\x65\x3c\x78";
// GBK: <2>\「我们没有翅膀」$
const char GB_BODY[] = "\x3c\x32\x3e\x5c\xa1\xb8\xce\xd2\xc3\xc7\xc3\xbb\xd3\xd0\xb3\xe1\xb0\xf2\xa1\xb9\x24";
const char GB_PLAIN[] = "\xa1\xb8\xce\xd2\xc3\xc7\xc3\xbb\xd3\xd0\xb3\xe1\xb0\xf2\xa1\xb9";
// UTF-8: full-width blank, blank, tab, 「うん」, full-width blank, line end
const char U8_PADDED[] = "\xe3\x80\x80\x20\x09\xe3\x80\x8c\xe3\x81\x86\xe3\x82\x93\xe3\x80\x8d\xe3\x80\x80\x0d\x0a";
const char U8_TRIMMED[] = "\xe3\x80\x8c\xe3\x81\x86\xe3\x82\x93\xe3\x80\x8d";
const char U8_GAME[] = "\xe4\xbf\xba\xe3\x81\x9f\xe3\x81\xa1\xe3\x81\xab\xe7\xbf\xbc\xe3\x81\xaf\xe3\x81\xaa\xe3\x81\x84";  // 俺たちに翼はない
const char U8_LINE1[] = "\xe6\x98\xa8\xe6\x97\xa5\xe3\x81\xae\xe8\xa9\xb1\xe3\x80\x81\xe8\xa6\x9a\xe3\x81\x88\xe3\x81\xa6\xe3\x82\x8b\xef\xbc\x9f";  // 昨日の話、覚えてる？
const char U8_NAME1[] = "\xe3\x83\xa6\xe3\x82\xad";  // ユキ
const char U8_LINE2[] = "\xe2\x80\xa6\xe2\x80\xa6\xe8\xa6\x9a\xe3\x81\x88\xe3\x81\xa6\xe3\x81\xaa\xe3\x81\x84\xe3\x80\x82";  // ……覚えてない。
const char U8_LINE3[] = "\xe7\xa9\xba\xe3\x81\x8c\xe3\x81\xbe\xe3\x81\xa0\xe6\x98\x8e\xe3\x82\x8b\xe3\x81\x84\xe3\x81\x86\xe3\x81\xa1\xe3\x81\xab\xe3\x80\x81\xe5\xae\xb6\xe3\x82\x92\xe5\x87\xba\xe3\x81\x9f\xe3\x80\x82";  // 空がまだ明るいうちに、家を出た。

// ---------------------------------------------------------------- mining.txt

void test_settings() {
	MiningSettings s;
	std::string problem;
	// the file as a VN Mining Server hands it out
	check(parse_mining_settings("# Put this file into the game's folder.\n"
	                            "# Whoever has the key can read and fill the server's stream.\n"
	                            "server = https://vn.example.org\n"
	                            "key = 0123456789abcdefghijABCDEFGHIJ_-0123456789a\n",
	                            s, problem),
	      "the server's own file is read");
	check(s.server == "https://vn.example.org" && s.key == "0123456789abcdefghijABCDEFGHIJ_-0123456789a",
	      "server and key are as written");
	check(s.voice && s.screenshot && s.game.empty(), "voice and screenshot are on unless turned off");

	check(parse_mining_settings("\xEF\xBB\xBF; made on Windows\r\nSERVER = \"HTTP://192.168.1.20:8733/sub/\"\r\nKey=a=b=c\r\n"
	                            "game = " + std::string(U8_GAME) + "\r\nvoice = no\r\nScreenshot = Off\r\nlater = unknown\r\n",
	                            s, problem),
	      "byte order mark, Windows line ends, quotes, capitals and unknown names are taken");
	check(s.server == "http://192.168.1.20:8733/sub", "the address loses its last slash and keeps its path");
	check(s.key == "a=b=c", "a key may hold the equals sign");
	check(s.game == U8_GAME && !s.voice && !s.screenshot, "title and switches are read");

	check(!parse_mining_settings("key = k\n", s, problem) && problem.find("server") != std::string::npos,
	      "a file without server is refused, and says so");
	check(!parse_mining_settings("server = https://vn.example.org\n", s, problem) && problem.find("key") != std::string::npos,
	      "a file without key is refused, and says so");
	check(!parse_mining_settings("server = vn.example.org\nkey = k\n", s, problem), "an address needs http:// or https://");
	check(!parse_mining_settings("server = ftp://vn.example.org\nkey = k\n", s, problem), "other schemes are refused");
	check(!parse_mining_settings("server = https://\nkey = k\n", s, problem), "a scheme alone is no address");
	check(!parse_mining_settings("server = https://vn.example.org/a b\nkey = k\n", s, problem), "an address holds no blank");
	check(!parse_mining_settings("server = https://vn.example.org\nkey = two words\n", s, problem), "a key holds no blank");
	check(!parse_mining_settings("server = https://vn.example.org\nkey = k\nvoice = maybe\n", s, problem),
	      "a switch takes yes or no only");
	check(!parse_mining_settings("server = https://vn.example.org\nkey = k\ngame = \x89\xb4\x82\xbd\n", s, problem) &&
	          problem.find("UTF-8") != std::string::npos,
	      "a title saved in another encoding is refused");
	check(s.server == "http://192.168.1.20:8733/sub", "a refused file leaves the settings as they were");
}

void write_file(const std::string &path, const std::string &text) {
	std::ofstream f(path, std::ios::binary);
	f << text;
}

void test_file_search() {
	char root[] = "/tmp/ore-mining-test-XXXXXX";
	if (!mkdtemp(root)) {
		check(false, "a temporary folder can be made");
		return;
	}
	std::string top = root;
	std::string game = top + "/a/b/Disc/SET_UP";
	std::string cmd = "mkdir -p '" + game + "'";
	check(system(cmd.c_str()) == 0, "the folders are made");
	MiningSettings s;
	std::string problem;
	check(find_mining_file(game).empty() && load_mining_settings(game, s, problem) == 0 && problem.empty(),
	      "no file, no mining, and no complaint");

	write_file(top + "/a/mining.txt", "server = https://too.far.example\nkey = k\n");
	check(find_mining_file(game).empty(), "three folders above the game is too far");
	write_file(top + "/a/b/Mining.TXT", "server = https://two.up.example\nkey = k\n");
	check(load_mining_settings(game, s, problem) == 1 && s.server == "https://two.up.example",
	      "the file is found two folders above, in any letter case");
	write_file(top + "/a/b/Disc/mining.txt", "server = https://one.up.example\nkey = k\n");
	check(load_mining_settings(game + "/", s, problem) == 1 && s.server == "https://one.up.example", "the nearer file wins");
	mkdir((game + "/mining.txt").c_str(), 0755);
	check(load_mining_settings(game, s, problem) == 1 && s.server == "https://one.up.example", "a folder of that name is not it");
	rmdir((game + "/mining.txt").c_str());
	write_file(game + "/MINING.TXT", "server = nowhere\nkey = k\n");
	check(load_mining_settings(game, s, problem) == -1 && !problem.empty(), "an unusable file in the game folder is reported");
	cmd = "rm -rf '" + top + "'";
	check(system(cmd.c_str()) == 0, "the folders are removed");
}

// ---------------------------------------------------------------- the text of a line

void test_text() {
	check(mining_plain_text(SJ_BODY, TextEncoding::ShiftJis) == SJ_PLAIN,
	      "tags, breaks and the end mark go; letters whose second byte is a backslash stay");
	check(mining_plain_text(SJ_WAITS, TextEncoding::ShiftJis) == SJ_WAITS_PLAIN, "the text goes on behind waits and page breaks");
	check(mining_plain_text(SJ_MARKS, TextEncoding::ShiftJis) == SJ_MARKS_PLAIN,
	      "doubled marks stand for themselves, text tags are kept, an unclosed bracket is text");
	check(mining_plain_text(GB_BODY, TextEncoding::Gbk) == GB_PLAIN, "the Chinese patch's text is read the same way");
	check(mining_plain_text("", TextEncoding::ShiftJis).empty() && mining_plain_text("$", TextEncoding::ShiftJis).empty(),
	      "an empty record gives no text");
	check(mining_trim(U8_PADDED) == U8_TRIMMED, "blanks, full-width ones too, are cut from both ends");
	check(mining_trim("\xe3\x80\x80 \xe3\x80\x80").empty() && mining_trim("").empty(), "blanks alone leave nothing");
	check(mining_trim("a \xe3\x80\x80 b") == "a \xe3\x80\x80 b", "blanks inside stay");
}

// ---------------------------------------------------------------- which lines are sent

LineGate::Record record(int line, const std::string &name, const std::string &body) {
	LineGate::Record r;
	r.type = 0;
	r.line = line;
	r.name = &name;
	r.body = &body;
	return r;
}

void test_gate() {
	const std::string name = "Yuki\\YUK0001", body = "one$", other = "two$";
	{
		// read at the usual pace: the text types itself out, then waits for the click
		LineGate gate;
		gate.shown(record(10, name, body), 1000);
		check(!gate.frame(0, record(10, name, body), 1000), "nothing is sent when a line starts");
		check(!gate.frame(1, record(10, name, body), 1400), "nothing is sent while the text types itself out");
		check(gate.frame(2, record(10, name, body), 1800), "the line is sent once it stands complete");
		check(!gate.frame(2, record(10, name, body), 1816) && !gate.armed(), "and only once");
	}
	{
		// text shown at once: sent a moment later, so that clicking through does not send
		LineGate gate;
		gate.shown(record(10, name, body), 1000);
		check(!gate.frame(2, record(10, name, body), 1016), "a complete line is not sent in its first moment");
		check(!gate.frame(2, record(10, name, body), 1000 + LineGate::kDwellMs - 1), "nor just before the moment is over");
		check(gate.frame(2, record(10, name, body), 1000 + LineGate::kDwellMs), "it is sent when the moment is over");
	}
	{
		LineGate gate;
		gate.shown(record(10, name, body), 1000);
		check(!gate.frame(2, record(10, name, body), 1100), "a line clicked through at once ...");
		check(!gate.frame(3, record(10, name, body), 1116) && !gate.armed(), "... is not sent when it ends");
		check(!gate.frame(2, record(10, name, body), 5000), "... nor later");
	}
	{
		LineGate gate;
		gate.shown(record(10, name, body), 1000);
		check(!gate.frame(4, record(10, name, body), 1000) && !gate.armed(), "a skipped line is not sent");
	}
	{
		// the question before a choice: the script lets it run through without a click, and it stays in view
		LineGate gate;
		gate.shown(record(10, name, body), 1000);
		check(!gate.frame(1, record(10, name, body), 1100), "a line that does not wait is not sent while it types");
		check(!gate.frame(3, record(10, name, body), 1200) && gate.armed(), "nor in its first moment when it has ended");
		check(gate.frame(3, record(10, name, body), 1400), "it is sent once it stood for a moment");
		check(!gate.frame(3, record(10, name, body), 1416), "and only once");
		gate.shown(record(11, name, other), 2000);
		check(!gate.frame(3, record(12, name, body), 2100) && !gate.armed(), "unless the next line came first");
	}
	{
		// the history screen puts other records into the message manager
		LineGate gate;
		gate.shown(record(10, name, body), 1000);
		check(!gate.frame(1, record(9, name, other), 1100) && !gate.armed(), "a line that lost its place is not sent");
	}
	{
		LineGate gate;
		gate.shown(record(10, name, body), 1000);
		check(gate.frame(2, record(10, name, body), 2000), "a line is sent");
		gate.shown(record(10, name, body), 9000);
		check(!gate.armed() && !gate.frame(2, record(10, name, body), 9900), "the same record shown again is not sent twice");
		gate.shown(record(11, name, body), 10000);
		check(gate.frame(2, record(11, name, body), 10900), "the next record is sent, even with the same words");
		gate.shown(record(10, name, body), 11000);
		check(gate.frame(2, record(10, name, body), 11900), "going back to an earlier line sends it again");
	}
	{
		LineGate gate;
		gate.shown(record(10, name, body), 1000);
		check(gate.leaving(1, record(10, name, body)), "leaving the app in the middle of a line sends it");
		check(!gate.leaving(1, record(10, name, body)) && !gate.frame(2, record(10, name, body), 5000), "but once only");
		gate.shown(record(11, name, other), 6000);
		check(!gate.frame(2, record(11, name, other), 6100) && !gate.leaving(3, record(11, name, other)),
		      "leaving after a line was clicked away sends nothing");
		gate.shown(record(14, name, other), 6500);
		check(gate.leaving(3, record(14, name, other)), "leaving at a question that did not wait sends it");
		gate.shown(record(12, name, other), 7000);
		check(gate.frame(2, record(12, name, other), 8000) && !gate.leaving(2, record(12, name, other)),
		      "leaving after a line was sent sends nothing more");
		gate.shown(record(13, name, other), 9000);
		gate.reset();
		check(!gate.leaving(1, record(13, name, other)), "a window that is gone has no line");
	}
}

// ---------------------------------------------------------------- the request

std::vector<uint32_t> test_screen(int width, int height) {
	// left half red, right half blue, a green bar below
	std::vector<uint32_t> pixels(size_t(width) * size_t(height));
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			pixels[size_t(y) * size_t(width) + size_t(x)] = y >= height * 3 / 4 ? 0xff00c000u : x < width / 2 ? 0xffd00000u : 0xff0000d0u;
	return pixels;
}

void test_picture() {
	const int w = 800, h = 600;
	std::vector<uint32_t> pixels = test_screen(w, h);
	std::vector<uint8_t> jpeg = mining_jpeg(pixels.data(), w, h, 85);
	check(jpeg.size() > 1000 && jpeg.size() < 200 * 1024, "the screen becomes a picture of a sensible size");
	check(jpeg.size() > 4 && jpeg[0] == 0xff && jpeg[1] == 0xd8 && jpeg[jpeg.size() - 2] == 0xff && jpeg[jpeg.size() - 1] == 0xd9,
	      "the picture is a JPEG from its first to its last byte");
	int dw = 0, dh = 0, dn = 0;
	stbi_uc *decoded = stbi_load_from_memory(jpeg.data(), int(jpeg.size()), &dw, &dh, &dn, 3);
	check(decoded && dw == w && dh == h, "it opens again at the screen's size");
	if (decoded) {
		auto at = [&](int x, int y) { return decoded + (size_t(y) * size_t(dw) + size_t(x)) * 3; };
		const stbi_uc *red = at(100, 100), *blue = at(700, 100), *green = at(400, 550);
		check(red[0] > 180 && red[1] < 40 && red[2] < 40, "red stays red");
		check(blue[2] > 180 && blue[0] < 40 && blue[1] < 40, "blue stays blue");
		check(green[1] > 160 && green[0] < 40 && green[2] < 40, "green stays green");
		stbi_image_free(decoded);
	}
	check(mining_jpeg(nullptr, w, h, 85).empty() && mining_jpeg(pixels.data(), 0, h, 85).empty(), "no screen, no picture");
}

std::string field_of(const std::string &body, const std::string &name) {
	std::string mark = "name=\"" + name + "\"";
	size_t at = body.find(mark);
	if (at == std::string::npos) return "<none>";
	size_t from = body.find("\r\n\r\n", at);
	size_t to = body.find("\r\n--", from);
	if (from == std::string::npos || to == std::string::npos) return "<broken>";
	return body.substr(from + 4, to - from - 4);
}

void test_request() {
	MiningSettings settings;
	settings.server = "https://vn.example.org/sub";
	settings.key = "the-key";
	MiningLine line;
	line.text = U8_LINE1;
	line.name = U8_NAME1;
	line.game = U8_GAME;
	line.voice = "YUK0001";
	line.audio_name = "YUK\"00\r\n01.ogg";
	line.audio = {'O', 'g', 'g', 'S', 0, 1, 2, 3};
	line.picture = {0xff, 0xd8, 0xff, 0xd9};
	HttpRequest request = mining_line_request(settings, line, "----oreBOUNDARY");
	check(request.method == "POST" && request.url == "https://vn.example.org/sub/Api/Lines", "lines go to Api/Lines of the server");
	bool bearer = false, type = false;
	for (const auto &header : request.headers) {
		if (header.first == "Authorization" && header.second == "Bearer the-key") bearer = true;
		if (header.first == "Content-Type" && header.second == "multipart/form-data; boundary=----oreBOUNDARY") type = true;
	}
	check(bearer, "the key goes in the Authorization header");
	check(type, "the body is announced as a form with its boundary");
	const std::string &body = request.body;
	check(body.compare(0, 19, "------oreBOUNDARY\r\n") == 0, "the form opens with the boundary");
	check(body.size() > 23 && body.compare(body.size() - 23, 23, "\r\n------oreBOUNDARY--\r\n") == 0, "and closes with it");
	check(field_of(body, "text") == U8_LINE1 && field_of(body, "name") == U8_NAME1 && field_of(body, "game") == U8_GAME &&
	          field_of(body, "voice") == "YUK0001",
	      "text, speaker, title and voice name are fields");
	check(field_of(body, "audio") == std::string("OggS\0\x01\x02\x03", 8), "the voice goes as it is");
	check(body.find("name=\"audio\"; filename=\"YUK_00__01.ogg\"\r\nContent-Type: audio/ogg\r\n") != std::string::npos,
	      "the voice file's name cannot break out of its header");
	check(field_of(body, "screenshot") == "\xff\xd8\xff\xd9" &&
	          body.find("name=\"screenshot\"; filename=\"screenshot.jpg\"\r\nContent-Type: image/jpeg\r\n") != std::string::npos,
	      "the picture goes as a JPEG file");

	MiningLine bare;
	bare.text = U8_LINE3;
	HttpRequest narrow = mining_line_request(settings, bare, "----oreB");
	check(field_of(narrow.body, "text") == U8_LINE3 && field_of(narrow.body, "name") == "<none>" &&
	          field_of(narrow.body, "audio") == "<none>" && field_of(narrow.body, "screenshot") == "<none>",
	      "narration goes with its text alone");
}

// ---------------------------------------------------------------- the queue

// A server that answers as it is told, and remembers what got through.
struct FakeServer {
	std::mutex mu;
	std::condition_variable changed;
	std::deque<int> answers;         // what the next requests get; 200 when empty
	std::vector<std::string> taken;  // the text of each line answered with 200
	std::vector<bool> pictured;      // whether it came with a picture
	int calls = 0;
	bool hold = false;               // requests wait here until released
	int waiting = 0;

	static HttpSend sender(const std::shared_ptr<FakeServer> &server) {
		return [server](const HttpRequest &request, std::string &answer) {
			std::unique_lock<std::mutex> lock(server->mu);
			++server->calls;
			++server->waiting;
			server->changed.notify_all();
			server->changed.wait(lock, [&] { return !server->hold; });
			--server->waiting;
			int status = 200;
			if (!server->answers.empty()) {
				status = server->answers.front();
				server->answers.pop_front();
			}
			if (status == 200) {
				server->taken.push_back(field_of(request.body, "text"));
				server->pictured.push_back(request.body.find("name=\"screenshot\"") != std::string::npos);
			}
			if (status <= 0) answer = "no route";
			server->changed.notify_all();
			return status;
		};
	}
	void set_hold(bool on) {
		std::lock_guard<std::mutex> lock(mu);
		hold = on;
		changed.notify_all();
	}
	bool wait_waiting(int count) {
		std::unique_lock<std::mutex> lock(mu);
		return changed.wait_for(lock, std::chrono::seconds(5), [&] { return waiting >= count; });
	}
	std::vector<std::string> got() {
		std::lock_guard<std::mutex> lock(mu);
		return taken;
	}
	int called() {
		std::lock_guard<std::mutex> lock(mu);
		return calls;
	}
};

MiningSettings test_settings_value() {
	MiningSettings settings;
	settings.server = "http://vn.example";
	settings.key = "the-key";
	return settings;
}

MiningQueue::Options quick() {
	MiningQueue::Options options;
	options.retry_ms = 5;
	options.retry_max_ms = 40;
	options.leave_ms = 200;
	return options;
}

MiningLine text_line(const std::string &text) {
	MiningLine line;
	line.text = text;
	return line;
}

void test_queue() {
	typedef std::vector<std::string> Texts;
	{
		auto server = std::make_shared<FakeServer>();
		MiningQueue queue(test_settings_value(), FakeServer::sender(server), quick());
		for (const char *text : {"1", "2", "3", "4", "5"}) queue.push(text_line(text));
		check(queue.flush(5000), "the queue empties");
		check(server->got() == Texts({"1", "2", "3", "4", "5"}), "lines arrive in the order they were shown");
		MiningQueue::Stats stats = queue.stats();
		check(stats.sent == 5 && stats.dropped == 0 && stats.pending == 0 && stats.last_status == 200 && !stats.refused,
		      "the count of sent lines is kept");
	}
	{
		// the network is away, then the server is in trouble, then all is well
		auto server = std::make_shared<FakeServer>();
		server->answers = {0, 0, 503, 429, 200};
		MiningQueue queue(test_settings_value(), FakeServer::sender(server), quick());
		queue.push(text_line("kept"));
		queue.push(text_line("next"));
		check(queue.flush(5000), "a line the network could not take is tried again until it goes through");
		check(server->got() == Texts({"kept", "next"}) && server->called() == 6, "and the lines behind it wait for their turn");
	}
	{
		// a line the server will never take must not hold up the rest
		auto server = std::make_shared<FakeServer>();
		server->answers = {400, 200, 500, 500, 500, 500, 500, 200};
		MiningQueue queue(test_settings_value(), FakeServer::sender(server), quick());
		for (const char *text : {"refused", "fine", "broken", "after"}) queue.push(text_line(text));
		check(queue.flush(5000), "the queue gets past lines the server does not take");
		check(server->got() == Texts({"fine", "after"}), "a refused line and one that fails five times are dropped");
		check(queue.stats().sent == 2 && queue.stats().dropped == 2, "and counted as dropped");
	}
	{
		// a wrong key: asking again would get the address locked out
		auto server = std::make_shared<FakeServer>();
		server->answers = {401};
		MiningQueue queue(test_settings_value(), FakeServer::sender(server), quick());
		for (const char *text : {"1", "2", "3"}) queue.push(text_line(text));
		check(queue.flush(5000) && queue.stats().refused, "a server that does not take the key ends the sending");
		queue.push(text_line("4"));
		std::this_thread::sleep_for(std::chrono::milliseconds(60));
		check(server->called() == 1 && server->got().empty(), "no second request is made with a refused key");
		check(queue.stats().dropped == 4 && queue.stats().pending == 0, "the lines are let go");
	}
	{
		// the server is away for long: the newest lines are the ones kept
		auto server = std::make_shared<FakeServer>();
		server->set_hold(true);
		MiningQueue::Options options = quick();
		options.max_pending = 4;
		MiningQueue queue(test_settings_value(), FakeServer::sender(server), options);
		queue.push(text_line("1"));
		check(server->wait_waiting(1), "the first line is on its way");
		for (const char *text : {"2", "3", "4", "5", "6", "7"}) queue.push(text_line(text));
		check(queue.stats().pending == 4 && queue.stats().dropped == 3, "no more lines wait than the queue may hold");
		check(!queue.flush(30), "waiting for the queue gives up after its time");
		server->set_hold(false);
		check(queue.flush(5000), "the queue empties once the server answers");
		check(server->got() == Texts({"1", "5", "6", "7"}), "the line on its way is finished, and the oldest waiting ones went");
	}
	{
		// screens are big: only a few may wait to be made into pictures
		auto server = std::make_shared<FakeServer>();
		server->set_hold(true);
		MiningQueue::Options options = quick();
		options.max_raw = 2;
		MiningQueue queue(test_settings_value(), FakeServer::sender(server), options);
		queue.push(text_line("first"));
		check(server->wait_waiting(1), "the worker is busy sending");
		for (const char *text : {"a", "b", "c", "d"}) {
			MiningLine line = text_line(text);
			line.width = 64;
			line.height = 48;
			line.pixels = test_screen(64, 48);
			queue.push(std::move(line));
		}
		server->set_hold(false);
		check(queue.flush(5000), "lines with screens go through");
		check(server->got() == Texts({"first", "a", "b", "c", "d"}), "in their order");
		std::lock_guard<std::mutex> lock(server->mu);
		check(server->pictured == std::vector<bool>({false, true, true, false, false}),
		      "lines beyond what may wait as raw screens go without a picture");
	}
	{
		// leaving the game must not hang on a server that does not answer
		auto server = std::make_shared<FakeServer>();
		server->set_hold(true);
		auto begin = std::chrono::steady_clock::now();
		{
			MiningQueue queue(test_settings_value(), FakeServer::sender(server), quick());
			queue.push(text_line("stuck"));
			check(server->wait_waiting(1), "a request hangs");
		}
		auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
		check(took < 1500, "the queue lets go of a hanging request when the game ends");
		server->set_hold(false);
		// the worker ends on its own; give it the time before the next test starts
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		check(server->got() == Texts({"stuck"}), "the hanging request still finishes by itself");
	}
	{
		// lines on their way get a moment when the game ends
		auto server = std::make_shared<FakeServer>();
		{
			MiningQueue queue(test_settings_value(), FakeServer::sender(server), quick());
			queue.push(text_line("last words"));
		}
		check(server->got() == Texts({"last words"}), "a line pushed just before the end is still sent");
	}
}

void test_check() {
	MiningSettings settings = test_settings_value();
	std::string message;
	HttpRequest seen;
	auto answering = [&](int status, const std::string &body) {
		return [&seen, status, body](const HttpRequest &request, std::string &answer) {
			seen = request;
			answer = body;
			return status;
		};
	};
	check(mining_check(settings, answering(200, "{\"Version\":\"0.1.0\",\"Lines\":3,\"MaxLines\":1000}"), message) &&
	          message == "Mining: lines go to vn.example (VN Mining Server 0.1.0).",
	      "a server that takes the key is named with its version");
	check(seen.method == "GET" && seen.url == "http://vn.example/Api/Status" && seen.body.empty() && !seen.headers.empty() &&
	          seen.headers[0].second == "Bearer the-key",
	      "the check asks Api/Status with the key");
	check(!mining_check(settings, answering(401, ""), message) && message.find("does not take the key") != std::string::npos,
	      "a wrong key is said to be one");
	check(!mining_check(settings, answering(0, "no route"), message) && message == "Mining: vn.example could not be reached: no route",
	      "a server that is away is said to be away, with the reason");
	check(!mining_check(settings, answering(404, ""), message) && message.find("404") != std::string::npos,
	      "another kind of server is named by its answer");
	check(!mining_check(settings, nullptr, message), "a build without HTTP says so");
}

// ---------------------------------------------------------------- a real server

// HTTP/1.0 over a plain socket: enough for a server on this machine.
int socket_send(const HttpRequest &request, std::string &answer) {
	answer.clear();
	std::string rest = request.url;
	if (rest.compare(0, 7, "http://") != 0) return answer = "only http:// here", 0;
	rest.erase(0, 7);
	size_t slash = rest.find('/');
	std::string host = rest.substr(0, slash), path = slash == std::string::npos ? "/" : rest.substr(slash);
	std::string port = "80";
	size_t colon = host.rfind(':');
	if (colon != std::string::npos) port = host.substr(colon + 1), host.erase(colon);
	addrinfo hints{}, *found = nullptr;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0 || !found) return answer = "unknown host", 0;
	int fd = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
	bool connected = fd >= 0 && connect(fd, found->ai_addr, found->ai_addrlen) == 0;
	freeaddrinfo(found);
	if (!connected) {
		if (fd >= 0) close(fd);
		return answer = "connection refused", 0;
	}
	std::string head = request.method + " " + path + " HTTP/1.0\r\nHost: " + host + ":" + port + "\r\n";
	for (const auto &header : request.headers) head += header.first + ": " + header.second + "\r\n";
	head += "Content-Length: " + std::to_string(request.body.size()) + "\r\nConnection: close\r\n\r\n";
	std::string out = head + request.body;
	for (size_t sent = 0; sent < out.size();) {
		ssize_t n = send(fd, out.data() + sent, out.size() - sent, 0);
		if (n <= 0) break;
		sent += size_t(n);
	}
	std::string in;
	char buffer[8192];
	for (ssize_t n; (n = recv(fd, buffer, sizeof buffer, 0)) > 0;) in.append(buffer, size_t(n));
	close(fd);
	if (in.compare(0, 5, "HTTP/") != 0 || in.size() < 12) return answer = "no answer", 0;
	size_t body = in.find("\r\n\r\n");
	if (body != std::string::npos) answer = in.substr(body + 4);
	return atoi(in.c_str() + 9);
}

int count_of(const std::string &text, const std::string &what) {
	int count = 0;
	for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++count;
	return count;
}

void test_server(const std::string &server, const std::string &key, const std::string &voice_file) {
	MiningSettings settings;
	std::string problem;
	check(parse_mining_settings("server = " + server + "\nkey = " + key + "\n", settings, problem), "the server's address is usable");
	std::string message;
	check(mining_check(settings, socket_send, message), "the server takes the key");
	printf("%s\n", message.c_str());
	MiningSettings wrong = settings;
	wrong.key = "not-the-key-of-this-server";
	check(!mining_check(wrong, socket_send, message) && message.find("does not take the key") != std::string::npos,
	      "the server refuses another key");

	std::vector<uint8_t> voice;
	if (!voice_file.empty()) {
		std::ifstream f(voice_file, std::ios::binary);
		voice.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		check(!voice.empty(), "the voice file is read");
	}
	std::string before;
	HttpRequest list;
	list.method = "GET";
	list.url = settings.server + "/Api/Lines?limit=50";
	list.headers.emplace_back("Authorization", "Bearer " + settings.key);
	check(socket_send(list, before) == 200, "the stream can be read");
	{
		MiningQueue queue(settings, socket_send);
		MiningLine one = text_line(U8_LINE1);
		one.name = U8_NAME1;
		one.game = U8_GAME;
		one.voice = "YUK0001";
		one.audio_name = "YUK0001.ogg";
		one.audio = voice;
		one.width = 800;
		one.height = 600;
		one.pixels = test_screen(800, 600);
		queue.push(std::move(one));
		MiningLine two = text_line(U8_LINE2);
		two.name = U8_NAME1;
		two.game = U8_GAME;
		two.width = 800;
		two.height = 600;
		two.pixels = test_screen(800, 600);
		queue.push(std::move(two));
		MiningLine three = text_line(U8_LINE3);
		three.game = U8_GAME;
		queue.push(std::move(three));
		check(queue.flush(20000), "three lines reach the server");
		MiningQueue::Stats stats = queue.stats();
		check(stats.sent == 3 && stats.dropped == 0 && stats.last_status == 200, "the server takes all three");
	}
	std::string after;
	check(socket_send(list, after) == 200, "the stream can be read again");
	size_t first = after.find(U8_LINE1), second = after.find(U8_LINE2), third = after.find(U8_LINE3);
	check(first != std::string::npos && second != std::string::npos && third != std::string::npos, "the stream holds the three lines");
	check(first < second && second < third, "in the order they were sent");
	check(count_of(after, "\"Id\":") - count_of(before, "\"Id\":") == 3, "and no line twice");
	check(count_of(after, U8_NAME1) - count_of(before, U8_NAME1) == 2 && after.find(U8_GAME) != std::string::npos,
	      "with the speaker and the game's title");
	check(count_of(after, "\"HasPicture\":true") - count_of(before, "\"HasPicture\":true") == 2, "two of them have a screenshot");
	if (!voice.empty())
		check(count_of(after, "\"HasAudio\":true") - count_of(before, "\"HasAudio\":true") == 1, "one of them has a voice");
}

}  // namespace

int main(int argc, char **argv) {
	std::string server, key, voice;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (a == "--server") server = next();
		else if (a == "--key") key = next();
		else if (a == "--voice") voice = next();
		else {
			fprintf(stderr, "usage: mining_test [--server URL --key KEY [--voice FILE]]\n");
			return 2;
		}
	}
	set_log_level(LogLevel::Error);  // the queue's warnings are what some tests ask for
	test_settings();
	test_file_search();
	test_text();
	test_gate();
	test_picture();
	test_request();
	test_queue();
	test_check();
	if (!server.empty()) test_server(server, key, voice);
	if (ok) printf("mining_test: %d checks passed%s\n", checks, server.empty() ? "" : ", a real server among them");
	return ok ? 0 : 1;
}
