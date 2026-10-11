#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.

# Spectrogram of a section with pitch tracks overlaid.
# Usage: plot_section.py wav start end out.png [label:csv:color[:marker] ...] [--lyrics lyrics.csv]
#        [--fmin 40] [--fmax 1500] [--chan mid|left|right|side]
import sys, argparse
import numpy as np
import soundfile as sf
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ap = argparse.ArgumentParser()
ap.add_argument('wav'); ap.add_argument('start', type=float); ap.add_argument('end', type=float)
ap.add_argument('out'); ap.add_argument('tracks', nargs='*')
ap.add_argument('--lyrics'); ap.add_argument('--fmin', type=float, default=40)
ap.add_argument('--fmax', type=float, default=1500); ap.add_argument('--chan', default='mid')
ap.add_argument('--win', type=int, default=4096); ap.add_argument('--hop', type=int, default=256)
ap.add_argument('--title', default='')
ap.add_argument('--width', type=float, default=22); ap.add_argument('--height', type=float, default=9)
a = ap.parse_args()

info = sf.info(a.wav)
sr = info.samplerate
s0 = max(0, int((a.start) * sr) - a.win // 2)
s1 = int(a.end * sr) + a.win // 2
x, _ = sf.read(a.wav, start=s0, stop=s1, always_2d=True, dtype='float64')
if x.shape[1] == 1 or a.chan == 'mid':
    y = x.mean(axis=1)
elif a.chan == 'left':
    y = x[:, 0]
elif a.chan == 'right':
    y = x[:, 1]
elif a.chan == 'side':
    y = (x[:, 0] - x[:, 1]) / 2
win = np.hanning(a.win)
n = 1 + (len(y) - a.win) // a.hop
frames = np.lib.stride_tricks.as_strided(y, shape=(n, a.win), strides=(y.strides[0] * a.hop, y.strides[0]))
S = np.abs(np.fft.rfft(frames * win, axis=1)) ** 2
freqs = np.fft.rfftfreq(a.win, 1 / sr)
times = (s0 + a.win / 2 + np.arange(n) * a.hop) / sr
# log-frequency resampling
nb = 600
logf = np.geomspace(a.fmin, a.fmax, nb)
idx = np.clip(np.searchsorted(freqs, logf), 1, len(freqs) - 1)
Sl = S[:, idx].T
Sdb = 10 * np.log10(Sl + 1e-12)
vmax = np.percentile(Sdb, 99.5)
fig, ax = plt.subplots(figsize=(a.width, a.height))
ax.pcolormesh(times, logf, Sdb, shading='auto', cmap='Greys', vmin=vmax - 70, vmax=vmax)
ax.set_yscale('log')
ax.set_ylim(a.fmin, a.fmax)
ticks = [55, 82.4, 110, 164.8, 220, 329.6, 440, 659.3, 880, 1318.5]
names = ['A1', 'E2', 'A2', 'E3', 'A3', 'E4', 'A4', 'E5', 'A5', 'E6']
ax.set_yticks(ticks); ax.set_yticklabels(['%s %g' % (n_, t) for n_, t in zip(names, ticks)])
ax.set_xlim(a.start, a.end)
for tr in a.tracks:
    parts = tr.split(':')
    label, path, color = parts[0], parts[1], parts[2]
    ms = float(parts[3]) if len(parts) > 3 else 2.0
    d = np.loadtxt(path, delimiter=',', ndmin=2)
    if d.size == 0:
        continue
    t, f = d[:, 0], d[:, 1]
    m = (t >= a.start) & (t <= a.end) & (f > 0)
    ax.plot(t[m], f[m], '.', color=color, ms=ms, label=label)
if a.lyrics:
    import csv
    for row in csv.reader(open(a.lyrics)):
        t0, dur = float(row[0]), float(row[1])
        if t0 + dur < a.start or t0 > a.end:
            continue
        ax.axvspan(t0, t0 + dur, ymin=0, ymax=0.03, color='tab:green', alpha=0.3)
        ax.text(t0 + dur / 2, a.fmin * 1.03, row[3], ha='center', va='bottom', fontsize=8)
if a.tracks:
    ax.legend(loc='upper right', markerscale=4)
ax.set_title(a.title)
ax.set_xlabel('time (s)')
fig.tight_layout()
fig.savefig(a.out, dpi=70)
print('wrote', a.out)
