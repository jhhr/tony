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

#include "LiveDotsFeed.h"

#include <QEvent>

#include <algorithm>

#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
#include <time.h>
#endif

using namespace sv;

// CPU time this thread has used, in ms; negative where there is no way
// of asking
static double
threadTimeMs()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
        return double(ts.tv_sec) * 1000.0 + double(ts.tv_nsec) / 1.0e6;
    }
#endif
    return -1.0;
}

static double
elapsedMs(const QElapsedTimer &timer)
{
    return double(timer.nsecsElapsed()) / 1.0e6;
}

void
LiveDotsFeed::Times::add(double ms)
{
    ++count;
    totalMs += ms;
    maxMs = std::max(maxMs, ms);
}

LiveDotsFeed::LiveDotsFeed(int intervalMs) :
    m_running(false),
    m_threadTimeAtReport(-1.0)
{
    m_timer.setInterval(intervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
}

LiveDotsFeed::~LiveDotsFeed()
{
    stop();
}

void
LiveDotsFeed::start(Source source, Handler handler)
{
    stop();
    m_source = source;
    m_handler = handler;
    m_running = true;
    m_report.newestFrame = -1;
    startPeriod();
    m_sinceTick.invalidate();
    m_timer.start();
}

void
LiveDotsFeed::stop()
{
    m_timer.stop();
    m_running = false;
    m_source = {};
    m_handler = {};
    if (m_painted) m_painted->removeEventFilter(this);
    m_painted = nullptr;
}

void
LiveDotsFeed::timePaintsOf(QObject *widget)
{
    if (m_painted) m_painted->removeEventFilter(this);
    m_painted = widget;
    if (m_painted) m_painted->installEventFilter(this);
}

void
LiveDotsFeed::startPeriod()
{
    sv_frame_t newest = m_report.newestFrame;
    m_report = Report();
    m_report.newestFrame = newest;
    m_sinceReport.start();
    m_threadTimeAtReport = threadTimeMs();
}

void
LiveDotsFeed::tick()
{
    if (!m_running) return;

    // How long since the last look, which is the interval when the GUI
    // thread has the time, and longer when it has not
    if (m_sinceTick.isValid()) {
        m_report.intervals.add(elapsedMs(m_sinceTick));
    }
    m_sinceTick.start();

    Estimates batch = m_source();
    if (!batch.empty()) {
        QElapsedTimer timer;
        timer.start();
        m_handler(batch);
        m_report.batches.add(elapsedMs(timer));
        m_report.estimates += int(batch.size());
        m_report.newestFrame = batch.back().frame;
        // (the handler may have stopped the feed)
        if (!m_running) return;
    }

    if (m_reporter && m_sinceReport.elapsed() >= 1000) {
        m_report.seconds = elapsedMs(m_sinceReport) / 1000.0;
        double threadTime = threadTimeMs();
        if (threadTime >= 0.0 && m_threadTimeAtReport >= 0.0 &&
            m_report.seconds > 0.0) {
            m_report.guiThreadShare = (threadTime - m_threadTimeAtReport) /
                (m_report.seconds * 1000.0);
        }
        m_reporter(m_report);
        startPeriod();
    }
}

bool
LiveDotsFeed::eventFilter(QObject *object, QEvent *event)
{
    if (object != m_painted || event->type() != QEvent::Paint) {
        return false;
    }

    // Delivered here, because nothing comes after a paint event to say
    // when it is over
    QElapsedTimer timer;
    timer.start();
    object->event(event);
    m_report.paints.add(elapsedMs(timer));
    return true;
}

QString
LiveDotsFeed::describe(const Report &r)
{
    QString text = QString("%1 batches of %2 dots, %3 ms each (at most %4), "
                           "every %5 ms (at most %6); pane painted %7 times, "
                           "%8 ms each (at most %9)")
        .arg(r.batches.count)
        .arg(r.batches.count > 0 ? double(r.estimates) / r.batches.count : 0.0,
             0, 'f', 1)
        .arg(r.batches.averageMs(), 0, 'f', 2)
        .arg(r.batches.maxMs, 0, 'f', 1)
        .arg(r.intervals.averageMs(), 0, 'f', 0)
        .arg(r.intervals.maxMs, 0, 'f', 0)
        .arg(r.paints.count)
        .arg(r.paints.averageMs(), 0, 'f', 2)
        .arg(r.paints.maxMs, 0, 'f', 1);
    if (r.guiThreadShare >= 0.0) {
        text += QString("; GUI thread %1% of a core")
            .arg(r.guiThreadShare * 100.0, 0, 'f', 0);
    }
    return text;
}
