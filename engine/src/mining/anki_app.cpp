// Cards for the Anki app on the same device: links, lists and the media server.
#include "mining/anki_app.h"

#include <algorithm>
#include <chrono>
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

// ---------------------------------------------------------------- one card on its way to AnkiMobile

AnkiMobileHandover::AnkiMobileHandover(std::string return_link) : return_link_(std::move(return_link)) {}

AnkiMobileHandover::~AnkiMobileHandover() = default;

bool AnkiMobileHandover::prepare(AnkiLinkNote note, std::vector<File> files, Prepared &out, std::string &problem) {
	std::lock_guard<std::mutex> lock(mutex_);
	card_.clear();
	offered_.clear();
	left_out_.clear();
	Prepared prepared;
	bool any = false;
	for (const File &file : files) any = any || (!file.bytes.empty() && anki_media_name(file.name));
	// A fresh server for every card: an app that slept in between may have lost its sockets.
	if (any && !server_.start(problem)) return false;
	if (!any) server_.stop();
	for (File &file : files) {
		std::string name = anki_media_safe_name(file.name);
		bool placed = false;
		if (!file.bytes.empty() && anki_media_name(name)) {
			std::string link;
			for (const std::string &field_name : file.fields) {
				auto field = std::find_if(note.fields.begin(), note.fields.end(),
				                          [&](const std::pair<std::string, std::string> &f) { return f.first == field_name; });
				if (field == note.fields.end() || !field->second.empty()) continue;
				if (link.empty()) link = server_.offer(name, std::move(file.bytes));
				if (link.empty()) break;
				field->second = link;
				placed = true;
			}
			if (placed) offered_.emplace_back(name, link);
		}
		if (!placed) prepared.left_out.push_back(file.name);
	}
	static const char kHex[] = "0123456789abcdef";
	std::random_device device;
	for (int i = 0; i < 2; ++i) {
		uint32_t word = device();
		for (int j = 0; j < 8; ++j) card_ += kHex[(word >> (j * 4)) & 15];
	}
	prepared.card = card_;
	left_out_ = prepared.left_out;
	std::string back = return_link_ + (return_link_.find('?') == std::string::npos ? "?card=" : "&card=") + card_;
	prepared.link = ankimobile_add_link(note, back);
	out = std::move(prepared);
	problem.clear();
	return true;
}

AnkiMobileHandover::Result AnkiMobileHandover::finish(Result::Kind kind) {
	Result result;
	result.kind = kind;
	result.card = card_;
	for (const auto &file : offered_)
		if (server_.fetched(file.second) == 0) result.not_fetched.push_back(file.first);
	result.left_out = std::move(left_out_);
	card_.clear();
	offered_.clear();
	left_out_.clear();
	server_.stop();
	return result;
}

AnkiMobileHandover::Result AnkiMobileHandover::returned(std::string_view link) {
	std::lock_guard<std::mutex> lock(mutex_);
	std::string card;
	size_t mark = link.find('?');
	bool ours = !card_.empty() && link.substr(0, mark) == std::string_view(return_link_).substr(0, return_link_.find('?')) &&
	            link_query_value(link, "card", card) && card == card_;
	if (!ours) return Result();
	return finish(Result::Added);
}

AnkiMobileHandover::Result AnkiMobileHandover::gave_up() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (card_.empty()) return Result();
	Result result = finish(Result::Unconfirmed);
	result.not_fetched.clear();  // nothing is known of a card that never came back
	return result;
}

bool AnkiMobileHandover::waiting() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return !card_.empty();
}

std::string AnkiMobileHandover::card() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return card_;
}

// ---------------------------------------------------------------- from the host

AppReturns &AppReturns::shared() {
	static AppReturns instance;
	return instance;
}

int AppReturns::listen(Listener listener) {
	std::lock_guard<std::mutex> lock(mutex_);
	int id = next_++;
	listeners_.emplace_back(id, std::make_shared<Listener>(std::move(listener)));
	return id;
}

void AppReturns::forget(int id) {
	std::lock_guard<std::mutex> lock(mutex_);
	listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto &entry) { return entry.first == id; }),
	                 listeners_.end());
}

void AppReturns::tell(const std::string &link) {
	std::vector<std::shared_ptr<Listener>> all;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (const auto &entry : listeners_) all.push_back(entry.second);
	}
	for (const auto &listener : all) (*listener)(link);
}

void AppReturns::opened_with(const std::string &link) {
	if (!link.empty()) tell(link);
}

void AppReturns::in_front() { tell(std::string()); }

// ---------------------------------------------------------------- AnkiMobile, from asking to knowing

