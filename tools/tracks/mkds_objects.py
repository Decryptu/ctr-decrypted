"""What each object id of a Mario Kart DS course is, as the game's own tables say (docs/TRACK-CONVERTER.md#objects).

Models come from the object table in the game's code, each id's definition naming its loaders; shapes from
MapObj/grpconf.tbl in data/MainRace.carc, the game's list of hit shapes. An id's model is never inferred
from what a course carries: `check` fails a conversion that draws anything else.

shape: none, sphere (a radius), cylinder (a radius, b half its height), box (half sizes a, b, c), custom (the
object's own test, a, b, c its reach). Sizes are in world units, before the object's scale.
"""
from collections import namedtuple

Object = namedtuple('Object', 'name models shape a b c')
OBJECTS = {
    0x1: Object('water plane', ('beach_waterA', 'beach_waterC'), 'none', 0, 0, 0),
    0x2: Object('per-cc value point', (), 'none', 0, 0, 0),
    0x3: Object('water plane', ('town_waterA', 'town_waterC'), 'none', 0, 0, 0),
    0x5: Object('water splash effect', (), 'none', 0, 0, 0),
    0x6: Object('water plane', ('yoshi_waterC',), 'none', 0, 0, 0),
    0x8: Object('course ambient emitter', (), 'none', 0, 0, 0),
    0x9: Object('water plane', ('hyudoro_waterA', 'hyudoro_waterC'), 'none', 0, 0, 0),
    0xa: Object('ambient sound point', (), 'none', 0, 0, 0),
    0xd: Object('puddle', ('puddle',), 'cylinder', 15, 1, 0),
    0xe: Object('water splash effect', (), 'none', 0, 0, 0),
    0xf: Object('water splash effect', (), 'none', 0, 0, 0),
    0x65: Object('item box', ('box', 'question'), 'sphere', 10, 0, 0),
    0x66: Object('sound trigger', (), 'cylinder', 10, 10, 0),
    0x67: Object('wood box', ('woodbox1',), 'box', 10, 10, 10),
    0xc9: Object('moving item box', ('box', 'question'), 'sphere', 15, 0, 0),
    0xca: Object('koopa_block', ('koopa_block',), 'none', 0, 0, 0),
    0xcb: Object('cog', ('gear_black', 'gear_white'), 'custom', 300, 50, 0),
    0xcc: Object('bridge', ('bridge',), 'none', 0, 0, 0),
    0xcd: Object('rotating hand', ('second_hand',), 'custom', 15, 75, 800),
    0xce: Object('rotating cylinder', ('test_cylinder',), 'custom', 300, 50, 0),
    0xcf: Object('pendulum', ('pendulum',), 'custom', 112, 0, 0),
    0xd0: Object('rotary_room', ('rotary_room',), 'custom', 472, 25, 0),
    0xd1: Object('rotary_bridge', ('rotary_bridge',), 'custom', 80, 2150, 0),
    0xd2: Object('drum', ('dram',), 'none', 0, 0, 0),
    0x12e: Object('tree with trunk', ('BeachTree1',), 'cylinder', 6, 80, 0),
    0x12f: Object('warp pipe', ('earthen_pipe1', 'earthen_pipe2'), 'cylinder', 11, 12, 0),
    0x130: Object('tree with trunk', ('opa_tree1',), 'cylinder', 6, 40, 0),
    0x131: Object('scenery', ('OlgPipe1',), 'none', 0, 0, 0),
    0x132: Object('scenery', ('OlgMush1',), 'none', 0, 0, 0),
    0x133: Object('scenery', ('of6yoshi1',), 'none', 0, 0, 0),
    0x134: Object('cow', ('cow',), 'none', 0, 0, 0),
    0x135: Object('scenery', ('NsCannon1',), 'none', 0, 0, 0),
    0x138: Object('tree with trunk', ('GardenTree1',), 'cylinder', 5, 100, 0),
    0x139: Object('not instantiated', (), 'none', 0, 0, 0),
    0x13a: Object('scenery tree', ('CrossTree1',), 'none', 0, 0, 0),
    0x13b: Object('teresa (spawned by 0x13d)', ('teresa',), 'none', 0, 0, 0),
    0x13c: Object('bakubaku', ('bakubaku',), 'none', 0, 0, 0),
    0x13d: Object('teresa group', ('teresa',), 'none', 0, 0, 0),
    0x13e: Object('scenery tree', ('Bank_Tree1',), 'none', 0, 0, 0),
    0x13f: Object('scenery tree', ('GardenTree1',), 'none', 0, 0, 0),
    0x140: Object('chandelier', ('chandelier',), 'none', 0, 0, 0),
    0x142: Object('scenery tree', ('MarioTree3',), 'none', 0, 0, 0),
    0x145: Object('tree with trunk', ('TownTree1',), 'cylinder', 7, 100, 0),
    0x146: Object('tree with trunk', ('Snow_Tree1',), 'cylinder', 5, 100, 0),
    0x148: Object('tree with trunk', ('DeTree1',), 'cylinder', 5, 100, 0),
    0x149: Object('scenery', ('BankEgg1',), 'none', 0, 0, 0),
    0x14b: Object('scenery', ('KinoHouse1',), 'none', 0, 0, 0),
    0x14c: Object('scenery', ('KinoHouse2',), 'none', 0, 0, 0),
    0x14d: Object('scenery', ('KinoMount1',), 'none', 0, 0, 0),
    0x14e: Object('scenery', ('KinoMount2',), 'none', 0, 0, 0),
    0x14f: Object('tree with trunk', ('olaTree1c',), 'cylinder', 8, 100, 0),
    0x150: Object('tree with trunk', ('osaTree1c',), 'cylinder', 12, 100, 0),
    0x151: Object('painting', ('picture1',), 'none', 0, 0, 0),
    0x152: Object('painting', ('picture2',), 'none', 0, 0, 0),
    0x153: Object('scenery tree', ('om6Tree1',), 'none', 0, 0, 0),
    0x154: Object('RainStar', ('RainStar',), 'none', 0, 0, 0),
    0x155: Object('scenery tree', ('Of6Tree1',), 'none', 0, 0, 0),
    0x156: Object('tree with trunk', ('Of6Tree1',), 'cylinder', 5, 100, 0),
    0x157: Object('TownMonte', ('TownMonte',), 'none', 0, 0, 0),
    0x158: Object('airship', ('airship',), 'none', 0, 0, 0),
    0x191: Object('kuribo', ('kuribo',), 'cylinder', 8, 10, 0),
    0x192: Object('rock', ('rock',), 'sphere', 27, 0, 0),
    0x193: Object('dossun', ('dossun',), 'cylinder', 36, 45, 0),
    0x195: Object('bus', ('bus_a', 'car_tire'), 'custom', 76, 0, 0),
    0x196: Object('wanwan, staked', ('wanwan', 'wanwan_chain', 'wanwan_kui'), 'sphere', 27, 0, 0),
    0x198: Object('lava bubble', ('mkd_ef_bubble',), 'sphere', 15, 0, 0),
    0x199: Object('choropu', ('choropu',), 'sphere', 9, 0, 0),
    0x19a: Object('car', ('car_a', 'car_tire'), 'custom', 25, 0, 0),
    0x19b: Object('pukupuku', ('pukupuku',), 'sphere', 10, 0, 0),
    0x19c: Object('truck', ('truck_a', 'car_tire'), 'custom', 50, 0, 0),
    0x19d: Object('snowman', ('sman_bottom', 'sman_top'), 'cylinder', 4, 15, 0),
    0x19e: Object('coffin', ('kanoke_64', 'basabasa'), 'box', 50, 40, 25),
    0x19f: Object('basabasa (spawned by 0x1a0)', ('basabasa',), 'sphere', 8, 0, 0),
    0x1a0: Object('basabasa group', ('basabasa',), 'none', 0, 0, 0),
    0x1a1: Object('Bullet cannon', ('NsKiller1', 'NsKiller2', 'NsKiller2_s'), 'none', 0, 0, 0),
    0x1a2: Object('Bullet (spawned by 0x1a1)', ('NsKiller2', 'NsKiller2_s'), 'sphere', 15, 0, 0),
    0x1a3: Object('move_tree', ('move_tree',), 'cylinder', 25, 280, 0),
    0x1a4: Object('burner', ('mkd_ef_burner',), 'cylinder', 15, 80, 0),
    0x1a5: Object('wanwan, on path', ('wanwan', 'wanwan_chain'), 'sphere', 27, 0, 0),
    0x1a6: Object('ob_pakkun_sf', ('ob_pakkun_sf',), 'cylinder', 8, 18, 0),
    0x1a7: Object('poo', ('poo', 'cover', 'hole'), 'custom', 12, 15, 0),
    0x1a8: Object('bound', ('bound',), 'custom', 10, 20, 0),
    0x1a9: Object('flipper', ('flipper',), 'custom', 200, 0, 0),
    0x1aa: Object('Pakkun', ('PakkunBody', 'PakkunZHead', 'PakkunMouth'), 'none', 0, 0, 0),
    0x1ab: Object('Pakkun shot (spawned by 0x1aa)', (), 'sphere', 15, 0, 0),
    0x1ac: Object('crab', ('crab', 'crab_hand'), 'sphere', 6, 0, 0),
    0x1ad: Object('sun', ('sun', 'sun_LOD'), 'none', 0, 0, 0),
    0x1af: Object('fireball2', ('fireball2',), 'custom', 200, 0, 0),
    0x1b0: Object('IronBall', ('IronBall',), 'sphere', 10, 0, 0),
    0x1b1: Object('rock, Choco Mountain', ('rock2',), 'sphere', 27, 0, 0),
    0x1b2: Object('sanbo', ('sanbo_h', 'sanbo_b'), 'cylinder', 10, 80, 0),
    0x1b3: Object('IronBall source', ('IronBall',), 'none', 0, 0, 0),
}

