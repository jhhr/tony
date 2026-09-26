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

#ifndef TEST_REALTIME_YIN_H
#define TEST_REALTIME_YIN_H

// Tier 1: the pitch maths of RealtimePitchTracker, without the thread.

#include "../LatencyCheck.h"
#include "../RealtimePitchTracker.h"

#include "TestSignals.h"
#include "PyinReference.h"

#include "bqfft/FFT.h"

#include <QObject>
#include <QtTest>

#include <cmath>
#include <vector>

class TestRealtimeYin : public QObject
{
    Q_OBJECT

    static const int kWindow = RealtimePitchTracker::kWindowSize;

    static std::vector<double> difference(const std::vector<float> &buf) {
        breakfastquay::FFT fft(int(buf.size()));
        std::vector<double> diff;
        RealtimePitchTracker::yinDifferenceFFT(buf, diff, &fft);
        return diff;
    }

    static std::vector<double> naiveDifference(const std::vector<float> &buf) {
        int half = int(buf.size() / 2);
        std::vector<double> diff(half, 0.0);
        for (int tau = 1; tau < half; ++tau) {
            for (int j = 0; j < half; ++j) {
                double delta = double(buf[j]) - double(buf[j + tau]);
                diff[tau] += delta * delta;
            }
        }
        return diff;
    }

    // One analysis window through the same steps, lag limits and
    // range check as RealtimePitchTracker::run(). Returns Hz, or a
    // negative value if nothing would have been emitted.
    static double detect(const std::vector<float> &buf, double sampleRate,
                         double minFreq = 60.0, double maxFreq = 1000.0,
                         double threshold = 0.15) {
        std::vector<double> diff = difference(buf);
        RealtimePitchTracker::yinCMND(diff);
        int minLag = std::max(1, int(std::floor(sampleRate / maxFreq)));
        int maxLag = std::min(kWindow / 2 - 2,
                              int(std::ceil(sampleRate / minFreq)));
        double lag = RealtimePitchTracker::yinFindPitch
            (diff, minLag, maxLag, threshold);
        if (lag <= 0.0) return -1.0;
        double hz = sampleRate / lag;
        if (hz < minFreq || hz > maxFreq) return -1.0;
        return hz;
    }

    static void compareDifference(const std::vector<double> &actual,
                                  const std::vector<double> &expected,
                                  double relativeTolerance) {
        QCOMPARE(actual.size(), expected.size());
        // Relative to the largest value: near a period the difference
        // function approaches zero and a per-value ratio means nothing
        double scale = 0.0;
        for (double e : expected) scale = std::max(scale, std::abs(e));
        QVERIFY(scale > 0.0);
        for (int tau = 1; tau < int(expected.size()); ++tau) {
            double err = std::abs(actual[tau] - expected[tau]) / scale;
            if (err > relativeTolerance) {
                QFAIL(qPrintable(QString("at lag %1: got %2, expected %3, "
                                         "relative error %4")
                                 .arg(tau).arg(actual[tau])
                                 .arg(expected[tau]).arg(err)));
            }
        }
    }

private slots:
    void sine_accuracy_data() {
        QTest::addColumn<double>("hz");
        QTest::addColumn<double>("sampleRate");
        for (double sr : { 44100.0, 48000.0 }) {
            for (double hz : { 82.0, 110.0, 220.0, 440.0, 880.0 }) {
                QTest::newRow(qPrintable(QString("%1 Hz at %2").arg(hz).arg(sr)))
                    << hz << sr;
            }
        }
    }

    void sine_accuracy() {
        QFETCH(double, hz);
        QFETCH(double, sampleRate);
        double detected = detect(TestSignals::sine(hz, sampleRate, kWindow),
                                 sampleRate);
        QVERIFY2(detected > 0.0, "no pitch detected");
        double cents = TestSignals::centsBetween(detected, hz);
        QVERIFY2(std::abs(cents) < 5.0,
                 qPrintable(QString("detected %1 Hz, %2 cents out")
                            .arg(detected).arg(cents)));
    }

