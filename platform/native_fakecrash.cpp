#include <platform/native_fakecrash.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

extern "C" {
#include <platform/native_assets.h>
#include <platform/native_disc_image.h>
#include <platform/native_disc_source.h>
#include <platform/native_fs_utf8.h>
}

#undef nullptr

namespace
{
using Bytes = std::vector<u8>;
using File = std::unique_ptr<FILE, decltype(&fclose)>;
constexpr int menuIndex = 216;
constexpr int highModelIndex = 256;
constexpr int vramIndex = 258;
constexpr int vramWidth = 1024;
constexpr int vramHeight = 512;
constexpr size_t maxEntrySize = 8 * 1024 * 1024;
constexpr char japaneseBoot[] = "SCPS_101.18";

NativeFakeCrashStatus importResult = NATIVE_FAKECRASH_NO_DISC;

void Require(bool valid, const char *message)
{
	if (!valid)
		throw std::runtime_error(message);
}

u32 Word(const Bytes &bytes, size_t offset)
{
	Require(offset <= bytes.size() && bytes.size() - offset >= 4, "word outside asset");
	return CTR_ReadU32LE(bytes.data() + offset);
}

void PutWord(Bytes &bytes, size_t offset, u32 value)
{
	Require(offset <= bytes.size() && bytes.size() - offset >= 4, "pointer outside asset");
	CTR_WriteU32LE(bytes.data() + offset, value);
}

struct Bigfile
{
	// A loose NTSC-U BIGFILE takes precedence, as it does in the game loader.
	FILE *loose;
	FILE *source;
	NativeDiscImageFile file{};

	Bytes Read(u32 sector, size_t size) const
	{
		const size_t sectors = (size + 2047) / 2048;
		Require(size > 0 && size <= maxEntrySize && u64(sector) * 2048 + size <= file.size,
		        "BIGFILE entry outside archive");
		Bytes bytes(sectors * 2048);
		if (loose)
		{
			Require(fseek(loose, long(sector) * 2048, SEEK_SET) == 0 &&
			        fread(bytes.data(), 1, size, loose) == size, "could not read loose BIGFILE");
		}
		else if (source)
		{
			for (size_t i = 0; i < sectors; ++i)
				Require(NativeDiscSource_ReadSector(source, file.lba + sector + i, bytes.data() + i * 2048),
				        "could not read Japanese BIGFILE");
		}
		else
		{
			Require(NativeDiscImage_ReadDataSectors(&file, sector, sectors, bytes.data()),
			        "could not read mounted BIGFILE");
		}
		bytes.resize(size);
		return bytes;
	}

	Bytes Entry(const Bytes &table, int index) const
	{
		return Read(Word(table, 8 + index * 8), Word(table, 12 + index * 8));
	}

	Bytes Table() const
	{
		const u32 count = Word(Read(0, 8), 4);
		Require(count > u32(g_fakeCrashIndices[NATIVE_FAKECRASH_COUNT - 1]) &&
		        count <= (maxEntrySize - 8) / 8, "invalid BIGFILE table size");
		return Read(0, 8 + size_t(count) * 8);
	}
};

struct MappedAsset
{
	Bytes body;
	std::vector<u32> pointers;

	explicit MappedAsset(const Bytes &raw)
	{
		const size_t size = Word(raw, 0);
		Require(size <= raw.size() - 4 && raw.size() - 4 - size >= 4, "invalid pointer map");
		const size_t mapSize = Word(raw, 4 + size);
		Require(mapSize % 4 == 0 && mapSize == raw.size() - 8 - size, "invalid pointer map size");
		body.assign(raw.begin() + 4, raw.begin() + 4 + size);
		for (size_t pos = 8 + size; pos < raw.size(); pos += 4)
		{
			const u32 offset = Word(raw, pos);
			Word(body, offset);
			pointers.push_back(offset);
		}
		std::sort(pointers.begin(), pointers.end());
		Require(std::adjacent_find(pointers.begin(), pointers.end()) == pointers.end(),
		        "duplicate pointer map offset");
	}

