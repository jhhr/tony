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

#include "LatencyCheck.h"

#include "bqfft/FFT.h"

#include <algorithm>
#include <cmath>

using namespace sv;
using std::vector;

namespace LatencyCheck {
namespace {

const double pi = 3.14159265358979323846;

// Sweep to sweep, in tenths of a second so that the times are the same
// at every rate: each value from 1.6 to 2.6 s once, in an irregular order
const int calibrationSpacings[] = { 21, 16, 25, 19, 23, 17, 26, 20, 18, 24, 22 };
const int calibrationSpacingCount =
    int(sizeof(calibrationSpacings) / sizeof(calibrationSpacings[0]));

// On from the last calibration event to the held tones and between
// them.  None of these can be a calibration spacing, which are all
// taken, and after a held tone the next event has to wait for its end
const int heldSpacings[] = { 28, 40, 43 };

const int firstSweepTenths = 10;

const double calibrationSeconds = 26.0;
const double devSeconds = 40.0;
const double longSeconds = 240.0;

// The silence after the last event, as the calibration layout has it
const double tailSeconds = 0.8;

const double pitches[] = { 220.5, 294.0, 196.0, 245.0 };
const int pitchCount = int(sizeof(pitches) / sizeof(pitches[0]));

sv_frame_t
framesAt(double seconds, sv_samplerate_t rate)
{
    return sv_frame_t(std::llround(seconds * rate));
}

double
ratioOf(double db)
{
    return std::pow(10.0, db / 20.0);
}

// A ratio in dB that stays finite: silence against silence is 0 dB,
// and anything against silence is +/-200
double
decibels(double value, double against)
{
    const double limit = 200.0;
    if (value <= 0.0 && against <= 0.0) return 0.0;
    if (value <= 0.0) return -limit;
    if (against <= 0.0) return limit;
    double db = 20.0 * std::log10(value / against);
    return std::max(-limit, std::min(limit, db));
}

// A function of time rather than of frames, like everything else the
// reference is made of, so that it is the same reference at any rate
double
fade(double t, double duration)
{
    if (t < kFadeSeconds) {
        return 0.5 * (1.0 - std::cos(pi * t / kFadeSeconds));
    }
    if (t > duration - kFadeSeconds) {
        return 0.5 * (1.0 - std::cos(pi * (duration - t) / kFadeSeconds));
    }
    return 1.0;
}

void
addEvent(Layout &layout, int sweepTenths, double toneSeconds)
{
    double at = sweepTenths / 10.0;
    Event e;
    e.sweepStart = framesAt(at, layout.rate);
    e.toneStart = framesAt(at + kSweepSeconds + kPauseSeconds, layout.rate);
    e.toneLength = framesAt(toneSeconds, layout.rate);
    e.toneHz = pitches[layout.events.size() % pitchCount];
    layout.events.push_back(e);
}

} // namespace
} // namespace LatencyCheck

LatencyCheck::Layout
LatencyCheck::calibrationLayout(sv_samplerate_t rate)
{
    Layout layout;
    layout.rate = rate;

    int at = firstSweepTenths;
    addEvent(layout, at, kToneSeconds);
    for (int spacing : calibrationSpacings) {
        at += spacing;
        addEvent(layout, at, kToneSeconds);
    }

    layout.length = framesAt(calibrationSeconds, rate);
    return layout;
}

LatencyCheck::Layout
LatencyCheck::devLayout(sv_samplerate_t rate)
{
    Layout layout = calibrationLayout(rate);

    int at = firstSweepTenths;
    for (int spacing : calibrationSpacings) at += spacing;
    for (int spacing : heldSpacings) {
        at += spacing;
        addEvent(layout, at, kHeldToneSeconds);
    }

    layout.length = framesAt(devSeconds, rate);
    return layout;
}

