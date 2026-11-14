"""Where a Mario Kart DS object is over time, tick by tick, from its update code (docs/TRACK-CONVERTER.md#movers).

Each function of MOTIONS runs an object's state machine and gives one or more Tracks, which mkds_movers
writes as MOVER_KEYS lists for game/ConvertedTrack/Mover.c. Positions are world units and the object's own
origin, as the game has it, so nothing is lifted by a model's foot. None in a track: it is not there.
What the game draws at random is drawn once here, from a seed the object's place gives, and then repeats.
"""
import math, random
from collections import namedtuple

import numpy as np

TICKS = 60  # a second
TURN = 65536  # the game's angles
POINT_SPEED = 0x3000 / 100 / 4096  # world units a tick per unit of a path point's +0xe, for the objects that read it as a speed
LAP_ONCE = 99  # a race never reaches it: what runs once stays gone

# model: None is the object's own; ticks: its place each tick; restart: the tick it runs on from after the
# last; lap_step: laps added to its gates each time it comes round; start: the tick it starts at; grow: its
# scale's (rise a tick, top), as a share of its placed size; gates: (tick, how near a kart must come, the
# lap the race must reach, the point it must come near); hit: its radius, world units: None is the
# object's own, 0 none; step: ticks a key; damage: None is the object's own.
Track = namedtuple('Track', 'ticks model restart lap_step start grow gates hit step damage anim', defaults=(None, 0, 0, 0, None, (), None, 3, None, None))


def plain(p):
    return np.array(p, float)


def along(points, speed_at, loop, ticks=100000):
    """Places a tick along a path, the speed going from one point's to the next's over the way. -> a list of
    (place, the point it left)."""
    out, i, part = [], 0, 0.0
    points = [plain(p) for p in points]
    last = len(points) if loop else len(points) - 1
    while i < last and len(out) < ticks:
        a, b = points[i], points[(i + 1) % len(points)]
        length = max(np.linalg.norm(b - a), 1e-3)
        out.append((a + (b - a) * part, i))
        part += (speed_at(i) + (speed_at((i + 1) % len(points)) - speed_at(i)) * part) / length
        while part >= 1 and i < last:
            part = (part - 1) * length
            i += 1
            if i == last:
                break
            a, b = points[i], points[(i + 1) % len(points)]
            length = max(np.linalg.norm(b - a), 1e-3)
            part /= length
    return out


def point_speeds(path, base):
    """The rule of the objects that read a path point's wait as a speed."""
    return lambda i: path.waits[i] * POINT_SPEED if path.waits[i] else base


def towards(a, b, speed_of):
    """From a to b, each tick's speed from speed_of(tick). -> places, not counting b."""
    a, b = plain(a), plain(b)
    length, done, out = np.linalg.norm(b - a), 0.0, []
    tick = 0
    while done < length:
        out.append(a + (b - a) * done / max(length, 1e-3))
        done += speed_of(tick)
        tick += 1
    return out


def thwomp(m, c):
    """0x193: its path says what it does at each point. Action 1 shakes, hops and drops to the next point,
    action 2 is the floor, where it waits and rises; action 0 waits and moves on."""
    path, s, slow = m['path'], m['size'][1], m['settings'][0]
    if path is None:
        return []
    up, ticks = np.array([0.0, 45.0 * s, 0.0]), []
    n = len(path.points)
    for i in range(n):
        at, to, wait, action = plain(path.points[i]), plain(path.points[(i + 1) % n]), path.waits[i], path.actions[i]
        if action == 1:
            count, phase = wait, 0
            while True:
                ticks.append(at + up + (0.0, math.sin(phase * 2 * math.pi / TURN) * 3 * s, 0.0))
                phase = (phase + 0x1555) % TURN
                count -= 1
                if count < 0 and phase < 0x1555:
                    break
            speed, high = -15.0 * s, 0.0
            while True:
                speed += s
                high -= speed
                if high < 0:
                    break
                ticks.append(at + up + (0.0, high, 0.0))
            ticks += [p + up for p in towards(at, to, lambda t: min(15.0 * s + t, 20.0))]
        elif action == 2:
            ticks += [at + up] * (wait + 1)
            ticks += [p + up for p in towards(at, to, lambda t: 2.0 * s)]
        else:
            ticks += [at + up] * wait
            ticks += [p + up for p in towards(at, to, lambda t: 0.75 if slow else 3.0)]
    return [Track(ticks)]