	Bytes Join() const
	{
		Bytes raw(8 + body.size() + pointers.size() * 4);
		PutWord(raw, 0, body.size());
		std::copy(body.begin(), body.end(), raw.begin() + 4);
		PutWord(raw, 4 + body.size(), pointers.size() * 4);
		for (size_t i = 0; i < pointers.size(); ++i)
			PutWord(raw, 8 + body.size() + i * 4, pointers[i]);
		return raw;
	}
};

bool IsFake(const Bytes &body, size_t start)
{
	return start <= body.size() && body.size() - start >= 16 &&
	       memcmp(body.data() + start, "fake", 5) == 0;
}

struct ModelRegion
{
	size_t start;
	size_t end;
};

ModelRegion FindModel(const MappedAsset &asset, bool menu)
{
	const Bytes &body = asset.body;
	size_t first = 4;
	size_t end = Word(body, 0);
	if (menu)
	{
		auto it = std::find_if(asset.pointers.begin(), asset.pointers.end(),
		                       [&](u32 offset) { return IsFake(body, Word(body, offset)); });
		Require(it != asset.pointers.end(), "main menu has no Fake Crash model");
		while (it != asset.pointers.begin() && *it >= 4 && *(it - 1) == *it - 4 &&
		       Word(body, *(it - 1)) != 0)
			--it;
		first = *it;
		end = body.size();
	}

	std::vector<u32> models;
	for (size_t pos = first; u32 model = Word(body, pos); pos += 4)
	{
		Require(model < body.size(), "invalid driver model offset");
		models.push_back(model);
	}
	size_t start = body.size();
	for (u32 model : models)
	{
		if (IsFake(body, model))
		{
			Require(start == body.size(), "multiple Fake Crash models");
			start = model;
		}
	}
	Require(start < body.size(), "driver pack has no Fake Crash model");
	if (end <= start || end > body.size())
		end = body.size();
	for (u32 model : models)
		if (model > start)
			end = std::min(end, size_t(model));
	return {start, end};
}

void Splice(MappedAsset &us, const MappedAsset &jp, ModelRegion old, ModelRegion replacement)
{
	Bytes body;
	body.insert(body.end(), us.body.begin(), us.body.begin() + old.start);
	body.insert(body.end(), jp.body.begin() + replacement.start, jp.body.begin() + replacement.end);
	body.insert(body.end(), us.body.begin() + old.end, us.body.end());
	const size_t newEnd = old.start + (replacement.end - replacement.start);

	// Move the Japanese model's internal pointers, then shift pointers in
	// the retail assets that follow it. The model array still points to old.start.
	std::vector<u32> pointers;
	for (u32 offset : jp.pointers)
	{
		if (offset < replacement.start || offset >= replacement.end)
			continue;
		const u32 value = Word(jp.body, offset);
		Require(value >= replacement.start && value < replacement.end &&
		        replacement.end - offset >= 4, "Japanese model has an external pointer");
		const size_t dest = old.start + (offset - replacement.start);
		PutWord(body, dest, old.start + (value - replacement.start));
		pointers.push_back(dest);
	}
	for (u32 offset : us.pointers)
	{
		if (offset >= old.start && offset < old.end)
			continue;
		const u32 value = Word(us.body, offset);
		Require(value <= old.start || value >= old.end, "another model points inside Fake Crash");
		const size_t dest = offset >= old.end ? newEnd + (offset - old.end) : offset;
		PutWord(body, dest, value >= old.end ? newEnd + (value - old.end) : value);
		pointers.push_back(dest);
	}
	Require(pointers.size() == us.pointers.size(), "Fake Crash pointer counts differ");
	std::sort(pointers.begin(), pointers.end());
	us.body.swap(body);
	us.pointers.swap(pointers);
}

using TextureSlots = std::array<u32, 512>;

struct ModelTextures
{
	TextureSlots slots{};
	std::vector<u32> layouts;
};

ModelTextures FindTextures(const Bytes &body, size_t model)
{
	ModelTextures textures;
	const u32 count = Word(body, model + 0x10) >> 16;
	const size_t headers = Word(body, model + 0x14);
	Require(headers <= body.size() && count <= (body.size() - headers) / 0x40,
	        "invalid Fake Crash model headers");
	for (u32 h = 0; h < count; ++h)
	{
		const size_t header = headers + h * 0x40;
		const size_t commands = Word(body, header + 0x20);
		const size_t table = Word(body, header + 0x28);
		if (!commands || !table)
			continue;
		Require(commands < body.size() && table < body.size(), "invalid Fake Crash texture table");
		for (size_t pos = commands + 4; ; pos += 4)
		{
			const u32 command = Word(body, pos);
			if (command == 0xFFFFFFFF)
				break;
			const unsigned index = command & 0x1FF;
			if (!(command & 0xFFFF0000) || !index)
				continue;
			const u32 offset = Word(body, table + (index - 1) * 4);
			Require(offset && offset <= body.size() && body.size() - offset >= 12,
			        "invalid Fake Crash texture layout");
			textures.slots[index] = offset; // Menu retargeting takes the last layout for a slot.
			textures.layouts.push_back(offset);
		}
	}
	// Each header has its own texture table. VRAM sampling includes every
	// layout, even when two headers use the same slot for different layouts.
	std::sort(textures.layouts.begin(), textures.layouts.end());
	textures.layouts.erase(std::unique(textures.layouts.begin(), textures.layouts.end()), textures.layouts.end());
	return textures;
}

struct Layout
{
	std::array<int, 4> u, v;
	int depth, pageX, pageY, clutX, clutY;

