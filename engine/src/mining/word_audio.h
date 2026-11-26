// How a word sounds, from the audio source the player set: an address with
// {term} and {reading} in it, as Yomitan's custom audio sources are, which
// answers with a list of sounds in JSON ("Custom URL (JSON)") or with the
// sound itself ("Custom URL").
#pragma once

#include <string>
#include <vector>

#include "mining/http_fetch.h"

namespace ore {

// A list of sounds larger than this is not read.
inline constexpr size_t kMaxSoundListBytes = 1024 * 1024;
// A sound larger than this is not fetched.
inline constexpr size_t kMaxSoundBytes = 8 * 1024 * 1024;
// How many sounds of a list are tried before the word is taken to have none.
inline constexpr int kMaxSoundTries = 3;
// A sound's name longer than this is cut.
inline constexpr size_t kMaxSoundName = 60;
// The name of the one sound of a source that answers with the sound itself.
inline constexpr char kDirectSoundName[] = "Sound";

enum class WordAudioOutcome {
	Found,
	NoSource,  // no audio source is set
	NotFound,  // the source has no sound for the word
	Failed,    // the source could not be reached, or answered with something else
};

struct WordSound {
	std::string data;
	std::string type;       // "audio/mpeg"
	std::string extension;  // "mp3"
};

// Reads an address as typed: http or https, without a name and password, with
// {term} in it. False with what is wrong with it in `problem`.
bool read_audio_address(const std::string &typed, std::string &address, std::string &problem);

// The address for one word; empty when it is no http or https address.
std::string fill_audio_address(const std::string &address, const std::string &term, const std::string &reading);

// The sound of the source's list named `source`, or without a name the first
// of the list that can be fetched. The reading is the word itself where it is empty.
WordAudioOutcome find_word_audio(const HttpFetch &fetch, const std::string &address, const std::string &term,
                                 const std::string &reading, const std::string &source, WordSound &sound);

// The sounds the source has for a word, by name, for the player to choose one.
WordAudioOutcome word_audio_choices(const HttpFetch &fetch, const std::string &address, const std::string &term,
                                    const std::string &reading, std::vector<std::string> &names);

}  // namespace ore