def rock(m, c):
    """0x192, 0x1b1: rolls down its path, y on the path and bouncing, growing, and starts again from a hop."""
    path, s, st = m['path'], m['size'][1], m['settings']
    if path is None:
        return []
    respawn, first = st[0], st[5]
    size, top, rise = s, 1 + st[4] / 1000, st[3] / 60000
    y, vy, run = float(path.points[0][1]), 8.0, []
    for place, _ in along(path.points, point_speeds(path, st[1] * 3 / 100), False):
        if rise and size < top:
            size += rise
        vy -= 0.25
        y += vy
        floor = place[1] + 30 * size
        if y < floor:
            y = floor
            vy = 0.0 if vy >= -2 else min(-vy * st[2] / 100, 4.0)
        run.append(np.array([place[0], y, place[2]]))
    grow = (rise / s, max(top, s) / s) if rise else None
    return [Track([None] * first + run + [None] * (respawn + 1), restart=first, grow=grow)]


def ball_speed(kind, fast, slow):
    """A ball's speed from a path point's kind; a bumper's (100 to 199) and a flipper's (200 to 299) are
    where it is struck, and slow by BALL_SLOWING a tick from there (0x20a95a8)."""
    if kind < 2:
        return fast
    if kind < 4:
        return slow
    return {10: 8.0, 11: 5.0}.get(kind, 15.0 if kind < 200 else 30.0)


BALL_SLOWING, BALL_SLOWEST = 0.1, 10.0
BALL_LOOPS = 4  # times round a path's last loop a ball's keys hold before they repeat


def ball(m, c):
    """0x1b0: runs its path each time the race's lead reaches a lap, s[3] ticks later. y is on the path; a
    point of kind 10 or 11 drops it to the next point, where it bounces. A path that ends going back to
    an earlier point keeps it there for good, as Waluigi Pinball's table at its end does."""
    path, st, rng = m['path'], m['settings'], c['rng']
    if path is None:
        return []
    fast, slow, n = st[0] * 10 / 100, st[0] * 5 / 100, len(path.points)
    lift, run = 1.0 + c['foot'], []
    i, jump, vjump, part = 0, 0.0, 0.0, 0.0
    speed = ball_speed(path.waits[0], fast, slow)
    loop_at, loops, loop_start = path.actions[n - 1], 0, None
    while len(run) < 20000:
        next_point = i + 1
        if next_point >= n:
            break
        a, b = plain(path.points[i]), plain(path.points[next_point])
        length = max(np.linalg.norm(b - a), 1e-3)
        hidden = path.high[i] and path.high[next_point]
        vjump -= 1
        jump += vjump
        if jump < 0:
            jump, vjump = 0.0, -vjump * 0.5
            if abs(vjump) < 1:
                vjump = 0.0
        run.append(None if hidden else a + (b - a) * part + (0.0, lift + jump, 0.0))
        part += speed / length
        if path.waits[i] >= 100:
            speed = max(speed - BALL_SLOWING, BALL_SLOWEST)
        if part >= 1:
            part, i = 0.0, next_point
            action, kind = path.actions[i], path.waits[i]
            if action >= 0 and (i == n - 1 or rng.random() < 0.8):
                i = action
            elif kind in (10, 11) and i + 1 < n:
                jump, vjump = float(path.points[i][1] - path.points[i + 1][1]), 10.0 if kind == 10 else -3.0
                i += 1
            kind = path.waits[i]
            speed = ball_speed(kind, fast, slow)  # struck again at a bumper or a flipper
            if loop_at >= 0 and i == loop_at:
                if loop_start is None:
                    loop_start = len(run)
                loops += 1
                if loops > BALL_LOOPS:
                    break
    lap = (st[1] + 5999) // 6000 + 1
    ticks = [None] * st[3] + run
    gates = [(0, 0, lap, None)]
    if loop_start is not None:
        return [Track(ticks, restart=st[3] + loop_start, gates=gates, step=6)]  # round the table for good
    ticks += [None] * 12
    if st[2]:
        return [Track(ticks, gates=gates, lap_step=1, step=6)]
    return [Track(ticks, gates=gates + [(len(ticks) - 1, 0, LAP_ONCE, None)], step=6)]


