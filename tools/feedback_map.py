#!/usr/bin/env python3
"""
feedback_map.py - where each table keeps what cabinet feedback watches.

    python3 tools/feedback_map.py            # all thirteen; build-linux.sh runs it
    python3 tools/feedback_map.py --show     # and print what it found

The thirteen tables are one engine at different offsets. This reads your own
lifted C (work/gen/epN: each statement carries its instruction in a comment)
and your own EXEs, finds the engine's anchors by the shape of a few short
instruction runs, and writes work/feedback/epN.map for src/watch.c, keyed
by the EXE's size and hash. Like everything else under work/ the maps are
generated from your copy and never committed: no address is in this file.

Every anchor must be found exactly once in all thirteen, or it says which
was not and writes nothing. A hooked function must still begin with
RECOMP_ENTER, the runtime's only way to see it called: a generator that
leaves that out fails here instead of going quiet in a cabinet.

See docs/cabinet-feedback.md.
"""
import glob
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, 'work', 'gen')
DATA = os.path.join(ROOT, 'original')
OUT = os.path.join(ROOT, 'work', 'feedback')
TABLES = 13
VERSION = 4
MAX_BUMPERS = 6                 # E105-E107 and E118-E120
RIM = 40                        # a small run this near a bumper is part of it

N = r'0x([0-9A-F]+)'            # a number, kept
A = r'0x[0-9A-F]+'              # a number, not kept
GO = r'[a-z]+ ' + A + r'\n'     # any conditional jump
MAP_W, MAP_H = 320, 400
SLINGS_BELOW = 270              # on every table they sit just over the flippers

# ---- each table's own features ----------------------------------------------------
# Every table has one switch handler: the ball scan hands it the collision-map
# colour under the ball (0xAA and up), and it branches to the table's own code
# for that colour. What each colour is was read from that code, and is game data,
# not an address: positions are the scanned ball's, as the handler compares them
# (inclusive). Features are numbered left to right, top first on a tie; the side
# of a cabinet comes from where the colour is drawn. A table not listed sends
# none of these events. See docs/cabinet-feedback.md.
# SSF: the effects that are the board's own, by their slot in the table's
# SFXn.PIN. Slots 0-6 are the same kind on every table (bumper, drop target,
# slingshot, thud or kickback, flipper, the ball in the shooter lane, the
# plunger); some tables have more. Award and jackpot calls stay on the front.
BOARD_EFFECTS = list(range(7))
BOARD_MORE = {'ep1': [0xB, 0x11]}          # its holes, its rubber posts


def sound_driver(v):
    """the Sound Blaster driver's voice setup, volume, stop and stop of a
    class (AH: 1 effects, 2 music), the same for every table: SSF follows a
    board effect through them"""
    f = read_functions('sb')
    for key, what, pattern in (('sfx_setup_fn', 'voice setup', r'cmp dword es:\[di\+0x2D\], 0xFFFFFFFF\n'),
                               ('sfx_volume_fn', 'voice volume', r'^cmp al, 0xFF\n.*\n.*\nmov byte ds:\[bx\+0x862\], cl\n'),
                               ('sfx_stop_fn', 'voice stop', r'\Acmp al, 0xFF\n(?:.*\n){0,3}mov byte ds:\[bx\+0x882\], 0x0\n'),
                               ('sfx_stop_class_fn', 'stop of a class', r'cmp byte ds:\[bx\+0x882\], ah\n')):
        hits = [n for n, (t, entered) in f.items() if re.search(pattern, t, re.M) and entered]
        if len(hits) != 1:
            raise Missing(f'sound driver {what}: found {len(hits)} times, not once')
        v[key] = hits[0]


def effects(f, v):
    """the table's one way to play an effect: AL the effect, AH a pan nibble"""
    hits = [n for n, (t, entered) in f.items()
            if 'mov cx, 0x60\n' in t and 'mul cx\n' in t and 'shl cx, 0x4\n' in t and 'sub cx, 0x80\n' in t and entered]
    if len(hits) != 1:
        raise Missing(f'effect function: found {len(hits)} times, not once')
    v['effect_fn'] = hits[0]
    m = re.search(rf'mov ds, cx\ncmp byte ds:\[{N}\], 0x0\nje ', f[hits[0]][0])
    if not m:
        raise Missing('the effects switch the effect function asks first')
    v['sfx_on'] = num(m.group(1))        # 0: SOUND FX OFF


