#include "romset.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <zlib.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace romset {

namespace {

// ---- a minimal zip reader: the central directory, stored and deflated entries (as VF2's) ----

struct entry { std::string name; uint32_t crc = 0, csize = 0, usize = 0, local = 0; uint16_t method = 0; };

uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }

struct zip
{
	std::vector<uint8_t> data;
	std::vector<entry> entries;

	bool open(const std::string &path)
	{
		// vf3.zip is about 112 MB: anything over 512 MB isn't the set, and isn't read
		std::error_code ec;
		const uintmax_t size = std::filesystem::file_size(path, ec);
		if (ec || size < 22 || size > (512u << 20)) return false;
		FILE *f = fopen(path.c_str(), "rb");
		if (!f) return false;
		const long n = long(size);
		data.resize(size_t(n));
		const bool got = fread(data.data(), 1, data.size(), f) == data.size();
		fclose(f);
		if (!got) return false;
		long eocd = -1;
		for (long i = n - 22; i >= 0 && i >= n - 22 - 65535; i--)
			if (le32(&data[i]) == 0x06054b50) { eocd = i; break; }
		if (eocd < 0) return false;
		const uint16_t count = le16(&data[eocd + 10]);
		size_t at = le32(&data[eocd + 16]);
		for (int k = 0; k < count; k++) {
			if (at + 46 > data.size() || le32(&data[at]) != 0x02014b50) return false;
			entry e;
			e.method = le16(&data[at + 10]);
			e.crc = le32(&data[at + 16]);
			e.csize = le32(&data[at + 20]);
			e.usize = le32(&data[at + 24]);
			const uint16_t nlen = le16(&data[at + 28]), xlen = le16(&data[at + 30]), clen = le16(&data[at + 32]);
			e.local = le32(&data[at + 42]);
			if (at + 46 + nlen > data.size()) return false;
			e.name.assign(reinterpret_cast<const char *>(&data[at + 46]), nlen);
			const size_t slash = e.name.find_last_of('/');
			if (slash != std::string::npos) e.name = e.name.substr(slash + 1);
			entries.push_back(e);
			at += 46 + nlen + xlen + clen;
		}
		return true;
	}

	bool read(const entry &e, std::vector<uint8_t> &out) const
	{
		const size_t local = e.local;
		if (local + 30 > data.size() || le32(&data[local]) != 0x04034b50) return false;
		const size_t start = local + 30 + le16(&data[local + 26]) + le16(&data[local + 28]);
		if (start + size_t(e.csize) > data.size() || e.usize > (64u << 20)) return false;
		out.resize(e.usize);
		if (e.method == 0) {
			if (e.csize != e.usize) return false;
			memcpy(out.data(), &data[start], e.usize);
		} else if (e.method == 8) {
			z_stream z{};
			if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
			z.next_in = const_cast<Bytef *>(&data[start]);
			z.avail_in = e.csize;
			z.next_out = out.data();
			z.avail_out = e.usize;
			const int r = inflate(&z, Z_FINISH);
			inflateEnd(&z);
			if (r != Z_STREAM_END || z.total_out != e.usize) return false;
		} else {
			return false;
		}
		return uint32_t(crc32(0, out.data(), uInt(out.size()))) == e.crc;
	}
};

// ---- the set: each ROM goes into its region in 2-byte chunks every `stride` bytes, byte-swapped or not ----

enum region_id { CROM, BANKED, VROM, SOUND, SAMPLES, N_REGIONS };
const size_t REGION_SIZE[N_REGIONS] = { 0x200000, 0x4000000, 0x2000000, 0x80000, 0x800000 };

