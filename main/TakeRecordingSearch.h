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

#ifndef TONY_TAKE_RECORDING_SEARCH_H
#define TONY_TAKE_RECORDING_SEARCH_H

#include "Coverage.h"
#include "RecordingAlignment.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

class QProgressDialog;
class QThread;
class QTimer;
class QWidget;

/**
 * The search Takes > Replace Take Audio from Recording makes: each range
 * of a take's coverage looked for in a longer recording of the same
 * singing (RecordingAlignment), on a thread of its own, the recording
 * read once for its level, with a progress dialog that can cancel it.
 * The dialog is shown, never exec()'d, and is modal to the window, so
 * that the take stays as it is meanwhile.
 *
 * finished() comes once, on the GUI thread, when the search has ended,
 * found or not or cancelled; result() then says how. Deleting it while
 * it runs cancels the search and waits for its thread.
 */
class TakeRecordingSearch : public QObject
{
    Q_OBJECT

public:
    struct Result {
        QString recordingPath;
        QString takePath;
        Coverage coverage;
        double takeRate = 0;
        double recordingRate = 0;
        sv::sv_frame_t recordingFrames = 0;

        /// The stretches of the coverage's ranges, in order, end to end
        /// over each range, each found where it is in the recording or
        /// not (RecordingAlignment::findSegments())
        std::vector<RecordingAlignment::Segment> segments;

        /// A file that could not be read
        QString error;

        bool cancelled = false;

        /// Whether a segment was looked for, so that one not found
        /// refuses the replacement: not one left as it is, too short to
        /// look for or not in the recording
        bool lookedFor(const RecordingAlignment::Segment &segment) const;
    };

    TakeRecordingSearch(QString takePath, const Coverage &coverage,
                        QString recordingPath, QWidget *parent);
    virtual ~TakeRecordingSearch();

    /// Show the dialog and start the search
    void start();

    const Result &result() const { return *m_result; }

signals:
    void finished();

private:
    struct Shared {
        Result result;
        std::atomic<int> percent { 0 };
        std::atomic<bool> cancelled { false };
    };
    std::shared_ptr<Shared> m_shared;
    const Result *m_result;
    QWidget *m_parent;
    QThread *m_thread;
    QPointer<QProgressDialog> m_dialog;
    QTimer *m_timer;

    void threadFinished();
};

#endif