FEATURES = {
    'ep1': dict(                                       # Super Android
        zones=[
            ('hole', 1, 0xF5, 0, 0, 319, 399),          # test hole, top left
            ('hole', 2, 0xF8, 0, 0, 50, 320),           # left hole
            ('hole', 3, 0xF6, 0, 0, 219, 399),          # power-up hole
            ('hole', 4, 0xF8, 145, 0, 319, 399),        # physical-systems hole
            ('hole', 5, 0xF6, 220, 0, 319, 399),        # right hole, the centre ramp's end
            ('kickback', 1, 0xF8, 0, 321, 50, 399),     # left outlane
            ('rollover', 1, 0xF8, 51, 0, 144, 399),     # star
            ('rollover', 2, 0xFB, 0, 0, 319, 399),      # left lane
            ('rollover', 3, 0xFA, 0, 0, 179, 399),      # the top four lanes
            ('rollover', 4, 0xFA, 180, 0, 209, 399),
            ('rollover', 5, 0xFA, 210, 0, 242, 399),
            ('rollover', 6, 0xFA, 243, 0, 319, 399),
            ('ramp', 1, 0xF9, 0, 0, 74, 399),           # left ramp
            ('ramp', 2, 0xF6, 220, 0, 319, 399),        # centre ramp, counted in the right hole
            ('ramp', 3, 0xF9, 281, 0, 319, 399),        # right ramp
        ],
        targets=True,                                   # drop targets: F2-F4
        lanes=True,                                     # the top lanes' lights
        ramps='RAMPS ',                                 # the bonus line that counts them
        messages=[
            ('jackpot', 'JACKPOT!!!'),
            ('super_jackpot', 'DOUBLE JACKPOT'), ('super_jackpot', 'SUPER JACKPOT!'),
            ('skill_shot', 'SKILL SHOT'),
            ('award', 'POWER UP'), ('award', 'FULL POWER-10M'), ('award', 'I.Q. UPGRADE'),
            ('award', 'BUMPER VALUE IS NOW        '), ('award', 'COMPUTER LINK ESTABLISHED'),
            ('award', 'PHYSICAL SYS COMPUTER ONLINE'), ('award', 'PHYSICAL SYSTEMS COMPLETE'),
            ('award', 'BASIC IO INSTALLED: 5M'), ('award', 'AI INSTALLED: 10M'),
            ('award', 'AI2 INSTALLED: 20M'), ('award', 'DATABASE INSTALLED: 35M'),
            ('award', 'ANDROID IS ACTIVATED'), ('award', 'VIRUS FLUSHED OUT - 15M'),
            ('award', '  MIL - ANDROID MILLIONS'),
            ('award', 'LEFT ARM ACTIVATED'), ('award', 'RIGHT ARM ACTIVATED'),
            ('award', 'LEFT LEG ACTIVATED'), ('award', 'HEAD ACTIVATED'), ('award', 'TORSO ACTIVATED'),
            ('award', 'LEVEL 1: DIAGNOSTIC'), ('award', 'LEVEL 1: LEFT ARM TEST'),
            ('award', 'LEVEL 1: RIGHT ARM TEST'), ('award', 'LEVEL 1: LEFT LEG TEST'),
            ('award', 'LEVEL 1: RIGHT LEG TEST'), ('award', 'LEVEL 1: HEAD TEST'),
            ('award', 'LEVEL 1: TORSO TEST'),
        ],
    ),
}
COUNTS = dict(hole=5, rollover=6, ramp=3, kickback=1, target=3)   # E121-E145


class Missing(Exception):
    pass


def num(s):
    return int(s, 16)


def read_functions(img):
    """name -> its instructions, a line each. A call to lifted code reads
    `call NAME`, so that a signature can pick up the function called."""
    out = {}
    for path in sorted(glob.glob(os.path.join(GEN, img, f'{img}_[0-9]*.c'))):
        name, ins, entered = None, [], False
        callee = None
        for line in open(path):
            m = re.match(r'void (\w+)\(CPU \*cpu\)', line)
            if m:
                name, ins, entered = m.group(1), [], False
                continue
            if name is None:
                continue
            if line.startswith('}'):
                out[name] = ('\n'.join(ins) + '\n', entered)
                name = None
                continue
            if f'RECOMP_ENTER("{name}")' in line:
                entered = True
            m = re.search(r'/\* ([^*]*?) \*/\s*$', line)
            if not m or m.group(1).endswith('call return addr'):
                continue
            c = re.match(rf'\s+({img}_[0-9A-F]+)\(cpu\);', line)
            text = m.group(1)
            if c and text.startswith('call '):
                text = 'call ' + c.group(1)
            ins.append(text)
    if not out:
        raise Missing('no lifted code: run tools/discover.py')
    return out


def find(funcs, what, pattern, within=None):
    """The one place a signature matches: (match, function)."""
    hits = []
    for name, (text, _) in funcs.items():
        if within is None or name == within:
            hits += [(m, name) for m in re.finditer(pattern, text)]
    if len(hits) != 1:
        raise Missing(f'{what}: found {len(hits)} times, not once')
    return hits[0]


def lifted_as(img):
    text = open(os.path.join(GEN, img, f'{img}_table.c')).read()
    m = re.search(r'\{"%s", "([^"]+)", 0x([0-9A-F]+), (\d+)u, 0x([0-9A-F]+)u' % img, text)
    if not m:
        raise Missing('no image identity in the lifted code')
    return m.group(1), num(m.group(2)), int(m.group(3)), m.group(4)


def exe_module(img, file, size):
    path = next((os.path.join(DATA, f) for f in os.listdir(DATA) if f.upper() == file.upper()), None)
    if not path:
        raise Missing(f'{file} is not in original/')
    raw = open(path, 'rb').read()
    if len(raw) != size:
        raise Missing(f'{file} is not the file that was lifted')
    return raw[struct.unpack_from('<H', raw, 8)[0] * 16:]


# ---- the anchors -----------------------------------------------------------------

