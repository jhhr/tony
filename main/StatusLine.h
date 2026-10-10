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

#ifndef TONY_STATUS_LINE_H
#define TONY_STATUS_LINE_H

#include <QObject>
#include <QString>
#include <QTimer>

/**
 * What has the status bar over what the views write there (the visible
 * range, playback's position, a take's recorded time, the note being
 * sung), first to last:
 *
 * - the countdown of a pre-roll's lead-in, while it runs, which the
 *   caller works out from the take and passes to text();
 * - a notice about the audio device, for noticeMs(): long enough to be
 *   read at the start of a take, whose time and notes take the status
 *   bar back after it;
 * - a held message, the last take's level, kept until something the
 *   user does replaces it: the next take, playback or a selection.
 *
 * Everything that writes the status bar asks text() first and writes
 * its own only when that is empty: the views write it often (a take's
 * time every 10 ms, playback's position every 20 ms), and anything
 * written between two of those would be gone before it could be read.
 * When a notice runs out, expired() says so, and what is under it is to
 * be shown again: nothing else may write the status bar for a while.
 */
class StatusLine : public QObject
{
    Q_OBJECT

public:
    /// How long a notice is shown for
    static constexpr int kNoticeMs = 8000;

    StatusLine(QObject *parent = nullptr);
    virtual ~StatusLine();

    void setNotice(QString text);
    QString notice() const { return m_notice; }

    void setHeld(QString text);
    void clearHeld();
    QString held() const { return m_held; }

    /// What belongs in the status bar now, given the countdown ("" when
    /// there is none); "" when the views may write it
    QString text(QString countdown) const;

    /// For the tests, which cannot wait 8 s
    void setNoticeMs(int ms) { m_noticeMs = ms; }
    int noticeMs() const { return m_noticeMs; }

signals:
    /// The notice ran out
    void expired();

private:
    QString m_notice;
    QString m_held;
    QTimer m_noticeTimer;
    int m_noticeMs;
};

#endif
