#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.

"""Scores a pitch track of a real song, which has no exact ground truth.

Usage: song_eval.py EST.pitch.csv [--lyrics lyrics.csv] [--voices voices.csv]
                    [--plots OUTDIR --wav song.wav [--sections 105-126,150-172]]
                    [--json out.json] [--name label]

With timed lyrics, which say where someone sings:
  word_VR     share of frames inside a word that are voiced
  gap_VFA     share of frames more than 0.5 s from any word that are voiced

With voices.csv (time,v1,v2,v3 on pYIN's grid, for annotated passages
only; 0 = voice absent, -1 = unsure, left out), per passage and overall:
  RPA_any     voiced, within 50 cents of some annotated voice, over frames with a voice
  RPA_low     ... of the lowest voice;  RPA_high ... of the highest
  VR, VFA     voicing recall over frames with a voice, false alarms over frames with none
  sub         share of voiced frames more than 0.8 octave below every voice
  breaks      voicing breaks inside annotated voiced runs, per second
"""

import argparse
import csv
import json
import os
import subprocess
import sys
import warnings

import numpy as np

SR, HOP, OFFSET = 44100, 256, 512
HERE = os.path.dirname(os.path.abspath(__file__))


def load(path):
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return np.zeros((0, 2))
    return np.loadtxt(path, delimiter=',', ndmin=2)


def to_grid(d, n):
    out = np.zeros(n)
    if len(d) == 0:
        return out
    idx = np.rint((d[:, 0] * SR - OFFSET) / HOP).astype(int)
    ok = (idx >= 0) & (idx < n)
    if len(d) > 1:
        dt = np.median(np.diff(np.sort(d[:, 0])))
        if dt > 1.5 * HOP / SR:
            # A coarser grid (another tracker): hold each value over its hop
            t = (np.arange(n) * HOP + OFFSET) / SR
            j = np.clip(np.searchsorted(d[:, 0], t), 1, len(d) - 1)
            pick = np.where(np.abs(t - d[j - 1, 0]) <= np.abs(d[j, 0] - t), j - 1, j)
            near = np.abs(d[pick, 0] - t) <= 0.6 * dt
            out[near] = d[pick[near], 1]
            return out
    out[idx[ok]] = d[ok, 1]
    return out


def cents(a, b):
    with np.errstate(divide='ignore', invalid='ignore'):
        return 1200 * np.log2(np.abs(a) / np.abs(b))


