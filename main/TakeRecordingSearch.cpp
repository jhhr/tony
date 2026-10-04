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

#include "TakeRecordingSearch.h"

#include <QFileInfo>
#include <QProgressDialog>
#include <QThread>
#include <QTimer>

bool
TakeRecordingSearch::Result::lookedFor
(const RecordingAlignment::Segment &segment) const
{
    return double(segment.end - segment.start) >=
        RecordingAlignment::kMinRangeSeconds * takeRate;
}

TakeRecordingSearch::TakeRecordingSearch(QString takePath,
                                         const Coverage &coverage,
                                         QString recordingPath,
                                         QWidget *parent) :
    QObject(parent),
    m_shared(std::make_shared<Shared>()),
    m_result(&m_shared->result),
    m_parent(parent),
    m_thread(nullptr),
    m_dialog(nullptr),
    m_timer(nullptr)
{
    m_shared->result.takePath = takePath;
    m_shared->result.coverage = coverage;
    m_shared->result.recordingPath = recordingPath;
}

TakeRecordingSearch::~TakeRecordingSearch()
{
    if (m_thread) {
        m_shared->cancelled.store(true);
        m_thread->wait();
        delete m_thread;
    }
    delete m_dialog;
}

void
TakeRecordingSearch::start()
{
    // The thread has the shared part to itself until it finishes, and
    // touches nothing else. All of the recording is read once, for its
    // level, which is the long part, and each range looked for in that,
    // as the stretches it is made of
    std::shared_ptr<Shared> shared = m_shared;
    m_thread = QThread::create([shared]() {
        Result &result = shared->result;
        QString error;
        auto take = RecordingAlignment::openWav(result.takePath, error);
        std::unique_ptr<RecordingAlignment::Source> recording;
        if (take) {
            recording = RecordingAlignment::openWav(result.recordingPath,
                                                    error);
        }
        if (!take || !recording) {
            result.error = error;
            return;
        }
        result.takeRate = take->rate();
        result.recordingRate = recording->rate();
        auto going = [&shared](int percent) {
            shared->percent.store(percent);
            return !shared->cancelled.load();
        };

        std::vector<double> levels;
        if (!RecordingAlignment::levels(*recording, levels, [&](int p) {
            return going(p * 8 / 10);
        })) return;

        const Coverage::Ranges ranges = result.coverage.getRanges();
        const int count = int(ranges.size());
        for (int i = 0; i < count; ++i) {
            const Coverage::Range &r = ranges[size_t(i)];
            for (const auto &segment : RecordingAlignment::findSegments
                     (*take, r.start, r.end, *recording, levels, [&](int p) {
                         return going(80 + (i * 100 + p) / (5 * count));
                     })) {
                result.segments.push_back(segment);
            }
            if (shared->cancelled.load()) return;
        }
        shared->percent.store(100);
    });
    connect(m_thread, &QThread::finished,
            this, &TakeRecordingSearch::threadFinished);

    m_dialog = new QProgressDialog
        (tr("Looking for the take's singing in \"%1\"...")
         .arg(QFileInfo(m_shared->result.recordingPath).fileName()),
         tr("Cancel"), 0, 100, m_parent);
    m_dialog->setWindowTitle(tr("Replace Take Audio from Recording"));
    m_dialog->setWindowModality(Qt::WindowModal);
    m_dialog->setMinimumDuration(0);
    m_dialog->setAutoClose(false);
    m_dialog->setAutoReset(false);
    m_dialog->setValue(0);
    connect(m_dialog, &QProgressDialog::canceled, this, [shared]() {
        shared->cancelled.store(true);
    });
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this, shared]() {
        if (m_dialog) m_dialog->setValue(shared->percent.load());
    });
    m_timer->start(100);
    m_dialog->show();

    m_thread->start();
}

void
TakeRecordingSearch::threadFinished()
{
    // Before the dialog goes: closing it says it was cancelled
    m_shared->result.cancelled = m_shared->cancelled.load();
    m_thread->deleteLater();
    m_thread = nullptr;
    m_timer->stop();
    if (m_dialog) {
        m_dialog->disconnect(this);
        m_dialog->close();
        m_dialog->deleteLater();
        m_dialog = nullptr;
    }
    emit finished();
}
