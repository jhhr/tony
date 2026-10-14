#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.

"""Runs a pitch tracker over every file of the test set, in parallel.

Usage: run_testset.py TESTSET OUTDIR -- command {in} {out} [args...]

{in} is replaced by the file's .wav, {out} by OUTDIR/<name> (the
tracker writes {out}.pitch.csv). -j sets the number of processes
(default 2), --files a glob over the names (condition__source).
"""

import argparse
import fnmatch
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor


def main():
    argv = sys.argv[1:]
    if '--' not in argv:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    i = argv.index('--')
    ap = argparse.ArgumentParser()
    ap.add_argument('testset')
    ap.add_argument('outdir')
    ap.add_argument('-j', type=int, default=2)
    ap.add_argument('--files', default='*')
    a = ap.parse_args(argv[:i])
    cmd = argv[i + 1:]
    os.makedirs(a.outdir, exist_ok=True)
    manifest = json.load(open(os.path.join(a.testset, 'manifest.json')))
    names = [b for b in manifest['files'] if fnmatch.fnmatch(b, a.files)]

    def run(name):
        c = [x.replace('{in}', os.path.join(a.testset, name + '.wav'))
              .replace('{out}', os.path.join(a.outdir, name)) for x in cmd]
        r = subprocess.run(c, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            return name, r.stderr[-2000:]
        return name, None

    failed = 0
    with ThreadPoolExecutor(max_workers=a.j) as ex:
        for name, err in ex.map(run, names):
            if err is not None:
                failed += 1
                print('FAILED %s:\n%s' % (name, err), file=sys.stderr)
    print('%d files, %d failed' % (len(names), failed))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
