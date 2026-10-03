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

#ifndef TEST_REALTIME_PITCH_TRACKER_H
#define TEST_REALTIME_PITCH_TRACKER_H

// Tier 2: the tracker thread, fed the way the record target feeds the
// model during a take.

#include "../RealtimePitchTracker.h"
#include "../VoiceThreshold.h"

#include "TestSignals.h"

#include "data/model/WritableWaveFileModel.h"

#include <QObject>
#include <QtTest>
#include <QTemporaryDir>
#include <QElapsedTimer>

#include <cmath>
#include <memory>
#include <vector>

// Takes the tracker's estimates on the test (GUI) thread, which is how
// MainWindow gets them (LiveDotsFeed): whatever has been found since the
// last look, whenever the count is asked for
class PitchCollector
{
public:
    struct Event {
        sv::sv_frame_t frame;
        double hz;
    };

    std::vector<Event> events;

    PitchCollector(RealtimePitchTracker *tracker) : m_tracker(tracker) { }

    int count() {
        for (const auto &e : m_tracker->takeEstimates()) {
            events.push_back({ e.frame, e.hz });
        }
        return int(events.size());
    }

private:
    RealtimePitchTracker *m_tracker;
};

class TestRealtimePitchTracker : public QObject
{
    Q_OBJECT

    static constexpr double kRate = 44100.0;
    static const int kWindow = 2048;
    static const int kHop = 256;
    static const int kBlock = 441; // 10 ms, the record target's cadence

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    struct Recording {
        std::shared_ptr<sv::WritableWaveFileModel> model;
        sv::ModelId id;
        int channels = 1;
        sv::sv_frame_t written = 0;

        ~Recording() {
            if (!id.isNone()) sv::ModelById::release(id);
        }

        // Append one channel-count-wide signal in record-sized blocks,
        // making each block readable straight away
        void append(const std::vector<std::vector<float>> &channelData) {
            int n = int(channelData[0].size());
            for (int start = 0; start < n; start += kBlock) {
                int count = std::min(kBlock, n - start);
                std::vector<const float *> ptrs;
                for (const auto &c : channelData) {
                    ptrs.push_back(c.data() + start);
                }
                model->addSamples(ptrs.data(), count);
                model->updateModel();
            }
            written += n;
        }

        void appendMono(const std::vector<float> &signal) {
            append({ signal });
        }
    };

    std::unique_ptr<Recording> makeRecording(int channels = 1) {
        auto r = std::make_unique<Recording>();
        QString path = m_dir.filePath
            (QString("take-%1.wav").arg(++m_fileCounter));
        r->model = std::make_shared<sv::WritableWaveFileModel>
            (path, kRate, channels,
             sv::WritableWaveFileModel::Normalisation::None);
        r->channels = channels;
        r->id = sv::ModelById::add(r->model);
        return r;
    }

    // Wait until the tracker has had the chance to handle every hop in
    // n frames: either the expected number of events has arrived, or
    // the count has stopped growing
    static void settle(PitchCollector &spy, int expected = -1) {
        QElapsedTimer timer;
        timer.start();
        int last = -1;
        int stableFor = 0;
        while (timer.elapsed() < 5000) {
            QTest::qWait(50);
            if (expected >= 0 && spy.count() >= expected) return;
            if (spy.count() == last) {
                if (++stableFor >= 6) return;
            } else {
                stableFor = 0;
                last = int(spy.count());
            }
        }
    }

    // The peak of a sine whose RMS is the given level
    static double peakAt(double dbfs) {
        return std::pow(10.0, dbfs / 20.0) * std::sqrt(2.0);
    }

    static int expectedHops(sv::sv_frame_t frames) {
        if (frames < kWindow) return 0;
        return int((frames - kWindow) / kHop) + 1;
    }

    // All the tracker finds in a recording made in full before it
    // starts, with the level floor given: every hop is analysed in one
    // pass, so the same input gives the same estimates every time
    void trackAll(const std::vector<float> &signal, double floor,
                  std::vector<PitchCollector::Event> &found) {
        auto rec = makeRecording();
        rec->appendMono(signal);
        RealtimePitchTracker tracker(rec->id);
        tracker.setMinLevel(floor);
        PitchCollector spy(&tracker);
        tracker.start();
        const sv::sv_frame_t lastWindowEnd =
            sv::sv_frame_t(expectedHops(rec->written) - 1) * kHop + kWindow;
        QTRY_COMPARE_WITH_TIMEOUT(tracker.getFramesAnalysed(),
                                  lastWindowEnd, 5000);
        tracker.stop();
        spy.count();
        found = spy.events;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        qRegisterMetaType<sv::sv_frame_t>("sv::sv_frame_t");
        qRegisterMetaType<sv::sv_frame_t>("sv_frame_t");
    }