	explicit Layout(const u8 *p)
		: u{p[0], p[4], p[8], p[10]}, v{p[1], p[5], p[9], p[11]},
		  depth((((p[7] << 8) | p[6]) >> 7) & 3), pageX((p[6] & 15) * 64),
		  pageY((p[6] & 16) * 16), clutX((p[2] & 63) * 16),
		  clutY(((p[3] << 8) | p[2]) >> 6 & 511)
	{
		Require(depth <= 2, "invalid Fake Crash texture depth");
	}
};

using VramMask = std::vector<bool>;

void MarkRect(VramMask &words, int x, int y, int w, int h)
{
	for (int yy = y; yy < std::min(y + h, vramHeight); ++yy)
		for (int xx = x; xx < std::min(x + w, vramWidth); ++xx)
			words[yy * vramWidth + xx] = true;
}

VramMask SampledWords(const Bytes &body, const ModelTextures &textures)
{
	VramMask words(vramWidth * vramHeight);
	for (u32 offset : textures.layouts)
	{
		const Layout layout(body.data() + offset);
		const auto u = std::minmax_element(layout.u.begin(), layout.u.end());
		const auto v = std::minmax_element(layout.v.begin(), layout.v.end());
		const int shift = 2 - layout.depth;
		const int left = *u.first >> shift;
		MarkRect(words, layout.pageX + left, layout.pageY + *v.first,
		         (*u.second >> shift) - left + 1, *v.second - *v.first + 1);
		if (layout.depth < 2)
			MarkRect(words, layout.clutX, layout.clutY, layout.depth ? 256 : 16, 1);
	}
	return words;
}

void RetargetMenuTextures(Bytes &body, const TextureSlots &slots,
                          const Bytes &source, const TextureSlots &sourceSlots)
{
	for (size_t i = 0; i < slots.size(); ++i)
	{
		Require(bool(slots[i]) == bool(sourceSlots[i]), "menu and race texture slots differ");
		if (!slots[i])
			continue;
		const Layout a(body.data() + slots[i]), b(source.data() + sourceSlots[i]);
		const int du = *std::min_element(a.u.begin(), a.u.end()) - *std::min_element(b.u.begin(), b.u.end());
		const int dv = *std::min_element(a.v.begin(), a.v.end()) - *std::min_element(b.v.begin(), b.v.end());
		Require(a.depth == b.depth, "menu and race texture depth differs");
		for (size_t corner = 0; corner < a.u.size(); ++corner)
			Require(a.u[corner] - b.u[corner] == du && a.v[corner] - b.v[corner] == dv,
			        "menu and race texture shapes differ");
		std::copy_n(source.begin() + sourceSlots[i], 12, body.begin() + slots[i]);
	}
}

template <typename Visit>
void VisitVram(const Bytes &raw, Visit visit)
{
	const bool packed = Word(raw, 0) == 0x20;
	size_t pos = packed ? 4 : 0;
	for (;;)
	{
		size_t size = raw.size();
		if (packed)
		{
			size = Word(raw, pos);
			pos += 4;
			if (!size)
				break;
			size &= ~size_t(3);
		}
		Require(size >= 20 && size <= raw.size() - pos, "invalid VRM block");
		const u32 position = Word(raw, pos + 12), dimensions = Word(raw, pos + 16);
		const int x = position & 0xFFFF, y = position >> 16;
		const int w = dimensions & 0xFFFF, h = dimensions >> 16;
		Require(x + w <= vramWidth && y + h <= vramHeight &&
		        size_t(w) * h <= (size - 20) / 2, "VRM block outside video memory");
		for (int row = 0; row < h; ++row)
			for (int col = 0; col < w; ++col)
				visit((y + row) * vramWidth + x + col, pos + 20 + 2 * (row * w + col));
		if (!packed)
			break;
		pos += size;
	}
}

void PatchVram(Bytes &us, const Bytes &jp, const VramMask &words)
{
	std::vector<size_t> jpOffsets(words.size());
	VisitVram(jp, [&](size_t word, size_t offset) {
		if (words[word])
			jpOffsets[word] = offset;
	});
	int changed = 0;
	VisitVram(us, [&](size_t word, size_t offset) {
		if (!words[word])
			return;
		const size_t from = jpOffsets[word];
		Require(from != 0, "missing Japanese Fake Crash VRAM word");
		if (us[offset] != jp[from] || us[offset + 1] != jp[from + 1])
			++changed;
		us[offset] = jp[from];
		us[offset + 1] = jp[from + 1];
	});
	constexpr int expectedChangedWords = 192;
	Require(changed == expectedChangedWords, "unexpected number of changed Fake Crash VRAM words");
}

using Pack = std::array<Bytes, NATIVE_FAKECRASH_COUNT>;

bool IsStandalone(int index)
{
	return index == highModelIndex || index == 322 || index == 346;
}

Pack BuildPack(const Bigfile &us, const Bigfile &jp)
{
	const Bytes usTable = us.Table(), jpTable = jp.Table();
	Pack pack;
	VramMask sampled(vramWidth * vramHeight);
	for (size_t i = 0; i < pack.size(); ++i)
	{
		const int index = g_fakeCrashIndices[i];
		if (!IsStandalone(index))
			continue;
		pack[i] = jp.Entry(jpTable, index);
		const Bytes retail = us.Entry(usTable, index);
		Require(pack[i].size() <= retail.size(), "replacement exceeds the retail buffer");
		const MappedAsset original(retail), replacement(pack[i]);
		Require(IsFake(original.body, 0) && IsFake(replacement.body, 0), "expected standalone Fake Crash");
		const auto words = SampledWords(replacement.body, FindTextures(replacement.body, 0));
		Require(words == SampledWords(original.body, FindTextures(original.body, 0)),
		        "standalone Fake Crash samples different VRAM");
		for (size_t word = 0; word < words.size(); ++word)
			sampled[word] = sampled[word] || words[word];
	}
	const auto highSlot = std::find(std::begin(g_fakeCrashIndices), std::end(g_fakeCrashIndices), highModelIndex)
	                      - std::begin(g_fakeCrashIndices);
	const MappedAsset high(pack[highSlot]);
	const auto highTextures = FindTextures(high.body, 0);

	for (size_t i = 0; i < pack.size(); ++i)
	{
		const int index = g_fakeCrashIndices[i];
		if (IsStandalone(index))
			continue;
		if (index == vramIndex)
		{
			pack[i] = us.Entry(usTable, index);
			PatchVram(pack[i], jp.Entry(jpTable, index), sampled);
			continue;
		}
		const bool menu = index == menuIndex;
		MappedAsset original(us.Entry(usTable, index));
		const MappedAsset replacement(jp.Entry(jpTable, index));
		const size_t originalSize = original.body.size();
		const auto old = FindModel(original, menu);
		Splice(original, replacement, old, FindModel(replacement, menu));
		Require(original.body.size() <= originalSize, "replacement exceeds the retail buffer");
		const auto textures = FindTextures(original.body, old.start);
		if (menu)
			RetargetMenuTextures(original.body, textures.slots, high.body, highTextures.slots);
		const auto words = SampledWords(original.body, textures);
		for (size_t word = 0; word < words.size(); ++word)
			Require(!words[word] || sampled[word], "Fake Crash model samples other VRAM");
		pack[i] = original.Join();
	}
	return pack;
}

void SavePack(const Pack &pack)
{
	char path[1024], staging[1024], lockPath[1024];
	Require(NativeAssets_BuildPath(NATIVE_FAKECRASH_PACK, path, sizeof path) &&
	        NativeAssets_BuildPath(NATIVE_FAKECRASH_PACK ".part", staging, sizeof staging) &&
	        NativeAssets_BuildPath(NATIVE_FAKECRASH_PACK ".lock", lockPath, sizeof lockPath),
	        "could not resolve Fake Crash pack path");
	const std::unique_ptr<NativeFsLock, decltype(&NativeFs_UnlockFile)>
		lock(NativeFs_TryLockFile(lockPath), NativeFs_UnlockFile);
	Require(bool(lock), "could not lock Fake Crash pack for import");
	NativeFs_Delete(staging);
	FILE *out = NativeFs_CreateExclusive(staging);
	Require(out != nullptr, "could not create temporary Fake Crash pack");

	// One atomic replacement publishes the models and VRM together.
	const char magic[] = NATIVE_FAKECRASH_MAGIC;
	bool ok = fwrite(magic, 1, sizeof magic - 1, out) == sizeof magic - 1;
	for (const auto &entry : pack)
	{
		const size_t size = entry.size();
		const u8 length[] = {u8(size), u8(size >> 8), u8(size >> 16), u8(size >> 24)};
		ok = ok && fwrite(length, 1, sizeof length, out) == sizeof length &&
		     fwrite(entry.data(), 1, size, out) == size;
	}
	ok = ok && NativeFs_FlushToDisk(out);
	ok = (fclose(out) == 0) && ok;
	ok = ok && NativeFs_Replace(staging, path);
	if (!ok)
		NativeFs_Delete(staging);
	Require(ok, "could not save Fake Crash pack");
	NativeFs_FlushDirectory(NativeAssets_GetAssetDir());
}
}

