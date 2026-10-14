#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.

"""Writes the reference pitch track, notes and lyrics of a Tony session
(.ton, bzip2'd XML) as CSV with times in seconds.

Usage: ton_tracks.py session.ton OUTDIR

OUTDIR/session_pitch.csv  time,f0       (the main model's smoothed pitch track)
OUTDIR/session_notes.csv  time,duration,f0
OUTDIR/lyrics.csv         time,duration,line,"word"
"""

import bz2
import os
import sys
import xml.etree.ElementTree as ET


def main(path, outdir):
    raw = open(path, 'rb').read()
    if raw[:3] == b'BZh':
        raw = bz2.decompress(raw)
    root = ET.fromstring(raw)
    data = root.find('data')
    models = {m.get('id'): m.attrib for m in data.findall('model')}
    datasets = {d.get('id'): d for d in data.findall('dataset')}
    main_id = next((i for i, m in models.items() if m.get('mainModel') == 'true'), None)
    derived = {}
    for d in data.findall('derivation'):
        if d.get('source') == main_id:
            derived[d.get('transform', '')] = d.get('model')
    os.makedirs(outdir, exist_ok=True)

    def points(model_id):
        m = models[model_id]
        sr = float(m['sampleRate'])
        return sr, datasets[m['dataset']].findall('point')

    pitch = derived.get('vamp:pyin:pyin:smoothedpitchtrack')
    if pitch:
        sr, pts = points(pitch)
        with open(os.path.join(outdir, 'session_pitch.csv'), 'w') as f:
            for p in pts:
                f.write('%.6f,%s\n' % (int(p.get('frame')) / sr, p.get('value')))
    notes = derived.get('vamp:pyin:pyin:notes')
    if notes:
        sr, pts = points(notes)
        with open(os.path.join(outdir, 'session_notes.csv'), 'w') as f:
            for p in pts:
                f.write('%.6f,%.6f,%s\n' % (int(p.get('frame')) / sr, int(p.get('duration')) / sr, p.get('value')))
    lyr = next((i for i, m in models.items() if m.get('name') == 'Lyrics'), None)
    if lyr:
        sr, pts = points(lyr)
        with open(os.path.join(outdir, 'lyrics.csv'), 'w') as f:
            for p in pts:
                f.write('%.6f,%.6f,%s,"%s"\n' % (int(p.get('frame')) / sr, int(p.get('duration')) / sr,
                                                 p.get('value'), p.get('label').replace('"', "'")))
    print('wrote', ', '.join(sorted(os.listdir(outdir))))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
