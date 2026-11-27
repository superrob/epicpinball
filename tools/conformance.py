#!/usr/bin/env python3
"""
conformance.py - does the recompiled Epic Pinball still do what the original does?

    py tools/conformance.py              # run every scenario (needs original/ and a build)
    py tools/conformance.py --update     # accept the current count as the baseline

There is no reference build to diff against frame by frame (the original runs
only under DOSBox), so the ground truth is behaviour the original visibly has:
PINBALL.EXE boots to its menu, hands each table to the right EXE, the table
draws its playfield and scoreboard, scrolls as the ball moves, plays music and
effects through the Sound Blaster, and nothing ever lands on an address with
no lifted code. Each scenario drives the game headless with scripted keys and
checks those things. See docs/conformance.md.

The game data is not redistributable, so on a machine without it (CI) every
scenario is reported as SKIP and the run passes. A count below the committed
baseline (tests/conformance_baseline.json) fails, so a regression cannot land
quietly; --update records a new, higher figure.
"""
import json
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, 'tests', 'conformance_baseline.json')
EXE = os.environ.get('EPIC_EXE') or (os.path.join(ROOT, 'build', 'Release', 'epicpinball.exe') if os.name == 'nt'
                                     else os.path.join(ROOT, 'build-linux', 'epicpinball'))
DATA = os.path.join(ROOT, 'original')
TABLES = 13


def read_bmp(path):
    """The runtime's own BMPs: 32-bit, top-down. -> (w, h, list of 0xRRGGBB)"""
    d = open(path, 'rb').read()
    w, h = struct.unpack_from('<ii', d, 18)
    h = abs(h)
    px = struct.unpack_from(f'<{w * h}I', d, 54)
    return w, h, [p & 0xFFFFFF for p in px]


def rows(img, y0, y1):
    w, h, px = img
    return px[y0 * w:min(y1, h) * w]


def bright(px, level=120):
    return sum(1 for p in px if max(p >> 16, (p >> 8) & 255, p & 255) > level)


