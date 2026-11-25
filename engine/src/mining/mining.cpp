// Mining (docs/MINING.md): the engine-free part.
#include "mining/mining.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

#include "util/log.h"

// The JPEG writer, kept to this file so that it cannot meet another copy of
// stb_image_write in a host program.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ore {
namespace {

std::string trim_ascii(const std::string &s) {
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return std::string();
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

std::string lower_ascii(std::string s) {
	for (char &c : s)
		if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return s;
}

std::string unquote(const std::string &s) {
	if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) return s.substr(1, s.size() - 2);
	return s;
}

bool parse_switch(const std::string &value, bool &out) {
	std::string v = lower_ascii(value);
	if (v == "yes" || v == "true" || v == "on" || v == "1") return out = true, true;
	if (v == "no" || v == "false" || v == "off" || v == "0") return out = false, true;
	return false;
}

// Printable ASCII without blanks: what an address or a key may hold.
bool is_token(const std::string &s) {
	for (unsigned char c : s)
		if (c <= 0x20 || c >= 0x7f) return false;
	return !s.empty();
}

bool is_utf8(const std::string &s) {
	size_t i = 0;
	while (i < s.size()) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		size_t extra;
		if (c < 0x80) extra = 0;
		else if ((c >> 5) == 0x6) extra = 1;
		else if ((c >> 4) == 0xe) extra = 2;
		else if ((c >> 3) == 0x1e) extra = 3;
		else return false;
		if (i + extra >= s.size()) return false;
		for (size_t k = 1; k <= extra; ++k)
			if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) return false;
		i += extra + 1;
	}
	return true;
}

std::string without_scheme(const std::string &server) {
	size_t at = server.find("://");
	return at == std::string::npos ? server : server.substr(at + 3);
}

std::string find_in_dir(const std::string &dir) {
	DIR *d = opendir(dir.c_str());
	if (!d) return std::string();
	std::string found;
	while (struct dirent *entry = readdir(d)) {
		if (lower_ascii(entry->d_name) != kMiningFileName) continue;
		found = dir + (dir.back() == '/' ? "" : "/") + entry->d_name;
		break;
	}
	closedir(d);
	struct stat st;
	if (found.empty() || stat(found.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return std::string();
	return found;
}

std::string parent_of(const std::string &dir) {
	size_t slash = dir.rfind('/');
	if (slash == std::string::npos || dir.size() == 1) return std::string();
	return slash == 0 ? "/" : dir.substr(0, slash);
}

}  // namespace

// ---------------------------------------------------------------- mining.txt

bool parse_mining_settings(const std::string &text, MiningSettings &out, std::string &problem) {
	MiningSettings s;
	size_t i = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
	while (i < text.size()) {
		size_t end = text.find('\n', i);
		if (end == std::string::npos) end = text.size();
		std::string line = trim_ascii(text.substr(i, end - i));
		i = end + 1;
		if (line.empty() || line[0] == '#' || line[0] == ';') continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string name = lower_ascii(trim_ascii(line.substr(0, eq)));
		std::string value = unquote(trim_ascii(line.substr(eq + 1)));
		if (name == "server") {
			s.server = value;
		} else if (name == "key") {
			s.key = value;
		} else if (name == "game") {
			s.game = value;
		} else if (name == "voice" || name == "screenshot") {
			bool on = true;
			if (!parse_switch(value, on)) {
				problem = "mining.txt: \"" + name + "\" takes yes or no.";
				return false;
			}
			(name == "voice" ? s.voice : s.screenshot) = on;
		}
	}
	if (s.server.empty()) {
		problem = "mining.txt has no line \"server = https://...\".";
		return false;
	}
	size_t sep = s.server.find("://");
	std::string scheme = sep == std::string::npos ? std::string() : lower_ascii(s.server.substr(0, sep + 3));
	if (scheme != "https://" && scheme != "http://") {
		problem = "The server in mining.txt has to begin with https:// or http://.";
		return false;
	}
	while (!s.server.empty() && s.server.back() == '/') s.server.pop_back();
	if (s.server.size() <= scheme.size() || !is_token(s.server)) {
		problem = "The server in mining.txt is not an address.";
		return false;
	}
	s.server.replace(0, scheme.size(), scheme);
	if (s.key.empty()) {
		problem = "mining.txt has no line \"key = ...\".";
		return false;
	}
	if (!is_token(s.key)) {
		problem = "The key in mining.txt must not hold blanks or other signs than letters, digits and marks.";
		return false;
	}
	if (!is_utf8(s.game)) {
		problem = "mining.txt has to be saved as UTF-8.";
		return false;
	}
	out = s;
	return true;
}

std::string find_mining_file(const std::string &game_dir) {
	std::string dir = game_dir;
	char resolved[PATH_MAX];
#ifdef _WIN32
	if (_fullpath(resolved, dir.c_str(), sizeof resolved)) dir = resolved;
#else
	if (realpath(dir.c_str(), resolved)) dir = resolved;
#endif
	while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
	for (int level = 0; level < 3 && !dir.empty(); ++level) {
		std::string found = find_in_dir(dir);
		if (!found.empty()) return found;
		dir = parent_of(dir);
	}
	return std::string();
}

int load_mining_settings(const std::string &game_dir, MiningSettings &out, std::string &problem) {
	problem.clear();
	std::string path = find_mining_file(game_dir);
	if (path.empty()) return 0;
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		problem = "mining.txt cannot be read.";
		return -1;
	}
	std::string text(64 * 1024, '\0');
	text.resize(fread(&text[0], 1, text.size(), f));
	fclose(f);
	return parse_mining_settings(text, out, problem) ? 1 : -1;
}

