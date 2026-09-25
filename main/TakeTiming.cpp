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

#include "TakeTiming.h"

#include "LatencyUtils.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

using namespace sv;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("TakeTiming", text);
}

} // namespace

sv_frame_t
TakeTiming::recordedToReference(sv_frame_t recordedFrames) const
{
    if (rate <= 0 || recordRate <= 0 || recordRate == rate) {
        return recordedFrames;
    }
    return sv_frame_t(std::llround(double(recordedFrames) * rate / recordRate));
}

sv_frame_t
TakeTiming::referenceToRecorded(sv_frame_t referenceFrames) const
{
    if (rate <= 0 || recordRate <= 0 || recordRate == rate) {
        return referenceFrames;
    }
    return sv_frame_t(std::llround(double(referenceFrames) * recordRate / rate));
}

sv_frame_t
TakeTiming::preRollBefore(sv_frame_t position, sv_frame_t wanted)
{
    if (wanted <= 0 || position <= 0) return 0;
    return std::min(wanted, position);
}

sv_frame_t
TakeTiming::playbackStart() const
{
    sv_frame_t start = position - preRoll;
    return start > 0 ? start : 0;
}

bool
TakeTiming::havePunchOut() const
{
    return end > position;
}

sv_frame_t
TakeTiming::spliceOffset() const
{
    // Everything before this was recorded before the singer could have
    // heard the reference at P: the round trip, and the lead-in they
    // were listening to before it
    sv_frame_t offset = recordedToReference(latency) + preRoll;
    return offset > 0 ? offset : 0;
}

sv_frame_t
TakeTiming::spliceLength() const
{
    if (!havePunchOut()) return -1;
    return end - position;
}

sv_frame_t
TakeTiming::autoStopFrames() const
{
    if (!havePunchOut()) return 0;
    // Counted as the record target counts, in frames of the recording
    sv_samplerate_t deviceRate = (recordRate > 0 ? recordRate : rate);
    sv_frame_t margin = sv_frame_t(deviceRate * autoStopMarginSeconds());
    return referenceToRecorded(spliceOffset() + (end - position)) + margin;
}

bool
TakeTiming::shouldStopAt(sv_frame_t framesReceived) const
{
    if (!havePunchOut()) return false;
    return framesReceived >= autoStopFrames();
}

sv_frame_t
TakeTiming::liveFrameIntoTake(sv_frame_t recordedFrame) const
{
    // The same shift the splice applies to the audio, so that a dot sits
    // where the finished pitch track will put the sound it stands for
    return compensatedLiveFrame(recordedToReference(recordedFrame),
                                spliceOffset());
}

bool
TakeTiming::isInLeadIn(sv_frame_t framesReceived) const
{
    // Without a pre-roll there is no lead-in to wait through: what
    // little comes before the latency is not worth counting down
    return preRoll > 0 && recordedToReference(framesReceived) < spliceOffset();
}

int
TakeTiming::countdownSeconds(sv_frame_t framesReceived) const
{
    if (rate <= 0 || !isInLeadIn(framesReceived)) return 0;
    double seconds =
        double(spliceOffset() - recordedToReference(framesReceived)) / rate;
    return int(std::ceil(seconds));
}

QString
TakeTiming::countdownText(sv_frame_t framesReceived) const
{
    int seconds = countdownSeconds(framesReceived);
    if (seconds <= 0) return "";
    return tr("Recording in %1…").arg(seconds);
}

bool
TakeTiming::chooseRange(const Coverage::Ranges &ranges, sv_frame_t playhead,
                        Coverage::Range &chosen)
{
    if (ranges.empty()) return false;

    for (const Coverage::Range &range : ranges) {
        if (playhead >= range.start && playhead < range.end) {
            chosen = range;
            return true;
        }
    }

    chosen = ranges[0];
    return true;
}
