// Cards for the Anki app on the same device: links, lists and the media server.
#include "mining/anki_app.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#include "util/json.h"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace ore {
namespace {

std::string lower_ascii(std::string_view s) {
	std::string out(s);
	for (char &c : out)
		if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return out;
}

// The ending of a file name without its dot, in small letters; empty when it has none.
std::string ending_of(std::string_view file_name) {
	size_t slash = file_name.find_last_of("/\\");
	if (slash != std::string_view::npos) file_name.remove_prefix(slash + 1);
	size_t dot = file_name.rfind('.');
	if (dot == std::string_view::npos || dot == 0 || dot + 1 == file_name.size()) return std::string();
	return lower_ascii(file_name.substr(dot + 1));
}

struct MediaKind {
	const char *ending, *type;
};

// The endings Anki counts as media (its own list), each with the type a web server names it by.
const MediaKind kMediaKinds[] = {
    {"jpg", "image/jpeg"},   {"jpeg", "image/jpeg"},  {"png", "image/png"},     {"gif", "image/gif"},
    {"webp", "image/webp"},  {"avif", "image/avif"},  {"svg", "image/svg+xml"}, {"ico", "image/x-icon"},
    {"tif", "image/tiff"},   {"tiff", "image/tiff"},  {"mp3", "audio/mpeg"},    {"ogg", "audio/ogg"},
    {"oga", "audio/ogg"},    {"opus", "audio/ogg"},   {"spx", "audio/ogg"},     {"ogx", "application/ogg"},
    {"wav", "audio/wav"},    {"flac", "audio/flac"},  {"m4a", "audio/mp4"},     {"aac", "audio/aac"},
    {"3gp", "video/3gpp"},   {"mp4", "video/mp4"},    {"webm", "video/webm"},   {"ogv", "video/ogg"},
    {"mkv", "video/x-matroska"}, {"mov", "video/quicktime"}, {"avi", "video/x-msvideo"},
    {"mpg", "video/mpeg"},   {"mpeg", "video/mpeg"},  {"flv", "video/x-flv"},   {"swf", "application/x-shockwave-flash"},
};

const MediaKind *media_kind(std::string_view file_name) {
	std::string ending = ending_of(file_name);
	if (ending.empty()) return nullptr;
	for (const MediaKind &kind : kMediaKinds)
		if (ending == kind.ending) return &kind;
	return nullptr;
}

int hex_digit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// A name out of a list entry that is either the name itself or {"name": ..}.
std::string name_of(const json::Value &entry) {
	if (entry.is_string()) return entry.as_string();
	if (entry.is_object()) return entry.str("name");
	return std::string();
}

void add_names(const json::Value &list, std::vector<std::string> &out) {
	for (const json::Value &entry : list.items()) {
		std::string name = name_of(entry);
		if (!name.empty()) out.push_back(std::move(name));
	}
}

json::Value names_json(const std::vector<std::string> &names) {
	json::Value list = json::Value::array();
	for (const std::string &name : names) list.push(name);
	return list;
}

}  // namespace

// ---------------------------------------------------------------- what an Anki app holds

const AnkiAppInfo::NoteType *AnkiAppInfo::note_type(std::string_view name) const {
	for (const NoteType &type : note_types)
		if (type.name == name) return &type;
	return nullptr;
}

bool ankimobile_parse_info(std::string_view text, AnkiAppInfo &out, std::string &problem) {
	json::Value root;
	if (!json::parse(text, root) || !root.is_object()) {
		problem = "AnkiMobile's answer could not be read.";
		return false;
	}
	AnkiAppInfo info;
	add_names(root["profiles"], info.profiles);
	add_names(root["decks"], info.decks);
	for (const json::Value &entry : root["notetypes"].items()) {
		AnkiAppInfo::NoteType type;
		type.name = name_of(entry);
		if (type.name.empty()) continue;
		add_names(entry["fields"], type.fields);
		if (type.fields.empty()) continue;  // a note type without fields takes no card
		info.note_types.push_back(std::move(type));
	}
	if (info.decks.empty()) {
		problem = "AnkiMobile named no deck.";
		return false;
	}
	if (info.note_types.empty()) {
		problem = "AnkiMobile named no note type.";
		return false;
	}
	out = std::move(info);
	problem.clear();
	return true;
}

