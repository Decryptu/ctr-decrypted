#include <platform/native_racer_library.h>
#include <platform/native_sha256.h>
extern "C" {
#include <platform/native_fs_utf8.h>
}
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// The installed custom racers (include/platform/native_racer_library.h,
// docs/CUSTOM_RACERS.md). Built into the custom_package library with the
// track packages, so racers come with the Mods screen.

using json = nlohmann::json;

namespace
{
const char *const kTemplates[16] = {"crash", "cortex", "tiny", "coco", "ngin", "dingodile", "polar", "pura",
                                    "pinstripe", "papu", "roo", "komodo", "tropy", "penta", "fakecrash", "oxide"};
const char *const kEngines[4] = {"balanced", "accel", "speed", "turn"};

void Fail(char *error, size_t errorSize, const std::string &text)
{
    if (error && errorSize) std::snprintf(error, errorSize, "%s", text.c_str());
}

int IndexOf(const char *const *table, int count, const std::string &value)
{
    for (int i = 0; i < count; i++)
        if (value == table[i]) return i;
    return -1;
}

// Names are drawn with the game's font: printable ASCII only.
bool ValidName(const std::string &name, size_t capacity)
{
    if (name.empty() || name.size() >= capacity || name.front() == ' ' || name.back() == ' ') return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return c >= 0x20 && c < 0x7f; });
}

// A file name inside the racer's folder: no separators, no "..".
bool ValidFileName(const std::string &path)
{
    return !path.empty() && path.size() < 64 && path.find_first_of("/\\:") == std::string::npos && path != "." && path != "..";
}

bool HashFile(const std::filesystem::path &path, size_t expectedBytes, const char *expectedSha256)
{
    FILE *f = NativeFs_OpenRead(path.u8string().c_str());
    if (!f) return false;
    NativeSha256Ctx ctx;
    NativeSha256_Init(&ctx);
    unsigned char buffer[16384];
    size_t total = 0, n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0)
    {
        NativeSha256_Update(&ctx, buffer, n);
        total += n;
    }
    const bool readError = std::ferror(f) != 0;
    std::fclose(f);
    unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
    char hex[NATIVE_SHA256_HEX_BYTES];
    NativeSha256_Final(&ctx, digest);
    NativeSha256_ToHex(digest, hex);
    return !readError && total == expectedBytes && NativeSha256_HexEquals(expectedSha256, hex);
}

bool HashBytes(const void *bytes, size_t size, size_t expectedBytes, const char *expectedSha256)
{
    NativeSha256Ctx ctx;
    NativeSha256_Init(&ctx);
    NativeSha256_Update(&ctx, bytes, size);
    unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
    char hex[NATIVE_SHA256_HEX_BYTES];
    NativeSha256_Final(&ctx, digest);
    NativeSha256_ToHex(digest, hex);
    return size == expectedBytes && NativeSha256_HexEquals(expectedSha256, hex);
}

// An install folder name: a plain name that is not hidden (staging folders are).
bool ValidFolder(const char *folder)
{
    return folder && ValidFileName(folder) && folder[0] != '.';
}

bool WriteFile(const std::filesystem::path &path, const void *bytes, size_t size)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char *>(bytes), static_cast<std::streamsize>(size));
    out.close();
    return !out.fail();
}

// The racer id in a folder's racer.json, or "" when there is none.
std::string FolderRacerId(const std::filesystem::path &folder)
{
    FILE *f = NativeFs_OpenRead((folder / "racer.json").u8string().c_str());
    if (!f) return "";
    std::string text;
    char buffer[4096];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0 && text.size() < 65536) text.append(buffer, n);
    std::fclose(f);
    try
    {
        return json::parse(text).value("id", std::string());
    }
    catch (const std::exception &)
    {
        return "";
    }
}
} // namespace

