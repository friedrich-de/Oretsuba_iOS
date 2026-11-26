// What the mining kit needs of the system (mining/kit_host.h), for the Linux
// test host: HTTP over plain sockets. It reaches http addresses only, which
// is what a test needs: an AnkiConnect, an audio source and a dictionary to
// download on the same machine or network. A voice stays as the game has it.
#include "mining/kit_host.h"
#if ORE_MINING_KIT

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ore {
namespace {

using Clock = std::chrono::steady_clock;

std::string lower(std::string s) {
	for (char &c : s)
		if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return s;
}

std::string trimmed(const std::string &s) {
	size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

struct Address {
	std::string host, port = "80", path = "/";
	std::string origin;  // "http://host:port"
};

bool read_address(const std::string &url, Address &out, std::string &problem) {
	std::string start = lower(url.substr(0, 8));
	if (start.rfind("https://", 0) == 0) {
		problem = "This test host reaches http addresses only.";
		return false;
	}
	if (start.rfind("http://", 0) != 0) {
		problem = "The address is not one.";
		return false;
	}
	size_t host = 7, end = url.find_first_of("/?#", host);
	std::string authority = url.substr(host, end == std::string::npos ? std::string::npos : end - host);
	if (authority.empty() || authority.find('@') != std::string::npos) {
		problem = "The address is not one.";
		return false;
	}
	out = Address();
	size_t colon = authority.rfind(':');
	if (authority[0] == '[') {
		size_t close = authority.find(']');
		if (close == std::string::npos) {
			problem = "The address is not one.";
			return false;
		}
		out.host = authority.substr(1, close - 1);
		if (close + 1 < authority.size() && authority[close + 1] == ':') out.port = authority.substr(close + 2);
	} else if (colon != std::string::npos) {
		out.host = authority.substr(0, colon);
		out.port = authority.substr(colon + 1);
	} else {
		out.host = authority;
	}
	if (end != std::string::npos) {
		size_t hash = url.find('#', end);
		std::string rest = url.substr(end, hash == std::string::npos ? std::string::npos : hash - end);
		out.path = rest[0] == '/' ? rest : "/" + rest;
	}
	out.origin = "http://" + authority;
	return true;
}

// Waits until the socket can be read or written. False when the time ran out or the request was given up.
bool wait_for(int fd, short what, int timeout_ms, const std::atomic<bool> *stop, bool &stopped) {
	Clock::time_point until = Clock::now() + std::chrono::milliseconds(timeout_ms);
	for (;;) {
		if (stop && stop->load()) {
			stopped = true;
			return false;
		}
		int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count());
		if (left <= 0) return false;
		pollfd p{fd, what, 0};
		int r = poll(&p, 1, std::min(left, 100));
		if (r > 0) return true;
		if (r < 0 && errno != EINTR) return false;
	}
}

int connect_to(const Address &address, int timeout_ms, const std::atomic<bool> *stop, std::string &problem) {
	addrinfo hints{}, *found = nullptr;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(address.host.c_str(), address.port.c_str(), &hints, &found) != 0 || !found) {
		problem = "The server's name is not known.";
		return -1;
	}
	int fd = -1;
	for (addrinfo *a = found; a; a = a->ai_next) {
		fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (fd < 0) continue;
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		bool stopped = false;
		int r = connect(fd, a->ai_addr, a->ai_addrlen);
		if (r == 0 || (errno == EINPROGRESS && wait_for(fd, POLLOUT, timeout_ms, stop, stopped))) {
			int err = 0;
			socklen_t len = sizeof err;
			getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
			if (err == 0) break;
		}
		close(fd);
		fd = -1;
		if (stopped) break;
	}
	freeaddrinfo(found);
	if (fd < 0) problem = "The server does not answer.";
	return fd;
}

bool send_all(int fd, const std::string &text, int timeout_ms, const std::atomic<bool> *stop) {
	size_t sent = 0;
	while (sent < text.size()) {
		ssize_t n = send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
		if (n > 0) {
			sent += size_t(n);
			continue;
		}
		bool stopped = false;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && wait_for(fd, POLLOUT, timeout_ms, stop, stopped)) continue;
		if (n < 0 && errno == EINTR) continue;
		return false;
	}
	return true;
}

// What comes off the socket, with what was read ahead.
struct Reader {
	int fd;
	int timeout_ms;
	const std::atomic<bool> *stop;
	std::string ahead;
	bool stopped = false;

	// Reads some more. False at the end, on an error or when the time ran out.
	bool more() {
		for (;;) {
			char block[16384];
			ssize_t n = recv(fd, block, sizeof block, 0);
			if (n > 0) {
				ahead.append(block, size_t(n));
				return true;
			}
			if (n == 0) return false;
			if (errno == EINTR) continue;
			if ((errno == EAGAIN || errno == EWOULDBLOCK) && wait_for(fd, POLLIN, timeout_ms, stop, stopped)) continue;
			return false;
		}
	}
	bool line(std::string &out) {
		size_t end;
		while ((end = ahead.find("\r\n")) == std::string::npos) {
			if (ahead.size() > 65536 || !more()) return false;
		}
		out = ahead.substr(0, end);
		ahead.erase(0, end + 2);
		return true;
	}
};

// Where the answer's body goes: into the answer or into a file.
struct Sink {
	const FetchRequest &request;
	FetchAnswer &answer;
	FILE *file = nullptr;
	uint64_t taken = 0, expected = 0;

