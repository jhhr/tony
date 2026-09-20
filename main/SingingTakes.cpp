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

#include "SingingTakes.h"

#include "TakeAudio.h"

#include <QDateTime>
#include <QDir>
#include <QSettings>

using namespace sv;

SingingTakes::SingingTakes(QObject *parent) :
    QObject(parent)
{
}

SingingTakes::~SingingTakes()
{
}

void
SingingTakes::clear()
{
    m_audioPath = "";
    m_coverage.clear();
    m_superseded.clear();
}

void
SingingTakes::setTake(QString path, const Coverage &coverage)
{
    if (m_audioPath != "" && m_audioPath != path) {
        m_superseded.push_back(m_audioPath);
    }
    m_audioPath = path;
    m_coverage = coverage;
}

void
SingingTakes::setWholeFileTake(QString path, sv_frame_t frames)
{
    Coverage whole;
    whole.add(0, frames);
    setTake(path, whole);
}

QString
SingingTakes::spliceRecording(QString recordingPath,
                              sv_frame_t recordingOffset,
                              sv_frame_t position,
                              sv_frame_t length,
                              QString directory,
                              Coverage::Range *placed)
{
    QString outPath = nextAudioPath(directory);
    if (outPath == "") {
        return tr("Could not find a name to write the singing track under, "
                  "in \"%1\"").arg(directory);
    }

    Coverage::Range range;
    QString error = TakeAudio::splice(m_audioPath, recordingPath,
                                      recordingOffset, position, length,
                                      outPath, &range);
    if (error != "") return error;

    if (m_audioPath != "") m_superseded.push_back(m_audioPath);
    m_audioPath = outPath;
    m_coverage.add(range.start, range.end);

    if (placed) *placed = range;
    return "";
}

bool
SingingTakes::coversPosition(sv_frame_t position) const
{
    return m_coverage.contains(position);
}

bool
SingingTakes::shouldConfirmRecordingAt(sv_frame_t position) const
{
    return coversPosition(position) && isOverwriteConfirmationWanted();
}

bool
SingingTakes::isOverwriteConfirmationWanted()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    bool wanted = settings.value("confirmrecordover", true).toBool();
    settings.endGroup();
    return wanted;
}

void
SingingTakes::setOverwriteConfirmationWanted(bool wanted)
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("confirmrecordover", wanted);
    settings.endGroup();
}

QString
SingingTakes::nextAudioPath(QString directory)
{
    QDir dir(directory);

    // The time of day is enough to tell one from the next within a
    // session; the counter is there because it need only be enough.
    // ":" is not allowed in a file name on Windows
    QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");

    for (int i = 0; i < 1000; ++i) {
        QString name = (i == 0 ?
                        QString("take-%1.wav").arg(stamp) :
                        QString("take-%1-%2.wav").arg(stamp).arg(i));
        if (!dir.exists(name)) return dir.filePath(name);
    }

    return "";
}
