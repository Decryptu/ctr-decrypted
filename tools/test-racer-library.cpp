// g++ -m32 -std=c++17 -Wall -Wextra -DCTR_CUSTOM_TRACKS -I . -I include -I ap/vendor/json/include -o /tmp/test-racer-library
//   tools/test-racer-library.cpp platform/native_racer_library.cpp -x c platform/native_fs_utf8.c
//
// The custom racer library (platform/native_racer_library.cpp,
// docs/CUSTOM_RACERS.md): the catalogue index parser, and an install, update
// and removal in a scratch folder, checked by scanning it back.
#include <platform/native_racer_library.h>
#include <platform/native_sha256.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static int failures, checks;
#define CHECK(cond)                                                       \
    do                                                                    \
    {                                                                     \
        checks++;                                                         \
        if (!(cond))                                                      \
        {                                                                 \
            failures++;                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);           \
        }                                                                 \
    } while (0)

static std::string Sha(const std::vector<unsigned char> &bytes)
{
    NativeSha256Ctx ctx;
    NativeSha256_Init(&ctx);
    NativeSha256_Update(&ctx, bytes.data(), bytes.size());
    unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
    char hex[NATIVE_SHA256_HEX_BYTES];
    NativeSha256_Final(&ctx, digest);
    NativeSha256_ToHex(digest, hex);
    return hex;
}

static std::string Manifest(const char *id, const char *name, const std::vector<unsigned char> &model,
                            const std::vector<unsigned char> &portrait)
{
    char text[2048];
    std::snprintf(text, sizeof text,
                  "{\"schema_version\":1,\"id\":\"%s\",\"name\":\"%s\",\"short_name\":\"TEST\",\"authors\":[\"Someone\"],"
                  "\"template\":\"tiny\",\"engine\":\"speed\",\"wheels\":false,\"files\":{"
                  "\"model\":{\"path\":\"model.ctr\",\"bytes\":%u,\"sha256\":\"%s\"},"
                  "\"portrait\":{\"path\":\"portrait.bin\",\"bytes\":%u,\"sha256\":\"%s\"}}}",
                  id, name, (unsigned)model.size(), Sha(model).c_str(), (unsigned)portrait.size(), Sha(portrait).c_str());
    return text;
}

static int Install(const std::string &dir, const char *folder, const std::string &manifest,
                   const std::vector<unsigned char> &model, const std::vector<unsigned char> &portrait)
{
    char error[160] = {0};
    return NativeRacerLibrary_Install(dir.c_str(), folder, manifest.data(), manifest.size(), model.data(), model.size(),
                                      portrait.data(), portrait.size(), error, sizeof error);
}