def airship(m, c):
    """0x158: flies its loop, bobbing; with s[1] it first waits for lap 2 and rises to its path."""
    path, st = m['path'], m['settings']
    if path is None:
        return []
    start = plain(path.points[0])
    flight = [p for p, _ in along(path.points, lambda i: st[0] * 5 / 100, True)]
    turns = max(round(len(flight) / 360), 1)  # it comes back to where it started
    ticks = [p + (0.0, 40 * (1 - math.cos(2 * math.pi * turns * t / len(flight))), 0.0) for t, p in enumerate(flight)]
    below = float(m['pos'][1] - start[1])
    if not st[1]:
        return [Track(ticks, step=6)]
    rise = [start + (0.0, below + t * (1 if below < 0 else -1), 0.0) for t in range(int(abs(below)))]
    return [Track(rise + ticks, restart=len(rise), gates=[(0, 0, 2, None)], step=6)]


SUN_FRAMES, SUN_THROW = 180, 37  # its animation's frames; the ticks of its pulse it throws in (state 3)


def sun(m, c):
    """0x1ad: from lap 2 flies to each stop of its path, a point with a wait, waits for a kart to come near,
    throws two flares that bounce away, shakes, and flies on. Its animation speeds up by a tenth of a frame
    a tick to 8 while it throws, and slows back to 1 (SUN_FRAMES)."""
    path, rng = m['path'], c['rng']
    if path is None:
        return []
    n, ticks, gates, flares = len(path.points), [plain(path.points[0])], [(0, 0, 2, None)], []
    throwing = [False]
    for i in range(n):
        at, to = plain(path.points[i]), plain(path.points[(i + 1) % n])
        if path.waits[i]:
            gates.append((len(ticks), 1200, 0, at))
            for t in range(66):
                throwing.append(t < SUN_THROW)
                radius = 20 * (1 - math.cos(0x222 * t * 2 * math.pi / TURN))
                angle = 0xe39 * t * 2 * math.pi / TURN
                ticks.append(at + (radius * math.cos(angle), radius * math.sin(angle), 0.0))
            for delay, turn in ((6, 0.0), (37, math.pi)):
                angle = rng.random() * 2 * math.pi + turn
                flares.append(Track([None] * delay + flare(at - (0.0, 30.0, 0.0), angle, c['ground'], rng) + [None] * 2,
                                    model=c['flare'], gates=[(0, 1200, 2, at)], lap_step=1, hit=10))
        travel = towards(at, to, lambda t: 60.0)
        ticks += travel
        throwing += [False] * len(travel)
    frame, speed, anim = 0.0, 1.0, []
    for _ in range(2):  # the second time round starts where the first ends, as the loop does
        anim = []
        for fast in throwing:
            anim.append(frame / SUN_FRAMES)
            speed = min(speed + 0.1, 8.0) if fast else max(speed - 0.1, 1.0)
            frame = (frame + speed) % SUN_FRAMES
    return [Track(ticks, restart=1, gates=gates, hit=0, anim=anim)] + flares


TREE_FRAMES = 60  # move_tree.nsbca