std::string anki_app_info_text(const AnkiAppInfo &info) {
	json::Value root = json::Value::object();
	root.set("profiles", names_json(info.profiles));
	root.set("decks", names_json(info.decks));
	json::Value types = json::Value::array();
	for (const AnkiAppInfo::NoteType &type : info.note_types) {
		json::Value entry = json::Value::object();
		entry.set("name", type.name);
		entry.set("fields", names_json(type.fields));
		types.push(std::move(entry));
	}
	root.set("noteTypes", std::move(types));
	return json::write(root);
}

bool anki_app_info_from_text(std::string_view text, AnkiAppInfo &out) {
	json::Value root;
	if (!json::parse(text, root) || !root.is_object()) return false;
	AnkiAppInfo info;
	add_names(root["profiles"], info.profiles);
	add_names(root["decks"], info.decks);
	for (const json::Value &entry : root["noteTypes"].items()) {
		AnkiAppInfo::NoteType type;
		type.name = name_of(entry);
		if (type.name.empty()) continue;
		add_names(entry["fields"], type.fields);
		info.note_types.push_back(std::move(type));
	}
	out = std::move(info);
	return true;
}

// ---------------------------------------------------------------- links

std::string link_escape(std::string_view text) {
	static const char kHex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(text.size() * 3);
	for (unsigned char c : text) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
		    c == '~') {
			out += char(c);
		} else {
			out += '%';
			out += kHex[c >> 4];
			out += kHex[c & 15];
		}
	}
	return out;
}

std::string link_unescape(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '%' && i + 2 < text.size() && hex_digit(text[i + 1]) >= 0 && hex_digit(text[i + 2]) >= 0) {
			out += char(hex_digit(text[i + 1]) << 4 | hex_digit(text[i + 2]));
			i += 2;
		} else {
			out += text[i];
		}
	}
	return out;
}

bool link_query_value(std::string_view link, std::string_view name, std::string &out) {
	size_t mark = link.find('?');
	if (mark == std::string_view::npos) return false;
	std::string_view query = link.substr(mark + 1);
	size_t hash = query.find('#');
	if (hash != std::string_view::npos) query = query.substr(0, hash);
	while (!query.empty()) {
		size_t amp = query.find('&');
		std::string_view pair = query.substr(0, amp);
		size_t eq = pair.find('=');
		std::string_view key = pair.substr(0, eq);
		if (link_unescape(key) == name) {
			out = eq == std::string_view::npos ? std::string() : link_unescape(pair.substr(eq + 1));
			return true;
		}
		if (amp == std::string_view::npos) break;
		query.remove_prefix(amp + 1);
	}
	return false;
}

std::string ankimobile_info_link(std::string_view success_link) {
	return "anki://x-callback-url/infoForAdding?x-success=" + link_escape(success_link);
}

std::string ankimobile_add_link(const AnkiLinkNote &note, std::string_view success_link) {
	std::string link = "anki://x-callback-url/addnote?type=" + link_escape(note.note_type) + "&deck=" + link_escape(note.deck);
	if (!note.profile.empty()) link += "&profile=" + link_escape(note.profile);
	for (const auto &field : note.fields) {
		if (field.first.empty() || field.second.empty()) continue;
		link += "&fld" + link_escape(field.first) + "=" + link_escape(field.second);
	}
	std::string tags;
	for (const std::string &tag : note.tags) {
		if (tag.empty()) continue;
		if (!tags.empty()) tags += ' ';
		for (char c : tag) tags += c == ' ' || c == '\t' || c == '\n' || c == '\r' ? '_' : c;  // a tag holds no blank
	}
	if (!tags.empty()) link += "&tags=" + link_escape(tags);
	if (note.allow_duplicate) link += "&dupes=1";
	if (!success_link.empty()) link += "&x-success=" + link_escape(success_link);
	return link;
}

