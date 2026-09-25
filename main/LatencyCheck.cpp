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
    int secondAt = chosen;
    for (int j = 0; j < lags; ++j) {
        if (std::abs(j - chosen) > closeBy && envelope[j] > second) {
            second = envelope[j];
            secondAt = j;
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
    arrival.secondDelaySeconds = double(secondAt - chosen) / rate;
    arrival.secondLevelDb = decibels(second, envelope[chosen]);
    arrival.found =
        arrival.peakOverMedianDb >= kMinPeakOverMedianDb &&
        arrival.peakOverSecondDb >= kMinPeakOverSecondDb;

    return arrival;
}

namespace LatencyCheck {
namespace {

double
median(vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2) ? v[n/2] : 0.5 * (v[n/2 - 1] + v[n/2]);
}

double
spreadOf(const vector<double> &v)
{
    if (v.empty()) return 0.0;
    auto range = std::minmax_element(v.begin(), v.end());
    return *range.second - *range.first;
}

// The echo, if more than half of the heard sweeps have a second peak
// after them at one delay.  When those that agree are more than half
// of the candidates, the median of the candidates' delays is among
// theirs, so looking around it finds them
Echo
echoOf(const vector<EventResult> &events)
{
    Echo echo;

    int heard = 0;
    vector<const Arrival *> candidates;
    for (const EventResult &e : events) {
        const Arrival &a = e.arrival;
        if (a.peakOverMedianDb < kMinPeakOverMedianDb) continue;
        ++heard;
        if (a.secondDelaySeconds >= kEchoMinDelaySeconds &&
            a.secondLevelDb >= -kEchoMaxBelowDb) {
            candidates.push_back(&a);
        }
    }
    if (candidates.empty()) return echo;

    vector<double> delays;
    for (const Arrival *a : candidates) delays.push_back(a->secondDelaySeconds);
    const double centre = median(delays);

    vector<double> agreeing, levels;
    for (const Arrival *a : candidates) {
        if (std::fabs(a->secondDelaySeconds - centre) <= kEchoToleranceSeconds) {
            agreeing.push_back(a->secondDelaySeconds);
            levels.push_back(a->secondLevelDb);
        }
    }

    const int count = int(agreeing.size());
    if (count >= kMinEchoEvents && 2 * count > heard) {
        echo.heard = true;
        echo.delaySeconds = median(agreeing);
        echo.levelDb = median(levels);
        echo.events = count;
    }
    return echo;
}

} // namespace
} // namespace LatencyCheck

const char *
LatencyCheck::verdictName(Verdict verdict)
{
    switch (verdict) {
    case Verdict::NoSignal: return "NoSignal";
    case Verdict::Clipped: return "Clipped";
    case Verdict::Fading: return "Fading";
    case Verdict::PositionDependent: return "PositionDependent";
    case Verdict::Scattered: return "Scattered";
    case Verdict::Unsteady: return "Unsteady";
    case Verdict::Ok: return "Ok";
    }
    return "Ok";
}

bool
LatencyCheck::TakeSummary::flagged(Verdict v) const
{
    return std::find(flags.begin(), flags.end(), v) != flags.end();
}