extern "C" int NativeRacerLibrary_ParseManifest(const char *text, size_t size, NativeRacerEntry *out, char *modelSha256,
                                                char *portraitSha256, size_t *modelBytes, size_t *portraitBytes,
                                                char *error, size_t errorSize)
{
    try
    {
        const json m = json::parse(text, text + size);
        if (m.at("schema_version").get<int>() != 1) return Fail(error, errorSize, "unknown schema_version"), 0;

        NativeRacerEntry e{};
        const std::string id = m.at("id").get<std::string>();
        if (id.size() != 36) return Fail(error, errorSize, "id is not a UUID"), 0;
        std::memcpy(e.id, id.c_str(), 37);

        const std::string name = m.at("name").get<std::string>();
        const std::string shortName = m.at("short_name").get<std::string>();
        if (!ValidName(name, sizeof e.name) || !ValidName(shortName, sizeof e.shortName))
            return Fail(error, errorSize, "name or short_name is empty, too long or not printable ASCII"), 0;
        std::snprintf(e.name, sizeof e.name, "%s", name.c_str());
        std::snprintf(e.shortName, sizeof e.shortName, "%s", shortName.c_str());
        std::snprintf(e.version, sizeof e.version, "%s", m.value("version", std::string("1")).c_str());

        std::string authors;
        for (const auto &a : m.value("authors", json::array()))
            authors += (authors.empty() ? "" : ", ") + a.get<std::string>();
        std::snprintf(e.authors, sizeof e.authors, "%s", authors.c_str());

        e.templateID = IndexOf(kTemplates, 16, m.at("template").get<std::string>());
        if (e.templateID < 0) return Fail(error, errorSize, "unknown template"), 0;
        e.engine = IndexOf(kEngines, 4, m.at("engine").get<std::string>());
        if (e.engine < 0) return Fail(error, errorSize, "unknown engine"), 0;
        e.wheels = m.value("wheels", true) ? 1 : 0;

        const json &files = m.at("files");
        const json &model = files.at("model");
        const json &portrait = files.at("portrait");
        const std::string modelPath = model.at("path").get<std::string>();
        const std::string portraitPath = portrait.at("path").get<std::string>();
        if (!ValidFileName(modelPath) || !ValidFileName(portraitPath)) return Fail(error, errorSize, "bad file path"), 0;
        std::snprintf(e.modelPath, sizeof e.modelPath, "%s", modelPath.c_str());
        std::snprintf(e.portraitPath, sizeof e.portraitPath, "%s", portraitPath.c_str());

        *modelBytes = model.at("bytes").get<size_t>();
        *portraitBytes = portrait.at("bytes").get<size_t>();
        if (*modelBytes < 0x40 || *modelBytes > NATIVE_RACER_MODEL_MAX_BYTES) return Fail(error, errorSize, "model size out of range"), 0;
        if (*portraitBytes != NATIVE_RACER_PORTRAIT_BYTES) return Fail(error, errorSize, "portrait is not 44x26"), 0;
        const std::string ms = model.at("sha256").get<std::string>(), ps = portrait.at("sha256").get<std::string>();
        if (ms.size() != 64 || ps.size() != 64) return Fail(error, errorSize, "bad sha256"), 0;
        std::memcpy(modelSha256, ms.c_str(), 65);
        std::memcpy(portraitSha256, ps.c_str(), 65);

        *out = e;
        return 1;
    }
    catch (const std::exception &x)
    {
        Fail(error, errorSize, x.what());
        return 0;
    }
}

extern "C" int NativeRacerLibrary_Scan(const char *racersDir, NativeRacerEntry *out, int max)
{
    std::vector<NativeRacerEntry> found;
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::u8path(racersDir);
    if (!std::filesystem::is_directory(root, ec)) return 0;

    for (const auto &item : std::filesystem::directory_iterator(root, ec))
    {
        if (!item.is_directory(ec) || item.is_symlink(ec)) continue;
        const std::filesystem::path manifestPath = item.path() / "racer.json";
        FILE *f = NativeFs_OpenRead(manifestPath.u8string().c_str());
        if (!f) continue;
        std::string text;
        char buffer[4096];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0 && text.size() < 65536) text.append(buffer, n);
        std::fclose(f);

        NativeRacerEntry e;
        char modelSha[65], portraitSha[65], error[160];
        size_t modelBytes = 0, portraitBytes = 0;
        const std::string folder = item.path().filename().u8string();
        if (!NativeRacerLibrary_ParseManifest(text.data(), text.size(), &e, modelSha, portraitSha, &modelBytes, &portraitBytes,
                                              error, sizeof error))
        {
            std::fprintf(stderr, "[Racers] %s: %s\n", folder.c_str(), error);
            continue;
        }
        const std::filesystem::path model = item.path() / std::filesystem::u8path(e.modelPath);
        const std::filesystem::path portrait = item.path() / std::filesystem::u8path(e.portraitPath);
        if (!HashFile(model, modelBytes, modelSha) || !HashFile(portrait, portraitBytes, portraitSha))
        {
            std::fprintf(stderr, "[Racers] %s: a file is missing or does not match racer.json\n", folder.c_str());
            continue;
        }
        if (std::any_of(found.begin(), found.end(), [&](const NativeRacerEntry &o) { return std::strcmp(o.id, e.id) == 0; }))
        {
            std::fprintf(stderr, "[Racers] %s: another folder has the same racer id\n", folder.c_str());
            continue;
        }
        std::snprintf(e.modelPath, sizeof e.modelPath, "%s", model.u8string().c_str());
        std::snprintf(e.portraitPath, sizeof e.portraitPath, "%s", portrait.u8string().c_str());
        std::snprintf(e.folder, sizeof e.folder, "%s", folder.c_str());
        {
            NativeSha256Ctx ctx;
            unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
            NativeSha256_Init(&ctx);
            NativeSha256_Update(&ctx, text.data(), text.size());
            NativeSha256_Final(&ctx, digest);
            NativeSha256_ToHex(digest, e.manifestSha256);
        }
        found.push_back(e);
    }

    std::sort(found.begin(), found.end(), [](const NativeRacerEntry &a, const NativeRacerEntry &b) {
        const int byName = std::strcmp(a.name, b.name);
        return byName ? byName < 0 : std::strcmp(a.id, b.id) < 0;
    });
    const int count = std::min<int>((int)found.size(), max);
    for (int i = 0; i < count; i++) out[i] = found[i];
    return count;
}

