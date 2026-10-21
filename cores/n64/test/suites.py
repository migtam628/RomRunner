#!/usr/bin/env python3
"""Runs the community N64 test cartridges on the core and checks for regressions.

  suites.py --romtest build/romtest --krom <PeterLemon/N64 checkout> \
            [--dillon <dir of built n64-tests .z64>] --baseline test/suite_baseline.txt \
            [--summary out.md] [--update-baseline]

krom's tests print PASS/FAIL in green/red; tests that only colour result
cells are compared with the reference screenshot shipped next to each ROM,
cell by cell (8x8 text cells classed red / green / bright / empty).
Dillon's tests report through register r30.

Exit status 1 if a test listed in the baseline no longer passes.
"""
import argparse
import concurrent.futures as cf
import glob
import os
import subprocess
import sys
import tempfile

try:
    from PIL import Image
except ImportError:  # screenshot comparison is skipped without Pillow
    Image = None


def find_font(rom, name):
    d = os.path.dirname(rom)
    for _ in range(3):
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
        d = os.path.dirname(d)
    return None


def cells(img):
    img = img.convert('RGB')
    w, h = img.size
    p = img.load()
    out = []
    for cy in range(h // 8):
        for cx in range(w // 8):
            r = g = b = 0
            for y in range(cy * 8, cy * 8 + 8):
                for x in range(cx * 8, cx * 8 + 8):
                    R, G, B = p[x, y]
                    if R > G + 60 and R > B + 60:
                        r += 1
                    elif G > R + 40 and G > B + 40:
                        g += 1
                    elif R > 150 and G > 150 and B > 150:
                        b += 1
            out.append('R' if r > 16 else 'G' if g > 16 else 'W' if b > 6 else '.')
    return out


def compare_screenshot(ppm, ref):
    if Image is None or not os.path.exists(ppm) or not os.path.exists(ref):
        return None
    a = Image.open(ppm)
    b = Image.open(ref).resize(a.size, Image.NEAREST)
    ca, cb = cells(a), cells(b)
    diff = sum(1 for x, y in zip(ca, cb) if x != y)
    marked = sum(1 for x in cb if x != '.')
    return diff, marked


def run_krom(romtest, rom, tmp):
    name = os.path.splitext(os.path.basename(rom))[0]
    red, green = find_font(rom, 'FontRed8x8.bin'), find_font(rom, 'FontGreen8x8.bin')
    if not red or not green:
        return name, 'SKIP', 'no fonts'
    ppm = os.path.join(tmp, name + '.ppm')
    env = dict(os.environ, ROMTEST_PPM=ppm, ROMTEST_RAW='1')
    try:
        out = subprocess.run([romtest, '--krom', rom, red, green], capture_output=True, text=True, timeout=300, env=env).stdout.strip()
    except subprocess.TimeoutExpired:
        return name, 'TIMEOUT', 'over 300 s'
    parts = out.split(' ', 2)
    status = parts[1] if len(parts) > 1 else 'ERROR'
    detail = parts[2] if len(parts) > 2 else out
    if status == 'TIMEOUT':
        # No PASS/FAIL words: judge by the reference screenshot instead.
        ref = os.path.splitext(rom)[0] + '.png'
        cmp = compare_screenshot(ppm, ref)
        if cmp is not None:
            diff, marked = cmp
            status = 'PASS' if diff == 0 and marked > 0 else 'FAIL'
            detail = f'screenshot: {diff} of {marked} marked cells differ'
    return name, status, detail


def pixel_match(ppm, ref, tolerance=24):
    """Percentage of pixels within tolerance of the reference (resized to ours)."""
    if Image is None or not os.path.exists(ppm):
        return None
    a = Image.open(ppm).convert('RGB')
    b = Image.open(ref).convert('RGB').resize(a.size, Image.NEAREST)
    pa, pb = a.load(), b.load()
    w, h = a.size
    good = 0
    for y in range(h):
        for x in range(w):
            p, q = pa[x, y], pb[x, y]
            if abs(p[0] - q[0]) <= tolerance and abs(p[1] - q[1]) <= tolerance and abs(p[2] - q[2]) <= tolerance:
                good += 1
    return 100.0 * good / (w * h)


def run_rdp(romtest, rom, tmp, frames=60):
    """krom's RDP tests: run, then compare the screen with the reference
    screenshot. References were captured either from the framebuffer or from
    the scaled video output, so both are tried."""
    base = os.path.splitext(rom)[0]
    name = 'rdp/' + os.path.relpath(base, os.path.dirname(os.path.dirname(rom))).replace(os.sep, '/')
    ref = base + '.png'
    if not os.path.exists(ref):
        pngs = sorted(glob.glob(os.path.join(os.path.dirname(rom), '*.png')))
        if not pngs:
            return name, 'SKIP', 'no reference'
        ref = pngs[0]
    best = None
    for raw in ('', '1'):
        ppm = os.path.join(tmp, name.replace('/', '_') + ('.raw' if raw else '') + '.ppm')
        env = dict(os.environ, ROMTEST_PPM=ppm)
        if raw:
            env['ROMTEST_RAW'] = '1'
        try:
            subprocess.run([romtest, '--shot', rom, str(frames)], capture_output=True, timeout=300, env=env)
        except subprocess.TimeoutExpired:
            return name, 'TIMEOUT', 'over 300 s'
        m = pixel_match(ppm, ref)
        if m is not None and (best is None or m > best):
            best = m
    if best is None:
        return name, 'SKIP', 'no screen'
    return name, 'PASS' if best >= 99.0 else 'FAIL', f'{best:.2f}% of pixels match'


def run_dillon(romtest, rom):
    name = os.path.splitext(os.path.basename(rom))[0]
    try:
        out = subprocess.run([romtest, '--r30', rom], capture_output=True, text=True, timeout=300).stdout.strip()
    except subprocess.TimeoutExpired:
        return 'dillon/' + name, 'TIMEOUT', 'over 300 s'
    parts = out.split(' ', 2)
    return 'dillon/' + name, parts[1] if len(parts) > 1 else 'ERROR', parts[2] if len(parts) > 2 else ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--romtest', required=True)
    ap.add_argument('--krom')
    ap.add_argument('--dillon')
    ap.add_argument('--baseline', required=True)
    ap.add_argument('--summary')
    ap.add_argument('--update-baseline', action='store_true')
    a = ap.parse_args()

    jobs = []
    tmp = tempfile.mkdtemp()
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        if a.krom:
            roms = [p for p in glob.glob(os.path.join(a.krom, '**', '*'), recursive=True)
                    if p.lower().endswith('.n64') and ('CPUTest' in p or 'RSPTest' in p)]
            jobs += [ex.submit(run_krom, a.romtest, r, tmp) for r in sorted(roms)]
            rdp = [p for p in glob.glob(os.path.join(a.krom, 'RDP', '**', '*'), recursive=True) if p.lower().endswith('.n64')]
            jobs += [ex.submit(run_rdp, a.romtest, r, tmp) for r in sorted(rdp)]
        if a.dillon:
            roms = [p for p in glob.glob(os.path.join(a.dillon, '*.z64')) if 'simpleboot' not in p]
            jobs += [ex.submit(run_dillon, a.romtest, r) for r in sorted(roms)]
        results = sorted(j.result() for j in jobs)

    passed = {n for n, s, _ in results if s == 'PASS'}
    baseline = set()
    if os.path.exists(a.baseline):
        baseline = {l.strip() for l in open(a.baseline) if l.strip() and not l.startswith('#')}
    if a.update_baseline:
        with open(a.baseline, 'w') as f:
            f.write('# Test cartridges the core is known to pass (regenerate with --update-baseline)\n')
            f.write('\n'.join(sorted(passed | {b for b in baseline if b not in {n for n, _, _ in results}})) + '\n')

    ran = {n for n, _, _ in results}
    regressions = sorted(b for b in baseline - passed if b in ran)
    skipped = sorted(b for b in baseline if b not in ran)
    new = sorted(passed - baseline)
    lines = [f'## N64 core: {len(passed)} of {len(results)} test cartridges pass', '']
    if regressions:
        lines += ['**Regressions:** ' + ', '.join(regressions), '']
    if new:
        lines += ['Newly passing: ' + ', '.join(new), '']
    if skipped:
        lines += [f'Not run here: {len(skipped)} baseline tests (suite not provided)', '']
    lines += ['| Test | Result | Detail |', '|---|---|---|']
    lines += [f'| {n} | {s} | {d} |' for n, s, d in results]
    report = '\n'.join(lines)
    print(report)
    if a.summary:
        with open(a.summary, 'a') as f:
            f.write(report + '\n')
    for r in regressions:
        print(f'::error title=N64 regression::{r} no longer passes')
    return 1 if regressions else 0


if __name__ == '__main__':
    sys.exit(main())
