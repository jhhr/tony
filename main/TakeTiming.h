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

#ifndef TONY_TAKE_TIMING_H
#define TONY_TAKE_TIMING_H

#include "Coverage.h"

#include "base/BaseTypes.h"

#include <QString>

/**
 * The frame arithmetic of one singing take: where the reference is
 * played from, which part of the recording is kept, when the take
 * stops by itself, and where a live dot belongs.
 *
 * P (position) is where the take's new material goes on the
 * reference's timeline; E (end) is where it stops if the singer is
 * recording into a selection; R (preRoll) is the lead-in that is
 * heard but not recorded over; L (latency) is the round trip, as
 * LatencyUtils.h describes it.  The device records from the press of
 * Record whatever else happens: pre-roll and punch-out only change
 * which part of the recording is used and when it stops.
 *
 * The device need not run at the reference's rate (phones run at
 * 48 kHz, the reference is always a 44.1 kHz model). P, E and R, and
 * anything placed on the reference's timeline, are frames at rate; L
 * and anything counted off the record target are frames of the
 * recording, at recordRate. The recording is converted to the
 * reference's rate before it is spliced, so the splice counts in the
 * reference's frames.
 *
 * Nothing here touches a model, a window or the settings, so all of
 * it is tested without either (TestTakeTiming): MainWindow fills the
 * fields in and does as the answers say.
 */
struct TakeTiming
{
    /// Of the reference, and of the take's audio
    sv::sv_samplerate_t rate;

    /// Of the recording: the device's. 0 means the same as rate
    sv::sv_samplerate_t recordRate;

    /// P: where the material recorded from now on is to land
    sv::sv_frame_t position;

    /// E: where the take stops by itself, or -1 for "when Stop is pressed"
    sv::sv_frame_t end;

    /// R: the lead-in played before P, never taking S below frame 0
    sv::sv_frame_t preRoll;

    /// L: what was recorded before the singer could hear the reference
    /// at S, in frames of the recording
    sv::sv_frame_t latency;

    TakeTiming() :
        rate(0), recordRate(0), position(0), end(-1), preRoll(0),
        latency(0) { }

    /// Frames of the recording as frames of the reference's timeline
    sv::sv_frame_t recordedToReference(sv::sv_frame_t recordedFrames) const;

    /// Frames of the reference's timeline as frames of the recording
    sv::sv_frame_t referenceToRecorded(sv::sv_frame_t referenceFrames) const;

    /**
     * The lead-in there is room for before position: the pre-roll
     * asked for, shortened near the start of the song and nothing at
     * frame 0, since a take cannot start before the song does.
     */
    static sv::sv_frame_t preRollBefore(sv::sv_frame_t position,
                                        sv::sv_frame_t wanted);

    /**
     * How much singing past E is recorded before the take stops
     * itself.  The latency can still be refined while the take runs,
     * and the timer that watches for the end only polls now and then,
     * so the margin is what makes sure everything up to E is there.
     * What is recorded past E is never used.
     */
    static double autoStopMarginSeconds() { return 0.25; }

    /// S: where playback starts, so the reference reaches P after R frames
    sv::sv_frame_t playbackStart() const;

    /// The take has an end to stop itself at
    bool havePunchOut() const;

    /**
     * The frame of the recording that the take's new material starts
     * at, once the recording is at the reference's rate
     */
    sv::sv_frame_t spliceOffset() const;

    /// How much of the recording to use, or -1 for all there is of it
    sv::sv_frame_t spliceLength() const;

    /**
     * How many frames the record target must have received for the
     * take to hold everything up to E, margin included.  0 when there
     * is no punch-out to stop at.
     */
    sv::sv_frame_t autoStopFrames() const;

    /// The take has everything it was asked for and can stop now
    bool shouldStopAt(sv::sv_frame_t framesReceived) const;

    /**
     * Where sound found at this frame of the recording belongs,
     * counted in the reference's frames from P.  Negative means it was
     * sung during the lead-in, before the take's own material begins,
     * and has no place on the reference's timeline.
     */
    sv::sv_frame_t liveFrameIntoTake(sv::sv_frame_t recordedFrame) const;

    /// The lead-in is still running: nothing recorded so far counts
    bool isInLeadIn(sv::sv_frame_t framesReceived) const;

    /// Seconds of the lead-in left, rounded up; 0 once it is over
    int countdownSeconds(sv::sv_frame_t framesReceived) const;

    /// What to show while the lead-in runs; empty once it is over
    QString countdownText(sv::sv_frame_t framesReceived) const;

    /**
     * The range to record into out of those selected: the one that
     * holds the playhead, else the first.  The ranges must be in
     * order.  False, leaving chosen alone, if there are none.
     */
    static bool chooseRange(const Coverage::Ranges &ranges,
                            sv::sv_frame_t playhead,
                            Coverage::Range &chosen);
};

#endif