	bool take(const char *data, size_t size) {
		if (taken + size > request.max_bytes) {
			answer.too_large = true;
			return false;
		}
		if (file) {
			if (fwrite(data, 1, size, file) != size) return false;
		} else {
			answer.body.append(data, size);
		}
		taken += size;
		if (request.progress && expected > 0) request.progress(std::min(1.0, double(taken) / double(expected)));
		return true;
	}
};

// One request without following where it points. False with the reason in answer.error.
bool once(const FetchRequest &request, const std::string &method, const std::string &url, const std::string &body, FetchAnswer &answer,
          std::string &location) {
	Address address;
	if (!read_address(url, address, answer.error)) return false;
	int fd = connect_to(address, request.timeout_ms, request.stop, answer.error);
	if (fd < 0) return false;
	std::string head = method + " " + address.path + " HTTP/1.1\r\nHost: " + address.origin.substr(7) + "\r\nConnection: close\r\nAccept: */*\r\n";
	for (const auto &header : request.headers) head += header.first + ": " + header.second + "\r\n";
	if (!body.empty() || method == "POST") head += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	head += "\r\n";
	Reader reader{fd, request.timeout_ms, request.stop, {}};
	std::string status_line;
	bool ok = send_all(fd, head + body, request.timeout_ms, request.stop) && reader.line(status_line);
	int status = 0;
	if (ok) ok = sscanf(status_line.c_str(), "HTTP/%*d.%*d %d", &status) == 1;
	bool chunked = false;
	int64_t length = -1;
	std::string type;
	while (ok) {
		std::string line;
		if (!reader.line(line)) {
			ok = false;
			break;
		}
		if (line.empty()) break;
		size_t colon = line.find(':');
		if (colon == std::string::npos) continue;
		std::string name = lower(line.substr(0, colon)), value = trimmed(line.substr(colon + 1));
		if (name == "content-length") length = atoll(value.c_str());
		else if (name == "transfer-encoding") chunked = lower(value).find("chunked") != std::string::npos;
		else if (name == "content-type") type = lower(trimmed(value.substr(0, value.find(';'))));
		else if (name == "location") location = value;
	}
	if (!ok) {
		close(fd);
		answer.error = reader.stopped ? "Given up." : "The server does not answer.";
		return false;
	}
	bool redirect = (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && !location.empty();
	Sink sink{request, answer};
	if (!redirect && !request.to_file.empty() && status >= 200 && status < 300) {
		sink.file = fopen(request.to_file.c_str(), "wb");
		if (!sink.file) {
			close(fd);
			answer.error = "The file cannot be written.";
			return false;
		}
	}
	sink.expected = length > 0 ? uint64_t(length) : 0;
	answer.status = status;
	answer.content_type = type;
	answer.final_url = url;
	bool whole = true;
	if (method == "HEAD" || status == 204 || status == 304) {
	} else if (chunked) {
		for (;;) {
			std::string line;
			if (!reader.line(line)) {
				whole = false;
				break;
			}
			uint64_t size = strtoull(line.c_str(), nullptr, 16);
			if (size == 0) break;
			while (reader.ahead.size() < size + 2 && reader.more()) {
			}
			if (reader.ahead.size() < size || !sink.take(reader.ahead.data(), size_t(size))) {
				whole = false;
				break;
			}
			reader.ahead.erase(0, size_t(size) + 2);
		}
	} else {
		uint64_t left = length >= 0 ? uint64_t(length) : UINT64_MAX;
		while (left > 0) {
			if (reader.ahead.empty() && !reader.more()) break;
			size_t part = size_t(std::min<uint64_t>(left, reader.ahead.size()));
			if (!sink.take(reader.ahead.data(), part)) {
				whole = false;
				break;
			}
			reader.ahead.erase(0, part);
			left -= part;
		}
		if (length >= 0 && left > 0) whole = false;
	}
	close(fd);
	if (sink.file) fclose(sink.file);
	if (!whole) {
		if (sink.file) remove(request.to_file.c_str());
		answer.body.clear();
		if (answer.too_large) return true;
		answer.status = 0;
		answer.error = reader.stopped ? "Given up." : "The answer broke off.";
		return false;
	}
	return true;
}

std::string pointed_to(const std::string &url, const std::string &location) {
	if (lower(location.substr(0, 7)) == "http://" || lower(location.substr(0, 8)) == "https://") return location;
	Address address;
	std::string problem;
	if (!read_address(url, address, problem)) return {};
	if (!location.empty() && location[0] == '/') return address.origin + location;
	std::string path = address.path.substr(0, address.path.find('?'));
	return address.origin + path.substr(0, path.rfind('/') + 1) + location;
}

void fetch(const FetchRequest &request, FetchAnswer &answer) {
	std::string method = request.method.empty() ? "GET" : request.method, url = request.url, body = request.body;
	for (int hop = 0; hop < 6; ++hop) {
		answer = FetchAnswer();
		std::string location;
		if (!once(request, method, url, body, answer, location)) return;
		if (answer.too_large || location.empty() || answer.status < 300 || answer.status >= 400) return;
		if (answer.status != 307 && answer.status != 308 && method != "GET" && method != "HEAD") {
			method = "GET";
			body.clear();
		}
		url = pointed_to(url, location);
	}
	answer = FetchAnswer();
	answer.error = "The server points on and on.";
}

}  // namespace

HttpFetch host_http_fetcher() {
	return fetch;
}

VoiceEncoder host_voice_encoder() {
	return nullptr;
}

}  // namespace ore

#endif  // ORE_MINING_KIT