    void harmonic_tone() {
        double detected = detect(TestSignals::sawtooth(196.0, 44100.0, kWindow),
                                 44100.0);
        QVERIFY2(detected > 0.0, "no pitch detected");
        double cents = TestSignals::centsBetween(detected, 196.0);
        // An octave error would be 1200 cents out
        QVERIFY2(std::abs(cents) < 10.0,
                 qPrintable(QString("detected %1 Hz, %2 cents out")
                            .arg(detected).arg(cents)));
    }

    // Calibrate Audio's tones, as the reference has them and with their
    // fundamental taken out, as a phone's speaker or an earbud held to
    // the microphone loses it: the pitch all the same, from the window
    // in the middle of each tone, at the reference's rate and a phone's
    void reference_tones_with_and_without_their_fundamental_data() {
        QTest::addColumn<double>("sampleRate");
        QTest::addColumn<int>("event");
        QTest::addColumn<bool>("withoutFundamental");
        for (double sr : { 44100.0, 48000.0 }) {
            for (int event = 0; event < 4; ++event) {
                for (bool without : { false, true }) {
                    QTest::newRow(qPrintable
                                  (QString("event %1 at %2%3").arg(event)
                                   .arg(sr)
                                   .arg(without ? ", no fundamental" : "")))
                        << sr << event << without;
                }
            }
        }
    }

    void reference_tones_with_and_without_their_fundamental() {
        QFETCH(double, sampleRate);
        QFETCH(int, event);
        QFETCH(bool, withoutFundamental);

        const LatencyCheck::Layout layout =
            LatencyCheck::calibrationLayout(sampleRate);
        const LatencyCheck::Event &e = layout.events[event];
        std::vector<float> x = LatencyCheck::generate(layout);
        if (withoutFundamental) {
            x = TestSignals::withoutFundamental(x, sampleRate, e.toneHz);
        }

        const sv::sv_frame_t middle = e.toneStart + e.toneLength / 2;
        std::vector<float> window(x.begin() + (middle - kWindow / 2),
                                  x.begin() + (middle + kWindow / 2));
        auto rms = [](const std::vector<float> &v) {
            double sum = 0.0;
            for (float s : v) sum += double(s) * s;
            return std::sqrt(sum / double(v.size()));
        };
        if (withoutFundamental) {
            // What is left is the harmonics, at more than half the
            // tone's level: something a speaker without bass still plays
            const std::vector<float> generated = LatencyCheck::generate(layout);
            const std::vector<float> whole
                (generated.begin() + (middle - kWindow / 2),
                 generated.begin() + (middle + kWindow / 2));
            QVERIFY2(rms(window) > 0.5 * rms(whole),
                     qPrintable(QString("%1 of %2").arg(rms(window))
                                .arg(rms(whole))));

            // Nothing of the fundamental is left: a pure tone at it, or
            // as far off as the vibrato swings, taken out the same way,
            // is 60 dB down by the time the window begins
            for (double cents : { -LatencyCheck::kVibratoCents, 0.0,
                                  LatencyCheck::kVibratoCents }) {
                const std::vector<float> pure = TestSignals::sine
                    (e.toneHz * std::pow(2.0, cents / 1200.0), sampleRate,
                     int(middle - e.toneStart));
                const std::vector<float> left = TestSignals::withoutFundamental
                    (pure, sampleRate, e.toneHz);
                double peak = 0.0;
                for (size_t i = left.size() - kWindow / 2; i < left.size();
                     ++i) {
                    peak = std::max(peak, double(std::fabs(left[i])));
                }
                QVERIFY2(peak < 0.5e-3,
                         qPrintable(QString("%1 cents off: %2")
                                    .arg(cents).arg(peak)));
            }
        }

        double detected = detect(window, sampleRate);
        QVERIFY2(detected > 0.0, "no pitch detected");
        double cents = TestSignals::centsBetween(detected, e.toneHz);
        QVERIFY2(std::abs(cents) < 10.0,
                 qPrintable(QString("detected %1 Hz, %2 cents from %3")
                            .arg(detected).arg(cents).arg(e.toneHz)));
    }

