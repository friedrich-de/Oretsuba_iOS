// One dictionary in one SQLite file.
#include "dict/dictionary_db.h"
#if ORE_MINING_KIT

#include <sqlite3.h>
#include <sys/stat.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ore::dict {

namespace {

const char kPlain = 0;
// 1 is Brotli in the files of the VN Mining Server, which this reads none of.
const char kDeflated = 2;

// A block shorter than this is kept as it is: compressing it saves nothing.
const size_t kCompressFrom = 96;

// How many unpacked blocks a reader keeps: a lookup's words are mostly in a few.
const size_t kBlocksKept = 12;

// At most this many texts are asked for in one query.
const size_t kKeysPerQuery = 500;

std::string text_of(sqlite3_stmt *s, int column) {
	const unsigned char *t = sqlite3_column_text(s, column);
	return t ? std::string(reinterpret_cast<const char *>(t), size_t(sqlite3_column_bytes(s, column))) : std::string();
}

void bind_text(sqlite3_stmt *s, int index, std::string_view text) {
	sqlite3_bind_text(s, index, text.data(), int(text.size()), SQLITE_TRANSIENT);
}

std::string numbered(size_t count) {
	std::string names;
	for (size_t i = 0; i < count; ++i) {
		if (i) names += ',';
		names += '?';
		names += std::to_string(i + 1);
	}
	return names;
}

struct Statement {
	sqlite3_stmt *s = nullptr;
	Statement(sqlite3 *db, const std::string &sql) { sqlite3_prepare_v2(db, sql.c_str(), int(sql.size()), &s, nullptr); }
	~Statement() { sqlite3_finalize(s); }
	explicit operator bool() const { return s != nullptr; }
};

}  // namespace

// ---------------------------------------------------------------- info

json::Value DictionaryInfo::to_json() const {
	json::Value v = json::Value::object();
	v.set("id", id);
	v.set("title", title);
	v.set("revision", revision);
	v.set("format", format);
	v.set("author", author);
	v.set("url", url);
	v.set("description", description);
	v.set("attribution", attribution);
	v.set("frequencyMode", frequency_mode);
	v.set("sourceLanguage", source_language);
	v.set("targetLanguage", target_language);
	v.set("indexUrl", index_url);
	v.set("downloadUrl", download_url);
	v.set("terms", terms);
	v.set("frequencies", frequencies);
	v.set("pitches", pitches);
	v.set("media", media);
	v.set("skipped", skipped);
	v.set("imported", imported);
	return v;
}

DictionaryInfo DictionaryInfo::from_json(const json::Value &v) {
	DictionaryInfo info;
	info.id = v.str("id");
	info.title = v.str("title");
	info.revision = v.str("revision");
	info.format = int(v.number("format"));
	info.author = v.str("author");
	info.url = v.str("url");
	info.description = v.str("description");
	info.attribution = v.str("attribution");
	info.frequency_mode = v.str("frequencyMode");
	info.source_language = v.str("sourceLanguage");
	info.target_language = v.str("targetLanguage");
	info.index_url = v.str("indexUrl");
	info.download_url = v.str("downloadUrl");
	info.terms = int64_t(v.number("terms"));
	info.frequencies = int64_t(v.number("frequencies"));
	info.pitches = int64_t(v.number("pitches"));
	info.media = int64_t(v.number("media"));
	info.skipped = int64_t(v.number("skipped"));
	info.imported = v.str("imported");
	return info;
}

// ---------------------------------------------------------------- definitions

std::string pack_block(std::string_view utf8) {
	if (utf8.size() >= kCompressFrom && utf8.size() < 0x7fffffff) {
		uLongf length = compressBound(uLong(utf8.size()));
		std::string packed(5 + size_t(length), '\0');
		// Blocks are packed on several threads at once, so the usual level costs an import little
		// and makes the file a fifth smaller than the fastest.
		if (compress2(reinterpret_cast<Bytef *>(&packed[5]), &length, reinterpret_cast<const Bytef *>(utf8.data()),
		              uLong(utf8.size()), 6) == Z_OK &&
		    size_t(length) + 5 < utf8.size() + 1) {
			packed[0] = kDeflated;
			uint32_t size = uint32_t(utf8.size());
			for (int i = 0; i < 4; ++i) packed[size_t(1 + i)] = char(size >> (8 * i));
			packed.resize(5 + size_t(length));
			return packed;
		}
	}
	std::string plain;
	plain.reserve(utf8.size() + 1);
	plain += kPlain;
	plain.append(utf8);
	return plain;
}