def flippers(f, v):
    for side, effect in (('l', '0x1004'), ('r', '0xF004')):
        m, name = find(f, f'flipper {side}',
                       rf'cmp word ds:\[{N}\], 0x9\n{GO}mov word ds:\[{A}\], {effect}\n'
                       rf'mov word ds:\[{A}\], 0x0\n(?:.*\n){{0,3}}?mov byte ds:\[{N}\], 0x1\ndec word ds:\[0x\1\]\n')
        v[f'flip_{side}'], v[f'up_{side}'] = num(m.group(1)), num(m.group(2))
        v['flipper_fn'] = name


def tilt_and_nudge(f, v):
    m, _ = find(f, 'nudge', rf'add byte ds:\[{N}\], 0x23\nmov byte ds:\[{N}\], 0x(?:A|9)\n')
    v['meter'], v['cooldown'] = num(m.group(1)), num(m.group(2))
    m, _ = find(f, 'tilt', rf'cmp byte ds:\[{N}\], 0x50\n{GO}cmp byte ds:\[{N}\], 0x1\n{GO}')
    if num(m.group(1)) != v['meter']:
        raise Missing('tilt: not the nudge meter')
    v['tilt'] = num(m.group(2))


def physics_and_kicker(f, v):
    m, step = find(f, 'kicker call',            # it follows the physics step's test of the flippers
                   rf'cmp byte ds:\[0x{v["up_r"]:X}\], 0x1\n{GO}mov byte ds:\[{A}\], 0x2\njmp {A}\n'
                   rf'(?:cmp byte es:\[bx\], {A}\n{GO})?'
                   rf'cmp byte ds:\[{N}\], 0x0\n{GO}'
                   rf'(?:cmp word ds:\[di\+{A}\], {A}\n{GO}){{0,2}}'
                   r'call (ep\d+_[0-9A-F]+)\n')
    v['step_fn'], v['kicker_fn'] = step, m.group(2)
    text = f[step][0]

    m, _ = find(f, 'ball active', rf'\A(?:.*\n){{0,14}}?cmp word ds:\[di\+{N}\], 0x1\n{GO}', step)
    v['active'] = num(m.group(1))
    m, _ = find(f, 'ball slots', rf'\A(?:.*\n){{0,20}}?add di, 0x2\ncmp di, {N}\n', step)
    v['slots'] = num(m.group(1)) // 2
    vel = re.findall(rf'mov ax, word ds:\[di\+{N}\]\nadd word ds:\[di\+{A}\], ax\n', text)
    pos = re.findall(rf'cmp word ds:\[di\+{N}\], 0x1\njge ', text)
    if len(vel) < 2 or len(pos) < 2:
        raise Missing('ball position and velocity')
    v['vx'], v['vy'] = num(vel[0]), num(vel[1])
    v['x'], v['y'] = num(pos[0]), num(pos[1])

    kick = v['kicker_fn']
    m, _ = find(f, 'kick strength', rf'mov (a[lh]), byte ds:\[{A}\]\nmov byte ds:\[{N}\], \1\n', kick)
    v['kick'] = num(m.group(2))
    body = f[kick][0]
    # the game's own line between its bumpers and its slingshots; none: no slings
    m = re.search(rf'cmp word ds:\[di\+0x{v["y"]:X}\], {N}\n', body)
    v['sling_y'] = num(m.group(1)) if m else 0xFFFF
    m = re.search(rf'cmp word ds:\[di\+0x{v["y"]:X}\], 0x168\njae ', body)
    v['weak_y'] = 0x168 if m else 0xFFFF


def kick_band(f, v):
    """The colours the physics step hands to the kicker on the main layer, or
    None where the game decides at run time (EP8 stamps its kickers)."""
    text = f[v['step_fn']][0]
    m = re.search(rf'mov al, {N}\n(?:mov ah, {A}\n)?mov dh, {N}\n', text)
    if not m:
        return None
    lo, dh = num(m.group(1)), num(m.group(2))
    below = re.search(rf'cmp byte es:\[bx\], {N}\nja {A}\ncmp byte ds:\[{A}\], 0x0\n', text)
    if below:
        return lo, num(below.group(1))
    return lo, dh - 1


def collision_map(f, v):
    m, _ = find(f, 'collision map',
                rf'call (ep\d+_[0-9A-F]+)\nmov word ds:\[{N}\], ax\nadd ax, 0xFA0\nmov word ds:\[{A}\], ax\n')
    g = re.match(rf'mov ax, {N}\n', f[m.group(1)][0])
    if not g:
        raise Missing('collision map getter')
    v['map'] = num(m.group(2))
    return num(g.group(1))