// ---------------------------------------------------------------- the text of a line

std::string mining_plain_text(const std::string &b, TextEncoding enc) {
	std::string out;
	size_t i = 0;
	while (i < b.size()) {
		uint8_t c = uint8_t(b[i]);
		if (c == '$' || c == '&' || c == '\\') {  // waits, new page, line break
			++i;
			continue;
		}
		if (c == '<') {
			if (i + 1 < b.size() && b[i + 1] == '<') {
				out += '<';
				i += 2;
				continue;
			}
			size_t close = b.find('>', i);
			if (close == std::string::npos) {  // not a tag: the engine draws it
				out += '<';
				++i;
				continue;
			}
			// <N> changes the size; <"text"> is drawn as it stands
			size_t s = i + 1;
			while (s < close && (b[s] == ' ' || b[s] == '\t')) ++s;
			if (s < close && b[s] == '"') {
				size_t quote = b.find('"', s + 1);
				if (quote != std::string::npos && quote < close) out.append(b, s + 1, quote - s - 1);
			}
			i = close + 1;
			continue;
		}
		if (c == '@') {
			if (i + 1 < b.size() && b[i + 1] == '@') {
				out += '@';
				i += 2;
				continue;
			}
			++i;
			while (i < b.size() && b[i] >= '0' && b[i] <= '9') ++i;
			continue;
		}
		if (c == '[' && i + 1 < b.size()) {
			if (b[i + 1]) out += b[i + 1];
			i += 2;
			continue;
		}
		if (is_lead_byte(c, enc) && i + 1 < b.size()) {
			out.append(b, i, 2);
			i += 2;
			continue;
		}
		if (c >= 0x20) out += char(c);
		++i;
	}
	return out;
}

