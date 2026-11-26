// The part of the mining kit that talks to an AnkiConnect: the add-on of a
// desktop Anki, or a server that speaks its interface. Nothing of AnkiConnect
// itself is here; the kit only asks it things over HTTP.
#pragma once

#include <string>

#include "mining/card_target.h"
#include "mining/http_fetch.h"
#include "util/json.h"

namespace ore {

// The version of AnkiConnect's interface the kit speaks.
inline constexpr int kAnkiVersion = 6;

class AnkiClient {
public:
	AnkiClient(std::string address, std::string key, HttpFetch fetch)
	    : address_(std::move(address)), key_(std::move(key)), fetch_(std::move(fetch)) {}

	const std::string &address() const { return address_; }
	bool usable() const { return !address_.empty() && fetch_; }

	// Asks AnkiConnect to do `action`. False with the reason in `problem`.
	bool invoke(const std::string &action, const json::Value &params, json::Value &result, AnkiProblem &problem,
	            int timeout_ms = 45000) const;
	bool invoke(const std::string &action, json::Value &result, AnkiProblem &problem) const {
		return invoke(action, json::Value(), result, problem);
	}

	// Puts a file into Anki's media under `name`. `stored` gets the name Anki kept it under.
	bool store_media(const std::string &name, const std::string &data, std::string &stored, AnkiProblem &problem) const;

private:
	std::string address_, key_;
	HttpFetch fetch_;
};

// Cards into the Anki behind an AnkiConnect.
class AnkiConnectTarget : public CardTarget {
public:
	explicit AnkiConnectTarget(AnkiClient client) : client_(std::move(client)) {}

	bool places(CardPlaces &places, AnkiProblem &problem) override;
	bool field_names(const std::string &note_type, std::vector<std::string> &names, AnkiProblem &problem) override;
	bool deck_of_newest(const std::string &note_type, const std::vector<std::string> &decks, std::string &deck,
	                    AnkiProblem &problem) override;
	bool known(const std::vector<CardNote> &notes, std::vector<Known> &answers, AnkiProblem &problem) override;
	bool add(const CardNote &note, AddOutcome &outcome, AnkiProblem &problem) override;

private:
	AnkiClient client_;
};

// An address as typed, made one AnkiConnect can be asked at: blanks cut, "http://"
// put before a bare host. False when it is no http or https address.
bool read_anki_address(const std::string &typed, std::string &address);

std::string base64(const std::string &data);
bool unbase64(const std::string &text, std::string &data);

// A short mark of a text, so the same text gets the same name (FNV-1a, 32
// bits, over UTF-16 as the mining page counts it).
std::string mark_of_text(const std::string &utf8);
// A short mark of a file's bytes.
std::string mark_of_bytes(const std::string &bytes);

}  // namespace ore
