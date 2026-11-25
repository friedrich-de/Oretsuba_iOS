// The dictionary of the mining kit (docs/MINING.md): the spellings a text is
// looked up as, undoing inflections, importing a Yomitan dictionary, looking
// words up. Needs no game files and no Apple system:
//
//   c++ -std=c++17 -Isrc -Ithird_party tests/dict_test.cpp src/dict/*.cpp src/util/json.cpp src/util/log.cpp
//       -lsqlite3 -lz -lpthread -o /tmp/dict_test && /tmp/dict_test
//
// It reads the rules (resources/mining/transforms.json) and the rows of
// Yomitan's conjugation tests (tests/japanese) from the repository it
// is run in, or from the folder given as its first argument (the engine's).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "dict/deinflector.h"
#include "dict/dictionary_db.h"
#include "dict/japanese_text.h"
#include "dict/library.h"
#include "dict/lookup.h"
#include "dict/yomitan_import.h"
#include "dict/zip_reader.h"
#include "util/json.h"
#include "util/utf8.h"

using namespace ore;
using namespace ore::dict;

namespace {

bool ok = true;
int checks = 0;

void check(bool value, const std::string &message) {
	++checks;
	if (!value) {
		fprintf(stderr, "FAIL: %s\n", message.c_str());
		ok = false;
	}
}

void check_eq(const std::string &got, const std::string &want, const std::string &message) {
	check(got == want, message + ": got \"" + got + "\", want \"" + want + "\"");
}

std::string read_file(const std::string &path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

std::string u8(const std::u32string &s) { return utf8::encode(s); }
std::u32string u32(const std::string &s) { return utf8::decode(s); }

std::vector<std::string> split(const std::string &s, char sep) {
	std::vector<std::string> out;
	size_t at = 0;
	for (;;) {
		size_t next = s.find(sep, at);
		if (next == std::string::npos) {
			out.push_back(s.substr(at));
			return out;
		}
		out.push_back(s.substr(at, next - at));
		at = next + 1;
	}
}

// ---------------------------------------------------------------- JSON

void test_json() {
	json::Value v;
	check(json::parse(R"({"a":[1,2.5,"x\nあ😀",true,null],"b":{"c":-3e2}})", v), "json parses");
	check(v["a"].size() == 5 && v["a"][0].as_number() == 1 && v["a"][1].as_number() == 2.5, "json numbers");
	check_eq(v["a"][2].as_string(), "x\n\xE3\x81\x82\xF0\x9F\x98\x80", "json string escapes");
	check(v["a"][3].as_bool() && v["a"][4].is_null(), "json true and null");
	check(v["b"]["c"].as_number() == -300, "json exponent");
	check_eq(json::write(v), R"({"a":[1,2.5,"x\n)" "\xE3\x81\x82\xF0\x9F\x98\x80" R"(",true,null],"b":{"c":-300}})", "json writes back");
	check(!json::parse("{\"a\":1,}", v) && !json::parse("[1 2]", v) && !json::parse("", v) && !json::parse("01", v), "json refuses what is none");
	check(!json::parse(std::string(600, '[') + std::string(600, ']'), v), "json refuses a text nested too deep");
	check_eq(json::quote("a\"b\\c\x01"), R"("a\"b\\c\u0001")", "json quotes");
	check_eq(json::number_text(73.0), "73", "whole numbers have no fraction");
	check_eq(json::number_text(0.1), "0.1", "fractions are short");

	std::string rows = " [ [\"a\", 1, {\"k\": [1, 2]}] , \"s,]\" , 3 ] ";
	json::ArrayWalk walk(rows);
	std::string_view item;
	std::vector<std::string> items;
	while (walk.next(item)) items.emplace_back(item);
	check(walk.ok() && items.size() == 3, "an array is walked item by item");
	if (items.size() == 3) {
		check_eq(json::compact(items[0]), R"(["a",1,{"k":[1,2]}])", "an item's text is kept, less its blanks");
		check_eq(items[1], "\"s,]\"", "a string with brackets is one item");
	}
	json::ArrayWalk broken("[1, 2");
	while (broken.next(item)) {
	}
	check(!broken.ok(), "an array that breaks off is not ok");
	json::ArrayWalk empty("[]");
	check(!empty.next(item) && empty.ok(), "an empty array is ok");
	json::ArrayWalk none("{}");
	check(!none.next(item) && !none.ok(), "an object is no array");
}

// ---------------------------------------------------------------- spellings

void test_spellings() {
	const char *kana[][2] = {{"タベル", "たべる"}, {"スーパー", "すうぱあ"}, {"コーヒー", "こうひい"}, {"ケーキ", "けえき"},
	                         {"ヴァイオリン", "ゔぁいおりん"}, {"ヵ月ヶ所", "ヵ月ヶ所"}, {"ーカ", "ーか"}};
	for (auto &row : kana) check_eq(u8(katakana_to_hiragana(u32(row[0]))), row[1], std::string("katakana to hiragana ") + row[0]);
	check_eq(u8(hiragana_to_katakana(u32("たべる漢字ー"))), "タベル漢字ー", "hiragana to katakana");

	struct Collapse {
		const char *text;
		bool full;
		const char *want;
	} collapse[] = {{"すっっごい", false, "すっごい"}, {"すっっごい", true, "すごい"},   {"すげーーー", false, "すげーーー"},
	                {"すげーーーよ", false, "すげーよ"}, {"すげーーーよ", true, "すげよ"}, {"っっあっっ", true, "っっあっっ"},
	                {"っー", true, "っー"}};
	for (auto &row : collapse) check_eq(u8(collapse_emphatic(u32(row.text), row.full)), row.want, std::string("drawn-out sounds ") + row.text);

	check_eq(u8(expand_iteration_marks(u32("時々"))), "時時", "repeat mark");
	check_eq(u8(expand_iteration_marks(u32("々"))), "々", "repeat mark alone");
	check_eq(u8(strip_middle_dots(u32("ジョン・スミス"))), "ジョンスミス", "middle dots");
	check_eq(u8(full_width_alphanumeric(u32("CD2"))), "ＣＤ２", "full width");

	check_eq(u8(plain_forms(u32("ﾀﾍﾞﾙ"))), "タベル", "half-width katakana with a voicing mark");
	check_eq(u8(plain_forms(u32("ﾊﾟﾝ"))), "パン", "half-width katakana with a half-voicing mark");
	check_eq(u8(plain_forms(u32("ＣＤ２！？"))), "CD2!?", "full-width letters and signs");
	check_eq(u8(plain_forms(u32("㍿①⑳Ⅳⅲ……"))), "株式会社120IViii......", "compatibility characters");
	check_eq(u8(plain_forms(u32("か\xE3\x82\x99う\xE3\x82\x99"))), "がゔ", "a kana and its voicing mark are one");
	check_eq(u8(plain_forms(u32("日本語"))), "日本語", "plain text stays");

	auto found = spellings("ﾀﾍﾞﾙ");
	check(!found.empty() && found[0].text == "ﾀﾍﾞﾙ" && found[0].changes == 0, "the text as written comes first");
	bool katakana = false, hiragana = false, sorted = true, distinct = true;
	for (size_t i = 0; i < found.size(); ++i) {
		if (found[i].text == "タベル" && found[i].changes == 1) katakana = true;
		if (found[i].text == "たべる" && found[i].changes == 2) hiragana = true;
		if (i > 0 && found[i].changes < found[i - 1].changes) sorted = false;
		for (size_t j = 0; j < i; ++j)
			if (found[j].text == found[i].text) distinct = false;
	}
	check(katakana && hiragana && sorted && distinct, "each spelling counts its changes, once, the fewest first");
	auto plain = spellings("日本語");
	check(plain.size() == 1 && plain[0].text == "日本語" && plain[0].changes == 0, "a plain kanji text has one spelling");
}

// ---------------------------------------------------------------- inflections

bool fits(const Deinflector &rules, const Deinflection &result, const std::string &term, const std::string &rule,
          const std::string &reasons) {
	if (result.text != term) return false;
	if (rule != "*" && !Deinflector::conditions_match(result.conditions, rules.conditions_named(rule))) return false;
	if (reasons == "*") return true;
	// The tests list inflections in the order they were made, the reverse of the order they are undone.
	std::vector<std::string> made;
	if (!reasons.empty()) made = split(reasons, '|');
	if (made.size() != result.trace.size()) return false;
	for (size_t i = 0; i < made.size(); ++i)
		if (result.trace[result.trace.size() - 1 - i]->name != made[i]) return false;
	return true;
}

void test_deinflector(const std::string &root) {
	std::string problem;
	auto rules = Deinflector::load(read_file(root + "/resources/mining/transforms.json"), problem);
	check(rules != nullptr, "the rules load: " + problem);
	if (!rules) return;

	int rows = 0;
	for (const char *file : {"yomitan.tsv", "cases.tsv"}) {
		std::ifstream in(root + "/tests/japanese/" + file);
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			auto cells = split(line, '\t');
			if (cells.size() < 5) continue;
			// Cat speech is a pass of its own in Hoshidicts, not a rule; the dictionary does not read it.
			if (cells[0] == "cat speech") continue;
			++rows;
			bool valid = cells[1] == "1";
			std::string reasons = cells.size() > 5 ? cells[5] : "";
			bool found = false;
			for (const Deinflection &result : rules->deinflect(cells[2]))
				if (fits(*rules, result, cells[3], cells[4], reasons)) {
					found = true;
					break;
				}
			check(found == valid, std::string(file) + ": " + cells[0] + ": " + cells[2] + (valid ? " should" : " should not") +
			                          " give " + cells[3] + " (" + cells[4] + ") by " + reasons);
		}
	}
	// 1,446 rows in Yomitan's corpus and the pack's 46, less its 13 rows of cat speech.
	check(rows == 1446 + 46 - 13, "the tests cover all of Yomitan's rows (" + std::to_string(rows) + ")");

	auto first = rules->deinflect("食べられなかった");
	check(first[0].text == "食べられなかった" && first[0].conditions == 0 && first[0].trace.empty(), "a text comes first as it is");
	bool traced = false;
	for (const Deinflection &r : first) {
		if (r.text != "食べる" || !Deinflector::conditions_match(r.conditions, rules->conditions_of_entry("v1"))) continue;
		traced = r.trace.size() == 3 && r.trace[0]->name == "-た" && r.trace[1]->name == "negative" &&
		         r.trace[2]->name == "potential or passive" && !r.whole_word && !r.trace[0]->description.empty();
		break;
	}
	check(traced, "the trace lists the inflections in the order they were undone");

	bool whole = false;
	for (const Deinflection &r : rules->deinflect("ございます"))
		if (r.text == "ござる") whole = r.whole_word;
	check(whole, "a word read whole by a rule says so");
	bool fake = false;
	for (const Deinflection &r : rules->deinflect("偽ございます"))
		if (r.text == "偽ござる") fake = true;
	check(!fake, "a whole-word rule does not read the end of a longer text");

	check(rules->conditions_of_entry("") == 0 && rules->conditions_of_entry("n vt") == 0, "an entry without a dictionary form has no conditions");
	check(rules->conditions_of_entry("v5 vt") != 0 && rules->conditions_of_entry("v1") == rules->conditions_named("v1"), "an entry counts the tags of dictionary forms");
	check(Deinflector::conditions_match(0, rules->conditions_of_entry("n")), "a text as given fits any entry");
	check(!Deinflector::conditions_match(rules->conditions_named("v1"), rules->conditions_of_entry("adj-i")), "a verb form does not fit an adjective");

	std::string longer;
	for (int i = 0; i < 30; ++i) longer += "させられませんでした";
	auto many = rules->deinflect(longer);
	bool bounded = !many.empty() && int(many.size()) <= Deinflector::kMaxResults;
	for (const Deinflection &r : many)
		if (r.text.empty() || r.text.size() > longer.size() + Deinflector::kMaxGrowth) bounded = false;
	check(bounded, "a long text stays within bounds");
}


// ---------------------------------------------------------------- a small Yomitan dictionary

std::string scratch;  // a folder of the test's own
int made = 0;

void put16(std::string &out, uint32_t v) {
	out += char(v);
	out += char(v >> 8);
}
void put32(std::string &out, uint32_t v) {
	put16(out, v);
	put16(out, v >> 16);
}

// Writes a zip: every other file deflated, the rest stored.
void write_zip(const std::string &path, const std::vector<std::pair<std::string, std::string>> &files) {
	std::string out, directory;
	int n = 0;
	for (const auto &file : files) {
		const std::string &name = file.first, &data = file.second;
		uint32_t crc = uint32_t(crc32(0, reinterpret_cast<const Bytef *>(data.data()), uInt(data.size())));
		std::string packed = data;
		int method = 0;
		if (n++ % 2 == 0 && !data.empty()) {
			z_stream z{};
			deflateInit2(&z, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
			packed.assign(deflateBound(&z, uLong(data.size())), '\0');
			z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
			z.avail_in = uInt(data.size());
			z.next_out = reinterpret_cast<Bytef *>(packed.data());
			z.avail_out = uInt(packed.size());
			deflate(&z, Z_FINISH);
			packed.resize(z.total_out);
			deflateEnd(&z);
			method = 8;
		}
		uint32_t offset = uint32_t(out.size());
		put32(out, 0x04034b50);
		put16(out, 20), put16(out, 0x800), put16(out, uint32_t(method)), put16(out, 0), put16(out, 0);
		put32(out, crc), put32(out, uint32_t(packed.size())), put32(out, uint32_t(data.size()));
		put16(out, uint32_t(name.size())), put16(out, 0);
		out += name;
		out += packed;
		put32(directory, 0x02014b50);
		put16(directory, 20), put16(directory, 20), put16(directory, 0x800), put16(directory, uint32_t(method)), put16(directory, 0),
		    put16(directory, 0);
		put32(directory, crc), put32(directory, uint32_t(packed.size())), put32(directory, uint32_t(data.size()));
		put16(directory, uint32_t(name.size())), put16(directory, 0), put16(directory, 0), put16(directory, 0), put16(directory, 0);
		put32(directory, 0), put32(directory, offset);
		directory += name;
	}
	uint32_t at = uint32_t(out.size());
	out += directory;
	put32(out, 0x06054b50);
	put16(out, 0), put16(out, 0), put16(out, uint32_t(files.size())), put16(out, uint32_t(files.size()));
	put32(out, uint32_t(directory.size())), put32(out, at), put16(out, 0);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f.write(out.data(), std::streamsize(out.size()));
}

// A format 3 term row.
std::string term(const std::string &expression, const std::string &reading, const std::string &definition_tags, const std::string &rules,
                 int score, const std::string &glossary, int sequence = 0, const std::string &term_tags = "") {
	return "[" + json::quote(expression) + "," + json::quote(reading) + "," + json::quote(definition_tags) + "," + json::quote(rules) +
	       "," + std::to_string(score) + "," + glossary + "," + std::to_string(sequence) + "," + json::quote(term_tags) + "]";
}

struct TestZip {
	std::string title = "Test dictionary";
	int format = 3;
	std::string frequency_mode, index_url;
	std::vector<std::string> terms, metas, tags;  // rows as JSON, 2 rows to a bank
	std::string styles;
	bool has_styles = false;
	std::vector<std::pair<std::string, std::string>> files;
	std::string index;  // written in place of the one made from the fields
	bool has_index = true, own_index = false;

	std::string write(const std::string &name = "") const {
		std::vector<std::pair<std::string, std::string>> all;
		if (own_index) {
			if (!index.empty()) all.emplace_back("index.json", index);
		} else if (has_index) {
			std::string made_index = "{\"title\": " + json::quote(title) + ", \"revision\": \"1\", \"format\": " + std::to_string(format) +
			                         ", \"author\": \"Tests\", \"attribution\": \"Made for the tests\"";
			if (!frequency_mode.empty()) made_index += ", \"frequencyMode\": " + json::quote(frequency_mode);
			if (!index_url.empty()) made_index += ", \"isUpdatable\": true, \"indexUrl\": " + json::quote(index_url);
			all.emplace_back("index.json", made_index + "}");
		}
		auto banks = [&](const char *prefix, const std::vector<std::string> &rows) {
			for (size_t i = 0; i * 2 < rows.size(); ++i) {
				std::string bank = "[" + rows[i * 2];
				if (i * 2 + 1 < rows.size()) bank += ",\n  " + rows[i * 2 + 1];
				all.emplace_back(std::string(prefix) + "_" + std::to_string(i + 1) + ".json", bank + "]");
			}
		};
		banks("term_bank", terms);
		banks("term_meta_bank", metas);
		banks("tag_bank", tags);
		if (has_styles) all.emplace_back("styles.css", styles);
		for (const auto &file : files) all.push_back(file);
		std::string path = scratch + "/" + (name.empty() ? "zip" + std::to_string(++made) : name) + ".zip";
		write_zip(path, all);
		return path;
	}
};

const std::atomic<bool> kGo{false};

bool exists(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

// What importing a zip says when it is refused; empty when it is taken.
std::string refusal(const TestZip &zip, const std::string &database) {
	try {
		import_yomitan(zip.write(), database, "refused", nullptr, kGo);
	} catch (const DictError &error) {
		return error.what();
	}
	return {};
}

const char kStructured[] =
    R"([{"type":"structured-content","content":[{"tag":"span","style":{"fontWeight":"bold"},"content":"to eat"},{"tag":"img","path":"pictures/bowl.png","width":2,"height":2},"a long example sentence that is worth compressing. a long example sentence that is worth compressing. a long example sentence that is worth compressing. a long example sentence that is worth compressing. a long example sentence that is worth compressing. a long example sentence that is worth compressing. a long example sentence that is worth compressing. a long example sentence that is worth compressing. "]}])";

void test_import() {
	TestZip zip;
	zip.title = "Test [2026-11-24]";
	zip.frequency_mode = "rank-based";
	zip.terms = {term("食べる", "たべる", "v1 vt", "v1", 100, kStructured, 1, "P"), term("食", "しょく", "n", "", 5, R"(["food"])"),
	             term("ごはん", "", "n", "", 3, R"(["rice", "meal"])")};
	zip.metas = {R"(["食べる","freq",{"reading":"たべる","frequency":{"value":512,"displayValue":"512㋕"}}])",
	             R"(["食べる","pitch",{"reading":"たべる","pitches":[{"position":2}]}])",
	             R"(["食べる","ipa",{"reading":"たべる","transcriptions":[{"ipa":"[tabeɾɯ]","tags":[]}]}])"};
	zip.tags = {R"(["v1","partOfSpeech",0,"Ichidan verb",0])", R"(["P","popular",-10,"common word",10])"};
	zip.styles = "span { color: red; }";
	zip.has_styles = true;
	zip.files = {{"pictures/bowl.png", std::string("\x89PNG", 4)}, {"notes.txt", "not a picture"}};
	std::string path = scratch + "/test.db";
	std::vector<double> progress;
	DictionaryInfo info;
	try {
		info = import_yomitan(zip.write(), path, "test", [&](double value) { progress.push_back(value); }, kGo);
	} catch (const std::exception &error) {
		check(false, std::string("a dictionary is imported: ") + error.what());
		return;
	}
	check(info.id == "test" && info.title == "Test [2026-11-24]" && info.format == 3 && info.frequency_mode == "rank-based", "the import reads the index");
	check(info.terms == 3 && info.frequencies == 1 && info.pitches == 2 && info.media == 1 && info.skipped == 0, "the import counts what it found");
	check(!progress.empty() && progress.back() == 1, "the import says when it is done");

	auto reader = DictionaryReader::open(path);
	check(reader != nullptr, "the imported file opens");
	if (!reader) return;
	check(reader->info().title == info.title && reader->styles() == "span { color: red; }", "the file keeps the title and the styles");
	check(reader->tags().count("v1") && reader->tags().at("v1").notes == "Ichidan verb" && reader->tags().at("P").category == "popular" &&
	          reader->tags().at("P").order == -10,
	      "the file keeps the tags");
	auto eat = reader->find_terms({"食べる"});
	check(eat.size() == 1, "a word is found as written");
	if (eat.size() == 1) {
		const TermRow &row = eat[0];
		check(row.expression == "食べる" && row.reading == "たべる" && row.definition_tags == "v1 vt" && row.rules == "v1" && row.score == 100 &&
		          row.sequence == 1 && row.term_tags == "P",
		      "a word keeps its columns");
		auto read = reader->find_terms({"たべる"});
		check(read.size() == 1 && read[0].id == row.id, "a word is found as read");
		check_eq(reader->glossaries({row.id})[row.id], kStructured, "a long definition comes back as it was");
	}
	// A word without a reading is read as it is written.
	auto rice = reader->find_terms({"ごはん"});
	check(rice.size() == 1 && rice[0].reading == "ごはん", "a word without a reading is read as written");
	if (rice.size() == 1) check_eq(reader->glossaries({rice[0].id})[rice[0].id], R"(["rice", "meal"])", "a definition is kept as the bank writes it");
	check(reader->find_metas({"食べる"}).size() == 3, "a word's frequencies and pitches are found");
	std::string picture;
	check(reader->media("pictures/bowl.png", picture) && picture == std::string("\x89PNG", 4), "a picture is kept");
	check(!reader->media("notes.txt", picture), "a file that is no picture is left out");

	std::string long_text = kStructured, short_text = R"(["food"])";
	std::string packed_long = pack_block(long_text), packed_short = pack_block(short_text), back;
	check(packed_long.size() < long_text.size() / 2, "long definitions are kept compressed");
	check(packed_short.size() == short_text.size() + 1, "short ones as they are");
	check(unpack_block(packed_long.data(), packed_long.size(), back) && back == long_text, "a compressed definition unpacks");
	check(unpack_block(packed_short.data(), packed_short.size(), back) && back == short_text, "a plain definition unpacks");
	check(!unpack_block("\x02\xff\xff\xff\x7f" "x", 6, back) && !unpack_block("\x09", 1, back), "a damaged definition is refused");

	// Definitions are gathered into blocks; one that repeats the one before it is kept once.
	TermBatch batch;
	auto word = [](const std::string &expression) {
		TermBatch::Row row;
		row.expression = row.reading = expression;
		return row;
	};
	std::string filler(kBlockBytes / 2 + 100, 'x');
	batch.add(word("一"), R"(["one"])");
	batch.add(word("壱"), R"(["one"])");
	batch.add(word("二"), "[\"" + filler + "\"]");
	batch.add(word("三"), "[\"" + filler + "\"]");
	batch.add(word("四"), "[\"" + filler + "y\"]");
	batch.add(word("五"), R"(["five"])");
	batch.finish();
	check(batch.rows.size() == 6 && batch.blocks.size() == 2, "definitions are gathered into blocks of a size");
	check(batch.rows[0].block == 0 && batch.rows[1].start == batch.rows[0].start && batch.rows[1].length == batch.rows[0].length,
	      "a definition that repeats is kept once");
	check(batch.rows[3].start == batch.rows[2].start && batch.rows[4].start > batch.rows[3].start && batch.rows[4].block == 0 &&
	          batch.rows[5].block == 1 && batch.rows[5].start == 0,
	      "and a full block is closed before the next definition");

	// Many banks are read at once and written in their order: every word has its own definitions.
	TestZip many;
	many.title = "Many banks";
	std::string padding(3000, 'p');
	for (int i = 0; i < 120; ++i)
		many.terms.push_back(term("語" + std::to_string(i), "ご" + std::to_string(i), "", "", i,
		                          "[\"meaning " + std::to_string(i) + " " + padding + "\"]"));
	std::string many_path = scratch + "/many.db";
	DictionaryInfo many_info = import_yomitan(many.write(), many_path, "many", nullptr, kGo);
	auto many_reader = DictionaryReader::open(many_path);
	bool in_order = many_reader != nullptr && many_info.terms == 120, all_there = in_order;
	for (int i = 0; many_reader && i < 120; ++i) {
		auto found = many_reader->find_terms({"語" + std::to_string(i)});
		if (found.size() != 1) {
			all_there = false;
			continue;
		}
		if (found[0].id != i + 1) in_order = false;
		if (many_reader->glossaries({found[0].id})[found[0].id] != "[\"meaning " + std::to_string(i) + " " + padding + "\"]") all_there = false;
	}
	check(in_order, "the words of many banks are written in the order of the banks");
	check(all_there, "and each has its own definitions");
	if (many_reader) {
		std::vector<int64_t> every;
		for (int i = 1; i <= 120; ++i) every.push_back(i);
		auto each = many_reader->glossaries(every);
		bool right = each.size() == 120;
		for (int i = 0; right && i < 120; ++i) right = each[i + 1].rfind("[\"meaning " + std::to_string(i) + " ", 0) == 0;
		check(right, "definitions from more blocks than a reader keeps are read");
	}
	TestZip cut = many;
	cut.terms[61] = "[\"語\", \"ご\"";
	check_eq(refusal(cut, scratch + "/broken.db"), "term_bank_31.json in that dictionary is not a list of rows.",
	         "a bank that is no list ends the import, however many are read at once");
	check(!exists(scratch + "/broken.db"), "and leaves no file");

	TestZip old;
	old.format = 1;
	old.terms = {R"(["犬","いぬ","n","",1,"dog","hound"])"};
	std::string old_path = scratch + "/old.db";
	import_yomitan(old.write(), old_path, "old", nullptr, kGo);
	auto old_reader = DictionaryReader::open(old_path);
	auto dog = old_reader ? old_reader->find_terms({"犬"}) : std::vector<TermRow>();
	check(dog.size() == 1 && old_reader->glossaries({dog[0].id})[dog[0].id] == R"(["dog","hound"])", "format 1 lists its definitions after the score");

	TestZip rows;
	std::string too_long;
	for (size_t i = 0; i < kMaxKeyLength + 1; ++i) too_long += "長";
	rows.terms = {term("猫", "ねこ", "", "", 0, R"(["cat"])"),
	              R"([5,"ねこ","","",0,["not a word"],0,""])",
	              R"(["","ねこ","","",0,["no word"],0,""])",
	              R"(["猫","ねこ","","","high",["no score"],0,""])",
	              term(too_long, "ながい", "", "", 0, R"(["too long"])")};
	rows.metas = {R"(["猫","sound",1])", R"(["猫","freq",12])"};
	DictionaryInfo counted = import_yomitan(rows.write(), scratch + "/rows.db", "rows", nullptr, kGo);
	check(counted.terms == 1 && counted.frequencies == 1 && counted.skipped == 5, "rows that cannot be read are left out and counted");

	std::string refused = scratch + "/refused.db";
	const char *indexes[][2] = {
	    {"", "That zip has no index.json at its top, so it is not a Yomitan dictionary."},
	    {"not json", "The index.json of that dictionary is not JSON."},
	    {"{\"revision\":\"1\",\"format\":3}", "That dictionary has no title in its index.json."},
	    {"{\"title\":\"New\",\"format\":4}", "That dictionary is in a format this app does not read (format 4; it reads 1 to 3)."},
	};
	for (auto &row : indexes) {
		TestZip bad;
		bad.own_index = true;
		bad.index = row[0];
		bad.terms = {term("猫", "ねこ", "", "", 0, R"(["cat"])")};
		check_eq(refusal(bad, refused), row[1], "a zip that is no dictionary is refused");
		check(!exists(refused), "and leaves no file");
	}
	TestZip kanji;
	kanji.files = {{"kanji_bank_1.json", R"([["日","ニチ","ひ","",["day"],{}]])"}};
	check(refusal(kanji, refused).rfind("That dictionary has no words, frequencies or pitch accents.", 0) == 0, "a kanji dictionary alone is refused");
	TestZip broken;
	broken.terms = {term("猫", "ねこ", "", "", 0, R"(["cat"])") + "]]"};
	check(refusal(broken, refused) == "term_bank_1.json in that dictionary is not a list of rows." && !exists(refused), "a bank that is no list of rows is refused");

	{
		std::ofstream f(scratch + "/not.zip");
		f << "hello";
	}
	std::string said;
	try {
		import_yomitan(scratch + "/not.zip", scratch + "/not.db", "not", nullptr, kGo);
	} catch (const DictError &error) {
		said = error.what();
	}
	check(said.rfind("That file is not a zip.", 0) == 0, "a file that is no zip is refused");

	std::atomic<bool> stop{true};
	bool stopped = false;
	TestZip cat;
	cat.terms = {term("猫", "ねこ", "", "", 0, R"(["cat"])")};
	try {
		import_yomitan(cat.write(), scratch + "/stopped.db", "stopped", nullptr, stop);
	} catch (const ImportStopped &) {
		stopped = true;
	}
	check(stopped && !exists(scratch + "/stopped.db"), "an import that is stopped leaves no file");

	{
		std::ofstream f(scratch + "/other.db");
		f << "not sqlite";
	}
	check(DictionaryReader::open(scratch + "/other.db") == nullptr, "a file that is no dictionary file is not opened");
}

// ---------------------------------------------------------------- looking up

std::shared_ptr<const Deinflector> rules;

TestZip words(const std::string &title = "Words") {
	TestZip zip;
	zip.title = title;
	zip.terms = {term("食べる", "たべる", "v1 vt", "v1", 10, R"(["to eat"])"),
	             term("食", "しょく", "n", "", 1, R"(["food"])"),
	             term("日本語", "にほんご", "n", "", 5, R"(["Japanese"])"),
	             term("日本", "にほん", "n", "", 5, R"(["Japan"])"),
	             term("日", "ひ", "n", "", 5, R"(["day"])"),
	             term("書く", "かく", "v5", "v5", 5, R"(["to write"])"),
	             term("書いる", "かいる", "n", "", 5, R"(["a noun no verb reaches"])"),
	             term("白い", "しろい", "adj-i", "adj-i", 5, R"(["white"])"),
	             term("買う", "かう", "v5", "v5", 1, R"(["to buy"])"),
	             term("飼う", "かう", "v5", "v5", 1, R"(["to keep an animal"])"),
	             term("ケーキ", "ケーキ", "n", "", 1, R"(["cake"])"),
	             term("ございます", "ございます", "exp", "", 1, R"j(["to be (polite)"])j"),
	             term("ござる", "ござる", "v5", "v5", 1, R"j(["to be (archaic)"])j")};
	zip.tags = {R"(["v1","partOfSpeech",0,"Ichidan verb",0])"};
	return zip;
}

std::shared_ptr<DictionaryReader> open_zip(const TestZip &zip) {
	std::string id = "lookup" + std::to_string(++made);
	std::string path = scratch + "/" + id + ".db";
	import_yomitan(zip.write(), path, id, nullptr, kGo);
	return DictionaryReader::open(path);
}

json::Value find(const std::string &text, const std::vector<TestZip> &zips) {
	std::vector<std::shared_ptr<DictionaryReader>> readers;
	for (const TestZip &zip : zips) readers.push_back(open_zip(zip));
	// Through its text, as the popup gets it.
	json::Value back;
	json::parse(json::write(look_up(text, readers, *rules)), back);
	return back["results"];
}

std::string expressions(const json::Value &results, size_t most = 99) {
	std::string all;
	for (size_t i = 0; i < results.size() && i < most; ++i) all += (i ? "," : "") + results[i]["term"].str("expression");
	return all;
}

std::string names(const json::Value &list, const char *member) {
	std::string all;
	for (size_t i = 0; i < list.size(); ++i) all += (i ? "," : "") + list[i].str(member);
	return all;
}

void test_lookup() {
	json::Value results = find("食べられなかったの。", {words()});
	const json::Value &first = results[0];
	check(first["term"].str("expression") == "食べる" && first["term"].str("reading") == "たべる", "a conjugated verb is found as its dictionary form");
	check(first.str("matched") == "食べられなかった" && first.str("deinflected") == "食べる", "with the text it was found as");
	check_eq(names(first["trace"], "name"), "-た,negative,potential or passive", "and what was undone");
	check(!first["trace"][0].str("description").empty(), "each with what it means");
	check(first["term"]["glossaries"].size() == 1 && first["term"]["glossaries"][0].str("glossary") == R"(["to eat"])", "its definition is JSON in a text");
	check(first["term"]["glossaries"][0]["tags"].size() == 1 && first["term"]["glossaries"][0]["tags"][0].str("notes") == "Ichidan verb", "its tags are explained");
	check(first["term"].str("rules") == "v1" && first["term"]["glossaries"][0].str("definitionTags") == "v1 vt", "its rules and tags are given");

	results = find("日本語を話す", {words()});
	check_eq(expressions(results), "日本語,日本,日", "the longest word comes first");
	check_eq(names(results, "matched"), "日本語,日本,日", "each with its own start of the text");

	results = find("書いた", {words()});
	check(results[0]["term"].str("expression") == "書く" && expressions(results).find("書いる") == std::string::npos,
	      "a word is reached by undoing an inflection only when it is that kind of word");
	results = find("白くない", {words()});
	check(results[0]["term"].str("expression") == "白い" && results[0].str("matched") == "白くない", "an adjective is found from its negative");
	for (const char *text : {"たべる", "タベル", "ﾀﾍﾞﾙ"})
		check(find(text, {words()})[0]["term"].str("expression") == "食べる", std::string("a word is found by its reading: ") + text);
	check(find("けーき", {words()})[0]["term"].str("expression") == "ケーキ", "a katakana word is found from hiragana with a long mark");
	results = find("ございます", {words()});
	check_eq(expressions(results, 2), "ございます,ござる", "a polite form finds its dictionary form after the word as written");
	check_eq(names(results[1]["trace"], "name"), "-ます", "by the rule that read it whole");

	TestZip ranks;
	ranks.title = "Ranks";
	ranks.frequency_mode = "rank-based";
	ranks.metas = {R"(["飼う","freq",{"reading":"かう","frequency":5000}])",
	               R"(["買う","freq",{"reading":"かう","frequency":{"value":100,"displayValue":"100"}}])",
	               R"(["買う","freq",{"reading":"ばいう","frequency":1}])"};
	TestZip counts;
	counts.title = "Counts";
	counts.frequency_mode = "occurrence-based";
	counts.metas = {R"(["飼う","freq",900])", R"(["買う","freq",10])"};
	check_eq(expressions(find("かう", {words(), ranks}), 2), "買う,飼う", "words that tie come by rank, the low first");
	check_eq(expressions(find("かう", {words(), counts}), 2), "飼う,買う", "or by count, the high first");
	results = find("かう", {words(), ranks, counts});
	const json::Value &buy = results[0]["term"]["frequencies"];
	check_eq(names(buy, "dictionary"), "Ranks,Counts", "a word has its frequencies by dictionary");
	check(buy[0]["frequencies"].size() == 1 && buy[0]["frequencies"][0].number("value") == 100 && buy[0]["frequencies"][0].str("displayValue") == "100",
	      "of its own reading only");

	TestZip pitches;
	pitches.title = "Pitches";
	pitches.metas = {R"(["食べる","pitch",{"reading":"たべる","pitches":[{"position":2,"devoice":1},{"position":"LHL"}]}])",
	                 R"(["食べる","pitch",{"reading":"くらべる","pitches":[{"position":0}]}])",
	                 R"(["食べる","ipa",{"reading":"たべる","transcriptions":[{"ipa":"[tabeɾɯ]"}]}])"};
	results = find("食べる", {words(), pitches});
	const json::Value &groups = results[0]["term"]["pitches"];
	check(groups.size() == 1 && groups[0].str("dictionary") == "Pitches", "a word has the pitch accents of its reading only");
	const json::Value &group = groups[0]["pitches"];
	check(group.size() == 2 && group[0].number("position") == 2 && group[1].number("position") == 2, "a pattern gives where it falls");
	check(group[0]["devoice"].size() == 1 && group[0]["devoice"][0].as_number() == 1 && !group[0].has("nasal") && !group[0].has("pattern") &&
	          group[1].str("pattern") == "LHL",
	      "with what the dictionary says of it and nothing else");
	check(groups[0]["transcriptions"].size() == 1 && groups[0]["transcriptions"][0].as_string() == "[tabeɾɯ]", "and its pronunciation");

	TestZip odd;
	odd.title = "Odd";
	odd.metas = {R"(["食べる","pitch",{"reading":42,"pitches":[{"position":2}]}])",
	             R"(["食べる","ipa",{"reading":["たべる"],"transcriptions":[{"ipa":"x"}]}])", R"(["食べる","freq",{"reading":true,"frequency":5}])"};
	results = find("食べる", {words(), odd});
	check(results[0]["term"].str("expression") == "食べる" && results[0]["term"]["pitches"].size() == 0 && results[0]["term"]["frequencies"].size() == 0,
	      "metadata that is not what the format says is left out and the word still found");
	check(!results[0]["term"]["pitches"].is_null() && results[0]["term"]["frequencies"].is_array(), "the lists are there when empty");

	TestZip other;
	other.title = "Other";
	other.terms = {term("食べる", "たべる", "v1", "v1", 1, R"(["to consume"])")};
	results = find("食べる", {other, words()});
	check_eq(names(results[0]["term"]["glossaries"], "dictionary"), "Other,Words", "a word in two dictionaries is one result with both definitions in their order");
	check(expressions(results).find("食べる", 1) == std::string::npos, "and no second result");

	check(find("日本 語", {words()})[0].str("matched") == "日本", "the text is read up to a space");
	std::string long_text;
	for (int i = 0; i < kScanLength + 10; ++i) long_text += "あ";
	TestZip longest;
	std::string sixteen, seventeen;
	for (int i = 0; i < kScanLength; ++i) sixteen += "あ";
	seventeen = sixteen + "あ";
	longest.terms = {term(sixteen, sixteen, "n", "", 0, R"(["sixteen"])"), term(seventeen, seventeen, "n", "", 0, R"(["seventeen"])")};
	check_eq(expressions(find(long_text, {longest})), sixteen, "and no further than the scan length");
	for (const char *text : {"", " 食べる", "xyz"}) check(find(text, {words()}).size() == 0, std::string("a text without a word at its start finds nothing: ") + text);

	TestZip many;
	many.title = "Many";
	for (int i = 0; i < kMaxEntries + 5; ++i) {
		std::string reading = "か";
		for (int k = 0; k < i; ++k) reading += "ー";
		many.terms.push_back(term("か", reading, "n", "", i, "[\"meaning " + std::to_string(i) + "\"]"));
	}
	results = find("か", {many});
	check(int(results.size()) == kMaxEntries, "no more words are given than the most a lookup gives");
	check_eq(results[0]["term"]["glossaries"][0].str("glossary"), "[\"meaning " + std::to_string(kMaxEntries + 4) + "\"]", "the best of them first");

	struct Case {
		const char *json;
		double value;
		const char *display;
	} frequencies[] = {{"12", 12, nullptr}, {"\"3㋕\"", 3, "3㋕"}, {"{\"value\":7,\"displayValue\":\"7 (rare)\"}", 7, "7 (rare)"},
	                   {"{\"reading\":\"たべる\",\"frequency\":9}", 9, nullptr}};
	for (auto &row : frequencies) {
		Frequency f;
		check(read_frequency(row.json, "たべる", f) && f.value == row.value && f.has_display == (row.display != nullptr) &&
		          (!row.display || f.display == row.display),
		      std::string("a frequency is read as Yomitan writes it: ") + row.json);
	}
	for (const char *text : {"{\"reading\":\"くう\",\"frequency\":9}", "{\"value\":\"many\"}", "null", "1e400"}) {
		Frequency f;
		check(!read_frequency(text, "たべる", f), std::string("a frequency for another reading or of no number is none: ") + text);
	}

	auto reader = open_zip(words());
	auto started = std::chrono::steady_clock::now();
	for (int i = 0; i < 20; ++i) look_up("食べさせられなかったんですけど本当に", {reader}, *rules);
	auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	check(took < 20 * 200, "a lookup is quick enough to follow a tap (" + std::to_string(took) + " ms for 20)");
}

// ---------------------------------------------------------------- the library

void test_library() {
	std::string folder = scratch + "/library";
	auto fetch_of = [](std::string path) {
		return [path](const std::function<void(double)> &progress, const std::atomic<bool> &) {
			if (progress) progress(1);
			return path;
		};
	};
	std::string first_id, second_id;
	{
		DictionaryLibrary library(folder, rules);
		check(library.installed().empty() && library.import_state().state == "idle", "a new library is empty");
		std::string zip = words().write("words");
		check(library.start_import("words.zip", fetch_of(zip), true), "an import starts");
		library.wait_import();
		ImportState state = library.import_state();
		check(state.state == "done" && state.title == "Words" && state.progress == 1 && state.source == "words.zip", "and says what it imported: " + state.error);
		check(!exists(zip), "the zip is removed when the import ends");
		check(library.installed().size() == 1 && library.installed()[0].enabled, "the dictionary is listed, on");
		first_id = library.installed().empty() ? "" : library.installed()[0].reader->info().id;
		check(library.lookup("食べた")["results"][0]["term"].str("expression") == "食べる", "the library looks words up");

		TestZip styled = words("Styled");
		styled.styles = ".x { color: red; }";
		styled.has_styles = true;
		styled.files = {{"img/a.png", "PNG"}};
		library.start_import("styled.zip", fetch_of(styled.write("styled")), true);
		library.wait_import();
		check(library.installed().size() == 2, "a second dictionary comes after the first");
		second_id = library.installed().size() == 2 ? library.installed()[1].reader->info().id : "";
		json::Value styles = library.styles();
		check(styles.size() == 1 && styles[0].str("dictionary") == "Styled" && styles[0].str("styles") == ".x { color: red; }", "the styles of the dictionaries that have some");
		std::string data, type;
		check(library.media(second_id, "img/a.png", data, type) && data == "PNG" && type == "image/png", "a picture by the dictionary's name");
		check(library.media("Styled", "img/a.png", data, type), "or by its title");
		check(!library.media("Styled", "img/none.png", data, type) && !library.media("Styled", "index.json", data, type) &&
		          !library.media("Nobody", "img/a.png", data, type),
		      "and none where there is none");

		check_eq(names(library.lookup("食べる")["results"][0]["term"]["glossaries"], "dictionary"), "Words,Styled", "lookups read the dictionaries in their order");
		check(!library.arrange({second_id}, {}) && !library.arrange({second_id, second_id}, {}) && !library.arrange({second_id, "nobody"}, {}),
		      "an order must name each dictionary once");
		check(library.arrange({second_id, first_id}, {first_id}), "the dictionaries are put in a new order");
		check_eq(names(library.lookup("食べる")["results"][0]["term"]["glossaries"], "dictionary"), "Styled", "one turned off is not read");

		// The same title again takes the place of the old one, off as it was.
		TestZip newer = words();
		newer.terms.push_back(term("新しい", "あたらしい", "adj-i", "adj-i", 1, R"(["new"])"));
		std::string old_path = library.installed()[1].reader->path();
		library.start_import("words.zip", fetch_of(newer.write("newer")), true);
		library.wait_import();
		auto now = library.installed();
		check(now.size() == 2 && now[1].reader->info().title == "Words" && !now[1].enabled && now[1].reader->info().id != first_id,
		      "a dictionary imported again takes the place of the old one, off as it was");
		check(!exists(old_path), "and the old file is gone");
		first_id = now[1].reader->info().id;

		TestZip bad;
		bad.own_index = true;
		library.start_import("bad.zip", fetch_of(bad.write("bad")), true);
		library.wait_import();
		check(library.import_state().state == "failed" && library.import_state().error.find("index.json") != std::string::npos, "an import that fails says why");
		library.start_import("gone", [](const std::function<void(double)> &, const std::atomic<bool> &) -> std::string { throw DictError("The address did not answer."); }, false);
		library.wait_import();
		check(library.import_state().error == "The address did not answer." && library.installed().size() == 2, "so does a download that fails");

		// One import at a time, and it can be stopped.
		std::atomic<bool> release{false};
		check(library.start_import("slow", [&](const std::function<void(double)> &, const std::atomic<bool> &stop) -> std::string {
			while (!release && !stop) std::this_thread::sleep_for(std::chrono::milliseconds(5));
			if (stop) throw ImportStopped();
			return "";
		}, false), "a slow import starts");
		check(library.import_state().running() && !library.start_import("second", fetch_of(""), false), "no second import starts while one runs");
		library.cancel_import();
		library.wait_import();
		check(library.import_state().state == "failed" && library.import_state().error == "The import was stopped.", "an import can be stopped");
	}
	{
		// Read again, as the app starts.
		{
			std::ofstream f(folder + "/half.importing");
			f << "x";
		}
		DictionaryLibrary library(folder, rules);
		auto now = library.installed();
		check(now.size() == 2 && now[0].reader->info().id == second_id && now[0].enabled && now[1].reader->info().id == first_id && !now[1].enabled,
		      "the library is read again in its order, on and off as they were");
		check(!exists(folder + "/half.importing"), "an import the end of the app cut short is thrown away");
		std::string path = now[0].reader->path();
		auto held = now[0].reader;
		now.clear();
		check(library.remove(second_id) && !library.remove(second_id), "a dictionary is removed once");
		check(library.installed().size() == 1 && exists(path), "its file stays while something reads it");
		check(held->find_terms({"食べる"}).size() == 1, "which still can");
		held.reset();
		check(!exists(path), "and goes when the last reader does");
	}
	{
		DictionaryLibrary library(folder, rules);
		check(library.installed().size() == 1 && library.installed()[0].reader->info().id == first_id, "what was removed stays removed");
	}
}

}  // namespace

int main(int argc, char **argv) {
	std::string root = argc > 1 ? argv[1] : ".";
	char folder[] = "/tmp/ore-dict-test-XXXXXX";
	if (!mkdtemp(folder)) return 2;
	scratch = folder;
	test_json();
	test_spellings();
	test_deinflector(root);
	std::string problem;
	rules = Deinflector::load(read_file(root + "/resources/mining/transforms.json"), problem);
	if (rules) {
		test_import();
		test_lookup();
		test_library();
	}
	std::string clean = "rm -rf '" + scratch + "'";
	if (system(clean.c_str()) != 0) fprintf(stderr, "could not remove %s\n", scratch.c_str());
	printf("%s: %d checks\n", ok ? "ok" : "FAILED", checks);
	return ok ? 0 : 1;
}
