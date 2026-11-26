// HTTP for the mining kit (mining/mining_kit.h): what it asks of AnkiConnect,
// a word's sound and a dictionary to download need the whole answer, its type
// and where it came from, which the sender of lines (mining.h) does not give.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ore {

struct FetchRequest {
	std::string method = "GET";
	std::string url;
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
	int timeout_ms = 15000;            // without an answer; a download may take longer in all
	uint64_t max_bytes = 8u << 20;     // an answer longer than this is given up
	std::string to_file;               // the answer goes into this file instead of `body`
	std::function<void(double)> progress;       // of a download, from 0 to 1, when its length is known
	const std::atomic<bool> *stop = nullptr;    // set to give the request up
};

struct FetchAnswer {
	int status = 0;            // the HTTP status, or 0 when no answer came
	std::string content_type;  // without its parameters, in small letters: "audio/mpeg"
	std::string final_url;     // where the answer came from, after redirections
	std::string body;          // empty when the answer went to a file
	std::string error;         // why no answer came, in words for the player
	bool too_large = false;
};

// Sends one request and waits for the answer. Redirections are followed. It
// may be called from any thread, several at a time, and must not depend on
// anything that can go away while the app runs.
using HttpFetch = std::function<void(const FetchRequest &request, FetchAnswer &answer)>;

}  // namespace ore
