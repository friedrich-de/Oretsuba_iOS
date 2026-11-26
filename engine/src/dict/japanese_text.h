// The other spellings a Japanese text is looked up as, as Yomitan and
// Hoshidicts make them: half-width and compatibility characters made plain,
// katakana as hiragana and back, drawn-out ー and っ shortened, 々 written out
// and ・ left out. Each spelling counts the changes that made it, so a word
// found as written comes first.
#pragma once

#include <string>
#include <vector>

namespace ore::dict {

struct Spelling {
	std::string text;  // UTF-8
	int changes = 0;   // how many of the steps changed it
};

// A text gets no more spellings than this.
inline constexpr int kMaxSpellings = 4096;

// The spellings of a text: the text itself with 0 changes, then the others,
// those with fewer changes first. A spelling two steps reach keeps the smaller number.
std::vector<Spelling> spellings(const std::string &text);

// The steps, each on its own (UTF-32).
// Compatibility characters made plain, as Unicode's NFKC does for the characters
// a Japanese sentence holds: full-width letters, digits and signs, half-width
// katakana, circled numbers, Roman numerals, squared words (㍿, ㌔), … as "...",
// and a kana with a voicing mark after it as one character.
std::u32string plain_forms(const std::u32string &text);
// ヵ and ヶ stay, and a ー after a kana becomes that kana's vowel, with う for an o
// (スーパー → すうぱあ, コーヒー → こうひい).
std::u32string katakana_to_hiragana(const std::u32string &text);
std::u32string hiragana_to_katakana(const std::u32string &text);
// A run of っ, ッ or ー becomes one (すっっごい → すっごい), or with `full` goes
// (すっごい → すごい). At the start and the end of the text they stay.
std::u32string collapse_emphatic(const std::u32string &text, bool full);
// Latin letters and digits in full width, as some dictionaries write them (ＣＤ).
std::u32string full_width_alphanumeric(const std::u32string &text);
// 々 written out as the character before it (時々 → 時時).
std::u32string expand_iteration_marks(const std::u32string &text);
// The middle dots between the words of a name left out (ジョン・スミス → ジョンスミス).
std::u32string strip_middle_dots(const std::u32string &text);

}  // namespace ore::dict
