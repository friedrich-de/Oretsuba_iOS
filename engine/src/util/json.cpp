// JSON as the mining kit needs it.
#include "util/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "util/utf8.h"

namespace ore::json {

const Value &Value::null() {
	static const Value v;
	return v;
}

const std::string &Value::empty_string() {
	static const std::string s;
	return s;
}

bool Value::as_int(int &out) const {
	if (kind_ != Number || !(number_ >= -2147483648.0 && number_ <= 2147483647.0) || number_ != std::floor(number_))
		return false;
	out = int(number_);
	return true;
}

const Value *Value::find(std::string_view key) const {
	if (kind_ != Object) return nullptr;
	for (const auto &m : members_)
		if (m.first == key) return &m.second;
	return nullptr;
}

Value &Value::set(std::string_view key, Value v) {
	if (kind_ != Object) *this = object();
	for (auto &m : members_)
		if (m.first == key) {
			m.second = std::move(v);
			return m.second;
		}
	members_.emplace_back(std::string(key), std::move(v));
	return members_.back().second;
}

bool Value::erase(std::string_view key) {
	if (kind_ != Object) return false;
	for (size_t i = 0; i < members_.size(); ++i)
		if (members_[i].first == key) {
			members_.erase(members_.begin() + long(i));
			return true;
		}
	return false;
}

std::string Value::str(std::string_view key, std::string_view fallback) const {
	const Value *v = find(key);
	return v && v->is_string() ? v->as_string() : std::string(fallback);
}

bool Value::boolean(std::string_view key, bool fallback) const {
	const Value *v = find(key);
	return v && v->is_bool() ? v->as_bool() : fallback;
}

double Value::number(std::string_view key, double fallback) const {
	const Value *v = find(key);
	return v && v->is_number() ? v->as_number() : fallback;
}

// ---------------------------------------------------------------- reading

namespace {

bool is_blank(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool hex4(const char *p, const char *end, uint32_t &out) {
	if (end - p < 4) return false;
	out = 0;
	for (int i = 0; i < 4; ++i) {
		char c = p[i];
		uint32_t d;
		if (c >= '0' && c <= '9')
			d = uint32_t(c - '0');
		else if (c >= 'a' && c <= 'f')
			d = uint32_t(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			d = uint32_t(c - 'A' + 10);
		else
			return false;
		out = out << 4 | d;
	}
	return true;
}

// p stands on the opening quote.
bool read_string(const char *&p, const char *end, std::string &out) {
	++p;
	const char *run = p;
	for (;;) {
		if (p >= end) return false;
		unsigned char c = (unsigned char)*p;
		if (c == '"') {
			out.append(run, size_t(p - run));
			++p;
			return true;
		}
		if (c < 0x20) return false;
		if (c != '\\') {
			++p;
			continue;
		}
		out.append(run, size_t(p - run));
		if (++p >= end) return false;
		char e = *p++;
		switch (e) {
		case '"': out += '"'; break;
		case '\\': out += '\\'; break;
		case '/': out += '/'; break;
		case 'b': out += '\b'; break;
		case 'f': out += '\f'; break;
		case 'n': out += '\n'; break;
		case 'r': out += '\r'; break;
		case 't': out += '\t'; break;
		case 'u': {
			uint32_t cp;
			if (!hex4(p, end, cp)) return false;
			p += 4;
			if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
				uint32_t low;
				if (hex4(p + 2, end, low) && low >= 0xDC00 && low < 0xE000) {
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					p += 6;
				}
			}
			// Half of a pair alone is no character.
			if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
			utf8::append(out, cp);
			break;
		}
		default: return false;
		}
		run = p;
	}
}

bool number_span(const char *&p, const char *end) {
	const char *q = p;
	if (q < end && *q == '-') ++q;
	if (q >= end) return false;
	if (*q == '0') {
		++q;
	} else if (*q >= '1' && *q <= '9') {
		while (q < end && *q >= '0' && *q <= '9') ++q;
	} else {
		return false;
	}
	if (q < end && *q == '.') {
		++q;
		if (q >= end || *q < '0' || *q > '9') return false;
		while (q < end && *q >= '0' && *q <= '9') ++q;
	}
	if (q < end && (*q == 'e' || *q == 'E')) {
		++q;
		if (q < end && (*q == '+' || *q == '-')) ++q;
		if (q >= end || *q < '0' || *q > '9') return false;
		while (q < end && *q >= '0' && *q <= '9') ++q;
	}
	p = q;
	return true;
}

bool word(const char *&p, const char *end, const char *w) {
	size_t n = strlen(w);
	if (size_t(end - p) < n || memcmp(p, w, n) != 0) return false;
	p += n;
	return true;
}

bool read_value(const char *&p, const char *end, Value &out, int depth) {
	skip_blank(p, end);
	if (p >= end) return false;
	switch (*p) {
	case '{': {
		if (depth <= 0) return false;
		++p;
		out = Value::object();
		skip_blank(p, end);
		if (p < end && *p == '}') {
			++p;
			return true;
		}
		for (;;) {
			skip_blank(p, end);
			if (p >= end || *p != '"') return false;
			std::string key;
			if (!read_string(p, end, key)) return false;
			skip_blank(p, end);
			if (p >= end || *p != ':') return false;
			++p;
			Value member;
			if (!read_value(p, end, member, depth - 1)) return false;
			// A name given twice keeps what came last, as browsers read it.
			out.set(key, std::move(member));
			skip_blank(p, end);
			if (p >= end) return false;
			if (*p == ',') {
				++p;
				continue;
			}
			if (*p == '}') {
				++p;
				return true;
			}
			return false;
		}
	}
	case '[': {
		if (depth <= 0) return false;
		++p;
		out = Value::array();
		skip_blank(p, end);
		if (p < end && *p == ']') {
			++p;
			return true;
		}
		for (;;) {
			Value item;
			if (!read_value(p, end, item, depth - 1)) return false;
			out.push(std::move(item));
			skip_blank(p, end);
			if (p >= end) return false;
			if (*p == ',') {
				++p;
				continue;
			}
			if (*p == ']') {
				++p;
				return true;
			}
			return false;
		}
	}
	case '"': {
		std::string s;
		if (!read_string(p, end, s)) return false;
		out = Value(std::move(s));
		return true;
	}
	case 't':
		if (!word(p, end, "true")) return false;
		out = Value(true);
		return true;
	case 'f':
		if (!word(p, end, "false")) return false;
		out = Value(false);
		return true;
	case 'n':
		if (!word(p, end, "null")) return false;
		out = Value();
		return true;
	default: {
		const char *start = p;
		if (!number_span(p, end)) return false;
		char buffer[64];
		size_t n = size_t(p - start);
		if (n < sizeof buffer) {
			memcpy(buffer, start, n);
			buffer[n] = 0;
			out = Value(strtod(buffer, nullptr));
		} else {
			out = Value(strtod(std::string(start, n).c_str(), nullptr));
		}
		return true;
	}
	}
}

}  // namespace

bool parse(std::string_view text, Value &out, int max_depth) {
	const char *p = text.data(), *end = p + text.size();
	// A file some editors write starts with a byte order mark.
	if (text.size() >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3;
	Value value;
	if (!read_value(p, end, value, max_depth)) return false;
	skip_blank(p, end);
	if (p != end) return false;
	out = std::move(value);
	return true;
}

// ---------------------------------------------------------------- writing

void quote(std::string_view text, std::string &out) {
	out += '"';
	const char *run = text.data(), *p = run, *end = run + text.size();
	for (; p < end; ++p) {
		unsigned char c = (unsigned char)*p;
		if (c >= 0x20 && c != '"' && c != '\\') continue;
		out.append(run, size_t(p - run));
		run = p + 1;
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default: {
			char buffer[8];
			snprintf(buffer, sizeof buffer, "\\u%04x", c);
			out += buffer;
		}
		}
	}
	out.append(run, size_t(p - run));
	out += '"';
}

std::string quote(std::string_view text) {
	std::string out;
	out.reserve(text.size() + 2);
	quote(text, out);
	return out;
}

std::string number_text(double value) {
	if (!std::isfinite(value)) return "null";
	char buffer[40];
	if (value == std::floor(value) && std::fabs(value) < 9007199254740992.0) {
		snprintf(buffer, sizeof buffer, "%lld", (long long)value);
		return buffer;
	}
	snprintf(buffer, sizeof buffer, "%.15g", value);
	if (strtod(buffer, nullptr) != value) snprintf(buffer, sizeof buffer, "%.17g", value);
	return buffer;
}

void write(const Value &value, std::string &out) {
	switch (value.kind()) {
	case Value::Null: out += "null"; break;
	case Value::Bool: out += value.as_bool() ? "true" : "false"; break;
	case Value::Number: out += number_text(value.as_number()); break;
	case Value::String: quote(value.as_string(), out); break;
	case Value::Array: {
		out += '[';
		bool first = true;
		for (const Value &item : value.items()) {
			if (!first) out += ',';
			first = false;
			write(item, out);
		}
		out += ']';
		break;
	}
	case Value::Object: {
		out += '{';
		bool first = true;
		for (const auto &m : value.members()) {
			if (!first) out += ',';
			first = false;
			quote(m.first, out);
			out += ':';
			write(m.second, out);
		}
		out += '}';
		break;
	}
	}
}

std::string write(const Value &value) {
	std::string out;
	write(value, out);
	return out;
}

// ---------------------------------------------------------------- scanning

void skip_blank(const char *&p, const char *end) {
	while (p < end && is_blank(*p)) ++p;
}

namespace {

bool skip_string(const char *&p, const char *end) {
	++p;
	while (p < end) {
		unsigned char c = (unsigned char)*p;
		if (c == '"') {
			++p;
			return true;
		}
		if (c < 0x20) return false;
		if (c == '\\') {
			if (++p >= end) return false;
		}
		++p;
	}
	return false;
}

}  // namespace

bool skip_value(const char *&p, const char *end, int depth) {
	skip_blank(p, end);
	if (p >= end) return false;
	switch (*p) {
	case '"': return skip_string(p, end);
	case '{':
	case '[': {
		if (depth <= 0) return false;
		char open = *p, close = open == '{' ? '}' : ']';
		++p;
		skip_blank(p, end);
		if (p < end && *p == close) {
			++p;
			return true;
		}
		for (;;) {
			if (open == '{') {
				skip_blank(p, end);
				if (p >= end || *p != '"' || !skip_string(p, end)) return false;
				skip_blank(p, end);
				if (p >= end || *p != ':') return false;
				++p;
			}
			if (!skip_value(p, end, depth - 1)) return false;
			skip_blank(p, end);
			if (p >= end) return false;
			if (*p == ',') {
				++p;
				continue;
			}
			if (*p == close) {
				++p;
				return true;
			}
			return false;
		}
	}
	case 't': return word(p, end, "true");
	case 'f': return word(p, end, "false");
	case 'n': return word(p, end, "null");
	default: return number_span(p, end);
	}
}

ArrayWalk::ArrayWalk(std::string_view text) : p_(text.data()), end_(text.data() + text.size()) {
	if (text.size() >= 3 && memcmp(p_, "\xEF\xBB\xBF", 3) == 0) p_ += 3;
	skip_blank(p_, end_);
	if (p_ >= end_ || *p_ != '[') {
		ok_ = false;
		done_ = true;
		return;
	}
	++p_;
}

bool ArrayWalk::next(std::string_view &item) {
	if (done_) return false;
	skip_blank(p_, end_);
	if (p_ < end_ && *p_ == ']' && !started_) {
		++p_;
		skip_blank(p_, end_);
		ok_ = p_ == end_;
		done_ = true;
		return false;
	}
	if (started_) {
		if (p_ < end_ && *p_ == ']') {
			++p_;
			skip_blank(p_, end_);
			ok_ = p_ == end_;
			done_ = true;
			return false;
		}
		if (p_ >= end_ || *p_ != ',') {
			ok_ = false;
			done_ = true;
			return false;
		}
		++p_;
		skip_blank(p_, end_);
	}
	started_ = true;
	const char *start = p_;
	if (!skip_value(p_, end_)) {
		ok_ = false;
		done_ = true;
		return false;
	}
	item = std::string_view(start, size_t(p_ - start));
	return true;
}

std::string compact(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	const char *p = text.data(), *end = p + text.size();
	while (p < end) {
		char c = *p;
		if (is_blank(c)) {
			++p;
			continue;
		}
		if (c != '"') {
			out += c;
			++p;
			continue;
		}
		const char *start = p;
		if (!skip_string(p, end)) p = end;
		out.append(start, size_t(p - start));
	}
	return out;
}

}  // namespace ore::json
