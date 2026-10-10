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

#include "StatusLine.h"

StatusLine::StatusLine(QObject *parent) :
    QObject(parent),
    m_noticeMs(kNoticeMs)
{
    m_noticeTimer.setSingleShot(true);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this]() {
        m_notice = "";
        emit expired();
    });
}

StatusLine::~StatusLine()
{
}

void
StatusLine::setNotice(QString text)
{
    m_notice = text;
    if (text == "") {
        m_noticeTimer.stop();
    } else {
        m_noticeTimer.start(m_noticeMs);
    }
}

void
StatusLine::setHeld(QString text)
{
    m_held = text;
}

void
StatusLine::clearHeld()
{
    m_held = "";
}

QString
StatusLine::text(QString countdown) const
{
    // The countdown is what the singer waits on; a notice's time runs
    // on under it
    if (countdown != "") return countdown;
    if (m_notice != "") return m_notice;
    return m_held;
}
