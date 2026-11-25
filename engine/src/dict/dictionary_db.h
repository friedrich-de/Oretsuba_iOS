// One dictionary in one SQLite file of its own, so that removing it is
// removing the file and an import that fails leaves nothing behind. Words are
// found by how they are written and how they are read; their definitions are
// kept compressed.
#pragma once

#include "dict/kit_config.h"
#if ORE_MINING_KIT

#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "util/json.h"

struct sqlite3;
struct sqlite3_stmt;

namespace ore::dict {

// Why a dictionary could not be imported or read, in words for the player.
struct DictError : std::runtime_error {
	using std::runtime_error::runtime_error;
};

// What a dictionary says of itself (its index.json), and what the import found in it.
struct DictionaryInfo {
	std::string id;        // the name of its file in the folder
	std::string title;     // as results name it
	std::string revision;
	int format = 0;        // the version of Yomitan's format it was written in
	std::string author, url, description, attribution;
	std::string frequency_mode;  // "rank-based" or "occurrence-based", or empty
	std::string source_language, target_language;
	std::string index_url;       // where a newer revision is announced, for one that can be updated
	std::string download_url;
	int64_t terms = 0, frequencies = 0, pitches = 0, media = 0;
	int64_t skipped = 0;         // rows the import could not read and left out
	std::string imported;        // when, as 2026-01-31T12:00:00Z

	json::Value to_json() const;
	static DictionaryInfo from_json(const json::Value &value);
};

// A word as a dictionary has it.
struct TermRow {
	int64_t id = 0;
	std::string expression, reading;  // the reading is the written form when the dictionary gives none
	std::string definition_tags;      // space-separated
	std::string rules;                // its parts of speech for deinflection ("v1", "v5", "adj-i")
	int64_t score = 0, sequence = 0;
	std::string term_tags;
};

// A frequency, pitch accent or pronunciation of a word.
struct MetaRow {
	std::string expression;
	std::string mode;  // "freq", "pitch" or "ipa"
	std::string data;  // what it says, as JSON
};

// A tag as the dictionary's tag bank explains it.
struct TagRow {
	std::string name, category;
	int64_t order = 0;
	std::string notes;
	int64_t score = 0;
};

// The schema; a file with another one is read as no dictionary.
inline constexpr int kSchemaVersion = 1;

// Keeps a definition: as it is when short, else deflated after its length.
std::string pack_glossary(std::string_view utf8);
// Reads back what pack_glossary kept. False when it is damaged.
bool unpack_glossary(const void *data, size_t size, std::string &out);

// Writes a new dictionary file. Nothing is in it for anyone else until finish() has run.
// Every function throws DictError when the file cannot be written.
class DictionaryWriter {
public:
	// Starts a dictionary file at `path`, which must not exist.
	explicit DictionaryWriter(const std::string &path);
	~DictionaryWriter();
	DictionaryWriter(const DictionaryWriter &) = delete;
	DictionaryWriter &operator=(const DictionaryWriter &) = delete;

	void add_term(const std::string &expression, const std::string &reading, const std::string &definition_tags,
	              const std::string &rules, int64_t score, std::string_view glossary, int64_t sequence,
	              const std::string &term_tags);
	void add_meta(const std::string &expression, const std::string &mode, std::string_view data);
	void add_tag(const TagRow &tag);  // a later one of the same name wins
	void add_media(const std::string &path, const std::string &data);
	// Indexes what was added, writes what the dictionary says of itself and closes the file.
	void finish(const DictionaryInfo &info, const std::string &styles);

private:
	void run(const char *sql);
	void step(sqlite3_stmt *statement);
	sqlite3 *db_ = nullptr;
	sqlite3_stmt *term_ = nullptr, *meta_ = nullptr, *tag_ = nullptr, *media_ = nullptr;
};

// Reads one dictionary file. Its calls may come from several threads.
class DictionaryReader {
public:
	// Opens a dictionary file, or gives null when it is not one this version can read.
	static std::shared_ptr<DictionaryReader> open(const std::string &path);
	~DictionaryReader();
	DictionaryReader(const DictionaryReader &) = delete;
	DictionaryReader &operator=(const DictionaryReader &) = delete;

	const std::string &path() const { return path_; }
	const DictionaryInfo &info() const { return info_; }
	const std::string &styles() const { return styles_; }
	int64_t file_size() const;
	// Its tags by name.
	const std::unordered_map<std::string, TagRow> &tags();

	// The words written or read as any of `keys`, without their definitions, each once.
	std::vector<TermRow> find_terms(const std::vector<std::string> &keys);
	// The definitions of some words, by their rows, as JSON.
	std::unordered_map<int64_t, std::string> glossaries(const std::vector<int64_t> &ids);
	std::vector<MetaRow> find_metas(const std::vector<std::string> &expressions);
	// A picture of the dictionary. False when it has none at that path.
	bool media(const std::string &path, std::string &out);

	// The dictionary is no longer listed: its file goes when the last holder of this reader does.
	void retire() { retired_ = true; }

private:
	DictionaryReader() = default;
	std::string path_;
	DictionaryInfo info_;
	std::string styles_;
	sqlite3 *db_ = nullptr;
	std::mutex gate_;
	bool tags_read_ = false;
	std::unordered_map<std::string, TagRow> tags_;
	bool retired_ = false;
};

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
