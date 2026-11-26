// The other spellings a Japanese text is looked up as.
#include "dict/japanese_text.h"

#include <algorithm>
#include <unordered_map>

#include "util/utf8.h"

namespace ore::dict {

namespace {

const char32_t kSmallTsuHiragana = U'っ', kSmallTsuKatakana = U'ッ', kProlonged = U'ー';
const char32_t kVoiced = 0x3099, kSemiVoiced = 0x309A;

// Half-width katakana U+FF61..U+FF9F as full-width; the two voicing marks as combining ones.
const char32_t kHalfWidth[] = U"。「」、・ヲァィゥェォャュョッーアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワン\x3099\x309A";

struct Written {
	char32_t from;
	const char32_t *as;
};

// The compatibility characters of the Shift-JIS a game is written in (NEC's and
// IBM's rows), and the two a Japanese sentence is full of.
const Written kWritten[] = {
    {0x2025, U".."},     {0x2026, U"..."},    {0x2116, U"No"},     {0x2121, U"TEL"},    {0x3000, U" "},
    {0x309B, U" \x3099"}, {0x309C, U" \x309A"}, {0x3231, U"(株)"},   {0x3232, U"(有)"},   {0x3239, U"(代)"},
    {0x32A4, U"上"},     {0x32A5, U"中"},     {0x32A6, U"下"},     {0x32A7, U"左"},     {0x32A8, U"右"},
    {0x3303, U"アール"}, {0x330D, U"カロリー"}, {0x3314, U"キロ"},   {0x3318, U"グラム"}, {0x3322, U"センチ"},
    {0x3323, U"セント"}, {0x3326, U"ドル"},   {0x3327, U"トン"},   {0x332B, U"パーセント"}, {0x3336, U"ヘクタール"},
    {0x333B, U"ページ"}, {0x3349, U"ミリ"},   {0x334A, U"ミリバール"}, {0x334D, U"メートル"}, {0x3351, U"リットル"},
    {0x3357, U"ワット"}, {0x337B, U"平成"},   {0x337C, U"昭和"},   {0x337D, U"大正"},   {0x337E, U"明治"},
    {0x337F, U"株式会社"}, {0x338E, U"mg"},   {0x338F, U"kg"},     {0x339C, U"mm"},     {0x339D, U"cm"},
    {0x339E, U"km"},     {0x33A1, U"m2"},     {0x33C4, U"cc"},     {0x33CD, U"KK"},     {0xFF5F, U"\x2985"},
    {0xFF60, U"\x2986"}, {0xFFE0, U"\xA2"},   {0xFFE1, U"\xA3"},   {0xFFE2, U"\xAC"},   {0xFFE3, U" \x304"},
    {0xFFE4, U"\xA6"},   {0xFFE5, U"\xA5"},   {0xFFE6, U"\x20A9"},
};

const char32_t *const kRoman[] = {U"I", U"II", U"III", U"IV", U"V", U"VI", U"VII", U"VIII", U"IX", U"X", U"XI", U"XII"};

// The kana a voicing mark makes of the kana before it, or 0.
char32_t voiced(char32_t base, char32_t mark) {
	static const std::u32string kPlus1 = U"かきくけこさしすせそたちつてとはひふへほカキクケコサシスセソタチツテトハヒフヘホ";
	static const std::u32string kPlus2 = U"はひふへほハヒフヘホ";
	if (mark == kVoiced) {
		if (kPlus1.find(base) != std::u32string::npos) return base + 1;
		switch (base) {
		case U'う': return U'ゔ';
		case U'ウ': return U'ヴ';
		case U'ワ': return 0x30F7;
		case U'ヰ': return 0x30F8;
		case U'ヱ': return 0x30F9;
		case U'ヲ': return 0x30FA;
		case U'ゝ': return U'ゞ';
		case U'ヽ': return U'ヾ';
		default: return 0;
		}
	}
	if (mark == kSemiVoiced && kPlus2.find(base) != std::u32string::npos) return base + 2;
	return 0;
}

// The vowel a hiragana ends in, for a ー after it; 0 when it has none.
char32_t vowel_of(char32_t kana) {
	static const std::unordered_map<char32_t, char32_t> map = [] {
		const std::pair<const char32_t *, char32_t> rows[] = {
		    {U"ぁあかがさざただなはばぱまゃやらゎわ", U'あ'},
		    {U"ぃいきぎしじちぢにひびぴみり", U'い'},
		    {U"ぅうくぐすずつづぬふぶぷむゅゆるゔ", U'う'},
		    {U"ぇえけげせぜてでねへべぺめれ", U'え'},
		    {U"ぉおこごそぞとどのほぼぽもょよろを", U'う'},
		};
		std::unordered_map<char32_t, char32_t> m;
		for (const auto &row : rows)
			for (const char32_t *p = row.first; *p; ++p) m[*p] = row.second;
		return m;
	}();
	auto it = map.find(kana);
	return it == map.end() ? 0 : it->second;
}

bool emphatic(char32_t c) {
	return c == kSmallTsuHiragana || c == kSmallTsuKatakana || c == kProlonged;
}

void put_plain(std::u32string &out, char32_t c) {
	if (c >= 0xFF01 && c <= 0xFF5E) {
		out += char32_t(c - 0xFEE0);
	} else if (c >= 0xFF61 && c <= 0xFF9F) {
		out += kHalfWidth[c - 0xFF61];
	} else if (c >= 0x2460 && c <= 0x2473) {
		int n = int(c - 0x2460) + 1;
		if (n >= 10) out += char32_t(U'0' + n / 10);
		out += char32_t(U'0' + n % 10);
	} else if (c >= 0x2160 && c <= 0x216B) {
		out += kRoman[c - 0x2160];
	} else if (c >= 0x2170 && c <= 0x217B) {
		for (const char32_t *p = kRoman[c - 0x2170]; *p; ++p) out += char32_t(*p + 0x20);
	} else {
		for (const Written &w : kWritten)
			if (w.from == c) {
				out += w.as;
				return;
			}
		out += c;
	}
}

}  // namespace

std::u32string plain_forms(const std::u32string &text) {
	std::u32string plain;
	plain.reserve(text.size());
	for (char32_t c : text) put_plain(plain, c);
	// A voicing mark joins the kana before it.
	std::u32string out;
	out.reserve(plain.size());
	for (char32_t c : plain) {
		if ((c == kVoiced || c == kSemiVoiced) && !out.empty()) {
			if (char32_t joined = voiced(out.back(), c)) {
				out.back() = joined;
				continue;
			}
		}
		out += c;
	}
	return out;
}

std::u32string katakana_to_hiragana(const std::u32string &text) {
	std::u32string out;
	out.reserve(text.size());
	for (char32_t c : text) {
		char32_t vowel = c == kProlonged && !out.empty() ? vowel_of(out.back()) : 0;
		if (vowel)
			out += vowel;
		else if (c >= U'ァ' && c <= U'ヴ')
			out += char32_t(c - 0x60);
		else
			out += c;
	}
	return out;
}

std::u32string hiragana_to_katakana(const std::u32string &text) {
	std::u32string out = text;
	for (char32_t &c : out)
		if (c >= U'ぁ' && c <= U'ゖ') c = char32_t(c + 0x60);
	return out;
}

std::u32string collapse_emphatic(const std::u32string &text, bool full) {
	size_t left = 0;
	while (left < text.size() && emphatic(text[left])) ++left;
	long right = long(text.size()) - 1;
	while (right >= 0 && emphatic(text[size_t(right)])) --right;
	if (long(left) > right) return text;

	std::u32string out;
	out.reserve(text.size());
	out.append(text, 0, left);
	char32_t last = 0;
	for (size_t i = left; i <= size_t(right); ++i) {
		char32_t c = text[i];
		if (emphatic(c)) {
			if (c != last) {
				last = c;
				if (!full) out += c;
			}
		} else {
			last = 0;
			out += c;
		}
	}
	out.append(text, size_t(right) + 1, std::u32string::npos);
	return out;
}

std::u32string full_width_alphanumeric(const std::u32string &text) {
	std::u32string out = text;
	for (char32_t &c : out)
		if ((c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z')) c = char32_t(c + 0xFEE0);
	return out;
}

std::u32string expand_iteration_marks(const std::u32string &text) {
	if (text.find(U'々') == std::u32string::npos) return text;
	std::u32string out;
	out.reserve(text.size());
	for (char32_t c : text) out += c == U'々' && !out.empty() ? out.back() : c;
	return out;
}

std::u32string strip_middle_dots(const std::u32string &text) {
	std::u32string out;
	out.reserve(text.size());
	for (char32_t c : text)
		if (c != U'・' && c != 0xFF65) out += c;
	return out;
}

std::vector<Spelling> spellings(const std::string &text) {
	using Step = std::u32string (*)(const std::u32string &);
	// The steps, in Hoshidicts' order; each gives one or more spellings beside the text itself.
	static const std::vector<std::vector<Step>> steps = {
	    {plain_forms},
	    {katakana_to_hiragana, hiragana_to_katakana},
	    {[](const std::u32string &t) { return collapse_emphatic(t, false); },
	     [](const std::u32string &t) { return collapse_emphatic(t, true); }},
	    {full_width_alphanumeric},
	    {expand_iteration_marks},
	    {strip_middle_dots},
	};

	std::vector<std::pair<std::u32string, int>> found{{utf8::decode(text), 0}};
	std::unordered_map<std::u32string, size_t> index{{found[0].first, 0}};
	for (const auto &step : steps) {
		// Each step reads the spellings as they were when it began.
		size_t before = found.size();
		std::vector<int> counts(before);
		for (size_t i = 0; i < before; ++i) counts[i] = found[i].second;
		for (size_t i = 0; i < before; ++i) {
			for (Step spell : step) {
				if (int(found.size()) >= kMaxSpellings) break;
				// By value: `found` grows underneath.
				std::u32string from = found[i].first;
				int changes = counts[i];
				std::u32string changed = spell(from);
				if (changed == from) continue;
				auto known = index.find(changed);
				if (known == index.end()) {
					index.emplace(changed, found.size());
					found.emplace_back(std::move(changed), changes + 1);
				} else if (found[known->second].second > changes + 1) {
					found[known->second].second = changes + 1;
				}
			}
		}
	}

	std::vector<Spelling> out;
	out.reserve(found.size());
	for (auto &item : found) out.push_back({utf8::encode(item.first), item.second});
	std::sort(out.begin(), out.end(), [](const Spelling &a, const Spelling &b) {
		return a.changes != b.changes ? a.changes < b.changes : a.text < b.text;
	});
	return out;
}

}  // namespace ore::dict
