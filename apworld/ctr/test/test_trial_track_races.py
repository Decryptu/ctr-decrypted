"""Generation, identity and wire gates for issue #203 trial-track races."""
import json

from ..trial_trophy import TRIAL_TROPHY_CLASS
from . import CTRTestBase


class TestTrialTrackRacesOff(CTRTestBase):
    def test_default_is_inert(self):
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertFalse(set(TRIAL_TROPHY_CLASS.names()) & names)
        wire = self.world.fill_slot_data()
        self.assertNotIn("trial_track_checks", wire)
        self.assertEqual(wire["schema_version"], 10)
        self.assertEqual(wire["ctr_options"]["slide_coliseum_races"], 0)
        self.assertEqual(wire["ctr_options"]["turbo_track_races"], 0)


class TestSlideTrophyOnly(CTRTestBase):
    options = {"slide_coliseum_races": "trophy_race"}

    def test_only_slide_trophy_is_created(self):
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertIn("Slide Coliseum: Trophy Race", names)
        self.assertNotIn("Slide Coliseum: CTR Token Challenge", names)
        self.assertNotIn("Turbo Track: Trophy Race", names)
        self.assertEqual(self.world.fill_slot_data()["trial_track_checks"], {
            "enabled": True,
            "locations": {"16": [35016200, -1], "17": [-1, -1]},
        })


class TestBothFamilies(CTRTestBase):
    options = {
        "slide_coliseum_races": "trophy_and_ctr_challenge",
        "turbo_track_races": "trophy_and_ctr_challenge",
        "wumpa_check": "per_track",
    }

    def test_both_tracks_create_four_checks_and_two_wumpa_routes(self):
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertTrue(set(TRIAL_TROPHY_CLASS.names()) <= names)
        self.assertIn("Slide Coliseum: Reach 10 Wumpa", names)
        self.assertIn("Turbo Track: Reach 10 Wumpa", names)
        wire = json.loads(json.dumps(self.world.fill_slot_data()))
        self.assertEqual(wire["trial_track_checks"]["locations"], {
            "16": [35016200, 35016210],
            "17": [35016201, 35016211],
        })
        self.assertIn("16", wire["wumpa_checks"]["retail_tracks"])
        self.assertIn("17", wire["wumpa_checks"]["retail_tracks"])

    def test_universal_tracker_restores_both_options(self):
        wire = self.world.fill_slot_data()
        self.world.options.slide_coliseum_races.value = 0
        self.world.options.turbo_track_races.value = 0
        self.world._ut_restore_options(wire)
        self.assertEqual(self.world.options.slide_coliseum_races.value, 2)
        self.assertEqual(self.world.options.turbo_track_races.value, 2)