bool unpack_block(const void *data, size_t size, std::string &out) {
	const char *p = static_cast<const char *>(data);
	out.clear();
	if (size == 0) return true;
	if (p[0] == kPlain) {
		out.assign(p + 1, size - 1);
		return true;
	}
	if (p[0] != kDeflated || size < 5) return false;
	uint32_t length = 0;
	for (int i = 0; i < 4; ++i) length |= uint32_t(uint8_t(p[1 + i])) << (8 * i);
	// A block is never larger than the bank its definitions came in.
	if (length > 256u * 1024 * 1024) return false;
	out.assign(length, '\0');
	uLongf written = length;
	if (uncompress(reinterpret_cast<Bytef *>(out.data()), &written, reinterpret_cast<const Bytef *>(p + 5), uLong(size - 5)) != Z_OK ||
	    written != length) {
		out.clear();
		return false;
	}
	return true;
}

void TermBatch::add(Row row, std::string_view glossary) {
	if (has_last_ && glossary.size() == last_length_ && open_.compare(last_start_, last_length_, glossary) == 0) {
		// Dictionaries repeat a word's definitions for each way it is written.
		row.start = last_start_;
	} else {
		if (open_.size() >= kBlockBytes) pack();
		row.start = uint32_t(open_.size());
		open_.append(glossary);
	}
	row.block = uint32_t(blocks.size());
	row.length = uint32_t(glossary.size());
	has_last_ = true;
	last_start_ = row.start;
	last_length_ = row.length;
	rows.push_back(std::move(row));
}

void TermBatch::pack() {
	blocks.push_back(pack_block(open_));
	open_.clear();
	has_last_ = false;
}

void TermBatch::finish() {
	if (!open_.empty()) pack();
	open_.shrink_to_fit();
}

// ---------------------------------------------------------------- writing

DictionaryWriter::DictionaryWriter(const std::string &path) {
	struct stat st;
	if (stat(path.c_str(), &st) == 0) throw DictError("A dictionary file is already there.");
	if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
		sqlite3_close(db_);
		db_ = nullptr;
		throw DictError("The dictionary's file could not be made.");
	}
	// The file is new and is thrown away if the import fails, so it needs no journal.
	run("PRAGMA journal_mode = OFF; PRAGMA synchronous = OFF; PRAGMA page_size = 8192;");
	run("CREATE TABLE info (key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID;"
	    "CREATE TABLE terms ("
	    " expression TEXT NOT NULL, reading TEXT NOT NULL, definition_tags TEXT NOT NULL, rules TEXT NOT NULL,"
	    " score INTEGER NOT NULL, sequence INTEGER NOT NULL, term_tags TEXT NOT NULL,"
	    " block INTEGER NOT NULL, start INTEGER NOT NULL, length INTEGER NOT NULL);"
	    "CREATE TABLE blocks (id INTEGER PRIMARY KEY, data BLOB NOT NULL);"
	    "CREATE TABLE metas (expression TEXT NOT NULL, mode TEXT NOT NULL, data TEXT NOT NULL);"
	    "CREATE TABLE tags (name TEXT PRIMARY KEY, category TEXT NOT NULL, sort INTEGER NOT NULL, notes TEXT NOT NULL,"
	    " score INTEGER NOT NULL) WITHOUT ROWID;"
	    // Pictures come in any order: they are found by an index made when all are in.
	    "CREATE TABLE media (path TEXT NOT NULL, data BLOB NOT NULL);");
	run("BEGIN");
	auto prepare = [this](const char *sql) {
		sqlite3_stmt *s = nullptr;
		if (sqlite3_prepare_v2(db_, sql, -1, &s, nullptr) != SQLITE_OK) throw DictError("The dictionary's file could not be written.");
		return s;
	};
	term_ = prepare("INSERT INTO terms VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)");
	block_ = prepare("INSERT INTO blocks VALUES (?1, ?2)");
	meta_ = prepare("INSERT INTO metas VALUES (?1, ?2, ?3)");
	tag_ = prepare("INSERT OR REPLACE INTO tags VALUES (?1, ?2, ?3, ?4, ?5)");
	media_ = prepare("INSERT INTO media VALUES (?1, ?2)");
}

