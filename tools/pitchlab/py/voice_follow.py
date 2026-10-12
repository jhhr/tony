#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.

"""Which voice a pitch track follows where two or more sing, and how
often it changes voice.

Usage: voice_follow.py song VOICES.csv TRACK.pitch.csv [TRACK...] [--json OUT]
       voice_follow.py set TESTSET RUNDIR [RUNDIR...] [--split dev|test|all]
                       [--files GLOB] [--json OUT]

A song's VOICES.csv is song_eval.py's (time,v1,v2,v3 on pYIN's grid,
voices sorted low to high on each row, 0 = absent, -1 = unsure). A test
set's .gt.csv has a column per voice after the time (0 = none), in any
order. Only frames where two or more voices sing and none is unsure
count; a track is on a voice within 50 cents of it.

  frames  multi-voice frames
  low     share of them with the track on the lowest voice
  mid3    ... on a middle voice, over frames where three or more sing
  high    ... on the highest voice
  any     ... on some voice
  VR      ... voiced
  sw/min  changes of voice per minute of multi-voice singing
  direct, gap, other
          the changes by what lies between the two frames: nothing,
          unvoiced frames only, or voiced frames on no voice (a glide
          or a wrong pitch)

The track changes voice between two consecutive frames on a voice when
it is not, at the second, on the voice nearest in pitch to the one it
was on at the first. So a voice entering or leaving, which moves the
others' ranks, is no change; a jump between voices is one, also across
a rest.
"""

import argparse
import fnmatch
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import evaluate as ev  # noqa: E402
import song_eval as se  # noqa: E402

SR, HOP = 44100, 256
TOL = 50


def sorted_voices(V):
    """Voices per row, sorted low to high, absent ones as NaN at the end."""
    Vp = np.where(V > 0, V, np.nan)
    return np.sort(Vp, axis=1)


def follow(e, V):
    """e: estimate per frame; V: frames x voices. Counts for one passage."""
    unsure = (V < 0).any(axis=1)
    Vs = sorted_voices(V)
    n_v = np.isfinite(Vs).sum(axis=1)
    multi = ~unsure & (n_v >= 2)
    three = multi & (n_v >= 3)
    voiced = e > 0
    with np.errstate(invalid='ignore', divide='ignore'):
        dev = np.abs(ev.cents(e[:, None], Vs))
    dev = np.where(np.isfinite(dev), dev, np.inf)
    near = np.argmin(dev, axis=1)
    on = multi & voiced & (dev[np.arange(len(e)), near] < TOL)
    top = n_v - 1
    on_low = on & (near == 0)
    on_high = on & (near == top)
    on_mid = on & three & (near > 0) & (near < top)

    k = np.flatnonzero(on)
    direct = gap = other = 0
    for a, b in zip(k[:-1], k[1:]):
        was = Vs[a, near[a]]
        with np.errstate(invalid='ignore', divide='ignore'):
            d = np.abs(ev.cents(Vs[b], was))
        same = int(np.nanargmin(d))
        if near[b] == same:
            continue
        between = e[a + 1:b]
        if b == a + 1:
            direct += 1
        elif np.all(between <= 0):
            gap += 1
        else:
            other += 1
    return {'frames': int(multi.sum()), 'frames3': int(three.sum()), 'low': int(on_low.sum()),
            'mid': int(on_mid.sum()), 'high': int(on_high.sum()), 'any': int(on.sum()),
            'voiced': int((multi & voiced).sum()), 'direct': direct, 'gap': gap, 'other': other}


def add(tot, c):
    for k, v in c.items():
        tot[k] = tot.get(k, 0) + v
    return tot


def shares(c):
    n = max(1, c['frames'])
    minutes = c['frames'] * HOP / SR / 60
    sw = c['direct'] + c['gap'] + c['other']
    return {'frames': c['frames'], 'low': c['low'] / n,
            'mid3': c['mid'] / c['frames3'] if c['frames3'] else float('nan'),
            'high': c['high'] / n, 'any': c['any'] / n, 'VR': c['voiced'] / n,
            'sw_per_min': sw / minutes if minutes > 0 else float('nan'),
            'direct': c['direct'], 'gap': c['gap'], 'other': c['other']}


