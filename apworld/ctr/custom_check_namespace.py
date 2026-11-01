"""Approved additive custom-check reservations.

These helpers do not register names or create locations. A registered family
must expose its complete name map regardless of seed options; activation is a
separate capability- and mode-dependent decision. Identity belongs to the
seed's logical slot, never its borrowed retail LevelID or package filename.
"""

CUSTOM_SLOT_COUNT = 132
LETTERS = ("C", "T", "R")
RELIC_TIERS = ("Sapphire", "Gold", "Platinum")

# (base, stride): append-only, approved September 12, 2026.
CUSTOM_FAMILIES = {
    "letter_location": (35020000, 3),
    "letter_item": (35021000, 3),
    "relic_location": (35022000, 3),
    "ctr_location": (35023000, 1),
    "perfect_location": (35024000, 1),
}


def custom_check_code(family: str, slot: int, variant: int = 0) -> int:
    """Return a reserved code; reject invalid identities rather than aliasing."""
    base, stride = CUSTOM_FAMILIES[family]
    if type(slot) is not int or not 1 <= slot <= CUSTOM_SLOT_COUNT:
        raise ValueError("custom slot must be an integer in 1..132")
    if type(variant) is not int or not 0 <= variant < stride:
        raise ValueError("variant is outside the reserved family stride")
    return base + (slot - 1) * stride + variant


def custom_check_name(family: str, slot: int, variant: int = 0) -> str:
    """Immutable datapackage spelling, not the resolved user-facing title."""
    custom_check_code(family, slot, variant)
    track = f"Custom Track {slot}"
    if family == "letter_item":
        return f"Letter {LETTERS[variant]} ({track})"
    if family == "letter_location":
        return f"{track}: Letter {LETTERS[variant]}"
    if family == "relic_location":
        return f"{track}: {RELIC_TIERS[variant]} Time Trial"
    if family == "ctr_location":
        return f"{track}: CTR Token Challenge"
    return f"{track}: Relic Race Perfect"