namespace {

constexpr char kNotThere[] = "AnkiMobile did not open. Is it on this device?";
constexpr char kNoWord[] =
    "AnkiMobile did not say that it added the card. Look there: it may have refused it, for example a word it has already.";
constexpr char kNoLists[] = "AnkiMobile left no answer. Allow the request there and fetch again.";

json::Value info_json(const AnkiAppInfo &info) {
	json::Value value;
	json::parse(anki_app_info_text(info), value);
	return value;
}

void sleep_ms(int ms) {
	if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace

struct AnkiMobile::State {
	State(AnkiAppHost h, std::string s, Tell t, Timing g)
	    : host(std::move(h)), scheme(std::move(s)), tell(std::move(t)), timing(g), handover(scheme + "://anki/added") {}

	const AnkiAppHost host;
	const std::string scheme;
	const Tell tell;
	const Timing timing;
	AnkiMobileHandover handover;

	std::mutex mutex;
	bool lists_asked = false;   // the question for the lists is out
	bool lists_opened = false;  // and its link was opened: AnkiMobile has it
	uint64_t lists_round = 0;   // which question: an older one's timers find a newer number and stop
	std::string opened_card;    // the card whose link was opened last

	// `tell` reaches into whoever owns this object, so nothing is told once the owner lets go.
	std::recursive_mutex telling;
	bool alive = true;
	int listener = 0;

	void say(const std::string &event, const json::Value &payload) {
		std::lock_guard<std::recursive_mutex> lock(telling);
		if (alive && tell) tell(event, json::write(payload));
	}

	void say_unconfirmed(const std::string &card, const char *reason) {
		json::Value payload = json::Value::object();
		payload.set("card", card);
		payload.set("reason", reason);
		say("card.unconfirmed", payload);
	}

	void say_no_lists(const char *problem) {
		json::Value payload = json::Value::object();
		payload.set("ok", false);
		payload.set("problem", problem);
		say("anki.lists", payload);
	}

	// A link this app was opened with, or "" for being in front again.
	static void heard(const std::shared_ptr<State> &state, const std::string &link) {
		if (link.empty()) {
			// In front again. AnkiMobile's way back, when there is one, arrives
			// about now; only when none has come after a while is the card given up.
			// That counts only for what AnkiMobile was already handed: word of
			// being in front can come late, after the next link was asked for and
			// before it is opened, and then it is the end of the trip before.
			std::string card = state->handover.card();
			uint64_t round;
			bool asked;
			{
				std::lock_guard<std::mutex> lock(state->mutex);
				asked = state->lists_asked && state->lists_opened;
				round = state->lists_round;
				if (card != state->opened_card) card.clear();
			}
			if (card.empty() && !asked) return;
			std::thread([state, card, asked, round] {
				sleep_ms(state->timing.confirm_ms);
				if (!card.empty() && state->handover.card() == card) {
					AnkiMobileHandover::Result result = state->handover.gave_up();
					if (result.kind == AnkiMobileHandover::Result::Unconfirmed && result.card == card)
						state->say_unconfirmed(card, kNoWord);
				}
				if (asked) {
					bool still;
					{
						std::lock_guard<std::mutex> lock(state->mutex);
						still = state->lists_asked && state->lists_round == round;
						if (still) state->lists_asked = false;
					}
					if (still) state->say_no_lists(kNoLists);
				}
			}).detach();
			return;
		}
		std::string head = link.substr(0, link.find('?'));
		if (head == state->scheme + "://anki/added") {
			AnkiMobileHandover::Result result = state->handover.returned(link);
			if (result.kind != AnkiMobileHandover::Result::Added) return;
			json::Value payload = json::Value::object();
			payload.set("card", result.card);
			json::Value missing = json::Value::array();
			for (const std::string &name : result.not_fetched) missing.push(name);
			payload.set("notFetched", std::move(missing));
			json::Value left_out = json::Value::array();
			for (const std::string &name : result.left_out) left_out.push(name);
			payload.set("leftOut", std::move(left_out));
			state->say("card.added", payload);
		} else if (head == state->scheme + "://anki/lists") {
			{
				std::lock_guard<std::mutex> lock(state->mutex);
				if (!state->lists_asked) return;
				state->lists_asked = false;
			}
			// The answer lies on the clipboard, and may take a moment to get there.
			std::thread([state] {
				std::string text;
				for (int i = 0; i < state->timing.answer_tries && text.empty(); ++i) {
					sleep_ms(state->timing.answer_wait_ms);
					text = state->host.take_answer();
				}
				if (text.empty()) return state->say_no_lists(kNoLists);
				AnkiAppInfo info;
				std::string problem;
				if (!ankimobile_parse_info(text, info, problem)) return state->say_no_lists(problem.c_str());
				json::Value payload = json::Value::object();
				payload.set("ok", true);
				payload.set("lists", info_json(info));
				state->say("anki.lists", payload);
			}).detach();
		}
	}
};

AnkiMobile::AnkiMobile(AnkiAppHost host, std::string scheme, Tell tell)
    : AnkiMobile(std::move(host), std::move(scheme), std::move(tell), Timing()) {}

AnkiMobile::AnkiMobile(AnkiAppHost host, std::string scheme, Tell tell, Timing timing)
    : state_(std::make_shared<State>(std::move(host), std::move(scheme), std::move(tell), timing)) {
	std::weak_ptr<State> weak = state_;
	state_->listener = AppReturns::shared().listen([weak](const std::string &link) {
		if (std::shared_ptr<State> state = weak.lock()) State::heard(state, link);
	});
}

AnkiMobile::~AnkiMobile() {
	AppReturns::shared().forget(state_->listener);
	std::lock_guard<std::recursive_mutex> lock(state_->telling);
	state_->alive = false;
}

bool AnkiMobile::possible() const { return state_->host.open_link && state_->host.take_answer; }

bool AnkiMobile::ask_lists(std::string &problem) {
	if (!possible()) {
		problem = "This device cannot reach AnkiMobile.";
		return false;
	}
	std::shared_ptr<State> state = state_;
	uint64_t round;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		state->lists_asked = true;
		state->lists_opened = false;
		round = ++state->lists_round;
	}
	state->host.open_link(ankimobile_info_link(state->scheme + "://anki/lists"), [state, round](bool opened) {
		if (opened) {
			std::lock_guard<std::mutex> lock(state->mutex);
			if (state->lists_round == round) state->lists_opened = true;
			return;
		}
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if (!state->lists_asked || state->lists_round != round) return;
			state->lists_asked = false;
		}
		state->say_no_lists(kNotThere);
	});
	problem.clear();
	return true;
}

