#include <platform/native_saphi_catalogue.h>
#include <platform/native_custom_music.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

struct CustomSaphiCatalogue { std::vector<CustomSaphiRevision> rows; };
namespace {
using json = nlohmann::json;
int source_integer(const json &value, int maximum)
{
    if (!value.is_number_integer() || value < 1 || value > maximum)
        throw std::runtime_error("Invalid Saphi source integer");
    return value.get<int>();
}
template<size_t N> void source_text(const json &value, char (&out)[N], bool empty = false)
{
    if (!value.is_string()) throw std::runtime_error("Invalid Saphi display text");
    auto text = value.get<std::string>();
    if ((!empty && text.empty()) || text.size() >= N ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        throw std::runtime_error("Saphi display text exceeds supported bounds");
    std::memcpy(out, text.c_str(), text.size() + 1);
}
/* Known tags become bits. Saphi adds modes over time (ring_rally, 2026-09):
   a tag this client does not know is left out of the bits instead of failing
   the whole catalogue, and goes into *other so revisions that differ only in
   such a tag still pair apart. */
unsigned int source_modes(const json &modes, std::string *other)
{
    static const char *names[] = {"arcade", "time_trial", "relic_race", "ctr_challenge", "crystal_challenge", "battle"};
    if (!modes.is_array() || modes.size() > 16) throw std::runtime_error("Invalid Saphi mode tags");
    unsigned int result = 0;
    std::set<std::string> unknown;
    for (const auto &mode : modes)
    {
        if (!mode.is_string()) throw std::runtime_error("Invalid Saphi mode name");
        unsigned int bit = 0;
        for (unsigned int i = 0; i < 6; i++) if (mode == names[i]) bit = 1u << i;
        if (!bit)
        {
            if (!unknown.insert(mode.get<std::string>()).second) throw std::runtime_error("Duplicate Saphi mode tag");
            continue;
        }
        if (result & bit) throw std::runtime_error("Unknown or duplicate Saphi mode tag");
        result |= bit;
    }
    if (other)
    {
        other->clear();
        for (const auto &name : unknown) { *other += name; *other += '\n'; }
    }
    return result;
}
CustomSaphiMedia source_media(const json &media, int track)
{
    CustomSaphiMedia result{};
    result.id = source_integer(media.at("id"), 2147483647);
    /* Saphi also publishes checksum-reference rows with no downloadable
       payload. Keep them visible, but never manufacture a download URL. */
    if (media.at("download_url").is_null() && media.at("file_size") == 0) return result;
    result.bytes = source_integer(media.at("file_size"), 8 * 1024 * 1024);
    const auto &crc = media.at("crc32_hash");
    if (!crc.is_number_integer() || crc < 0 || crc > UINT32_MAX)
        throw std::runtime_error("Invalid Saphi media checksum");
    result.crc32 = crc.get<uint32_t>();
    std::string expected = "/api/v3/tracks/" + std::to_string(track) + "/downloads/" + std::to_string(result.id);
    std::string path = media.at("download_url").get<std::string>();
    const std::string origin = "https://www.projectsaphi.com";
    if (path.compare(0, origin.size(), origin) == 0) path.erase(0, origin.size());
    if (path != expected) throw std::runtime_error("Saphi media URL does not match its source identity");
    std::snprintf(result.path, sizeof result.path, "%s", path.c_str());
    return result;
}
void sort_rows(std::vector<CustomSaphiRevision> &rows)
{
    auto folded = [](const char *text) {
        std::string value(text);
        for (char &c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return value;
    };
    std::sort(rows.begin(), rows.end(), [&](const auto &a, const auto &b) {
        auto at = folded(a.title), bt = folded(b.title);
        if (at != bt) return at < bt;
        if (a.current != b.current) return a.current > b.current;
        int version = std::strcmp(a.version,b.version);
        if (version) return version < 0;
        if (a.trackID != b.trackID) return a.trackID < b.trackID;
        if (a.decrypted != b.decrypted) return a.decrypted < b.decrypted;
        if (a.lev.id != b.lev.id) return a.lev.id < b.lev.id;
        return a.vrm.id < b.vrm.id;
    });
}
template<size_t N> void lowercase_hex(const json &value, char (&out)[N], const char *what)
{
    auto text = value.get<std::string>();
    if (text.size() != N - 1 || text.find_first_not_of("0123456789abcdef-") != std::string::npos)
        throw std::runtime_error(what);
    std::memcpy(out, text.c_str(), N);
}
CustomSaphiMedia listed_file(const json &file)
{
    CustomSaphiMedia result{};
    result.bytes = source_integer(file.at("bytes"), 8 * 1024 * 1024);
    const auto &crc = file.at("crc32");
    if (!crc.is_number_integer() || crc < 0 || crc > UINT32_MAX) throw std::runtime_error("Invalid track checksum");
    result.crc32 = crc.get<uint32_t>();
    return result;
}
bool same_file(const CustomSaphiMedia &a, const CustomSaphiMedia &b) { return a.bytes && a.bytes == b.bytes && a.crc32 == b.crc32; }
}

extern "C" int CustomSaphi_ParseCatalogue(const char *input, size_t size,
    CustomSaphiCatalogue **out, char *error, size_t errorSize)
{
    try
    {
        if (!input || !size || size > 4 * 1024 * 1024 || !out || *out)
            throw std::runtime_error("Expected bounded Saphi catalogue and empty output");
        json root = json::parse(input, input + size);
        if (!root.is_object() || !root.at("data").is_array() || root["data"].size() > 1024)
            throw std::runtime_error("Invalid Saphi catalogue response");
        auto result = std::make_unique<CustomSaphiCatalogue>();
        std::set<int> trackIDs, mediaIDs;
        for (const auto &track : root["data"])
        {
            if (!track.is_object()) throw std::runtime_error("Invalid Saphi track");
            int id = source_integer(track.at("id"), 2147483647);
            if (!trackIDs.insert(id).second) throw std::runtime_error("Duplicate Saphi track identity");
            if (!track.at("is_active").is_boolean()) throw std::runtime_error("Invalid Saphi active flag");
            if (!track["is_active"].get<bool>()) continue;
            if (track.at("type") != "race" && track.at("type") != "battle") continue;
            CustomSaphiRevision base{};
            base.trackID = id;
            source_text(track.at("name"), base.title);
            source_text(track.at("author"), base.author, true);
            if (track.contains("lap_count") && !track["lap_count"].is_null())
                base.sourceLaps = source_integer(track["lap_count"], 2147483647);
            const auto &downloads = track.at("downloads");
            if (!downloads.is_array() || downloads.size() > 512) throw std::runtime_error("Too many Saphi media rows");
            struct Pair { CustomSaphiRevision revision{}; int levCount = 0, vrmCount = 0; bool levCurrent = false, vrmCurrent = false; };
            std::map<std::tuple<std::string, unsigned int, std::string>, Pair> pairs;
            /* Audio is optional: a malformed or ambiguous .sca row leaves the
               track without its own music instead of failing the catalogue. */
            CustomSaphiMedia sca{};
            int currentSca = 0;
            for (const auto &media : downloads)
            {
                try
                {
                    if (!media.is_object() || media.at("type") != "sca" || media.at("is_current") != true) continue;
                    auto file = source_media(media, id);
                    if (!file.bytes || file.bytes > CTR_SCA_MAX_BYTES) continue;
                    sca = file;
                    currentSca++;
                }
                catch (const std::exception &) {}
            }
            if (currentSca != 1) sca = CustomSaphiMedia{};
            int currentPictures = 0;
            for (const auto &media : downloads)
            {
                try
                {
                    if (!media.is_object() || media.at("type") != "track_thumbnail" || media.at("is_current") != true) continue;
                    auto file = source_media(media, id);
                    if (!file.path[0]) continue;
                    base.picture = file;
                    currentPictures++;
                }
                catch (const std::exception &) {}
            }
            if (currentPictures != 1) base.picture = CustomSaphiMedia{};
            for (const auto &media : downloads)
            {
                const auto &type = media.at("type");
                if (type != "lev" && type != "vrm") continue;
                CustomSaphiRevision row = base;
                source_text(media.at("version"), row.version);
                std::string otherModes;
                row.modeTags = source_modes(media.at("modes"), &otherModes);
                auto file = source_media(media, id);
                if (!mediaIDs.insert(file.id).second) throw std::runtime_error("Duplicate Saphi media identity");
                if (!media.at("is_current").is_boolean()) throw std::runtime_error("Invalid Saphi current flag");
                auto &pair = pairs[std::make_tuple(std::string(row.version), row.modeTags, otherModes)];
                if (!pair.levCount && !pair.vrmCount) pair.revision = row;
                if (type == "lev") { pair.revision.lev = file; pair.levCount++; pair.levCurrent = media["is_current"]; }
                else { pair.revision.vrm = file; pair.vrmCount++; pair.vrmCurrent = media["is_current"]; }
            }
            for (auto &item : pairs)
            {
                auto &pair = item.second;
                if (pair.levCount != 1 || pair.vrmCount != 1)
                    std::snprintf(pair.revision.disabledReason, sizeof pair.revision.disabledReason,
                        "%s %s", pair.levCount != 1 ? pair.levCount ? "Ambiguous" : "Missing" : pair.vrmCount ? "Ambiguous" : "Missing",
                        pair.levCount != 1 ? "LEV" : "VRM");
                else if (!pair.revision.lev.bytes || !pair.revision.vrm.bytes)
                    std::snprintf(pair.revision.disabledReason, sizeof pair.revision.disabledReason,
                        "Files not hosted");
                pair.revision.current = pair.levCurrent && pair.vrmCurrent;
                if (pair.revision.current) pair.revision.sca = sca;
                result->rows.push_back(pair.revision);
                if (result->rows.size() > 1024) throw std::runtime_error("Too many Saphi revisions");
            }
        }
        sort_rows(result->rows);
        *out = result.release();
        if (error && errorSize) error[0] = 0;
        return 1;
    }
    catch (const std::exception &e)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", e.what());
        return 0;
    }
}
extern "C" int CustomSaphi_AddDecrypted(CustomSaphiCatalogue *catalogue, const char *input, size_t size,
    char *error, size_t errorSize)
{
    try
    {
        if (!catalogue || !input || !size || size > 1024 * 1024) throw std::runtime_error("Expected a bounded track list");
        json root = json::parse(input, input + size);
        if (root.at("schema_version") != 1 || !root.at("tracks").is_array() || root["tracks"].size() > 256)
            throw std::runtime_error("Invalid track list");
        std::vector<CustomSaphiRevision> rows = catalogue->rows;
        std::set<std::string> ids;
        int next = -1;
        for (const auto &track : root["tracks"])
        {
            CustomSaphiRevision row{};
            lowercase_hex(track.at("id"), row.packageUuid, "Invalid track id");
            if (!ids.insert(row.packageUuid).second) throw std::runtime_error("Duplicate track id");
            lowercase_hex(track.at("pin"), row.pin, "Invalid track pin");
            source_text(track.at("title"), row.title);
            source_text(track.at("author"), row.author);
            source_text(track.at("version"), row.version);
            source_text(track.at("folder"), row.folder);
            if (std::strspn(row.folder, "abcdefghijklmnopqrstuvwxyz0123456789-") != std::strlen(row.folder))
                throw std::runtime_error("Invalid track folder");
            row.manifestBytes = source_integer(track.at("manifest_bytes"), 65536);
            row.lev = listed_file(track.at("lev"));
            row.vrm = listed_file(track.at("vrm"));
            row.modeTags = source_modes(track.at("modes"), nullptr);
            row.current = row.decrypted = 1;
            row.trackID = next--;
            int saphi = 0;
            if (track.contains("saphi_track_id")) saphi = source_integer(track["saphi_track_id"], 2147483647);
            int same = 0;
            for (const auto &other : rows)
            {
                if (other.decrypted) continue;
                if (other.trackID == saphi || (same_file(row.lev, other.lev) && same_file(row.vrm, other.vrm)))
                {
                    same = other.trackID;
                    break;
                }
            }
            bool listed = false;
            if (same)
            {
                row.trackID = same;
                for (auto &other : rows)
                {
                    if (other.decrypted || other.trackID != same) continue;
                    std::memcpy(other.packageUuid, row.packageUuid, sizeof row.packageUuid);
                    row.picture = other.picture;
                    if (!other.disabledReason[0] && !std::strcmp(other.version, row.version)) listed = true;
                }
            }
            if (listed) continue;
            rows.push_back(row);
            if (rows.size() > 1024) throw std::runtime_error("Too many track revisions");
        }
        sort_rows(rows);
        catalogue->rows = std::move(rows);
        if (error && errorSize) error[0] = 0;
        return 1;
    }
    catch (const std::exception &e)
    {
        if (error && errorSize) std::snprintf(error, errorSize, "%s", e.what());
        return 0;
    }
}
extern "C" size_t CustomSaphi_Count(const CustomSaphiCatalogue *c) { return c ? c->rows.size() : 0; }
extern "C" const CustomSaphiRevision *CustomSaphi_Row(const CustomSaphiCatalogue *c, size_t index)
{ return c && index < c->rows.size() ? &c->rows[index] : nullptr; }
extern "C" void CustomSaphi_Free(CustomSaphiCatalogue **c) { if (c) { delete *c; *c = nullptr; } }