DictionaryWriter::~DictionaryWriter() {
	sqlite3_finalize(term_);
	sqlite3_finalize(block_);
	sqlite3_finalize(meta_);
	sqlite3_finalize(tag_);
	sqlite3_finalize(media_);
	if (db_) sqlite3_close(db_);
}

void DictionaryWriter::run(const char *sql) {
	if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
		throw DictError(sqlite3_errcode(db_) == SQLITE_FULL ? "There is no room left for the dictionary."
		                                                   : "The dictionary's file could not be written.");
}

void DictionaryWriter::step(sqlite3_stmt *statement) {
	int result = sqlite3_step(statement);
	sqlite3_reset(statement);
	sqlite3_clear_bindings(statement);
	if (result != SQLITE_DONE)
		throw DictError(result == SQLITE_FULL ? "There is no room left for the dictionary."
		                                      : "The dictionary's file could not be written.");
}

void DictionaryWriter::add_terms(const TermBatch &batch) {
	int64_t first = blocks_ + 1;
	for (const std::string &block : batch.blocks) {
		sqlite3_bind_int64(block_, 1, ++blocks_);
		sqlite3_bind_blob64(block_, 2, block.data(), sqlite3_uint64(block.size()), SQLITE_STATIC);
		step(block_);
	}
	for (const TermBatch::Row &row : batch.rows) {
		// The texts stay where they are until the row is written.
		auto text = [this](int index, const std::string &value) {
			sqlite3_bind_text(term_, index, value.data(), int(value.size()), SQLITE_STATIC);
		};
		text(1, row.expression);
		text(2, row.reading);
		text(3, row.definition_tags);
		text(4, row.rules);
		sqlite3_bind_int64(term_, 5, row.score);
		sqlite3_bind_int64(term_, 6, row.sequence);
		text(7, row.term_tags);
		sqlite3_bind_int64(term_, 8, first + row.block);
		sqlite3_bind_int64(term_, 9, row.start);
		sqlite3_bind_int64(term_, 10, row.length);
		step(term_);
	}
}

void DictionaryWriter::add_meta(const std::string &expression, const std::string &mode, std::string_view data) {
	bind_text(meta_, 1, expression);
	bind_text(meta_, 2, mode);
	bind_text(meta_, 3, data);
	step(meta_);
}

void DictionaryWriter::add_tag(const TagRow &tag) {
	bind_text(tag_, 1, tag.name);
	bind_text(tag_, 2, tag.category);
	sqlite3_bind_int64(tag_, 3, tag.order);
	bind_text(tag_, 4, tag.notes);
	sqlite3_bind_int64(tag_, 5, tag.score);
	step(tag_);
}

void DictionaryWriter::add_media(const std::string &path, const std::string &data) {
	bind_text(media_, 1, path);
	sqlite3_bind_blob64(media_, 2, data.data(), sqlite3_uint64(data.size()), SQLITE_TRANSIENT);
	step(media_);
}