def move_tree(m, c):
    """0x1a3 (0x2095a3c): walks its path at s[0]/100 a tick, playing its animation a frame a tick; at each
    point stands that point's wait and a tick more, its animation running on to its first frame and
    holding there. A path that does not loop it walks back. A kart that touches it shakes it at two frames
    a tick for 30 ticks, which is left out."""
    path, st = m['path'], m['settings']
    if path is None:
        return []
    points, waits = [plain(p) for p in path.points], list(path.waits)
    if not path.loop:  # there and back
        points += points[-2:0:-1]
        waits += waits[-2:0:-1]
    n, speed = len(points), max(st[0], 1) / 100
    ticks, frames, frame = [], [], 0
    for i in range(n):
        for place, _ in along([points[i], points[(i + 1) % n]], lambda k: speed, False):
            ticks.append(place)
            frames.append(frame)
            frame = (frame + 1) % TREE_FRAMES
        for _ in range(waits[(i + 1) % n] + 1):
            ticks.append(points[(i + 1) % n])
            frames.append(frame)
            frame = (frame + 1) % TREE_FRAMES if frame else 0
    ticks.append(ticks[0])
    frames.append(frame)
    return [Track(ticks, anim=[f / TREE_FRAMES for f in frames])]


def flare(at, angle, ground, rng):
    """0x1ae: falls, and on the ground rests a second and hops on, back to its start when it is too far."""
    origin, place = plain(at), plain(at)
    vx, vz, vy, out, life, rest = 2 * math.cos(angle), 2 * math.sin(angle), 0.0, [], 450, 0
    while life > 0:
        out.append(place.copy())
        if rest:
            rest -= 1
            continue
        life -= 1
        vy = max(vy - 0.1, -8.0)
        place += (vx, vy, vz)
        found = ground(place) if life > 120 and vy < 0 else None
        if found is not None and place[1] - 10 <= found:
            place[1] = found + 10
            if math.hypot(place[0] - origin[0], place[2] - origin[2]) < 400:
                angle = rng.random() * 2 * math.pi
                vx, vz = 1.5 * math.cos(angle), 1.5 * math.sin(angle)
            else:
                vx, vz = -vx, -vz
            vy, rest = 2.75, 60
    return out


def chomp_hop(ticks_since):
    """A Chain Chomp's height over its rest: it hops from each landing, vy 3 less 0.25 a tick."""
    t = ticks_since % 24
    return 3.0 * t - 0.125 * t * t


def chomp_rest(m):
    """The flat plane a Chain Chomp bounces on, whatever is under it: rest height, its back's distance."""
    sy = m['size'][1]
    return float(m['pos'][1]) + 8 - (15 - 30 * sy), 30 * sy


def chain_path(m, c):
    """0x1a5: bounces round its path, its chain's links coming after it."""
    path = m['path']
    if path is None:
        return []
    rest, back = chomp_rest(m)
    places = [p for p, _ in along(path.points, point_speeds(path, m['settings'][0] * 3 / 100), True)]
    ticks = [np.array([p[0], rest + chomp_hop(t), p[2]]) for t, p in enumerate(places)]
    tracks = [Track(ticks)]
    speed = np.mean([np.linalg.norm(b - a) for a, b in zip(places, places[1:])])
    spacing = max(85 - back, 20) / PATH_LINKS
    for link in range(PATH_LINKS):
        behind = int((back + spacing * (link + 0.5)) / max(speed, 0.1))
        tracks.append(Track([np.array([p[0], rest - 26 * m['size'][1], p[2]]) for p in places[-behind:] + places[:-behind]], model=c['chain'], hit=0, step=6))
    return tracks


PATH_LINKS = 4


def bakubaku(m, c):
    """0x13c: leaps out of the water in an arc when a kart comes near a point ahead of it."""
    forward = c['forward'](m)
    start = plain(m['pos']) - (0.0, 50.0, 0.0)
    arc, vy, y = [], 12.0, 0.0
    for t in range(96):
        arc.append(start + forward * 12 * t + (0.0, y, 0.0))
        vy -= 0.25
        y += vy
    near = plain(m['pos']) + forward * 800
    return [Track([None] * 2 + arc + [None] * 1800, gates=[(0, 800, 0, near)], step=6)]