def passage_scores(e, V):
    """e: estimate per frame; V: frames x voices (0 absent, -1 unsure)."""
    unsure = (V < 0).any(axis=1)
    Vp = np.where(V > 0, V, np.nan)
    has = (~unsure) & np.isfinite(Vp).any(axis=1)
    none = (~unsure) & ~np.isfinite(Vp).any(axis=1)
    ev = e > 0
    with np.errstate(invalid='ignore'):
        dev = np.abs(cents(e[:, None], Vp))
        ok_any = has & ev & (np.nanmin(np.where(np.isfinite(dev), dev, 1e9), axis=1) < 50)
        lo = np.nanmin(Vp, axis=1)
        hi = np.nanmax(Vp, axis=1)
        ok_lo = has & ev & (np.abs(cents(e, lo)) < 50)
        ok_hi = has & ev & (np.abs(cents(e, hi)) < 50)
        sub = has & ev & (cents(e, lo) < -960)
    edges = np.flatnonzero(np.diff(np.r_[0, has.astype(int), 0]))
    breaks = sum(int(np.sum(np.diff(ev[a:b].astype(int)) == -1)) for a, b in zip(edges[::2], edges[1::2]))
    n_has = max(1, has.sum())
    return {
        'frames_voice': int(has.sum()), 'frames_none': int(none.sum()),
        'RPA_any': ok_any.sum() / n_has, 'RPA_low': ok_lo.sum() / n_has, 'RPA_high': ok_hi.sum() / n_has,
        'VR': (has & ev).sum() / n_has, 'VFA': (none & ev).sum() / max(1, none.sum()),
        'sub': sub.sum() / max(1, (has & ev).sum()),
        'breaks': breaks / max(1e-9, has.sum() * HOP / SR),
        '_c': [int(ok_any.sum()), int(ok_lo.sum()), int(ok_hi.sum()), int((has & ev).sum()),
               int((none & ev).sum()), int(sub.sum()), breaks, int(has.sum()), int(none.sum())],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('est')
    ap.add_argument('--lyrics')
    ap.add_argument('--voices')
    ap.add_argument('--plots')
    ap.add_argument('--wav')
    ap.add_argument('--sections', default='60-82,105-127,128-144,160-182,269-295')
    ap.add_argument('--json')
    ap.add_argument('--name', default='')
    a = ap.parse_args()

    words = [(float(r[0]), float(r[1])) for r in csv.reader(open(a.lyrics))] if a.lyrics else []
    est = load(a.est)
    vd = np.loadtxt(a.voices, delimiter=',', ndmin=2) if a.voices else np.zeros((0, 4))
    # The grid reaches past the last word, estimate and annotated frame
    end = max([t0 + d for t0, d in words] + [est[-1, 0] if len(est) else 0] +
              [vd[-1, 0] if len(vd) else 0]) + 5
    n = int((end * SR - OFFSET) / HOP) + 1
    t = (np.arange(n) * HOP + OFFSET) / SR
    e = to_grid(est, n)
    out = {'name': a.name}
    if words:
        inword = np.zeros(n, bool)
        near = np.zeros(n, bool)
        for t0, d in words:
            inword |= (t >= t0) & (t <= t0 + d)
            near |= (t >= t0 - 0.5) & (t <= t0 + d + 0.5)
        first, last = words[0][0] - 5, words[-1][0] + words[-1][1] + 5
        span = (t >= first) & (t <= last)
        out['word_VR'] = float((inword & (e > 0)).sum() / max(1, inword.sum()))
        out['gap_VFA'] = float((span & ~near & (e > 0)).sum() / max(1, (span & ~near).sum()))
        print('%s word_VR %.3f  gap_VFA %.3f' % (a.name, out['word_VR'], out['gap_VFA']))

    if a.voices:
        idx = np.rint((vd[:, 0] * SR - OFFSET) / HOP).astype(int)
        V = vd[:, 1:]
        ev = e[idx]
        # passages: contiguous runs of annotated frames
        cut = np.flatnonzero(np.diff(idx) > 1) + 1
        parts = np.split(np.arange(len(idx)), cut)
        tot = np.zeros(9)
        out['passages'] = {}
        print('%-14s %7s %7s %7s %7s %7s %7s %7s' % ('passage', 'RPA_any', 'RPA_low', 'RPA_hi', 'VR', 'VFA', 'sub', 'breaks'))
        for p in parts:
            with warnings.catch_warnings():
                # Frames with no voice make all-NaN rows, which count as none
                warnings.simplefilter('ignore', RuntimeWarning)
                s = passage_scores(ev[p], V[p])
            label = '%.0f-%.0f' % (vd[p[0], 0], vd[p[-1], 0])
            out['passages'][label] = {k: v for k, v in s.items() if k != '_c'}
            tot += np.array(s['_c'])
            print('%-14s %7.3f %7.3f %7.3f %7.3f %7.3f %7.3f %7.2f' % (label, s['RPA_any'], s['RPA_low'], s['RPA_high'], s['VR'], s['VFA'], s['sub'], s['breaks']))
        hv, nn = max(1, tot[7]), max(1, tot[8])
        overall = {'RPA_any': tot[0] / hv, 'RPA_low': tot[1] / hv, 'RPA_high': tot[2] / hv, 'VR': tot[3] / hv,
                   'VFA': tot[4] / nn, 'sub': tot[5] / max(1, tot[3]), 'breaks': tot[6] / max(1e-9, tot[7] * HOP / SR)}
        out['overall'] = overall
        print('%-14s %7.3f %7.3f %7.3f %7.3f %7.3f %7.3f %7.2f' % ('OVERALL', overall['RPA_any'], overall['RPA_low'], overall['RPA_high'], overall['VR'], overall['VFA'], overall['sub'], overall['breaks']))

    if a.plots:
        if not a.wav:
            sys.exit('--plots needs --wav')
        os.makedirs(a.plots, exist_ok=True)
        for sec in a.sections.split(','):
            s0, s1 = sec.split('-')
            png = os.path.join(a.plots, '%s_%s.png' % (a.name or 'est', sec))
            cmd = [sys.executable, '-I', os.path.join(HERE, 'plot_section.py'), a.wav, s0, s1, png,
                   '%s:%s:red:3' % (a.name or 'est', a.est), '--title', '%s %s s' % (a.name, sec)]
            if a.lyrics:
                cmd += ['--lyrics', a.lyrics]
            if a.voices:
                vtmp = os.path.join(a.plots, '_voices_%s.csv' % sec)
                with open(vtmp, 'w') as f:
                    for row in np.loadtxt(a.voices, delimiter=',', ndmin=2):
                        for v in row[1:]:
                            if v > 0:
                                f.write('%.6f,%.4f\n' % (row[0], v))
                cmd.insert(cmd.index('--title'), 'annotated:%s:royalblue:0.6' % vtmp)
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        print('plots in', a.plots)
    if a.json:
        json.dump(out, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
