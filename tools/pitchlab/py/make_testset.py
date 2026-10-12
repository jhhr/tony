#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.

"""Builds the pitch lab's test set: clean singing with a known f0,
degraded the ways an AI-separated vocal stem of a produced song is.

Sources: two clean recordings of singing whose f0 is taken from pYIN
on the clean audio (where pYIN is reliable), and two synthetic voices
whose f0 is exact. Conditions: echo (tempo-synced ping-pong delay),
reverb, both, a second voice in harmony (thirds and sixths, as in a
double-tracked harmony), a doubled lead, instrument bleed, separation
damage (masked spectrum, thin low end), and combinations.

Each file is stereo (centre-extraction ideas need both channels), at a
level like the stems (frame RMS around 0.1 while singing), with a
ground truth CSV: time, lead f0, second voice f0 (0 = none).

Usage: make_testset.py OUTDIR --pyin PYIN_CLI [--seed 1]
"""

import argparse
import json
import os
import subprocess
import sys

import numpy as np
import scipy.signal as sps
import soundfile as sf

SR = 44100
HOP = 256
OFFSET = 512  # pYIN's timestamp offset (block size / 4)

HERE = os.path.dirname(os.path.abspath(__file__))
TONY = os.path.abspath(os.path.join(HERE, '..', '..', '..'))

REAL_SOURCES = {
    'bob': os.path.join(TONY, 'pyin', 'testdata', 'bob_02.wav'),
    'hb': os.path.join(TONY, 'testdata', 'happy_birthday_gp_masked.wav'),
}


def grid_times(n_samples):
    n = 1 + (n_samples - 1) // HOP
    return (np.arange(n) * HOP + OFFSET) / SR


# ---------------------------------------------------------------- sources

