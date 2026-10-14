#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.

"""Scores pitch tracks against the test set's ground truth.

Usage: evaluate.py TESTSET ESTDIR [--json out.json] [--files pattern] [--quiet]

ESTDIR holds <condition>__<source>.pitch.csv (time,f0), as pyin_cli
writes. f0 <= 0 or a missing frame is unvoiced; a negative f0 is an
unvoiced frame's pitch guess, which only the forced-voicing metric
(RPA_fv) uses.

Metrics, per file and per condition (frame-weighted):
  RPA     share of voiced frames with a voiced estimate within 50 cents of the lead
  RPA_any as RPA, within 50 cents of either voice (harmony conditions)
  RCA     as RPA, octave errors forgiven
  VR, VFA voicing recall, voicing false alarm
  OA      overall accuracy (voiced right and in tune, or unvoiced right)
  RPA_fv  RPA counting unvoiced frames' pitch guesses as voiced
  sub     share of voiced, estimated frames more than 0.8 octave below every voice
          (sub-harmonic and common-period errors)
  up      share more than 0.8 octave above every voice
  breaks  voicing breaks inside sung segments, per second of singing
  switch  lead <-> second voice switches per second (harmony conditions)
"""

import argparse
import fnmatch
import json
import os
import sys

import numpy as np

DEV = ('bob', 'synm')
TEST = ('hb', 'synf')


def load_est(path):
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return np.zeros(0), np.zeros(0)
    d = np.loadtxt(path, delimiter=',', ndmin=2)
    o = np.argsort(d[:, 0])
    return d[o, 0], d[o, 1]


def on_grid(gt_t, et, ef):
    """Estimate per ground-truth frame: nearest estimate within 0.6 hop."""
    out = np.zeros(len(gt_t))
    if len(et) == 0:
        return out
    dt = np.diff(et)
    dt = dt[dt > 1e-6]
    hop = np.median(dt) if len(dt) else 0.0058
    tol = max(0.003, 0.6 * hop)
    j = np.clip(np.searchsorted(et, gt_t), 1, len(et) - 1)
    left = et[j - 1]
    right = et[j]
    pick = np.where(np.abs(gt_t - left) <= np.abs(right - gt_t), j - 1, j)
    near = np.abs(et[pick] - gt_t) <= tol
    out[near] = ef[pick[near]]
    return out


def cents(a, b):
    with np.errstate(divide='ignore', invalid='ignore'):
        return 1200 * np.log2(np.abs(a) / np.abs(b))


def score(gt, est):
    t, lead, second = gt[:, 0], gt[:, 1], gt[:, 2]
    e = on_grid(t, *est)
    known = lead >= 0
    gv = (lead > 0) & known
    gu = (lead == 0) & known
    ev = e > 0
    c_lead = cents(e, lead)
    c_sec = cents(e, second)
    ok_lead = gv & ev & (np.abs(c_lead) < 50)
    ok_sec = gv & ev & (second > 0) & (np.abs(c_sec) < 50)
    chroma = np.abs(((c_lead + 600) % 1200) - 600) < 50
    ok_chroma = gv & ev & chroma
    fv = gv & (e != 0) & (np.abs(cents(np.abs(e), lead)) < 50)
    voices_min = np.where(second > 0, np.minimum(lead, second), lead)
    voices_max = np.where(second > 0, np.maximum(lead, second), lead)
    both = gv & ev
    sub = both & (cents(e, voices_min) < -960)
    up = both & (cents(e, voices_max) > 960)
    # voicing breaks inside sung segments
    edges = np.flatnonzero(np.diff(np.r_[0, gv.astype(int), 0]))
    breaks = 0
    for a, b in zip(edges[::2], edges[1::2]):
        seg = ev[a:b].astype(int)
        breaks += int(np.sum(np.diff(seg) == -1))
    hop_s = np.median(np.diff(t))
    sung_s = gv.sum() * hop_s
    which = np.where(ok_lead, 1, np.where(ok_sec, 2, 0))
    w = which[which > 0]
    switches = int(np.sum(w[1:] != w[:-1])) if len(w) > 1 else 0
    n_gv = max(1, gv.sum())
    n_gu = max(1, gu.sum())
    n_both = max(1, both.sum())
    return {
        'n_voiced': int(gv.sum()), 'n_unvoiced': int(gu.sum()), 'n_both': int(both.sum()),
        'RPA': ok_lead.sum() / n_gv,
        'RPA_any': (ok_lead | ok_sec).sum() / n_gv,
        'RCA': ok_chroma.sum() / n_gv,
        'VR': (gv & ev).sum() / n_gv,
        'VFA': (gu & ev).sum() / n_gu,
        'OA': (ok_lead.sum() + (gu & ~ev).sum()) / max(1, known.sum()),
        'RPA_fv': fv.sum() / n_gv,
        'sub': sub.sum() / n_both,
        'up': up.sum() / n_both,
        'breaks': breaks / max(1e-9, sung_s),
        'switch': switches / max(1e-9, sung_s),
        '_counts': {
            'ok_lead': int(ok_lead.sum()), 'ok_any': int((ok_lead | ok_sec).sum()),
            'ok_chroma': int(ok_chroma.sum()), 'gv_ev': int((gv & ev).sum()),
            'gu_ev': int((gu & ev).sum()), 'gu_uv': int((gu & ~ev).sum()),
            'known': int(known.sum()), 'fv': int(fv.sum()), 'sub': int(sub.sum()),
            'up': int(up.sum()), 'breaks': breaks, 'switches': switches, 'sung_s': sung_s,
        },
    }