extern "C" int NativeRacerLibrary_ParseIndex(const char *text, size_t size, NativeRacerCatalogueEntry *out, int max,
                                             char *error, size_t errorSize)
{
    try
    {
        const json index = json::parse(text, text + size);
        if (index.at("schema_version").get<int>() != 1) return Fail(error, errorSize, "unknown index schema_version"), -1;
        int count = 0;
        for (const auto &r : index.at("racers"))
        {
            if (count >= max) break;
            NativeRacerCatalogueEntry e{};
            const std::string id = r.at("id").get<std::string>();
            const std::string name = r.at("name").get<std::string>();
            const std::string folder = r.at("folder").get<std::string>();
            const json &manifest = r.at("manifest");
            const std::string sha = manifest.at("sha256").get<std::string>();
            e.templateID = IndexOf(kTemplates, 16, r.at("template").get<std::string>());
            e.manifestBytes = manifest.at("bytes").get<size_t>();
            // A row this client cannot use is left out, not the whole index.
            if (id.size() != 36 || !ValidName(name, sizeof e.name) || !ValidFolder(folder.c_str()) ||
                folder.size() >= sizeof e.folder || sha.size() != 64 || e.templateID < 0 || e.manifestBytes > 65536)
                continue;
            std::string authors;
            for (const auto &a : r.value("authors", json::array()))
                authors += (authors.empty() ? "" : ", ") + a.get<std::string>();
            std::memcpy(e.id, id.c_str(), 37);
            std::snprintf(e.name, sizeof e.name, "%s", name.c_str());
            std::snprintf(e.authors, sizeof e.authors, "%s", authors.c_str());
            std::snprintf(e.folder, sizeof e.folder, "%s", folder.c_str());
            std::memcpy(e.manifestSha256, sha.c_str(), 65);
            out[count++] = e;
        }
        return count;
    }
    catch (const std::exception &x)
    {
        Fail(error, errorSize, x.what());
        return -1;
    }
}

extern "C" int NativeRacerLibrary_Install(const char *racersDir, const char *folder, const void *manifest, size_t manifestSize,
                                          const void *model, size_t modelSize, const void *portrait, size_t portraitSize,
                                          char *error, size_t errorSize)
{
    if (!ValidFolder(folder)) return Fail(error, errorSize, "bad folder name"), 0;
    NativeRacerEntry e;
    char modelSha[65], portraitSha[65];
    size_t modelBytes = 0, portraitBytes = 0;
    if (!NativeRacerLibrary_ParseManifest((const char *)manifest, manifestSize, &e, modelSha, portraitSha, &modelBytes,
                                          &portraitBytes, error, errorSize))
        return 0;
    if (!HashBytes(model, modelSize, modelBytes, modelSha) || !HashBytes(portrait, portraitSize, portraitBytes, portraitSha))
        return Fail(error, errorSize, "a downloaded file does not match racer.json"), 0;

    std::error_code ec;
    const std::filesystem::path root = std::filesystem::u8path(racersDir);
    const std::filesystem::path target = root / std::filesystem::u8path(folder);
    const std::filesystem::path staging = root / std::filesystem::u8path(std::string(".") + folder + ".part");
    std::filesystem::create_directories(root, ec);
    std::filesystem::remove_all(staging, ec);
    if (!std::filesystem::create_directory(staging, ec) ||
        !WriteFile(staging / "racer.json", manifest, manifestSize) ||
        !WriteFile(staging / std::filesystem::u8path(e.modelPath), model, modelSize) ||
        !WriteFile(staging / std::filesystem::u8path(e.portraitPath), portrait, portraitSize))
    {
        std::filesystem::remove_all(staging, ec);
        return Fail(error, errorSize, "could not write the racer's files"), 0;
    }

    // An update replaces the racer wherever it was installed.
    for (const auto &item : std::filesystem::directory_iterator(root, ec))
    {
        const std::string name = item.path().filename().u8string();
        if (item.is_directory(ec) && !item.is_symlink(ec) && name[0] != '.' && FolderRacerId(item.path()) == e.id)
            std::filesystem::remove_all(item.path(), ec);
    }
    std::filesystem::remove_all(target, ec);
    std::filesystem::rename(staging, target, ec);
    if (ec)
    {
        std::filesystem::remove_all(staging, ec);
        return Fail(error, errorSize, "could not move the racer into place"), 0;
    }
    return 1;
}

extern "C" int NativeRacerLibrary_Remove(const char *racersDir, const char *folder, char *error, size_t errorSize)
{
    if (!ValidFolder(folder)) return Fail(error, errorSize, "bad folder name"), 0;
    std::error_code ec;
    const std::filesystem::path target = std::filesystem::u8path(racersDir) / std::filesystem::u8path(folder);
    if (std::filesystem::is_symlink(target, ec) || !std::filesystem::is_directory(target, ec))
        return Fail(error, errorSize, "the racer's folder is not there"), 0;
    std::filesystem::remove_all(target, ec);
    if (ec) return Fail(error, errorSize, "could not delete the racer's files"), 0;
    return 1;
}