bool anki_media_name(std::string_view file_name) { return media_kind(file_name) != nullptr; }

const char *anki_media_type(std::string_view file_name) {
	const MediaKind *kind = media_kind(file_name);
	return kind ? kind->type : "application/octet-stream";
}

std::string anki_media_safe_name(std::string_view file_name) {
	size_t slash = file_name.find_last_of("/\\");
	if (slash != std::string_view::npos) file_name.remove_prefix(slash + 1);
	std::string out;
	out.reserve(file_name.size());
	for (char c : file_name) {
		bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
		out += keep ? c : '_';
	}
	while (!out.empty() && out.front() == '.') out.erase(out.begin());  // no hidden file, no ".."
	if (out.empty()) out = "file";
	return out;
}

// ---------------------------------------------------------------- the media server

struct MediaServer::State {
	struct Item {
		std::shared_ptr<const std::vector<uint8_t>> bytes;
		std::string type;
		int fetched = 0;
	};
	mutable std::mutex mutex;
	std::map<std::string, Item> items;  // by path, "/SECRET/NAME"
	std::string secret;
	int port = 0;
	bool running = false;
	bool leave = false;
	int listen_fd[2] = {-1, -1};  // 127.0.0.1 and ::1
	int wake_fd[2] = {-1, -1};    // a pipe that ends the thread's wait
	std::thread thread;
};

#if defined(_WIN32)

// The Windows test host has no Anki app beside it.
MediaServer::MediaServer() : state_(std::make_shared<State>()) {}
MediaServer::~MediaServer() {}
bool MediaServer::start(std::string &problem) {
	problem = "There is no media server on Windows.";
	return false;
}
void MediaServer::stop() {}

#else

namespace {

constexpr int kRequestLimit = 8192;
constexpr int kPatienceSeconds = 5;

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void close_fd(int &fd) {
	if (fd >= 0) close(fd);
	fd = -1;
}

void set_blocking(int fd, bool blocking) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return;
	fcntl(fd, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
}

std::string new_secret() {
	static const char kHex[] = "0123456789abcdef";
	std::random_device device;
	std::string out;
	for (int i = 0; i < 8; ++i) {
		uint32_t word = device();
		for (int j = 0; j < 4; ++j) out += kHex[(word >> (j * 4)) & 15];
	}
	return out;
}

bool send_all(int fd, const char *data, size_t size) {
	while (size > 0) {
		ssize_t n = send(fd, data, size, kSendFlags);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return false;
		data += n;
		size -= size_t(n);
	}
	return true;
}

bool send_head(int fd, const char *status, const std::string &type, size_t length, const char *extra = "") {
	std::string head = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " +
	                   std::to_string(length) + "\r\nCache-Control: no-store\r\nConnection: close\r\n" + extra + "\r\n";
	return send_all(fd, head.data(), head.size());
}

// The whole request head, or what came of it before the other side fell silent.
std::string read_head(int fd) {
	std::string head;
	char buffer[1024];
	while (head.size() < size_t(kRequestLimit) && head.find("\r\n\r\n") == std::string::npos) {
		ssize_t n = recv(fd, buffer, sizeof buffer, 0);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) break;
		head.append(buffer, size_t(n));
	}
	return head;
}