# What a kart in its normal state gets from touching each object: the game's reaction table (0x216b96c in the
# European ROM's ARM9, a row a hundred ids, a column a kart state), the ids it gives one. mkds_movers.REACTIONS
# says what each is here.
REACTION = {
    0xd: 6, 0x66: 2, 0x67: 2, 0xca: 2, 0xcb: 2, 0xcc: 2, 0xcd: 4, 0xce: 2, 0xcf: 8, 0xd0: 2, 0xd1: 2, 0xd2: 2, 0x12e: 2, 0x12f: 2,
    0x130: 2, 0x138: 2, 0x145: 2, 0x146: 2, 0x148: 2, 0x14f: 2, 0x150: 2, 0x156: 2, 0x191: 4, 0x192: 10, 0x193: 2, 0x195: 9,
    0x196: 10, 0x197: 2, 0x198: 5, 0x199: 7, 0x19a: 9, 0x19b: 4, 0x19c: 9, 0x19d: 10, 0x19e: 2, 0x19f: 12, 0x1a2: 10, 0x1a3: 2,
    0x1a4: 5, 0x1a5: 10, 0x1a6: 4, 0x1a7: 7, 0x1a8: 12, 0x1a9: 2, 0x1ab: 5, 0x1ac: 4, 0x1ae: 5, 0x1af: 5, 0x1b0: 10, 0x1b1: 10,
    0x1b2: 9,
}

