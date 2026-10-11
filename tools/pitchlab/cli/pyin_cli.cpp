/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

// Runs the pYIN plugin class on an audio file the way Tony's
// Analyser does (the transformer's mean of the channels, block 2048,
// step 256, Tony's parameters) and writes its outputs as CSV, so
// that variants of the pYIN sources can be compared offline.
//
//   pyin_cli in.wav outprefix [--start s] [--end s] [--param name=value]...
//            [--block n] [--step n] [--candidates] [--no-normalise]
//
// The audio is scaled so that its peak over all channels and the whole
// file is 1, as Tony reads every file (its "normalise audio"
// preference), unless --no-normalise is given: pYIN's low-amplitude
// suppression depends on the level.
//
// Writes outprefix.pitch.csv (time,f0; f0 < 0 is an unvoiced
// estimate when outputunvoiced=2), outprefix.notes.csv
// (time,duration,f0) and, with --candidates,
// outprefix.cands.csv (time,f0,prob,f0,prob,...).

#include "PYinVamp.h"

#include <sndfile.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using std::string;
using std::vector;

static void usage()
{
    fprintf(stderr, "usage: pyin_cli in.wav outprefix [--start s] [--end s] "
            "[--param name=value]... [--block n] [--step n] [--candidates] "
            "[--no-normalise]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc < 3) usage();
    string inPath = argv[1];
    string prefix = argv[2];
    double startSec = 0, endSec = -1;
    int blockSize = 2048, stepSize = 256;
    bool candidates = false;
    bool normalise = true;

    // Tony's Analyser::buildAnalysisTransforms() with its default settings
    std::map<string, float> params {
        { "threshdistr", 2.f },
        { "fixedlag", 1.f },
        { "outputunvoiced", 0.f },
        { "precisetime", 0.f },
        { "lowampsuppression", 0.2f },
        { "onsetsensitivity", 0.7f },
        { "prunethresh", 0.1f },
    };

    for (int i = 3; i < argc; ++i) {
        string a = argv[i];
        if (a == "--start" && i + 1 < argc) startSec = atof(argv[++i]);
        else if (a == "--end" && i + 1 < argc) endSec = atof(argv[++i]);
        else if (a == "--block" && i + 1 < argc) blockSize = atoi(argv[++i]);
        else if (a == "--step" && i + 1 < argc) stepSize = atoi(argv[++i]);
        else if (a == "--candidates") candidates = true;
        else if (a == "--no-normalise") normalise = false;
        else if (a == "--param" && i + 1 < argc) {
            string p = argv[++i];
            auto eq = p.find('=');
            if (eq == string::npos) usage();
            params[p.substr(0, eq)] = float(atof(p.substr(eq + 1).c_str()));
        } else usage();
    }

    SF_INFO info;
    memset(&info, 0, sizeof(info));
    SNDFILE *sf = sf_open(inPath.c_str(), SFM_READ, &info);
    if (!sf) {
        fprintf(stderr, "cannot open %s: %s\n", inPath.c_str(), sf_strerror(nullptr));
        return 1;
    }
    int channels = info.channels;
    double sr = info.samplerate;
    sf_count_t total = info.frames;
    sf_count_t f0 = sf_count_t(startSec * sr);
    sf_count_t f1 = endSec > 0 ? std::min(total, sf_count_t(endSec * sr)) : total;
    if (f0 >= f1) { fprintf(stderr, "empty range\n"); return 1; }

    // svcore's reader scales by the peak of every channel over the
    // whole file, whatever part of it is analysed
    float gain = 1.f;
    vector<float> buf(size_t(4096) * channels);
    if (normalise) {
        float peak = 0.f;
        sf_seek(sf, 0, SEEK_SET);
        sf_count_t got;
        while ((got = sf_readf_float(sf, buf.data(), 4096)) > 0) {
            for (sf_count_t i = 0; i < got * channels; ++i) {
                peak = std::max(peak, std::fabs(buf[i]));
            }
        }
        if (peak > 0.f) gain = 1.f / peak;
    }

    // The mean of the channels, as svcore's transformer gives a
    // one-channel plugin when the channel is -1
    vector<float> mono(f1 - f0);
    {
        sf_seek(sf, f0, SEEK_SET);
        sf_count_t pos = 0;
        while (pos < f1 - f0) {
            sf_count_t want = std::min<sf_count_t>(4096, f1 - f0 - pos);
            sf_count_t got = sf_readf_float(sf, buf.data(), want);
            if (got <= 0) break;
            for (sf_count_t i = 0; i < got; ++i) {
                float s = 0;
                for (int c = 0; c < channels; ++c) s += buf[i * channels + c];
                mono[pos + i] = gain * s / float(channels);
            }
            pos += got;
        }
    }
    sf_close(sf);

    PYinVamp plugin{float(sr)};
    for (auto &p : params) plugin.setParameter(p.first, p.second);
    if (!plugin.initialise(1, stepSize, blockSize)) {
        fprintf(stderr, "initialise failed\n");
        return 1;
    }

    auto outputs = plugin.getOutputDescriptors();
    std::map<string, int> index;
    for (int i = 0; i < int(outputs.size()); ++i) index[outputs[i].identifier] = i;

    FILE *pitchOut = fopen((prefix + ".pitch.csv").c_str(), "w");
    FILE *notesOut = fopen((prefix + ".notes.csv").c_str(), "w");
    FILE *candOut = candidates ? fopen((prefix + ".cands.csv").c_str(), "w") : nullptr;
    if (!pitchOut || !notesOut || (candidates && !candOut)) {
        fprintf(stderr, "cannot write outputs\n");
        return 1;
    }

    double offsetSec = double(f0) / sr;
    auto write = [&](Vamp::Plugin::FeatureSet &fs) {
        for (auto &f : fs[index["smoothedpitchtrack"]]) {
            if (f.values.empty()) continue;
            double t = f.timestamp.sec + f.timestamp.nsec * 1e-9 + offsetSec;
            fprintf(pitchOut, "%.6f,%.6f\n", t, f.values[0]);
        }
        for (auto &f : fs[index["notes"]]) {
            if (f.values.empty()) continue;
            double t = f.timestamp.sec + f.timestamp.nsec * 1e-9 + offsetSec;
            double d = f.duration.sec + f.duration.nsec * 1e-9;
            fprintf(notesOut, "%.6f,%.6f,%.6f\n", t, d, f.values[0]);
        }
        if (candOut) {
            auto &cs = fs[index["f0candidates"]];
            auto &ps = fs[index["f0probs"]];
            for (size_t k = 0; k < cs.size() && k < ps.size(); ++k) {
                double t = cs[k].timestamp.sec + cs[k].timestamp.nsec * 1e-9 + offsetSec;
                fprintf(candOut, "%.6f", t);
                for (size_t j = 0; j < cs[k].values.size() && j < ps[k].values.size(); ++j) {
                    fprintf(candOut, ",%.4f,%.6f", cs[k].values[j], ps[k].values[j]);
                }
                fprintf(candOut, "\n");
            }
        }
    };

    // As svcore's transformer feeds a time-domain plugin: blocks from
    // the first frame, the last ones padded with zeros
    vector<float> block(blockSize);
    sf_count_t n = sf_count_t(mono.size());
    for (sf_count_t start = 0; start < n; start += stepSize) {
        for (int i = 0; i < blockSize; ++i) {
            sf_count_t j = start + i;
            block[i] = j < n ? mono[j] : 0.f;
        }
        const float *bufs[1] = { block.data() };
        auto fs = plugin.process(bufs, Vamp::RealTime::frame2RealTime(start, int(lrint(sr))));
        write(fs);
    }
    auto rest = plugin.getRemainingFeatures();
    write(rest);

    fclose(pitchOut);
    fclose(notesOut);
    if (candOut) fclose(candOut);
    return 0;
}