NativeFakeCrashStatus NativeFakeCrash_Status(void)
{
	if (importResult == NATIVE_FAKECRASH_RESTART || importResult == NATIVE_FAKECRASH_FAILED)
		return importResult;
	if (NativeAssets_BigfileOverride(highModelIndex))
		return NATIVE_FAKECRASH_INSTALLED;
	char path[1024];
	const File source(NativeDiscSource_Open(japaneseBoot, path, sizeof path), fclose);
	return source ? NATIVE_FAKECRASH_READY : NATIVE_FAKECRASH_NO_DISC;
}

NativeFakeCrashStatus NativeFakeCrash_Import(void)
{
	if (importResult == NATIVE_FAKECRASH_RESTART || NativeAssets_BigfileOverride(highModelIndex))
		return NativeFakeCrash_Status();
	char path[1024];
	const File source(NativeDiscSource_Open(japaneseBoot, path, sizeof path), fclose);
	if (!source)
		return importResult = NATIVE_FAKECRASH_NO_DISC;

	try
	{
		const File loose(NativeAssets_OpenHostBigfile("rb"), fclose);
		Bigfile us{loose.get(), nullptr};
		if (loose)
		{
			Require(fseek(loose.get(), 0, SEEK_END) == 0, "could not seek loose BIGFILE");
			const long size = ftell(loose.get());
			Require(size > 0 && u64(size) <= INT32_MAX, "invalid loose BIGFILE size");
			us.file.size = size;
		}
		else
			Require(NativeDiscImage_FindFile("BIGFILE.BIG", &us.file), "no NTSC-U BIGFILE");
		Bigfile jp{nullptr, source.get()};
		Require(NativeDiscSource_FindPath(source.get(), "BIGFILE.BIG", &jp.file.lba, &jp.file.size) &&
		        u64(jp.file.lba) + (u64(jp.file.size) + 2047) / 2048 <= UINT32_MAX,
		        "no valid Japanese BIGFILE");
		SavePack(BuildPack(us, jp));
		fprintf(stderr, "[CTR Native] Japanese Fake Crash imported from %s; restart to apply\n", path);
		return importResult = NATIVE_FAKECRASH_RESTART;
	}
	catch (const std::exception &error)
	{
		fprintf(stderr, "[CTR Native] Japanese Fake Crash import failed: %s\n", error.what());
		return importResult = NATIVE_FAKECRASH_FAILED;
	}
}
