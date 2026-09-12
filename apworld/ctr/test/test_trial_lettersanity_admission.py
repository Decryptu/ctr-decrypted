"""Admission invariant for the upcoming trial lettersanity extension."""
import unittest
from types import SimpleNamespace

from ..lettersanity import LETTER_TRACKS, eligible_letter_tracks
from ..lettersanity import LETTERSANITY_CLASS, ALL_ITEM_NAMES
from .test_lettersanity import _build, _collect_all


class TestTrialLettersanityAdmission(unittest.TestCase):
    def test_ut_rejects_forged_trial_rows_and_counts(self):
        import copy
        world = _build(lettersanity="locations_and_items", letters_per_track=2,
                       slide_coliseum_races=2).worlds[1]
        wire = world.fill_slot_data()
        before = copy.deepcopy(world.options._lettersanity_selected)
        for row in ([35012500, -1, -1], [35012548, -1, -1],
                    [35012548, 35012549], [True, 35012549, -1],
                    [35012548, 35012549, 35012550]):
            with self.subTest(row=row):
                bad = copy.deepcopy(wire)
                bad["lettersanity_checks"]["locations"]["16"] = row
                with self.assertRaises(ValueError):
                    world._ut_restore_options(bad)
                self.assertEqual(world.options._lettersanity_selected, before)
        world._ut_restore_options(wire)
        self.assertEqual({k: set(v) for k, v in world.options._lettersanity_selected.items()},
                         {k: set(v) for k, v in before.items()})

    def test_sizer_counts_activated_trial_and_retail_letter_items(self):
        from .. import rung_sizer
        baseline = None
        for mode, expected in (("off", 0), ("locations_and_items", 36), ("items_only", 54)):
            world = _build(lettersanity=mode, letters_per_track=2,
                           slide_coliseum_races=2, turbo_track_races=2).worlds[1]
            predicted = rung_sizer.predicted_mandatory_pool(world)
            if baseline is None:
                baseline = predicted
            self.assertEqual(predicted - baseline, expected)

    def test_sizer_counts_custom_demand_before_regions_without_rng(self):
        import copy
        from .. import rung_sizer
        from ..custom_tracks import BABY_T_PARK_EXAMPLE
        from .. import ctrAPWorld
        from test.general import setup_multiworld
        descriptor = copy.deepcopy(BABY_T_PARK_EXAMPLE)
        descriptor["modes"] = {"ctr_challenge": True}
        for mode, letters in (("off", 0), ("locations_only", 0),
                              ("locations_and_items", 2), ("items_only", 3)):
            with self.subTest(mode=mode):
                mw = setup_multiworld(ctrAPWorld, ("generate_early",), seed=148,
                                      options={"lettersanity": mode, "letters_per_track": 2,
                                               "custom_tracks": {"baby-t-park": descriptor}})
                world = mw.worlds[1]
                rng = world.random.getstate()
                demand = rung_sizer.predicted_mandatory_pool(world)
                supply = rung_sizer._base_location_supply(world)
                old = world.options.custom_tracks.value
                disabled = copy.deepcopy(old)
                disabled["baby-t-park"]["modes"]["ctr_challenge"] = False
                world.options.custom_tracks.value = disabled
                self.assertEqual(demand - rung_sizer.predicted_mandatory_pool(world), letters)
                expected_supply = 1 + (2 if mode in ("locations_only", "locations_and_items") else 0)
                self.assertEqual(supply - rung_sizer._base_location_supply(world), expected_supply)
                world.options.custom_tracks.value = old
                self.assertEqual(world.random.getstate(), rng)

    def test_generated_trial_shapes(self):
        for mode in ("off", "locations_only", "locations_and_items", "items_only"):
            for count in (1, 2, 3):
                with self.subTest(mode=mode, count=count):
                    mw = _build(lettersanity=mode, letters_per_track=count,
                                slide_coliseum_races=2, turbo_track_races=1)
                    world = mw.worlds[1]
                    locations = [loc for loc in mw.get_locations(1)
                                 if loc.name.startswith("Slide Coliseum: Letter ")]
                    items = [item for item in mw.itempool
                             if item.name in ALL_ITEM_NAMES and
                             item.name.endswith("(Slide Coliseum)")]
                    self.assertEqual(len(locations), count if mode in
                                     ("locations_only", "locations_and_items") else 0)
                    self.assertEqual(len(items), count if mode == "locations_and_items"
                                     else 3 if mode == "items_only" else 0)
                    self.assertFalse(any(loc.name.startswith("Turbo Track: Letter ")
                                         for loc in mw.get_locations(1)))
                    self.assertFalse(any(item.name.endswith("(Turbo Track)") and
                                         item.name in ALL_ITEM_NAMES for item in mw.itempool))
                    if mode != "off":
                        wire = world.fill_slot_data()["lettersanity_checks"]["locations"]
                        self.assertIn("16", wire)
                        self.assertNotIn("17", wire)

    def test_trial_location_append_codes(self):
        for ti, track in enumerate(("Slide Coliseum", "Turbo Track")):
            for li, letter in enumerate(("C", "T", "R")):
                self.assertEqual(LETTERSANITY_CLASS.code_for(track, letter),
                                 35012548 + ti * 3 + li)

    def test_trial_token_and_pickup_require_selected_received_items(self):
        mw = _build(lettersanity="locations_and_items", letters_per_track=2,
                    slide_coliseum_races=2)
        world = mw.worlds[1]
        selected = world.options._lettersanity_selected["Slide Coliseum"]
        token = mw.get_location("Slide Coliseum: CTR Token Challenge", 1)
        full = _collect_all(mw)
        self.assertTrue(token.access_rule(full))
        for letter in selected:
            item = f"Letter {letter} (Slide Coliseum)"
            missing = _collect_all(mw, exclude=item)
            self.assertFalse(token.access_rule(missing))
            pickup = mw.get_location(f"Slide Coliseum: Letter {letter}", 1)
            self.assertFalse(pickup.access_rule(missing))
            self.assertTrue(pickup.access_rule(full))

    def test_independent_trial_options_and_frozen_prefix(self):
        for slide in range(3):
            for turbo in range(3):
                with self.subTest(slide=slide, turbo=turbo):
                    options = SimpleNamespace(
                        slide_coliseum_races=SimpleNamespace(value=slide),
                        turbo_track_races=SimpleNamespace(value=turbo))
                    expected = tuple(LETTER_TRACKS)
                    if slide == 2:
                        expected += ("Slide Coliseum",)
                    if turbo == 2:
                        expected += ("Turbo Track",)
                    admitted = eligible_letter_tracks(options)
                    self.assertEqual(admitted, expected)
                    self.assertEqual(admitted[:16], tuple(LETTER_TRACKS))

    def test_old_options_do_not_admit_trials(self):
        self.assertEqual(eligible_letter_tracks(None), tuple(LETTER_TRACKS))
        self.assertEqual(eligible_letter_tracks(SimpleNamespace()),
                         tuple(LETTER_TRACKS))