HEADER = '%-26s %-12s %6s %6s %6s %6s %6s %6s %7s %6s %6s %6s' % (
    'track', 'part', 'frames', 'low', 'mid3', 'high', 'any', 'VR', 'sw/min', 'direct', 'gap', 'other')


def row(name, part, s):
    return '%-26s %-12s %6d %6.3f %6s %6.3f %6.3f %6.3f %7.1f %6d %6d %6d' % (
        name[:26], part, s['frames'], s['low'], '-' if np.isnan(s['mid3']) else '%6.3f' % s['mid3'],
        s['high'], s['any'], s['VR'], s['sw_per_min'], s['direct'], s['gap'], s['other'])


def song(a):
    vd = np.loadtxt(a.voices, delimiter=',', ndmin=2)
    idx = np.rint((vd[:, 0] * SR - se.OFFSET) / HOP).astype(int)
    cut = np.flatnonzero(np.diff(idx) > 1) + 1
    parts = np.split(np.arange(len(idx)), cut)
    out = {}
    print(HEADER)
    for path in a.tracks:
        est = se.load(path)
        n = max(idx.max() + 1, int(est[-1, 0] * SR / HOP) + 2 if len(est) else 0)
        e = se.to_grid(est, n)[idx]
        res = {'passages': {}}
        tot = {}
        name = os.path.basename(path)
        for p in parts:
            c = follow(e[p], vd[p, 1:])
            if c['frames'] == 0:
                continue
            label = '%.0f-%.0f' % (vd[p[0], 0], vd[p[-1], 0])
            res['passages'][label] = shares(c)
            print(row(name, label, res['passages'][label]))
            add(tot, c)
        if tot:
            res['ALL'] = shares(tot)
            print(row(name, 'ALL', res['ALL']))
        out[path] = res
    return out


def testset(a):
    manifest = json.load(open(os.path.join(a.testset, 'manifest.json')))
    srcs = {'dev': ev.DEV, 'test': ev.TEST, 'all': ev.DEV + ev.TEST}[a.split]
    files = [b for b in manifest['files']
             if b.split('__')[1] in srcs and fnmatch.fnmatch(b, a.files)]
    out = {}
    print(HEADER)
    for r in a.tracks:
        name = os.path.basename(r.rstrip('/'))
        by_cond = {}
        tot = {}
        for b in files:
            gt = np.loadtxt(os.path.join(a.testset, b + '.gt.csv'), delimiter=',', ndmin=2)
            V = gt[:, 1:]
            if not ((V > 0).sum(axis=1) >= 2).any():
                continue
            e = ev.on_grid(gt[:, 0], *ev.load_est(os.path.join(r, b + '.pitch.csv')))
            c = follow(e, V)
            add(by_cond.setdefault(b.split('__')[0], {}), c)
            add(tot, c)
        res = {cond: shares(c) for cond, c in by_cond.items()}
        for cond, s in res.items():
            print(row(name, cond, s))
        if tot:
            res['ALL'] = shares(tot)
            print(row(name, 'ALL', res['ALL']))
        out[r] = res
    return out


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='mode', required=True)
    s = sub.add_parser('song')
    s.add_argument('voices')
    s.add_argument('tracks', nargs='+')
    s.add_argument('--json')
    t = sub.add_parser('set')
    t.add_argument('testset')
    t.add_argument('tracks', nargs='+')
    t.add_argument('--split', default='all', choices=['dev', 'test', 'all'])
    t.add_argument('--files', default='*')
    t.add_argument('--json')
    a = ap.parse_args()
    out = song(a) if a.mode == 'song' else testset(a)
    if a.json:
        json.dump(out, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