def plunger(f, v):
    m, main = find(f, 'plunger keys',
                   rf'cmp byte ds:\[{N}\], 0x1\n{GO}cmp byte cs:\[{N}\], 0x1\n{GO}'
                   rf'cmp byte cs:\[{N}\], 0x1\n{GO}'
                   rf'(?:cmp word ds:\[{N}\], {N}\n(ja|jae) {A}\nadd word ds:\[0x\4\], {N}\n'
                   rf'mov ax, word ds:\[0x\4\]\nshr ax, {N}\nadd ax, {N}\n'
                   rf'|mov word ds:\[{N}\], {N}\n)')
    v['main_fn'] = main
    v['demo'], v['ctrl'], v['space'] = num(m.group(1)), num(m.group(2)), num(m.group(3))
    if m.group(4):
        limit, inc = num(m.group(5)), num(m.group(7))
        # what holding the key reaches: the last add allowed still goes through
        last = limit // inc * inc if m.group(6) == 'ja' else (limit - 1) // inc * inc
        v['pull'], v['pull_inc'], v['pull_max'] = num(m.group(4)), inc, last + inc
        v['pull_shift'] = num(m.group(8))
    else:
        v['pull'], v['pull_inc'], v['pull_max'] = num(m.group(10)), 0, num(m.group(11))
        v['pull_shift'] = 0

    before = f[main][0][:m.start()]
    counts = list(re.finditer(rf'cmp byte ds:\[{N}\], 0x0\n{GO}dec byte ds:\[0x\1\]\n', before))
    if not counts:
        raise Missing('serve count')
    v['count'] = num(counts[-1].group(1))
    gate = before[:counts[-1].start()]
    lane = re.search(rf'cmp word ds:\[0x{v["active"]:X}\], 0x0\n{GO}'
                     rf'(?:cmp byte ds:\[{N}\], 0x0\n{GO})?'
                     rf'cmp word ds:\[0x{v["x"]:X}\], {N}\n{GO}'
                     rf'cmp word ds:\[0x{v["y"]:X}\], {N}\n{GO}\Z', gate)
    if lane:
        v['lane_off'] = num(lane.group(1)) if lane.group(1) else 0
        v['lane_x'], v['lane_y'] = num(lane.group(2)), num(lane.group(3))
    else:       # no lane (Enigma): the trigger works while no ball is out
        none_out = ''.join(rf'cmp word ds:\[0x{v["active"] + 2 * i:X}\], 0x1\n{GO}' for i in range(3))
        out = re.search(rf'(?:cmp byte ds:\[{N}\], 0x[01]\n{GO})?{none_out}\Z', gate)
        if not out or v['pull_inc']:
            raise Missing('plunger lane')
        v['lane_off'] = num(out.group(1)) if out.group(1) else 0
        v['lane_x'] = v['lane_y'] = 0

    over = re.search(rf'\A(?:.*\n){{0,6}}?cmp byte ds:\[{N}\], 0x1\n{GO}mov byte ds:\[{A}\], 0x0\njmp ',
                     f[main][0][m.end():]) if v['pull_inc'] else None
    v['over_pull'] = num(over.group(1)) if over else 0


def balls_lost(f, v):
    m, _ = find(f, 'drain', rf'cmp word ds:\[di\+0x{v["y"] - 2:X}\], {N}\njb ', v['main_fn'])
    v['drain_y'] = num(m.group(1))
    # the extra-ball flag: what a tilt takes away along with the tilt itself
    # (perhaps with its lamp), or, where they add up, what a drain counts down
    taken = (rf'mov byte ds:\[{N}\], 0x0\n(?:mov byte ds:\[{A}\], 0x1\n)?'
             rf'mov byte ds:\[0x{v["tilt"]:X}\], 0x0\n')
    counted = rf'cmp byte ds:\[{N}\], 0x0\n{GO}dec byte ds:\[0x\1\]\nlea bx, '
    stacks = not any(re.search(taken, text) for text, _ in f.values())
    m, name = find(f, 'extra ball', counted if stacks else taken)
    v['extra'], v['drain_fn'] = num(m.group(1)), name
    # game over: the flag set or tested where the table goes to its exit (3039h)
    m, _ = find(f, 'game over', rf'mov di, 0x3039\nmov byte ds:\[{N}\], 0x1\n'
                                rf'|cmp byte ds:\[{N}\], 0x1\n{GO}mov di, 0x3039\n')
    v['over'] = num(m.group(1) or m.group(2))


def switches(f, v):
    """The switch handler, and where the ball scan that calls it keeps the
    ball it is looking at. Every table has both."""
    m, name = find(f, 'switch handler', rf'sub bx, 0xAA\nshl bx, 0x1\nmov bx, word cs:\[bx\+{A}\]\n')
    if not f[name][1]:
        raise Missing(f'{name} has no RECOMP_ENTER, so its calls cannot be seen')
    v['switch_fn'] = name
    m, _ = find(f, 'switch scan', rf'mov ax, word ds:\[{N}\]\nmov di, word ds:\[{N}\]\nmov cx, 0x14\nmul cx\n'
                                  rf'add ax, word ds:\[0x{v["map"]:X}\]\n(?=(?:.*\n)*?call {name}\n)')
    v['scan_y'], v['scan_x'] = num(m.group(1)), num(m.group(2))


