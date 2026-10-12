#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.

"""Builds the pitch lab's voice-role set: synthetic harmonies whose
voices sing lines of their own at levels that change, to show which
voice a pitch track follows and how often it jumps between them. The
main test set's harmonies are pitch-shifted copies of the lead at a
fixed level, so there the louder voice is always the same one; in a
real song the louder voice changes from phrase to phrase.

Each file holds two or three of make_testset.py's synthetic voices in E
minor: a melody, and harmony lines a number of scale steps from it
(so a third is sometimes major, sometimes minor) that now and then hold
their note while the melody moves. Each voice has its own vibrato,
timing and formants and the melody's vowels. Conditions:

  alt3   a third above; the louder voice changes every 2-4 notes, by 2-6 dB
  alt6   a sixth below, likewise
  alt5   a fifth (or now and then a fourth) above, likewise
  loq    a third above, the lower voice 4 dB quieter throughout
  upq    a third above, the upper voice 4 dB quieter throughout
  close  a sixth below, the voices within 1 dB of each other
  oct    an octave below, the louder changing as in alt3
  trio   a low voice, a middle voice a third or a fourth above it and a
         top voice an octave above the low one, which comes and goes by
         phrase and sometimes mid-phrase; the loudest changing as in alt3
  sep    alt3 with echo and separation damage

All have make_testset.py's reverb on the harmonies. Sources: synm, a
male range (the dev split), and synf, a female range (the test split).

Output in OUTDIR: <condition>__<source>.wav, .gt.csv (time, then each
voice's f0 on pYIN's grid, 0 = silent; the melody first), .levels.csv
(time, then each voice's level on its own in dB, before reverb) and
manifest.json.

Usage: make_voicesets.py OUTDIR [--seed 1] [--dur 60]
"""

import argparse
import json
import os
import sys

import numpy as np
import scipy.signal as sps
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_testset as mt  # noqa: E402

SR, HOP, OFFSET = mt.SR, mt.HOP, mt.OFFSET

# name: lowest and highest MIDI note of all voices, formant shift, seed
SOURCES = {'synm': (45, 69, 1.0, 10), 'synf': (55, 79, 1.15, 20)}

# name: the other voices' scale steps from the melody (7 = an octave; a
# tuple is a choice per note), how the levels go, and extra damage
CONDS = [
    ('alt3', [(2,)], 'alternate', False),
    ('alt6', [(-5,)], 'alternate', False),
    ('alt5', [(4, 4, 4, 3)], 'alternate', False),
    ('loq', [(2,)], 'lowquiet', False),
    ('upq', [(2,)], 'upquiet', False),
    ('close', [(-5,)], 'close', False),
    ('oct', [(-7,)], 'alternate', False),
    ('trio', [(2, 3), (7,)], 'alternate', False),
    ('sep', [(2,)], 'alternate', True),
]

FULL = mt.scale_pool(24, 108)
CONSONANT = {3, 4, 5, 7, 8, 9}


def notes_of(midi):
    """(start, end, MIDI note) of each note: the runs of one target pitch."""
    change = np.flatnonzero(np.diff(midi) != 0) + 1
    bounds = np.r_[0, change, len(midi)]
    return [(a, b, midi[a]) for a, b in zip(bounds[:-1], bounds[1:]) if midi[a] > 0]


def phrases_of(notes):
    """Lists of note indices, one per phrase (notes with no rest between)."""
    out = [[0]]
    for i in range(1, len(notes)):
        if notes[i][0] == notes[i - 1][1]:
            out[-1].append(i)
        else:
            out.append([i])
    return out


def harmony_notes(rng, notes, steps, hold=0.25):
    """A note for each melody note, the given scale steps from it, or
    the last one held where that is still a consonance on the same
    side of the melody. An octave is never held."""
    out = []
    prev = None
    for i, (a, b, m) in enumerate(notes):
        st = steps[rng.integers(len(steps))]
        h = FULL[FULL.index(int(m)) + st]
        if i > 0 and notes[i - 1][1] != a:
            prev = None
        if prev is not None and abs(st) != 7 and rng.random() < hold:
            iv = (prev - m) * np.sign(st)
            if iv > 0 and iv in CONSONANT:
                h = prev
        out.append(h)
        prev = h
    return out


