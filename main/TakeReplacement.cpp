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

#include "TakeReplacement.h"

#include "SingingTakes.h"
#include "TakeAudio.h"
#include "UserText.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

using namespace sv;

namespace TakeReplacement
{

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("TakeReplacement", text);
}

}

QString
describe(const RecordingAlignment::Segment &segment, double takeRate)
{
    using Left = RecordingAlignment::Match::Left;
    const RecordingAlignment::Match &m = segment.match;
    const QString when = tr("%1 to %2")
        .arg(UserText::minutesAndSeconds(double(segment.start) / takeRate),
             UserText::minutesAndSeconds(double(segment.end) / takeRate));
    if (!m.found) {
        switch (m.left) {
        case Left::TooShort:
            return tr("%1: too short to look for, left as it was").arg(when);
        case Left::ShortSession:
            return tr("%1: sung at another time, too short to look for, "
                      "left as it was").arg(when);
        case Left::OutsideRecording:
            return tr("%1: not in the recording, which began later or "
                      "ended sooner, left as it was").arg(when);
        case Left::No:
            break;
        }
        return tr("%1: not found: %2").arg(when, m.error.toHtmlEscaped());
    }

    QString line = tr("%1: at %2 in the recording, %3 alike")
        .arg(when, UserText::minutesAndSeconds
             ((double(segment.start) + m.offset) / takeRate))
        .arg(m.confidence, 0, 'f', 2);
    const double drift = m.drift(takeRate);
    if (m.endsMeasured &&
        std::fabs(drift) > RecordingAlignment::kDriftSeconds) {
        line += tr("; <b>its two ends lie %1 ms apart</b>: the "
                   "transmitter's clock and the receiver's differ. The "
                   "singing was not stretched to fit, and its ends are up "
                   "to %2 ms from where the take had them")
            .arg(std::fabs(drift) * 1000.0, 0, 'f', 1)
            .arg(std::fabs(drift) * 500.0, 0, 'f', 1);
    } else if (m.endsMeasured) {
        line += tr("; its two ends %1 ms apart")
            .arg(std::fabs(drift) * 1000.0, 0, 'f', 1);
    }
    const double gainDb = 20.0 * std::log10(std::max(m.gain, 1e-9));
    if (std::fabs(gainDb) >= 0.5) {
        line += tr("; brought %1 by %2 dB to the take's level")
            .arg(gainDb > 0 ? tr("up") : tr("down"))
            .arg(std::fabs(gainDb), 0, 'f', 1);
    }
    return line;
}

QString
replace(SingingTakes &takes,
        const std::vector<RecordingAlignment::Segment> &segments,
        QString recordingPath, double takeRate, double recordingRate,
        sv_frame_t recordingFrames, QString directory, Result &result)
{
    result = Result();

    // The stretches of the recording go in through files of their own,
    // as a recording does, which go again with the folder
    QTemporaryDir scratch(QDir(directory).filePath("replacing-XXXXXX"));
    if (!scratch.isValid()) {
        return tr("Could not make a folder to work in, in \"%1\"")
            .arg(directory);
    }

    const QString pathBefore = takes.getAudioPath();
    const Coverage coverageBefore = takes.getCoverage();
    QStringList written;
    bool any = false;
    for (size_t i = 0; i < segments.size(); ++i) {
        const RecordingAlignment::Segment &segment = segments[i];
        result.lines << describe(segment, takeRate);
        const RecordingAlignment::Match &m = segment.match;
        if (!m.found) continue;

        // The recording's frames that hold the stretch, within the
        // recording, brought to the take's level and converted to its
        // rate by the splice, which leaves off the front what they begin
        // early by
        const RecordingAlignment::Span span = RecordingAlignment::recordingSpan
            (segment.start, segment.end, m.offset, takeRate, recordingRate,
             recordingFrames);
        if (span.end <= span.start) continue;
        const QString part =
            scratch.filePath(QString("part-%1.wav").arg(int(i)));
        QString error = TakeAudio::extract(recordingPath, span.from,
                                           span.count, float(m.gain), part);
        Coverage::Range placed;
        const QString previous = takes.getAudioPath();
        if (error == "") {
            error = takes.spliceRecording(part, span.lead, span.start,
                                          span.end - span.start, directory,
                                          &placed, takeRate);
        }
        if (error != "") {
            takes.restoreTake(pathBefore, coverageBefore);
            for (const QString &path : written) takes.discardWritten(path);
            return error;
        }
        written << takes.getAudioPath();
        if (previous != pathBefore && takes.discardWritten(previous)) {
            written.removeAll(previous);
        }
        QFile::remove(part);

        result.whole = any ?
            Coverage::Range(std::min(result.whole.start, placed.start),
                            std::max(result.whole.end, placed.end)) : placed;
        any = true;
        ++result.replaced;
    }

    if (!any) {
        return tr("Nothing in the take could be replaced: every stretch "
                  "of it was too short to look for, or not in the "
                  "recording");
    }
    return "";
}

}