bool AnkiMobile::add(AnkiLinkNote note, std::vector<AnkiMobileHandover::File> files, std::string &card,
                     std::vector<std::string> &left_out, std::string &problem) {
	if (!state_->host.open_link) {
		problem = "This device cannot reach AnkiMobile.";
		return false;
	}
	AnkiMobileHandover::Prepared prepared;
	if (!state_->handover.prepare(std::move(note), std::move(files), prepared, problem)) return false;
	card = prepared.card;
	left_out = std::move(prepared.left_out);
	std::shared_ptr<State> state = state_;
	std::string name = card;
	state->host.open_link(prepared.link, [state, name](bool opened) {
		if (opened) {
			std::lock_guard<std::mutex> lock(state->mutex);
			state->opened_card = name;
			return;
		}
		if (state->handover.card() != name) return;
		AnkiMobileHandover::Result result = state->handover.gave_up();
		if (result.kind == AnkiMobileHandover::Result::Unconfirmed && result.card == name) state->say_unconfirmed(name, kNotThere);
	});
	problem.clear();
	return true;
}

// ---------------------------------------------------------------- AnkiDroid

std::string ankidroid_add_request(const AnkiLinkNote &note, const std::vector<AnkiDroidFile> &files) {
	json::Value root = json::Value::object();
	root.set("deck", note.deck);
	root.set("noteType", note.note_type);
	json::Value fields = json::Value::object();
	for (const auto &field : note.fields) fields.set(field.first, field.second);
	root.set("fields", std::move(fields));
	root.set("tags", names_json(note.tags));
	root.set("allowDuplicate", note.allow_duplicate);
	json::Value media = json::Value::array();
	for (const AnkiDroidFile &file : files) {
		json::Value entry = json::Value::object();
		entry.set("name", file.name);
		entry.set("sound", file.sound);
		entry.set("fields", names_json(file.fields));
		media.push(std::move(entry));
	}
	root.set("media", std::move(media));
	return json::write(root);
}

std::string ankidroid_known_request(std::string_view note_type, std::string_view field, std::string_view value) {
	json::Value root = json::Value::object();
	root.set("noteType", note_type);
	root.set("field", field);
	root.set("value", value);
	return json::write(root);
}

std::string ankidroid_newest_request(std::string_view note_type) {
	json::Value root = json::Value::object();
	root.set("noteType", note_type);
	return json::write(root);
}

AnkiDroidAnswer ankidroid_answer(std::string_view text) {
	AnkiDroidAnswer answer;
	json::Value root;
	if (!json::parse(text, root) || !root.is_object()) {
		answer.problem = "AnkiDroid's answer could not be read.";
		answer.code = "failed";
		return answer;
	}
	if (root.has("error")) {
		answer.problem = root.str("error");
		if (answer.problem.empty()) answer.problem = "AnkiDroid refused.";
		answer.code = root.str("code", "failed");
		return answer;
	}
	answer.ok = true;
	answer.installed = root.boolean("installed");
	answer.allowed = root.boolean("allowed");
	answer.media_folder = root.str("mediaFolder");
	anki_app_info_from_text(text, answer.info);
	answer.known = root.boolean("known");
	answer.deck = root.str("deck");
	answer.id = root.str("id");
	add_names(root["missing"], answer.missing);
	return answer;
}

}  // namespace ore
