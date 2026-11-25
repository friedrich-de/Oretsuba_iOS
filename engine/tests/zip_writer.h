// Writes a zip for the tests of the dictionary and of the mining kit.
#pragma once

#include <zlib.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace testing_zip {

inline void put16(std::string &out, uint32_t v) {
	out += char(v);
	out += char(v >> 8);
}
inline void put32(std::string &out, uint32_t v) {
	put16(out, v);
	put16(out, v >> 16);
}

// Writes a zip: every other file deflated, the rest stored.
inline void write_zip(const std::string &path, const std::vector<std::pair<std::string, std::string>> &files) {
	std::string out, directory;
	int n = 0;
	for (const auto &file : files) {
		const std::string &name = file.first, &data = file.second;
		uint32_t crc = uint32_t(crc32(0, reinterpret_cast<const Bytef *>(data.data()), uInt(data.size())));
		std::string packed = data;
		int method = 0;
		if (n++ % 2 == 0 && !data.empty()) {
			z_stream z{};
			deflateInit2(&z, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
			packed.assign(deflateBound(&z, uLong(data.size())), '\0');
			z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
			z.avail_in = uInt(data.size());
			z.next_out = reinterpret_cast<Bytef *>(packed.data());
			z.avail_out = uInt(packed.size());
			deflate(&z, Z_FINISH);
			packed.resize(z.total_out);
			deflateEnd(&z);
			method = 8;
		}
		uint32_t offset = uint32_t(out.size());
		put32(out, 0x04034b50);
		put16(out, 20), put16(out, 0x800), put16(out, uint32_t(method)), put16(out, 0), put16(out, 0);
		put32(out, crc), put32(out, uint32_t(packed.size())), put32(out, uint32_t(data.size()));
		put16(out, uint32_t(name.size())), put16(out, 0);
		out += name;
		out += packed;
		put32(directory, 0x02014b50);
		put16(directory, 20), put16(directory, 20), put16(directory, 0x800), put16(directory, uint32_t(method)), put16(directory, 0),
		    put16(directory, 0);
		put32(directory, crc), put32(directory, uint32_t(packed.size())), put32(directory, uint32_t(data.size()));
		put16(directory, uint32_t(name.size())), put16(directory, 0), put16(directory, 0), put16(directory, 0), put16(directory, 0);
		put32(directory, 0), put32(directory, offset);
		directory += name;
	}
	uint32_t at = uint32_t(out.size());
	out += directory;
	put32(out, 0x06054b50);
	put16(out, 0), put16(out, 0), put16(out, uint32_t(files.size())), put16(out, uint32_t(files.size()));
	put32(out, uint32_t(directory.size())), put32(out, at), put16(out, 0);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f.write(out.data(), std::streamsize(out.size()));
}

}  // namespace testing_zip
