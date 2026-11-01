"""Starting Roster: with vanilla_eight the slot holds the eight retail
Adventure racers from the start, so the retail character select offers them
all, and only the other eight racers are items."""
import unittest

from test.general import setup_multiworld

from .. import ctrAPWorld
from ..characters import ADVENTURE_STARTERS, ROSTER

SEEDS = (1, 2, 3, 7, 11)
OTHERS = set(ROSTER) - set(ADVENTURE_STARTERS)


def _build(seed=1, **options):
    options.setdefault("starting_roster", "vanilla_eight")
    return setup_multiworld(ctrAPWorld, seed=seed, options=options)


def _from_pool(mw):
    """Racers the world starts from the pool. The world creates only the
    copies left in the pool and keeps the starts it took in
    ctr_content_original_starts; setup_multiworld stops before Main.main
    hands them out, so they are not precollected yet."""
    starts = mw.worlds[1].ctr_content_original_starts
    return {name for name, count in starts.items() if count and name in ROSTER}


def _pooled_racers(mw):
    return [i.name for i in mw.itempool if i.player == 1 and i.name in ROSTER]


def _held_racers(mw):
    """The starting racer (slot data, not an item) and the racers started from
    the pool."""
    return {mw.worlds[1].ctr_starting_character} | _from_pool(mw)


class TestVanillaEight(unittest.TestCase):

    def test_the_eight_starters_are_held_and_only_the_others_are_pooled(self):
        for seed in SEEDS:
            with self.subTest(seed=seed):
                mw = _build(seed)
                self.assertEqual(_held_racers(mw), set(ADVENTURE_STARTERS))
                self.assertEqual(sorted(_pooled_racers(mw)), sorted(OTHERS))

    def test_a_non_starter_starting_racer_is_held_too(self):
        mw = _build(1, starting_character="nitros_oxide")
        self.assertEqual(_held_racers(mw), set(ADVENTURE_STARTERS) | {"Nitros Oxide"})
        self.assertEqual(sorted(_pooled_racers(mw)), sorted(OTHERS - {"Nitros Oxide"}))

    def test_remove_playable_oxide_leaves_seven_items(self):
        mw = _build(1, remove_playable_oxide=True)
        self.assertEqual(sorted(_pooled_racers(mw)), sorted(OTHERS - {"Nitros Oxide"}))

    def test_racer_locks_never_name_a_held_racer(self):
        for seed in SEEDS:
            with self.subTest(seed=seed):
                world = _build(seed, racer_locked_pads=12).worlds[1]
                locked = set(world.ctr_racer_locks.values())
                self.assertTrue(locked)
                self.assertTrue(locked <= OTHERS, locked - OTHERS)

    def test_character_unlocks_off_holds_nothing_extra(self):
        mw = _build(1, character_unlocks=False)
        self.assertEqual(_pooled_racers(mw), [])
        self.assertLessEqual(len(_held_racers(mw)), 1)