def real_source(name, path, pyin_cli, workdir):
    x, sr = sf.read(path, always_2d=True)
    assert sr == SR
    x = x.mean(axis=1)
    prefix = os.path.join(workdir, 'gt_' + name)
    subprocess.run([pyin_cli, path, prefix], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    d = np.loadtxt(prefix + '.pitch.csv', delimiter=',', ndmin=2)
    t = grid_times(len(x))
    f0 = np.zeros(len(t))
    idx = np.rint((d[:, 0] * SR - OFFSET) / HOP).astype(int)
    ok = (idx >= 0) & (idx < len(t))
    f0[idx[ok]] = d[ok, 1]
    # Short islands are where clean pYIN itself is least sure: leave
    # them out of the ground truth by marking them unknown (-1)
    voiced = f0 > 0
    edges = np.flatnonzero(np.diff(np.r_[0, voiced.astype(int), 0]))
    for a, b in zip(edges[::2], edges[1::2]):
        if b - a < 8:
            f0[a:b] = -1
    return x, f0


VOWELS = {  # F1, F2, F3, F4 in Hz (roughly, adult)
    'a': (730, 1090, 2440, 3400),
    'e': (530, 1840, 2480, 3500),
    'i': (300, 2200, 2950, 3700),
    'o': (570, 840, 2410, 3300),
    'u': (320, 900, 2300, 3300),
}


SCALE = [4, 6, 7, 9, 11, 12, 14]  # E minor degrees (semitones from C)


def scale_pool(low_midi, high_midi):
    return [m for m in range(low_midi, high_midi + 1) if (m % 12) in [s % 12 for s in SCALE]]


def synth_voice(rng, dur, low_midi, high_midi, formant_shift=1.0):
    """A sung melody in E minor with portamento, vibrato, drift,
    jitter, vowels and breath. Returns audio and exact f0 on the grid."""
    pool = scale_pool(low_midi, high_midi)
    midi, amp, vowel_w = melody(rng, dur, pool)
    return render(rng, midi, amp, vowel_w, pool[len(pool) // 2], formant_shift)


def melody(rng, dur, pool):
    """A melody on the notes of pool, in phrases: target pitch (0 =
    rest), amplitude and vowel weights per sample."""
    n = int(dur * SR)
    midi = np.zeros(n)  # target pitch per sample, 0 = rest
    amp = np.zeros(n)
    vowel_w = {v: np.zeros(n) for v in VOWELS}
    t = 0.3
    cur = rng.choice(pool[len(pool) // 3: 2 * len(pool) // 3])
    while t < dur - 1.0:
        phrase = rng.uniform(1.8, 5.0)
        tp = t
        while tp < min(t + phrase, dur - 0.6):
            nd = rng.choice([0.18, 0.25, 0.35, 0.5, 0.7, 1.0, 1.5])
            a, b = int(tp * SR), int(min(tp + nd, dur - 0.5) * SR)
            step = rng.choice([-3, -2, -1, -1, 0, 1, 1, 2, 3, 4, -4])
            j = int(np.clip(pool.index(cur) + step, 0, len(pool) - 1))
            cur = pool[j]
            midi[a:b] = cur
            v = rng.choice(list(VOWELS))
            vowel_w[v][a:b] = 1.0
            # syllable envelope: attack, gentle decay; a short dip between syllables
            env = np.ones(b - a)
            at = min(len(env), int(0.03 * SR))
            env[:at] = np.linspace(0.2, 1, at)
            env *= np.linspace(1.0, rng.uniform(0.6, 0.9), len(env))
            if rng.random() < 0.5:
                dip = min(len(env), int(0.04 * SR))
                env[:dip] *= np.linspace(0.4, 1, dip)
            amp[a:b] = env * rng.uniform(0.7, 1.0)
            tp += nd
        t = tp + rng.uniform(0.3, 1.2)
    return midi, amp, vowel_w


def render(rng, midi, amp, vowel_w, rest_midi, formant_shift=1.0):
    """Sings a melody: portamento, vibrato, drift, jitter, vowels and
    breath, starting from rest_midi. Returns audio and exact f0 on the
    grid."""
    n = len(midi)
    voiced = midi > 0
    # Fill rests with neighbouring pitch so that glides are continuous
    idx = np.where(voiced, np.arange(n), 0)
    np.maximum.accumulate(idx, out=idx)
    pitch = midi[idx]
    pitch[pitch == 0] = rest_midi
    # portamento: smooth the step function with a ~60 ms raised cosine
    k = int(0.06 * SR)
    win = np.hanning(2 * k + 1)
    win /= win.sum()
    pitch = sps.fftconvolve(np.pad(pitch, k, mode='edge'), win, mode='valid')
    # vibrato that grows in over each note, slow drift and jitter
    tt = np.arange(n) / SR
    rate = rng.uniform(5.0, 6.3)
    depth = np.zeros(n)
    edges = np.flatnonzero(np.diff(np.r_[0, (midi > 0).astype(int), 0]))
    note_change = np.flatnonzero(np.diff(midi) != 0) + 1
    starts = np.unique(np.r_[edges[::2], note_change])
    for s in starts:
        e = s + 1
        while e < n and midi[e] == midi[s]:
            e += 1
        L = e - s
        if midi[s] == 0 or L < int(0.4 * SR):
            continue
        d = rng.uniform(0.15, 0.6)  # semitones peak
        ramp = np.clip((np.arange(L) - 0.2 * SR) / (0.3 * SR), 0, 1)
        depth[s:e] = d * ramp
    vib = depth * np.sin(2 * np.pi * rate * tt + rng.uniform(0, 6.28))
    drift = sps.lfilter([1], [1, -0.99995], rng.normal(0, 1, n))
    drift = 0.08 * drift / (np.abs(drift).max() + 1e-9)
    jit = sps.lfilter(*sps.butter(2, 25 / (SR / 2)), rng.normal(0, 1, n))
    jit = 0.03 * jit / (np.std(jit) + 1e-9)
    pitch = pitch + vib + drift + jit
    f0 = 440 * 2 ** ((pitch - 69) / 12)
    # smooth amplitude and vowel weights
    sm = np.hanning(int(0.02 * SR))
    sm /= sm.sum()
    amp = sps.fftconvolve(amp, sm, mode='same')
    tot = np.zeros(n)
    vowel_w = {v: sps.fftconvolve(w, np.hanning(int(0.05 * SR)) / (0.025 * SR), mode='same')
               for v, w in vowel_w.items()}
    for v in VOWELS:
        tot += vowel_w[v]
    tot[tot < 1e-6] = 1
    phase = 2 * np.pi * np.cumsum(f0) / SR
    y = np.zeros(n)
    shimmer = 1 + 0.08 * sps.lfilter(*sps.butter(2, 30 / (SR / 2)), rng.normal(0, 1, n))
    hmax = int(5500 / f0.min())
    for h in range(1, hmax + 1):
        fh = h * f0
        a = h ** -1.3 * (fh < 5500)
        env = np.zeros(n)
        for v, form in VOWELS.items():
            g = np.zeros(n)
            for i, F in enumerate(form):
                F = F * formant_shift
                bw = 70 + 0.06 * F
                g += (1.0 / (1 + ((fh - F) / bw) ** 2)) * (0.9 ** i)
            env += vowel_w[v] / tot * (0.15 + g)
        y += a * env * np.sin(h * phase + rng.uniform(0, 6.28))
    y *= amp * shimmer
    breath = sps.lfilter(*sps.butter(2, [1000 / (SR / 2), 6000 / (SR / 2)], 'band'), rng.normal(0, 1, n))
    y += 0.03 * np.std(y) / (np.std(breath) + 1e-9) * breath * (amp > 0.05)
    g = grid_times(n)
    gi = np.clip((g * SR).astype(int), 0, n - 1)
    gt = np.where(amp[gi] > 0.1, f0[gi], 0.0)
    return y, gt


# ----------------------------------------------------------- degradations

def lowpass(x, hz, order=2):
    b, a = sps.butter(order, hz / (SR / 2))
    return sps.lfilter(b, a, x)


def echo(stereo, delay, feedback, wet, lp_hz=3500.0):
    """Ping-pong feedback delay: odd repeats left, even right, each
    darker than the last."""
    mono = stereo.mean(axis=1)
    out = stereo.copy()
    d = int(delay * SR)
    tap = mono.copy()
    gain = wet
    k = 1
    while gain > 0.003 and k * d < len(mono):
        tap = lowpass(tap, lp_hz)
        ch = 0 if k % 2 else 1
        out[k * d:, ch] += gain * tap[:len(mono) - k * d] * 1.4
        gain *= feedback
        k += 1
    return out


def reverb(stereo, rng, rt60=1.8, wet=0.35, predelay=0.02):
    n = int(rt60 * 1.2 * SR)
    t = np.arange(n) / SR
    out = stereo.copy()
    mono = stereo.mean(axis=1)
    for ch in range(2):
        ir = rng.normal(0, 1, n) * np.exp(-6.9 * t / rt60)
        # later parts darker
        ir = 0.5 * ir + 0.5 * lowpass(ir, 2500)
        ir = np.r_[np.zeros(int(predelay * SR)), ir]
        ir /= np.sqrt(np.sum(ir ** 2))
        wetsig = sps.fftconvolve(mono, ir)[:len(mono)]
        out[:, ch] += wet * wetsig * (np.std(mono) / (np.std(wetsig) + 1e-12))
    return out


def pitch_shift(y, semitones):
    import librosa
    return librosa.effects.pitch_shift(y, sr=SR, n_steps=semitones, n_fft=2048, hop_length=256)


def pan(y, p):
    """p in [-1, 1]: constant-power pan of a mono signal to stereo."""
    a = (p + 1) * np.pi / 4
    return np.stack([np.cos(a) * y, np.sin(a) * y], axis=1) * np.sqrt(2)


def doubled(y, rng):
    """A second take of the same line: 20-40 ms late, slowly wandering
    by a few cents (a modulated fractional delay)."""
    n = len(y)
    t = np.arange(n) / SR
    base = rng.uniform(0.02, 0.04)
    dl = base + 0.004 * np.sin(2 * np.pi * 0.25 * t + rng.uniform(0, 6)) \
        + 0.002 * np.sin(2 * np.pi * 0.61 * t)
    pos = np.arange(n) - dl * SR
    return np.interp(pos, np.arange(n), y, left=0, right=0)


def bleed(n, rng, level):
    """Quiet accompaniment leaking into the stem: a picked E minor
    arpeggio (Karplus-Strong) and a bass line, both low-passed."""
    out = np.zeros(n)
    bass = np.zeros(n)
    chords = [[40, 47, 52, 55, 59, 64], [45, 52, 57, 60, 64], [43, 50, 55, 59, 62], [47, 54, 59, 62, 66]]
    t = 0.0
    ci = 0
    step = 60 / 143 / 2
    while t < n / SR - 0.5:
        chord = chords[ci % len(chords)]
        for k in range(8):
            note = chord[k % len(chord)] if k < 6 else chord[(6 - k) % len(chord)]
            f = 440 * 2 ** ((note - 69) / 12)
            L = int(SR / f)
            buf = rng.uniform(-1, 1, L)
            m = int(1.2 * SR)
            s = int(t * SR)
            if s + m > n:
                m = n - s
            if m <= L:
                break
            # Karplus-Strong: y[i] = x[i] + 0.498 (y[i-L] + y[i-L-1])
            exc = np.zeros(m)
            exc[:L] = buf
            den = np.zeros(L + 2)
            den[0] = 1
            den[L] = den[L + 1] = -0.498
            y = sps.lfilter([1.0], den, exc)
            out[s:s + m] += y * 0.6
            t += step
        root = 440 * 2 ** ((chord[0] - 12 - 69) / 12)
        s = int((t - 8 * step) * SR)
        m = min(int(8 * step * SR), n - s)
        if m > 0:
            tt = np.arange(m) / SR
            bass[s:s + m] += (np.sin(2 * np.pi * root * tt) + 0.4 * np.sin(4 * np.pi * root * tt)) * np.exp(-tt / 1.2)
        ci += 1
    out = lowpass(out, 3000) + 0.8 * bass
    return level * out / (np.std(out) + 1e-12)


def separation_damage(stereo, rng, lowcut=130.0, mask_depth=0.7, dropout=0.08):
    """What a source separator does to a stem: a soft, fluttering
    time-frequency mask, holes, and a thinned low end."""
    out = np.zeros_like(stereo)
    nfft, hop = 2048, 512
    f = np.fft.rfftfreq(nfft, 1 / SR)
    lowgain = np.clip((f / lowcut) ** 2, 0.15, 1)
    for ch in range(2):
        _, _, Z = sps.stft(stereo[:, ch], fs=SR, nperseg=nfft, noverlap=nfft - hop)
        if ch == 0:
            noise = rng.normal(0, 1, Z.shape)
            noise = sps.fftconvolve(noise, np.ones((3, 3)) / 9, mode='same')
            mask = np.clip(1 - mask_depth * np.abs(noise) * 1.6, 0.05, 1)
            mask[rng.random(Z.shape) < dropout] = 0.05
        Z = Z * mask * lowgain[:, None]
        _, y = sps.istft(Z, fs=SR, nperseg=nfft, noverlap=nfft - hop)
        out[:, ch] = y[:len(stereo)] if len(y) >= len(stereo) else np.r_[y, np.zeros(len(stereo) - len(y))]
    return out


# ------------------------------------------------------------------ build

def normalise(stereo, voiced_mask_samples):
    m = stereo.mean(axis=1)
    fr = np.lib.stride_tricks.sliding_window_view(m, 2048)[::512]
    rms = np.sqrt((fr ** 2).mean(axis=1))
    level = np.percentile(rms, 95) if len(rms) else 1
    g = 0.1 / (level + 1e-12)
    out = stereo * g
    peak = np.abs(out).max()
    if peak > 0.98:
        out *= 0.98 / peak
    return out


def build(outdir, pyin_cli, seed):
    os.makedirs(outdir, exist_ok=True)
    work = os.path.join(outdir, '_work')
    os.makedirs(work, exist_ok=True)
    rng = np.random.default_rng(seed)
    sources = {}
    for name, path in REAL_SOURCES.items():
        sources[name] = real_source(name, path, pyin_cli, work)
    sources['synm'] = synth_voice(np.random.default_rng(seed + 10), 60.0, 45, 64, 1.0)
    sources['synf'] = synth_voice(np.random.default_rng(seed + 20), 60.0, 55, 76, 1.15)

    conds = ['clean', 'echo', 'reverb', 'studio', 'harmA', 'harmB', 'harmC',
             'double', 'bleed', 'sepdmg', 'moises', 'all']
    manifest = {'sources': list(sources), 'conditions': conds, 'files': []}

    for sname, (y, gt) in sources.items():
        y = y / (np.std(y) + 1e-12) * 0.1
        n = len(y)
        g = grid_times(n)
        harm = {}
        for key, st in [('harmA', 4), ('harmB', -3), ('harmC', 9)]:
            harm[key] = (pitch_shift(y, st), st)
        crng = np.random.default_rng(seed + 100 + len(sname))
        for cond in conds:
            f2 = np.zeros(len(gt))
            lead = pan(y, 0.0)
            if cond in ('harmA', 'harmB', 'harmC', 'all'):
                key = 'harmA' if cond == 'all' else cond
                h, st = harm[key]
                lvl = {'harmA': 10 ** (-2 / 20), 'harmB': 1.0, 'harmC': 10 ** (-3 / 20)}[key]
                s = lead + pan(h * lvl, 0.45 if key != 'harmB' else -0.45)
                f2 = np.where(gt > 0, gt * 2 ** (st / 12), 0)
            elif cond == 'double':
                s = pan(y, -0.15) * 0.8 + pan(doubled(y, crng), 0.6) * 0.8
            else:
                s = lead
            if cond in ('echo', 'studio', 'moises', 'all'):
                s = echo(s, delay=60 / 143 * 0.75, feedback=0.45, wet=0.5)
            if cond in ('reverb', 'studio', 'moises', 'all', 'harmA', 'harmB', 'harmC', 'double'):
                s = reverb(s, crng, rt60=1.8, wet=0.3 if cond.startswith('harm') or cond == 'double' else 0.4)
            if cond in ('bleed', 'moises', 'all'):
                b = bleed(n, crng, level=0.025)
                s = s + pan(b, 0.2)
            if cond in ('sepdmg', 'moises', 'all'):
                s = separation_damage(s, crng)
            s = normalise(s, None)
            base = '%s__%s' % (cond, sname)
            sf.write(os.path.join(outdir, base + '.wav'), s.astype(np.float32), SR, subtype='FLOAT')
            with open(os.path.join(outdir, base + '.gt.csv'), 'w') as f:
                for t, a, b in zip(g, gt, f2):
                    f.write('%.6f,%.4f,%.4f\n' % (t, a, b))
            manifest['files'].append(base)
            print(base, flush=True)
    json.dump(manifest, open(os.path.join(outdir, 'manifest.json'), 'w'), indent=1)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('outdir')
    ap.add_argument('--pyin', required=True)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    build(a.outdir, a.pyin, a.seed)
