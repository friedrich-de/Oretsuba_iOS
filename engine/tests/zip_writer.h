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

// A file of a test zip, with what an odd packer may do to it.
struct File {
	std::string name, data;
	// The name in the packer's own encoding (Shift-JIS on a Japanese Windows). When it is set the records carry
	// these bytes without the flag for UTF-8, and `name` goes beside them in Info-ZIP's "Unicode Path" field.
	std::string own_name;
	bool unicode_field = true;    // off: `own_name` alone, as a packer without the field writes it
	bool renamed = false;         // the field's checksum is of another name, as after a rename by a tool that kept it
	bool wrong_checksum = false;  // the zip states a checksum the data does not have
};

// Writes a zip: every other file deflated, the rest stored.
inline void write_files(const std::string &path, const std::vector<File> &files) {
	std::string out, directory;
	int n = 0;
	for (const File &file : files) {
		const std::string &data = file.data;
		const std::string &name = file.own_name.empty() ? file.name : file.own_name;
		uint32_t flags = file.own_name.empty() ? 0x800 : 0;
		std::string extra;
		if (!file.own_name.empty() && file.unicode_field) {
			const std::string &of = file.renamed ? file.name : file.own_name;
			put16(extra, 0x7075), put16(extra, uint32_t(5 + file.name.size()));
			extra += char(1);
			put32(extra, uint32_t(crc32(0, reinterpret_cast<const Bytef *>(of.data()), uInt(of.size()))));
			extra += file.name;
		}
		uint32_t crc = uint32_t(crc32(0, reinterpret_cast<const Bytef *>(data.data()), uInt(data.size())));
		if (file.wrong_checksum) crc ^= 0x5a5a5a5a;
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
		put16(out, 20), put16(out, flags), put16(out, uint32_t(method)), put16(out, 0), put16(out, 0);
		put32(out, crc), put32(out, uint32_t(packed.size())), put32(out, uint32_t(data.size()));
		put16(out, uint32_t(name.size())), put16(out, uint32_t(extra.size()));
		out += name;
		out += extra;
		out += packed;
		put32(directory, 0x02014b50);
		put16(directory, 20), put16(directory, 20), put16(directory, flags), put16(directory, uint32_t(method)), put16(directory, 0),
		    put16(directory, 0);
		put32(directory, crc), put32(directory, uint32_t(packed.size())), put32(directory, uint32_t(data.size()));
		put16(directory, uint32_t(name.size())), put16(directory, uint32_t(extra.size())), put16(directory, 0), put16(directory, 0),
		    put16(directory, 0);
		put32(directory, 0), put32(directory, offset);
		directory += name;
		directory += extra;
	}
	uint32_t at = uint32_t(out.size());
	out += directory;
	put32(out, 0x06054b50);
	put16(out, 0), put16(out, 0), put16(out, uint32_t(files.size())), put16(out, uint32_t(files.size()));
	put32(out, uint32_t(directory.size())), put32(out, at), put16(out, 0);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f.write(out.data(), std::streamsize(out.size()));
}

inline void write_zip(const std::string &path, const std::vector<std::pair<std::string, std::string>> &files) {
	std::vector<File> all;
	for (const auto &file : files) {
		File one;
		one.name = file.first, one.data = file.second;
		all.push_back(std::move(one));
	}
	write_files(path, all);
}

}  // namespace testing_zip
