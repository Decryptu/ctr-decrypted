"""Slot-owned custom letter identities; activation awaits certified CTR modes.

No borrowed retail LevelID, mutable package title, or cup-leg number contributes
to the address. Repeated routes to the same seed slot share these identities.
"""
from .custom_check_namespace import (
    CUSTOM_SLOT_COUNT, LETTERS, custom_check_code, custom_check_name,
)
from .location_class import LocationClass
from BaseClasses import ItemClassification

# Separate from positional data/items.json: these addresses deliberately use
# their own reserved block and must not shift the shipped 0..199 index table.
CUSTOM_LETTER_ITEM_DATA = tuple({
    "name": custom_check_name("letter_item", slot, li),
    "code": custom_check_code("letter_item", slot, li),
    "count": 0,
    "classification": ItemClassification.progression,
} for slot in range(1, CUSTOM_SLOT_COUNT + 1) for li in range(len(LETTERS)))


def admitted_custom_ctr_wire_slots(slot_data):
    """Admit explicit pinned parent modes, never asset capability alone.

    Call only after the existing custom parent descriptor validator succeeds.
    This additional validator binds each requested CTR mode to its slot-owned
    challenge address before its child letter block can be restored.
    """
    parent = slot_data.get("custom_tracks", {})
    if not isinstance(parent, dict) or parent.get("enabled") is not True:
        return ()
    admitted = []
    for track in parent.get("tracks", ()):
        modes = track.get("modes", {})
        if not isinstance(modes, dict):
            raise ValueError("custom modes must be an object")
        requested = modes.get("ctr_challenge", False)
        if type(requested) is not bool:
            raise ValueError("custom CTR mode must be Boolean")
        if not requested:
            continue
        slot = track.get("slot")
        expected = custom_check_code("ctr_location", slot)
        flags, locations = track.get("flags", {}), track.get("locations", {})
        code = locations.get("ctr")
        if flags.get("ctr_letters") is not True or type(code) is not int or code != expected:
            raise ValueError("custom CTR mode lacks owned challenge identity")
        if slot in admitted:
            raise ValueError("duplicate custom CTR slot")
        admitted.append(slot)
    return tuple(sorted(admitted))


def select_custom_letters(mode, count, admitted_slots, rng):
    """Resolve once after certified CTR-mode admission, never from asset flags."""
    if type(mode) is not int or mode not in range(4):
        raise ValueError("invalid custom lettersanity mode")
    if type(count) is not int or count not in (1, 2, 3):
        raise ValueError("invalid letters per track")
    slots = list(admitted_slots)
    for slot in slots:
        custom_check_code("letter_location", slot)
    if len(set(slots)) != len(slots):
        raise ValueError("duplicate admitted custom slot")
    if mode == 0:
        return {}
    return {slot: tuple(sorted(rng.sample(LETTERS, count), key=LETTERS.index)) if mode in (1, 2)
            else LETTERS for slot in sorted(slots)}


def custom_letter_wire(mode, count, selection):
    """Encode slot-owned required items/checks without any retail host keys."""
    # Validate scalar types even when there are no admitted slots.
    select_custom_letters(mode, count, (), None)
    if mode == 0:
        if selection:
            raise ValueError("off mode cannot carry custom selection")
        return None
    tracks = []
    for slot, selected in sorted(selection.items()):
        custom_check_code("letter_location", slot)
        if (len(set(selected)) != len(selected) or
                any(letter not in LETTERS for letter in selected) or
                len(selected) != (3 if mode == 3 else count)):
            raise ValueError("custom selection does not match the mode/count")
        tracks.append({"slot": slot,
                       "locations": [custom_check_code("letter_location", slot, li)
                                     if mode in (1, 2) and letter in selected else -1
                                     for li, letter in enumerate(LETTERS)],
                       "items": [custom_check_code("letter_item", slot, li)
                                 if mode in (2, 3) and letter in selected else -1
                                 for li, letter in enumerate(LETTERS)]})
    return {"version": 1, "mode": mode, "letters_per_track": count,
            "tracks": tracks}


