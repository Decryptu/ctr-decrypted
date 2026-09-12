import unittest
import random
import copy
from types import SimpleNamespace

from ..custom_lettersanity import CUSTOM_LETTERSANITY_CLASS
from ..custom_lettersanity import CUSTOM_LETTER_ITEM_DATA
from ..custom_lettersanity import (select_custom_letters, custom_letter_wire,
                                  restore_custom_letter_wire, admitted_custom_ctr_wire_slots)
from ..custom_lettersanity import restore_custom_letter_options
from .. import ctrAPWorld
from ..Items import load_item_table


class TestCustomLetterIdentities(unittest.TestCase):
    def test_restore_options_is_exact_and_atomic(self):
        options = SimpleNamespace(lettersanity=SimpleNamespace(value=2),
                                  letters_per_track=SimpleNamespace(value=2))
        doc = {"custom_tracks": {"enabled": True, "tracks": [{
            "slot": 1, "modes": {"ctr_challenge": True},
            "flags": {"ctr_letters": True}, "locations": {"ctr": 35023000}}]},
            "custom_lettersanity_checks": custom_letter_wire(2, 2, {1: ("C", "R")})}
        restore_custom_letter_options(options, doc, (1,))
        self.assertEqual(options._custom_lettersanity_selected, {1: ("C", "R")})
        before = copy.deepcopy(vars(options))
        for valid_slots, bad in (((), doc), ((1,), {**doc, "custom_lettersanity_checks":
                                  custom_letter_wire(1, 2, {1: ("C", "R")})}),
                                 ((1,), {"custom_tracks": doc["custom_tracks"]})):
            with self.assertRaises(ValueError):
                restore_custom_letter_options(options, bad, valid_slots)
            self.assertEqual(options._custom_lettersanity_selected,
                             before["_custom_lettersanity_selected"])
            self.assertEqual(options._custom_ctr_admitted_slots,
                             before["_custom_ctr_admitted_slots"])
        restore_custom_letter_options(options, {}, ())
        self.assertEqual(options._custom_ctr_admitted_slots, ())
        self.assertEqual(options._custom_lettersanity_selected, {})

    def test_explicit_parent_mode_admission(self):
        track = {"slot": 132, "modes": {"ctr_challenge": True},
                 "flags": {"ctr_letters": True}, "locations": {"ctr": 35023131}}
        doc = {"custom_tracks": {"enabled": True, "tracks": [track]}}
        self.assertEqual(admitted_custom_ctr_wire_slots(doc), (132,))
        for fault in range(5):
            bad = copy.deepcopy(doc)
            row = bad["custom_tracks"]["tracks"][0]
            if fault == 0:
                row["locations"]["ctr"] = 35012300
            elif fault == 1:
                row["modes"]["ctr_challenge"] = 1
            elif fault == 2:
                row["flags"]["ctr_letters"] = False
            elif fault == 3:
                row["slot"] = True
            else:
                bad["custom_tracks"]["tracks"].append(copy.deepcopy(row))
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                admitted_custom_ctr_wire_slots(bad)
        track.pop("modes")
        self.assertEqual(admitted_custom_ctr_wire_slots(doc), ())

    def test_created_subset_follows_owned_admission_and_exact_selection(self):
        for mode in range(4):
            options = SimpleNamespace(
                lettersanity=SimpleNamespace(value=mode),
                letters_per_track=SimpleNamespace(value=2),
                _custom_ctr_admitted_slots=(1, 132),
                _custom_lettersanity_selected=select_custom_letters(
                    mode, 2, (1, 132), random.Random(42)))
            checks = CUSTOM_LETTERSANITY_CLASS.created_location_names(options)
            items = CUSTOM_LETTERSANITY_CLASS.created_item_names(options)
            self.assertEqual(len(checks), 4 if mode in (1, 2) else 0)
            self.assertEqual(len(items), 4 if mode == 2 else 6 if mode == 3 else 0)
            self.assertFalse(any("Crash Cove" in name for name in checks + items))
        options._custom_ctr_admitted_slots = (2,)
        with self.assertRaises(ValueError):
            CUSTOM_LETTERSANITY_CLASS.created_location_names(options)

    def test_selection_wire_round_trip_all_shapes(self):
        for mode in range(4):
            for count in (1, 2, 3):
                with self.subTest(mode=mode, count=count):
                    selected = select_custom_letters(mode, count, [132, 1, 7], random.Random(42))
                    self.assertEqual(selected, select_custom_letters(
                        mode, count, [7, 132, 1], random.Random(42)))
                    wire = custom_letter_wire(mode, count, selected)
                    if mode == 0:
                        self.assertIsNone(wire)
                        self.assertEqual(selected, {})
                    else:
                        self.assertEqual(restore_custom_letter_wire(wire, [1, 7, 132]), selected)
                        self.assertEqual([t["slot"] for t in wire["tracks"]], [1, 7, 132])
                        for track in wire["tracks"]:
                            self.assertEqual(sum(code >= 0 for code in track["locations"]),
                                             count if mode in (1, 2) else 0)
                            self.assertEqual(sum(code >= 0 for code in track["items"]),
                                             count if mode == 2 else 3 if mode == 3 else 0)

    def test_wire_rejects_host_aliases_and_forged_receipts(self):
        good = custom_letter_wire(2, 3, {1: ("C", "T", "R")})
        for field, value in (("slot", 0), ("slot", True),
                             ("locations", [35012500, 35012501, 35012502]),
                             ("items", [35010139, 35010140, 35010141]),
                             ("items", [True, 35021001, 35021002]),
                             ("items", [35021000, 35021001])):
            bad = copy.deepcopy(good)
            bad["tracks"][0][field] = value
            with self.subTest(field=field, value=value):
                with self.assertRaises(ValueError):
                    restore_custom_letter_wire(bad, [1])
        bad = copy.deepcopy(good)
        bad["tracks"].append(copy.deepcopy(bad["tracks"][0]))
        with self.assertRaises(ValueError):
            restore_custom_letter_wire(bad, [1])
        with self.assertRaises(ValueError):
            restore_custom_letter_wire(good, [2])
        with self.assertRaises(ValueError):
            restore_custom_letter_wire(good, [1, 2])

    def test_separate_item_map_preserves_positional_table(self):
        self.assertEqual(len(CUSTOM_LETTER_ITEM_DATA), 396)
        old = load_item_table()
        self.assertEqual([item["code"] for item in old],
                         list(range(35010000, 35010200)))
        self.assertFalse({item["code"] for item in old}.intersection(
            item["code"] for item in CUSTOM_LETTER_ITEM_DATA))
        self.assertFalse({item["name"] for item in old}.intersection(
            item["name"] for item in CUSTOM_LETTER_ITEM_DATA))
        for item in CUSTOM_LETTER_ITEM_DATA:
            self.assertEqual(ctrAPWorld.item_name_to_id[item["name"]], item["code"])
            self.assertEqual(item["count"], 0)

    def test_complete_permanent_slot_family(self):
        rows = CUSTOM_LETTERSANITY_CLASS.all_locations()
        self.assertEqual(len(rows), 396)
        self.assertEqual(rows[0], ("Custom Track 1: Letter C", 35020000,
                                   "Custom Track 1"))
        self.assertEqual(rows[-1], ("Custom Track 132: Letter R", 35020395,
                                    "Custom Track 132"))
        self.assertEqual(len({code for _, code, _ in rows}), 396)
        for slot in range(1, 133):
            for li, letter in enumerate(("C", "T", "R")):
                self.assertEqual(CUSTOM_LETTERSANITY_CLASS.code_for(slot, letter),
                                 35020000 + (slot - 1) * 3 + li)

    def test_declared_letters_do_not_auto_activate_mode(self):
        options = SimpleNamespace(custom_tracks=SimpleNamespace(value={
            "baby_t_park": {"flags": {"ctr_letters": True}}}),
            lettersanity=SimpleNamespace(value=2))
        self.assertEqual(CUSTOM_LETTERSANITY_CLASS.created_location_names(options), [])
