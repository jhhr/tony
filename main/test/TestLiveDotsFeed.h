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

#ifndef TEST_LIVE_DOTS_FEED_H
#define TEST_LIVE_DOTS_FEED_H

// Tier 2: the live tracker's estimates brought to the GUI thread in
// batches. That a take's dots keep up with a slow GUI thread is
// TestRecordWorkflow's business (live_dots_keep_up_with_a_slow_gui).

#include "../LiveDotsFeed.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QObject>
#include <QtTest>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

class TestLiveDotsFeed : public QObject
{
    Q_OBJECT

    typedef RealtimePitchTracker::Estimate Estimate;
    typedef RealtimePitchTracker::Estimates Estimates;

    static constexpr int kInterval = 20;

    // What the tracker would hold: found on another thread, taken whole
    struct Found {
        std::mutex mutex;
        Estimates estimates;
        sv::sv_frame_t next = 1024;

        void add(int n) {
            std::lock_guard<std::mutex> guard(mutex);
            for (int i = 0; i < n; ++i) {
                estimates.push_back({ next, 220.0 });
                next += 256;
            }
        }
        Estimates take() {
            Estimates taken;
            std::lock_guard<std::mutex> guard(mutex);
            taken.swap(estimates);
            return taken;
        }
        int waiting() {
            std::lock_guard<std::mutex> guard(mutex);
            return int(estimates.size());
        }
    };

    static void busy(int ms) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) { }
    }

    // A widget, as far as its paint events go: they take a while
    class Painted : public QObject
    {
    public:
        int paints = 0;
        bool event(QEvent *e) override {
            if (e->type() == QEvent::Paint) {
                ++paints;
                busy(5);
                return true;
            }
            return QObject::event(e);
        }
    };

