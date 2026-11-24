"""dmd_survey.py - every message a table can show, from its lifted code.

    python3 tools/dmd_survey.py [ep1 ...]        # writes work/dmd/epN.txt and .map, prints a summary

All 13 tables share one message engine (docs/dmd.md): a message function
rasterises a text into a list of dots, and "appenders" add further lines to
the same list. This finds both by their instructions, then lists every call
with what is loaded just before it: the text (BX, read from the EXE's data
segment), the effect (AL), the font (AH: 0 small, 1 large, 2 tiny; 3 more
for the same without centring) and the place (DI = y*320 + x). A value set
elsewhere is shown as '?'. Nothing here is committed output: it reads your
copy of the game.

work/dmd/epN.map is what src/dmd.c reads to put a table's messages on a DMD:
where the dot list, the effect and its frame counter are, each font the
message function and the appenders rasterise with, and on tables 1-8 the
lists of what the dots were plotted over, and the physics step (it stops
while the game waits on the player). Keyed by the EXE's size and FNV,
as the feedback maps are; no address is committed.
"""
import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import feedback_map as fm

OUT = os.path.join(fm.ROOT, 'work', 'dmd')
BACK = 14                       # instructions looked back from a call for its arguments
VERSION = 1

FONT = {0: 'small', 1: 'large', 2: 'tiny'}


def message_fns(f):
    """(the message function, its appenders), by their instructions"""
    msg = [n for n, (t, _) in f.items()
           if re.search(r'cmp cx, 0x1E\n', t) and 'mov cx, 0x140\n' in t and re.search(r'sub ah, 0x3\n', t)]
    if len(msg) != 1:
        raise fm.Missing(f'message function: found {len(msg)}, not one')
    app = sorted(n for n, (t, _) in f.items() if re.search(
        r'mov ax, ds\nmov es, ax\npush ds\nmov ax, 0x[0-9A-F]+\nmov ds, ax\n'
        r'mov si, word ds:\[0x([0-9A-F]+)\]\nmov word ds:\[0x\1\], 0x0\n', t))
    return msg[0], app


def raster(f, name, load):
    """how a rasteriser draws: (segment, offset, bytes a glyph, rows, columns,
    pixels a character advances), or None for an empty stub"""
    t = f[name][0]
    seg = re.search(r'mov cx, 0x([0-9A-F]+)\nmov ds, cx\n', t)
    glyph = re.search(r'mov al, byte ds:\[bx\+(-?)0x([0-9A-F]+)\]\n', t)
    if not seg or not glyph:
        return None
    rows = re.search(r'mov dl, 0x([0-9A-F]+)\n', t)
    cols = re.search(r'cmp cl, 0x([0-9A-F]+)\n', t)
    mul = re.search(r'mov bx, 0x([0-9A-F]+)\nmul bx\n', t)
    stride = 8 if 'shl ax, 0x3\n' in t else fm.num(mul.group(1)) if mul else None
    if not rows or not cols or stride is None:
        raise fm.Missing(f'{name}: not a rasteriser this knows')
    after = t[cols.end():].split('\n')[:4]
    tail = next((fm.num(m.group(1)) for x in after if (m := re.match(r'add di, 0x([0-9A-F]+)$', x))), 1 if 'inc di' in after else 0)
    ncols = fm.num(cols.group(1)) - 1
    off = fm.num(glyph.group(2))
    return ((fm.num(seg.group(1)) - load) & 0xFFFF, (0x10000 - off if glyph.group(1) else off) & 0xFFFF, stride,
            fm.num(rows.group(1)), ncols, ncols * 2 + tail)


def engine(img, f, msg, app):
    """the map's lines for the message engine"""
    file, load, size, fnv = fm.lifted_as(img)
    t = f[msg][0]
    v = {}
    ds = data_segment(img, f, v)[0]
    out = [f'epicpinball-dmd {VERSION}', f'image {img} {size} {fnv}', f'ds {ds:X}', f'msg_fn {msg}',
           f'step_fn {v["step_fn"]}']          # the physics step: it runs only while a ball is played
    calls = re.findall(rf'call ({img}_[0-9A-F]+)\n', t)
    if len(calls) != 3:
        raise fm.Missing(f'{msg}: calls {len(calls)} rasterisers, not 3 (tiny, large, small)')
    for font, fn in zip(('tiny', 'large', 'small'), calls):
        r = raster(f, fn, load)
        if r:
            out.append(f'font {font} {r[0]:X} {r[1]:X} {r[2]} {r[3]} {r[4]} {r[5]}')
    for fn in app:
        r = raster(f, fn, load)
        if not r:
            raise fm.Missing(f'{fn}: an appender that draws nothing')
        out.append(f'append {fn} {r[0]:X} {r[1]:X} {r[2]} {r[3]} {r[4]} {r[5]}')
    for what, pattern in (('list', r'lea si, word ds:\[0x([0-9A-F]+)\]\n'),
                          ('effect', r'mov byte ds:\[0x([0-9A-F]+)\], al\n'),
                          ('count', r'mov word ds:\[0x([0-9A-F]+)\], 0x1\n')):
        m = re.findall(pattern, t)
        if len(set(m)) != 1:
            raise fm.Missing(f'{msg}: its {what} found {len(set(m))} times, not once')
        out.append(f'{what} {m[0]}')
    saves = [(n, m) for n, (x, _) in f.items()
             for m in [re.findall(r'lea si, word ds:\[0x([0-9A-F]+)\]\nmov es, word ds:\[0x([0-9A-F]+)\]\n'
                                  r'mov word ds:\[0x\2\], ax\n', x)] if m]
    if len(saves) > 1 or (saves and len(saves[0][1]) != 2):
        raise fm.Missing('the save-under lists: not two in one renderer')
    out.append(f'family {"playfield" if saves else "bar"}')
    for lst, seg in (saves[0][1] if saves else []):
        out.append(f'save {lst} {seg}')
    return out