def bat(place, velocity, climb, dive, cruise, forward):
    """0x19f: dives, levels out, cruises and climbs away."""
    out, place = [], plain(place)
    vy = climb
    v = plain(velocity)
    for _ in range(dive + 1):
        out.append(place.copy())
        v += forward * 0.1
        vy -= 0.1
        place += v + (0.0, vy, 0.0)
    while vy < 0:
        out.append(place.copy())
        v += forward * 0.1
        vy = min(vy + 0.1, 0.0)
        place += v + (0.0, vy, 0.0)
    for _ in range(cruise + 1):
        out.append(place.copy())
        place += v
    while place[1] < 440 and len(out) < 900:
        out.append(place.copy())
        v -= forward * 0.05
        vy += 1
        place += v + (0.0, vy, 0.0)
    return out


def released(m, c, first, every, per_loop, count, radius, speed, spread, vy, lift, dive, cruise):
    """Bats a spawner lets go, per_loop of them at its frame `first` of each `every`, one track each."""
    rng, pos, facing = c['rng'], plain(m['pos']), math.atan2(*c['forward'](m)[[0, 2]])
    tracks = []
    for n in range(count):
        angle = facing + (rng.random() * 2 - 1) * spread
        v = np.array([math.sin(angle), 0.0, math.cos(angle)]) * (speed[0] + rng.random() * (speed[1] - speed[0]))
        start = pos + (rng.random() * 10 - 5, lift + rng.random() * 10 - 5, rng.random() * 80 - 40)
        flight = bat(start, v, vy[0] + rng.random() * (vy[1] - vy[0]), dive[0] + rng.randrange(dive[1]), cruise[0] + rng.randrange(cruise[1]), c['forward'](m))
        wait = first + n // per_loop * every + rng.randrange(30)
        tracks.append(Track([None] * wait + flight + [None] * 2, gates=[(0, radius, 0, pos)], hit=8))
    return tracks


def bats(m, c):
    """0x1a0: when a kart comes near, eight bats."""
    return released(m, c, 20, 50, 2, 8, 1200, (2, 3), 0.288, (-0.625, -0.375), 35, (15, 1), (140, 8))


def coffin(m, c):
    """0x19e: stands where it is; when a kart comes near, it lets out ten bats, upwards."""
    return released(m, c, 10, 41, 3, 10, 1500, (0.5, 1.5), 0.096, (2.125, 2.375), 25, (40, 2), (80, 5))


COFFIN_OPEN = 9 * 41  # ticks: its lid's 41-frame animation, 9 times over, from a kart coming near (0x20919e8)


def coffin_lid(m, c):
    """0x19e's own box: stands where it is, and opens and shuts its lid while it lets its bats out
    (coffin). Its animation runs on this clock (mkds_movers.ANIMATED), shut while the gate holds it."""
    pos = plain(m['pos'])
    return [Track([pos] * (COFFIN_OPEN + 1), gates=[(0, 1500, 0, pos)], hit=0)]


def moving_box(m, c):
    """0xc9: an item box on its path, from the point s[4]. A point with an action sends it back to the first."""
    path, st = m['path'], m['settings']
    if path is None:
        return []
    points = [plain(p) for p in path.points]
    speed_at, n = point_speeds(path, st[0] * 3 / 100), len(points)
    stops = next((i for i in range(n) if path.actions[i]), None)
    last = stops + 1 if stops is not None else n
    run = along(points[:last], speed_at, path.loop and stops is None)
    ticks = []
    for t, (place, i) in enumerate(run):
        hidden = path.high[i] and path.high[(i + 1) % n]
        ticks.append(None if hidden else place + (0.0, 4 * math.sin(t * 2 * math.pi / 90), 0.0))
    first = next((t for t, (_, i) in enumerate(run) if i >= st[4] % n), 0)
    return [Track(ticks, start=first, step=6, hit=15, damage=c['crate'])]