struct rom { const char *name; uint32_t crc; region_id region; uint32_t offset; uint32_t stride; uint32_t chunk; bool swap; };
const rom ROMS[] = {
	{ "epr-19230d.20", 0x43C08240, CROM, 0, 8, 2, true },
	{ "epr-19229d.19", 0x6773F715, CROM, 2, 8, 2, true },
	{ "epr-19228d.18", 0xA2470C78, CROM, 4, 8, 2, true },
	{ "epr-19227d.17", 0x8B650966, CROM, 6, 8, 2, true },
	{ "mpr-19196.4", 0xF386B850, BANKED, 0x0000000, 8, 2, true },
	{ "mpr-19195.3", 0xBD5E27A3, BANKED, 0x0000002, 8, 2, true },
	{ "mpr-19194.2", 0x66254702, BANKED, 0x0000004, 8, 2, true },
	{ "mpr-19193.1", 0x7BAB33D2, BANKED, 0x0000006, 8, 2, true },
	{ "mpr-19200.8", 0x74941091, BANKED, 0x1000000, 8, 2, true },
	{ "mpr-19199.7", 0x9F80D6FE, BANKED, 0x1000002, 8, 2, true },
	{ "mpr-19198.6", 0xD8EE5032, BANKED, 0x1000004, 8, 2, true },
	{ "mpr-19197.5", 0xA22D76C9, BANKED, 0x1000006, 8, 2, true },
	{ "mpr-19204.12", 0x2F93310A, BANKED, 0x2000000, 8, 2, true },
	{ "mpr-19203.11", 0x0AFA6334, BANKED, 0x2000002, 8, 2, true },
	{ "mpr-19202.10", 0xAAA086C6, BANKED, 0x2000004, 8, 2, true },
	{ "mpr-19201.9", 0x7C4A8C31, BANKED, 0x2000006, 8, 2, true },
	{ "mpr-19208.16", 0x08F30F71, BANKED, 0x3000000, 8, 2, true },
	{ "mpr-19207.15", 0x2CE1612D, BANKED, 0x3000002, 8, 2, true },
	{ "mpr-19206.14", 0x71A98D73, BANKED, 0x3000004, 8, 2, true },
	{ "mpr-19205.13", 0x199C328E, BANKED, 0x3000006, 8, 2, true },
	{ "mpr-19211.26", 0x9C8F5DF1, VROM, 0, 32, 2, false },
	{ "mpr-19212.27", 0x75036234, VROM, 2, 32, 2, false },
	{ "mpr-19213.28", 0x67B123CF, VROM, 4, 32, 2, false },
	{ "mpr-19214.29", 0xA6F5576B, VROM, 6, 32, 2, false },
	{ "mpr-19215.30", 0xC6FD9F0D, VROM, 8, 32, 2, false },
	{ "mpr-19216.31", 0x201BB1ED, VROM, 10, 32, 2, false },
	{ "mpr-19217.32", 0x4DADD41A, VROM, 12, 32, 2, false },
	{ "mpr-19218.33", 0xCFF91953, VROM, 14, 32, 2, false },
	{ "mpr-19219.34", 0xC610D521, VROM, 16, 32, 2, false },
	{ "mpr-19220.35", 0xE62924D0, VROM, 18, 32, 2, false },
	{ "mpr-19221.36", 0x24F83E3C, VROM, 20, 32, 2, false },
	{ "mpr-19222.37", 0x61A6AA7D, VROM, 22, 32, 2, false },
	{ "mpr-19223.38", 0x1A8C1980, VROM, 24, 32, 2, false },
	{ "mpr-19224.39", 0x0A79A1BD, VROM, 26, 32, 2, false },
	{ "mpr-19225.40", 0x91A985EB, VROM, 28, 32, 2, false },
	{ "mpr-19226.41", 0x00091722, VROM, 30, 32, 2, false },
	{ "epr-19231.21", 0xB416FE96, SOUND, 0, 2, 2, false },
	{ "mpr-19209.22", 0x3715E38C, SAMPLES, 0x000000, 2, 2, false },
	{ "mpr-19210.24", 0xC03D6502, SAMPLES, 0x400000, 2, 2, false },
};

void place(std::vector<uint8_t> &r, const rom &ro, const std::vector<uint8_t> &d)
{
	for (size_t i = 0, at = ro.offset; i + ro.chunk <= d.size() && at + ro.chunk <= r.size(); i += ro.chunk, at += ro.stride)
		for (uint32_t j = 0; j < ro.chunk; j++)
			r[at + j] = d[i + (ro.swap ? ro.chunk - 1 - j : j)];
}

void to_host_words(std::vector<uint8_t> &v)
{
	for (size_t i = 0; i + 3 < v.size(); i += 4) {
		uint32_t w = uint32_t(v[i]) << 24 | uint32_t(v[i + 1]) << 16 | uint32_t(v[i + 2]) << 8 | v[i + 3];
		memcpy(&v[i], &w, 4);
	}
}

} // namespace