void DictionaryWriter::finish(const DictionaryInfo &info, const std::string &styles) {
	{
		Statement s(db_, "INSERT INTO info VALUES ('schema', ?1), ('info', ?2), ('styles', ?3)");
		if (!s) throw DictError("The dictionary's file could not be written.");
		bind_text(s.s, 1, std::to_string(kSchemaVersion));
		bind_text(s.s, 2, json::write(info.to_json()));
		bind_text(s.s, 3, styles);
		if (sqlite3_step(s.s) != SQLITE_DONE) throw DictError("The dictionary's file could not be written.");
	}
	run("COMMIT");
	run("CREATE INDEX terms_expression ON terms (expression);"
	    "CREATE INDEX terms_reading ON terms (reading);"
	    "CREATE INDEX metas_expression ON metas (expression);"
	    "CREATE INDEX media_path ON media (path);");
	sqlite3_finalize(term_);
	sqlite3_finalize(block_);
	sqlite3_finalize(meta_);
	sqlite3_finalize(tag_);
	sqlite3_finalize(media_);
	term_ = block_ = meta_ = tag_ = media_ = nullptr;
	int closed = sqlite3_close(db_);
	db_ = nullptr;
	if (closed != SQLITE_OK) throw DictError("The dictionary's file could not be written.");
}

// ---------------------------------------------------------------- reading

std::shared_ptr<DictionaryReader> DictionaryReader::open(const std::string &path) {
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
		sqlite3_close(db);
		return nullptr;
	}
	std::unordered_map<std::string, std::string> values;
	{
		Statement s(db, "SELECT key, value FROM info");
		if (!s) {
			sqlite3_close(db);
			return nullptr;
		}
		while (sqlite3_step(s.s) == SQLITE_ROW) values[text_of(s.s, 0)] = text_of(s.s, 1);
	}
	json::Value info;
	if (values["schema"] != std::to_string(kSchemaVersion) || !values.count("info") || !json::parse(values["info"], info) ||
	    !info.is_object()) {
		sqlite3_close(db);
		return nullptr;
	}
	std::shared_ptr<DictionaryReader> reader(new DictionaryReader());
	reader->path_ = path;
	reader->info_ = DictionaryInfo::from_json(info);
	reader->styles_ = values["styles"];
	reader->db_ = db;
	return reader;
}

DictionaryReader::~DictionaryReader() {
	if (db_) sqlite3_close(db_);
	if (retired_) remove(path_.c_str());
}

int64_t DictionaryReader::file_size() const {
	struct stat st;
	return stat(path_.c_str(), &st) == 0 ? int64_t(st.st_size) : 0;
}

const std::unordered_map<std::string, TagRow> &DictionaryReader::tags() {
	std::lock_guard<std::mutex> lock(gate_);
	if (!tags_read_) {
		tags_read_ = true;
		Statement s(db_, "SELECT name, category, sort, notes, score FROM tags");
		while (s && sqlite3_step(s.s) == SQLITE_ROW) {
			TagRow tag;
			tag.name = text_of(s.s, 0);
			tag.category = text_of(s.s, 1);
			tag.order = sqlite3_column_int64(s.s, 2);
			tag.notes = text_of(s.s, 3);
			tag.score = sqlite3_column_int64(s.s, 4);
			tags_[tag.name] = std::move(tag);
		}
	}
	return tags_;
}

std::vector<TermRow> DictionaryReader::find_terms(const std::vector<std::string> &keys) {
	std::lock_guard<std::mutex> lock(gate_);
	std::vector<TermRow> found;
	std::unordered_map<int64_t, size_t> seen;
	const char *columns = "rowid, expression, reading, definition_tags, rules, score, sequence, term_tags";
	for (size_t from = 0; from < keys.size(); from += kKeysPerQuery) {
		size_t count = std::min(kKeysPerQuery, keys.size() - from);
		std::string names = numbered(count);
		Statement s(db_, std::string("SELECT ") + columns + " FROM terms WHERE expression IN (" + names + ") UNION SELECT " + columns +
		                     " FROM terms WHERE reading IN (" + names + ")");
		if (!s) continue;
		for (size_t i = 0; i < count; ++i) bind_text(s.s, int(i + 1), keys[from + i]);
		while (sqlite3_step(s.s) == SQLITE_ROW) {
			TermRow row;
			row.id = sqlite3_column_int64(s.s, 0);
			row.expression = text_of(s.s, 1);
			row.reading = text_of(s.s, 2);
			row.definition_tags = text_of(s.s, 3);
			row.rules = text_of(s.s, 4);
			row.score = sqlite3_column_int64(s.s, 5);
			row.sequence = sqlite3_column_int64(s.s, 6);
			row.term_tags = text_of(s.s, 7);
			auto known = seen.find(row.id);
			if (known == seen.end()) {
				seen.emplace(row.id, found.size());
				found.push_back(std::move(row));
			} else {
				found[known->second] = std::move(row);
			}
		}
	}
	return found;
}