def strings(data, text, whole=True):
    """Where a string (or, not whole, one that begins so) is in the data
    segment: every place."""
    raw = text.encode('latin-1')
    out = [i for i in range(1, len(data) - len(raw))
           if data.startswith(raw, i) and (not whole or data[i + len(raw)] in (0, 0x24))
           and (data[i - 1] == 0 or any(struct.unpack_from('<H', data, i - 2 * j)[0] == i
                                        for j in range(1, min(17, i // 2 + 1))))]  # or ends a table of them
    if not out:
        raise Missing(f'text {text!r} not in the data segment')
    return out


def side(x):
    return 'LCR'[min(2, x * 3 // MAP_W)]


def features(img, f, v, art, data):
    """A table's own features (FEATURES): its zones checked against the
    collision map, and what its switch handler keeps for them."""
    rules = FEATURES[img]
    sw = f[v['switch_fn']][0]
    out = dict(zones=[], timers=[], targets=[], lanes=None, ramps=None, msg_fn=None, msgs=[])

    # the handler's gate: on the ramp layer only some colours are handled, and
    # tilted only some others
    m = re.search(rf'\Apush es\npusha\ncmp al, 0xAA\njb {A}\ncmp byte ds:\[{N}\], 0x1\njne {A}\n'
                  rf'((?:cmp al, {A}\nje {A}\n)+)jmp {A}\n((?:cmp al, {A}\nje {A}\n)+)'
                  rf'cmp byte ds:\[0x{v["tilt"]:X}\], 0x1\nje {A}\n', sw)
    if not m:
        raise Missing('switch handler gate')
    colours = lambda t: [num(c) for c in re.findall(r'cmp al, 0x([0-9A-F]+)', t)]
    out['gate'] = (num(m.group(1)), colours(m.group(2)), colours(m.group(3)))
    seen = {}
    for kind, n, colour, x0, y0, x1, y1 in rules['zones']:
        regions = seen.setdefault(colour, kick_regions(art, (colour, colour), 2))
        # the scan looks at the pixels under the ball from its top left
        inside = [r for r in regions if x0 <= r[1] - 4 <= x1 and y0 <= r[2] - 4 <= y1]
        if not inside:
            raise Missing(f'{kind} {n}: colour {colour:02X} is not drawn there')
        x = sum(r[0] * r[1] for r in inside) // sum(r[0] for r in inside)
        out['zones'].append((kind, n, colour, x0, y0, x1, y1, side(x)))
    for colour, regions in seen.items():
        for r in regions:
            if not any(z[2] == colour and z[3] <= r[1] - 4 <= z[5] and z[4] <= r[2] - 4 <= z[6]
                       for z in out['zones']):
                raise Missing(f'colour {colour:02X} at {r[1]},{r[2]} is in no zone')
    for kind, count in COUNTS.items():
        got = sorted(z[1] for z in out['zones'] if z[0] == kind)
        if len(got) != len(set(got)) or any(x > count for x in got):
            raise Missing(f'{kind}: numbered {got}, events are made for {count}')

    # the holes' hold timers: set when a ball is caught, counted down to its kick
    for t, _ in re.findall(rf'cmp byte ds:\[{N}\], 0x0\njne {A}\n(?:.*\n){{0,16}}?mov byte ds:\[0x\1\], (0x[0-9A-F]+)\n', sw):
        if f'dec byte ds:[0x{t}]\n' in sw and num(t) not in out['timers']:
            out['timers'].append(num(t))
    holes = [z for z in out['zones'] if z[0] == 'hole']
    if holes and not out['timers']:
        raise Missing('hole timers')

    if rules.get('targets'):
        m, _ = find(f, 'drop targets', rf'sub al, {N}\nmov ah, 0x0\nmov di, ax\ncmp byte ds:\[di\+{N}\], 0x1\nje {A}\n'
                                       rf'(?:mov byte ds:\[di\+{A}\], 0x2\n)*mov byte ds:\[di\+0x\2\], 0x1\n'
                                       rf'mov di, 0x0\n(?:.*\n){{0,5}}?cmp di, {N}\n', v['switch_fn'])
        first, flags, count = num(m.group(1)), num(m.group(2)), num(m.group(3))
        found = []
        for i in range(count):
            regions = kick_regions(art, (first + i, first + i), 2)
            if not regions:
                raise Missing(f'drop target {i + 1}: colour {first + i:02X} is not drawn')
            r = max(regions)
            found.append((r[1], r[2], flags + i))
        if count > COUNTS['target']:
            raise Missing(f'{count} drop targets: events are made for {COUNTS["target"]}')
        for n, (x, y, a) in enumerate(sorted(found), 1):
            out['targets'].append((a, n, side(x)))

    if rules.get('lanes'):
        m, _ = find(f, 'lane lights', rf'mov byte ds:\[bx\+{N}\], 0x1\n((?:cmp byte ds:\[{A}\], 0x2\nje {A}\n)+)',
                    v['switch_fn'])
        out['lanes'] = (num(m.group(1)), m.group(2).count('cmp'))
        if f'cmp byte ds:[0x{out["lanes"][0]:X}], 0x2' not in m.group(2):
            raise Missing('lane lights: not the first one tested')

    if rules.get('ramps'):
        line = strings(data, rules['ramps'], whole=False)
        if len(line) != 1:
            raise Missing(f'bonus line {rules["ramps"]!r}: {len(line)} of them')
        m, _ = find(f, 'ramp counter', rf'mov ax, word ds:\[{N}\]\nmov cx, 0xA\n(?:.*\n){{0,12}}?'
                                        rf'lea bx, word ds:\[0x{line[0]:X}\]\n')
        out['ramps'] = num(m.group(1))

    if rules.get('messages'):
        called = set()
        for kind, text in rules['messages']:
            for at in strings(data, text):
                out['msgs'].append((at, kind))
                for _, (body, _) in f.items():
                    called |= set(re.findall(rf'lea bx, word ds:\[0x{at:X}\]\n(?:mov (?:ax|di), {A}\n){{0,2}}call (ep\d+_[0-9A-F]+)\n', body))
        if len(called) != 1:
            raise Missing(f'message function: {sorted(called) or "none"}')
        out['msg_fn'] = called.pop()
        if not f[out['msg_fn']][1]:
            raise Missing(f'{out["msg_fn"]} has no RECOMP_ENTER, so its calls cannot be seen')
    return out


# ---- the kickers in the collision map --------------------------------------------

def kick_regions(art, band, least=4):
    """Connected runs of kicker pixels: (pixels, mean x, mean y, top, bottom)."""
    lo, hi = band
    left = {i for i, c in enumerate(art) if lo <= c <= hi}
    out = []
    while left:
        todo, n, sx, sy, top, bottom = [left.pop()], 0, 0, 0, MAP_H, 0
        while todo:
            o = todo.pop()
            x, y = o % MAP_W, o // MAP_W
            n += 1
            sx += x
            sy += y
            top, bottom = min(top, y), max(bottom, y)
            for dy in range(-2, 3):           # two pixels: the outlines are dithered
                for dx in range(-2, 3):
                    p = o + dy * MAP_W + dx
                    if p in left and abs(p % MAP_W - x) <= 2:
                        left.remove(p)
                        todo.append(p)
        if n >= least:
            out.append((n, sx // n, sy // n, top, bottom))
    return sorted(out, key=lambda r: r[1])


def sling_line(regions, sling_y):
    """The map row between a table's bumpers and its slingshots. The game's
    own line (sling_y) is of the ball's top, so a low bumper hit from below
    would count as a slingshot; the gap between the two in the map does not."""
    if sling_y == 0xFFFF:
        return MAP_H
    low = [r for r in regions if r[2] >= SLINGS_BELOW]
    high = [r for r in regions if r[2] < SLINGS_BELOW]
    if not low:
        return MAP_H
    if not high:
        return 0
    over, under = max(r[4] for r in high), min(r[3] for r in low)
    if over >= under:
        raise Missing('bumpers and slingshots overlap in the collision map')
    return (over + under) // 2


def bumpers(regions, line):
    """The bumpers, left to right, each with the side of a cabinet it is on
    (0, 1, 2: by rank from the left). One bumper can be several runs of pixels
    (a dithered ring), so the largest gathers what is within its radius. A
    small group near a bumper is the rest of it, where a ramp crosses it
    (table 2); one far from any is a bumper mostly out of sight (table 11's,
    behind the wall). None of the thirteen has a kicking rubber of its own."""
    groups = []                      # [seed x, seed y, pixels, sum x, sum y]
    for n, x, y, _, _ in sorted((r for r in regions if r[2] < line), reverse=True):
        near = [g for g in groups if (g[0] - x) ** 2 + (g[1] - y) ** 2 <= 30 ** 2]
        if near:
            g = min(near, key=lambda g: (g[0] - x) ** 2 + (g[1] - y) ** 2)
            g[2] += n
            g[3] += n * x
            g[4] += n * y
        else:
            groups.append([x, y, n, n * x, n * y])
    if not groups:
        return []
    most = max(g[2] for g in groups)
    big = [g for g in groups if g[2] * 5 >= most * 2]
    for g in [g for g in groups if g[2] * 5 < most * 2]:
        x, y = g[3] // g[2], g[4] // g[2]
        near = min(big, key=lambda b: (b[3] // b[2] - x) ** 2 + (b[4] // b[2] - y) ** 2)
        if (near[3] // near[2] - x) ** 2 + (near[4] // near[2] - y) ** 2 <= RIM ** 2:
            for i in (2, 3, 4):
                near[i] += g[i]
        else:
            big.append(g)
    found = sorted((g[3] // g[2], g[4] // g[2]) for g in big)
    out = []
    for i, (x, y) in enumerate(found):
        if len(found) == 1:
            slot = 1
        elif len(found) == 2:
            slot = 2 * i
        else:
            slot = i * 3 // len(found)
        out.append((x, y, slot))
    if len(out) > MAX_BUMPERS:
        raise Missing(f'{len(out)} bumpers: events are made for {MAX_BUMPERS}')
    return out


def scan(img):
    file, load, size, fnv = lifted_as(img)
    f = read_functions(img)
    v = {}
    flippers(f, v)
    tilt_and_nudge(f, v)
    physics_and_kicker(f, v)
    plunger(f, v)
    balls_lost(f, v)
    art_seg = collision_map(f, v)
    switches(f, v)
    effects(f, v)
    sound_driver(v)

    m = re.match(rf'push ds\n(?:.*\n){{0,3}}?mov ax, {N}\nmov ds, ax\n', f[v['main_fn']][0])
    if not m:
        raise Missing('data segment')
    v['ds'] = (num(m.group(1)) - load) & 0xFFFF
    v['cs'] = int(v['main_fn'].split('_')[1], 16) >> 4      # the main routine opens its segment

    for k in ('step_fn', 'kicker_fn', 'flipper_fn'):
        if not f[v[k]][1]:
            raise Missing(f'{v[k]} has no RECOMP_ENTER, so its calls cannot be seen')

    band = kick_band(f, v)
    regions = []
    module = exe_module(img, file, size)
    at = ((art_seg - load) & 0xFFFF) * 16
    art = module[at:at + MAP_W * MAP_H]
    if (band or img in FEATURES) and len(art) != MAP_W * MAP_H:
        raise Missing('collision map runs past the EXE')
    own = None
    if img in FEATURES:
        own = features(img, f, v, art, module[v['ds'] * 16:v['ds'] * 16 + 0x10000])
    if band:
        regions = kick_regions(art, band)
        if not regions:
            raise Missing('no kicker in the collision map')
    v['sling_line'] = sling_line(regions, v['sling_y']) if band else MAP_H
    del v['sling_y']
    return dict(file=file, size=size, fnv=fnv, values=v, band=band, regions=regions,
                bumpers=bumpers(regions, v['sling_line']), own=own)


def text_of(img, r):
    out = [f'epicpinball-feedback {VERSION}', f'image {img} {r["size"]} {r["fnv"]}']
    for k, x in sorted(r['values'].items()):
        if k in ('flipper_fn', 'main_fn', 'drain_fn'):
            continue
        out.append(f'{k} {x}' if k.endswith('_fn') else f'{k} {x:X}')
    out += [f'bumper {x} {y} {slot}' for x, y, slot in r['bumpers']]       # left to right: E105 on
    out.append('board ' + ' '.join(f'{e:X}' for e in BOARD_EFFECTS + BOARD_MORE.get(img, [])))
    own = r['own']
    if own:
        layer, on_layer, tilted = own['gate']
        out.append(f'layer {layer:X}')
        out.append('on_layer ' + ' '.join(f'{c:X}' for c in on_layer))
        out.append('tilt_pass ' + ' '.join(f'{c:X}' for c in tilted))
        if own['msg_fn']:
            out.append(f'msg_fn {own["msg_fn"]}')
        out += [f'zone {k} {n} {c:X} {x0} {y0} {x1} {y1} {s}' for k, n, c, x0, y0, x1, y1, s in own['zones']]
        out += [f'timer {a:X}' for a in own['timers']]
        out += [f'target {a:X} {n} {s}' for a, n, s in own['targets']]
        if own['lanes']:
            out.append('lanes %X %d' % own['lanes'])
        if own['ramps'] is not None:
            out.append(f'ramps {own["ramps"]:X}')
        out += [f'msg {a:X} {k}' for a, k in own['msgs']]
    out.append('end')
    return '\n'.join(out) + '\n'


MENUS_VERSION = 1


def scan_menus():
    """PINBALL's menus, for --table (src/autostart.c): the functions each
    screen calls while it waits for a key, and the variables that say where
    its cursor is. -> (file, size, fnv, {key: value})"""
    file, load, size, fnv = lifted_as('pin')
    f = read_functions('pin')
    v = {}
    # each menu loop: its cursor in AH, the frame call, then the key the
    # keyboard handler left (a make code below 80h)
    loops = [(m, name) for name, (text, _) in f.items() for m in re.finditer(
        rf'mov ah, byte cs:\[{N}\]\npush cs\ncall (pin_\w+)\nmov al, byte cs:\[{N}\]\ncmp al, 0x80\n', text)]
    _, v['exec_fn'] = find(f, 'table EXEC', r'mov dx, word cs:\[bx\+' + A + r'\]\nmov al, 0x0\nmov ah, 0x4B\n')
    lists = [(m, name) for m, name in loops if f'call {v["exec_fn"]}\n' in f[name][0]]
    if len(lists) != 1:
        raise Missing(f'the table list: {len(lists)} menu loops beside the table EXEC, not one')
    m, v['menus_fn'] = lists[0]
    v['cursor'], v['frame_fn'], v['key'] = num(m.group(1)), m.group(2), num(m.group(3))
    mains = [name for m, name in loops if name != v['menus_fn'] and m.groups() == lists[0][0].groups()]
    if len(mains) != 1:         # the main menu: the one other loop on the list's cursor
        raise Missing(f'the main menu: {len(mains)} loops on the list\'s cursor, not one')
    v['main_fn'] = mains[0]
    # the table screen's own screens (rules, options): their Esc is back to it
    subs = sorted(name for m, name in loops if name not in (v['menus_fn'], v['main_fn']))
    if not subs:
        raise Missing('the table screen\'s rules and options: no menu loop of their own')
    v['sub_fns'] = subs
    m, _ = find(f, 'table screen cursor', rf'mov ah, byte cs:\[{N}\]\ncmp ah, 0x3\n{GO}mov ah, 0x3\npush cs\n'
                rf'call {v["frame_fn"]}\n', v['menus_fn'])
    v['screen_cursor'] = num(m.group(1))
    m, _ = find(f, 'list scroll and selection',
                rf'mov bl, byte cs:\[0x{v["cursor"]:X}\]\nmov bh, 0x0\nadd bx, word cs:\[{N}\]\n'
                rf'cmp bx, word cs:\[{A}\]\n{GO}jmp {A}\nmov byte cs:\[{N}\], 0x0\nmov di, 0x0\n'
                rf'cmp word cs:\[di\+{N}\], 0x1\n', v['menus_fn'])
    v['scroll'], v['selected'], v['present'] = num(m.group(1)), num(m.group(2)), num(m.group(3))
    _, v['screen_fn'] = find(f, 'table screen', rf'\Apusha\nmov di, {A}\nmov al, byte cs:\[0x{v["selected"]:X}\]\n'
                             rf'mov ah, 0x0\nmov bx, 0x28\nmul bx\n')
    m, _ = find(f, 'initials entry', rf'cmp word cs:\[{N}\], 0x0\n{GO}jmp {A}\ncmp al, 0x1\n{GO}cmp al, 0x1C\n',
                v['menus_fn'])
    v['initials'] = num(m.group(1))     # not 0 while a high score's initials are typed
    m, _ = find(f, 'main menu frames', rf'inc word cs:\[0x([0-9A-F]+)\]\ncmp word cs:\[0x\1\], {A}\n{GO}'
                rf'push cs\ncall pin_\w+\n', v['main_fn'])
    v['menu_frames'] = num(m.group(1))  # the main menu's frames toward its demo: only it counts them
    m, _ = find(f, 'intro frame', rf'push di\npush cs\ncall (pin_\w+)\npop di\ninc di\ncmp di, {A}\n{GO}'
                rf'cmp byte cs:\[0x{v["key"]:X}\], 0x80\n', v['main_fn'])
    v['intro_fn'] = m.group(1)
    _, v['end_fn'] = find(f, 'end screen', rf'inc word cs:\[({A})\]\ncmp word cs:\[\1\], 0x32\n{GO}'
                          rf'cmp byte cs:\[0x{v["key"]:X}\], 0x80\n')
    for k in ('frame_fn', 'menus_fn', 'screen_fn', 'exec_fn', 'intro_fn', 'end_fn'):
        if not f[v[k]][1]:
            raise Missing(f'{v[k]} has no RECOMP_ENTER, so its calls cannot be seen')
    # the variables are in PINBALL's first code segment, where these run
    for k in ('frame_fn', 'menus_fn', 'main_fn', 'screen_fn', 'exec_fn'):
        if int(v[k].split('_')[1], 16) >= 0x10000:
            raise Missing(f'{v[k]} is not in the first code segment')
    for k in v['sub_fns']:
        if not f[k][1]:
            raise Missing(f'{k} has no RECOMP_ENTER, so its calls cannot be seen')
    del v['main_fn']
    return file, size, fnv, v


def menus_text(size, fnv, v):
    out = [f'epicpinball-menus {MENUS_VERSION}', f'image pin {size} {fnv}']
    out += [f'{k} {x}' if k.endswith('_fn') else f'{k} {x:X}' for k, x in sorted(v.items()) if k != 'sub_fns']
    out += [f'sub_fn {x}' for x in v['sub_fns']]
    out.append('end')
    return '\n'.join(out) + '\n'


def write_menus(show):
    """work/feedback/pin.map, or why not: --table is then off"""
    try:
        _, size, fnv, v = scan_menus()
    except (Missing, OSError) as e:
        print(f'menus map: not written (--table will be off): {e}')
        return
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, 'pin.map')
    text = menus_text(size, fnv, v)
    if not os.path.exists(path) or open(path).read() != text:
        with open(path, 'w') as out:
            out.write(text)
    if show:
        print('pin: ' + ' '.join(f'{k}={x}' if k.endswith(('_fn', '_fns')) else f'{k}={x:X}' for k, x in v.items()))
    print('menus map: work/feedback/pin.map')


def main():
    show = '--show' in sys.argv
    write_menus(show)
    found, bad = {}, []
    for n in range(1, TABLES + 1):
        img = f'ep{n}'
        try:
            found[img] = scan(img)
        except (Missing, OSError) as e:
            bad.append(f'{img}: {e}')
    if bad:
        print('feedback map: not written (cabinet feedback and the analog plunger will be off)')
        for b in bad:
            print('  ' + b)
        return 1
    os.makedirs(OUT, exist_ok=True)
    for img, r in found.items():
        path = os.path.join(OUT, f'{img}.map')
        text = text_of(img, r)
        if not os.path.exists(path) or open(path).read() != text:
            with open(path, 'w') as out:
                out.write(text)
        if show:
            print(f'{img}: ' + ' '.join(f'{k}={x}' if k.endswith('_fn') else f'{k}={x:X}'
                                        for k, x in r['values'].items()))
            band = 'at run time' if not r['band'] else '%02X-%02X' % r['band']
            print(f'  kickers {band}: ' + ', '.join(f'{x},{y} ({n}px)' for n, x, y, _, _ in r['regions']))
            print('  bumpers: ' + ', '.join(f'{x},{y}={"LCR"[s]}' for x, y, s in r['bumpers']))
            if r['own']:
                o = r['own']
                print('  zones: ' + ', '.join(f'{k}_{n}={c:X}{s}' for k, n, c, *_, s in o['zones']))
                print(f'  timers {[hex(t) for t in o["timers"]]} targets {o["targets"]} lanes {o["lanes"]} '
                      f'ramps {o["ramps"]} messages {len(o["msgs"])} through {o["msg_fn"]}')
    print(f'feedback map: {len(found)} tables in work/feedback/')
    return 0


if __name__ == '__main__':
    sys.exit(main())
