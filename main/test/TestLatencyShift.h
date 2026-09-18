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

#ifndef TEST_LATENCY_SHIFT_H
#define TEST_LATENCY_SHIFT_H

// Tier 2: what is done with the latency figure. A singing take is
// aligned with the reference by giving its model a negative start
// frame; these tests pin down what that does to reads.

#include "../LatencyUtils.h"

#include "data/model/WritableWaveFileModel.h"

#include <QObject>
#include <QtTest>
#include <QTemporaryDir>

#include <memory>
#include <vector>

class TestLatencyShift : public QObject
{
    Q_OBJECT

    static const int kLength = 8192;
    static const int kImpulseAt = 1000; // the latency, N

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    // A finished take: silence with a single impulse at kImpulseAt
    std::shared_ptr<sv::WritableWaveFileModel> makeTake() {
        QString path = m_dir.filePath
            (QString("take-%1.wav").arg(++m_fileCounter));
        auto model = std::make_shared<sv::WritableWaveFileModel>
            (path, 44100.0, 1,
             sv::WritableWaveFileModel::Normalisation::None);
        std::vector<float> data(kLength, 0.f);
        data[kImpulseAt] = 1.f;
        const float *ptr = data.data();
        model->addSamples(&ptr, kLength);
        model->writeComplete();
        return model;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void start_frame_shifts_reads() {
        auto model = makeTake();
        QCOMPARE(model->getData(0, kImpulseAt, 1).size(), size_t(1));
        QCOMPARE(model->getData(0, kImpulseAt, 1)[0], 1.f);

        model->setStartFrame(-kImpulseAt);

        QCOMPARE(model->getStartFrame(), sv::sv_frame_t(-kImpulseAt));
        auto atZero = model->getData(0, 0, 1);
        QCOMPARE(atZero.size(), size_t(1));
        QCOMPARE(atZero[0], 1.f);
        auto atOld = model->getData(0, kImpulseAt, 1);
        QCOMPARE(atOld.size(), size_t(1));
        QCOMPARE(atOld[0], 0.f);
    }

    void negative_region_is_silent() {
        auto model = makeTake();
        model->setStartFrame(-kImpulseAt);

        // Wholly before the start: nothing, or silence
        for (float v : model->getData(0, -kImpulseAt - 100, 50)) {
            QCOMPARE(v, 0.f);
        }

        // Straddling the start: a short read is acceptable, but whatever
        // comes back must be the opening silence of the file, not the
        // impulse and not garbage
        auto straddling = model->getData(0, -kImpulseAt - 10, 20);
        QVERIFY(straddling.size() <= size_t(20));
        for (float v : straddling) {
            QCOMPARE(v, 0.f);
        }
    }

    void end_frame_shifts() {
        auto model = makeTake();
        sv::sv_frame_t before = model->getEndFrame();
        model->setStartFrame(-kImpulseAt);
        QCOMPARE(model->getEndFrame(), before - kImpulseAt);
    }

    void latency_sum() {
        QCOMPARE(computeRecordingLatency(512, 256), sv::sv_frame_t(768));
        QCOMPARE(computeRecordingLatency(512, 0), sv::sv_frame_t(512));
        QCOMPARE(computeRecordingLatency(0, 256), sv::sv_frame_t(256));
        QCOMPARE(computeRecordingLatency(0, 0), sv::sv_frame_t(0));
        // An unavailable figure must never pull the take later
        QCOMPARE(computeRecordingLatency(-1, 256), sv::sv_frame_t(256));
        QCOMPARE(computeRecordingLatency(-100, -100), sv::sv_frame_t(0));
    }

    // Review finding 8: a live dot goes where the finished pitch track
    // will put the same sound
    void live_dot_shift() {
        QCOMPARE(compensatedLiveFrame(5000, 768), sv::sv_frame_t(4232));
        QCOMPARE(compensatedLiveFrame(768, 768), sv::sv_frame_t(0));
        QCOMPARE(compensatedLiveFrame(5000, 0), sv::sv_frame_t(5000));
        // Sound from before the reference started has nowhere to go:
        // the caller drops anything negative
        QVERIFY(compensatedLiveFrame(767, 768) < 0);
        QVERIFY(compensatedLiveFrame(0, 768) < 0);
        // An unavailable latency must never pull the dot later
        QCOMPARE(compensatedLiveFrame(5000, -1), sv::sv_frame_t(5000));
    }
};

#endif