    void silence() {
        std::vector<float> zeros(kWindow, 0.f);
        QVERIFY(detect(zeros, 44100.0) < 0.0);
    }

    void noise() {
        QVERIFY(detect(TestSignals::whiteNoise(kWindow, 12345), 44100.0) < 0.0);
    }

    void out_of_range_low() {
        double detected = detect(TestSignals::sine(40.0, 44100.0, kWindow),
                                 44100.0);
        QVERIFY2(detected < 0.0,
                 qPrintable(QString("detected %1 Hz").arg(detected)));
    }

    void out_of_range_high() {
        double detected = detect(TestSignals::sine(2000.0, 44100.0, kWindow),
                                 44100.0);
        QVERIFY2(detected < 0.0,
                 qPrintable(QString("detected %1 Hz").arg(detected)));
    }

    void dc_offset() {
        double detected = detect
            (TestSignals::sine(220.0, 44100.0, kWindow, 0.5, 0.3), 44100.0);
        QVERIFY2(detected > 0.0, "no pitch detected");
        QVERIFY(std::abs(TestSignals::centsBetween(detected, 220.0)) < 10.0);
    }

    void low_level() {
        // -50 dBFS: guards the precision of the single-precision FFT
        double amplitude = std::pow(10.0, -50.0 / 20.0);
        double detected = detect
            (TestSignals::sine(220.0, 44100.0, kWindow, amplitude), 44100.0);
        QVERIFY2(detected > 0.0, "no pitch detected");
        QVERIFY(std::abs(TestSignals::centsBetween(detected, 220.0)) < 10.0);
    }

    void diff_matches_pyin_data() {
        QTest::addColumn<bool>("useNoise");
        QTest::newRow("noise") << true;
        QTest::newRow("sine") << false;
    }

    void diff_matches_pyin() {
        QFETCH(bool, useNoise);
        std::vector<float> buf = useNoise ?
            TestSignals::whiteNoise(kWindow, 999) :
            TestSignals::sine(330.0, 44100.0, kWindow);
        std::vector<double> in(buf.begin(), buf.end());
        compareDifference(difference(buf), pyinFastDifference(in), 1e-4);
    }

    void diff_matches_naive() {
        // Independent of pyin: the textbook O(n^2) sum
        //   d(tau) = sum_{j<half} (x[j] - x[j+tau])^2
        std::vector<float> buf = TestSignals::whiteNoise(kWindow, 4242);
        std::vector<double> expected = naiveDifference(buf);

        // ...except for one known deviation, inherited from pYIN's
        // YinUtil::fastDifference. The running power term for lag tau
        // should cover x[tau] .. x[tau+half-1], but the recursion
        // brings in x[tau+half] at each step rather than
        // x[tau+half-1], so the window it sums skips x[half] and takes
        // x[half+tau] instead. Harmless for pitch (one sample's energy
        // in a thousand), but it has to be allowed for here, and it is
        // kept in the implementation so as to stay in step with pYIN.
        int half = kWindow / 2;
        for (int tau = 1; tau < half; ++tau) {
            expected[tau] += double(buf[half + tau]) * double(buf[half + tau])
                - double(buf[half]) * double(buf[half]);
        }

        compareDifference(difference(buf), expected, 1e-4);
    }

    void cmnd_properties() {
        std::vector<double> diff =
            difference(TestSignals::sine(220.0, 44100.0, kWindow));
        RealtimePitchTracker::yinCMND(diff);
        QCOMPARE(diff[0], 1.0);
        for (double d : diff) QVERIFY(d >= 0.0);

        std::vector<double> zeros(kWindow / 2, 0.0);
        RealtimePitchTracker::yinCMND(zeros);
        for (double d : zeros) QCOMPARE(d, 1.0);
    }
};

#endif