FIRE_TURN = 0x6d  # the head's turn a tick, of TURN
FIRE_TILT, FIRE_TILT_TICKS = 0xa4, 31  # it nods this much a tick before a shot, and back after
FIRE_ROUND = 2400  # ticks of the game's timing drawn before it repeats
FIRE_BOB, FIRE_PULSE, FIRE_AT, FIRE_AFTER = 21, 29, 10, 19  # ticks: waiting for its bob, its swell, the shot into its mouth's animation, the rest of it


def _turn_by(at, want, most):
    """An angle of TURN a tick nearer want, by most at the most."""
    gap = (want - at + TURN // 2) % TURN - TURN // 2
    return want if abs(gap) <= most else at + (most if gap > 0 else -most)


def fire_plant_plan(m, c):
    """0x1aa's state machine (0x20983c4): picks another of its paths at random, turns its head to it, and half
    the time nods, swells and fires down it, then nods back. -> (the head's (turn, nod) a tick, of TURN, its
    turn from the object's own; the ticks each path fires; the ticks a round lasts, a multiple of 3), or None
    without paths. Drawn once from the object's place, so the head and its shots agree."""
    routes = c['fire_paths'](m)
    if not routes:
        return None
    rng = random.Random(int(abs(m['pos'][0]) * 7 + abs(m['pos'][2]) * 13))
    own = math.radians(m['rot'][1])
    yaws = [round((math.atan2(*(plain(r[1]) - plain(r[0]))[[0, 2]]) - own) / (2 * math.pi) * TURN) % TURN for r in routes]
    aims, times, yaw, aim = [], [[] for _ in routes], 0, 0

    def hold(n, nod=0):
        aims.extend([(yaw, nod)] * n)

    while len(aims) < FIRE_ROUND:
        aim = (aim + 1 + rng.randrange(max(len(routes) - 1, 1))) % len(routes)
        hold(1)
        while yaw != yaws[aim]:
            yaw = _turn_by(yaw, yaws[aim], FIRE_TURN)
            hold(1)
        if rng.random() < 0.5:
            hold(FIRE_BOB)
            aims.extend((yaw, FIRE_TILT * (n + 1)) for n in range(FIRE_TILT_TICKS))
            top = FIRE_TILT * FIRE_TILT_TICKS
            hold(FIRE_BOB + FIRE_PULSE + FIRE_AT, top)
            times[aim].append(len(aims))
            hold(FIRE_AFTER, top)
            aims.extend((yaw, top - FIRE_TILT * (n + 1)) for n in range(FIRE_TILT_TICKS))
    while yaw != 0:  # back to where it started, so the round repeats without a jump
        yaw = _turn_by(yaw, 0, FIRE_TURN)
        hold(1)
    return aims, times, routes


def _flight(m, route):
    """A shot's places a tick: it bounces down the path at 7.5 a tick and goes at its end."""
    run = [p for p, _ in along(route, lambda i: 7.5, False)]
    high, speed, flight = float(m['pos'][1] - route[0][1]) + 70, -5.0, []
    for place in run:
        flight.append(place + (0.0, 15 + high, 0.0))
        high += speed
        speed -= 1
        if high <= 0:
            high, speed = 0.0, 5.5
    return flight


def _round_length(aims, ends):
    """Ticks a round of a plan lasts: past every shot's flight, a whole number of keys."""
    ticks = max([len(aims)] + ends)
    return ticks + (-ticks) % 3


def fire_plant(m, c):
    """0x1aa's shots, one track a path, in its plan's round (fire_plant_plan)."""
    plan = fire_plant_plan(m, c)
    if plan is None:
        return []
    aims, times, routes = plan
    flights = [_flight(m, route) for route in routes]
    rounds = _round_length(aims, [t + len(f) for shots, f in zip(times, flights) for t in shots])
    tracks = []
    for flight, shots in zip(flights, times):
        if not shots:
            continue
        ticks, at = [], 0
        for shot in shots:
            shot = max(shot, at)
            ticks += [None] * (shot - at) + flight
            at = shot + len(flight)
        tracks.append(Track(ticks + [None] * (rounds + 1 - at), hit=15))
    return tracks


def fire_plant_head(m, c):
    """0x1aa's head: its (turn, nod) a tick through its plan's round, the last the same as the first."""
    plan = fire_plant_plan(m, c)
    if plan is None:
        return [(0, 0)]
    aims, times, routes = plan
    flights = [len(_flight(m, route)) for route in routes]
    rounds = _round_length(aims, [t + f for shots, f in zip(times, flights) for t in shots])
    return aims + [aims[-1]] * (rounds + 1 - len(aims))


# 0xd2's speed slots by row (0x215a0a8): six rows of which drum takes which. Slot 0 is s[0], 2 is s[4],
# 1 their mean (ctor 0x20ffb1d)
DRUM_SLOTS = ((0, 1, 2), (2, 0, 1), (1, 2, 0), (0, 2, 1), (1, 0, 2), (2, 1, 0))
DRUM_CREEP, DRUM_FACE = 0x96, 0x1999  # its least speed a tick, and a tenth of a turn: it stops with a face up
DRUM_ROUNDS = 6


def drums(m, c):
    """0xd2 (states 0x216c740): when every drum on the course stands still, a random row of DRUM_SLOTS
    gives each a speed; each waits s[3] ticks, speeds up over s[2], turns s[1], slows over s[2] to
    DRUM_CREEP, creeps to its next face and stops. -> its turn a tick, of TURN, the last a whole number of
    faces from the first."""
    every = c['drums']
    mine = next(i for i, d in enumerate(every) if np.allclose(d[0], m['pos']))
    rng = random.Random(int(abs(every[0][0][0]) * 7 + abs(every[0][0][2]) * 13))
    angles = [0] * len(every)
    out = [0]
    for _ in range(DRUM_ROUNDS):
        row = DRUM_SLOTS[rng.randrange(len(DRUM_SLOTS))]
        runs = []
        for i, (_, st) in enumerate(every):
            fast, slow = (st[0] << 7) // 100, (st[4] << 7) // 100
            top = (fast, (fast + slow) // 2, slow)[row[i % 3]]
            ramp, angle, run = max(st[2], 1), angles[i], []
            speeds = [0] * st[3] + [top * (k + 1) // ramp for k in range(ramp)] + [top] * st[1] + [max(top * (ramp - k - 1) // ramp, DRUM_CREEP) for k in range(ramp)]
            for v in speeds:
                angle += v
                run.append(angle)
            face = -(-angle // DRUM_FACE) * DRUM_FACE
            while angle < face:
                angle = min(angle + DRUM_CREEP, face)
                run.append(angle)
            angles[i] = angle
            runs.append(run)
        longest = max(len(r) for r in runs)
        run = runs[mine]
        out += run + [run[-1]] * (longest - len(run))
    out += [out[-1]] * ((-(len(out) - 1)) % 3)
    return out


def rocky_wrench(m, c):
    """0x1a7: peeks out of its hole, sinks, comes right out, stays and goes down again."""
    rng, ground = c['rng'], c['settled'](m['pos'])[1]
    st, ticks = m['settings'], []

    def at(height):
        ticks.append(None if height <= 0 else np.array([m['pos'][0], ground + height - 15, m['pos'][2]]))

    for _ in range(4):
        for _ in range(max(st[0] + rng.randrange(60) - 30, 1)):
            at(0)
        height, peek = 2.0, 30 + rng.randrange(30)
        for _ in range(peek):
            at(height)
            height = min(height + 0.25, 7.5)
        while height > 2:
            at(height)
            height -= 0.25
        for _ in range(15 + rng.randrange(15)):
            at(height)
        while height < 18.3:
            at(height)
            height += 0.75
        for _ in range(68 + rng.randrange(90)):
            at(height)
        while height > 0:
            at(height)
            height -= 0.75
    return [Track(ticks)]


def monty_mole(m, c):
    """0x199: rises from its hole, looks round, sinks, leaps out and is gone."""
    ground, st, s = c['settled'](m['pos'])[1], m['settings'], m['size'][1]
    base = ground - 18 * s
    place = lambda high: np.array([m['pos'][0], base + high * s, m['pos'][2]])
    run = [place(h) for h in range(17)] + [place(17)] * 20 + [place(h) for h in range(17, 0, -1)]
    vy, high = 4.5, 0.0
    while high >= 0:
        run.append(place(high))
        vy -= 0.15
        high += vy
    return [Track([None] * st[1] + run + [None] * max(st[0], 1), restart=st[1])]


def cheep_cheep(m, c):
    """0x19b: leaps from the water near its home, a chain of low hops and a high one."""
    rng, home, ticks = c['rng'], plain(m['pos']), []
    x, z, base = 0.0, 0.0, float(home[1])
    for _ in range(3):
        count = 4 + rng.randrange(3)
        for hop in range(count + 1):
            vy, y = (3.1 if hop == count else 2 + rng.random()), 0.0
            vx = 0.8 if x < -20 else -0.8 if x > 20 else rng.choice((-0.8, 0.8))
            vz = rng.choice((-0.8, 0.8))
            while y >= 0:
                ticks.append(np.array([home[0] + x, base + y, home[2] + z]))
                x, z = x + vx, z + vz
                vy -= 0.22
                y += vy
    return [Track(ticks)]


BUBBLE_DEPTH, BUBBLE_LEAP, BUBBLE_GRAVITY, BUBBLE_FASTEST = 16.0, 4.7, 0.1, 3.0


def bubble(m, c):
    """0x198: leaps out of the lava BUBBLE_DEPTH under its place, falls back, and once under where it left
    hides for s[0] ticks, then leaps again (0x20df938, 0x20df890, 0x20df83c)."""
    base = plain(m['pos']) - (0.0, BUBBLE_DEPTH, 0.0)
    y, vy, run = base[1], BUBBLE_LEAP, []
    while y >= base[1]:
        run.append(np.array([base[0], y, base[2]]))
        vy = max(vy - BUBBLE_GRAVITY, -BUBBLE_FASTEST)
        y += vy
    return [Track(run + [None] * (m['settings'][0] + 1), hit=15)]


MOTIONS = {'coffin_lid': coffin_lid, 'move_tree': move_tree, 'thwomp': thwomp, 'rock': rock, 'ball': ball, 'airship': airship, 'sun': sun, 'chain_path': chain_path,
           'bakubaku': bakubaku, 'bats': bats, 'coffin': coffin, 'moving_box': moving_box, 'fire_plant': fire_plant, 'rocky_wrench': rocky_wrench,
           'monty_mole': monty_mole, 'cheep_cheep': cheep_cheep, 'bubble': bubble}


# What turns where it stands, a tick at a time: name -> its (first, second) angles a tick, of TURN
FIRE_MOUTH_FRAMES, FIRE_MOUTH_TICKS = 30, 29  # its mouth's animation, and the ticks it plays from FIRE_AT before a shot


def fire_plant_mouth(m, c):
    """0x1aa's head itself (PakkunMouth): turns as fire_plant_head, and opens its mouth round each shot.
    -> (turn, nod, how far through its animation) a tick."""
    aims = fire_plant_head(m, c)
    plan = fire_plant_plan(m, c)
    frames = [0] * len(aims)
    for shots in (plan[1] if plan else []):
        for shot in shots:
            for k in range(FIRE_MOUTH_TICKS):
                if 0 <= shot - FIRE_AT + k < len(frames):
                    frames[shot - FIRE_AT + k] = k
    return [(turn, nod, frame / FIRE_MOUTH_FRAMES) for (turn, nod), frame in zip(aims, frames)]


TURNS = {'fire_plant_head': fire_plant_head, 'fire_plant_mouth': fire_plant_mouth, 'drums': lambda m, c: [(a, 0) for a in drums(m, c)]}


def turns(name, mover, context):
    return TURNS[name](mover, context)


def tracks(name, mover, context):
    seed = int(abs(mover['pos'][0]) * 7 + abs(mover['pos'][2]) * 13)
    return MOTIONS[name](mover, {**context, 'rng': random.Random(seed)})