const std::string *DictionaryReader::block(int64_t id) {
	for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
		if (it->first != id) continue;
		blocks_.splice(blocks_.begin(), blocks_, it);
		return &blocks_.front().second;
	}
	Statement s(db_, "SELECT data FROM blocks WHERE id = ?1");
	if (!s) return nullptr;
	sqlite3_bind_int64(s.s, 1, id);
	std::string unpacked;
	if (sqlite3_step(s.s) != SQLITE_ROW ||
	    !unpack_block(sqlite3_column_blob(s.s, 0), size_t(sqlite3_column_bytes(s.s, 0)), unpacked))
		return nullptr;
	blocks_.emplace_front(id, std::move(unpacked));
	if (blocks_.size() > kBlocksKept) blocks_.pop_back();
	return &blocks_.front().second;
}

std::unordered_map<int64_t, std::string> DictionaryReader::glossaries(const std::vector<int64_t> &ids) {
	std::lock_guard<std::mutex> lock(gate_);
	struct Place {
		int64_t id, block, start, length;
	};
	std::vector<Place> places;
	for (size_t from = 0; from < ids.size(); from += kKeysPerQuery) {
		size_t count = std::min(kKeysPerQuery, ids.size() - from);
		std::string list;
		for (size_t i = 0; i < count; ++i) {
			if (i) list += ',';
			list += std::to_string(ids[from + i]);
		}
		Statement s(db_, "SELECT rowid, block, start, length FROM terms WHERE rowid IN (" + list + ")");
		while (s && sqlite3_step(s.s) == SQLITE_ROW)
			places.push_back(Place{sqlite3_column_int64(s.s, 0), sqlite3_column_int64(s.s, 1), sqlite3_column_int64(s.s, 2),
			                       sqlite3_column_int64(s.s, 3)});
	}
	// By block, so that each is unpacked once however many are kept.
	std::sort(places.begin(), places.end(), [](const Place &a, const Place &b) { return a.block < b.block; });
	std::unordered_map<int64_t, std::string> found;
	for (const Place &place : places) {
		const std::string *text = block(place.block);
		bool there = text && place.start >= 0 && place.length > 0 && uint64_t(place.start) + uint64_t(place.length) <= text->size();
		found[place.id] = there ? text->substr(size_t(place.start), size_t(place.length)) : std::string("[]");
	}
	return found;
}

std::vector<MetaRow> DictionaryReader::find_metas(const std::vector<std::string> &expressions) {
	std::lock_guard<std::mutex> lock(gate_);
	std::vector<MetaRow> found;
	for (size_t from = 0; from < expressions.size(); from += kKeysPerQuery) {
		size_t count = std::min(kKeysPerQuery, expressions.size() - from);
		Statement s(db_, "SELECT expression, mode, data FROM metas WHERE expression IN (" + numbered(count) + ")");
		if (!s) continue;
		for (size_t i = 0; i < count; ++i) bind_text(s.s, int(i + 1), expressions[from + i]);
		while (sqlite3_step(s.s) == SQLITE_ROW) found.push_back(MetaRow{text_of(s.s, 0), text_of(s.s, 1), text_of(s.s, 2)});
	}
	return found;
}

bool DictionaryReader::media(const std::string &path, std::string &out) {
	std::lock_guard<std::mutex> lock(gate_);
	Statement s(db_, "SELECT data FROM media WHERE path = ?1 LIMIT 1");
	if (!s) return false;
	bind_text(s.s, 1, path);
	if (sqlite3_step(s.s) != SQLITE_ROW) return false;
	const void *data = sqlite3_column_blob(s.s, 0);
	out.assign(static_cast<const char *>(data ? data : ""), size_t(sqlite3_column_bytes(s.s, 0)));
	return true;
}

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
