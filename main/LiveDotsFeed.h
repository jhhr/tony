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

#ifndef TONY_LIVE_DOTS_FEED_H
#define TONY_LIVE_DOTS_FEED_H

#include "RealtimePitchTracker.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <functional>

/**
 * Hands the live tracker's pitch estimates to the GUI thread in
 * batches: everything found since the last batch, once an interval.
 *
 * The tracker finds about 170 estimates a second. Handed over one at a
 * time, each is a call queued on the GUI thread, and a GUI thread that
 * needs longer for one than the tracker takes to find the next (5.8 ms)
 * falls behind for good: the dots trail the singing further and further
 * for as long as the take lasts. A phone's GUI thread is several times
 * slower than a desktop's. Taken in batches, a slower thread takes
 * bigger batches, later, and nothing queues up. The interval is also
 * how often the pane is told of new dots, each time a redraw of it.
 *
 * Once a second it reports what the batches, and the paints of the
 * pane the dots are in, have cost the GUI thread: for the log, since
 * whether the dots keep up on a phone cannot be seen from the desktop.
 *
 * Lives on the GUI thread, as the handler does.
 */
class LiveDotsFeed : public QObject
{
public:
    typedef RealtimePitchTracker::Estimates Estimates;
    typedef std::function<Estimates()> Source;
    typedef std::function<void(const Estimates &)> Handler;

    explicit LiveDotsFeed(int intervalMs);
    ~LiveDotsFeed();

    /// Take what the source has found once an interval, and give it to
    /// the handler, if there is anything
    void start(Source source, Handler handler);

    /// No more batches or reports, and no more paints timed. What the
    /// source still holds is left with it
    void stop();

    bool isRunning() const { return m_running; }

    /// Time the paint events of this widget (the pane the dots are in)
    /// for the reports, until stop(). The feed delivers them to the
    /// widget itself, to see when they are over: event filters the
    /// widget had before do not see its paint events meanwhile
    void timePaintsOf(QObject *widget);

    struct Times {
        int count = 0;
        double totalMs = 0.0;
        double maxMs = 0.0;
        void add(double ms);
        double averageMs() const { return count > 0 ? totalMs / count : 0.0; }
    };

    struct Report {
        double seconds = 0.0;   ///< since the last report
        int estimates = 0;      ///< handed over since then
        /// Centre frame of the newest estimate handed over (not only
        /// since the last report), or -1
        sv::sv_frame_t newestFrame = -1;
        Times batches;          ///< the handler's time, each batch
        Times intervals;        ///< between one look at the source and the next
        Times paints;           ///< the widget's paint events
        /// Of one core, over the period; negative where unknown
        double guiThreadShare = -1.0;
    };

    /// Called with what happened since the last report, once a second
    /// while running
    void setReporter(std::function<void(const Report &)> reporter) {
        m_reporter = reporter;
    }

    /// The costs of a report, for a log line
    static QString describe(const Report &report);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void tick();
    void startPeriod();

    QTimer m_timer;
    bool m_running;
    Source m_source;
    Handler m_handler;
    std::function<void(const Report &)> m_reporter;
    QPointer<QObject> m_painted;

    Report m_report;
    QElapsedTimer m_sinceTick;
    QElapsedTimer m_sinceReport;
    double m_threadTimeAtReport;
};

#endif