def envelope(rng, length):
    """A syllable's amplitude, as make_testset.melody() draws it."""
    env = np.ones(length)
    at = min(length, int(0.03 * SR))
    env[:at] = np.linspace(0.2, 1, at)
    env *= np.linspace(1.0, rng.uniform(0.6, 0.9), length)
    if rng.random() < 0.5:
        dip = min(length, int(0.04 * SR))
        env[:dip] *= np.linspace(0.4, 1, dip)
    return env * rng.uniform(0.7, 1.0)


def harmony_voice(rng, notes, pitches, vowels, n, sings):
    """Target pitch, amplitude and vowel weights per sample for a voice
    singing pitches[i] with the melody's notes where sings[i]: each
    boundary between notes 10 ms late on average, give or take 15 ms."""
    lag = {}

    def moved(s):
        if s not in lag:
            lag[s] = int(np.clip(rng.normal(0.01, 0.015), -0.03, 0.05) * SR)
        return int(np.clip(s + lag[s], 0, n))

    midi = np.zeros(n)
    amp = np.zeros(n)
    vowel_w = {v: np.zeros(n) for v in mt.VOWELS}
    for (a, b, _), h, v, on in zip(notes, pitches, vowels, sings):
        if not on:
            continue
        a, b = moved(a), moved(b)
        if b - a < int(0.05 * SR):
            continue
        midi[a:b] = h
        amp[a:b] = envelope(rng, b - a)
        vowel_w[v][a:b] = 1.0
    return midi, amp, vowel_w


def gains(rng, notes, n, roles, mode):
    """Per-sample gain in dB for each voice. roles: each voice's rank,
    0 the lowest."""
    nv = len(roles)
    g = np.zeros((nv, n))
    if mode == 'alternate':
        loud = rng.integers(nv)
        i = 0
        while i < len(notes):
            k = int(rng.integers(2, 5))
            a = notes[i][0]
            b = notes[i + k][0] if i + k < len(notes) else n
            d = rng.uniform(2, 6)
            for v in range(nv):
                g[v, a:b] = d / 2 if v == loud else -d / 2 + rng.uniform(-1, 1) * (nv > 2)
            loud = (loud + 1 + rng.integers(nv - 1)) % nv
            i += k
        g[:, :notes[0][0]] = g[:, notes[0][0]][:, None]
    else:
        for a, b, _ in notes:
            for v in range(nv):
                if mode == 'close':
                    g[v, a:b] = rng.uniform(-0.5, 0.5)
                else:
                    quiet = roles[v] == (0 if mode == 'lowquiet' else nv - 1)
                    g[v, a:b] = (-4 if quiet else 0) + rng.uniform(-1, 1)
        # rests keep the last note's gain
        idx = np.where(g[0] != 0, np.arange(n), 0)
        np.maximum.accumulate(idx, out=idx)
        g = g[:, idx]
    k = int(0.06 * SR)
    win = np.hanning(2 * k + 1)
    win /= win.sum()
    return np.stack([sps.fftconvolve(np.pad(x, k, mode='edge'), win, mode='valid') for x in g])


def levels(y, n_frames):
    """A voice's level in dB on pYIN's grid, over 2048 samples centred
    on each frame's time."""
    out = np.full(n_frames, -120.0)
    p = np.r_[np.zeros(1024), y, np.zeros(1024)]
    c = np.cumsum(np.r_[0, p ** 2])
    for i in range(n_frames):
        s = i * HOP + OFFSET
        e2 = (c[s + 2048] - c[s]) / 2048 if s + 2048 < len(c) else 0
        if e2 > 1e-12:
            out[i] = 10 * np.log10(e2)
    return out