LatencyCheck::TakeSummary
LatencyCheck::judgeTake(const Layout &layout,
                        const float *take, sv_frame_t count,
                        sv_samplerate_t rate,
                        const vector<PunchIn> &punchIns)
{
    TakeSummary summary;

    const double takeSeconds =
        (take && count > 0 && rate > 0) ? double(count) / rate : 0.0;

    // All that the finder reads for an event, around its expected time
    const double before = kSearchSeconds + kJudgeMarginSeconds;
    const double after = kSearchSeconds + kSweepSeconds + kJudgeMarginSeconds;

    for (int p = 0; p < int(punchIns.size()); ++p) {

        PunchInResult result;
        result.range = punchIns[p];
        const double start = punchIns[p].start;
        const double end = std::min(punchIns[p].end, takeSeconds);

        const sv_frame_t from = std::max(sv_frame_t(0), framesAt(start, rate));
        const sv_frame_t to = std::min(count, framesAt(end, rate));
        for (sv_frame_t f = from; f < to; ++f) {
            summary.inputPeak = std::max(summary.inputPeak,
                                         double(std::fabs(take[f])));
        }

        vector<double> offsets;
        for (int i = 0; i < int(layout.events.size()); ++i) {

            const double expected =
                double(layout.events[i].sweepStart) / layout.rate;
            const double first = expected - before;
            const double last = expected + after;
            if (first < start || last > end) continue;

            // A later punch-in over any of it replaced what this one
            // recorded there
            bool replaced = false;
            for (int q = p + 1; q < int(punchIns.size()); ++q) {
                if (first < punchIns[q].end && last > punchIns[q].start) {
                    replaced = true;
                }
            }
            if (replaced) continue;

            EventResult e;
            e.event = i;
            e.punchIn = p;
            e.expectedSeconds = expected;
            e.arrival = findSweep(take, count, rate, expected);

            ++result.judged;
            if (e.arrival.found) {
                ++result.found;
                offsets.push_back(e.arrival.errorSeconds);
            }
            summary.events.push_back(e);
        }

        result.medianOffset = median(offsets);
        result.spread = spreadOf(offsets);
        summary.judged += result.judged;
        summary.found += result.found;
        summary.punchIns.push_back(result);
    }

    // Across punch-ins: each one that found anything counts once, at
    // its median, since what moves from one to the next is the stream
    // start, which every punch-in makes once
    vector<double> positions, medians;
    double within = 0.0;
    for (const PunchInResult &r : summary.punchIns) {
        if (r.found == 0) continue;
        positions.push_back(r.range.start);
        medians.push_back(r.medianOffset);
        within = std::max(within, r.spread);
    }
    summary.medianOffset = median(medians);
    summary.spread = spreadOf(medians);

    // Least squares.  A device at another rate, placed frame for frame,
    // misplaces a punch-in in proportion to where it starts
    summary.slopeResidual = summary.spread;
    if (positions.size() >= 2) {
        double mx = 0.0, my = 0.0;
        for (size_t k = 0; k < positions.size(); ++k) {
            mx += positions[k];
            my += medians[k];
        }
        mx /= double(positions.size());
        my /= double(positions.size());
        double sxx = 0.0, sxy = 0.0;
        for (size_t k = 0; k < positions.size(); ++k) {
            sxx += (positions[k] - mx) * (positions[k] - mx);
            sxy += (positions[k] - mx) * (medians[k] - my);
        }
        if (sxx > 0.0) {
            summary.slope = sxy / sxx;
            vector<double> residuals;
            for (size_t k = 0; k < positions.size(); ++k) {
                residuals.push_back(medians[k] -
                                    (my + summary.slope * (positions[k] - mx)));
            }
            summary.slopeResidual = spreadOf(residuals);
        }
    }

    // The level rather than a confidence: over the median of a window
    // of near silence a confidence is anything up to the 200 dB clamp,
    // so its trend in a clean take is noise, while the level is what
    // an echo canceller, noise suppressor or gain control changes.
    // Events not found count too, at the level of whatever the finder
    // took instead, which is lower when the sweep has gone
    const int n = int(summary.events.size());
    if (n >= kMinFadingEvents) {
        vector<double> early, late;
        for (int k = 0; k < n / 2; ++k) {
            early.push_back(summary.events[k].arrival.levelDb);
        }
        for (int k = n - n / 2; k < n; ++k) {
            late.push_back(summary.events[k].arrival.levelDb);
        }
        summary.fadingDb = median(early) - median(late);
    }

    summary.echo = echoOf(summary.events);

    const double spread = std::max(summary.spread, within);

    if (summary.found == 0 ||
        double(summary.found) / double(summary.judged) < kMinFoundShare) {
        summary.flags.push_back(Verdict::NoSignal);
    }
    if (summary.inputPeak >= ratioOf(kClippedDbfs)) {
        summary.flags.push_back(Verdict::Clipped);
    }
    if (summary.fadingDb >= kFadingDb) {
        summary.flags.push_back(Verdict::Fading);
    }
    if (int(positions.size()) >= kMinPunchInsForSlope &&
        std::fabs(summary.slope) > kPositionSlope &&
        std::max(summary.slopeResidual, within) <= kSteadySeconds) {
        summary.flags.push_back(Verdict::PositionDependent);
    }
    if (spread > kScatteredSeconds) {
        summary.flags.push_back(Verdict::Scattered);
    } else if (spread > kSteadySeconds) {
        summary.flags.push_back(Verdict::Unsteady);
    }

    summary.verdict = summary.flags.empty() ? Verdict::Ok : summary.flags.front();
    return summary;
}

double
LatencyCheck::calibratedRoundTrip(double usedRoundTripSeconds,
                                  double medianOffsetSeconds)
{
    return usedRoundTripSeconds + medianOffsetSeconds;
}