result load(const std::string &path)
{
	result res;
	zip z;
	if (!z.open(path)) { res.error = "Couldn't read " + path + " as a zip file."; return res; }
	std::vector<uint8_t> region[N_REGIONS];
	for (int r = 0; r < N_REGIONS; r++) region[r].assign(REGION_SIZE[r], 0);
	int bad = 0;
	std::vector<uint8_t> d;
	for (const rom &ro : ROMS) {
		const entry *e = nullptr;
		for (const entry &x : z.entries) if (x.crc == ro.crc && x.name == ro.name) { e = &x; break; }
		if (!e) for (const entry &x : z.entries) if (x.crc == ro.crc) { e = &x; break; }
		if (!e || !z.read(*e, d)) {
			if (!bad++) res.error = "This isn't a complete Virtua Fighter 3 (Revision D) set, \"vf3\". Not usable:";
			res.error += std::string(" ") + ro.name;
			continue;
		}
		place(region[ro.region], ro, d);
	}
	if (bad) return res;

	// The fixed CROM: the program ROMs in its top 2 MB, the start of banked CROM below them
	res.crom.assign(0x800000, 0);
	memcpy(&res.crom[0x800000 - REGION_SIZE[CROM]], region[CROM].data(), REGION_SIZE[CROM]);
	memcpy(&res.crom[0], region[BANKED].data(), 0x800000 - REGION_SIZE[CROM]);
	res.banked.resize(0x8000000);
	memcpy(&res.banked[0], region[BANKED].data(), REGION_SIZE[BANKED]);
	memcpy(&res.banked[REGION_SIZE[BANKED]], region[BANKED].data(), REGION_SIZE[BANKED]);
	to_host_words(res.crom);
	to_host_words(res.banked);
	res.vrom.resize(0x4000000);
	memcpy(&res.vrom[0], region[VROM].data(), REGION_SIZE[VROM]);
	memcpy(&res.vrom[REGION_SIZE[VROM]], region[VROM].data(), REGION_SIZE[VROM]);
	res.sound = std::move(region[SOUND]);
	res.samples.resize(0x1000000);
	memcpy(&res.samples[0], region[SAMPLES].data(), REGION_SIZE[SAMPLES]);
	memcpy(&res.samples[REGION_SIZE[SAMPLES]], region[SAMPLES].data(), REGION_SIZE[SAMPLES]);
	res.ok = true;
	return res;
}

result import_file(const std::string &source, const std::string &destination)
{
	namespace fs = std::filesystem;
	const auto failure = [](const std::string &message) { result r; r.error = message; return r; };
	constexpr uintmax_t max_size = 512u << 20;
	std::error_code ec;
	const auto size = fs::file_size(source, ec);
	if (ec || size < 22 || size > max_size) return failure("Choose a ZIP file between 22 bytes and 512 MB.");
	const fs::path target(destination);
	const fs::path parent = target.has_parent_path() ? target.parent_path() : fs::path(".");
	fs::create_directories(parent, ec);
	if (ec) return failure("Couldn't create the ROM folder: " + ec.message());
	static std::atomic<unsigned> serial{0};
	fs::path staging;
	for (int attempt = 0; attempt < 10; ++attempt) {
		staging = parent / (".vf3-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
			+ "-" + std::to_string(serial.fetch_add(1)));
		if (fs::create_directory(staging, ec)) break;
		if (ec) return failure("Couldn't prepare the import: " + ec.message());
		staging.clear();
	}
	if (staging.empty()) return failure("Couldn't prepare a temporary ROM file.");
	struct cleanup {
		fs::path path;
		~cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
	} clean{staging};
	const auto temporary = staging / "vf3.zip";
	std::ifstream input(source, std::ios::binary);
	std::unique_ptr<FILE, decltype(&fclose)> output(fopen(temporary.string().c_str(), "wb"), fclose);
	if (!input || !output) return failure("Couldn't open the ROM file for copying.");
	char buffer[64 * 1024];
	uintmax_t copied = 0;
	while (input) {
		input.read(buffer, sizeof buffer);
		const auto count = input.gcount();
		copied += static_cast<uintmax_t>(count);
		if (copied > max_size) return failure("The ZIP exceeds 512 MB.");
		if (fwrite(buffer, 1, size_t(count), output.get()) != size_t(count))
			return failure("Couldn't copy the ROM file. Check free storage.");
	}
	if (!input.eof() || input.bad() || copied != size)
		return failure("The ROM file couldn't be copied completely. Try again after the file finishes downloading.");
	bool synced = fflush(output.get()) == 0;
#ifdef _WIN32
	if (synced) synced = _commit(_fileno(output.get())) == 0;
#else
	if (synced) synced = fsync(fileno(output.get())) == 0;
#endif
	const bool closed = fclose(output.release()) == 0;
	if (!synced || !closed) return failure("Couldn't save the ROM file to disk. Check free storage.");
	result loaded = load(temporary.string());
	if (!loaded.ok) return loaded;
	// Same filesystem: readers see the old complete ZIP or the new complete ZIP.
	// On a platform where replacing an existing path is unsupported, fail safely.
	fs::rename(temporary, target, ec);
	if (ec) return failure("Couldn't save the ROM file: " + ec.message());
	return loaded;
}

} // namespace romset
