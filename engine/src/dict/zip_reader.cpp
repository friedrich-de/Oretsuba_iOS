// Reads the files of a zip.
#include "dict/zip_reader.h"
#if ORE_MINING_KIT

#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace ore::dict {

namespace {

uint16_t u16(const uint8_t *p) {
	return uint16_t(p[0] | p[1] << 8);
}
uint32_t u32(const uint8_t *p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint64_t u64(const uint8_t *p) {
	return uint64_t(u32(p)) | uint64_t(u32(p + 4)) << 32;
}

// Reads at a place of the file without moving it, so that several threads may read at once.
bool read_at(FILE *f, uint64_t offset, void *out, size_t size) {
	uint8_t *to = static_cast<uint8_t *>(out);
	while (size > 0) {
		ssize_t got = pread(fileno(f), to, size, off_t(offset));
		if (got <= 0) return false;
		to += got;
		offset += uint64_t(got);
		size -= size_t(got);
	}
	return true;
}

// A zip's directory is never larger than this here: a dictionary has some thousand files.
const uint64_t kMaxDirectory = 64u * 1024 * 1024;

// The flag of a file whose name is written in UTF-8. Without it the name is in the packer's own encoding: a
// Japanese Windows writes Shift-JIS, in which a dictionary's 漢-default.svg is another run of bytes than the
// name its meanings ask for.
const uint16_t kNamesInUtf8 = 0x800;
// Info-ZIP's "Unicode Path" field, which such a packer adds: a version (1), the checksum of the name in the
// record, and the name in UTF-8. Yomitan takes the name from it, so dictionaries are made counting on it.
const uint16_t kUnicodePath = 0x7075;

}  // namespace

std::unique_ptr<ZipReader> ZipReader::open(const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return nullptr;
	std::unique_ptr<ZipReader> zip(new ZipReader());
	zip->file_ = f;
	if (fseeko(f, 0, SEEK_END) != 0) return nullptr;
	zip->file_size_ = uint64_t(ftello(f));
	if (!zip->read_directory()) return nullptr;
	return zip;
}

ZipReader::~ZipReader() {
	if (file_) fclose(file_);
}

bool ZipReader::read_directory() {
	// The end record is the last thing in the file, before a comment of at most 64 KB.
	if (file_size_ < 22) return false;
	size_t tail = size_t(std::min<uint64_t>(file_size_, 22 + 65535));
	std::vector<uint8_t> buffer(tail);
	if (!read_at(file_, file_size_ - tail, buffer.data(), tail)) return false;
	long at = -1;
	for (long i = long(tail) - 22; i >= 0; --i)
		if (u32(&buffer[size_t(i)]) == 0x06054b50) {
			at = i;
			break;
		}
	if (at < 0) return false;
	const uint8_t *end = &buffer[size_t(at)];
	uint64_t count = u16(end + 10), size = u32(end + 12), offset = u32(end + 16);
	uint64_t end_offset = file_size_ - tail + uint64_t(at);

	// A large zip says the same in a longer record before it.
	if ((count == 0xffff || size == 0xffffffff || offset == 0xffffffff) && end_offset >= 20) {
		uint8_t locator[20];
		if (read_at(file_, end_offset - 20, locator, 20) && u32(locator) == 0x07064b50) {
			uint8_t record[56];
			uint64_t where = u64(locator + 8);
			if (where > file_size_ || !read_at(file_, where, record, 56) || u32(record) != 0x06064b50) return false;
			count = u64(record + 32);
			size = u64(record + 40);
			offset = u64(record + 48);
		}
	}
	if (size > kMaxDirectory || offset > file_size_ || size > file_size_ - offset) return false;

	std::vector<uint8_t> directory((size_t(size)));
	if (size && !read_at(file_, offset, directory.data(), directory.size())) return false;
	const uint8_t *p = directory.data(), *stop = p + directory.size();
	for (uint64_t i = 0; i < count; ++i) {
		if (stop - p < 46 || u32(p) != 0x02014b50) return false;
		Entry entry;
		entry.flags = u16(p + 8);
		entry.method = u16(p + 10);
		entry.crc = u32(p + 16);
		entry.packed = u32(p + 20);
		entry.size = u32(p + 24);
		size_t name = u16(p + 28), extra = u16(p + 30), comment = u16(p + 32);
		entry.offset = u32(p + 42);
		if (size_t(stop - p) < 46 + name + extra + comment) return false;
		entry.name.assign(reinterpret_cast<const char *>(p + 46), name);
		// The sizes too large for the record are in an extra field, and so is the name in UTF-8 when the packer
		// wrote the name itself in its own encoding.
		std::string unicode_name;
		const uint8_t *x = p + 46 + name, *xend = x + extra;
		while (xend - x >= 4) {
			uint16_t id = u16(x), length = u16(x + 2);
			x += 4;
			if (xend - x < length) break;
			if (id == 1) {
				const uint8_t *v = x, *vend = x + length;
				if (entry.size == 0xffffffff && vend - v >= 8) entry.size = u64(v), v += 8;
				if (entry.packed == 0xffffffff && vend - v >= 8) entry.packed = u64(v), v += 8;
				if (entry.offset == 0xffffffff && vend - v >= 8) entry.offset = u64(v), v += 8;
			} else if (id == kUnicodePath && length > 5 && x[0] == 1) {
				// It holds for the name it was written beside, which its checksum tells: a tool that renamed the
				// file and kept the field leaves one that no longer fits.
				if (u32(x + 1) == uint32_t(crc32(0, p + 46, uInt(name))))
					unicode_name.assign(reinterpret_cast<const char *>(x + 5), size_t(length) - 5);
			}
			x += length;
		}
		if (!(entry.flags & kNamesInUtf8) && !unicode_name.empty()) entry.name = std::move(unicode_name);
		p += 46 + name + extra + comment;
		entries_.push_back(std::move(entry));
	}
	return true;
}

bool ZipReader::read(const Entry &entry, uint64_t limit, std::string &out, std::string &problem) const {
	out.clear();
	if (entry.size > limit) {
		problem = "large";
		return false;
	}
	problem = "damaged";
	if ((entry.flags & 1) || (entry.method != 0 && entry.method != 8)) return false;
	uint8_t header[30];
	if (entry.offset > file_size_ || !read_at(file_, entry.offset, header, 30) || u32(header) != 0x04034b50) return false;
	uint64_t data = entry.offset + 30 + u16(header + 26) + u16(header + 28);
	if (data > file_size_ || entry.packed > file_size_ - data) return false;

	if (entry.method == 0) {
		if (entry.packed != entry.size) return false;
		out.resize(size_t(entry.size));
		if (entry.size && !read_at(file_, data, out.data(), out.size())) return false;
	} else {
		z_stream z;
		memset(&z, 0, sizeof z);
		if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
		out.resize(size_t(entry.size));
		std::vector<uint8_t> chunk(256 * 1024);
		uint64_t left = entry.packed;
		size_t written = 0;
		int result = Z_OK;
		while (result != Z_STREAM_END) {
			if (z.avail_in == 0) {
				size_t want = size_t(std::min<uint64_t>(left, chunk.size()));
				if (want == 0 || !read_at(file_, data + (entry.packed - left), chunk.data(), want)) break;
				left -= want;
				z.next_in = chunk.data();
				z.avail_in = uInt(want);
			}
			// One byte more than the size it says, to notice a file that is longer.
			uint8_t over;
			bool full = written == out.size();
			z.next_out = full ? &over : reinterpret_cast<Bytef *>(out.data() + written);
			z.avail_out = full ? 1 : uInt(std::min<size_t>(out.size() - written, 1u << 30));
			uInt before = z.avail_out;
			result = inflate(&z, Z_NO_FLUSH);
			if (result != Z_OK && result != Z_STREAM_END) break;
			if (full && z.avail_out != before) {
				result = Z_DATA_ERROR;
				break;
			}
			written += before - z.avail_out;
		}
		inflateEnd(&z);
		if (result != Z_STREAM_END || written != out.size()) {
			out.clear();
			return false;
		}
	}
	// A checksum that does not fit refuses nothing. Some dictionaries are packed with a wrong one beside files
	// that are whole: they unpack to their end and to the size stated, which is what was asked of them above.
	// Yomitan, which the dictionaries are made for, does not look at the checksum either.
	uint32_t crc = 0;
	for (size_t at = 0; at < out.size();) {
		size_t part = std::min<size_t>(out.size() - at, 1u << 30);
		crc = uint32_t(crc32(crc, reinterpret_cast<const Bytef *>(out.data() + at), uInt(part)));
		at += part;
	}
	if (crc != entry.crc) wrong_checksums_.fetch_add(1, std::memory_order_relaxed);
	problem.clear();
	return true;
}

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