// Opens one listening socket on `port` of this device's own address; port 0 =
// any free one. -1 when it cannot, then with the system's reason in `why`.
int listen_on(bool v6, int port, int &bound_port, int &why) {
	why = 0;
	int fd = socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		why = errno;
		return -1;
	}
	int one = 1;
	bool bound;
	if (v6) {
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
		sockaddr_in6 address;
		memset(&address, 0, sizeof address);
		address.sin6_family = AF_INET6;
		address.sin6_addr = in6addr_loopback;
		address.sin6_port = htons(uint16_t(port));
		bound = bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0;
		bound_port = port;
	} else {
		sockaddr_in address;
		memset(&address, 0, sizeof address);
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(uint16_t(port));
		bound = bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0;
		socklen_t length = sizeof address;
		if (bound && getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) == 0) bound_port = ntohs(address.sin_port);
	}
	if (!bound || listen(fd, 8) != 0) {
		why = errno;
		close(fd);
		return -1;
	}
	set_blocking(fd, false);
	return fd;
}

}  // namespace

MediaServer::MediaServer() : state_(std::make_shared<State>()) {}

MediaServer::~MediaServer() { stop(); }

bool MediaServer::start(std::string &problem) {
	stop();
	State &s = *state_;
	int wake[2] = {-1, -1};
	if (pipe(wake) != 0) {
		problem = "The media server could not start (no pipe).";
		return false;
	}
	set_blocking(wake[0], false);
	set_blocking(wake[1], false);
	int v4 = -1, v6 = -1, port = 0;
	// The same port on both addresses, since "localhost" may mean either. A
	// port that is free on one and taken on the other is given back.
	for (int attempt = 0; attempt < 8 && v4 < 0; ++attempt) {
		int why = 0, same = 0;
		v4 = listen_on(false, 0, port, why);
		if (v4 < 0) break;
		v6 = listen_on(true, port, same, why);
		if (v6 < 0 && why == EADDRINUSE) close_fd(v4);  // a device without ::1 goes on with 127.0.0.1 alone
	}
	if (v4 < 0) {
		close(wake[0]);
		close(wake[1]);
		problem = "The media server could not start (no free port on this device).";
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		s.items.clear();
		s.secret = new_secret();
		s.port = port;
		s.running = true;
		s.leave = false;
		s.listen_fd[0] = v4;
		s.listen_fd[1] = v6;
		s.wake_fd[0] = wake[0];
		s.wake_fd[1] = wake[1];
	}
	std::shared_ptr<State> state = state_;
	s.thread = std::thread([state, v4, v6, wake_read = wake[0]] {
		for (;;) {
			pollfd fds[3];
			int count = 0;
			fds[count++] = {wake_read, POLLIN, 0};
			fds[count++] = {v4, POLLIN, 0};
			if (v6 >= 0) fds[count++] = {v6, POLLIN, 0};
			int ready = poll(fds, nfds_t(count), -1);
			if (ready < 0 && errno == EINTR) continue;
			bool broken = ready < 0;
			{
				std::lock_guard<std::mutex> lock(state->mutex);
				if (state->leave) break;
			}
			for (int i = 1; i < count && !broken; ++i) {
				// The system took the socket away (an app that was suspended): this server is over.
				if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) broken = true;
			}
			if (broken) break;
			for (int i = 1; i < count; ++i) {
				if (!(fds[i].revents & POLLIN)) continue;
				int fd = accept(fds[i].fd, nullptr, nullptr);
				if (fd < 0) continue;
				set_blocking(fd, true);
				timeval patience = {kPatienceSeconds, 0};
				setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof patience);
				setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &patience, sizeof patience);
#if defined(SO_NOSIGPIPE)
				int one = 1;
				setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
				std::string head = read_head(fd);
				size_t line_end = head.find("\r\n");
				std::string line = head.substr(0, line_end);
				size_t a = line.find(' '), b = line.rfind(' ');
				std::string method = a == std::string::npos ? std::string() : line.substr(0, a);
				std::string target = a == std::string::npos || b <= a ? std::string() : line.substr(a + 1, b - a - 1);
				size_t mark = target.find_first_of("?#");
				if (mark != std::string::npos) target.erase(mark);
				bool get = method == "GET", just_head = method == "HEAD";
				if (head.find("\r\n\r\n") == std::string::npos || target.empty()) {
					send_head(fd, "400 Bad Request", "text/plain", 0);
				} else if (!get && !just_head) {
					send_head(fd, "405 Method Not Allowed", "text/plain", 0, "Allow: GET, HEAD\r\n");
				} else {
					std::shared_ptr<const std::vector<uint8_t>> bytes;
					std::string type;
					{
						std::lock_guard<std::mutex> lock(state->mutex);
						auto found = state->items.find(target);
						if (found != state->items.end()) {
							bytes = found->second.bytes;
							type = found->second.type;
						}
					}
					if (!bytes) {
						send_head(fd, "404 Not Found", "text/plain", 0);
					} else {
						bool sent = send_head(fd, "200 OK", type, bytes->size());
						if (sent && get) sent = send_all(fd, reinterpret_cast<const char *>(bytes->data()), bytes->size());
						if (sent && get) {
							std::lock_guard<std::mutex> lock(state->mutex);
							auto found = state->items.find(target);
							if (found != state->items.end() && found->second.bytes == bytes) ++found->second.fetched;
						}
					}
				}
				shutdown(fd, SHUT_WR);
				// Let the other side read to the end before the socket goes.
				char rest[256];
				while (recv(fd, rest, sizeof rest, 0) > 0) {
				}
				close(fd);
			}
		}
		std::lock_guard<std::mutex> lock(state->mutex);
		state->running = false;
	});
	problem.clear();
	return true;
}