std::string mining_trim(const std::string &s) {
	size_t a = 0, b = s.size();
	for (;;) {
		if (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
		else if (b - a >= 3 && s.compare(a, 3, "\xE3\x80\x80") == 0) a += 3;
		else break;
	}
	for (;;) {
		if (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
		else if (b - a >= 3 && s.compare(b - 3, 3, "\xE3\x80\x80") == 0) b -= 3;
		else break;
	}
	return s.substr(a, b - a);
}

// ---------------------------------------------------------------- which lines are sent

std::string LineGate::key_of(const Record &r) {
	std::string key = std::to_string(r.type) + ":" + std::to_string(r.line) + ":";
	if (r.name) key += *r.name;
	key += '\n';
	if (r.body) key += *r.body;
	return key;
}

void LineGate::shown(const Record &record, int64_t now) {
	key_ = key_of(record);
	at_ = now;
	waited_ = false;
	if (next_known_) {
		next_known_ = false;
		sent_ = key_;
	}
	// the same record shown again (after the history screen, a menu, a load) was sent already
	armed_ = key_ != sent_;
}

bool LineGate::alive(int state, const Record &current) {
	if (!armed_) return false;
	if (state == 2) waited_ = true;
	// The line is lost when it was skipped (4), when another record took its place, or when it
	// ended (3) after waiting: then the reader clicked it away. A line that ends without having
	// waited was let through by the script, as the question before a choice is, and stays in view.
	if (state == 4 || (state == 3 && waited_) || key_of(current) != key_) armed_ = false;
	return armed_;
}

bool LineGate::frame(int state, const Record &current, int64_t now) {
	if (!alive(state, current)) return false;
	if (state < 2 || now - at_ < kDwellMs) return false;
	armed_ = false;
	sent_ = key_;
	return true;
}

bool LineGate::leaving(int state, const Record &current) {
	if (!alive(state, current)) return false;
	armed_ = false;
	sent_ = key_;
	return true;
}

// ---------------------------------------------------------------- the request

std::vector<uint8_t> mining_jpeg(const uint32_t *pixels, int width, int height, int quality) {
	std::vector<uint8_t> out;
	if (!pixels || width <= 0 || height <= 0) return out;
	size_t count = size_t(width) * size_t(height);
	std::vector<uint8_t> rgb(count * 3);
	uint8_t *d = rgb.data();
	for (size_t i = 0; i < count; ++i) {
		uint32_t p = pixels[i];
		*d++ = uint8_t(p >> 16);
		*d++ = uint8_t(p >> 8);
		*d++ = uint8_t(p);
	}
	out.reserve(192 * 1024);
	auto sink = [](void *context, void *data, int size) {
		auto *bytes = static_cast<std::vector<uint8_t> *>(context);
		const uint8_t *from = static_cast<const uint8_t *>(data);
		bytes->insert(bytes->end(), from, from + size);
	};
	if (!stbi_write_jpg_to_func(sink, &out, width, height, 3, rgb.data(), quality)) out.clear();
	return out;
}

namespace {

void add_field(std::string &body, const std::string &boundary, const char *name, const std::string &value) {
	body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
	body += value;
	body += "\r\n";
}

void add_file(std::string &body, const std::string &boundary, const char *name, const std::string &file_name,
              const char *type, const std::vector<uint8_t> &data) {
	std::string safe;
	for (char c : file_name)
		safe += (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-'
		            ? c
		            : '_';
	body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"; filename=\"" + safe +
	        "\"\r\nContent-Type: " + type + "\r\n\r\n";
	body.append(reinterpret_cast<const char *>(data.data()), data.size());
	body += "\r\n";
}

const char *audio_type(const std::string &file_name) {
	size_t dot = file_name.rfind('.');
	std::string ext = dot == std::string::npos ? std::string() : lower_ascii(file_name.substr(dot + 1));
	if (ext == "ogg" || ext == "oga") return "audio/ogg";
	if (ext == "mp3") return "audio/mpeg";
	if (ext == "wav") return "audio/wav";
	if (ext == "flac") return "audio/flac";
	if (ext == "m4a") return "audio/mp4";
	return "application/octet-stream";
}

std::string new_boundary(std::mt19937_64 &rng) {
	static const char hex[] = "0123456789abcdef";
	std::string boundary = "----ore";
	for (int i = 0; i < 4; ++i) {
		uint64_t v = rng();
		for (int k = 0; k < 8; ++k) boundary += hex[(v >> (k * 4)) & 15];
	}
	return boundary;
}

}  // namespace

HttpRequest mining_line_request(const MiningSettings &settings, const MiningLine &line, const std::string &boundary) {
	HttpRequest request;
	request.method = "POST";
	request.url = settings.server + "/Api/Lines";
	request.headers.emplace_back("Authorization", "Bearer " + settings.key);
	request.headers.emplace_back("Content-Type", "multipart/form-data; boundary=" + boundary);
	std::string &body = request.body;
	body.reserve(1024 + line.text.size() + line.audio.size() + line.picture.size());
	add_field(body, boundary, "text", line.text);
	if (!line.name.empty()) add_field(body, boundary, "name", line.name);
	if (!line.game.empty()) add_field(body, boundary, "game", line.game);
	if (!line.voice.empty()) add_field(body, boundary, "voice", line.voice);
	if (!line.audio.empty()) {
		std::string name = line.audio_name.empty() ? "voice.ogg" : line.audio_name;
		add_file(body, boundary, "audio", name, audio_type(name), line.audio);
	}
	if (!line.picture.empty()) add_file(body, boundary, "screenshot", "screenshot.jpg", "image/jpeg", line.picture);
	body += "--" + boundary + "--\r\n";
	return request;
}

bool mining_check(const MiningSettings &settings, const HttpSend &send, std::string &message) {
	std::string host = without_scheme(settings.server);
	if (!send) {
		message = "Mining: this build cannot send lines.";
		return false;
	}
	HttpRequest request;
	request.method = "GET";
	request.url = settings.server + "/Api/Status";
	request.headers.emplace_back("Authorization", "Bearer " + settings.key);
	request.timeout_ms = 8000;
	std::string answer;
	int status = send(request, answer);
	if (status == 200) {
		// {"Version":"0.1.0","Lines":12,"MaxLines":1000}
		std::string version;
		size_t at = answer.find("\"Version\":\"");
		if (at != std::string::npos) {
			size_t from = at + 11, to = answer.find('"', from);
			if (to != std::string::npos && to - from <= 32) version = answer.substr(from, to - from);
		}
		message = "Mining: lines go to " + host + (version.empty() ? "." : " (VN Mining Server " + version + ").");
		return true;
	}
	if (status == 401 || status == 403) {
		message = "Mining: " + host + " does not take the key in mining.txt.";
	} else if (status == 429) {
		message = "Mining: " + host + " takes no keys for a minute, after too many wrong ones.";
	} else if (status <= 0) {
		message = "Mining: " + host + " could not be reached" + (answer.empty() ? "." : ": " + answer);
	} else {
		message = "Mining: " + host + " answered " + std::to_string(status) + ", which a mining server does not.";
	}
	return false;
}

// ---------------------------------------------------------------- the queue

struct MiningQueue::State {
	struct Item {
		uint64_t id = 0;
		MiningLine line;
		std::shared_ptr<const HttpRequest> request;  // built when the line is first sent
		int attempts = 0;
	};

	MiningSettings settings;
	HttpSend send;
	Options options;

	std::mutex mu;
	std::condition_variable wake;  // for the worker: a line came, or it is time to stop
	std::condition_variable idle;  // for those who wait: the worker got something done
	std::deque<Item> items;
	uint64_t next_id = 1;
	uint64_t in_flight = 0;  // the id of the line being sent
	int failures = 0;        // requests in a row that brought no line through
	std::chrono::steady_clock::time_point retry_at;
	bool stop = false, done = false;
	Stats stats;

	void pop_front_if(uint64_t id) {
		if (!items.empty() && items.front().id == id) items.pop_front();
	}
};

MiningQueue::MiningQueue(MiningSettings settings, HttpSend send) : MiningQueue(std::move(settings), std::move(send), Options()) {}

MiningQueue::MiningQueue(MiningSettings settings, HttpSend send, Options options) : state_(std::make_shared<State>()) {
	state_->settings = std::move(settings);
	state_->send = std::move(send);
	state_->options = options;
	// The worker holds the state itself: it may still sit in a request when the game is long gone.
	std::thread(run, state_).detach();
}

MiningQueue::~MiningQueue() {
	State &s = *state_;
	std::unique_lock<std::mutex> lock(s.mu);
	// Lines on their way get a moment, unless the server was away anyhow.
	if (s.failures == 0 && s.options.leave_ms > 0)
		s.idle.wait_for(lock, std::chrono::milliseconds(s.options.leave_ms),
		                [&s] { return s.done || (s.items.empty() && s.in_flight == 0); });
	s.stop = true;
	s.wake.notify_all();
	s.idle.wait_for(lock, std::chrono::milliseconds(200), [&s] { return s.done; });
}

void MiningQueue::push(MiningLine line) {
	State &s = *state_;
	std::lock_guard<std::mutex> lock(s.mu);
	if (s.stop || s.stats.refused) {
		++s.stats.dropped;
		return;
	}
	// A raw screen is big. When the worker cannot keep up with making pictures, a line goes without one.
	int raw = 0;
	for (const State::Item &item : s.items) raw += !item.line.pixels.empty();
	if (raw >= s.options.max_raw) std::vector<uint32_t>().swap(line.pixels);
	while (int(s.items.size()) >= std::max(1, s.options.max_pending)) {
		// the oldest goes; the one in the middle of being sent is left to finish
		auto oldest = s.items.begin();
		if (oldest->id == s.in_flight) ++oldest;
		if (oldest == s.items.end()) break;
		s.items.erase(oldest);
		++s.stats.dropped;
	}
	State::Item item;
	item.id = s.next_id++;
	item.line = std::move(line);
	s.items.push_back(std::move(item));
	s.wake.notify_all();
}

bool MiningQueue::flush(int timeout_ms) {
	State &s = *state_;
	std::unique_lock<std::mutex> lock(s.mu);
	s.idle.wait_for(lock, std::chrono::milliseconds(timeout_ms),
	                [&s] { return s.done || (s.items.empty() && s.in_flight == 0); });
	return s.items.empty() && s.in_flight == 0;
}

MiningQueue::Stats MiningQueue::stats() const {
	State &s = *state_;
	std::lock_guard<std::mutex> lock(s.mu);
	Stats stats = s.stats;
	stats.pending = int(s.items.size());
	return stats;
}

void MiningQueue::run(std::shared_ptr<State> state) {
	State &s = *state;
	std::mt19937_64 rng(std::random_device{}());
	std::unique_lock<std::mutex> lock(s.mu);
	while (!s.stop) {
		// Screens are made into pictures first, also while the server is away: they are what takes room.
		auto raw = std::find_if(s.items.begin(), s.items.end(),
		                        [](const State::Item &item) { return !item.line.pixels.empty(); });
		if (raw != s.items.end()) {
			uint64_t id = raw->id;
			int width = raw->line.width, height = raw->line.height;
			std::vector<uint32_t> pixels;
			pixels.swap(raw->line.pixels);
			int quality = s.options.jpeg_quality;
			lock.unlock();
			std::vector<uint8_t> picture;
			if (width > 0 && height > 0 && pixels.size() == size_t(width) * size_t(height))
				picture = mining_jpeg(pixels.data(), width, height, quality);
			lock.lock();
			for (State::Item &item : s.items)
				if (item.id == id) item.line.picture = std::move(picture);
			continue;
		}
		if (s.items.empty()) {
			s.idle.notify_all();
			s.wake.wait(lock);
			continue;
		}
		auto now = std::chrono::steady_clock::now();
		if (s.failures > 0 && now < s.retry_at) {
			s.wake.wait_until(lock, s.retry_at);
			continue;
		}

		State::Item &head = s.items.front();
		if (!head.request) {
			head.request = std::make_shared<const HttpRequest>(mining_line_request(s.settings, head.line, new_boundary(rng)));
			std::vector<uint8_t>().swap(head.line.audio);
			std::vector<uint8_t>().swap(head.line.picture);
		}
		std::shared_ptr<const HttpRequest> request = head.request;
		uint64_t id = head.id;
		s.in_flight = id;
		lock.unlock();
		std::string answer;
		int status = s.send ? s.send(*request, answer) : 0;
		lock.lock();
		s.in_flight = 0;
		s.stats.last_status = status;

		if (status >= 200 && status < 300) {
			s.pop_front_if(id);
			++s.stats.sent;
			s.failures = 0;
		} else if (status == 401 || status == 403) {
			// Asking again with a wrong key gets this address locked out; the player has to look at mining.txt.
			log_warn("mining: the server does not take the key in mining.txt (%d); no more lines are sent", status);
			s.stats.refused = true;
			s.stats.dropped += int(s.items.size());
			s.items.clear();
		} else if (status >= 400 && status < 500 && status != 408 && status != 429) {
			log_warn("mining: the server refused a line (%d): %.200s", status, answer.c_str());
			s.pop_front_if(id);
			++s.stats.dropped;
			s.failures = 0;
		} else {
			// No answer, or the server is in trouble: wait, each time longer, and try the same line again.
			++s.failures;
			if (s.failures == 1)
				log_warn("mining: no line went through (%d%s%.200s); trying again", status, answer.empty() ? "" : ", ",
				         answer.c_str());
			if (status > 0 && status != 429 && !s.items.empty() && s.items.front().id == id &&
			    ++s.items.front().attempts >= s.options.attempts) {
				s.items.pop_front();
				++s.stats.dropped;
			}
			long long wait = s.options.retry_ms;
			for (int i = 1; i < s.failures && wait < s.options.retry_max_ms; ++i) wait *= 2;
			if (status == 429) wait = std::max<long long>(wait, 10LL * s.options.retry_ms);
			wait = std::min<long long>(wait, s.options.retry_max_ms);
			s.retry_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait);
		}
		s.idle.notify_all();
	}
	s.done = true;
	s.idle.notify_all();
}

}  // namespace ore
