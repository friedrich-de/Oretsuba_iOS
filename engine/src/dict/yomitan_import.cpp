// Reads a dictionary in Yomitan's format into a dictionary file.
#include "dict/yomitan_import.h"
#if ORE_MINING_KIT

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "dict/zip_reader.h"
#include "util/json.h"
#include "util/utf8.h"

namespace ore::dict {

namespace {

using Entry = ZipReader::Entry;

std::string lower(std::string s) {
	for (char &c : s)
		if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return s;
}

// The text without the blanks at both ends, the full-width one among them.
std::string trimmed(const std::string &s) {
	size_t a = 0, b = s.size();
	for (;;) {
		if (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) {
			++a;
		} else if (b - a >= 3 && s.compare(a, 3, "\xE3\x80\x80") == 0) {
			a += 3;
		} else {
			break;
		}
	}
	for (;;) {
		if (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) {
			--b;
		} else if (b - a >= 3 && s.compare(b - 3, 3, "\xE3\x80\x80") == 0) {
			b -= 3;
		} else {
			break;
		}
	}
	return s.substr(a, b - a);
}

// A number that fits a whole number of 64 bits.
bool number(const json::Value &value, double &out) {
	out = value.as_number();
	return value.is_number() && std::fabs(out) < 9e15;
}

bool key(const json::Value &value, std::string &out) {
	out = value.as_string();
	return value.is_string() && !out.empty() && utf8::length16(out) <= kMaxKeyLength;
}

std::string tags_of(const json::Value &value) {
	return value.is_string() ? trimmed(value.as_string()) : std::string();
}

int64_t whole(double value) {
	return int64_t(std::nearbyint(value));
}

// "term_bank_12.json" with the prefix "term_bank_": 12. -1 for any other name.
long bank_number(const std::string &name, const char *prefix) {
	size_t n = strlen(prefix);
	if (name.compare(0, n, prefix) != 0 || name.size() < n + 6 || name.compare(name.size() - 5, 5, ".json") != 0) return -1;
	std::string digits = name.substr(n, name.size() - n - 5);
	if (digits.empty() || digits.size() > 9) return -1;
	for (char c : digits)
		if (c < '0' || c > '9') return -1;
	return atol(digits.c_str());
}

// The banks of one kind, in the order of their numbers.
std::vector<const Entry *> banks(const std::vector<const Entry *> &files, const char *prefix) {
	std::vector<std::pair<long, const Entry *>> found;
	for (const Entry *file : files) {
		long n = bank_number(file->name, prefix);
		if (n >= 0) found.emplace_back(n, file);
	}
	std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	std::vector<const Entry *> out;
	for (const auto &item : found) out.push_back(item.second);
	return out;
}

std::string read(ZipReader &zip, const Entry &entry, uint64_t limit) {
	std::string out, problem;
	if (!zip.read(entry, limit, out, problem))
		throw DictError(entry.name + " in that dictionary is " + (problem == "large" ? "too large." : "damaged."));
	return out;
}

// The elements of one row, each as the text it is written in.
bool row_items(std::string_view row, std::vector<std::string_view> &items) {
	items.clear();
	json::ArrayWalk walk(row);
	std::string_view item;
	while (walk.next(item)) items.push_back(item);
	return walk.ok();
}

json::Value value_of(std::string_view text) {
	json::Value v;
	json::parse(text, v);
	return v;
}

DictionaryInfo read_index(const std::string &text) {
	json::Value root;
	if (!json::parse(text, root, 1024)) throw DictError("The index.json of that dictionary is not JSON.");
	if (!root.is_object()) throw DictError("The index.json of that dictionary is not an object.");
	DictionaryInfo info;
	info.title = trimmed(root.str("title"));
	if (info.title.empty() || utf8::length16(info.title) > 256) throw DictError("That dictionary has no title in its index.json.");
	int format = 0;
	for (const char *name : {"format", "version"})
		if (root[name].as_int(format)) break;
	if (format < 1 || format > 3)
		throw DictError("That dictionary is in a format this app does not read (format " + std::to_string(format) + "; it reads 1 to 3).");
	info.format = format;
	info.revision = root.str("revision");
	info.author = root.str("author");
	info.url = root.str("url");
	info.description = root.str("description");
	info.attribution = root.str("attribution");
	info.frequency_mode = root.str("frequencyMode");
	info.source_language = root.str("sourceLanguage");
	info.target_language = root.str("targetLanguage");
	if (root.boolean("isUpdatable")) info.index_url = root.str("indexUrl");
	info.download_url = root.str("downloadUrl");
	return info;
}

void read_terms(const std::string &name, const std::string &bank, DictionaryWriter &writer, DictionaryInfo &info) {
	json::ArrayWalk rows(bank);
	std::string_view row;
	std::vector<std::string_view> items;
	std::string glossary, expression;
	while (rows.next(row)) {
		// Format 3: [expression, reading, definitionTags, rules, score, glossary[], sequence, termTags];
		// format 1: [expression, reading, definitionTags, rules, score, ...glossary strings].
		double score = 0;
		if (row.empty() || row[0] != '[' || !row_items(row, items) || items.size() < 5 || !key(value_of(items[0]), expression)) {
			++info.skipped;
			continue;
		}
		json::Value read_as = value_of(items[1]);
		if (!read_as.is_string() || !number(value_of(items[4]), score)) {
			++info.skipped;
			continue;
		}
		std::string reading = read_as.as_string();
		if (reading.empty()) {
			reading = expression;
		} else if (utf8::length16(reading) > kMaxKeyLength) {
			++info.skipped;
			continue;
		}

		int64_t sequence = 0;
		std::string term_tags;
		glossary.clear();
		if (info.format == 1) {
			glossary += '[';
			for (size_t i = 5; i < items.size(); ++i) {
				if (i > 5) glossary += ',';
				glossary += json::compact(items[i]);
			}
			glossary += ']';
		} else {
			if (items.size() < 6 || items[5].empty() || items[5][0] != '[') {
				++info.skipped;
				continue;
			}
			glossary = json::compact(items[5]);
			double n = 0;
			if (items.size() > 6 && number(value_of(items[6]), n)) sequence = whole(n);
			if (items.size() > 7) term_tags = tags_of(value_of(items[7]));
		}
		writer.add_term(expression, reading, tags_of(value_of(items[2])), tags_of(value_of(items[3])), whole(score), glossary, sequence,
		                term_tags);
		++info.terms;
	}
	if (!rows.ok()) throw DictError(name + " in that dictionary is not a list of rows.");
}

void read_metas(const std::string &name, const std::string &bank, DictionaryWriter &writer, DictionaryInfo &info) {
	json::ArrayWalk rows(bank);
	std::string_view row;
	std::vector<std::string_view> items;
	std::string expression;
	while (rows.next(row)) {
		// [expression, "freq" | "pitch" | "ipa", data]
		if (row.empty() || row[0] != '[' || !row_items(row, items) || items.size() < 3 || !key(value_of(items[0]), expression)) {
			++info.skipped;
			continue;
		}
		std::string mode = value_of(items[1]).as_string();
		if ((mode != "freq" && mode != "pitch" && mode != "ipa") || items[2] == "null") {
			++info.skipped;
			continue;
		}
		writer.add_meta(expression, mode, json::compact(items[2]));
		if (mode == "freq")
			++info.frequencies;
		else
			++info.pitches;
	}
	if (!rows.ok()) throw DictError(name + " in that dictionary is not a list of rows.");
}

void read_tags(const std::string &name, const std::string &bank, DictionaryWriter &writer, DictionaryInfo &info) {
	json::ArrayWalk rows(bank);
	std::string_view row;
	while (rows.next(row)) {
		// [name, category, order, notes, score]
		json::Value v = value_of(row);
		if (!v.is_array() || v.size() < 5 || !v[0].is_string()) {
			++info.skipped;
			continue;
		}
		double n = 0;
		TagRow tag;
		tag.name = v[0].as_string();
		tag.category = v[1].as_string();
		tag.order = number(v[2], n) ? whole(n) : 0;
		tag.notes = v[3].as_string();
		tag.score = number(v[4], n) ? whole(n) : 0;
		writer.add_tag(tag);
	}
	if (!rows.ok()) throw DictError(name + " in that dictionary is not a list of rows.");
}

std::string now_utc() {
	time_t now = time(nullptr);
	struct tm t;
	gmtime_r(&now, &t);
	char buffer[32];
	strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &t);
	return buffer;
}

DictionaryInfo import_into(const std::string &zip_path, const std::string &database_path, const std::string &id,
                           const std::function<void(double)> &progress, const std::atomic<bool> &stop) {
	auto zip = ZipReader::open(zip_path);
	if (!zip) throw DictError("That file is not a zip. A Yomitan dictionary is a zip with an index.json in it.");

	std::vector<const Entry *> files;
	uint64_t unpacked = 0;
	const Entry *index = nullptr, *css = nullptr;
	for (const Entry &entry : zip->entries()) {
		if (entry.is_folder()) continue;
		files.push_back(&entry);
		unpacked += entry.size;
		if (entry.name == "index.json" && !index) index = &entry;
		if (entry.name == "styles.css" && !css) css = &entry;
	}
	if (unpacked > kMaxTotalBytes) throw DictError("That dictionary is too large to import.");
	if (!index) throw DictError("That zip has no index.json at its top, so it is not a Yomitan dictionary.");
	DictionaryInfo info = read_index(read(*zip, *index, kMaxSmallFileBytes));
	info.id = id;

	auto terms = banks(files, "term_bank_"), metas = banks(files, "term_meta_bank_"), tags = banks(files, "tag_bank_");
	if (terms.empty() && metas.empty())
		throw DictError("That dictionary has no words, frequencies or pitch accents. Kanji dictionaries are not read: the popup looks "
		                "kanji up as words.");
	std::string styles = css ? read(*zip, *css, kMaxSmallFileBytes) : std::string();
	std::vector<const Entry *> media;
	for (const Entry *file : files)
		if (!media_type_of(file->name).empty() && file->size <= kMaxMediaBytes) media.push_back(file);

	uint64_t total = 0, done = 0;
	for (const auto *list : {&terms, &metas, &tags, &media})
		for (const Entry *entry : *list) total += entry->packed;
	total = std::max<uint64_t>(total, 1);
	auto advance = [&](const Entry &entry) {
		done += entry.packed;
		if (progress) progress(std::min(0.99, double(done) / double(total)));
	};
	auto check = [&] {
		if (stop.load()) throw ImportStopped();
	};

	DictionaryWriter writer(database_path);
	for (const Entry *bank : tags) {
		check();
		read_tags(bank->name, read(*zip, *bank, kMaxBankBytes), writer, info);
		advance(*bank);
	}
	for (const Entry *bank : terms) {
		check();
		read_terms(bank->name, read(*zip, *bank, kMaxBankBytes), writer, info);
		advance(*bank);
	}
	for (const Entry *bank : metas) {
		check();
		read_metas(bank->name, read(*zip, *bank, kMaxBankBytes), writer, info);
		advance(*bank);
	}
	for (const Entry *entry : media) {
		check();
		writer.add_media(entry->name, read(*zip, *entry, kMaxMediaBytes));
		++info.media;
		advance(*entry);
	}
	if (info.terms == 0 && info.frequencies == 0 && info.pitches == 0) throw DictError("No row of that dictionary could be read.");

	info.imported = now_utc();
	check();
	writer.finish(info, styles);
	if (progress) progress(1);
	return info;
}

}  // namespace

std::string media_type_of(const std::string &path) {
	size_t dot = path.rfind('.');
	size_t slash = path.find_last_of("/\\");
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
	std::string ext = lower(path.substr(dot));
	if (ext == ".png") return "image/png";
	if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
	if (ext == ".gif") return "image/gif";
	if (ext == ".webp") return "image/webp";
	if (ext == ".avif") return "image/avif";
	if (ext == ".bmp") return "image/bmp";
	if (ext == ".svg") return "image/svg+xml";
	return {};
}

DictionaryInfo import_yomitan(const std::string &zip_path, const std::string &database_path, const std::string &id,
                              const std::function<void(double)> &progress, const std::atomic<bool> &stop) {
	try {
		return import_into(zip_path, database_path, id, progress, stop);
	} catch (...) {
		remove(database_path.c_str());
		throw;
	}
}

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
