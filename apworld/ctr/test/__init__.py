from test.bases import WorldTestBase

from ..Options import StartingRoster

# The suite (and every test upstream adds) counts 15 unlock items, the
# one-racer roster. Tests of the eight-racer roster, the player default, ask
# for it explicitly (test_starting_roster.py).
StartingRoster.default = StartingRoster.option_starting_character_only


class CTRTestBase(WorldTestBase):
    game = "Crash Team Racing"
    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        from .plan_options import plan_options
        if "options" in cls.__dict__:
            cls.options = plan_options(cls.options)
