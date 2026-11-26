// One dictionary in one SQLite file of its own, so that removing it is
// removing the file and an import that fails leaves nothing behind. Words are
// found by how they are written and how they are read. Their definitions are
// kept in blocks, many definitions compressed together: that is several times
// smaller and far quicker to write than each on its own, and a lookup unpacks
// only the few blocks it reads from.
#pragma once

#include "dict/kit_config.h"
#if ORE_MINING_KIT

#include <cstdint>
#include <list>
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
inline constexpr int kSchemaVersion = 2;

// Definitions are gathered until a block is about this long.
inline constexpr size_t kBlockBytes = 32 * 1024;

// Keeps a block of definitions: as it is when short, else deflated after its length.
std::string pack_block(std::string_view utf8);
// Reads back what pack_block kept. False when it is damaged.
bool unpack_block(const void *data, size_t size, std::string &out);

// The words of one bank, ready to be written: their definitions are in blocks
// already packed, so that several banks can be made ready at once.
struct TermBatch {
	struct Row {
		std::string expression, reading, definition_tags, rules, term_tags;
		int64_t score = 0, sequence = 0;
		uint32_t block = 0, start = 0, length = 0;  // where its definitions are, among the blocks of this batch
	};
	std::vector<Row> rows;
	std::vector<std::string> blocks;

	// Adds a word. A definition that is the one of the word before it is kept once.
	void add(Row row, std::string_view glossary);
	// Packs what is left. Nothing is added after it.
	void finish();

private:
	void pack();
	std::string open_;  // the block being gathered
	bool has_last_ = false;
	uint32_t last_start_ = 0, last_length_ = 0;
};

// Writes a new dictionary file. Nothing is in it for anyone else until finish() has run.
// Every function throws DictError when the file cannot be written.
class DictionaryWriter {
public:
	// Starts a dictionary file at `path`, which must not exist.
	explicit DictionaryWriter(const std::string &path);
	~DictionaryWriter();
	DictionaryWriter(const DictionaryWriter &) = delete;
	DictionaryWriter &operator=(const DictionaryWriter &) = delete;

	// Writes the words of a finished batch.
	void add_terms(const TermBatch &batch);
	void add_meta(const std::string &expression, const std::string &mode, std::string_view data);
	void add_tag(const TagRow &tag);  // a later one of the same name wins
	void add_media(const std::string &path, const std::string &data);
	// Indexes what was added, writes what the dictionary says of itself and closes the file.
	void finish(const DictionaryInfo &info, const std::string &styles);

private:
	void run(const char *sql);
	void step(sqlite3_stmt *statement);
	sqlite3 *db_ = nullptr;
	sqlite3_stmt *term_ = nullptr, *block_ = nullptr, *meta_ = nullptr, *tag_ = nullptr, *media_ = nullptr;
	int64_t blocks_ = 0;
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
	// The blocks read last, unpacked, the newest first.
	std::list<std::pair<int64_t, std::string>> blocks_;
	const std::string *block(int64_t id);
	bool tags_read_ = false;
	std::unordered_map<std::string, TagRow> tags_;
	bool retired_ = false;
};

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
