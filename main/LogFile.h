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

#ifndef TONY_LOG_FILE_H
#define TONY_LOG_FILE_H

#include <QByteArray>
#include <QFile>
#include <QString>

/**
 * A log file that stays small. On Android Tony's output goes to the
 * system log, which no one reads without a computer and adb; a copy
 * kept here can be saved from Tony (Help > Save Log...) and sent.
 *
 * When opened, the file an earlier run left becomes <path>.1, in place
 * of an older one, and whenever the file grows past limit bytes it goes
 * the same way and a new one is begun: the two hold the last limit to
 * twice limit bytes, the end of the run before included when this one's
 * is short. One thread writes; it is not locked.
 */
class LogFile
{
public:
    LogFile(QString path, qint64 limit);

    // Moves an earlier run's file aside and begins a new one; false if
    // it cannot be written
    bool open();

    bool isOpen() const { return m_file.isOpen(); }

    // Appends line and a newline, at once, in case Tony ends soon after
    void write(const QByteArray &line);

    // <path>.1, the older part
    static QString olderPath(QString path);

    // The older part and the newer, in that order: the log to be sent
    static QByteArray contents(QString path);

private:
    QString m_path;
    qint64 m_limit;
    QFile m_file;

    bool begin();
};

#endif
