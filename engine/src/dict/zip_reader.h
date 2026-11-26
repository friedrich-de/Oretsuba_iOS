// Reads the files of a zip one at a time, each whole into memory: a Yomitan
// dictionary is a zip of JSON banks and pictures.
#pragma once

#include "dict/kit_config.h"
#if ORE_MINING_KIT

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace ore::dict {

class ZipReader {
public:
	struct Entry {
		std::string name;     // its path in the zip, in UTF-8 when the zip says it in UTF-8 anywhere
		uint64_t size = 0;    // unpacked
		uint64_t packed = 0;  // as stored
		uint64_t offset = 0;  // of its local header
		uint32_t crc = 0;
		uint16_t method = 0, flags = 0;
		bool is_folder() const { return !name.empty() && name.back() == '/'; }
	};

	// Null when the file is not a zip.
	static std::unique_ptr<ZipReader> open(const std::string &path);
	~ZipReader();
	ZipReader(const ZipReader &) = delete;
	ZipReader &operator=(const ZipReader &) = delete;

	const std::vector<Entry> &entries() const { return entries_; }
	// Reads a file whole, but never more of it than `limit`. False with the
	// reason in `problem`: "large" when it is over the limit, else "damaged".
	// Several threads may read at once.
	bool read(const Entry &entry, uint64_t limit, std::string &out, std::string &problem) const;
	// How many of the files read so far the zip states a checksum for that they do not have. They are read
	// all the same when they unpack whole.
	uint32_t wrong_checksums() const { return wrong_checksums_.load(std::memory_order_relaxed); }

private:
	ZipReader() = default;
	bool read_directory();
	FILE *file_ = nullptr;
	uint64_t file_size_ = 0;
	std::vector<Entry> entries_;
	mutable std::atomic<uint32_t> wrong_checksums_{0};
};

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
