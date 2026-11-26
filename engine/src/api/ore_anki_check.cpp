// Device smoke test of the cards for AnkiMobile (api/ore_api.h); debug builds
// of the app only. It does what the settings and the popup do, without either:
// fetches the lists, hands a card over, hands the same word over again, and
// once more with duplicates allowed. tools/apple/ankimobile-standin plays
// AnkiMobile and reads this file's report.
#include "api/ore_api.h"

#if DEBUG

#include "dict/kit_config.h"

#if ORE_MINING_KIT

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mining/anki_app_target.h"
#include "mining/kit_host.h"
#include "util/log.h"

namespace {

using namespace ore;

struct Check {
	std::string report;
	std::mutex mutex;
	std::condition_variable changed;
	std::vector<std::pair<std::string, std::string>> events;
	size_t seen = 0;

	void line(const std::string &text) {
		log_info("AnkiMobile check: %s", text.c_str());
		if (FILE *file = fopen(report.c_str(), "a")) {
			fputs(text.c_str(), file);
			fputc('\n', file);
			fclose(file);
		}
	}

	// The next thing told, or false when none comes in time.
	bool next(int seconds, std::string &event, std::string &json) {
		std::unique_lock<std::mutex> lock(mutex);
		if (!changed.wait_for(lock, std::chrono::seconds(seconds), [this] { return events.size() > seen; })) return false;
		event = events[seen].first;
		json = events[seen].second;
		++seen;
		return true;
	}
};

// Bytes that tell a whole file from a broken one: the stand-in adds them up.
std::string pattern(size_t size, unsigned seed) {
	std::string bytes(size, '\0');
	unsigned value = seed;
	for (char &c : bytes) {
		value = value * 1103515245u + 12345u;
		c = char(value >> 16);
	}
	return bytes;
}

unsigned sum_of(const std::string &bytes) {
	unsigned sum = 0;
	for (unsigned char c : bytes) sum += c;
	return sum;
}

void run(std::string folder, std::string report) {
	auto check = std::make_shared<Check>();
	check->report = std::move(report);
	remove(check->report.c_str());
	check->line("start");
	AnkiMobileTarget target(host_anki_app(), kAppLinkScheme, folder + "/ankimobile-check.json",
	                        [check](const std::string &event, const std::string &json) {
		                        {
			                        std::lock_guard<std::mutex> lock(check->mutex);
			                        check->events.emplace_back(event, json);
		                        }
		                        check->changed.notify_all();
	                        });
	if (!target.possible()) return check->line("done: this device cannot reach AnkiMobile");

	AnkiProblem problem;
	std::string event, json;
	if (!target.fetch(problem)) return check->line("done: the lists could not be asked for: " + problem.message);
	if (!check->next(60, event, json)) return check->line("done: no answer about the lists");
	check->line(event + " " + json);
	CardPlaces places;
	if (!target.places(places, problem)) return check->line("done: no lists: " + problem.message);
	std::string decks, types;
	for (const std::string &deck : places.decks) decks += (decks.empty() ? "" : ", ") + deck;
	for (const std::string &type : places.note_types) types += (types.empty() ? "" : ", ") + type;
	check->line("decks " + decks);
	check->line("note types " + types);

	// The last note type and deck: a stand-in names the ones it wants used last.
	CardNote note;
	note.deck = places.decks.back();
	note.note_type = places.note_types.back();
	std::vector<std::string> names;
	if (!target.field_names(note.note_type, names, problem) || names.size() < 4) return check->line("done: the note type needs four fields");
	for (const std::string &name : names) note.fields.emplace_back(name, std::string());
	note.fields[0].second = "\xE9\xA3\x9F\xE3\x81\xB9\xE3\x82\x8B";  // 食べる
	note.fields[1].second = "\xE3\x81\x94\xE9\xA3\xAF\xE3\x82\x92<b>\xE9\xA3\x9F\xE3\x81\xB9\xE3\x82\x8B</b>\xE3\x80\x82 & \"more\" + 100%";
	note.tags = {"oretsuba-check", "second-tag"};
	std::string picture = pattern(150000, 1), sound = pattern(40000, 2);
	note.media.push_back({"picture", "oretsuba_check_picture.jpg", picture, "image/jpeg", {names[2]}});
	note.media.push_back({"sound", "oretsuba_check_voice.m4a", sound, "audio/mp4", {names[3]}});
	check->line("files picture " + std::to_string(picture.size()) + " " + std::to_string(sum_of(picture)) + " sound " +
	            std::to_string(sound.size()) + " " + std::to_string(sum_of(sound)));

	const char *steps[] = {"first card", "same word again", "same word, duplicates allowed"};
	for (int i = 0; i < 3; ++i) {
		note.allow_duplicate = i == 2;
		AddOutcome outcome;
		if (!target.add(note, outcome, problem)) return check->line(std::string("done: ") + steps[i] + " could not be handed over: " + problem.message);
		check->line(std::string(steps[i]) + ": handed over");
		// The second card AnkiMobile keeps to itself: the answer comes once the app is brought back to the front.
		if (!check->next(120, event, json)) return check->line(std::string("done: nothing was told about: ") + steps[i]);
		check->line(std::string(steps[i]) + ": " + event + " " + json);
	}
	check->line("done: three cards");
}

}  // namespace

extern "C" void ore_debug_ankimobile_check(const char *folder, const char *report) {
	std::thread(run, std::string(folder ? folder : ""), std::string(report ? report : "")).detach();
}

#else

extern "C" void ore_debug_ankimobile_check(const char *, const char *) {}

#endif  // ORE_MINING_KIT

#endif  // DEBUG
