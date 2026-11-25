// Undoes Japanese inflections with Yomitan's rules.
#include "dict/deinflector.h"

#include <algorithm>
#include <functional>

#include "util/json.h"

namespace ore::dict {

std::unique_ptr<Deinflector> Deinflector::load(std::string_view text, std::string &problem) {
	json::Value file;
	if (!json::parse(text, file) || !file.is_object()) {
		problem = "The rules file is not JSON.";
		return nullptr;
	}
	const json::Value &conditions = file["conditions"];
	const json::Value &transforms = file["transforms"];
	if (!conditions.is_object() || !transforms.is_array()) {
		problem = "The rules file has no conditions or no transforms.";
		return nullptr;
	}

	auto d = std::unique_ptr<Deinflector>(new Deinflector());
	// Each plain condition gets a bit, in the file's order; one made of others stands for all of theirs.
	int bit = 0;
	for (const auto &member : conditions.members()) {
		const json::Value &subs = member.second["subConditions"];
		if (subs.is_array() && subs.size() > 0) continue;
		if (bit == 64) {
			problem = "The rules have more than 64 conditions.";
			return nullptr;
		}
		d->conditions_[member.first] = uint64_t(1) << bit++;
	}

	bool unknown = false;
	std::function<uint64_t(const std::string &, size_t)> flags = [&](const std::string &name, size_t depth) -> uint64_t {
		auto known = d->conditions_.find(name);
		if (known != d->conditions_.end()) return known->second;
		const json::Value *condition = conditions.find(name);
		if (depth > conditions.size() || !condition || !(*condition)["subConditions"].is_array()) {
			unknown = true;
			problem = "The rules name a condition they do not define: " + name + ".";
			return 0;
		}
		uint64_t all = 0;
		for (const json::Value &sub : (*condition)["subConditions"].items()) all |= flags(sub.as_string(), depth + 1);
		d->conditions_[name] = all;
		return all;
	};
	for (const auto &member : conditions.members()) flags(member.first, 0);
	if (unknown) return nullptr;
	for (const auto &member : conditions.members())
		if (member.second.boolean("isDictionaryForm")) d->dictionary_forms_[member.first] = d->conditions_[member.first];

	auto all = [&](const json::Value &names) {
		uint64_t out = 0;
		if (names.is_array())
			for (const json::Value &name : names.items()) out |= flags(name.as_string(), 0);
		return out;
	};

	for (const json::Value &transform : transforms.items()) {
		std::string id = transform.str("id");
		auto step = std::make_unique<DeinflectionStep>();
		step->name = transform["name"].is_string() ? transform.str("name") : id;
		step->has_description = transform["description"].is_string();
		step->description = transform.str("description");
		const DeinflectionStep *kept = step.get();
		d->steps_.push_back(std::move(step));
		for (const json::Value &rule : transform["rules"].items()) {
			std::string type = rule.str("type");
			bool whole = type == "wholeWord";
			if (!whole && type != "suffix") {
				problem = "The dictionary does not know rules of the type " + type + ".";
				return nullptr;
			}
			auto read = std::make_unique<Rule>();
			read->step = kept;
			read->inflected = rule.str("inflected");
			read->deinflected = rule.str("deinflected");
			if (read->inflected.empty()) {
				problem = "A rule of " + id + " has no ending.";
				return nullptr;
			}
			read->in = all(rule["conditionsIn"]);
			read->out = all(rule["conditionsOut"]);
			read->whole_word = whole;
			(whole ? d->by_word_ : d->by_suffix_)[read->inflected].push_back(read.get());
			d->rules_.push_back(std::move(read));
		}
	}
	if (unknown) return nullptr;
	for (const auto &entry : d->by_suffix_) d->longest_suffix_ = std::max(d->longest_suffix_, entry.first.size());
	return d;
}

uint64_t Deinflector::conditions_of_entry(std::string_view rules) const {
	uint64_t flags = 0;
	size_t i = 0;
	while (i < rules.size()) {
		while (i < rules.size() && rules[i] == ' ') ++i;
		size_t start = i;
		while (i < rules.size() && rules[i] != ' ') ++i;
		if (i > start) {
			auto form = dictionary_forms_.find(std::string(rules.substr(start, i - start)));
			if (form != dictionary_forms_.end()) flags |= form->second;
		}
	}
	return flags;
}

uint64_t Deinflector::conditions_named(std::string_view name) const {
	auto found = conditions_.find(std::string(name));
	return found == conditions_.end() ? 0 : found->second;
}

void Deinflector::apply(int at, const std::vector<const Rule *> &rules, std::vector<Node> &nodes, size_t limit) const {
	// By value: `nodes` grows underneath.
	const std::string text = nodes[size_t(at)].text;
	const uint64_t conditions = nodes[size_t(at)].conditions;
	const int depth = nodes[size_t(at)].depth;
	const bool whole_so_far = nodes[size_t(at)].whole_word;
	for (const Rule *rule : rules) {
		if (int(nodes.size()) >= kMaxResults) return;
		if (!conditions_match(conditions, rule->in)) continue;
		std::string next = rule->whole_word ? rule->deinflected
		                                    : text.substr(0, text.size() - rule->inflected.size()) + rule->deinflected;
		if (next.empty() || next.size() > limit) continue;
		// The rule applied to the same text on the way here would go round.
		bool applied = false;
		for (int n = at; n >= 0 && nodes[size_t(n)].rule; n = nodes[size_t(n)].from) {
			const Node &node = nodes[size_t(n)];
			if (node.rule == rule && nodes[size_t(node.from)].text == text) {
				applied = true;
				break;
			}
		}
		if (applied) continue;
		nodes.push_back(Node{std::move(next), rule->out, rule, at, depth + 1, depth == 0 ? rule->whole_word : whole_so_far});
	}
}

std::vector<Deinflection> Deinflector::deinflect(const std::string &text) const {
	std::vector<Node> nodes;
	nodes.push_back(Node{text, 0, nullptr, -1, 0, false});
	size_t limit = text.size() + size_t(kMaxGrowth);
	for (size_t i = 0; i < nodes.size() && int(nodes.size()) < kMaxResults; ++i) {
		if (nodes[i].depth >= kMaxDepth) continue;
		auto words = by_word_.find(nodes[i].text);
		if (words != by_word_.end()) apply(int(i), words->second, nodes, limit);
		size_t size = nodes[i].text.size();
		for (size_t length = std::min(size, longest_suffix_); length > 0; --length) {
			// An ending starts at the start of a character.
			if ((uint8_t(nodes[i].text[size - length]) & 0xC0) == 0x80) continue;
			auto rules = by_suffix_.find(nodes[i].text.substr(size - length));
			if (rules != by_suffix_.end()) apply(int(i), rules->second, nodes, limit);
		}
	}

	std::vector<Deinflection> out;
	out.reserve(nodes.size());
	for (const Node &node : nodes) {
		Deinflection d;
		d.text = node.text;
		d.conditions = node.conditions;
		d.whole_word = node.whole_word;
		d.trace.resize(size_t(node.depth));
		const Node *n = &node;
		for (int k = node.depth - 1; k >= 0; --k) {
			d.trace[size_t(k)] = n->rule->step;
			n = &nodes[size_t(n->from)];
		}
		out.push_back(std::move(d));
	}
	return out;
}

}  // namespace ore::dict