def data_segment(img, f, v=None):
    """the data segment, in paragraphs from where the EXE loads, and its bytes, as the map tool finds it"""
    v = {} if v is None else v
    for anchor in (fm.flippers, fm.tilt_and_nudge, fm.physics_and_kicker, fm.plunger):
        anchor(f, v)                # as the map tool does: the plunger finds the main routine
    file, load, size, _ = fm.lifted_as(img)
    m = re.match(rf'push ds\n(?:.*\n){{0,3}}?mov ax, {fm.N}\nmov ds, ax\n', f[v['main_fn']][0])
    if not m:
        raise fm.Missing('data segment')
    ds = (fm.num(m.group(1)) - load) & 0xFFFF
    module = fm.exe_module(img, file, size)
    return ds, module[ds * 16:ds * 16 + 0x10000]


def text_at(data, off):
    end = data.find(b'\0', off)
    if off >= len(data) or end < 0 or end - off > 60:
        return None
    s = data[off:end]
    return ''.join(chr(c) if 32 <= c < 127 else '?' for c in s)


def last_set(lines, reg):
    """the last value given to a register in these instructions: a number, or None"""
    for ins in reversed(lines):
        m = re.match(rf'(?:mov|lea) {reg}, (?:word ds:\[)?0x([0-9A-F]+)\]?$', ins)
        if m:
            return fm.num(m.group(1))
        if re.match(rf'\w+ {reg}\b', ins) or ins.startswith('call '):
            return None
    return None


def args(lines):
    ax = last_set(lines, 'ax')
    al, ah = (ax & 0xFF, ax >> 8) if ax is not None else (last_set(lines, 'al'), last_set(lines, 'ah'))
    return dict(bx=last_set(lines, 'bx'), al=al, ah=ah, di=last_set(lines, 'di'))


def survey(img):
    f = fm.read_functions(img)
    msg, app = message_fns(f)
    _, data = data_segment(img, f)
    rows = []
    for caller, (text, _) in sorted(f.items()):
        lines = text.split('\n')
        for i, ins in enumerate(lines):
            m = re.match(r'call (\w+)$', ins)
            if not m or m.group(1) not in [msg] + app:
                continue
            a = args(lines[max(0, i - BACK):i])
            rows.append(dict(fn=m.group(1), caller=caller, kind='msg' if m.group(1) == msg else 'line', **a,
                             text=text_at(data, a['bx']) if a['bx'] is not None else None))
    return msg, app, rows


def fmt(r):
    h = lambda x, w=2: '?' * w if x is None else f'{x:0{w}X}'
    font = '?' if r['ah'] is None else FONT.get(r['ah'] % 3, str(r['ah'])) + ('' if r['ah'] < 3 else ' left')
    place = '?' if r['di'] is None else f'y={r["di"] // 320:3d} x={r["di"] % 320:3d}'
    text = '?' if r['text'] is None else f'"{r["text"]}"'
    return f'{r["kind"]:4} {r["fn"]:12} effect {h(r["al"])} {font:11} {place:13} {text:34} from {r["caller"]}'


def main():
    imgs = sys.argv[1:] or [f'ep{t}' for t in range(1, fm.TABLES + 1)]
    os.makedirs(OUT, exist_ok=True)
    for img in imgs:
        try:
            msg, app, rows = survey(img)
        except fm.Missing as e:
            print(f'{img}: {e}')
            continue
        try:
            lines = engine(img, fm.read_functions(img), msg, app)
        except fm.Missing as e:
            print(f'{img}: no DMD map: {e}')
            lines = None
        if lines:
            with open(os.path.join(OUT, f'{img}.map'), 'w', newline='\n') as out:
                out.write('\n'.join(lines) + '\n')
        with open(os.path.join(OUT, f'{img}.txt'), 'w') as out:
            out.write(f'{img}: message function {msg}, line appenders {", ".join(app) or "none"}\n\n')
            for r in rows:
                out.write(fmt(r) + '\n')
        known = sum(r['text'] is not None for r in rows)
        print(f'{img}: {msg}, appenders {", ".join(app) or "none"}: {len(rows)} calls, {known} with a fixed text')


if __name__ == '__main__':
    main()