void MediaServer::stop() {
	State &s = *state_;
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		s.leave = true;
	}
	if (s.wake_fd[1] >= 0) {
		char byte = 1;
		ssize_t written = write(s.wake_fd[1], &byte, 1);
		(void)written;
	}
	if (s.thread.joinable()) s.thread.join();
	std::lock_guard<std::mutex> lock(s.mutex);
	close_fd(s.listen_fd[0]);
	close_fd(s.listen_fd[1]);
	close_fd(s.wake_fd[0]);
	close_fd(s.wake_fd[1]);
	s.items.clear();
	s.running = false;
	s.port = 0;
}

#endif  // !_WIN32

bool MediaServer::running() const {
	std::lock_guard<std::mutex> lock(state_->mutex);
	return state_->running;
}

int MediaServer::port() const {
	std::lock_guard<std::mutex> lock(state_->mutex);
	return state_->running ? state_->port : 0;
}

std::string MediaServer::offer(std::string_view file_name, std::vector<uint8_t> bytes, std::string_view content_type) {
	State &s = *state_;
	std::lock_guard<std::mutex> lock(s.mutex);
	if (!s.running) return std::string();
	std::string name = anki_media_safe_name(file_name);
	std::string path = "/" + s.secret + "/" + name;
	State::Item item;
	item.bytes = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
	item.type = content_type.empty() ? std::string(anki_media_type(name)) : std::string(content_type);
	s.items[path] = std::move(item);
	return "http://localhost:" + std::to_string(s.port) + path;
}

void MediaServer::clear() {
	std::lock_guard<std::mutex> lock(state_->mutex);
	state_->items.clear();
}

int MediaServer::fetched(std::string_view link) const {
	// "http://localhost:PORT/SECRET/NAME": the path begins at the third slash.
	size_t scheme = link.find("://");
	size_t slash = scheme == std::string_view::npos ? std::string_view::npos : link.find('/', scheme + 3);
	if (slash == std::string_view::npos) return 0;
	std::lock_guard<std::mutex> lock(state_->mutex);
	auto found = state_->items.find(std::string(link.substr(slash)));
	return found == state_->items.end() ? 0 : found->second.fetched;
}

int MediaServer::offered() const {
	std::lock_guard<std::mutex> lock(state_->mutex);
	return int(state_->items.size());
}

}  // namespace ore