# The same table's column for a kart with a Star, CTR's mask (0x216b96c, column 1): 2 a wall, 12 a bounce, 1 the
# kart rams through; an id not here lets it through untouched. Any other column-0 reaction does nothing to it.
STAR_REACTION = {
    0x66: 2, 0x67: 1, 0xca: 2, 0xcb: 2, 0xcc: 2, 0xce: 2, 0xcf: 2, 0xd0: 2, 0xd1: 2, 0xd2: 2, 0x12e: 2, 0x12f: 1, 0x130: 2,
    0x138: 2, 0x145: 2, 0x146: 2, 0x148: 2, 0x14f: 2, 0x150: 2, 0x156: 2, 0x192: 1, 0x193: 1, 0x195: 1, 0x196: 1, 0x197: 2,
    0x19a: 1, 0x19c: 1, 0x19e: 2, 0x1a2: 1, 0x1a3: 2, 0x1a5: 1, 0x1a8: 12, 0x1a9: 2, 0x1b0: 1, 0x1b1: 1,
}

# The objects a kart with a Star strikes: the second reaction table (0x216b9cc, column 1) holds 1 for them, and
# their kart callback then plays their struck reaction (mkds_movers.STRUCK says which).
STRUCK = {0x65, 0xc9, 0x12f, 0x191, 0x192, 0x193, 0x195, 0x196, 0x19a, 0x19b, 0x19c, 0x19d, 0x1a2, 0x1a5, 0x1a6, 0x1ab, 0x1ac,
          0x1ae, 0x1b0, 0x1b1, 0x1b2}


def check(object_id, model):
    """Stops a conversion that would draw a model the game does not give this id."""
    known = OBJECTS.get(object_id)
    if known is None or model not in known.models:
        raise ValueError(f'object {object_id:#x} is drawn as {model}, and the game gives it {known.models if known else "nothing"}')
