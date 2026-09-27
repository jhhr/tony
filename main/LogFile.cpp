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

#include "LogFile.h"

#include <QDir>
#include <QFileInfo>

LogFile::LogFile(QString path, qint64 limit) :
    m_path(path),
    m_limit(limit)
{
}

QString
LogFile::olderPath(QString path)
{
    return path + ".1";
}

bool
LogFile::begin()
{
    // What is there now becomes the older part, the older part goes
    if (QFileInfo::exists(m_path)) {
        QFile::remove(olderPath(m_path));
        QFile::rename(m_path, olderPath(m_path));
    }

    m_file.setFileName(m_path);
    return m_file.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

bool
LogFile::open()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    return begin();
}

void
LogFile::write(const QByteArray &line)
{
    if (!m_file.isOpen()) return;

    m_file.write(line);
    m_file.write("\n", 1);
    m_file.flush();

    if (m_file.size() >= m_limit) {
        m_file.close();
        begin();
    }
}

QByteArray
LogFile::contents(QString path)
{
    QByteArray all;
    for (QString part : { olderPath(path), path }) {
        QFile file(part);
        if (file.open(QIODevice::ReadOnly)) {
            all += file.readAll();
        }
    }
    return all;
}
