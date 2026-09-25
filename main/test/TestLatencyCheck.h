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

#ifndef TEST_LATENCY_CHECK_H
#define TEST_LATENCY_CHECK_H

// Tier 2: the test reference that Calibrate Audio plays, and the finder
// that locates its sweeps in a recording. The takes are made here from
// the reference itself: shifted, filtered, with noise or a reflection
// added. No window, no device and no files.

#include "../LatencyCheck.h"

#include "TestSignals.h"

#include <QObject>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

class TestLatencyCheck : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;
    typedef std::vector<float> samples_t;

    static constexpr double kRate = 44100.0;

    static frame_t framesOf(double seconds, double rate = kRate) {
        return frame_t(std::llround(seconds * rate));
    }

    // The first n events of the calibration layout, and the reference up
    // to where the next would begin: as much as a finder test needs
    static LatencyCheck::Layout firstEvents(int n, double rate = kRate) {
        LatencyCheck::Layout layout = LatencyCheck::calibrationLayout(rate);
        layout.length = layout.events[n].sweepStart;
        layout.events.resize(n);
        return layout;
    }

    static double expectedAt(const LatencyCheck::Layout &layout, int i) {
        return double(layout.events[i].sweepStart) / layout.rate;
    }

    // The take as it would be if all of the reference arrived this many
    // frames late (early, if negative)
    static samples_t shifted(const samples_t &x, frame_t by) {
        const frame_t n = frame_t(x.size());
        samples_t y(x.size(), 0.f);
        for (frame_t i = std::max(frame_t(0), by); i < n && i - by < n; ++i) {
            y[i] = x[i - by];
        }
        return y;
    }

    static samples_t mixed(const samples_t &x, const samples_t &y,
                           double gain) {
        samples_t out(x);
        for (size_t i = 0; i < out.size() && i < y.size(); ++i) {
            out[i] += float(gain * y[i]);
        }
        return out;
    }

    static double ratioOf(double db) { return std::pow(10.0, db / 20.0); }

    static double rms(const samples_t &x) {
        double sum = 0.0;
        for (float v : x) sum += double(v) * v;
        return x.empty() ? 0.0 : std::sqrt(sum / double(x.size()));
    }

    static double peakOf(const samples_t &x, frame_t from, frame_t to) {
        double peak = 0.0;
        for (frame_t i = from; i < to; ++i) {
            peak = std::max(peak, double(std::fabs(x[i])));
        }
        return peak;
    }

    // Sign changes: twice the cycles of a sine, whatever its level
    static int crossings(const samples_t &x, frame_t from, frame_t to) {
        int n = 0;
        for (frame_t i = from + 1; i < to; ++i) {
            if ((x[i-1] < 0.f) != (x[i] < 0.f)) ++n;
        }
        return n;
    }

    // A second-order Butterworth section (the Audio EQ Cookbook's), as a
    // small speaker's roll-off
    static samples_t filtered(const samples_t &x, double rate, double hz,
                              bool highPass) {
        const double w = 2.0 * TestSignals::kPi * hz / rate;
        const double alpha = std::sin(w) / std::sqrt(2.0);
        const double cosw = std::cos(w);
        const double b0 = highPass ? (1.0 + cosw) / 2.0 : (1.0 - cosw) / 2.0;
        const double b1 = highPass ? -(1.0 + cosw) : (1.0 - cosw);
        const double b2 = b0;
        const double a0 = 1.0 + alpha, a1 = -2.0 * cosw, a2 = 1.0 - alpha;
        samples_t y(x.size());
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

    static LatencyCheck::Arrival find(const samples_t &take, double rate,
                                      double expectedSeconds) {
        return LatencyCheck::findSweep(take.data(), frame_t(take.size()),
                                       rate, expectedSeconds);
    }

    static QByteArray describe(const LatencyCheck::Arrival &a) {
        return QString("found %1, error %2 frames (%3 ms), %4 dB over the "
                       "median, %5 dB over the second, level %6 dB")
            .arg(a.found ? "yes" : "no")
            .arg(qint64(a.errorFrames))
            .arg(a.errorSeconds * 1000.0)
            .arg(a.peakOverMedianDb)
            .arg(a.peakOverSecondDb)
            .arg(a.levelDb)
            .toUtf8();
    }

private slots:
    // The same layout and the same samples every time: nothing in the
    // reference depends on a clock or a random source
    void generator_is_deterministic() {
        const LatencyCheck::Layout a = LatencyCheck::devLayout();
        const LatencyCheck::Layout b = LatencyCheck::devLayout();
        QCOMPARE(a.length, b.length);
        QCOMPARE(a.events.size(), b.events.size());
        for (size_t i = 0; i < a.events.size(); ++i) {
            QCOMPARE(a.events[i].sweepStart, b.events[i].sweepStart);
            QCOMPARE(a.events[i].toneStart, b.events[i].toneStart);
            QCOMPARE(a.events[i].toneLength, b.events[i].toneLength);
            QCOMPARE(a.events[i].toneHz, b.events[i].toneHz);
        }
        QVERIFY(LatencyCheck::generate(a) == LatencyCheck::generate(b));
    }

    // Each event is where the layout says and what it says: silence, a
    // sweep rising from 1 to 8 kHz, a pause, a tone at its pitch, silence
    void generator_puts_events_where_it_says() {
        const LatencyCheck::Layout layout = LatencyCheck::calibrationLayout();
        const samples_t x = LatencyCheck::generate(layout);
        const samples_t sweep = LatencyCheck::sweep(kRate);
        const frame_t sweepLength = frame_t(sweep.size());

        QCOMPARE(layout.rate, kRate);
        QCOMPARE(layout.length, framesOf(26.0));
        QCOMPARE(frame_t(x.size()), layout.length);
        QCOMPARE(int(layout.events.size()), 12);
        QCOMPARE(sweepLength, framesOf(0.2));

        std::set<double> pitches;
        frame_t silentFrom = 0;

        for (const LatencyCheck::Event &e : layout.events) {

            QCOMPARE(peakOf(x, silentFrom, e.sweepStart), 0.0);

            int misplaced = 0;
            for (frame_t i = 0; i < sweepLength; ++i) {
                if (x[e.sweepStart + i] != sweep[i]) ++misplaced;
            }
            QCOMPARE(misplaced, 0);

            // Rising: 1 to 4.5 kHz in the first 0.1 s is 550 sign
            // changes, 1 to 8 kHz in 0.2 s is 1800. Falling, the first
            // half would have 1250
            int half = crossings(x, e.sweepStart, e.sweepStart + sweepLength / 2);
            int whole = crossings(x, e.sweepStart, e.sweepStart + sweepLength);
            QVERIFY2(std::abs(half - 550) <= 3, qPrintable(QString::number(half)));
            QVERIFY2(std::abs(whole - 1800) <= 3, qPrintable(QString::number(whole)));

            QCOMPARE(e.toneStart - e.sweepStart, framesOf(0.3));
            QCOMPARE(peakOf(x, e.sweepStart + sweepLength, e.toneStart), 0.0);

            // A whole number of samples per period at 44.1 kHz, and
            // that pitch in the samples
            QCOMPARE(e.toneLength, framesOf(0.8));
            const double period = kRate / e.toneHz;
            QCOMPARE(period, std::round(period));
            const int tone = crossings(x, e.toneStart, e.toneStart + e.toneLength);
            const double cycles = e.toneHz * double(e.toneLength) / kRate;
            QVERIFY2(std::fabs(tone - 2.0 * cycles) <= 2.0,
                     qPrintable(QString("%1 at %2 Hz").arg(tone).arg(e.toneHz)));
            pitches.insert(e.toneHz);

            silentFrom = e.toneStart + e.toneLength;
        }

        QCOMPARE(peakOf(x, silentFrom, frame_t(x.size())), 0.0);
        QCOMPARE(pitches, (std::set<double> { 196.0, 220.5, 245.0, 294.0 }));
    }

    // Sweep to sweep: never less than twice the finder's reach, so that
    // a neighbour's sweep is never in its window; 1.6 to 2.6 s between
    // events with short tones; and all different, or in the long
    // layout, any eleven in a row
    void generator_spacings_are_irregular() {
        struct Case {
            LatencyCheck::Layout layout;
            double seconds;
            int held;
            int run;
        };
        const Case cases[] = {
            { LatencyCheck::calibrationLayout(), 26.0, 0, 0 },
            { LatencyCheck::devLayout(), 40.0, 3, 0 },
            { LatencyCheck::longLayout(), 240.0, 0, 11 },
        };

        for (const Case &c : cases) {
            const LatencyCheck::Layout &layout = c.layout;
            const int n = int(layout.events.size());
            QCOMPARE(layout.length, framesOf(c.seconds));

            std::vector<double> spacings;
            int held = 0;

            for (int i = 0; i < n; ++i) {
                const LatencyCheck::Event &e = layout.events[i];
                const frame_t end = e.toneStart + e.toneLength;
                QVERIFY(end <= layout.length);
                const bool isHeld = (e.toneLength == framesOf(3.0));
                if (isHeld) ++held;
                if (i + 1 == n) break;

                const LatencyCheck::Event &next = layout.events[i + 1];
                QVERIFY(end < next.sweepStart);
                const double spacing =
                    double(next.sweepStart - e.sweepStart) / layout.rate;
                QVERIFY2(spacing >= 2.0 * LatencyCheck::kSearchSeconds - 1e-9,
                         qPrintable(QString::number(spacing)));
                if (!isHeld && next.toneLength == framesOf(0.8)) {
                    QVERIFY2(spacing <= 2.6 + 1e-9,
                             qPrintable(QString::number(spacing)));
                }
                spacings.push_back(spacing);
            }

            QCOMPARE(held, c.held);

            const int count = int(spacings.size());
            const int run = c.run > 0 ? c.run : count;
            for (int i = 0; i < count; ++i) {
                for (int j = i + 1; j < count && j < i + run; ++j) {
                    QVERIFY2(std::fabs(spacings[i] - spacings[j]) >= 0.1 - 1e-9,
                             qPrintable(QString("%1 and %2").arg(i).arg(j)));
                }
            }
        }

        QVERIFY(LatencyCheck::longLayout().events.size() > 100);
    }

    // Every sweep and every tone peaks at -12 dBFS, and nothing is louder
    void generator_peaks_at_minus_12_dbfs() {
        const LatencyCheck::Layout layout = LatencyCheck::devLayout();
        const samples_t x = LatencyCheck::generate(layout);
        const double peak = ratioOf(LatencyCheck::kPeakDbfs);
        const frame_t sweepLength = framesOf(LatencyCheck::kSweepSeconds);

        QVERIFY(peakOf(x, 0, frame_t(x.size())) <= peak * (1.0 + 1e-6));
        for (const LatencyCheck::Event &e : layout.events) {
            QVERIFY(peakOf(x, e.sweepStart, e.sweepStart + sweepLength) >=
                    peak * ratioOf(-0.05));
            QVERIFY(peakOf(x, e.toneStart, e.toneStart + e.toneLength) >=
                    peak * ratioOf(-0.05));
        }
    }

    // The reference as generated: every event where it is, at the level
    // it was made at
    void finder_finds_every_event_of_the_reference() {
        const LatencyCheck::Layout layout = LatencyCheck::devLayout();
        const samples_t x = LatencyCheck::generate(layout);
        for (int i = 0; i < int(layout.events.size()); ++i) {
            LatencyCheck::Arrival a = find(x, kRate, expectedAt(layout, i));
            QVERIFY2(a.found, describe(a).constData());
            QCOMPARE(a.errorFrames, frame_t(0));
            QVERIFY2(std::fabs(a.errorSeconds) < 1e-9, describe(a).constData());
            QVERIFY2(std::fabs(a.levelDb) < 0.01, describe(a).constData());
        }
    }

    // Takes that land early and late, by up to nearly the finder's
    // reach. At 0.75 s, the neighbour across the smallest spacing (1.6
    // s, between the second and third events) is 0.85 s from where this
    // sweep is expected: just out of reach, which is what the spacing
    // is for
    void finder_finds_shifts_across_the_window() {
        const LatencyCheck::Layout layout = firstEvents(4);
        const samples_t reference = LatencyCheck::generate(layout);

        const frame_t shifts[] = {
            -framesOf(0.75), -framesOf(0.5), -8821, -1, 0, 1,
            framesOf(0.1), 13231, framesOf(0.75)
        };
        for (frame_t shift : shifts) {
            const samples_t take = shifted(reference, shift);
            for (int i = 0; i < int(layout.events.size()); ++i) {
                LatencyCheck::Arrival a = find(take, kRate, expectedAt(layout, i));
                QVERIFY2(a.found, describe(a).constData());
                QCOMPARE(a.errorFrames, shift);
                QVERIFY2(std::fabs(a.errorSeconds - shift / kRate) < 1e-9,
                         describe(a).constData());
            }
        }
    }

    // Half a frame late. The reference made at twice the rate, read at
    // every other sample, is the reference itself from an even sample
    // and the reference half a frame late from an odd one
    void finder_finds_a_fractional_shift() {
        const LatencyCheck::Layout layout = firstEvents(4);
        const samples_t reference = LatencyCheck::generate(layout);
        const samples_t doubled = LatencyCheck::generate(firstEvents(4, 2 * kRate));
        QCOMPARE(doubled.size(), 2 * reference.size());

        int different = 0;
        for (size_t i = 0; i < reference.size(); ++i) {
            if (doubled[2 * i] != reference[i]) ++different;
        }
        QCOMPARE(different, 0);

        const frame_t whole = 100;
        samples_t take(reference.size(), 0.f);
        for (frame_t i = 0; i < frame_t(take.size()); ++i) {
            frame_t j = 2 * i - (2 * whole + 1);
            if (j >= 0) take[i] = doubled[j];
        }

        for (int i = 0; i < int(layout.events.size()); ++i) {
            LatencyCheck::Arrival a = find(take, kRate, expectedAt(layout, i));
            QVERIFY2(a.found, describe(a).constData());
            QVERIFY2(a.errorFrames == whole || a.errorFrames == whole + 1,
                     describe(a).constData());
            QVERIFY2(std::fabs(a.errorSeconds - (whole + 0.5) / kRate)
                     <= 0.5 / kRate + 1e-9, describe(a).constData());
        }
    }

    // A device at 48 kHz: the take's frames are 48 kHz frames, and the
    // expected times come from the session's 44.1 kHz layout. The take
    // is the same reference made at 48 kHz, the same times in seconds
    void finder_works_at_the_take_rate() {
        const double deviceRate = 48000.0;
        const LatencyCheck::Layout session = firstEvents(4);
        const LatencyCheck::Layout device = firstEvents(4, deviceRate);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(device.events[i].sweepStart * 441,
                     session.events[i].sweepStart * 480);
        }

        const frame_t shift = 4801;
        const samples_t take = shifted(LatencyCheck::generate(device), shift);
        for (int i = 0; i < 4; ++i) {
            LatencyCheck::Arrival a =
                find(take, deviceRate, expectedAt(session, i));
            QVERIFY2(a.found, describe(a).constData());
            QCOMPARE(a.errorFrames, shift);
            QVERIFY2(std::fabs(a.errorSeconds - shift / deviceRate) < 1e-9,
                     describe(a).constData());
            QVERIFY2(std::fabs(a.levelDb) < 0.01, describe(a).constData());
        }
    }

    // White noise as loud as the sweep, and ten times louder in power.
    // The sweep is still found to the frame; what the noise changes is
    // how sure the finder is
    void finder_hears_through_noise() {
        const LatencyCheck::Layout layout = firstEvents(4);
        const frame_t shift = 12345;
        const samples_t take = shifted(LatencyCheck::generate(layout), shift);
        const double sweepRms = rms(LatencyCheck::sweep(kRate));

        std::vector<double> sureness(layout.events.size(), 1000.0);
        for (double snr : { 0.0, -10.0 }) {
            // Uniform noise: its RMS is its amplitude over root 3
            const double amplitude = sweepRms * ratioOf(-snr) * std::sqrt(3.0);
            const samples_t noisy =
                mixed(take, TestSignals::whiteNoise(int(take.size()), 20260925,
                                                    amplitude), 1.0);
            for (int i = 0; i < int(layout.events.size()); ++i) {
                LatencyCheck::Arrival a = find(noisy, kRate, expectedAt(layout, i));
                QVERIFY2(a.found, describe(a).constData());
                QVERIFY2(std::abs(a.errorFrames - shift) <= 1, describe(a).constData());
                QVERIFY2(a.peakOverMedianDb < sureness[i], describe(a).constData());
                sureness[i] = a.peakOverMedianDb;
            }
        }
    }

    // A small speaker, or an earcup held to a mic: nothing much below
    // 1 kHz or above 4 kHz. The filters delay the band a little
    // themselves (the 4 kHz low-pass by about 60 microseconds), which
    // is all the error there is
    void finder_hears_a_band_limited_path() {
        const LatencyCheck::Layout layout = firstEvents(4);
        const frame_t shift = 12345;
        const samples_t take = shifted(LatencyCheck::generate(layout), shift);

        const samples_t highPassed = filtered(take, kRate, 1000.0, true);
        const samples_t lowPassed = filtered(take, kRate, 4000.0, false);
        const samples_t both = filtered(highPassed, kRate, 4000.0, false);

        for (const samples_t *path : { &highPassed, &lowPassed, &both }) {
            for (int i = 0; i < int(layout.events.size()); ++i) {
                LatencyCheck::Arrival a = find(*path, kRate, expectedAt(layout, i));
                QVERIFY2(a.found, describe(a).constData());
                QVERIFY2(std::fabs(a.errorSeconds - shift / kRate) < 0.0001,
                         describe(a).constData());
            }
        }
    }

    // A speaker or mic wired the other way round
    void finder_ignores_polarity() {
        const LatencyCheck::Layout layout = firstEvents(4);
        const frame_t shift = 12345;
        samples_t take = shifted(LatencyCheck::generate(layout), shift);
        for (float &v : take) v = -v;

        for (int i = 0; i < int(layout.events.size()); ++i) {
            LatencyCheck::Arrival a = find(take, kRate, expectedAt(layout, i));
            QVERIFY2(a.found, describe(a).constData());
            QCOMPARE(a.errorFrames, shift);
            QVERIFY2(std::fabs(a.levelDb) < 0.01, describe(a).constData());
        }
    }

    // A reflection 7 ms after the direct sound, and stronger than it:
    // the direct sound's time is the one wanted. Exactly kEarliestPeakDb
    // stronger is the rule's edge, where rounding decides, so the test
    // is half a dB inside it; a dB outside it the reflection is taken,
    // which is where the rule gives up
    void finder_takes_the_direct_sound_before_a_stronger_reflection() {
        const LatencyCheck::Layout layout = firstEvents(4);
        const samples_t reference = LatencyCheck::generate(layout);
        const frame_t shift = 12345;
        const frame_t reflection = framesOf(0.007);
        const samples_t direct = shifted(reference, shift);
        const samples_t late = shifted(reference, shift + reflection);

        const samples_t inside =
            mixed(direct, late, ratioOf(LatencyCheck::kEarliestPeakDb - 0.5));
        const samples_t outside =
            mixed(direct, late, ratioOf(LatencyCheck::kEarliestPeakDb + 1.0));

        for (int i = 0; i < int(layout.events.size()); ++i) {
            LatencyCheck::Arrival a = find(inside, kRate, expectedAt(layout, i));
            QVERIFY2(a.found, describe(a).constData());
            QCOMPARE(a.errorFrames, shift);

            a = find(outside, kRate, expectedAt(layout, i));
            QVERIFY2(a.found, describe(a).constData());
            QCOMPARE(a.errorFrames, shift + reflection);
        }
    }

    // A path that loses the middle of the band (2.75 to 6.25 kHz: the
    // middle 100 ms of each sweep) makes the envelope beat, with
    // near-equal peaks a fraction of a millisecond apart. Those are one
    // arrival, not an earlier one
    void finder_takes_one_arrival_as_one() {
        const LatencyCheck::Layout layout = firstEvents(4);
        const frame_t shift = 12345;
        samples_t take = shifted(LatencyCheck::generate(layout), shift);
        for (const LatencyCheck::Event &e : layout.events) {
            for (frame_t f = framesOf(0.05); f < framesOf(0.15); ++f) {
                take[e.sweepStart + shift + f] = 0.f;
            }
        }

        for (int i = 0; i < int(layout.events.size()); ++i) {
            LatencyCheck::Arrival a = find(take, kRate, expectedAt(layout, i));
            QVERIFY2(a.found, describe(a).constData());
            QCOMPARE(a.errorFrames, shift);
        }
    }

    // Nothing but silence, or nothing but room noise: no sweep there
    void finder_finds_nothing_in_silence() {
        const samples_t silence(size_t(framesOf(5.0)), 0.f);
        const samples_t noise =
            TestSignals::whiteNoise(int(framesOf(5.0)), 7, 0.05);

        for (double expected : { 1.0, 2.5, 4.0 }) {
            LatencyCheck::Arrival a = find(silence, kRate, expected);
            QVERIFY2(!a.found, describe(a).constData());
            QVERIFY2(a.peakOverMedianDb < LatencyCheck::kMinPeakOverMedianDb,
                     describe(a).constData());

            a = find(noise, kRate, expected);
            QVERIFY2(!a.found, describe(a).constData());
        }
    }

    // The sweep's level as it arrived, and the loudest sample around it
    void finder_reports_levels() {
        const LatencyCheck::Layout layout = firstEvents(1);
        samples_t take = shifted(LatencyCheck::generate(layout), 12345);
        for (float &v : take) v *= 0.5f;

        LatencyCheck::Arrival a = find(take, kRate, expectedAt(layout, 0));
        QVERIFY2(a.found, describe(a).constData());
        QVERIFY2(std::fabs(a.levelDb - 20.0 * std::log10(0.5)) < 0.01,
                 describe(a).constData());
        QVERIFY(std::fabs(a.inputPeak -
                          0.5 * ratioOf(LatencyCheck::kPeakDbfs)) < 1e-5);
    }

    // The window stops at the ends of the take
    void finder_window_stops_at_the_ends_of_the_take() {
        const LatencyCheck::Layout layout = firstEvents(1);
        const samples_t reference = LatencyCheck::generate(layout);
        const frame_t sweepStart = layout.events[0].sweepStart;

        // From 0.1 s before the sweep to 50 ms after it: less than the
        // reach on either side
        const samples_t take(reference.begin() + (sweepStart - framesOf(0.1)),
                             reference.begin() + (sweepStart + framesOf(0.25)));

        LatencyCheck::Arrival a = find(take, kRate, 0.1);
        QVERIFY2(a.found, describe(a).constData());
        QCOMPARE(a.errorFrames, frame_t(0));

        // Expected before the take begins, and still within reach
        a = find(take, kRate, -0.5);
        QVERIFY2(a.found, describe(a).constData());
        QCOMPARE(a.errorFrames, framesOf(0.6));

        // Out of reach either side, or no take at all
        QVERIFY(!find(take, kRate, 5.0).found);
        QVERIFY(!find(take, kRate, -1.0).found);
        QVERIFY(!find(samples_t(), kRate, 0.1).found);
    }
};

#endif
