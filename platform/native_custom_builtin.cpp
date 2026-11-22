/* Built-in packages: tracks the release ships, kept in the package store so
   they race on the Arcade and Time Trial custom pages like any installed track.

   Cortex Vortex ships as plain files in assets/tracks/cortex-vortex/, the
   folder the Archipelago loader reads and the in-game updater replaces. Before
   each store scan, CustomPackage_EnsureBuiltins builds its package from those
   files when the store does not hold it yet. The manifest below is fixed and
   its pin is checked by CustomPackage_AcquireBuffers like any other package,
   so files that differ from the release's install nothing. Older built-in
   revisions (same UUID, other pin) are removed once the current one is in.

   The Mods screen can turn a built-in track off but never removes it: it would
   only come back at the next scan (CustomPackage_IsBuiltin). Play Solo leaves
   it out, as seeds have their own Cortex Vortex option. To change a built-in
   package, regenerate its manifest and pin with tools/tracks/builtin_manifest.py;
   its --check, run in CI, fails when they differ from the shipped files. */
#include <platform/native_custom_package.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
struct BuiltinFile { const char *role, *source; };
struct BuiltinPackage
{
    const char *uuid, *pin, *manifest, *presentation, *folder;
    BuiltinFile files[4];
};

const BuiltinPackage builtins[] = {
    {"65e834d1-1a23-5cda-9f40-015607944855",
     "46c396b9f403aec3838e8be43057440aee576101d46b56e19edbe987898cd0dd",
     "{\"compatibility\":{\"apworld_contract\":1,\"native_contract\":1},\"files\":[{\"bytes\":2526488,\"path\":\"original/lev.lev\",\"role\":\"lev\",\"sha256\":\"4e3a2daf56c67be3ac645d3bb5375e516c828a0bca24c35ac69b3366c466fe13\"},{\"bytes\":9408,\"path\":\"metadata/picture.ctp\",\"role\":\"picture\",\"sha256\":\"64e82a94d583243fcb5af79dc9db71094a311f986c4ec0571072d87e46ce3545\"},{\"bytes\":68,\"path\":\"metadata/decrypted-source.json\",\"role\":\"presentation\",\"sha256\":\"d1a50cfa3982a985036d59959bc6eeec80bcf40d8722e61592f50f29e57fe544\"},{\"bytes\":401108,\"path\":\"original/sca.sca\",\"role\":\"sca\",\"sha256\":\"ddde81cf1be8042400c717edeeded0d8727e3bb8ba24f0100201708372ef8b1c\"},{\"bytes\":458808,\"path\":\"original/vrm.vrm\",\"role\":\"vrm\",\"sha256\":\"4131444b9d1d53971befcfd11349efceaf887c20b795c8890fdcb2c36bdff07d\"}],\"package_uuid\":\"65e834d1-1a23-5cda-9f40-015607944855\",\"schema_version\":1,\"title\":\"Cortex Vortex\",\"version\":\"1.0.0\"}",
     "{\"schema_version\":1,\"provider\":\"ctr-decrypted\",\"author\":\"Lockheart\"}",
     "cortex-vortex",
     {{"lev", "CVortex Arcade All.lev"}, {"vrm", "CVortex Arcade All.vrm"}, {"sca", "CVortex Arcade All.sca"},
      {"picture", "CVortex Picture.ctp"}}},
};

std::vector<unsigned char> read_file(const std::filesystem::path &path, size_t maximum)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, ec)) || ec)
        throw std::runtime_error("missing " + path.filename().u8string());
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > maximum) throw std::runtime_error("unexpected size of " + path.filename().u8string());
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() != size) throw std::runtime_error("short read of " + path.filename().u8string());
    return bytes;
}

/* Pins of other revisions in the store that carry this UUID. */
struct Stale { const char *uuid, *pin; std::vector<std::string> pins; };
int want_stale(const char *pin, const CustomPackageManifest *manifest, void *opaque)
{
    auto &s = *static_cast<Stale *>(opaque);
    if (manifest && !std::strcmp(manifest->uuid, s.uuid) && std::strcmp(pin, s.pin)) s.pins.emplace_back(pin);
    return 0; /* never acquire: the manifest says enough */
}
void visit_none(const CustomPackageStoreEntry *, void *) {}

int ensure_one(const BuiltinPackage &b, const std::string &assets, char *error, size_t errorSize)
{
    const auto root = std::filesystem::u8path(assets) / "tracks";
    const auto store = root / "packages";
    std::error_code ec;
    int installed = 0;
    /* A build that ships no such folder (the authoring client) has nothing to add. */
    if (!std::filesystem::is_directory(root / b.folder, ec)) return 2;
    if (!std::filesystem::is_directory(store / b.pin, ec))
    {
        std::vector<std::vector<unsigned char>> buffers;
        std::vector<CustomPackageInput> inputs;
        buffers.reserve(sizeof b.files / sizeof b.files[0]);
        for (const auto &f : b.files)
        {
            buffers.push_back(read_file(root / b.folder / f.source, 16u << 20));
            inputs.push_back({f.role, buffers.back().data(), buffers.back().size()});
        }
        inputs.push_back({"presentation", b.presentation, std::strlen(b.presentation)});
        CustomPackageOwned *package = nullptr;
        if (!CustomPackage_AcquireBuffers(b.manifest, std::strlen(b.manifest), b.pin, inputs.data(), inputs.size(),
                                          &package, error, errorSize)) return 0;
        installed = CustomPackage_InstallAssets(package, assets.c_str(), error, errorSize);
        CustomPackage_Free(&package);
        if (!installed) return 0;
    }
    Stale stale{b.uuid, b.pin, {}};
    char scanError[256];
    if (CustomPackage_ScanStoreMatching(store.u8string().c_str(), want_stale, visit_none, &stale, scanError, sizeof scanError))
        for (const auto &pin : stale.pins) CustomPackage_RemoveInstalled(assets.c_str(), pin.c_str(), scanError, sizeof scanError);
    return installed ? installed : 2;
}
}

extern "C" int CustomPackage_IsBuiltin(const char *uuid)
{
    if (!uuid || !*uuid) return 0;
    for (const auto &b : builtins)
        if (!std::strcmp(uuid, b.uuid)) return 1;
    return 0;
}

extern "C" int CustomPackage_EnsureBuiltins(const char *assets, char *error, size_t errorSize)
{
    if (error && errorSize) error[0] = 0;
    int ok = 1;
    for (const auto &b : builtins)
    {
        char why[256] = {};
        try
        {
            if (!assets || std::strlen(assets) > 31000) throw std::runtime_error("no assets directory");
            if (ensure_one(b, assets, why, sizeof why)) continue;
        }
        catch (const std::exception &exception) { std::snprintf(why, sizeof why, "%s", exception.what()); }
        ok = 0;
        std::fprintf(stderr, "[CustomTracks] built-in %s not in the store: %s\n", b.folder, why);
        if (error && errorSize && !error[0]) std::snprintf(error, errorSize, "%s: %s", b.folder, why);
    }
    return ok;
}
