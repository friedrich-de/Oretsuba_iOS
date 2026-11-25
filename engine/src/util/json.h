// JSON as the mining kit needs it: a small value tree for settings, requests
// and answers, and a scanner that walks a large file (a dictionary's bank of
// rows) value by value without building all of it.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ore::json {

class Value {
public:
	enum Kind { Null, Bool, Number, String, Array, Object };

	Value() = default;
	Value(bool v) : kind_(Bool), bool_(v) {}
	Value(double v) : kind_(Number), number_(v) {}
	Value(int v) : kind_(Number), number_(v) {}
	Value(int64_t v) : kind_(Number), number_(double(v)) {}
	Value(const char *v) : kind_(String), string_(v ? v : "") {}
	Value(std::string v) : kind_(String), string_(std::move(v)) {}
	Value(std::string_view v) : kind_(String), string_(v) {}

	static Value array() {
		Value v;
		v.kind_ = Array;
		return v;
	}
	static Value object() {
		Value v;
		v.kind_ = Object;
		return v;
	}

	Kind kind() const { return kind_; }
	bool is_null() const { return kind_ == Null; }
	bool is_bool() const { return kind_ == Bool; }
	bool is_number() const { return kind_ == Number; }
	bool is_string() const { return kind_ == String; }
	bool is_array() const { return kind_ == Array; }
	bool is_object() const { return kind_ == Object; }

	bool as_bool(bool fallback = false) const { return kind_ == Bool ? bool_ : fallback; }
	double as_number(double fallback = 0) const { return kind_ == Number ? number_ : fallback; }
	// A whole number that fits an int; false for anything else.
	bool as_int(int &out) const;
	const std::string &as_string() const { return kind_ == String ? string_ : empty_string(); }

	// Arrays.
	size_t size() const { return kind_ == Array ? items_.size() : kind_ == Object ? members_.size() : 0; }
	const Value &operator[](size_t i) const { return kind_ == Array && i < items_.size() ? items_[i] : null(); }
	const Value &operator[](int i) const { return i < 0 ? null() : (*this)[size_t(i)]; }
	const std::vector<Value> &items() const { return items_; }
	Value &push(Value v) {
		if (kind_ != Array) *this = array();
		items_.push_back(std::move(v));
		return items_.back();
	}

	// Objects keep their members in the order they were given.
	const std::vector<std::pair<std::string, Value>> &members() const { return members_; }
	const Value *find(std::string_view key) const;
	const Value &operator[](std::string_view key) const {
		const Value *v = find(key);
		return v ? *v : null();
	}
	const Value &operator[](const char *key) const { return (*this)[std::string_view(key)]; }
	Value &set(std::string_view key, Value v);
	bool erase(std::string_view key);
	bool has(std::string_view key) const { return find(key) != nullptr; }

	// Shorthands for a member of a kind, or the fallback when it is missing or of another kind.
	std::string str(std::string_view key, std::string_view fallback = {}) const;
	bool boolean(std::string_view key, bool fallback = false) const;
	double number(std::string_view key, double fallback = 0) const;

	static const Value &null();

private:
	static const std::string &empty_string();
	Kind kind_ = Null;
	bool bool_ = false;
	double number_ = 0;
	std::string string_;
	std::vector<Value> items_;
	std::vector<std::pair<std::string, Value>> members_;
};

// Reads one JSON text. False when it is not JSON or nests deeper than max_depth.
bool parse(std::string_view text, Value &out, int max_depth = 256);

// Writes a value without blanks.
std::string write(const Value &value);
void write(const Value &value, std::string &out);

// A text as a JSON string, quotes included.
std::string quote(std::string_view text);
void quote(std::string_view text, std::string &out);

// A number as JSON writes it: whole numbers without a fraction.
std::string number_text(double value);

// ---------------------------------------------------------------- scanning

// Walks JSON text without building it. Each function moves `p` on and returns
// false at the first thing that is not JSON.
void skip_blank(const char *&p, const char *end);
// Moves past one value of any kind; `depth` limits how deep it may nest.
bool skip_value(const char *&p, const char *end, int depth = 512);

// The elements of an array, each as the piece of text it is written in.
// Usage: ArrayWalk walk(text); std::string_view item; while (walk.next(item)) ...; if (!walk.ok()) ...
class ArrayWalk {
public:
	explicit ArrayWalk(std::string_view text);
	bool next(std::string_view &item);
	// False when the text was not an array or broke off inside one.
	bool ok() const { return ok_; }

private:
	const char *p_, *end_;
	bool ok_ = true, started_ = false, done_ = false;
};

// A piece of JSON text without the blanks between its values.
std::string compact(std::string_view text);

}  // namespace ore::json
