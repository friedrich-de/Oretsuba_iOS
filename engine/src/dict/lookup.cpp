// Looks up the words a Japanese text starts with.
#include "dict/lookup.h"
#if ORE_MINING_KIT

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "dict/japanese_text.h"
#include "util/utf8.h"

namespace ore::dict {

namespace {

// A form looked up: the start of the text it came from, its length in characters and how it was made.
struct Form {
	const std::string *matched;
	int length;
	int changes;
	const Deinflection *deinflection;
};

// A dictionary row found, by its dictionary's place in the order.
struct Found {
	int dictionary;
	TermRow row;
	Form form;
	bool by_expression;
};

// Which of two ways to a word is better: negative when a is.
int rank(const Form &a, bool a_by_expression, const Form &b, bool b_by_expression) {
	if (a.length != b.length) return b.length < a.length ? -1 : 1;
	if (a.changes != b.changes) return a.changes < b.changes ? -1 : 1;
	if (a.deinflection->trace.size() != b.deinflection->trace.size())
		return a.deinflection->trace.size() < b.deinflection->trace.size() ? -1 : 1;
	if (a.deinflection->whole_word != b.deinflection->whole_word) return a.deinflection->whole_word ? -1 : 1;
	if (a_by_expression != b_by_expression) return a_by_expression ? -1 : 1;
	return 0;
}

// A word: rows written and read alike.
struct Word {
	std::string expression, reading;
	std::vector<Found> rows;
	const Found *best = nullptr;
	int first_dictionary = 0;
	int64_t score = 0;
	int discovered = 0;
	bool has_frequency = false;
	double frequency = 0;
};

bool blank_or_control(uint32_t c) {
	return c < 0x20 || (c >= 0x7F && c <= 0xA0) || c == 0x20 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 ||
	       c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

// A mark that belongs to the character before it.
bool joins_previous(uint32_t c) {
	return (c >= 0x0300 && c <= 0x036F) || c == 0x3099 || c == 0x309A || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xE0100 && c <= 0xE01EF);
}

bool for_reading(const json::Value &data, const std::string &reading) {
	return data.is_object() && data["reading"].is_string() && data["reading"].as_string() == reading;
}

bool plain_number(const json::Value &value, double &out) {
	out = value.as_number();
	return value.is_number() && std::fabs(out) < 9e15;
}

json::Value morae(const json::Value &pitch, const char *name) {
	const json::Value *value = pitch.find(name);
	int one = 0;
	if (!value) return json::Value();
	if (value->as_int(one)) {
		json::Value list = json::Value::array();
		list.push(one);
		return list;
	}
	if (value->is_array()) {
		json::Value list = json::Value::array();
		for (const json::Value &item : value->items())
			if (item.as_int(one)) list.push(one);
		return list;
	}
	return json::Value();
}

// Pitch accents as Yomitan writes them: {reading, pitches: [{position, nasal, devoice}]}.
void read_pitches(const std::string &text, const std::string &reading, json::Value &out) {
	json::Value data;
	if (!json::parse(text, data) || !for_reading(data, reading) || !data["pitches"].is_array()) return;
	for (const json::Value &pitch : data["pitches"].items()) {
		if (!pitch.is_object() || !pitch.has("position")) continue;
		const json::Value &position = pitch["position"];
		json::Value view = json::Value::object();
		int mora = 0;
		if (position.as_int(mora)) {
			view.set("position", mora);
		} else if (position.is_string()) {
			// A pattern of highs and lows; the fall is after the last high that a low follows.
			const std::string &pattern = position.as_string();
			size_t fall = pattern.find("HL");
			view.set("position", fall == std::string::npos ? 0 : int(utf8::length16(std::string_view(pattern).substr(0, fall))) + 1);
			view.set("pattern", pattern);
		} else {
			continue;
		}
		json::Value nasal = morae(pitch, "nasal"), devoice = morae(pitch, "devoice");
		if (!nasal.is_null()) view.set("nasal", std::move(nasal));
		if (!devoice.is_null()) view.set("devoice", std::move(devoice));
		out.push(std::move(view));
	}
}

// Pronunciations as Yomitan writes them: {reading, transcriptions: [{ipa}]}.
void read_transcriptions(const std::string &text, const std::string &reading, json::Value &out) {
	json::Value data;
	if (!json::parse(text, data) || !for_reading(data, reading) || !data["transcriptions"].is_array()) return;
	for (const json::Value &item : data["transcriptions"].items())
		if (item.is_object() && item["ipa"].is_string()) out.push(item["ipa"].as_string());
}

std::vector<std::string_view> words_of(std::string_view text) {
	std::vector<std::string_view> out;
	size_t i = 0;
	while (i < text.size()) {
		while (i < text.size() && text[i] == ' ') ++i;
		size_t start = i;
		while (i < text.size() && text[i] != ' ') ++i;
		if (i > start) out.push_back(text.substr(start, i - start));
	}
	return out;
}

}  // namespace

bool read_frequency(const std::string &text, const std::string &reading, Frequency &out) {
	json::Value parsed;
	if (!json::parse(text, parsed)) return false;
	const json::Value *data = &parsed;
	if (data->is_object() && data->has("reading")) {
		if (!(*data)["reading"].is_string() || (*data)["reading"].as_string() != reading || !data->has("frequency")) return false;
		data = data->find("frequency");
	}
	out = Frequency();
	if (data->is_number()) return plain_number(*data, out.value);
	if (data->is_string()) {
		const std::string &s = data->as_string();
		size_t digits = 0;
		while (digits < s.size() && s[digits] >= '0' && s[digits] <= '9') ++digits;
		out.value = digits ? strtod(s.substr(0, digits).c_str(), nullptr) : 0;
		out.display = s;
		out.has_display = true;
		return true;
	}
	if (data->is_object() && data->has("value") && plain_number((*data)["value"], out.value)) {
		if ((*data)["displayValue"].is_string()) {
			out.display = (*data)["displayValue"].as_string();
			out.has_display = true;
		}
		return true;
	}
	return false;
}

json::Value look_up(const std::string &text, const std::vector<std::shared_ptr<DictionaryReader>> &dictionaries,
                    const Deinflector &rules) {
	json::Value answer = json::Value::object();
	json::Value &results = answer.set("results", json::Value::array());

	// The forms the text is looked up as: each start of it, in each spelling, with each set of
	// inflections undone, by the form. A form reached from several starts keeps each.
	std::vector<size_t> ends;
	{
		const char *p = text.data(), *end = p + text.size();
		while (p < end) {
			const char *start = p;
			uint32_t c = utf8::next(p, end);
			if (joins_previous(c) && !ends.empty()) {
				ends.back() = size_t(p - text.data());
				continue;
			}
			if (int(ends.size()) >= kScanLength || blank_or_control(c)) {
				p = start;
				break;
			}
			ends.push_back(size_t(p - text.data()));
		}
	}
	std::deque<std::string> matched_texts;
	std::deque<std::vector<Deinflection>> deinflections;
	std::unordered_map<std::string, std::vector<Form>> forms;
	std::vector<std::string> keys;
	for (int i = int(ends.size()) - 1; i >= 0; --i) {
		matched_texts.push_back(text.substr(0, ends[size_t(i)]));
		const std::string &matched = matched_texts.back();
		for (const Spelling &spelling : spellings(matched)) {
			deinflections.push_back(rules.deinflect(spelling.text));
			for (const Deinflection &deinflection : deinflections.back()) {
				auto place = forms.find(deinflection.text);
				if (place == forms.end()) {
					place = forms.emplace(deinflection.text, std::vector<Form>()).first;
					keys.push_back(deinflection.text);
				}
				place->second.push_back(Form{&matched, i + 1, spelling.changes, &deinflection});
			}
		}
	}
	if (forms.empty()) return answer;

	// Every row the forms find, each with the best way it was reached.
	std::vector<Found> found;
	for (size_t index = 0; index < dictionaries.size(); ++index) {
		DictionaryReader &dictionary = *dictionaries[index];
		if (dictionary.info().terms == 0) continue;
		for (TermRow &row : dictionary.find_terms(keys)) {
			uint64_t wanted = rules.conditions_of_entry(row.rules);
			const Form *best = nullptr;
			bool by_expression = false;
			for (int pass = 0; pass < 2; ++pass) {
				bool is_expression = pass == 0;
				auto candidates = forms.find(is_expression ? row.expression : row.reading);
				if (candidates == forms.end()) continue;
				for (const Form &form : candidates->second)
					if (Deinflector::conditions_match(form.deinflection->conditions, wanted) &&
					    (!best || rank(form, is_expression, *best, by_expression) < 0)) {
						best = &form;
						by_expression = is_expression;
					}
			}
			if (best) found.push_back(Found{int(index), std::move(row), *best, by_expression});
		}
	}

	// A word is what is written and read alike, whichever dictionaries have it.
	std::vector<Word> words;
	{
		std::unordered_map<std::string, size_t> place;
		for (Found &item : found) {
			std::string id = item.row.expression + '\0' + item.row.reading;
			auto known = place.find(id);
			if (known == place.end()) {
				known = place.emplace(std::move(id), words.size()).first;
				Word word;
				word.expression = item.row.expression;
				word.reading = item.row.reading;
				word.discovered = int(words.size());
				words.push_back(std::move(word));
			}
			words[known->second].rows.push_back(std::move(item));
		}
	}
	if (words.empty()) return answer;
	for (Word &word : words) {
		word.best = &word.rows[0];
		word.first_dictionary = word.rows[0].dictionary;
		word.score = word.rows[0].row.score;
		for (const Found &row : word.rows) {
			if (rank(row.form, row.by_expression, word.best->form, word.best->by_expression) < 0) word.best = &row;
			word.first_dictionary = std::min(word.first_dictionary, row.dictionary);
			word.score = std::max(word.score, row.row.score);
		}
	}

	// What the dictionaries with frequencies and pitch accents say of the words.
	std::vector<std::string> expressions;
	{
		std::unordered_set<std::string> seen;
		for (const Word &word : words)
			if (seen.insert(word.expression).second) expressions.push_back(word.expression);
	}
	std::map<int, std::vector<MetaRow>> metas;
	for (size_t index = 0; index < dictionaries.size(); ++index) {
		const DictionaryInfo &info = dictionaries[index]->info();
		if (info.frequencies > 0 || info.pitches > 0) metas[int(index)] = dictionaries[index]->find_metas(expressions);
	}

	// The first dictionary with frequencies sorts words that tie on everything else.
	int sort_by = -1;
	for (size_t index = 0; index < dictionaries.size() && sort_by < 0; ++index)
		if (dictionaries[index]->info().frequencies > 0) sort_by = int(index);
	bool counts = sort_by >= 0 && dictionaries[size_t(sort_by)]->info().frequency_mode == "occurrence-based";
	if (sort_by >= 0) {
		for (Word &word : words)
			for (const MetaRow &meta : metas[sort_by]) {
				Frequency frequency;
				if (meta.mode != "freq" || meta.expression != word.expression || !read_frequency(meta.data, word.reading, frequency))
					continue;
				// Its best rank, or its highest count.
				if (!word.has_frequency || (counts ? frequency.value > word.frequency : frequency.value < word.frequency))
					word.frequency = frequency.value;
				word.has_frequency = true;
			}
	}
	std::vector<const Word *> ranked;
	for (const Word &word : words) ranked.push_back(&word);
	std::stable_sort(ranked.begin(), ranked.end(), [counts](const Word *a, const Word *b) {
		int order = rank(a->best->form, a->best->by_expression, b->best->form, b->best->by_expression);
		if (order != 0) return order < 0;
		// A word the sorting dictionary knows comes before one it does not; ranks low first, counts high first.
		if (a->has_frequency != b->has_frequency) return a->has_frequency;
		if (a->has_frequency && a->frequency != b->frequency) return counts ? a->frequency > b->frequency : a->frequency < b->frequency;
		if (a->first_dictionary != b->first_dictionary) return a->first_dictionary < b->first_dictionary;
		if (a->score != b->score) return a->score > b->score;
		return a->discovered < b->discovered;
	});
	if (int(ranked.size()) > kMaxEntries) ranked.resize(size_t(kMaxEntries));

	std::map<std::pair<int, int64_t>, std::string> glossaries;
	{
		std::map<int, std::vector<int64_t>> wanted;
		for (const Word *word : ranked)
			for (const Found &row : word->rows) wanted[row.dictionary].push_back(row.row.id);
		for (const auto &group : wanted)
			for (auto &entry : dictionaries[size_t(group.first)]->glossaries(group.second))
				glossaries[{group.first, entry.first}] = std::move(entry.second);
	}

	for (const Word *word : ranked) {
		std::vector<const Found *> rows;
		for (const Found &row : word->rows) rows.push_back(&row);
		std::stable_sort(rows.begin(), rows.end(), [](const Found *a, const Found *b) {
			if (a->dictionary != b->dictionary) return a->dictionary < b->dictionary;
			if (a->row.score != b->row.score) return a->row.score > b->row.score;
			return a->row.id < b->row.id;
		});

		json::Value term = json::Value::object();
		term.set("expression", word->expression);
		term.set("reading", word->reading);
		{
			std::string all;
			std::unordered_set<std::string_view> seen;
			for (const Found *item : rows)
				for (std::string_view rule : words_of(item->row.rules))
					if (seen.insert(rule).second) {
						if (!all.empty()) all += ' ';
						all += rule;
					}
			term.set("rules", std::move(all));
		}
		json::Value &entries = term.set("glossaries", json::Value::array());
		for (const Found *item : rows) {
			DictionaryReader &dictionary = *dictionaries[size_t(item->dictionary)];
			json::Value entry = json::Value::object();
			entry.set("dictionary", dictionary.info().title);
			auto glossary = glossaries.find({item->dictionary, item->row.id});
			entry.set("glossary", glossary == glossaries.end() ? std::string("[]") : glossary->second);
			entry.set("definitionTags", item->row.definition_tags);
			entry.set("termTags", item->row.term_tags);
			json::Value &tags = entry.set("tags", json::Value::array());
			const auto &known = dictionary.tags();
			std::string names = item->row.definition_tags + " " + item->row.term_tags;
			std::unordered_set<std::string_view> seen;
			for (std::string_view name : words_of(names)) {
				if (!seen.insert(name).second) continue;
				auto tag = known.find(std::string(name));
				if (tag == known.end()) continue;
				json::Value view = json::Value::object();
				view.set("name", tag->second.name);
				view.set("category", tag->second.category);
				view.set("order", tag->second.order);
				view.set("notes", tag->second.notes);
				view.set("score", tag->second.score);
				tags.push(std::move(view));
			}
			entries.push(std::move(entry));
		}

		json::Value &frequency_groups = term.set("frequencies", json::Value::array());
		json::Value &pitch_groups = term.set("pitches", json::Value::array());
		for (const auto &group : metas) {
			const std::string &title = dictionaries[size_t(group.first)]->info().title;
			json::Value frequencies = json::Value::array(), pitches = json::Value::array(), transcriptions = json::Value::array();
			for (const MetaRow &meta : group.second) {
				if (meta.expression != word->expression) continue;
				if (meta.mode == "freq") {
					Frequency frequency;
					if (!read_frequency(meta.data, word->reading, frequency)) continue;
					json::Value view = json::Value::object();
					view.set("value", frequency.value);
					if (frequency.has_display) view.set("displayValue", frequency.display);
					frequencies.push(std::move(view));
				} else if (meta.mode == "pitch") {
					read_pitches(meta.data, word->reading, pitches);
				} else if (meta.mode == "ipa") {
					read_transcriptions(meta.data, word->reading, transcriptions);
				}
			}
			if (frequencies.size() > 0) {
				json::Value view = json::Value::object();
				view.set("dictionary", title);
				view.set("frequencies", std::move(frequencies));
				frequency_groups.push(std::move(view));
			}
			if (pitches.size() > 0 || transcriptions.size() > 0) {
				json::Value view = json::Value::object();
				view.set("dictionary", title);
				view.set("pitches", std::move(pitches));
				if (transcriptions.size() > 0) view.set("transcriptions", std::move(transcriptions));
				pitch_groups.push(std::move(view));
			}
		}

		const Form &best = word->best->form;
		json::Value result = json::Value::object();
		result.set("matched", *best.matched);
		result.set("deinflected", best.deinflection->text);
		json::Value &trace = result.set("trace", json::Value::array());
		for (const DeinflectionStep *step : best.deinflection->trace) {
			json::Value view = json::Value::object();
			view.set("name", step->name);
			if (step->has_description) view.set("description", step->description);
			trace.push(std::move(view));
		}
		result.set("term", std::move(term));
		results.push(std::move(result));
	}
	return answer;
}

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