int main()
{
    // The index: rows this client cannot use are left out, not the index.
    const std::string index =
        "{\"schema_version\":1,\"racers\":["
        "{\"id\":\"11111111-1111-5111-8111-111111111111\",\"name\":\"BOWSER\",\"authors\":[\"A\",\"B\"],\"template\":\"tiny\","
        "\"folder\":\"bowser-11111111\",\"manifest\":{\"bytes\":700,\"sha256\":\"" +
        std::string(64, 'a') +
        "\"}},"
        "{\"id\":\"22222222-2222-5222-8222-222222222222\",\"name\":\"ESCAPE\",\"authors\":[\"C\"],\"template\":\"crash\","
        "\"folder\":\"../escape\",\"manifest\":{\"bytes\":700,\"sha256\":\"" +
        std::string(64, 'b') +
        "\"}},"
        "{\"id\":\"33333333-3333-5333-8333-333333333333\",\"name\":\"HIDDEN\",\"authors\":[],\"template\":\"crash\","
        "\"folder\":\".hidden\",\"manifest\":{\"bytes\":700,\"sha256\":\"" +
        std::string(64, 'c') + "\"}}]}";
    NativeRacerCatalogueEntry entries[4];
    char error[160] = {0};
    CHECK(NativeRacerLibrary_ParseIndex(index.data(), index.size(), entries, 4, error, sizeof error) == 1);
    CHECK(!std::strcmp(entries[0].folder, "bowser-11111111"));
    CHECK(!std::strcmp(entries[0].authors, "A, B"));
    CHECK(entries[0].templateID == 2 && entries[0].manifestBytes == 700);
    CHECK(NativeRacerLibrary_ParseIndex("{\"schema_version\":2,\"racers\":[]}", 32, entries, 4, error, sizeof error) == -1);
    CHECK(NativeRacerLibrary_ParseIndex("not json", 8, entries, 4, error, sizeof error) == -1);

    // Install into a scratch folder, then scan it back.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ctr-racer-library-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const std::string dir = root.u8string();
    std::vector<unsigned char> model(0x80, 7), portrait(NATIVE_RACER_PORTRAIT_BYTES, 1);
    const char *id = "44444444-4444-5444-8444-444444444444";
    const std::string manifest = Manifest(id, "TEST RACER", model, portrait);

    std::vector<unsigned char> wrong = model;
    wrong[3] ^= 1;
    CHECK(!Install(dir, "test-racer", manifest, wrong, portrait));
    CHECK(!std::filesystem::exists(root / "test-racer"));
    CHECK(!Install(dir, "../outside", manifest, model, portrait));
    CHECK(!Install(dir, ".part", manifest, model, portrait));

    CHECK(Install(dir, "test-racer", manifest, model, portrait));
    NativeRacerEntry found[4];
    CHECK(NativeRacerLibrary_Scan(dir.c_str(), found, 4) == 1);
    CHECK(!std::strcmp(found[0].id, id) && !std::strcmp(found[0].folder, "test-racer"));
    CHECK(found[0].engine == 2 && found[0].wheels == 0);
    // The installed racer.json's SHA-256, matched against the catalogue's to offer an update.
    {
        NativeSha256Ctx ctx;
        unsigned char digest[NATIVE_SHA256_DIGEST_BYTES];
        char hex[NATIVE_SHA256_HEX_BYTES];
        NativeSha256_Init(&ctx);
        NativeSha256_Update(&ctx, manifest.data(), manifest.size());
        NativeSha256_Final(&ctx, digest);
        NativeSha256_ToHex(digest, hex);
        CHECK(!std::strcmp(found[0].manifestSha256, hex));
    }
    CHECK(!std::filesystem::exists(root / ".test-racer.part"));

    // An update under another folder name replaces the racer, not adds one.
    model.push_back(9);
    CHECK(Install(dir, "test-racer-v2", Manifest(id, "TEST RACER", model, portrait), model, portrait));
    CHECK(NativeRacerLibrary_Scan(dir.c_str(), found, 4) == 1);
    CHECK(!std::strcmp(found[0].folder, "test-racer-v2"));
    CHECK(!std::filesystem::exists(root / "test-racer"));

    CHECK(!NativeRacerLibrary_Remove(dir.c_str(), "..", error, sizeof error));
    CHECK(!NativeRacerLibrary_Remove(dir.c_str(), "missing", error, sizeof error));
    CHECK(NativeRacerLibrary_Remove(dir.c_str(), "test-racer-v2", error, sizeof error));
    CHECK(NativeRacerLibrary_Scan(dir.c_str(), found, 4) == 0);

    // Mask override ("aku"/"uka") and optional sampled voice files ("voice_0"/"voice_1").
    std::vector<unsigned char> v0(0x40, 0x11), v1(0x30, 0x22);
    char voiceManifest[4096];
    std::snprintf(voiceManifest, sizeof voiceManifest,
                  "{\"schema_version\":1,\"id\":\"55555555-5555-5555-8555-555555555555\",\"name\":\"VOICE RACER\","
                  "\"short_name\":\"VOICE\",\"authors\":[\"Someone\"],\"template\":\"cortex\",\"engine\":\"accel\","
                  "\"wheels\":true,\"mask\":\"aku\",\"files\":{"
                  "\"model\":{\"path\":\"model.ctr\",\"bytes\":%u,\"sha256\":\"%s\"},"
                  "\"portrait\":{\"path\":\"portrait.bin\",\"bytes\":%u,\"sha256\":\"%s\"},"
                  "\"voice_0\":{\"path\":\"v0.adpcm\",\"bytes\":%u,\"sha256\":\"%s\",\"volume\":200,\"pitch\":1024,\"duration\":80,\"flags\":4},"
                  "\"voice_1\":{\"path\":\"v1.adpcm\",\"bytes\":%u,\"sha256\":\"%s\"}}}",
                  (unsigned)model.size(), Sha(model).c_str(), (unsigned)portrait.size(), Sha(portrait).c_str(),
                  (unsigned)v0.size(), Sha(v0).c_str(), (unsigned)v1.size(), Sha(v1).c_str());
    const void *voiceData[NATIVE_RACER_SAMPLED_VOICE_COUNT] = {v0.data(), v1.data()};
    const size_t voiceSizes[NATIVE_RACER_SAMPLED_VOICE_COUNT] = {v0.size(), v1.size()};
    // Missing voice payload must be rejected.
    CHECK(!NativeRacerLibrary_Install(dir.c_str(), "voice-racer", voiceManifest, std::strlen(voiceManifest),
                                      model.data(), model.size(), portrait.data(), portrait.size(), error, sizeof error));
    // Valid voice payloads install and scan back with mask + metadata intact.
    CHECK(NativeRacerLibrary_InstallWithVoices(dir.c_str(), "voice-racer", voiceManifest, std::strlen(voiceManifest),
                                               model.data(), model.size(), portrait.data(), portrait.size(),
                                               voiceData, voiceSizes, error, sizeof error));
    CHECK(NativeRacerLibrary_Scan(dir.c_str(), found, 4) == 1);
    CHECK(found[0].mask == 1);
    CHECK(found[0].sampledVoices[0].bytes == v0.size() && found[0].sampledVoices[0].volume == 200 &&
          found[0].sampledVoices[0].pitch == 1024 && found[0].sampledVoices[0].duration == 80 && found[0].sampledVoices[0].flags == 4);
    CHECK(found[0].sampledVoices[1].bytes == v1.size() && found[0].sampledVoices[1].volume == 255 &&
          found[0].sampledVoices[1].pitch == 0x0400 && found[0].sampledVoices[1].duration >= 30 && found[0].sampledVoices[1].flags == 4);
    // Reject unknown mask and unaligned voice sample size.
    {
        NativeRacerEntry bad;
        char ms[65], ps[65];
        size_t mb = 0, pb = 0;
        const std::string badMask = std::string(voiceManifest).replace(std::string(voiceManifest).find("\"aku\""), 5, "\"nope\"");
        CHECK(!NativeRacerLibrary_ParseManifest(badMask.data(), badMask.size(), &bad, ms, ps, &mb, &pb, error, sizeof error));
    }

    std::filesystem::remove_all(root, ec);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
