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

#include "UserText.h"

#include <QChar>
#include <QCoreApplication>

#include <cmath>
#include <cstdlib>

namespace UserText
{

QString
minutesAndSeconds(double seconds)
{
    // Tenths first: rounding the seconds after the minutes were taken
    // off made 59.97 s "0:60.0"
    const long long tenths = std::llround(std::fabs(seconds) * 10.0);
    const long long minutes = tenths / 600;
    const double rest = double(tenths - minutes * 600) / 10.0;
    const QString text = QString("%1:%2").arg(minutes)
        .arg(rest, 4, 'f', 1, QChar('0'));
    return (seconds < 0.0 && tenths > 0) ? QChar(0x2212) + text : text;
}

QString
dbfs(double db)
{
    QString number = QString::number(std::fabs(db), 'f', 1);
    if (db < -0.05) number = QChar(0x2212) + number;
    return QCoreApplication::translate("UserText", "%1 dBFS").arg(number);
}

}
