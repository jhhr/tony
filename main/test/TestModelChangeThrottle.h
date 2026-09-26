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

#ifndef TEST_MODEL_CHANGE_THROTTLE_H
#define TEST_MODEL_CHANGE_THROTTLE_H

// Tier 2: telling a model's views of its changes, at most once an
// interval. What a pane does when told is TestUiChecks' business
// (live_dots_under_the_cursor).

#include "../ModelChangeThrottle.h"

#include "data/model/SparseTimeValueModel.h"

#include <QElapsedTimer>
#include <QObject>
#include <QtTest>

#include <memory>
#include <utility>
#include <vector>

class TestModelChangeThrottle : public QObject
{
    Q_OBJECT

    typedef sv::sv_frame_t frame_t;

    static constexpr int kInterval = 50;

    // A model like the live dots': changes held back by the model itself
    std::shared_ptr<sv::SparseTimeValueModel> m_model;
    sv::ModelId m_id;
    std::vector<std::pair<frame_t, frame_t>> m_told;

private slots:
    void init() {
        m_model = std::make_shared<sv::SparseTimeValueModel>(44100, 256, false);
        m_id = sv::ModelById::add(m_model);
        m_told.clear();
        connect(m_model.get(), &sv::Model::modelChangedWithin,
                this, [this](sv::ModelId, frame_t from, frame_t to) {
                    m_told.push_back({ from, to });
                });
    }

    void cleanup() {
        m_model.reset();
        sv::ModelById::release(m_id);
    }

    // Why there is a throttle at all: a dot added to such a model tells
    // nobody, so nothing would ever draw it
    void the_model_tells_nothing_itself() {
        m_model->add(sv::Event(1000, 220.f, ""));
        m_model->add(sv::Event(1256, 221.f, ""));
        QCoreApplication::processEvents();
        QCOMPARE(int(m_told.size()), 0);
    }

    void first_change_is_told_at_once() {
        ModelChangeThrottle throttle(kInterval);
        throttle.setModel(m_id);
        throttle.changed(1000, 1256);
        QCOMPARE(int(m_told.size()), 1);
        QCOMPARE(m_told[0], std::make_pair(frame_t(1000), frame_t(1256)));
    }

    // ... and what follows within the interval is told together, once,
    // when it is up
    void changes_within_an_interval_are_told_together() {
        ModelChangeThrottle throttle(kInterval);
        throttle.setModel(m_id);
        throttle.changed(1000, 1256);
        throttle.changed(2000, 2256);
        throttle.changed(1500, 1756);
        QCOMPARE(int(m_told.size()), 1);

        QTRY_COMPARE_WITH_TIMEOUT(int(m_told.size()), 2, kInterval * 10);
        QCOMPARE(m_told[1], std::make_pair(frame_t(1500), frame_t(2256)));

        // Nothing more to tell
        QTest::qWait(kInterval * 3);
        QCOMPARE(int(m_told.size()), 2);
    }

    // A steady stream is told once an interval, however many changes
    void a_stream_is_told_once_an_interval() {
        ModelChangeThrottle throttle(kInterval);
        throttle.setModel(m_id);
        QElapsedTimer timer;
        timer.start();
        int changes = 0;
        while (timer.elapsed() < kInterval * 10) {
            throttle.changed(changes * 256, changes * 256 + 256);
            ++changes;
            // A change every 2 ms by the clock, the throttle's timer
            // running meanwhile. Not qWait(2): on Windows a wait that
            // short lasts a timer tick of about 15 ms
            QElapsedTimer step;
            step.start();
            while (step.nsecsElapsed() < 2000000) {
                QCoreApplication::processEvents();
            }
        }
        QVERIFY(changes > 100);
        QVERIFY2(m_told.size() >= 5 && m_told.size() <= 13,
                 qPrintable(QString("%1 changes over ten intervals were told "
                                    "%2 times")
                            .arg(changes).arg(m_told.size())));
        // Between them the notices cover every change. The last one comes
        // an interval after the changes stop, as late as the timer fires:
        // wait for it rather than for a fixed time, which a loaded
        // machine's timers can overrun
        QCOMPARE(m_told.front().first, frame_t(0));
        QTRY_COMPARE_WITH_TIMEOUT(m_told.back().second, frame_t(changes * 256),
                                  kInterval * 40);
        for (size_t i = 1; i < m_told.size(); ++i) {
            QVERIFY(m_told[i].first <= m_told[i-1].second);
        }
    }

    // After a quiet interval the next change is told at once again
    void quiet_then_told_at_once() {
        ModelChangeThrottle throttle(kInterval);
        throttle.setModel(m_id);
        throttle.changed(1000, 1256);
        QTest::qWait(kInterval * 3);
        QCOMPARE(int(m_told.size()), 1);
        throttle.changed(5000, 5256);
        QCOMPARE(int(m_told.size()), 2);
    }

    // A change not yet told goes with the model, and with no model
    // nothing is told at all
    void no_model_tells_nothing() {
        ModelChangeThrottle throttle(kInterval);
        throttle.changed(1000, 1256);
        QCOMPARE(int(m_told.size()), 0);

        throttle.setModel(m_id);
        throttle.changed(1000, 1256);
        throttle.changed(2000, 2256);
        QCOMPARE(int(m_told.size()), 1);
        throttle.setModel({});
        QTest::qWait(kInterval * 3);
        QCOMPARE(int(m_told.size()), 1);
    }

    // The model released under it: nothing to tell, and no harm
    void model_gone() {
        ModelChangeThrottle throttle(kInterval);
        sv::ModelId id = m_id;
        throttle.setModel(id);
        throttle.changed(1000, 1256);
        throttle.changed(2000, 2256);
        m_model.reset();
        sv::ModelById::release(id);
        m_id = {};
        QTest::qWait(kInterval * 3);
        throttle.changed(3000, 3256);
        QCOMPARE(int(m_told.size()), 1);
    }
};

#endif
