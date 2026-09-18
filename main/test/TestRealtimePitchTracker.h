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

#include "TestSignals.h"

#include "data/model/WritableWaveFileModel.h"

#include <QObject>
#include <QtTest>
#include <QTemporaryDir>
#include <QElapsedTimer>

#include <memory>
#include <vector>

// Collects pitchDetected() on the test (GUI) thread through a queued
// connection, which is how MainWindow receives it. QSignalSpy would
// connect directly and be written to from the tracker thread.
class PitchCollector : public QObject
{
    Q_OBJECT

public:
    struct Event {
        sv::sv_frame_t frame;
        double hz;
    };

    std::vector<Event> events;

    PitchCollector(RealtimePitchTracker *tracker) {
        connect(tracker, &RealtimePitchTracker::pitchDetected,
                this, &PitchCollector::pitchDetected);
    }

    int count() const { return int(events.size()); }

public slots:
    void pitchDetected(sv::sv_frame_t frame, double hz) {
        events.push_back({ frame, hz });
    }
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

    static int expectedHops(sv::sv_frame_t frames) {
        if (frames < kWindow) return 0;
        return int((frames - kWindow) / kHop) + 1;
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

        QEXPECT_FAIL("", "Review finding 11: the tracker reads channel 0 only",
                     Continue);
        QVERIFY(spy.count() > 0);
    }
};

#endif
