"""Room-local custom track labels for an unmodified Archipelago server.

Datapackages are per game, not per player. Conflicting aliases therefore name
every owner explicitly. Never pick one player's track for everybody, move an
address, or mutate the registered package shared by concurrent generations.
"""
from collections import Counter, defaultdict
from copy import deepcopy

from worlds.AutoWorld import data_package_checksum

from .custom_track_presentation import location_aliases


def embed_track_names(multiworld, multidata, game: str) -> None:
    package = multidata["datapackage"][game]
    canonical = {code: name for name, code in package["location_name_to_id"].items()}
    owners = defaultdict(list)
    changed = set()
    for player in sorted(multiworld.player_ids):
        world = multiworld.worlds[player]
        if world.game != game:
            continue
        aliases = location_aliases(world, escape_markup=False)
        # Include actual users without an alias too (e.g. legacy role checks).
        for location in multiworld.get_locations(player):
            code = location.address
            if code not in canonical:
                continue
            label = aliases.get(code, canonical[code])
            owners[code].append((player, label))
            if label != canonical[code]:
                changed.add(code)
    if not changed:
        return

    labels = dict(canonical)
    for code in sorted(changed):
        entries = owners[code]
        if len({label for _, label in entries}) == 1:
            labels[code] = entries[0][1]
        else:
            # Player number also disambiguates identical display names.
            labels[code] = " / ".join(
                f"{label} ({multiworld.player_name[player]}, P{player})"
                for player, label in entries)

    # A title can equal another title or even a registered retail name. Keep
    # the reverse name->ID map bijective, including deliberately crafted text.
    counts = Counter(labels.values())
    reserved = set(labels.values()) | set(canonical.values())
    for code in sorted(changed):
        label = labels[code]
        if counts[label] > 1:
            candidate = f"{label} [location {code}]"
            while candidate in reserved:
                candidate += "*"
            labels[code] = candidate
            reserved.add(candidate)

    result = deepcopy(package)
    result["location_name_to_id"] = dict(sorted((name, code) for code, name in labels.items()))
    renames = {canonical[code]: labels[code] for code in changed}
    result["location_name_groups"] = {
        group: sorted(renames.get(name, name) for name in names)
        for group, names in sorted(result.get("location_name_groups", {}).items())
    }
    result.pop("checksum", None)
    result = dict(sorted(result.items()))
    result["checksum"] = data_package_checksum(result)
    multidata["datapackage"][game] = result
