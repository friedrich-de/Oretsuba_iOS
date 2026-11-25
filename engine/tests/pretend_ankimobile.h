// What the tests of the Anki app on the same device share (anki_app_test.cpp,
// anki_target_test.cpp): one request to this device's own web server, and an
// AnkiMobile made up from what its manual says it does.
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "mining/anki_app.h"

#if !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0  // Apple has none; the tests do not write to a closed socket
#endif

namespace ore_test {

using namespace ore;

struct Answer {
	bool connected = false;
	int status = 0;
	std::string head, body;
};

// One request to this device's own address, v4 or v6, read to the end.
inline Answer ask(bool v6, int port, const std::string &request) {
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

inline std::string path_of(const std::string &link) { return link.substr(link.find('/', link.find("://") + 3)); }

inline Answer get(bool v6, int port, const std::string &path, const char *method = "GET") {
	return ask(v6, port, std::string(method) + " " + path + " HTTP/1.1\r\nHost: localhost:" + std::to_string(port) + "\r\nAccept: */*\r\n\r\n");
}

// The "name=value" pairs of a link's query, unescaped.
inline std::vector<std::pair<std::string, std::string>> query_pairs(const std::string &link) {
	std::vector<std::pair<std::string, std::string>> out;
	size_t mark = link.find('?');
	if (mark == std::string::npos) return out;
	std::string query = link.substr(mark + 1);
	size_t at = 0;
	while (at <= query.size()) {
		size_t amp = query.find('&', at);
		std::string pair = query.substr(at, amp == std::string::npos ? std::string::npos : amp - at);
		size_t eq = pair.find('=');
		out.emplace_back(link_unescape(pair.substr(0, eq)), eq == std::string::npos ? std::string() : link_unescape(pair.substr(eq + 1)));
		if (amp == std::string::npos) break;
		at = amp + 1;
	}
	return out;
}

inline constexpr char kPretendLists[] = "{\"profiles\":[{\"name\":\"User 1\"}],\"decks\":[{\"name\":\"Default\"},{\"name\":\"Mining\"}],"
                             "\"notetypes\":[{\"name\":\"Lapis\",\"fields\":[{\"name\":\"Expression\"},{\"name\":\"Picture\"}]}]}";

// What AnkiMobile does when it is opened with one of its links, as far as its
// manual says: it works while this app is in the back, then opens the way back.
struct PretendAnkiMobile {
	std::mutex mutex;
	bool installed = true;      // an app takes anki:// links
	bool comes_back = true;     // it opens the way back
	bool fetches = true;        // it fetches the files a card links to
	bool front_first = false;   // this app hears "in front" before it hears the way back
	std::string lists = kPretendLists;
	int clipboard_late = 0;     // looks at the clipboard that find nothing yet
	std::string clipboard;
	std::vector<std::string> links;
	std::vector<std::pair<std::string, size_t>> fetched;  // field, bytes
	std::vector<std::thread> threads;

	AnkiAppHost host() {
		AnkiAppHost h;
		h.open_link = [this](const std::string &link, std::function<void(bool)> done) {
			bool there;
			{
				std::lock_guard<std::mutex> lock(mutex);
				links.push_back(link);
				there = installed;
			}
			done(there);
			if (!there) return;
			std::lock_guard<std::mutex> lock(mutex);
			threads.emplace_back([this, link] { work(link); });
		};
		h.take_answer = [this] {
			std::lock_guard<std::mutex> lock(mutex);
			if (clipboard_late > 0) {
				--clipboard_late;
				return std::string();
			}
			std::string text = clipboard;
			clipboard.clear();
			return text;
		};
		return h;
	}

	void work(const std::string &link) {
		std::string back;
		bool answer;
		{
			std::lock_guard<std::mutex> lock(mutex);
			answer = comes_back;
		}
		link_query_value(link, "x-success", back);
		if (link.compare(0, 37, "anki://x-callback-url/infoForAdding?x") == 0) {
			std::lock_guard<std::mutex> lock(mutex);
			if (answer) clipboard = lists;
		} else if (link.compare(0, 30, "anki://x-callback-url/addnote?") == 0) {
			for (const auto &pair : query_pairs(link)) {
				// a field whose whole value is a web link with a media ending is fetched
				if (pair.first.compare(0, 3, "fld") != 0 || pair.second.compare(0, 17, "http://localhost:") != 0) continue;
				if (!anki_media_name(pair.second) || !fetches) continue;
				Answer a = get(false, atoi(pair.second.c_str() + 17), path_of(pair.second));
				std::lock_guard<std::mutex> lock(mutex);
				fetched.emplace_back(pair.first.substr(3), a.status == 200 ? a.body.size() : 0);
			}
		}
		if (front_first) AppReturns::shared().in_front();
		if (answer && !back.empty()) AppReturns::shared().opened_with(back);
		if (!front_first) AppReturns::shared().in_front();
	}

	void finish() {
		std::vector<std::thread> all;
		{
			std::lock_guard<std::mutex> lock(mutex);
			all.swap(threads);
		}
		for (std::thread &t : all) t.join();
	}
	~PretendAnkiMobile() { finish(); }
};

struct Told {
	std::mutex mutex;
	std::vector<std::pair<std::string, std::string>> events;
	AnkiMobile::Tell tell() {
		return [this](const std::string &event, const std::string &json) {
			std::lock_guard<std::mutex> lock(mutex);
			events.emplace_back(event, json);
		};
	}
	// Waits until `count` events are there, at most a few seconds.
	bool wait(size_t count) {
		for (int i = 0; i < 400; ++i) {
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (events.size() >= count) return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return false;
	}
	size_t count() {
		std::lock_guard<std::mutex> lock(mutex);
		return events.size();
	}
	std::pair<std::string, std::string> at(size_t i) {
		std::lock_guard<std::mutex> lock(mutex);
		return i < events.size() ? events[i] : std::pair<std::string, std::string>();
	}
};

}  // namespace ore_test
