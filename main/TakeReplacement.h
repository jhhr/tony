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

#ifndef TONY_TAKE_REPLACEMENT_H
#define TONY_TAKE_REPLACEMENT_H

#include "Coverage.h"
#include "RecordingAlignment.h"

#include "base/BaseTypes.h"

#include <QString>
#include <QStringList>

#include <vector>

class SingingTakes;

/**
 * Takes > Replace Take Audio from Recording, once its search has found
 * where the take's stretches are in the recording: each found stretch's
 * span of the recording put into the active take in place of what it
 * has, as a recording goes in (docs/takes.md). No window: the take's
 * files and the recording's, and the report's words.
 */
namespace TakeReplacement
{
    struct Result {
        /// From the first stretch replaced to the last, as placed
        Coverage::Range whole;
        int replaced = 0;

        /// A line for the report for each segment, in order, in HTML
        QStringList lines;
    };

    /**
     * For each segment found: its span of the recording
     * (RecordingAlignment::recordingSpan()) written to a file of its own
     * in a folder beside the take's files, brought to the take's level by
     * the match's gain, and spliced into the active take of takes at
     * the take's rate, with SingingTakes::spliceRecording()'s fades. Each
     * take file written on the way goes as soon as the next is written:
     * undo knows the take before and after, nothing in between.
     *
     * On success returns "" and fills result; the take's coverage is as
     * it was. Otherwise the take is as it was, every file this wrote is
     * gone, and the return is a message for the user.
     */
    QString replace(SingingTakes &takes,
                    const std::vector<RecordingAlignment::Segment> &segments,
                    QString recordingPath, double takeRate,
                    double recordingRate, sv::sv_frame_t recordingFrames,
                    QString directory, Result &result);

    /// The report's line for a segment, whether it was found or not
    QString describe(const RecordingAlignment::Segment &segment,
                     double takeRate);
}

#endif