    void emits_correct_pitch() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();
        rec->appendMono(TestSignals::sine(330.0, kRate, int(kRate)));
        settle(spy, expectedHops(rec->written));
        tracker.stop();

        QVERIFY(spy.count() > 0);
        for (const auto &event : spy.events) {
            double hz = event.hz;
            QVERIFY2(std::abs(TestSignals::centsBetween(hz, 330.0)) < 10.0,
                     qPrintable(QString("%1 Hz").arg(hz)));
        }
    }

    void frame_grid() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();
        rec->appendMono(TestSignals::sine(330.0, kRate, int(kRate)));
        settle(spy, expectedHops(rec->written));
        tracker.stop();

        QVERIFY(spy.count() > 0);
        sv::sv_frame_t previous = -1;
        for (const auto &event : spy.events) {
            sv::sv_frame_t frame = event.frame;
            QVERIFY2(frame >= kWindow / 2 && (frame - kWindow / 2) % kHop == 0,
                     qPrintable(QString("frame %1 is off the hop grid")
                                .arg(frame)));
            QVERIFY2(frame > previous,
                     qPrintable(QString("frame %1 after %2")
                                .arg(frame).arg(previous)));
            previous = frame;
        }
    }

    void coverage() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();
        rec->appendMono(TestSignals::sine(330.0, kRate, int(kRate)));
        int expected = expectedHops(rec->written);
        settle(spy, expected);
        tracker.stop();

        QVERIFY2(std::abs(int(spy.count()) - expected) <= 2,
                 qPrintable(QString("%1 events for %2 hops")
                            .arg(spy.count()).arg(expected)));
    }

    void unvoiced_gap() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();

        const int n = int(kRate / 2);
        rec->appendMono(TestSignals::sine(330.0, kRate, n));
        rec->appendMono(std::vector<float>(n, 0.f));
        rec->appendMono(TestSignals::sine(330.0, kRate, n, 0.5, 0.0, 2 * n));
        settle(spy);
        tracker.stop();

        bool before = false, after = false;
        for (const auto &event : spy.events) {
            sv::sv_frame_t centre = event.frame;
            sv::sv_frame_t windowStart = centre - kWindow / 2;
            sv::sv_frame_t windowEnd = centre + kWindow / 2;
            QVERIFY2(!(windowStart >= n && windowEnd <= 2 * n),
                     qPrintable(QString("event at %1 lies wholly in silence")
                                .arg(centre)));
            if (windowEnd <= n) before = true;
            if (windowStart >= 2 * n) after = true;
        }
        QVERIFY(before);
        QVERIFY(after);
    }

    // A steady tone below the level floor, as a room's fans are between
    // the sounds, gives no pitch, though YIN alone finds one in it; the
    // same tone above the floor does
    void quiet_tone_gives_no_pitch() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();

        const int n = int(kRate / 2);
        rec->appendMono(TestSignals::sine(306.0, kRate, n, peakAt(-66.0)));
        rec->appendMono(TestSignals::sine(306.0, kRate, n, peakAt(-50.0),
                                          0.0, n));
        settle(spy);
        tracker.stop();

        int loud = 0;
        for (const auto &event : spy.events) {
            QVERIFY2(event.frame + kWindow / 2 > n,
                     qPrintable(QString("a pitch at frame %1, of the quiet "
                                        "tone alone").arg(event.frame)));
            if (event.frame - kWindow / 2 >= n) ++loud;
        }
        QVERIFY2(loud > (expectedHops(n) * 3) / 4,
                 qPrintable(QString("%1 pitches of the loud tone").arg(loud)));
    }

    // A loud sound with no pitch, starting after the quiet tone, gives no
    // pitch either: not in the windows whose second half reaches into
    // it, where YIN still hears the tone in their first half
    void quiet_tone_before_a_sound_gives_no_pitch() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();

        const int n = int(kRate / 2);
        std::vector<float> after =
            TestSignals::sine(306.0, kRate, n, peakAt(-66.0), 0.0, n);
        const std::vector<float> noise = TestSignals::whiteNoise(n, 3, 0.1);
        for (int i = 0; i < n; ++i) after[i] += noise[i];
        rec->appendMono(TestSignals::sine(306.0, kRate, n, peakAt(-66.0)));
        rec->appendMono(after);
        settle(spy);
        tracker.stop();

        QVERIFY2(spy.count() == 0,
                 qPrintable(QString("%1 pitches, the first at frame %2 "
                                    "(the sound starts at %3)")
                            .arg(spy.count())
                            .arg(spy.events.empty() ? -1 :
                                 spy.events[0].frame)
                            .arg(n)));
    }

    // The floor is each input's level: a quiet tone on both inputs, whose
    // mixdown, their sum, reads 6 dB louder and above the floor, gives no
    // pitch
    void quiet_tone_on_both_inputs_gives_no_pitch() {
        auto rec = makeRecording(2);
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();

        const int n = int(kRate / 2);
        std::vector<float> tone =
            TestSignals::sine(306.0, kRate, n, peakAt(-63.0));
        rec->append({ tone, tone });
        settle(spy);
        tracker.stop();

        QVERIFY2(spy.count() == 0,
                 qPrintable(QString("%1 pitches").arg(spy.count())));
    }

    // The voice threshold raises the floor (VoiceThreshold::liveFloor()):
    // music the microphone hears from speakers, under it, gives no
    // pitch, where at the tracker's own floor it gives one nearly every
    // hop
    void a_tone_under_the_threshold_gives_no_pitch() {
        const std::vector<float> tone =
            TestSignals::sine(330.0, kRate, int(kRate / 2), peakAt(-45.0));

        std::vector<PitchCollector::Event> atFloor, raised;
        trackAll(tone, RealtimePitchTracker::kMinLevel, atFloor);
        if (QTest::currentTestFailed()) return;
        trackAll(tone, VoiceThreshold::liveFloor(-40.0), raised);
        if (QTest::currentTestFailed()) return;

        QVERIFY2(int(atFloor.size()) > expectedHops(tone.size()) * 3 / 4,
                 qPrintable(QString("%1 pitches at the tracker's own floor")
                            .arg(atFloor.size())));
        QVERIFY2(raised.empty(),
                 qPrintable(QString("%1 pitches under the threshold, the "
                                    "first at frame %2")
                            .arg(raised.size())
                            .arg(raised.empty() ? -1 : raised[0].frame)));
    }

    // With the threshold under the singing, the tracker finds exactly
    // what it finds without one: the same frames and the same Hz. The
    // threshold moves no dot and delays none; it only spares YIN the
    // windows it gates. The phrases start and stop on the hop grid, in
    // room noise at about -71 dBFS, so that every window's first half
    // holds either none of them (under both floors) or a hop's worth or
    // more (about -29 dBFS or louder, over both)
    void a_threshold_under_the_singing_changes_no_pitch() {
        const int n = int(kRate);
        std::vector<float> signal = TestSignals::whiteNoise(n, 11, 0.0005);

        // A steady phrase, then one gliding from 200 to 300 Hz
        const double amplitude = peakAt(-23.0);
        for (int i = 10 * kHop; i < 70 * kHop; ++i) {
            signal[i] += float(amplitude *
                               std::sin(2.0 * M_PI * 220.5 * i / kRate));
        }
        double phase = 0.0;
        const int glideFrom = 90 * kHop, glideTo = 160 * kHop;
        for (int i = glideFrom; i < glideTo; ++i) {
            double hz = 200.0 + 100.0 * (i - glideFrom) / (glideTo - glideFrom);
            phase += 2.0 * M_PI * hz / kRate;
            signal[i] += float(amplitude * std::sin(phase));
        }

        std::vector<PitchCollector::Event> atFloor, raised;
        trackAll(signal, RealtimePitchTracker::kMinLevel, atFloor);
        if (QTest::currentTestFailed()) return;
        trackAll(signal, VoiceThreshold::liveFloor(-40.0), raised);
        if (QTest::currentTestFailed()) return;

        // Not vacuous: dots on most hops of both phrases, at many pitches
        const int phraseHops = 60 + 70;
        QVERIFY2(int(atFloor.size()) > phraseHops * 3 / 4,
                 qPrintable(QString("%1 pitches").arg(atFloor.size())));
        double lowest = 1e9, highest = 0.0;
        for (const auto &e : atFloor) {
            lowest = std::min(lowest, e.hz);
            highest = std::max(highest, e.hz);
        }
        QVERIFY2(lowest < 215.0 && highest > 280.0,
                 qPrintable(QString("pitches from %1 to %2 Hz")
                            .arg(lowest).arg(highest)));

        QCOMPARE(raised.size(), atFloor.size());
        for (size_t i = 0; i < atFloor.size(); ++i) {
            QVERIFY2(raised[i].frame == atFloor[i].frame &&
                     raised[i].hz == atFloor[i].hz,
                     qPrintable(QString("pitch %1: %2 Hz at frame %3 with "
                                        "the threshold, %4 Hz at %5 without")
                                .arg(i).arg(raised[i].hz, 0, 'g', 17)
                                .arg(raised[i].frame)
                                .arg(atFloor[i].hz, 0, 'g', 17)
                                .arg(atFloor[i].frame)));
        }
    }

    void stop_is_prompt() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        tracker.start();
        rec->appendMono(TestSignals::sine(330.0, kRate, int(kRate) / 4));

        QElapsedTimer timer;
        timer.start();
        tracker.stop();
        QVERIFY2(timer.elapsed() < 200,
                 qPrintable(QString("stop() took %1 ms").arg(timer.elapsed())));
        QVERIFY(tracker.isFinished());
    }

    void no_events_after_stop() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();
        rec->appendMono(TestSignals::sine(330.0, kRate, int(kRate) / 2));
        settle(spy, expectedHops(rec->written));
        tracker.stop();
        QCoreApplication::processEvents();

        auto countAtStop = spy.count();
        rec->appendMono(TestSignals::sine(330.0, kRate, int(kRate) / 2));
        QTest::qWait(100);
        QCOMPARE(spy.count(), countAtStop);
    }

    void model_released_midway() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();
        rec->appendMono(TestSignals::sine(330.0, kRate, int(kRate) / 2));
        settle(spy, expectedHops(rec->written));

        // As when the document lets go of the model. We keep our own
        // reference until the tracker has stopped: the tracker holds
        // one per iteration, and if that were the last, the model (a
        // QObject owning a timer) would be destroyed on its thread.
        sv::ModelById::release(rec->id);
        rec->id = {};

        QTest::qWait(100);
        QVERIFY(tracker.isRunning());
        tracker.stop();
        QVERIFY(tracker.isFinished());
    }

    void starts_before_data() {
        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();
        QTest::qWait(100);
        QCOMPARE(int(spy.count()), 0);

        rec->appendMono(TestSignals::sine(330.0, kRate, int(kRate) / 2));
        settle(spy, expectedHops(rec->written));
        tracker.stop();
        QVERIFY(spy.count() > 0);
    }

    void stereo_signal_on_ch1() {
        // A stereo interface with the microphone on its second input
        auto rec = makeRecording(2);
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();
        const int n = int(kRate) / 2;
        rec->append({ std::vector<float>(n, 0.f),
                      TestSignals::sine(330.0, kRate, n) });
        settle(spy, expectedHops(rec->written));
        tracker.stop();

        QVERIFY(spy.count() > 0);
        for (const auto &event : spy.events) {
            double hz = event.hz;
            QVERIFY2(std::abs(TestSignals::centsBetween(hz, 330.0)) < 10.0,
                     qPrintable(QString("%1 Hz").arg(hz)));
        }
    }

    void stereo_other_input_noisy() {
        // As above, but the unused input is not silent: the mixdown
        // carries its noise along with the voice
        auto rec = makeRecording(2);
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();
        const int n = int(kRate) / 2;
        rec->append({ TestSignals::whiteNoise(n, 7, 0.02),
                      TestSignals::sine(330.0, kRate, n) });
        settle(spy, expectedHops(rec->written));
        tracker.stop();

        QVERIFY(spy.count() > expectedHops(rec->written) / 2);
        for (const auto &event : spy.events) {
            double hz = event.hz;
            QVERIFY2(std::abs(TestSignals::centsBetween(hz, 330.0)) < 20.0,
                     qPrintable(QString("%1 Hz").arg(hz)));
        }
    }

    // One input chosen (InputChannel): the tracker hears it alone, not
    // the other input. Two tones, one on each input, each tracked
    // alone; the mixdown of the two would give neither throughout
    void one_input_chosen_is_tracked_alone() {
        const int n = int(kRate) / 2;
        for (int channel : { 0, 1 }) {
            auto rec = makeRecording(2);
            rec->append({ TestSignals::sine(330.0, kRate, n, 0.3f),
                          TestSignals::sine(440.0, kRate, n, 0.5f) });
            RealtimePitchTracker tracker(rec->id);
            tracker.setChannel(channel);
            PitchCollector spy(&tracker);
            tracker.start();
            settle(spy, expectedHops(rec->written));
            tracker.stop();

            const double hz = (channel == 0 ? 330.0 : 440.0);
            QCOMPARE(int(spy.count()), expectedHops(rec->written));
            for (const auto &event : spy.events) {
                QVERIFY2(std::abs(TestSignals::centsBetween(event.hz, hz))
                         < 10.0,
                         qPrintable(QString("input %1: %2 Hz, not %3")
                                    .arg(channel + 1).arg(event.hz)
                                    .arg(hz)));
            }
        }
    }

    // ... and at its own level: a microphone on input 1 at -30 dBFS, the
    // other input silent, is over a floor of -33 dBFS; the channels'
    // average reads it 6 dB down, under the floor
    void one_input_chosen_is_heard_at_its_own_level() {
        const int n = int(kRate) / 2;
        for (int channel : { 0, -1 }) {
            auto rec = makeRecording(2);
            rec->append({ TestSignals::sine(330.0, kRate, n,
                                            float(peakAt(-30.0))),
                          std::vector<float>(size_t(n), 0.f) });
            RealtimePitchTracker tracker(rec->id);
            tracker.setChannel(channel);
            tracker.setMinLevel(-33.0);
            PitchCollector spy(&tracker);
            tracker.start();
            const sv::sv_frame_t lastWindowEnd =
                sv::sv_frame_t(expectedHops(rec->written) - 1) * kHop +
                kWindow;
            QTRY_COMPARE_WITH_TIMEOUT(tracker.getFramesAnalysed(),
                                      lastWindowEnd, 5000);
            tracker.stop();
            if (channel == 0) {
                QCOMPARE(int(spy.count()), expectedHops(rec->written));
            } else {
                QCOMPARE(int(spy.count()), 0);
            }
        }
    }

    // An input chosen that the recording does not have, as on a device
    // with one input: what there is, as without a choice
    void a_chosen_input_the_recording_lacks_is_the_mixdown() {
        auto rec = makeRecording(1);
        const int n = int(kRate) / 2;
        rec->appendMono(TestSignals::sine(330.0, kRate, n));
        RealtimePitchTracker tracker(rec->id);
        tracker.setChannel(1);
        PitchCollector spy(&tracker);
        tracker.start();
        settle(spy, expectedHops(rec->written));
        tracker.stop();
        QCOMPARE(int(spy.count()), expectedHops(rec->written));
    }

    // A tone of 220.5 Hz with its harmonics to 4 kHz (as the audio
    // check's, without the vibrato), and 30 ms of its subharmonic under
    // it, faded in and out. In the windows that hold the burst the dip
    // at the period rises over YIN's threshold (to 0.25 or more) while
    // the one at twice the period stays under (0.08 or less): 4 hops
    // slip an octave low, and the hops either side are on the tone
    // (0.11 or less), at every alignment of the burst with the hops.
    // The slips are dropped, and every other hop's dot is there
    void an_octave_slip_is_dropped() {
        const double hz = 220.5;
        const int n = int(kRate);
        const int harmonics = int(4000.0 / hz);
        std::vector<float> signal(n);
        double peak = 0.0;
        std::vector<double> x(n, 0.0);
        for (int i = 0; i < n; ++i) {
            double t = i / kRate;
            for (int k = 1; k <= harmonics; ++k) {
                x[i] += std::sin(2.0 * M_PI * hz * k * t +
                                 M_PI * k * k / harmonics) / k;
            }
            peak = std::max(peak, std::fabs(x[i]));
        }
        const int burstStart = 22082, burstLength = 1323;
        for (int i = 0; i < n; ++i) {
            double v = 0.25 * x[i] / peak;
            int b = i - burstStart;
            if (b >= 0 && b < burstLength) {
                double hann = 0.5 - 0.5 * std::cos
                    (2.0 * M_PI * b / (burstLength - 1));
                v += 0.1 * hann * std::sin(2.0 * M_PI * (hz / 2.0) *
                                           b / kRate);
            }
            signal[i] = float(v);
        }

        auto rec = makeRecording();
        RealtimePitchTracker tracker(rec->id);
        PitchCollector spy(&tracker);
        tracker.start();
        rec->appendMono(signal);
        const int slipped = 4;
        settle(spy, expectedHops(rec->written) - slipped);
        tracker.stop();

        sv::sv_frame_t widestGap = 0;
        for (int i = 0; i < int(spy.events.size()); ++i) {
            double cents = TestSignals::centsBetween(spy.events[i].hz, hz);
            QVERIFY2(std::abs(cents) < 20.0,
                     qPrintable(QString("a dot at %1 Hz, frame %2")
                                .arg(spy.events[i].hz)
                                .arg(spy.events[i].frame)));
            if (i > 0) {
                widestGap = std::max(widestGap, spy.events[i].frame -
                                     spy.events[i-1].frame);
            }
        }
        QCOMPARE(int(spy.events.size()),
                 expectedHops(rec->written) - slipped);
        QCOMPARE(widestGap, sv::sv_frame_t((slipped + 1) * kHop));
    }
};

#endif
