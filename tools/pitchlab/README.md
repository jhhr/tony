# Pitch lab

An offline bench for trying changes to Tony's pitch analysis on difficult audio: AI-separated
vocal stems with echo, reverb, a second voice in harmony or a doubled lead, instrument
bleed and separation damage. It runs the pYIN plugin class exactly as Tony's `Analyser`
does (the mean of the channels, block 2048, step 256, Tony's parameters), on a test set
whose f0 is known, and on real songs, scored against their timed lyrics and hand-checked
annotations.

Nothing here is built into Tony. Variants of pYIN are made by copying the sources of the
`pyin/` checkout (upstream `c4dm/pyin` v1.2, as `repoint-lock.json` pins it) into a
directory of their own, editing the copy and building a runner from it.

## Pieces

| Path | What |
| --- | --- |
| `cli/pyin_cli.cpp`, `cli/build.sh` | `pyin_cli in.wav outprefix [--param name=value]... [--candidates]`: writes `outprefix.pitch.csv` (time,f0), `.notes.csv`, with `--candidates` `.cands.csv`, and with `--param voicetracks=1` each voice's track, `.strongestvoice.csv`, `.lowervoice.csv` and `.highervoice.csv`. `build.sh <pyin source dir> <binary>` builds one per variant against `vamp-plugin-sdk/` |
| `py/make_testset.py` | Builds the test set: 4 sources (two real clean recordings, f0 from clean pYIN; two synthetic voices, exact f0) x 12 conditions, stereo, at a stem's level, with `time,lead f0,second voice f0` ground truth |
| `py/run_testset.py` | Runs any tracker over the test set in parallel: `run_testset.py TESTSET OUTDIR -j 2 -- cmd {in} {out} ...` |
| `py/evaluate.py` | Scores a directory of `<condition>__<source>.pitch.csv` per condition: RPA, RPA_any (either voice), RCA, VR, VFA, OA, RPA_fv, sub (more than an octave below every voice), breaks/s, switches/s; dev split (bob, synm), test split (hb, synf) |
| `py/song_eval.py` | Scores a real song's track: voicing inside and outside the lyrics' words, and, with an annotation of the voices, accuracy against any/lowest/highest voice; plots of chosen sections |
| `py/plot_section.py` | Spectrogram of a section with tracks and lyrics on it |
| `py/compare_section.py` | One panel per track over the same spectrogram, with an annotation of the voices on each: methods side by side |
| `py/ton_tracks.py` | A `.ton` session's reference pitch, notes and lyrics as CSV |
| `py/make_voicesets.py` | Builds the voice-role set: synthetic harmonies whose voices sing lines of their own (thirds, sixths, fifths, octaves, a trio whose top voice comes and goes) at levels that change, so that the louder voice changes from phrase to phrase, as in a song; a column of f0 per voice and each voice's own level. Dev split synm, test split synf |
| `py/voice_follow.py` | Which voice a track follows where two or more sing: the share on the lowest, a middle and the highest voice, and changes of voice per minute (a voice entering or leaving is no change), for a song's annotation or a set's two-voice files |

## Conditions

`clean`; `echo` (ping-pong delay of a dotted eighth at 143 bpm, feedback 0.45, each repeat
darker); `reverb` (RT60 1.8 s); `studio` (both); `harmA` (a second voice a major third
above, -2 dB, panned), `harmB` (a minor third below, equal level), `harmC` (a major sixth
above, -3 dB), each with reverb; `double` (the lead sung twice, 20-40 ms apart and a few
cents adrift); `bleed` (a quiet picked guitar and bass); `sepdmg` (a fluttering
time-frequency mask, holes, a thin low end); `moises` (damage, bleed, echo and reverb);
`all` (harmA with everything).

## Setup

```sh
python3 -m venv venv && venv/bin/pip install numpy scipy matplotlib soundfile librosa
tools/pitchlab/cli/build.sh pyin bin/pyin_baseline
venv/bin/python tools/pitchlab/py/make_testset.py testset --pyin bin/pyin_baseline
venv/bin/python tools/pitchlab/py/run_testset.py testset out/base -j 2 -- bin/pyin_baseline {in} {out}
venv/bin/python tools/pitchlab/py/evaluate.py testset out/base
```

The pyin fork's parameters for vocals with harmonies, doubled parts and echoes
([docs/forks.md](../../docs/forks.md#pyin)) are off by default, so a runner built from
`pyin/` is pYIN 1.2 as it stands. Tony's analysis of the reference is that runner with
`--param salmode=1 --param candidaterule=1 --param commonf0=0.8 --param levelfloor=13
--param voicedfloor=0.2 --param voicehold=3`, and with Analysis > Voice to Follow at
Lower Voice `--param voicefollow=1` as well (2 the higher voice, 3 the middle, which
Tony offers per selection or not at all), and `--param voicetracks=1` for the voice
tracks a selection's candidates are cut from. `voice_follow.py` scores which voice a track
keeps, on the voice-role set or a song's annotation of its voices:

```sh
tools/pitchlab/cli/build.sh pyin bin/pyin_fork
venv/bin/python tools/pitchlab/py/make_voicesets.py voicesets
venv/bin/python tools/pitchlab/py/run_testset.py voicesets out/lower -j 2 -- bin/pyin_fork {in} {out} \
    --param salmode=1 --param candidaterule=1 --param commonf0=0.8 --param levelfloor=13 \
    --param voicedfloor=0.2 --param voicehold=3 --param voicefollow=1
venv/bin/python tools/pitchlab/py/voice_follow.py set voicesets out/lower --split dev
```

The ground truth of the real sources is pYIN's own track on the clean audio, so the
`clean` condition measures how a variant departs from baseline pYIN rather than from the
truth; the synthetic sources' f0 is exact.
