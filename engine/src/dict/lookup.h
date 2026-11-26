// Looks up the words a Japanese text starts with, as Yomitan does: every
// start of the text from the longest, each in its other spellings, each with
// its inflections undone, is looked up as written and as read; an entry is
// kept when its part of speech fits what undoing the inflections asked for.
// The longest match comes first, then the one with fewer changes of spelling
// and fewer inflections, then the more frequent and the dictionary's order.
#pragma once

#include "dict/kit_config.h"
#if ORE_MINING_KIT

#include <memory>
#include <string>
#include <vector>

#include "dict/deinflector.h"
#include "dict/dictionary_db.h"
#include "util/json.h"

namespace ore::dict {

// Only this many characters of the text are looked at, as Yomitan's default scan length.
inline constexpr int kScanLength = 16;
// At most this many words are given.
inline constexpr int kMaxEntries = 20;

// Looks `text` up in `dictionaries`, which are in the order the player gave
// them. The answer is in the shape the dictionary popup draws (Hoshidicts'
// results): {"results": [{"matched", "deinflected", "trace": [{"name",
// "description"}], "term": {"expression", "reading", "rules", "glossaries":
// [{"dictionary", "glossary", "definitionTags", "termTags", "tags"}],
// "frequencies": [{"dictionary", "frequencies": [{"value", "displayValue"}]}],
// "pitches": [{"dictionary", "pitches": [{"position", "pattern", "nasal",
// "devoice"}], "transcriptions"}]}}]}.
json::Value look_up(const std::string &text, const std::vector<std::shared_ptr<DictionaryReader>> &dictionaries,
                    const Deinflector &rules);

// A frequency as Yomitan's term meta banks write it: a number, a text,
// {value, displayValue}, or any of these under "frequency" beside the
// "reading" it is for. One for another reading gives nothing (false).
struct Frequency {
	double value = 0;
	std::string display;
	bool has_display = false;
};
bool read_frequency(const std::string &json, const std::string &reading, Frequency &out);

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