def restore_custom_letter_wire(block, admitted_slots):
    """UT restores exact selection and rejects forged addresses/slot ownership.

    Caller admits slots using the pinned package/mode descriptors, not this
    block. Canonical re-encoding checks fixed C/T/R arrays and mode semantics.
    """
    if not isinstance(block, dict) or type(block.get("version")) is not int or block["version"] != 1:
        raise ValueError("unsupported custom letter wire version")
    mode, count = block.get("mode"), block.get("letters_per_track")
    select_custom_letters(mode, count, (), None)
    admitted = select_custom_letters(3, count, admitted_slots, None)
    if mode == 0:
        raise ValueError("off mode must omit custom letter wire")
    tracks = block.get("tracks")
    if not isinstance(tracks, (list, tuple)):
        raise ValueError("custom tracks must be an ordered array")
    selection = {}
    for track in tracks:
        if not isinstance(track, dict):
            raise ValueError("custom track entry must be an object")
        slot = track.get("slot")
        custom_check_code("letter_location", slot)
        if slot not in admitted or slot in selection:
            raise ValueError("unadmitted or duplicate custom slot")
        locations, items = track.get("locations"), track.get("items")
        for codes in (locations, items):
            if not isinstance(codes, (list, tuple)) or len(codes) != 3 or any(type(code) is not int for code in codes):
                raise ValueError("custom letter arrays require three integer codes")
        source = items if mode == 3 else locations
        selection[slot] = tuple(letter for letter, code in zip(LETTERS, source) if code >= 0)
    canonical = custom_letter_wire(mode, count, selection)
    normalized = {"version": block["version"], "mode": mode,
                  "letters_per_track": count,
                  "tracks": [{"slot": t["slot"], "locations": list(t["locations"]),
                              "items": list(t["items"])} for t in tracks]}
    if set(selection) != set(admitted) or canonical != normalized:
        raise ValueError("custom wire differs from admitted canonical identities")
    return selection


def restore_custom_letter_options(options, slot_data, validated_slots):
    """Atomically restore the child only against validated parent slots.

    No RNG is consumed. Failed validation leaves the caller's existing state
    untouched; an absent child clears stale letter selection from older seeds.
    """
    admitted = admitted_custom_ctr_wire_slots(slot_data)
    if not set(admitted).issubset(set(validated_slots)):
        raise ValueError("custom CTR mode references an invalid parent descriptor")
    block = slot_data.get("custom_lettersanity_checks")
    if block is None:
        if admitted and int(options.lettersanity.value) != 0:
            raise ValueError("admitted custom CTR mode is missing its letter block")
        selection = {}
    else:
        selection = restore_custom_letter_wire(block, admitted)
        if block["mode"] != int(options.lettersanity.value):
            raise ValueError("custom letter mode differs from seed lettersanity")
        if block["letters_per_track"] != int(options.letters_per_track.value):
            raise ValueError("custom letter count differs from seed lettersanity")
    options._custom_ctr_admitted_slots = admitted
    options._custom_lettersanity_selected = selection


class CustomLettersanityLocationClass(LocationClass):
    key = "custom_lettersanity"
    display_name = "Custom Track Lettersanity"
    code_blocks = (35020000,)

    def all_locations(self):
        return [(custom_check_name("letter_location", slot, li),
                 custom_check_code("letter_location", slot, li),
                 f"Custom Track {slot}")
                for slot in range(1, CUSTOM_SLOT_COUNT + 1)
                for li in range(len(LETTERS))]

    def location_name(self, slot, letter):
        return custom_check_name("letter_location", slot, LETTERS.index(letter))

    def created_location_names(self, options):
        block = self.wire_block(options)
        if block is None or block["mode"] not in (1, 2):
            return []
        return [self.location_name(track["slot"], letter)
                for track in block["tracks"]
                for letter, code in zip(LETTERS, track["locations"]) if code >= 0]

    def wire_block(self, options):
        # Admission belongs to the pinned CTR-mode resolver. Asset flags or
        # raw custom YAML alone cannot populate either of these owned fields.
        if options is None or not hasattr(options, "_custom_ctr_admitted_slots"):
            return None
        mode = int(options.lettersanity.value)
        count = int(options.letters_per_track.value)
        selected = getattr(options, "_custom_lettersanity_selected", {})
        block = custom_letter_wire(mode, count, selected)
        if block is not None:
            restore_custom_letter_wire(block, options._custom_ctr_admitted_slots)
        elif options._custom_ctr_admitted_slots and selected:
            raise ValueError("off mode cannot create custom letters")
        return block

    def created_item_names(self, options):
        block = self.wire_block(options)
        if block is None:
            return []
        return [custom_check_name("letter_item", track["slot"], li)
                for track in block["tracks"]
                for li, code in enumerate(track["items"]) if code >= 0]


CUSTOM_LETTERSANITY_CLASS = CustomLettersanityLocationClass()


class CustomCtrChallengeLocationClass(LocationClass):
    key = "custom_ctr_challenge"
    display_name = "Custom Track CTR Challenges"
    code_blocks = (35023000, 35023100)

    def all_locations(self):
        return [(custom_check_name("ctr_location", slot),
                 custom_check_code("ctr_location", slot), f"Custom Track {slot}")
                for slot in range(1, CUSTOM_SLOT_COUNT + 1)]

    def created_location_names(self, options):
        return [custom_check_name("ctr_location", slot)
                for slot in getattr(options, "_custom_ctr_admitted_slots", ())]


CUSTOM_CTR_CHALLENGE_CLASS = CustomCtrChallengeLocationClass()
