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
VERSION = 2
MAX_BUMPERS = 6                 # E105-E107 and E118-E120
RIM = 40                        # a small run this near a bumper is part of it

N = r'0x([0-9A-F]+)'            # a number, kept
A = r'0x[0-9A-F]+'              # a number, not kept
GO = r'[a-z]+ ' + A + r'\n'     # any conditional jump
MAP_W, MAP_H = 320, 400
SLINGS_BELOW = 270              # on every table they sit just over the flippers


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


# ---- the kickers in the collision map --------------------------------------------

def kick_regions(art, band):
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
        if n >= 4:
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
    if band:
        at = ((art_seg - load) & 0xFFFF) * 16
        art = exe_module(img, file, size)[at:at + MAP_W * MAP_H]
        if len(art) != MAP_W * MAP_H:
            raise Missing('collision map runs past the EXE')
        regions = kick_regions(art, band)
        if not regions:
            raise Missing('no kicker in the collision map')
    v['sling_line'] = sling_line(regions, v['sling_y']) if band else MAP_H
    del v['sling_y']
    return dict(file=file, size=size, fnv=fnv, values=v, band=band, regions=regions,
                bumpers=bumpers(regions, v['sling_line']))


def text_of(img, r):
    out = [f'epicpinball-feedback {VERSION}', f'image {img} {r["size"]} {r["fnv"]}']
    for k, x in sorted(r['values'].items()):
        if k in ('flipper_fn', 'main_fn', 'drain_fn'):
            continue
        out.append(f'{k} {x}' if k.endswith('_fn') else f'{k} {x:X}')
    out += [f'bumper {x} {y} {slot}' for x, y, slot in r['bumpers']]       # left to right: E105 on
    out.append('end')
    return '\n'.join(out) + '\n'


def main():
    show = '--show' in sys.argv
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
    print(f'feedback map: {len(found)} tables in work/feedback/')
    return 0


if __name__ == '__main__':
    sys.exit(main())
