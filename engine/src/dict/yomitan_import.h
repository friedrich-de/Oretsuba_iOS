// Reads a dictionary in Yomitan's format, a zip of index.json, term banks,
// term meta banks (frequencies, pitch accents, pronunciations), tag banks, a
// stylesheet and pictures, into a dictionary file. Kanji banks are left out:
// the popup looks kanji up as words.
#pragma once

#include "dict/kit_config.h"
#if ORE_MINING_KIT

#include <atomic>
#include <functional>
#include <string>

#include "dict/dictionary_db.h"

namespace ore::dict {

// A bank larger than this is refused.
inline constexpr uint64_t kMaxBankBytes = 256ull * 1024 * 1024;
// A picture larger than this is left out.
inline constexpr uint64_t kMaxMediaBytes = 16ull * 1024 * 1024;
// A stylesheet or index.json larger than this is refused.
inline constexpr uint64_t kMaxSmallFileBytes = 1024 * 1024;
// A dictionary that unpacks to more than this is refused.
inline constexpr uint64_t kMaxTotalBytes = 8ull * 1024 * 1024 * 1024;
// A word written or read longer than this is left out.
inline constexpr size_t kMaxKeyLength = 256;

// The import was stopped.
struct ImportStopped : std::runtime_error {
	ImportStopped() : std::runtime_error("The import was stopped.") {}
};

// The media type of a picture a definition may show, by its path; empty for any other file.
std::string media_type_of(const std::string &path);

// Imports the dictionary in `zip_path` into a new file at `database_path`,
// which gets the name `id`. `progress` is told how far it is, from 0 to 1, and
// `stop` ends it. On any failure the new file is removed and a DictError says
// why, in words for the player (ImportStopped when it was stopped).
DictionaryInfo import_yomitan(const std::string &zip_path, const std::string &database_path, const std::string &id,
                              const std::function<void(double)> &progress, const std::atomic<bool> &stop);

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