def wav_rms(path):
    d = open(path, 'rb').read()
    n = (len(d) - 44) // 2
    if n <= 0:
        return 0.0
    s = struct.unpack_from(f'<{n}h', d, 44)
    sub = s[::max(1, n // 200000)]
    return (sum(v * v for v in sub) / len(sub)) ** 0.5


MENU_MS = 22000          # the menu is up: the notice (7 s) and the intro (13 s) are over
PLAY_MS = 30000          # of play after the table is up


def table_keys(n):
    """PINBALL's menu to table n, then a game (tools/discover.py plays the
    same script). BEGIN, n-1 short DOWNs (a held key auto-repeats),
    ENTER on the table, ENTER on PLAY GAME; on the table SPACE starts a game,
    then a long SPACE (the plunger) every 6 s -- each new ball waits in the
    lane for it -- with both flippers in between.
    -> (keys, ms the table is up, ms to run for)"""
    keys, t = [f'{MENU_MS}:ENTER'], MENU_MS + 2000        # the list takes a second to form
    for _ in range(n - 1):
        keys.append(f'{t}:DOWN+40')
        t += 500
    keys += [f'{t}:ENTER', f'{t + 2000}:ENTER']
    p = t + 9700                  # the menu fades out for 1.5 s, then 6.2 s of table intro
    keys.append(f'{p}:SPACE')
    end = p + PLAY_MS
    f = p + 2000
    while f < end - 3000:
        keys.append(f'{f}:SPACE+1500')
        for k in range(2):
            keys += [f'{f + 2000 + 1700 * k}:LSHIFT+150', f'{f + 2700 + 1700 * k}:RSHIFT+150']
        f += 6000
    return ','.join(keys), p, end


SETUP_CARDS = 8           # No Sound, PC Speaker, Gravis, PAS, SB, SB16, SB clone, SB Pro


def setup_keys(seq):
    """SETUP.EXE (--setup), a key at a time: Enter on the language first,
    then `seq`. A screen takes about a second to draw (its menus read their
    texts from SETUP.EXE); a step within one, a third of that.
    -> (keys, ms to run for)"""
    keys, t = [], 1500
    for k in ['ENTER'] + list(seq):
        keys.append(f'{t}:{k}')
        t += 300 if k in ('DOWN', 'UP') else 1200
    return ','.join(keys), t + 2000


def setup_walks():
    """Every branch of SETUP's menus, each a key script ending in Exit and
    Save (tools/discover.py lifts what they reach; each saved card then
    starts PINBALL). The main menu: Select Sound Card, Select Language,
    Instructions, Exit and Save; a card's menus (Yes/No on its BLASTER
    settings, port, IRQs, DMAs, quality) default to their first or current
    item, and DOWN x9 takes any of them to its last.
    -> {name: [keys]}"""
    w = {}
    for card in range(SETUP_CARDS):
        pick = ['ENTER'] + ['DOWN'] * card + ['ENTER']
        w[f'card{card}'] = pick + ['ENTER'] * 7
        for menu in range(6):
            seq = []
            for j in range(7):
                seq += (['DOWN'] * 9 if j == menu else []) + ['ENTER']
            w[f'card{card}_menu{menu}_last'] = pick + seq
    for lang in range(4):                 # each language, then Instructions: HELPME, its every page
        w[f'lang{lang}'] = ['DOWN', 'ENTER'] + ['DOWN'] * lang + ['ENTER', 'DOWN', 'DOWN', 'ENTER',
                           'ENTER', 'PAGEDOWN', 'P', 'ESC', 'ESC', 'ENTER', 'DOWN', 'ENTER']
    # Instructions after a card is set up: SETUP's heap is larger then, and
    # HELPME must still load where it was lifted (src/dos.c, SETUP_CHILD_SEG)
    w['card5_then_helpme'] = ['ENTER'] + ['DOWN'] * 5 + ['ENTER', 'ENTER', 'ENTER', 'UP', 'ENTER',
                                                    'ENTER', 'ESC', 'ESC', 'ENTER', 'DOWN', 'ENTER']
    for doc in range(6):                  # HELPME's every entry, 1-5 and Exit
        w[f'helpme{doc}'] = ['DOWN', 'DOWN', 'ENTER'] + ['DOWN'] * doc + ['ENTER', 'PAGEDOWN', 'DOWN', 'UP',
                            'ESC', 'ESC', 'ENTER', 'DOWN', 'ENTER']
    return w


def run(out, seconds, keys, shots, extra=(), env=None):
    save = os.path.join(out, 'save')
    os.makedirs(save, exist_ok=True)
    cmd = [EXE, '--headless', '--trace', '--seconds', str(seconds), '--save', save,
           '--data', DATA, '--wav', os.path.join(out, 'audio.wav')]
    cmd += list(extra)
    if keys:
        cmd += ['--keys', keys]
    if shots:
        cmd += ['--shot-at', ','.join(f'{ms}:{os.path.join(out, n + ".bmp")}'     # in time order
                                      for n, ms in sorted(shots.items(), key=lambda kv: kv[1]))]
    try:
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=300,
                           env=dict(os.environ, **env) if env else None)
        log, code = p.stderr.decode('latin-1'), p.returncode
    except subprocess.TimeoutExpired as e:
        log, code = (e.stderr or b'').decode('latin-1') + '\n[timeout]', -1
    open(os.path.join(out, 'run.log'), 'w', encoding='latin-1').write(log)
    return code, log


def misses(log):
    return [l for l in log.splitlines() if l.startswith('[miss]')]


def scenario_boot(out):
    code, log = run(out, MENU_MS / 1000 + 2, None, {'menu': MENU_MS})
    img = read_bmp(os.path.join(out, 'menu.bmp')) if os.path.exists(os.path.join(out, 'menu.bmp')) else None
    return [
        ('boot: clean exit', code == 0),
        ('boot: no dispatch misses', not misses(log)),
        ('boot: SETUP check answered, sound drivers placed',
         'SETUP -cd answered' in log and 'MDRV004R.MUS live' in log and 'MDRV000R.MUS live' in log),
        ('boot: menu on screen', img is not None and len(set(img[2])) >= 8 and bright(img[2]) > 500),
    ]


def scenario_table(out, n):
    keys, play, end = table_keys(n)
    a, b = play + 12000, end - 500
    # and one a second between: where the ball is at any one moment is chance
    during = {f'm{i}': play + i * 1000 for i in range(2, PLAY_MS // 1000)}
    for k in during:                   # none left from the last run
        if os.path.exists(os.path.join(out, k + '.bmp')):
            os.remove(os.path.join(out, k + '.bmp'))
    code, log = run(out, end / 1000, keys, {'a': a, 'b': b, **during})
    checks = [(f'table {n}: clean exit', code == 0),
              (f'table {n}: PINBALL ran EP{n}.EXE', f'exec ep{n}.exe' in log.lower()),
              (f'table {n}: no dispatch misses', not misses(log))]
    imgs = [read_bmp(os.path.join(out, f'{k}.bmp')) if os.path.exists(os.path.join(out, f'{k}.bmp')) else None
            for k in ('a', 'b')]
    ok = all(i is not None and i[0] == 320 and i[1] == 240 for i in imgs)
    checks.append((f'table {n}: 320x240 playfield with 32+ colours',
                   ok and all(len(set(rows(i, 0, 220))) >= 32 for i in imgs)))
    # the score display: CRTC line compare, rows 221+ from VRAM address 0
    # (lit dots: Pot of Gold's are a dark green)
    checks.append((f'table {n}: scoreboard in the split screen', ok and bright(rows(imgs[1], 221, 240), 60) > 40))
    changed = 0
    if ok:
        pa = rows(imgs[0], 0, 220)
        for k in ['b', *during]:
            p = os.path.join(out, f'{k}.bmp')
            img = read_bmp(p) if os.path.exists(p) else None
            if img and img[:2] == (320, 240):
                changed = sum(1 for x, y in zip(pa, rows(img, 0, 220)) if x != y) / len(pa)
                if changed > 0.02:
                    break
    checks.append((f'table {n}: the view moves in play', changed > 0.02))
    checks.append((f'table {n}: SB16 stereo stream started', '[sb] SB16 stereo auto-init' in log))
    checks.append((f'table {n}: audio', wav_rms(os.path.join(out, 'audio.wav')) > 200))
    return checks


def scenario_remaster(out, n, plain):
    """The remaster's picture, on a table with each scoreboard height (1-8: 20
    rows, 9-13: 30). Whole table: the page the screen scrolls over, drawn from
    VRAM -- the frame the game shows at the same moment must be a band of it --
    with the scoreboard under it. The presenter (offscreen: Direct3D 11, or
    SDL2's software renderer on Linux): that picture letterboxed into 1280x720
    with the blur beside it."""
    keys, play, end = table_keys(n)
    t = end - 500                      # scenario_table's frame `b`, same script: the same moment
    w_dir, p_dir = os.path.join(out, 'whole'), os.path.join(out, 'present')
    for d in (w_dir, p_dir):
        os.makedirs(d, exist_ok=True)
    run(w_dir, end / 1000, keys, {'w': t}, ['--whole-table'])
    run(p_dir, end / 1000, keys, {'p': t}, ['--whole-table', '--present', '1280x720'])
    get = lambda d, k: read_bmp(os.path.join(d, k + '.bmp')) if os.path.exists(os.path.join(d, k + '.bmp')) else None
    whole, shown, pres = get(w_dir, 'w'), plain, get(p_dir, 'p')
    band = False
    if whole and shown and whole[0] == 320 and 400 <= whole[1] <= 440:
        view = rows(shown, 0, 200)                             # playfield on every table
        band = any(rows(whole, k, k + 200) == view for k in range(0, whole[1] - 200 - 19 + 1))
    dmd = whole is not None and bright(rows(whole, whole[1] - 19, whole[1]), 60) > 40
    pic = bars = False
    if pres and whole and pres[0] == 1280 and pres[1] == 720:
        pw = round(720 * 320 / whole[1])                       # square pixels: the page's own shape
        x0 = (1280 - pw) // 2
        mid = [pres[2][y * 1280 + x] for y in range(0, 720, 8) for x in range(x0 + 4, x0 + pw - 4, 8)]
        side = [pres[2][y * 1280 + x] for y in range(0, 720, 8) for x in range(0, x0 - 4, 8)]
        pic = len(set(mid)) >= 64
        bars = bright(side, 8) > len(side) // 2 and bright(side, 160) == 0     # lit, but dark
    return [(f'remaster table {n}: whole table is the page the screen scrolls over', band),
            (f'remaster table {n}: scoreboard under the whole table', dmd),
            (f'remaster table {n}: presenter letterboxes it at 1280x720', pic),
            (f'remaster table {n}: a dark blur of the picture beside it', bars)]


def shots_of(out, name, seconds, keys, shots, extra=(), env=None):
    """One run's frames by name; None for one it did not write."""
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    paths = {k: os.path.join(d, k + '.bmp') for k in shots}
    if '--cabinet' in extra:
        paths.update({k + '-score': os.path.join(d, k + '-score.bmp') for k in shots})
    for p in paths.values():                        # none left from the last run
        if os.path.exists(p):
            os.remove(p)
    run(d, seconds, keys, shots, extra, env)
    return {k: read_bmp(p) if os.path.exists(p) else None for k, p in paths.items()}


def scenario_cabinet_menu(out):
    """--cabinet with no table up: the menu on the playfield, the scoreboard black."""
    plain = shots_of(out, 'plain', MENU_MS / 1000 + 1, None, {'m': MENU_MS})['m']
    cab = shots_of(out, 'cabinet', MENU_MS / 1000 + 1, None, {'m': MENU_MS}, ['--cabinet'])
    board = cab['m-score']
    # F5's stretch is for tables: the menu keeps its shape
    look = ['--cabinet', '--scale', 'nearest', '--bars', 'black']
    fit = shots_of(out, 'fit', MENU_MS / 1000 + 1, None, {'m': MENU_MS}, look + ['--present', '1080x1920'])['m']
    held = shots_of(out, 'stretch', MENU_MS / 1000 + 1, None, {'m': MENU_MS}, look + ['--stretch', '--present', '1080x1920'])['m']
    shaped = fit is not None and held == fit and not any(fit[2][:1080])       # bars above it, as fitted
    return [('cabinet menu: on the playfield, the scoreboard black, and --stretch leaves it in shape',
             plain is not None and cab['m'] == plain and board is not None and not any(board[2]) and shaped)]


def scenario_cabinet(out, n):
    """--cabinet on a table of each layout. What its two pictures must be is
    read past the composers: EPIC_VRAM0 makes the game's own scanout show 240
    rows from any address, here each page's first row and address 0."""
    dmd, page_rows, bases = (19, 399, (1600, 33600)) if n <= 8 else (29, 394, (2400, 34000))
    keys, play, end = table_keys(n)
    keys += f',{play + 13000}:ENTER'                # the game hides its scoreboard
    at = {'shown': play + 12000, 'hidden': play + 14500}
    secs = (play + 15000) / 1000
    plain = shots_of(out, 'plain', secs, keys, at)
    cab = shots_of(out, 'cabinet', secs, keys, at, ['--cabinet'])
    tops = [shots_of(out, f'from{b}', secs, keys, at, env={'EPIC_VRAM0': str(b)}) for b in bases]
    zero = shots_of(out, 'from0', secs, keys, at, env={'EPIC_VRAM0': '0'})

    def playfield(k, view):                         # view: how many rows of it the game shows
        c, p = cab[k], plain[k]
        if not c or not p or c[:2] != (320, page_rows) or p[:2] != (320, 240):
            return False
        front = any(t[k] and rows(c, 0, 240) == t[k][2] for t in tops)
        band = any(rows(c, y, y + view) == rows(p, 0, view) for y in range(page_rows - view + 1))
        return front and band                       # a page from its first row, the one on screen

    def board(k, want):
        c = cab[k + '-score']
        return c is not None and c[:2] == (320, dmd) and c[2] == want

    shown = playfield('shown', 240 - dmd)
    score = plain['shown'] is not None and board('shown', rows(plain['shown'], 240 - dmd, 240))
    hidden = (zero['hidden'] is not None and playfield('hidden', 240)
              and board('hidden', rows(zero['hidden'], 0, dmd)))

    # turned a quarter it is the portrait picture, turned: pixel for pixel
    w, h, turned = 1280, 720, False
    look = ['--cabinet', '--present']
    up = shots_of(out, 'upright', secs, keys, {'t': at['shown']}, look + [f'{h}x{w}'])['t']
    side = shots_of(out, 'turned', secs, keys, {'t': at['shown']}, look + [f'{w}x{h}', '--rotate', '90'])['t']
    if up and side and up[:2] == (h, w) and side[:2] == (w, h):
        turned = all(side[2][y * w + x] == up[2][(w - 1 - x) * h + y] for y in range(h) for x in range(w))

    # --stretch: the playfield to every pixel of the window, out of shape, turned too
    near = ['--cabinet', '--scale', 'nearest', '--bars', 'black', '--stretch', '--present']
    sup = shots_of(out, 'stretched', secs, keys, {'t': at['shown']}, near + ['1080x1920'])['t']
    sside = shots_of(out, 'stretched-turned', secs, keys, {'t': at['shown']}, near + ['1920x1080', '--rotate', '90'])['t']
    stretched, c = False, cab['shown']
    if sup and sside and c and sup[:2] == (1080, 1920) and sside[:2] == (1920, 1080):
        sw, sh = sup[:2]
        fw, fh = c[:2]
        same = sum(sup[2][y * sw + x] == c[2][int((y + .5) * fh / sh) * fw + int((x + .5) * fw / sw)]
                   for y in range(0, sh, 7) for x in range(0, sw, 5))
        edges = (any(sup[2][x] & 0xFFFFFF for x in range(sw)) and any(sup[2][(sh - 1) * sw + x] & 0xFFFFFF for x in range(sw))
                 and any(sup[2][y * sw] & 0xFFFFFF for y in range(sh)) and any(sup[2][y * sw + sw - 1] & 0xFFFFFF for y in range(sh)))
        filled = edges and same >= .95 * len(range(0, sh, 7)) * len(range(0, sw, 5))      # nearest rounds at edges
        stretched = filled and all(sside[2][y * sh + x] == sup[2][(sh - 1 - x) * sw + y]
                                   for y in range(0, sw, 3) for x in range(0, sh, 3))
    return [(f'cabinet table {n}: the playfield is the front page, no scoreboard', shown),
            (f'cabinet table {n}: the scoreboard is the rows under the split', score),
            (f'cabinet table {n}: both the same with the scoreboard hidden (Enter)', hidden),
            (f'cabinet table {n}: --rotate 90 presents the same picture, turned', turned),
            (f'cabinet table {n}: --stretch fills the display with it, turned too', stretched)]


CABINET_CHECKS = 11      # scenario_cabinet_menu's, and scenario_cabinet's on two tables


def scenario_feedback(out, n, plain_dir):
    """Cabinet feedback and the analog plunger, on a table of each layout. The
    script is scenario_table's, so the keys say what must be felt, and that
    run's frame and sound are what watching must not change."""
    keys, play, end = table_keys(n)
    t = end - 500                      # scenario_table's frame `b`

    def go(name, keys, extra, seconds=end / 1000, shots=None):
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        ev = os.path.join(d, 'events')
        for p in [ev, os.path.join(d, 'b.bmp'), os.path.join(d, 'audio.wav')]:
            if os.path.exists(p):
                os.remove(p)
        run(d, seconds, keys, shots, [*extra, '--feedback-log', ev])
        events = [ln.split() for ln in open(ev)] if os.path.exists(ev) else []
        return d, [(float(e[0]), e[2], int(e[6])) for e in events if e[3] == '1' and e[-1] == 'felt']

    def same(d):
        for f in ('b.bmp', 'audio.wav'):
            a, b = os.path.join(d, f), os.path.join(plain_dir, f)
            if not (os.path.exists(a) and os.path.exists(b)) or open(a, 'rb').read() != open(b, 'rb').read():
                return False
        return True

    d, felt = go('log', keys, [], shots={'b': t})
    tilted, since = [], None           # the script's long Spaces nudge, and tilt: no flips until the next ball
    for at, what, _ in felt:
        if what == 'tilt' and since is None:
            since = at
        if what == 'ball_release' and since is not None:
            tilted.append((since, at))
            since = None
    if since is not None:
        tilted.append((since, end))
    flips = True
    for key, side in (('LSHIFT', 'flipper_left'), ('RSHIFT', 'flipper_right')):
        pressed = [p for p in (int(k.split(':')[0]) for k in keys.split(',') if f':{key}' in k)
                   if p < end and not any(a <= p < b for a, b in tilted)]
        rises = [e[0] for e in felt if e[1] == side]
        flips &= len(rises) == len(pressed) > 0 and all(0 <= r - p < 20 for p, r in zip(pressed, rises))
    kinds = {e[1] for e in felt}
    played = {'ball_release', 'launch'} <= kinds and any(k.startswith('bumper_') for k in kinds)
    idle, _ = go('idle', keys, ['--plunger-trace', '0:0'], shots={'b': t})

    # a whole pull and a release, beside Space held down and let go
    lane = play + 1500
    menu = ','.join(k for k in keys.split(',') if int(k.split(':')[0]) < play)
    secs = (lane + 4000) / 1000
    _, by_key = go('key', f'{menu},{lane}:SPACE+2000', [], secs)
    _, by_axis = go('axis', menu, ['--plunger-trace', f'0:0,{lane}:0,{lane + 300}:100,{lane + 2000}:100,{lane + 2035}:0'], secs)
    strength = lambda evs: [e[2] for e in evs if e[1] == 'launch' and e[0] >= lane][:1]
    return [(f'feedback table {n}: a flipper event for each press unless tilted, within 20 ms', flips),
            (f'feedback table {n}: ball release, launch and bumpers are felt', played),
            (f'feedback table {n}: watching changes no pixel and no sample', same(d)),
            (f'plunger table {n}: a sensor at rest changes no pixel and no sample', same(idle)),
            (f'plunger table {n}: a full pull launches as hard as Space', strength(by_key) == strength(by_axis) != [])]


def scenario_setup(out):
    """SETUP.EXE (--setup): a Sound Blaster 16 at its defaults, saved as the
    original saves it; Instructions (HELPME.EXE) and back; a mono Sound
    Blaster set up there, then PINBALL playing its menu music with it."""
    def go(name, seq):
        o = os.path.join(out, name)
        keys, end = setup_keys(seq)
        code, log = run(o, end / 1000, keys, {}, ['--setup'])
        return o, code, log
    w = setup_walks()
    o, code, log = go('sb16', w['card5'])
    save = os.path.join(o, 'save')
    inf = os.path.join(save, 'SOUNDCRD.INF')
    drv = open(os.path.join(save, 'MDRV004R.MUS'), 'rb').read() if os.path.exists(os.path.join(save, 'MDRV004R.MUS')) else b''
    at = struct.unpack_from('<I', drv, 0x30)[0] if len(drv) > 0x34 else 0
    c = drv[at:at + 0x40] if at else b''
    o2, code2, log2 = go('helpme', w['card5_then_helpme'])
    o3, code3, log3 = go('mono', w['card4'])
    music = 0.0
    if code3 == 0:
        run(o3, MENU_MS / 1000, None, {})
        music = wav_rms(os.path.join(o3, 'audio.wav'))
    o5, code5, log5 = go('pas', w['card3_menu0_last'])        # the Pro Audio Spectrum at port 288h
    pas, plog = 0.0, ''
    if code5 == 0:
        plog = run(o5, MENU_MS / 1000, None, {})[1]
        pas = wav_rms(os.path.join(o5, 'audio.wav'))
    o4, code4, log4 = go('gravis', w['card2_menu0_last'])     # the Gravis at port 260h
    gus, glog = 0.0, ''
    if code4 == 0:
        glog = run(o4, MENU_MS / 1000, None, {})[1]
        gus = wav_rms(os.path.join(o4, 'audio.wav'))
    return [
        ('setup: a Sound Blaster 16 saved, clean exit, no misses', code == 0 and not misses(log) and os.path.exists(inf)),
        ("setup: its SOUNDCRD.INF is GOG's, byte for byte",
         os.path.exists(inf) and open(inf, 'rb').read() == open(os.path.join(DATA, 'SOUNDCRD.INF'), 'rb').read()),
        ('setup: the driver holds port 220h, IRQ 7, DMA 1, High (20,100 Hz)',
         len(c) == 0x40 and struct.unpack_from('<H', c, 0x19)[0] == 0x220 and c[0x1D] == 7 and c[0x2D] == 1
         and struct.unpack_from('<H', c, 0x31)[0] == 20100),
        ('setup: after a card, Instructions run HELPME.EXE, and SETUP carries on after it',
         code2 == 0 and not misses(log2) and 'helpme.exe exited, code 0' in log2),
        ('setup: a mono Sound Blaster set up there plays the menu music', code3 == 0 and music > 300),
        ('setup: a Gravis Ultrasound at 260h set up there plays the menu music',
         code4 == 0 and gus > 300 and '[gus] port 260' in glog and not misses(glog)),
        ('setup: a Pro Audio Spectrum at 288h set up there plays the menu music',
         code5 == 0 and pas > 300 and '[pas] port 288' in plog and not misses(plog)),
    ]


def scenario_determinism(out):
    """Headless time is deterministic: the same script twice, the same frame."""
    keys, play, end = table_keys(1)
    frames = []
    for k in ('x', 'y'):
        d = os.path.join(out, k)
        os.makedirs(d, exist_ok=True)
        run(d, end / 1000, keys, {'f': end - 1000})
        p = os.path.join(d, 'f.bmp')
        frames.append(open(p, 'rb').read() if os.path.exists(p) else None)
    return [('determinism: two runs, identical frames', frames[0] is not None and frames[0] == frames[1])]


def lift_checks():
    out = []
    gen = os.path.join(ROOT, 'work', 'gen')
    for name in sorted(os.listdir(gen)) if os.path.isdir(gen) else []:
        j = os.path.join(gen, name, 'lift.json')
        if os.path.exists(j):
            u = json.load(open(j)).get('unhandled', {})
            out.append((f'lift {name}: every instruction form handled', not u))
    return out


def main():
    update = '--update' in sys.argv
    if not os.path.exists(os.path.join(DATA, 'PINBALL.EXE')) or not os.path.exists(EXE):
        print('SKIP: no game data in original/ or no build -- conformance needs your own copy '
              '(README, Getting Started)')
        return 0
    results = lift_checks()
    skipped = 0
    tmp = os.path.join(ROOT, 'work', 'conformance')     # kept: the frames and logs explain a FAIL
    if True:
        def sub(name):
            d = os.path.join(tmp, name)
            os.makedirs(d, exist_ok=True)
            return d
        results += scenario_boot(sub('boot'))
        for n in range(1, TABLES + 1):
            results += scenario_table(sub(f'table{n}'), n)
            print(f'  table {n} done', file=sys.stderr)
        for n in (1, 9):
            b = os.path.join(tmp, f'table{n}', 'b.bmp')
            results += scenario_remaster(sub(f'remaster{n}'), n, read_bmp(b) if os.path.exists(b) else None)
        if os.name == 'nt':
            skipped = CABINET_CHECKS                 # --cabinet is Linux only
            print(f'SKIP  cabinet: Linux only ({skipped} checks)')
        else:
            results += scenario_cabinet_menu(sub('cabinet-menu'))
            for n in (1, 9):
                results += scenario_cabinet(sub(f'cabinet{n}'), n)
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'feedback_map.py')],
                       cwd=ROOT, capture_output=True)    # work/feedback; a FAIL below if it cannot
        for n in (1, 9):
            results += scenario_feedback(sub(f'feedback{n}'), n, os.path.join(tmp, f'table{n}'))
        results += scenario_setup(sub('setup'))
        results += scenario_determinism(sub('det'))
    for name, ok in results:
        print(f'{"PASS" if ok else "FAIL"}  {name}')
    passed, total = sum(ok for _, ok in results), len(results)
    print(f'\nconformance: {passed}/{total}')
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {'passed': 0, 'total': 0}
    if update:
        json.dump({'passed': passed + skipped, 'total': total + skipped}, open(BASELINE, 'w'), indent=1)
        print(f'baseline updated: {passed + skipped}/{total + skipped}')
        return 0
    if passed + skipped < base['passed']:
        print(f'REGRESSION: {passed} passed, baseline {base["passed"]}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