private slots:
    void hands_over_everything_at_once() {
        Found found;
        std::vector<Estimates> batches;
        LiveDotsFeed feed(kInterval);
        found.add(100);
        feed.start([&]() { return found.take(); },
                   [&](const Estimates &e) { batches.push_back(e); });
        QTest::qWait(kInterval * 5);
        feed.stop();

        QCOMPARE(int(batches.size()), 1);
        QCOMPARE(int(batches[0].size()), 100);
        for (int i = 1; i < 100; ++i) {
            QVERIFY(batches[0][i].frame > batches[0][i-1].frame);
        }
        QCOMPARE(found.waiting(), 0);
    }

    void nothing_found_nothing_handed_over() {
        int looked = 0, handed = 0;
        LiveDotsFeed feed(kInterval);
        feed.start([&]() { ++looked; return Estimates(); },
                   [&](const Estimates &) { ++handed; });
        QTest::qWait(kInterval * 10);
        feed.stop();
        QVERIFY(looked >= 3);
        QCOMPARE(handed, 0);
    }

    // The point of it all: estimates come in faster than a slow GUI
    // thread could take them one by one, and it keeps up all the same,
    // with bigger batches, one a look
    void a_slow_gui_takes_bigger_batches() {
        Found found;
        std::atomic<int> produced(0);
        std::atomic<bool> producing(true);
        std::thread producer([&]() {
            // one a hop, as the tracker finds them: 5.8 ms
            while (producing) {
                found.add(1);
                ++produced;
                std::this_thread::sleep_for(std::chrono::microseconds(5800));
            }
        });

        int handedOver = 0, calls = 0, worstBehind = 0;
        LiveDotsFeed feed(kInterval);
        QElapsedTimer timer;
        timer.start();
        feed.start([&]() { return found.take(); },
                   [&](const Estimates &e) {
                       ++calls;
                       handedOver += int(e.size());
                       // far more than an estimate's worth of time
                       busy(25);
                       worstBehind = std::max
                           (worstBehind, produced - handedOver);
                   });
        QTest::qWait(1500);
        producing = false;
        producer.join();
        QTest::qWait(kInterval * 5);
        feed.stop();
        qint64 elapsed = timer.elapsed();

        // Everything arrived, and never more than a look and a slow
        // batch's worth of them waited: 45 ms is 8 estimates; handed over
        // one at a time, 25 ms each, some 1100 would be waiting by now
        QCOMPARE(handedOver, int(produced));
        QCOMPARE(found.waiting(), 0);
        QVERIFY2(worstBehind <= 40,
                 qPrintable(QString("%1 estimates were waiting at worst")
                            .arg(worstBehind)));
        QVERIFY2(calls <= elapsed / kInterval + 2,
                 qPrintable(QString("%1 batches in %2 ms")
                            .arg(calls).arg(elapsed)));
        QVERIFY2(double(handedOver) / calls >= 4.0,
                 qPrintable(QString("%1 estimates in %2 batches")
                            .arg(handedOver).arg(calls)));
    }

    void stop_leaves_the_rest_with_the_source() {
        Found found;
        int handed = 0;
        LiveDotsFeed feed(kInterval);
        feed.start([&]() { return found.take(); },
                   [&](const Estimates &) { ++handed; });
        QVERIFY(feed.isRunning());
        feed.stop();
        QVERIFY(!feed.isRunning());
        found.add(10);
        QTest::qWait(kInterval * 5);
        QCOMPARE(handed, 0);
        QCOMPARE(found.waiting(), 10);
    }

    void reports_once_a_second() {
        Found found;
        std::vector<LiveDotsFeed::Report> reports;
        int handedOver = 0;
        sv::sv_frame_t newest = -1;
        LiveDotsFeed feed(kInterval);
        feed.setReporter([&](const LiveDotsFeed::Report &r) {
            reports.push_back(r);
        });
        feed.start([&]() { return found.take(); },
                   [&](const Estimates &e) {
                       handedOver += int(e.size());
                       newest = e.back().frame;
                   });
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 2300) {
            found.add(3);
            QTest::qWait(30);
        }
        feed.stop();

        QCOMPARE(int(reports.size()), 2);
        int reported = 0;
        for (const auto &r : reports) {
            QVERIFY2(r.seconds >= 0.95 && r.seconds < 1.5,
                     qPrintable(QString("a report of %1 s").arg(r.seconds)));
            QVERIFY(r.batches.count > 10);
            QVERIFY(r.intervals.count > 10);
            QVERIFY2(r.intervals.averageMs() >= kInterval * 0.8 &&
                     r.intervals.averageMs() < kInterval * 3,
                     qPrintable(QString("looked every %1 ms")
                                .arg(r.intervals.averageMs())));
            QCOMPARE(r.paints.count, 0);
            reported += r.estimates;
        }
        QVERIFY(reported > 0 && reported <= handedOver);
        QVERIFY(reports.back().newestFrame > 0);
        QVERIFY(reports.back().newestFrame <= newest);
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QVERIFY(reports.back().guiThreadShare >= 0.0);
#endif
    }

    void times_the_paints_of_a_widget() {
        Found found;
        Painted painted;
        std::vector<LiveDotsFeed::Report> reports;
        LiveDotsFeed feed(kInterval);
        feed.setReporter([&](const LiveDotsFeed::Report &r) {
            reports.push_back(r);
        });
        feed.start([&]() { return found.take(); },
                   [&](const Estimates &) { });
        feed.timePaintsOf(&painted);
        for (int i = 0; i < 3; ++i) {
            QEvent paint(QEvent::Paint);
            QCoreApplication::sendEvent(&painted, &paint);
        }
        // and they still reach the widget
        QCOMPARE(painted.paints, 3);
        QTRY_VERIFY_WITH_TIMEOUT(!reports.empty(), 3000);
        QCOMPARE(reports[0].paints.count, 3);
        QVERIFY(reports[0].paints.averageMs() >= 4.5);
        QVERIFY(reports[0].paints.maxMs >= 4.5);

        // Not timed after the take, and not held up either
        feed.stop();
        QEvent paint(QEvent::Paint);
        QCoreApplication::sendEvent(&painted, &paint);
        QCOMPARE(painted.paints, 4);
    }

    void describes_a_report() {
        LiveDotsFeed::Report r;
        r.seconds = 1.0;
        r.estimates = 175;
        for (int i = 0; i < 25; ++i) r.batches.add(i == 3 ? 1.5 : 0.25);
        for (int i = 0; i < 25; ++i) r.intervals.add(i == 7 ? 95.0 : 40.0);
        for (int i = 0; i < 49; ++i) r.paints.add(i == 0 ? 18.0 : 3.0);
        r.guiThreadShare = 0.284;
        QString text = LiveDotsFeed::describe(r);
        QVERIFY2(text.startsWith("25 batches of 7.0 dots, 0.30 ms each "
                                 "(at most 1.5), every 42 ms (at most 95); "
                                 "pane painted 49 times, 3.31 ms each "
                                 "(at most 18.0); GUI thread 28% of a core"),
                 qPrintable(text));
        r.guiThreadShare = -1.0;
        QVERIFY(!LiveDotsFeed::describe(r).contains("GUI thread"));
    }
};

#endif
