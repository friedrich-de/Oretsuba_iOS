// Undoes Japanese inflections: 食べられなかった gives 食べる, among other
// texts. It runs Yomitan's rules, as the Hoshidicts language pack writes them
// as data (transforms.json), the way Yomitan's LanguageTransformer runs them:
// every rule whose ending fits the text and whose conditions fit the text's
// gives a new text, which the rules read again, until none fits.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ore::dict {

// One inflection undone on the way from the text to a dictionary form.
struct DeinflectionStep {
	std::string name;         // its short name, such as "-た" or "negative"
	std::string description;  // what it means, for the popup's tooltip; may be empty
	bool has_description = false;
};

// A text a dictionary may hold, reached from the text looked up by undoing inflections.
struct Deinflection {
	std::string text;
	uint64_t conditions = 0;                      // what kind of word the text must be; 0 for the text as given
	std::vector<const DeinflectionStep *> trace;  // the inflections undone, the first undone first
	bool whole_word = false;                      // the first step read the whole text as one known word (ございます)
};

class Deinflector {
public:
	// A text gets no more than this many rules undone after each other.
	static constexpr int kMaxDepth = 128;
	// A text found is never this many bytes longer than the text given.
	static constexpr int kMaxGrowth = 96;
	// A lookup never considers more texts than this for one text given.
	static constexpr int kMaxResults = 4096;

	// Reads rules in the Hoshidicts pack's transforms.json shape. Null with the
	// reason in `problem` when the text is not such a file.
	static std::unique_ptr<Deinflector> load(std::string_view json, std::string &problem);

	// Whether a text with conditions `current` may be read by something that wants
	// `wanted`: always when the text is as it was given (0), else when they share a flag.
	static bool conditions_match(uint64_t current, uint64_t wanted) { return current == 0 || (current & wanted) != 0; }

	// The conditions of a dictionary entry, from its space-separated rule tags
	// ("v5 vt"). Only the tags that name a dictionary form count.
	uint64_t conditions_of_entry(std::string_view rules) const;
	// The flags of one condition by its name, or 0.
	uint64_t conditions_named(std::string_view name) const;

	// Every text that undoing inflections reaches from `text`, the text itself
	// first, each with the shortest trace that reached it first.
	std::vector<Deinflection> deinflect(const std::string &text) const;

private:
	struct Rule {
		const DeinflectionStep *step;
		std::string inflected, deinflected;
		uint64_t in = 0, out = 0;
		bool whole_word = false;
	};
	struct Node {
		std::string text;
		uint64_t conditions;
		const Rule *rule;
		int from;  // index of the node it was found from, -1 for the text given
		int depth;
		bool whole_word;
	};
	void apply(int at, const std::vector<const Rule *> &rules, std::vector<Node> &nodes, size_t limit) const;

	std::vector<std::unique_ptr<DeinflectionStep>> steps_;
	std::vector<std::unique_ptr<Rule>> rules_;
	std::unordered_map<std::string, std::vector<const Rule *>> by_suffix_, by_word_;
	std::unordered_map<std::string, uint64_t> conditions_, dictionary_forms_;
	size_t longest_suffix_ = 0;
};

}  // namespace ore::dict