LatencyCheck::Layout
LatencyCheck::longLayout(sv_samplerate_t rate)
{
    Layout layout;
    layout.rate = rate;

    const double eventSeconds = kSweepSeconds + kPauseSeconds + kToneSeconds;

    int at = firstSweepTenths;
    for (int i = 0; at / 10.0 + eventSeconds + tailSeconds <= longSeconds;
         ++i) {
        addEvent(layout, at, kToneSeconds);
        at += calibrationSpacings[i % calibrationSpacingCount];
    }

    layout.length = framesAt(longSeconds, rate);
    return layout;
}

vector<float>
LatencyCheck::sweep(sv_samplerate_t rate)
{
    const sv_frame_t length = framesAt(kSweepSeconds, rate);
    const double amplitude = ratioOf(kPeakDbfs);
    const double rise = (kSweepEndHz - kSweepStartHz) / kSweepSeconds;

    vector<float> out(length);
    for (sv_frame_t i = 0; i < length; ++i) {
        double t = double(i) / rate;
        double phase = 2.0 * pi * (kSweepStartHz * t + 0.5 * rise * t * t);
        out[i] = float(amplitude * fade(t, kSweepSeconds) * std::sin(phase));
    }
    return out;
}

vector<float>
LatencyCheck::generate(const Layout &layout)
{
    vector<float> out(std::max(layout.length, sv_frame_t(0)), 0.f);

    const vector<float> s = sweep(layout.rate);
    const double amplitude = ratioOf(kPeakDbfs);

    for (const Event &e : layout.events) {

        for (sv_frame_t i = 0; i < sv_frame_t(s.size()); ++i) {
            sv_frame_t f = e.sweepStart + i;
            if (f >= 0 && f < layout.length) out[f] += s[i];
        }

        const double duration = double(e.toneLength) / layout.rate;
        for (sv_frame_t i = 0; i < e.toneLength; ++i) {
            sv_frame_t f = e.toneStart + i;
            if (f < 0 || f >= layout.length) continue;
            double t = double(i) / layout.rate;
            out[f] += float(amplitude * fade(t, duration) *
                            std::sin(2.0 * pi * e.toneHz * t));
        }
    }

    return out;
}

