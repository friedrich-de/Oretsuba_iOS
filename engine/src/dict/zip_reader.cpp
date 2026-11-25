// Reads the files of a zip.
#include "dict/zip_reader.h"
#if ORE_MINING_KIT

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

bool read_at(FILE *f, uint64_t offset, void *out, size_t size) {
	return fseeko(f, off_t(offset), SEEK_SET) == 0 && fread(out, 1, size, f) == size;
}

// A zip's directory is never larger than this here: a dictionary has some thousand files.
const uint64_t kMaxDirectory = 64u * 1024 * 1024;

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
		// The sizes too large for the record are in an extra field.
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
			}
			x += length;
		}
		p += 46 + name + extra + comment;
		entries_.push_back(std::move(entry));
	}
	return true;
}

bool ZipReader::read(const Entry &entry, uint64_t limit, std::string &out, std::string &problem) {
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
	if (fseeko(file_, off_t(data), SEEK_SET) != 0) return false;

	if (entry.method == 0) {
		if (entry.packed != entry.size) return false;
		out.resize(size_t(entry.size));
		if (entry.size && fread(out.data(), 1, out.size(), file_) != out.size()) return false;
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
				if (want == 0 || fread(chunk.data(), 1, want, file_) != want) break;
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
	if (uint32_t(crc32(0, reinterpret_cast<const Bytef *>(out.data()), uInt(out.size()))) != entry.crc) {
		out.clear();
		return false;
	}
	problem.clear();
	return true;
}

}  // namespace ore::dict

#endif  // ORE_MINING_KIT