def build_file(rng, dur, src, steps, mode, damage):
    lo, hi, formant, _ = SOURCES[src]
    # The melody's range, so that the other voices stay within the source's
    above = max(max(st) for st in steps)
    below = min(min(st) for st in steps)
    pool = mt.scale_pool(lo, hi)
    mlo = pool[max(0, -below)]
    mhi = pool[len(pool) - 1 - max(0, above)]
    mpool = mt.scale_pool(mlo, mhi)
    assert len(mpool) >= 6, (src, steps)
    midi, amp, vowel_w = mt.melody(rng, dur, mpool)
    n = len(midi)
    notes = notes_of(midi)
    vowels = [max(mt.VOWELS, key=lambda v: vowel_w[v][(a + b) // 2]) for a, b, _ in notes]
    voices = [(midi, amp, vowel_w, notes[0][2])]
    pitches = [[m for _, _, m in notes]]
    for vi, st in enumerate(steps):
        hp = harmony_notes(rng, notes, st)
        sings = [True] * len(notes)
        if len(steps) > 1 and vi == len(steps) - 1:
            # the top voice of a trio comes and goes
            for ph in phrases_of(notes):
                on = rng.random() < 0.6
                cut = ph[rng.integers(len(ph))] if rng.random() < 0.3 else None
                late = rng.random() < 0.5
                for i in ph:
                    sings[i] = on and (cut is None or (i >= cut if late else i < cut))
        hm, ha, hv = harmony_voice(rng, notes, hp, vowels, n, sings)
        voices.append((hm, ha, hv, hp[0]))
        pitches.append(hp)
    grid = mt.grid_times(n)
    ys, gts = [], []
    for vi, (m, a, w, rest) in enumerate(voices):
        fs = formant if vi == 0 else formant * rng.uniform(0.97, 1.03)
        y, gt = mt.render(rng, m, a, w, rest, fs)
        sung = a > 0.1
        rms = np.sqrt(np.mean(y[sung] ** 2)) if sung.any() else 1.0
        ys.append(y / (rms + 1e-12) * 0.1)
        gts.append(gt)
    # Each voice's rank from its mean pitch: 0 the lowest
    mean = [np.mean(p) for p in pitches]
    roles = list(np.argsort(np.argsort(mean)))
    gdb = gains(rng, notes, n, roles, mode)
    pans = [0.0, 0.45, -0.4]
    s = np.zeros((n, 2))
    lv = []
    for vi, y in enumerate(ys):
        y = y * 10 ** (gdb[vi] / 20)
        lv.append(levels(y, len(grid)))
        s += mt.pan(y, pans[vi])
    if damage:
        s = mt.echo(s, delay=60 / 143 * 0.75, feedback=0.45, wet=0.5)
    s = mt.reverb(s, rng, rt60=1.8, wet=0.3)
    if damage:
        s = mt.separation_damage(s, rng)
    s = mt.normalise(s, None)
    return s, grid, gts, lv


def build(outdir, seed, dur):
    os.makedirs(outdir, exist_ok=True)
    manifest = {'sources': list(SOURCES), 'conditions': [c[0] for c in CONDS], 'files': []}
    for src, (_, _, _, sseed) in SOURCES.items():
        for ci, (cond, steps, mode, damage) in enumerate(CONDS):
            rng = np.random.default_rng([seed, sseed, ci])
            s, g, gts, lv = build_file(rng, dur, src, steps, mode, damage)
            base = '%s__%s' % (cond, src)
            sf.write(os.path.join(outdir, base + '.wav'), s.astype(np.float32), SR, subtype='FLOAT')
            with open(os.path.join(outdir, base + '.gt.csv'), 'w') as f:
                for i, t in enumerate(g):
                    f.write('%.6f,' % t + ','.join('%.4f' % gt[i] for gt in gts) + '\n')
            with open(os.path.join(outdir, base + '.levels.csv'), 'w') as f:
                for i, t in enumerate(g):
                    f.write('%.6f,' % t + ','.join('%.1f' % x[i] for x in lv) + '\n')
            manifest['files'].append(base)
            print(base, flush=True)
    json.dump(manifest, open(os.path.join(outdir, 'manifest.json'), 'w'), indent=1)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('outdir')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--dur', type=float, default=60.0)
    a = ap.parse_args()
    build(a.outdir, a.seed, a.dur)
