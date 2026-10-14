#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.

"""Several pitch tracks of one section, one panel each over the same
spectrogram, to compare methods by eye.

Usage: compare_section.py wav start end out.png label:track.csv [label:track.csv ...]
                          [--voices voices.csv] [--fmin 60] [--fmax 1500] [--title text]

voices.csv is an annotation (time,v1,v2,v3), drawn in blue in every panel.
"""

import argparse

import numpy as np
import soundfile as sf
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def spectrogram(wav, t0, t1, fmin, fmax, n=4096, hop=256, bins=500):
    sr = sf.info(wav).samplerate
    s0 = max(0, int(t0 * sr) - n // 2)
    x, _ = sf.read(wav, start=s0, stop=int(t1 * sr) + n // 2, always_2d=True)
    y = x.mean(axis=1)
    frames = np.lib.stride_tricks.sliding_window_view(y, n)[::hop]
    S = np.abs(np.fft.rfft(frames * np.hanning(n), axis=1)) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    t = (s0 + n / 2 + np.arange(len(frames)) * hop) / sr
    logf = np.geomspace(fmin, fmax, bins)
    idx = np.clip(np.searchsorted(f, logf), 1, len(f) - 1)
    db = 10 * np.log10(S[:, idx].T + 1e-12)
    return t, logf, db


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('start', type=float)
    ap.add_argument('end', type=float)
    ap.add_argument('out')
    ap.add_argument('tracks', nargs='+')
    ap.add_argument('--voices')
    ap.add_argument('--fmin', type=float, default=60)
    ap.add_argument('--fmax', type=float, default=1500)
    ap.add_argument('--title', default='')
    a = ap.parse_args()

    t, f, db = spectrogram(a.wav, a.start, a.end, a.fmin, a.fmax)
    vmax = np.percentile(db, 99.5)
    voices = None
    if a.voices:
        v = np.loadtxt(a.voices, delimiter=',', ndmin=2)
        voices = v[(v[:, 0] >= a.start) & (v[:, 0] <= a.end)]

    n = len(a.tracks)
    fig, axs = plt.subplots(n, 1, figsize=(16, 3.2 * n), sharex=True, squeeze=False)
    ticks = [55, 82.4, 110, 164.8, 220, 329.6, 440, 659.3, 880, 1318.5]
    names = ['A1', 'E2', 'A2', 'E3', 'A3', 'E4', 'A4', 'E5', 'A5', 'E6']
    for ax, spec in zip(axs[:, 0], a.tracks):
        label, path = spec.rsplit(':', 1)
        ax.pcolormesh(t, f, db, shading='auto', cmap='Greys', vmin=vmax - 70, vmax=vmax)
        ax.set_yscale('log')
        ax.set_ylim(a.fmin, a.fmax)
        ax.set_yticks([x for x in ticks if a.fmin <= x <= a.fmax])
        ax.set_yticklabels([nm for x, nm in zip(ticks, names) if a.fmin <= x <= a.fmax])
        ax.minorticks_off()
        if voices is not None:
            for k in range(1, voices.shape[1]):
                m = voices[:, k] > 0
                ax.plot(voices[m, 0], voices[m, k], '.', color='royalblue', ms=1)
        d = np.loadtxt(path, delimiter=',', ndmin=2)
        if d.size:
            m = (d[:, 0] >= a.start) & (d[:, 0] <= a.end) & (d[:, 1] > 0)
            ax.plot(d[m, 0], d[m, 1], '.', color='red', ms=2.5)
        ax.text(0.005, 0.97, label, transform=ax.transAxes, va='top', fontsize=11,
                bbox=dict(facecolor='white', alpha=0.8, edgecolor='none'))
    axs[0, 0].set_title(a.title)
    axs[-1, 0].set_xlim(a.start, a.end)
    axs[-1, 0].set_xlabel('time (s)')
    fig.tight_layout()
    fig.savefig(a.out, dpi=65)
    print('wrote', a.out)


if __name__ == '__main__':
    main()
