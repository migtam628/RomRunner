#!/usr/bin/env python3
"""Runs JaCzekanski's ps1-tests (MIT) on the core and checks for regressions.

  suites.py --exerun build/exerun --tests <unzipped tests.zip> [--bios openbios.bin] \
            --baseline test/suite_baseline.txt [--summary out.md] [--update-baseline]

Each program prints "pass - <check>" or "fail - <check>: ..." per check;
results are recorded per check as "<program>: <check>". Programs that only
draw (GPU tests with reference VRAM images) are run but not judged until
the rasteriser lands (stage 2).

Exit status 1 if a check listed in the baseline no longer passes.
"""
import argparse
import concurrent.futures as cf
import glob
import os
import re
import subprocess
import sys

# Interactive, endless or hardware-measuring programs: run briefly or skip.
SKIP = {'cdrom/terminal', 'cdrom/disc-swap', 'spu/toolbox', 'spu/ram-sandbox', 'input/pad', 'gte-fuzz',
        'mdec/movie', 'gpu/benchmark', 'gpu/bandwidth', 'timer-dump', 'tools/diffvram'}
FRAMES = 600   # ten seconds of emulated time

PASS_RE = re.compile(r'^\s*pass\s*-\s*(.+?)\s*$', re.I)
FAIL_RE = re.compile(r'^\s*fail\s*-\s*([^:`]+?)(?::|\s|$)(.*)$', re.I)


def run(exerun, exe, bios, root):
    name = os.path.relpath(os.path.dirname(exe), root).replace(os.sep, '/')
    base = os.path.splitext(os.path.basename(exe))[0]
    if base != os.path.basename(name):
        name = f'{name}/{base}'
    args = [exerun, exe, str(FRAMES), bios or '-']
    try:
        out = subprocess.run(args, capture_output=True, text=True, errors='replace', timeout=300).stdout
    except subprocess.TimeoutExpired:
        return name, {}, 'timeout'
    checks = {}
    for line in out.splitlines():
        m = PASS_RE.match(line)
        if m:
            checks[m.group(1)] = 'PASS'
            continue
        m = FAIL_RE.match(line)
        if m:
            checks[m.group(1).strip()] = 'FAIL'
    last = [l for l in out.splitlines() if l.strip()][-1:] or ['']
    return name, checks, last[0][:80]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exerun', required=True)
    ap.add_argument('--tests', required=True)
    ap.add_argument('--bios')
    ap.add_argument('--baseline', required=True)
    ap.add_argument('--summary')
    ap.add_argument('--update-baseline', action='store_true')
    ap.add_argument('--update-baseline-to', help='write the passing checks to this file instead')
    a = ap.parse_args()

    exes = sorted(glob.glob(os.path.join(a.tests, '**', '*.exe'), recursive=True))
    exes = [e for e in exes if os.path.relpath(os.path.dirname(e), a.tests).replace(os.sep, '/') not in SKIP]
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        results = sorted(ex.map(lambda e: run(a.exerun, e, a.bios, a.tests), exes))

    passed, total = set(), 0
    rows = []
    for name, checks, last in results:
        p = sum(1 for s in checks.values() if s == 'PASS')
        f = sum(1 for s in checks.values() if s == 'FAIL')
        total += len(checks)
        passed |= {f'{name}: {c}' for c, s in checks.items() if s == 'PASS'}
        detail = f'{p} pass, {f} fail' if checks else f'no checks reported ({last})'
        rows.append(f'| {name} | {detail} |')

    if a.update_baseline_to:
        with open(a.update_baseline_to, 'w') as f:
            f.write('\n'.join(sorted(passed)) + '\n')
    baseline = set()
    if os.path.exists(a.baseline) and os.path.isfile(a.baseline):
        baseline = {l.strip() for l in open(a.baseline) if l.strip() and not l.startswith('#')}
    if a.update_baseline:
        with open(a.baseline, 'w') as f:
            f.write('# ps1-tests checks the core is known to pass (regenerate with --update-baseline)\n')
            f.write('\n'.join(sorted(passed | baseline)) + '\n')

    ran = {r[0] for r in results}
    regressions = sorted(b for b in baseline - passed if b.split(':')[0] in ran)
    new = sorted(passed - baseline)
    bios = os.path.basename(a.bios) if a.bios else 'no BIOS (HLE console output only)'
    lines = [f'## PS1 core: {len(passed)} of {total} reported checks pass ({len(results)} programs, {bios})', '']
    if regressions:
        lines += ['**Regressions:** ' + ', '.join(regressions), '']
    if new:
        lines += [f'Newly passing: {len(new)}', '']
    lines += ['| Program | Result |', '|---|---|'] + rows
    report = '\n'.join(lines)
    print(report)
    if a.summary:
        with open(a.summary, 'a') as f:
            f.write(report + '\n')
    for r in regressions:
        print(f'::error title=PS1 regression::{r} no longer passes')
    return 1 if regressions else 0


if __name__ == '__main__':
    sys.exit(main())
