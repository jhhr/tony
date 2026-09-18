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

#ifndef TEST_SIGNALS_H
#define TEST_SIGNALS_H

// Synthetic signals and pitch comparison shared by the Tony test suites.

#include <cmath>
#include <random>
#include <vector>

namespace TestSignals {

const double kPi = 3.14159265358979323846;

inline std::vector<float>
sine(double hz, double sampleRate, int n, double amplitude = 0.5,
     double dc = 0.0, int startIndex = 0)
{
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i) {
        v[i] = float(dc + amplitude *
                     std::sin(2.0 * kPi * hz * (startIndex + i) / sampleRate));
    }
    return v;
}

/** Naive (aliasing) sawtooth: all harmonics present, fundamental at hz. */
inline std::vector<float>
sawtooth(double hz, double sampleRate, int n, double amplitude = 0.5)
{
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i) {
        double phase = std::fmod(hz * i / sampleRate, 1.0);
        v[i] = float(amplitude * (2.0 * phase - 1.0));
    }
    return v;
}

/** Deterministic: the same seed always yields the same buffer. */
inline std::vector<float>
whiteNoise(int n, unsigned seed, double amplitude = 0.5)
{
    std::mt19937 gen(seed);
    std::uniform_real_distribution<float> dist(float(-amplitude),
                                               float(amplitude));
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i) v[i] = dist(gen);
    return v;
}

inline double
centsBetween(double hz, double referenceHz)
{
    return 1200.0 * std::log2(hz / referenceHz);
}

}

#endif
