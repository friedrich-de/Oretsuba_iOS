// Tries dictionaries in Yomitan's format against the mining kit's importer, outside the app, to
// find why a dictionary a player has does not import or does not show: it imports each zip given
// into a library folder and says what came of it, then looks up the words given after "--" and
// asks the library for every picture their entries name.
//
// It needs the dictionary sources alone, SQLite and zlib. In engine/, as one line:
//
//   g++ -std=c++17 -O2 -Isrc -Ithird_party tests/dict_probe.cpp src/dict/*.cpp src/util/*.cpp
//       -lsqlite3 -lz -lpthread -o ../build/dict_probe
//   ../build/dict_probe FOLDER resources/mining/transforms.json one.zip two.zip -- 昨日 丈夫
//
// FOLDER is made if it is not there; a dictionary already in it stays, so the words can be asked
// again without importing.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include "dict/deinflector.h"
#include "dict/library.h"
#include "util/json.h"

using namespace ore;

namespace {

// The pictures a meaning names: every "path" of an object of its JSON.
void pictures_of(const json::Value &value, std::set<std::string> &paths) {
	if (value.is_object()) {
		for (const auto &member : value.members()) {
			if (member.first == "path" && member.second.is_string())
				paths.insert(member.second.as_string());
			else
				pictures_of(member.second, paths);
		}
	} else if (value.is_array()) {
		for (size_t i = 0; i < value.size(); ++i) pictures_of(value[i], paths);
	}
}

}  // namespace

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: dict_probe FOLDER transforms.json [ZIP ...] [-- WORD ...]\n");
		return 2;
	}
	std::string folder = argv[1];
	std::filesystem::create_directories(folder);
	std::ifstream rules_file(argv[2]);
	std::string rules_text((std::istreambuf_iterator<char>(rules_file)), std::istreambuf_iterator<char>());
	std::string problem;
	std::shared_ptr<const dict::Deinflector> rules = dict::Deinflector::load(rules_text, problem);
	if (!rules) {
		fprintf(stderr, "%s: %s\n", argv[2], problem.c_str());
		return 2;
	}
	dict::DictionaryLibrary library(folder, rules);

	int failures = 0;
	int at = 3;
	for (; at < argc && std::string(argv[at]) != "--"; ++at) {
		std::string zip = argv[at];
		library.start_import(zip, [zip](const std::function<void(double)> &, const std::atomic<bool> &) { return zip; }, false);
		library.wait_import();
		dict::ImportState state = library.import_state();
		if (state.state == "done") {
			printf("imported  %s\n", zip.c_str());
		} else {
			printf("FAILED    %s\n          %s\n", zip.c_str(), state.error.c_str());
			++failures;
		}
	}
	for (const auto &item : library.installed()) {
		dict::DictionaryInfo info = item.reader->info();
		printf("there     %s: %lld words, %lld frequencies, %lld pitch accents, %lld pictures, %lld rows left out\n", info.title.c_str(),
		       (long long)info.terms, (long long)info.frequencies, (long long)info.pitches, (long long)info.media, (long long)info.skipped);
	}

	for (++at; at < argc; ++at) {
		json::Value found = library.lookup(argv[at]);
		const json::Value &results = found["results"];
		printf("lookup    %s: %zu results\n", argv[at], results.size());
		for (size_t i = 0; i < results.size(); ++i) {
			const json::Value &term = results[i]["term"];
			printf("  %s 【%s】", term.str("expression").c_str(), term.str("reading").c_str());
			const json::Value &pitches = term["pitches"];
			for (size_t p = 0; p < pitches.size(); ++p)
				printf("  pitch from %s: %s", pitches[p].str("dictionary").c_str(), json::write(pitches[p]["pitches"]).c_str());
			printf("\n");
			const json::Value &glossaries = term["glossaries"];
			for (size_t g = 0; g < glossaries.size(); ++g) {
				std::string dictionary = glossaries[g].str("dictionary");
				json::Value meaning;
				std::set<std::string> paths;
				if (json::parse(glossaries[g].str("glossary"), meaning)) pictures_of(meaning, paths);
				printf("    %s: %zu pictures\n", dictionary.c_str(), paths.size());
				for (const std::string &path : paths) {
					std::string data, type;
					if (library.media(dictionary, path, data, type)) {
						printf("      there    %s (%s, %zu bytes)\n", path.c_str(), type.c_str(), data.size());
					} else {
						printf("      MISSING  %s\n", path.c_str());
						++failures;
					}
				}
			}
		}
	}
	return failures == 0 ? 0 : 1;
}