def pool(results):
    c = {}
    for r in results:
        for k, v in r['_counts'].items():
            c[k] = c.get(k, 0) + v
    nv = max(1, sum(r['n_voiced'] for r in results))
    nu = max(1, sum(r['n_unvoiced'] for r in results))
    nb = max(1, sum(r['n_both'] for r in results))
    return {
        'RPA': c['ok_lead'] / nv, 'RPA_any': c['ok_any'] / nv, 'RCA': c['ok_chroma'] / nv,
        'VR': c['gv_ev'] / nv, 'VFA': c['gu_ev'] / nu,
        'OA': (c['ok_lead'] + c['gu_uv']) / max(1, c['known']),
        'RPA_fv': c['fv'] / nv, 'sub': c['sub'] / nb, 'up': c['up'] / nb,
        'breaks': c['breaks'] / max(1e-9, c['sung_s']),
        'switch': c['switches'] / max(1e-9, c['sung_s']),
    }


COLS = ['RPA', 'RPA_any', 'RCA', 'VR', 'VFA', 'OA', 'RPA_fv', 'sub', 'up', 'breaks', 'switch']


def fmt_row(name, m):
    return '%-10s ' % name + ' '.join(
        ('%7.3f' % m[k]) if k not in ('breaks', 'switch') else ('%7.2f' % m[k]) for k in COLS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('testset')
    ap.add_argument('estdir')
    ap.add_argument('--json')
    ap.add_argument('--files', default='*')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()
    manifest = json.load(open(os.path.join(a.testset, 'manifest.json')))
    per_file = {}
    for base in manifest['files']:
        if not fnmatch.fnmatch(base, a.files):
            continue
        gt = np.loadtxt(os.path.join(a.testset, base + '.gt.csv'), delimiter=',', ndmin=2)
        est = load_est(os.path.join(a.estdir, base + '.pitch.csv'))
        per_file[base] = score(gt, est)
    if not per_file:
        print('no files matched', file=sys.stderr)
        sys.exit(1)
    by_cond = {}
    for split, srcs in [('dev', DEV), ('test', TEST), ('all', DEV + TEST)]:
        for cond in manifest['conditions']:
            rs = [r for b, r in per_file.items() if b.split('__')[0] == cond and b.split('__')[1] in srcs]
            if rs:
                by_cond.setdefault(split, {})[cond] = pool(rs)
        rs = [r for b, r in per_file.items() if b.split('__')[1] in srcs]
        if rs:
            by_cond.setdefault(split, {})['ALL'] = pool(rs)
    if not a.quiet:
        header = '%-10s ' % 'condition' + ' '.join('%7s' % k for k in COLS)
        for split in ('all', 'dev', 'test'):
            if split not in by_cond:
                continue
            print('== %s (%s)' % (split, ', '.join({'dev': DEV, 'test': TEST, 'all': DEV + TEST}[split])))
            print(header)
            for cond, m in by_cond[split].items():
                print(fmt_row(cond, m))
    if a.json:
        out = {'by_condition': by_cond,
               'per_file': {b: {k: v for k, v in r.items() if k != '_counts'} for b, r in per_file.items()}}
        json.dump(out, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
