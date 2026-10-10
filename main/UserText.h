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

#ifndef TONY_USER_TEXT_H
#define TONY_USER_TEXT_H

#include <QString>

/**
 * Figures as the user reads them, wherever Tony shows them: the status
 * bar's take report, Replace Take Audio's report, Check Input Level.
 */
namespace UserText
{
    /**
     * A time on the reference's timeline: "1:02.5". Rounded to a tenth
     * of a second before the minutes are taken off, so that 59.97 s is
     * "1:00.0" and not "0:60.0". Under 0, a minus sign before it.
     */
    QString minutesAndSeconds(double seconds);

    /**
     * A level in dBFS, to a tenth, with a minus sign rather than a
     * hyphen: "−3.2 dBFS". What rounds to 0.0 has no sign.
     */
    QString dbfs(double db);
}

#endif