LatencyCheck::Arrival
LatencyCheck::findSweep(const float *take, sv_frame_t count,
                        sv_samplerate_t rate, double expectedSeconds)
{
    Arrival arrival;
    if (!take || count <= 0 || rate <= 0) return arrival;

    // In frames of the take, at its own rate: the reference's frames
    // would put the window in the wrong place in a take at any other
    const sv_frame_t expected = framesAt(expectedSeconds, rate);
    const sv_frame_t reach = framesAt(kSearchSeconds, rate);
    const sv_frame_t first = std::max(sv_frame_t(0), expected - reach);
    const sv_frame_t last = std::min(count - 1, expected + reach);
    if (first > last) return arrival;

    // The sweep at the take's rate too: at the reference's, it would be
    // the wrong length, and would match a stretched sweep, not this one
    const vector<float> reference = sweep(rate);
    const sv_frame_t sweepLength = sv_frame_t(reference.size());

    // The correlation at a lag reads a sweep's length on from it, so the
    // stretch read runs that far past the last lag
    const sv_frame_t end = std::min(count, last + sweepLength);
    const int lags = int(last - first + 1);

    for (sv_frame_t i = first; i < end; ++i) {
        arrival.inputPeak = std::max(arrival.inputPeak,
                                     double(std::fabs(take[i])));
    }

    // Padded with zeros to a sweep's length beyond the stretch at least,
    // so that no lag looked at wraps round the end
    int size = 1;
    while (size < (end - first) + sweepLength) size *= 2;

    vector<double> x(size, 0.0), s(size, 0.0);
    for (sv_frame_t i = first; i < end; ++i) x[i - first] = take[i];
    for (sv_frame_t i = 0; i < sweepLength; ++i) s[i] = reference[i];

    breakfastquay::FFT fft(size);
    const int bins = size / 2 + 1;
    vector<double> xre(bins), xim(bins), sre(bins), sim(bins);
    fft.forward(x.data(), xre.data(), xim.data());
    fft.forward(s.data(), sre.data(), sim.data());

    // The take's spectrum times the conjugate of the sweep's is their
    // correlation: the matched filter.  That product is itself the
    // weighting to the sweep's band, since the sweep's spectrum is flat
    // from 1 to 8 kHz and falls some 100 dB below that at the tones'
    // pitches and at mains hum: a full-scale 100 Hz hum under the
    // sweep comes through 100 dB down.  A band mask on top of it
    // changed nothing that could be measured, so there is none.
    // The second spectrum is the first turned by -90 degrees, whose
    // inverse is the correlation's Hilbert transform: the two together
    // give its envelope (the magnitude of the analytic signal), which
    // peaks where the sweep begins whatever the polarity or the phase
    // of the carrier.  The correlation itself oscillates at the band's
    // centre, and its largest sample can be half a cycle off.
    vector<double> cre(bins), cim(bins), hre(bins), him(bins);
    for (int k = 0; k < bins; ++k) {
        cre[k] = xre[k] * sre[k] + xim[k] * sim[k];
        cim[k] = xim[k] * sre[k] - xre[k] * sim[k];
        hre[k] = cim[k];
        him[k] = -cre[k];
    }
    hre[0] = him[0] = hre[bins-1] = him[bins-1] = 0.0;

    vector<double> c(size), h(size);
    fft.inverse(cre.data(), cim.data(), c.data());
    fft.inverse(hre.data(), him.data(), h.data());

    // bqfft's inverse is unscaled.  Scaled, the envelope of a take
    // holding the reference's own sweep peaks at the sweep's energy
    vector<double> envelope(lags);
    for (int j = 0; j < lags; ++j) {
        envelope[j] = std::hypot(c[j], h[j]) / size;
    }

    const int largest = int(std::max_element(envelope.begin(), envelope.end())
                            - envelope.begin());

    // The earliest peak within kEarliestPeakDb of the largest: in a
    // room a reflection can be stronger than the direct sound, and the
    // direct sound's time is the one wanted.  A peak is a local maximum
    // of the envelope, and only one at least kSeparateArrivalSeconds
    // before the largest counts.  One arrival has no earlier peak that
    // high unless its spectrum has a hole in it: then the envelope
    // beats, a row of near-equal peaks with deep dips between them, so
    // a dip between two peaks does not tell arrivals apart, but the
    // time does, since those peaks lie within about one over the width
    // of what is left of the band.  A reflection closer than the
    // spacing is too close to matter: the larger peak is taken.
    int chosen = largest;
    const int apart = std::max(1, int(framesAt(kSeparateArrivalSeconds, rate)));
    const double within = envelope[largest] * ratioOf(-kEarliestPeakDb);
    for (int j = 1; j + apart <= largest; ++j) {
        if (envelope[j] >= within &&
            envelope[j] > envelope[j-1] && envelope[j] >= envelope[j+1]) {
            chosen = j;
            break;
        }
    }

    vector<double> ordered(envelope);
    std::nth_element(ordered.begin(), ordered.begin() + lags / 2,
                     ordered.end());
    const double median = ordered[lags / 2];

    // The chosen peak's own reflections are near it, and are not
    // another candidate for where the sweep is
    const int closeBy = int(framesAt(kSecondPeakSeconds, rate));
    double second = 0.0;
    for (int j = 0; j < lags; ++j) {
        if (std::abs(j - chosen) > closeBy) {
            second = std::max(second, envelope[j]);
        }
    }

    double energy = 0.0;
    for (float v : reference) energy += double(v) * double(v);

    const sv_frame_t at = first + chosen;
    arrival.errorFrames = at - expected;
    arrival.errorSeconds = double(at) / rate - expectedSeconds;
    arrival.peakOverMedianDb = decibels(envelope[chosen], median);
    arrival.peakOverSecondDb = decibels(envelope[chosen], second);
    arrival.levelDb = decibels(envelope[chosen], energy);
    arrival.found =
        arrival.peakOverMedianDb >= kMinPeakOverMedianDb &&
        arrival.peakOverSecondDb >= kMinPeakOverSecondDb;

    return arrival;
}
