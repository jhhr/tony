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

/**
 * x through a second-order Butterworth high-pass at hz (the Audio EQ
 * Cookbook's): 12 dB an octave below it, as a small speaker rolls off
 */
inline std::vector<float>
highPassed(const std::vector<float> &x, double sampleRate, double hz)
{
    const double w = 2.0 * kPi * hz / sampleRate;
    const double alpha = std::sin(w) / std::sqrt(2.0);
    const double cosw = std::cos(w);
    const double b0 = (1.0 + cosw) / 2.0, b1 = -(1.0 + cosw), b2 = b0;
    const double a0 = 1.0 + alpha, a1 = -2.0 * cosw, a2 = 1.0 - alpha;
    std::vector<float> y(x.size());
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        double in = x[i];
        double out = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        y[i] = float(out);
    }
    return y;
}

/**
 * x with hz taken out: a notch (the Audio EQ Cookbook's), which leaves
 * nothing of a steady tone at hz once it has settled, some q periods
 * in, and little of anything an octave away.  As a small speaker loses
 * a voice's fundamental, and more cleanly.
 */
inline std::vector<float>
notched(const std::vector<float> &x, double sampleRate, double hz,
        double q = 4.0)
{
    const double w = 2.0 * kPi * hz / sampleRate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double cosw = std::cos(w);
    const double b0 = 1.0, b1 = -2.0 * cosw, b2 = 1.0;
    const double a0 = 1.0 + alpha, a1 = -2.0 * cosw, a2 = 1.0 - alpha;
    std::vector<float> y(x.size());
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        double in = x[i];
        double out = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        y[i] = float(out);
    }
    return y;
}

/**
 * x with a tone at hz, and anything within a few cents of it, taken out
 * as a small speaker loses a voice's fundamental: two notches of q 2,
 * which leave a steady tone within 10 cents of hz 60 dB down or more
 * once settled, and take 1 dB or so off the octave above.
 */
inline std::vector<float>
withoutFundamental(const std::vector<float> &x, double sampleRate, double hz)
{
    return notched(notched(x, sampleRate, hz, 2.0), sampleRate, hz, 2.0);
}

}

#endif
