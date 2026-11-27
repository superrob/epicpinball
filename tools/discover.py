#!/usr/bin/env python3
"""
discover.py - lift, build, play, repeat, until the lift stops growing.

    py tools/discover.py            (python3 on Linux)

What the lift needs that the binaries alone do not say:

  - where each table EXE and sound driver lands in memory. That depends on
    what PINBALL.EXE allocated before loading it, so it is learned by running:
    src/images.c writes work/layout.txt as each image is placed.
  - the entry points nothing calls directly: interrupt handlers installed
    through a variable, routines picked out of tables. src/images.c
    writes each one the guest reaches to work/misses/<image>.txt.
  - SETUP.EXE's and HELPME.EXE's branches, which only keys reach, and the
    sound drivers its choices load: every menu of SETUP is walked
    (conformance.setup_walks), and PINBALL started on what each card saved.

So: lift what is known, build, start PINBALL headless and play every table a
little from its menu (the conformance script's input), and lift again with
what that run recorded. From an empty work/ this settles in four rounds,
about 80 seconds in all. Everything it writes is under work/, generated
from your own copy of the game, and never committed. See docs/lifting.md.
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import conformance  # noqa: E402  (the menu script and the runner)


def state():
    """What the runtime has recorded so far."""
    files = [os.path.join(ROOT, 'work', 'layout.txt')] + glob.glob(os.path.join(ROOT, 'work', 'misses', '*.txt'))
    return {f: set(open(f).read().split('\n')) for f in files if os.path.exists(f)}


def step(cmd, what):
    print(f'  {what}', flush=True)
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.stdout.write(r.stdout[-3000:] + r.stderr[-3000:])
        sys.exit(f'{what} failed')
    return r.stdout


def main():
    py = sys.executable
    for rnd in range(1, 7):
        print(f'round {rnd}')
        step([py, 'tools/lift.py'], 'lift')
        if os.name == 'nt':
            step(['cmake', '-B', 'build'], 'configure')
            step(['cmake', '--build', 'build', '--config', 'Release', '--', '-m'], 'build')
        else:                          # Linux: build-linux/epicpinball (build-linux.sh)
            step(['cmake', '-B', 'build-linux', '-DCMAKE_BUILD_TYPE=Release'], 'configure')
            step(['cmake', '--build', 'build-linux', '-j', str(os.cpu_count() or 4)], 'build')
        before = state()
        out = os.path.join(ROOT, 'work', 'discover')
        os.makedirs(out, exist_ok=True)
        print('  play: boot, then each table', flush=True)
        missed = set(conformance.misses(conformance.run(out, conformance.MENU_MS / 1000 + 2, None, {})[1]))
        for n in range(1, conformance.TABLES + 1):
            keys, _, end = conformance.table_keys(n)
            missed |= set(conformance.misses(conformance.run(out, end / 1000, keys, {})[1]))
        # SETUP.EXE (--setup): every branch of its menus, then PINBALL on
        # what each saved, so every card's driver is placed and run
        print('  setup: every menu, then each card it saves', flush=True)
        walks = conformance.setup_walks()
        def walk(item):
            name, seq = item
            o = os.path.join(out, 'setup', name)
            keys, end = conformance.setup_keys(seq)
            m = set(conformance.misses(conformance.run(o, end / 1000, keys, {}, ['--setup'])[1]))
            if name.startswith('card'):
                # PINBALL on the card it saved: its driver loads (as set up) and plays the menu's music
                m |= set(conformance.misses(conformance.run(o, conformance.MENU_MS / 1000, None, {})[1]))
            return m
        for item in sorted(walks.items()):   # one at a time: each run rewrites work/layout.txt
            missed |= walk(item)
        after = state()
        for f in sorted(set(before) | set(after)):
            new = sorted(x for x in after.get(f, set()) - before.get(f, set()) if x)
            if new:
                print(f'    {os.path.relpath(f, ROOT)}: ' + ', '.join(new[:8]) + (' ...' if len(new) > 8 else ''))
        if not missed and after == before:
            print(f'settled after {rnd} round{"s" if rnd > 1 else ""}: every address reached is lifted')
            return 0
        if missed and after == before:
            # recorded already, lifted, and still missing: a lifter or runtime bug
            print('misses the lift does not fix:\n  ' + '\n  '.join(sorted(missed)))
            return 1
    print('still finding code after 6 rounds; run again, or see docs/lifting.md')
    return 1


if __name__ == '__main__':
    sys.exit(main())
