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

    typedef std::vector<LatencyCheck::PunchIn> punchins_t;

    // The take as the splice would make it: inside each punch-in's
    // range, the reference as it arrived that time, this many frames
    // late (early, if negative); silence outside them. A later punch-in
    // replaces an earlier one where they overlap
    static samples_t spliced(const samples_t &reference, double rate,
                             const punchins_t &punchIns,
                             const std::vector<frame_t> &shifts) {
        const frame_t n = frame_t(reference.size());
        samples_t take(reference.size(), 0.f);
        for (size_t p = 0; p < punchIns.size(); ++p) {
            const frame_t from = std::max(frame_t(0),
                                          framesOf(punchIns[p].start, rate));
            const frame_t to = std::min(n, framesOf(punchIns[p].end, rate));
            for (frame_t f = from; f < to; ++f) {
                const frame_t g = f - shifts[p];
                take[f] = (g >= 0 && g < n) ? reference[g] : 0.f;
            }
        }
        return take;
    }

    // Four punch-ins at about 2, 8, 14 and 20 s, as a calibration makes
    // them. In the calibration layout they judge two events each, and
    // one in the last: 3.1, 4.7 / 9.1, 11.4 / 15.7, 17.7 / 21.9 s
    static punchins_t fourPunchIns() {
        return { { 2.0, 7.0 }, { 8.0, 13.0 }, { 14.0, 19.0 }, { 20.0, 25.0 } };
    }

    // Room noise at about -65 dBFS RMS, far under the sweeps
    static samples_t withRoomNoise(const samples_t &x) {
        return mixed(x, TestSignals::whiteNoise(int(x.size()), 4711, 0.001),
                     1.0);
    }

    // Scale one event's sweep, as it lies in a take placed this late
    static void scaleSweep(samples_t &take, const LatencyCheck::Layout &layout,
                           int event, frame_t shift, double gain) {
        const frame_t from = layout.events[event].sweepStart + shift;
        const frame_t length = framesOf(LatencyCheck::kSweepSeconds, layout.rate);
        for (frame_t f = from; f < from + length; ++f) {
            take[f] = float(take[f] * gain);
        }
    }

    static LatencyCheck::TakeSummary judge(const LatencyCheck::Layout &layout,
                                           const samples_t &take, double rate,
                                           const punchins_t &punchIns) {
        return LatencyCheck::judgeTake(layout, take.data(),
                                       frame_t(take.size()), rate, punchIns);
    }

    static QByteArray describe(const LatencyCheck::TakeSummary &s) {
        QStringList flags;
        for (LatencyCheck::Verdict v : s.flags) {
            flags << LatencyCheck::verdictName(v);
        }
        return QString("%1 [%2], found %3 of %4, median %5 ms, spread %6 "
                       "ms, slope %7 %, residual %8 ms, input peak %9, "
                       "fading %10 dB, echo %11 at %12 ms %13 dB")
            .arg(LatencyCheck::verdictName(s.verdict))
            .arg(flags.join(" "))
            .arg(s.found)
            .arg(s.judged)
            .arg(s.medianOffset * 1000.0)
            .arg(s.spread * 1000.0)
            .arg(s.slope * 100.0)
            .arg(s.slopeResidual * 1000.0)
            .arg(s.inputPeak)
            .arg(s.fadingDb)
            .arg(s.echo.heard ? "heard" : "none")
            .arg(s.echo.delaySeconds * 1000.0)
            .arg(s.echo.levelDb)
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

    // Judging a take. Four punch-ins all placed the same, 12345 frames
    // late: every judged event found at that offset, in the order the
    // punch-ins were recorded, and nothing else to say
    void judge_a_steady_take() {
        const LatencyCheck::Layout layout = LatencyCheck::calibrationLayout();
        const frame_t shift = 12345;
        const punchins_t punchIns = fourPunchIns();
        const samples_t take = spliced(LatencyCheck::generate(layout), kRate,
                                       punchIns, { shift, shift, shift, shift });

        const LatencyCheck::TakeSummary s = judge(layout, take, kRate, punchIns);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::Ok, describe(s).constData());
        QVERIFY2(s.flags.empty(), describe(s).constData());
        QCOMPARE(s.judged, 7);
        QCOMPARE(s.found, 7);
        QVERIFY2(std::fabs(s.medianOffset - shift / kRate) < 1e-9,
                 describe(s).constData());
        QVERIFY2(s.spread < 1e-9 && std::fabs(s.slope) < 1e-9 &&
                 s.slopeResidual < 1e-9, describe(s).constData());
        QVERIFY2(std::fabs(s.inputPeak - ratioOf(LatencyCheck::kPeakDbfs)) < 1e-5,
                 describe(s).constData());
        QVERIFY2(std::fabs(s.fadingDb) < 0.01, describe(s).constData());
        QVERIFY2(!s.echo.heard, describe(s).constData());

        QCOMPARE(int(s.punchIns.size()), 4);
        const int judgedIn[] = { 2, 2, 2, 1 };
        for (int p = 0; p < 4; ++p) {
            QCOMPARE(s.punchIns[p].range.start, punchIns[p].start);
            QCOMPARE(s.punchIns[p].judged, judgedIn[p]);
            QCOMPARE(s.punchIns[p].found, judgedIn[p]);
            QVERIFY(std::fabs(s.punchIns[p].medianOffset - shift / kRate) < 1e-9);
            QVERIFY(s.punchIns[p].spread < 1e-9);
        }

        const int events[] = { 1, 2, 4, 5, 7, 8, 10 };
        const int in[] = { 0, 0, 1, 1, 2, 2, 3 };
        QCOMPARE(int(s.events.size()), 7);
        for (int k = 0; k < 7; ++k) {
            QCOMPARE(s.events[k].event, events[k]);
            QCOMPARE(s.events[k].punchIn, in[k]);
            QCOMPARE(s.events[k].expectedSeconds, expectedAt(layout, events[k]));
            QCOMPARE(s.events[k].arrival.errorFrames, shift);
        }
    }

    // A sweep lost, in room noise. Found out of judged is what counts:
    // one or two of six lost still measures, three is too many, and
    // silence is nothing at all. The first sweep lost is in the second
    // half of the run, and one of three there does not make it Fading
    void judge_a_take_with_events_missing() {
        const LatencyCheck::Layout layout = LatencyCheck::calibrationLayout();
        const frame_t shift = 12345;
        const punchins_t punchIns = { { 2.0, 7.0 }, { 8.0, 13.0 }, { 14.0, 19.0 } };
        samples_t take = spliced(LatencyCheck::generate(layout), kRate,
                                 punchIns, { shift, shift, shift });

        // Events 1, 2 / 4, 5 / 7, 8 judged
        scaleSweep(take, layout, 5, shift, 0.0);
        LatencyCheck::TakeSummary s =
            judge(layout, withRoomNoise(take), kRate, punchIns);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::Ok, describe(s).constData());
        QCOMPARE(s.judged, 6);
        QCOMPARE(s.found, 5);
        QCOMPARE(s.punchIns[1].found, 1);
        QVERIFY(!s.events[3].arrival.found);
        QCOMPARE(s.events[3].event, 5);
        QVERIFY2(std::abs(s.medianOffset * kRate - shift) < 1.0,
                 describe(s).constData());

        // Exactly two thirds found
        scaleSweep(take, layout, 1, shift, 0.0);
        s = judge(layout, withRoomNoise(take), kRate, punchIns);
        QCOMPARE(s.found, 4);
        QVERIFY2(!s.flagged(LatencyCheck::Verdict::NoSignal), describe(s).constData());

        scaleSweep(take, layout, 7, shift, 0.0);
        s = judge(layout, withRoomNoise(take), kRate, punchIns);
        QCOMPARE(s.found, 3);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::NoSignal, describe(s).constData());

        s = judge(layout, withRoomNoise(samples_t(take.size(), 0.f)), kRate,
                  punchIns);
        QCOMPARE(s.judged, 6);
        QCOMPARE(s.found, 0);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::NoSignal, describe(s).constData());
        QCOMPARE(s.medianOffset, 0.0);
    }

    // Something in the input path taking the sweeps out as the run goes
    // on: 5 dB quieter at every event, down to -30 dB, all still found
    void judge_a_fading_take() {
        const LatencyCheck::Layout layout = LatencyCheck::calibrationLayout();
        const frame_t shift = 12345;
        const punchins_t punchIns = fourPunchIns();
        samples_t take = spliced(LatencyCheck::generate(layout), kRate,
                                 punchIns, { shift, shift, shift, shift });
        const int events[] = { 1, 2, 4, 5, 7, 8, 10 };
        for (int k = 0; k < 7; ++k) {
            scaleSweep(take, layout, events[k], shift, ratioOf(-5.0 * k));
        }

        const LatencyCheck::TakeSummary s = judge(layout, take, kRate, punchIns);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::Fading, describe(s).constData());
        QCOMPARE(int(s.flags.size()), 1);
        QCOMPARE(s.found, 7);
        // -5 dB against -25, the halves' medians
        QVERIFY2(std::fabs(s.fadingDb - 20.0) < 0.1, describe(s).constData());
        QVERIFY2(std::fabs(s.medianOffset - shift / kRate) < 1e-9,
                 describe(s).constData());
    }

    // Too loud for the input: clipped at full scale. Found where they
    // are all the same, but the verdict is the level. Just under it
    // (-1.1 dBFS) is fine
    void judge_a_clipped_take() {
        const LatencyCheck::Layout layout = LatencyCheck::calibrationLayout();
        const frame_t shift = 12345;
        const punchins_t punchIns = fourPunchIns();
        const samples_t take = spliced(LatencyCheck::generate(layout), kRate,
                                       punchIns, { shift, shift, shift, shift });

        samples_t loud(take), clipped(take);
        for (float &v : loud) v *= 3.5f;
        for (float &v : clipped) v = std::max(-1.f, std::min(1.f, v * 8.f));

        LatencyCheck::TakeSummary s = judge(layout, clipped, kRate, punchIns);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::Clipped, describe(s).constData());
        QCOMPARE(s.inputPeak, 1.0);
        QCOMPARE(s.found, 7);
        QVERIFY2(std::abs(s.medianOffset * kRate - shift) < 1.0,
                 describe(s).constData());

        s = judge(layout, loud, kRate, punchIns);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::Ok, describe(s).constData());
        QVERIFY2(s.inputPeak < ratioOf(-1.0), describe(s).constData());
    }

    // A device at 48 kHz, its take placed frame for frame on the 44.1
    // kHz timeline: each punch-in lands early by P (1 - 44100/48000),
    // P being where it starts, and the take is read at 48 kHz. On a
    // line, so PositionDependent (and Scattered, since it is). The same
    // punch-ins at offsets whose line is as steep but leaves 30 ms
    // about it: Scattered. The punch-ins stay under 10 s, where the
    // misplacement is still within the finder's reach
    void judge_position_dependent_against_scattered() {
        const double deviceRate = 48000.0;
        const LatencyCheck::Layout layout = LatencyCheck::calibrationLayout();
        const samples_t recorded =
            LatencyCheck::generate(LatencyCheck::calibrationLayout(deviceRate));

        // Events 0, 1 and 3, one each
        const punchins_t punchIns = { { 0.1, 2.1 }, { 2.2, 4.2 }, { 6.3, 8.3 } };
        std::vector<frame_t> shifts;
        for (const LatencyCheck::PunchIn &p : punchIns) {
            shifts.push_back(-framesOf(p.start * (1.0 - kRate / deviceRate),
                                       deviceRate));
        }

        LatencyCheck::TakeSummary s =
            judge(layout, spliced(recorded, deviceRate, punchIns, shifts),
                  deviceRate, punchIns);
        QCOMPARE(s.found, 3);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::PositionDependent,
                 describe(s).constData());
        QVERIFY2(s.flagged(LatencyCheck::Verdict::Scattered), describe(s).constData());
        QVERIFY2(std::fabs(s.slope + (1.0 - kRate / deviceRate)) < 1e-4,
                 describe(s).constData());
        QVERIFY2(s.slopeResidual < 0.0001, describe(s).constData());

        // Slope about -0.7 %, residuals +12, -18 and +6 ms
        const double offsets[] = { 0.0, -0.045, -0.050 };
        for (int p = 0; p < 3; ++p) shifts[p] = framesOf(offsets[p], deviceRate);
        s = judge(layout, spliced(recorded, deviceRate, punchIns, shifts),
                  deviceRate, punchIns);
        QCOMPARE(s.found, 3);
        QVERIFY2(std::fabs(s.slope) > LatencyCheck::kPositionSlope,
                 describe(s).constData());
        QVERIFY2(s.verdict == LatencyCheck::Verdict::Scattered,
                 describe(s).constData());
        QVERIFY2(!s.flagged(LatencyCheck::Verdict::PositionDependent),
                 describe(s).constData());
    }

    // Two stream starts that placed their punch-ins differently. Two
    // punch-ins always lie on a line, so however steep (here 20 ms over
    // 2.1 s), it is never PositionDependent
    void judge_two_punch_ins_apart() {
        const LatencyCheck::Layout layout = firstEvents(3);
        const samples_t reference = LatencyCheck::generate(layout);
        const punchins_t punchIns = { { 0.1, 2.1 }, { 2.2, 4.2 } };
        const frame_t shift = framesOf(0.1);

        struct { double apart; LatencyCheck::Verdict verdict; } cases[] = {
            { 0.020, LatencyCheck::Verdict::Scattered },
            { 0.010, LatencyCheck::Verdict::Unsteady },
            { 0.003, LatencyCheck::Verdict::Ok },
        };
        for (const auto &c : cases) {
            const frame_t other = shift + framesOf(c.apart);
            const LatencyCheck::TakeSummary s =
                judge(layout, spliced(reference, kRate, punchIns, { shift, other }),
                      kRate, punchIns);
            QCOMPARE(s.found, 2);
            QVERIFY2(s.verdict == c.verdict, describe(s).constData());
            QCOMPARE(int(s.flags.size()), c.verdict == LatencyCheck::Verdict::Ok ? 0 : 1);
            QVERIFY2(std::fabs(s.spread - c.apart) < 2.0 / kRate,
                     describe(s).constData());
            QVERIFY2(std::fabs(s.medianOffset - (shift + other) / 2.0 / kRate)
                     < 1e-9, describe(s).constData());
        }
    }

    // The take's own input played back out and heard again, 40 ms
    // later and 12 dB down: named, and the take still measures. At 3
    // dB down it hides the sweeps from the finder, and is still named.
    // Noise at 0 dB SNR has second peaks as loud, at no one delay. A
    // reflection 9 ms late and 5.5 dB stronger leaves a second peak
    // at 10.1 ms, 23 dB down, after every sweep: too early for an echo
    void judge_hears_a_monitoring_echo() {
        const LatencyCheck::Layout layout = LatencyCheck::calibrationLayout();
        const frame_t shift = 12345;
        const punchins_t punchIns = fourPunchIns();
        const samples_t take = spliced(LatencyCheck::generate(layout), kRate,
                                       punchIns, { shift, shift, shift, shift });
        const frame_t delay = framesOf(0.040);

        LatencyCheck::TakeSummary s =
            judge(layout, mixed(take, shifted(take, delay), ratioOf(-12.0)),
                  kRate, punchIns);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::Ok, describe(s).constData());
        QVERIFY2(s.echo.heard, describe(s).constData());
        QCOMPARE(s.echo.events, 7);
        QVERIFY2(std::fabs(s.echo.delaySeconds - delay / kRate) < 1e-9,
                 describe(s).constData());
        QVERIFY2(std::fabs(s.echo.levelDb + 12.0) < 0.5, describe(s).constData());

        s = judge(layout, mixed(take, shifted(take, delay), ratioOf(-3.0)),
                  kRate, punchIns);
        QCOMPARE(s.found, 0);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::NoSignal, describe(s).constData());
        QVERIFY2(s.echo.heard, describe(s).constData());
        QVERIFY2(std::fabs(s.echo.levelDb + 3.0) < 0.5, describe(s).constData());

        const double sweepRms = rms(LatencyCheck::sweep(kRate));
        const samples_t noisy =
            mixed(take, TestSignals::whiteNoise(int(take.size()), 20260925,
                                                sweepRms * std::sqrt(3.0)), 1.0);
        s = judge(layout, noisy, kRate, punchIns);
        QCOMPARE(s.found, 7);
        for (const LatencyCheck::EventResult &e : s.events) {
            QVERIFY(e.arrival.secondLevelDb > -LatencyCheck::kEchoMaxBelowDb);
        }
        QVERIFY2(!s.echo.heard, describe(s).constData());

        s = judge(layout, mixed(take, shifted(take, framesOf(0.009)),
                                ratioOf(5.5)),
                  kRate, punchIns);
        QCOMPARE(s.found, 7);
        QVERIFY2(std::fabs(s.medianOffset - shift / kRate) < 1e-9,
                 describe(s).constData());
        for (const LatencyCheck::EventResult &e : s.events) {
            QVERIFY(e.arrival.secondLevelDb > -LatencyCheck::kEchoMaxBelowDb);
        }
        QVERIFY2(!s.echo.heard, describe(s).constData());
    }

    // What the finder reads for an event has to lie inside the punch-in,
    // kJudgeMarginSeconds from its ends; otherwise the event is not
    // judged, since a sweep cut by the splice is no fault of the path.
    // Where a later punch-in overlaps, the event is judged in that one
    void judge_only_events_inside_a_punch_in() {
        const LatencyCheck::Layout layout = LatencyCheck::calibrationLayout();
        const samples_t reference = LatencyCheck::generate(layout);
        const frame_t shift = framesOf(0.1);
        const double before =
            LatencyCheck::kSearchSeconds + LatencyCheck::kJudgeMarginSeconds;
        const double after = LatencyCheck::kSearchSeconds +
            LatencyCheck::kSweepSeconds + LatencyCheck::kJudgeMarginSeconds;

        // The sweep at 3.1 s arrives at 3.2 s, where the range ends
        punchins_t punchIns = { { 2.0, 3.2 } };
        LatencyCheck::TakeSummary s =
            judge(layout, spliced(reference, kRate, punchIns, { shift }),
                  kRate, punchIns);
        QCOMPARE(s.judged, 0);
        QVERIFY2(s.verdict == LatencyCheck::Verdict::NoSignal, describe(s).constData());

        // Just inside the margin at either end, then just outside it
        const double t = expectedAt(layout, 4);
        for (double by : { 0.001, -0.001 }) {
            const bool inside = by > 0.0;
            for (const LatencyCheck::PunchIn &p :
                     { LatencyCheck::PunchIn(t - before - by, t + after + 0.1),
                       LatencyCheck::PunchIn(t - before - 0.1, t + after + by) }) {
                punchIns = { p };
                s = judge(layout, spliced(reference, kRate, punchIns, { shift }),
                          kRate, punchIns);
                QCOMPARE(s.judged, inside ? 1 : 0);
                QCOMPARE(s.found, inside ? 1 : 0);
            }
        }

        // 9.1 s in the first punch-in only; 11.4 s in both, so in the
        // second, which recorded over the first
        const frame_t later = framesOf(0.2);
        punchIns = { { 8.0, 13.0 }, { 10.5, 13.0 } };
        s = judge(layout, spliced(reference, kRate, punchIns, { shift, later }),
                  kRate, punchIns);
        QCOMPARE(s.judged, 2);
        QCOMPARE(s.punchIns[0].judged, 1);
        QCOMPARE(s.punchIns[1].judged, 1);
        QCOMPARE(s.events[0].event, 4);
        QCOMPARE(s.events[1].event, 5);
        QCOMPARE(s.events[1].punchIn, 1);
        QCOMPARE(s.events[1].arrival.errorFrames, later);
    }

    // The calibration's arithmetic. A take that landed late was placed
    // with too small a round trip, and the new one is larger by the
    // offset; early, smaller. From takes placed with 0.2 and 0.31 s
    // through a path whose round trip is 0.25 s
    void calibration_adds_the_offset() {
        QVERIFY(std::fabs(LatencyCheck::calibratedRoundTrip(0.2, 0.03) - 0.23)
                < 1e-12);
        QVERIFY(std::fabs(LatencyCheck::calibratedRoundTrip(0.2, -0.05) - 0.15)
                < 1e-12);

        const LatencyCheck::Layout layout = LatencyCheck::calibrationLayout();
        const samples_t reference = LatencyCheck::generate(layout);
        const punchins_t punchIns = { { 2.0, 7.0 } };
        const double roundTrip = 0.25;

        for (double used : { 0.2, 0.31 }) {
            const frame_t late = framesOf(roundTrip - used);
            const LatencyCheck::TakeSummary s =
                judge(layout, spliced(reference, kRate, punchIns, { late }),
                      kRate, punchIns);
            QCOMPARE(s.found, 2);
            QVERIFY2((s.medianOffset > 0.0) == (used < roundTrip),
                     describe(s).constData());
            const double measured =
                LatencyCheck::calibratedRoundTrip(used, s.medianOffset);
            QVERIFY2(std::fabs(measured - roundTrip) < 1e-9,
                     describe(s).constData());
        }
    }
};

#endif
