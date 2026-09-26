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

#ifndef TONY_TAKE_DIFF_H
#define TONY_TAKE_DIFF_H

#include "Coverage.h"
#include "LatencyCheck.h"

#include "base/BaseTypes.h"
#include "base/Event.h"

#include <utility>
#include <vector>

/**
 * What a change did to a take, and what the joins it left look like:
 * the comparisons the development checks make on a real take (the
 * audio and events outside a punch-in, the join of two punch-ins
 * inside a held tone, and where its live dots lie among the
 * reference's sounds).  Each returns whether it passed and the numbers
 * behind the answer, so that a check can report them.
 *
 * Pure functions over sample buffers, event vectors and frames: no
 * model, no window and no device, so all of it is tested without them
 * (TestTakeDiff).  Samples are interleaved, as WavFileReader gives
 * them.  Frames are on the take's timeline, which is the reference's,
 * and ranges are [start, end) as everywhere else.
 */
namespace TakeDiff
{
    /**
     * Whether the audio of a take outside a range is what it was.
     */
    struct AudioDiff {
        /// Every sample outside the range is bit for bit what it was
        bool pass;

        /// The first frame outside the range where any channel
        /// differs, or -1 if none does
        sv::sv_frame_t firstDifference;

        /// How many frames outside the range differ, and the largest
        /// difference of a sample there, full scale being 1
        sv::sv_frame_t differences;
        double largestDifference;

        AudioDiff() : pass(false), firstDifference(-1), differences(0),
                      largestDifference(0) { }
    };

    /**
     * Compare a take's audio before and after a change that is to
     * touch only the range: a punch-in's placed range, or an erased
     * one.  Both buffers have the given number of channels.  A frame
     * past the end of either is silence, as a take is wherever nothing
     * was recorded: a splice past the end makes the file longer.
     *
     * Nothing outside the range is excused.  TakeAudio::splice() and
     * erase() crossfade over the first and last fade frames *inside*
     * the range, the range's own first and last frame included, and
     * copy every other frame from the old file (weightAt() in
     * TakeAudio.cpp).  Take files are 32-bit float, so the copy is
     * exact.
     */
    AudioDiff audioOutside(const float *before, sv::sv_frame_t beforeFrames,
                           const float *after, sv::sv_frame_t afterFrames,
                           int channels, const Coverage::Range &range);

    /**
     * How far either side of a changed range its pitch and notes may
     * change: a ranged analysis replaces what lies within 0.25 s of
     * the range it was asked for (docs/takes.md, "W"), and
     * manual-checklist item 10 allows the same.
     */
    constexpr double kEventMarginSeconds = 0.25;

    /**
     * What changed outside a window.  An event is outside when no
     * part of it lies in the window; a note that crosses the window's
     * edge counts as inside.
     */
    struct EventDiff {
        /// Nothing outside the window was added, removed or changed
        bool pass;

        /// Outside the window: events only the vector after the change
        /// has, events only the one before it had, and events at one
        /// frame that differ, as (before, after).  An event that moved
        /// is removed at one frame and added at the other; a note that
        /// grew into the window is removed
        sv::EventVector added;
        sv::EventVector removed;
        std::vector<std::pair<sv::Event, sv::Event>> changed;

        /// The first frame of any of those, or -1
        sv::sv_frame_t firstDifference;

        /// The window left out: the range widened by the margin
        Coverage::Range window;

        EventDiff() : pass(false), firstDifference(-1) { }
    };

    /**
     * Compare pitch events, or notes, before and after a change to
     * the range, outside the range widened by the margin either side.
     * Events are compared in full (frame, value, duration, level and
     * label): outside the window the merge leaves the very same
     * events.  The vectors need not be sorted.
     */
    EventDiff eventsOutside(const sv::EventVector &before,
                            const sv::EventVector &after,
                            const Coverage::Range &range,
                            sv::sv_samplerate_t rate,
                            double marginSeconds = kEventMarginSeconds);

    /// pYIN's step, in frames of the audio it analyses (Analyser.cpp):
    /// over a voiced stretch the pitch track has an event every hop
    constexpr sv::sv_frame_t kHopFrames = 256;

    /**
     * How far either side of a join the pitch track is looked at.  A
     * ranged analysis runs over its range widened by 0.5 s and merges
     * what lies within 0.25 s of it (docs/takes.md), so a join made
     * by a punch-in has the merge's seam 0.25 s before it; this takes
     * it in with room, and all that the run could reach.
     */
    constexpr double kPitchWindowSeconds = 0.5;

    /// The longest step between neighbouring pitch events that is not
    /// a hole, in hops.  Over a held tone pYIN stamps every hop, and
    /// the hole a bad merge once left was two hops
    constexpr int kMaxGapHops = 1;

    /**
     * Whether the pitch track runs through a join: no hole, no frame
     * twice, frames in order.
     */
    struct PitchJoin {
        /// No gap longer than the largest allowed, nothing doubled and
        /// nothing out of order, within the window
        bool pass;

        /// The pitch events within the window
        int events;

        /// The longest step within the window from one event to the
        /// next, in frames, and the frame it starts from.  A step that
        /// begins before the window or ends after it counts.  Where the
        /// track has no event beyond the window's edge, the edge
        /// itself counts as one, so a track that stops inside the
        /// window, or has nothing in it, shows a long gap
        sv::sv_frame_t largestGap;
        sv::sv_frame_t largestGapFrom;

        /// Frames within the window that hold more than one event, and
        /// the first of them, or -1
        int doubled;
        sv::sv_frame_t firstDoubled;

        /// Events within the window that come, in the order given,
        /// after one at a later frame; and the first of them, or -1
        int outOfOrder;
        sv::sv_frame_t firstOutOfOrder;

        /// The window looked at: [join - reach, join + reach)
        Coverage::Range window;

        PitchJoin() : pass(false), events(0), largestGap(0),
                      largestGapFrom(0), doubled(0), firstDoubled(-1),
                      outOfOrder(0), firstOutOfOrder(-1) { }
    };

    /**
     * Look at the pitch events around a join, in the order given (a
     * model gives them sorted).  Only the window is looked at: pYIN
     * itself stamps one frame twice 100 hops before the end of every
     * run, which a whole-file track keeps and a ranged merge drops.
     */
    PitchJoin pitchAcross(const sv::EventVector &pitch, sv::sv_frame_t join,
                          sv::sv_samplerate_t rate,
                          double windowSeconds = kPitchWindowSeconds,
                          int maxGapHops = kMaxGapHops);

    /**
     * How close to a join no note may begin or end: as far as the
     * pitch window, so that a note split at the merge's seam 0.25 s
     * from the join is caught.  The held tone a join is made in has to
     * reach further than this on both sides (the dev layout's reach
     * 1.5 s from their middle).
     */
    constexpr double kNoteClearanceSeconds = 0.5;

    /**
     * Whether one note runs through a join.
     */
    struct NoteJoin {
        /// Exactly one note holds the join frame, and no note begins
        /// or ends within the clearance of it
        bool pass;

        /// The notes that hold the join frame
        sv::EventVector spanning;

        /// The notes that begin or end within the clearance of the join
        sv::EventVector edgesNear;

        /// The onset or end of any note nearest the join, in frames
        /// from it (negative before it); 0 if there are no notes
        sv::sv_frame_t nearestEdge;

        NoteJoin() : pass(false), nearestEdge(0) { }
    };

    /**
     * Look at the notes around a join.  A note is [frame, frame +
     * duration): one that ends at the join does not hold it, and its
     * end is 0 frames from it.
     */
    NoteJoin notesAcross(const sv::EventVector &notes, sv::sv_frame_t join,
                         sv::sv_samplerate_t rate,
                         double clearanceSeconds = kNoteClearanceSeconds);

    /// Where a step at a join is looked for: first differences within
    /// this of the join ...
    constexpr double kStepSeconds = 0.002;

    /// ... against those of this much audio around it, centred on it
    constexpr double kStepContextSeconds = 0.05;

    /// The context's typical first difference is this quantile of
    /// their sizes.  For a steady tone that is within 0.03 dB of its
    /// largest, so a clean join in a tone reads about 0 dB; unlike a
    /// mean, a click or two in the context does not raise it
    constexpr double kTypicalQuantile = 0.95;

    /**
     * A join is a step when its largest first difference is this far
     * above the typical one.  Measured with this code's arithmetic: a
     * steady tone of 196 to 294 Hz reads 0.03 dB, and so does the
     * splice's crossfade into it at the opposite phase.  White noise reads
     * 3.6 dB typically and at most 6.7 dB in 200 trials (the largest
     * of 177 differences against the 95th percentile of 2205).  A hard
     * cut in a 220 Hz tone at a random phase reads the jump against
     * the tone's steepest step: 27 dB typically, up to 36 dB.  About
     * one such cut in ten reads under 10 dB: the two sides happened
     * to meet within three of the tone's own steps (a tenth of its
     * amplitude), which is hardly a click.  10 dB keeps noise well
     * clear and misses only those.
     */
    constexpr double kMaxStepDb = 10.0;

    /**
     * Whether the samples step at a join.
     */
    struct SampleStep {
        /// stepDb is within kMaxStepDb
        bool pass;

        /// The largest first difference within kStepSeconds of the
        /// join against the typical one over kStepContextSeconds, in
        /// dB, in the channel where that is largest.  0 dB when both
        /// are silent, 200 when only the typical one is
        double stepDb;

        /// The two first differences, full scale being 1, and the
        /// frame the largest leads into (it is x[f] - x[f - 1])
        double largest;
        double typical;
        sv::sv_frame_t largestAt;

        /// The channel they were read from
        int channel;

        SampleStep() : pass(false), stepDb(0), largest(0), typical(0),
                       largestAt(0), channel(0) { }
    };

    /**
     * Look for a step in the samples at a join.  Both stretches are
     * cut short at the ends of the samples; with no first difference
     * to read within kStepSeconds of the join (a join outside the
     * audio), it does not pass.
     * The context is read as it is: a join between two stretches of
     * very different level is read against the whole of it.
     */
    SampleStep stepAt(const float *samples, sv::sv_frame_t frames,
                      int channels, sv::sv_samplerate_t rate,
                      sv::sv_frame_t join);

    /**
     * Live dots against the reference they were sung to, on a loopback,
     * where what is sung is the reference itself (the dev checks' item
     * 3).
     *
     * A dot is drawn at the middle of the live tracker's window, but YIN
     * hears mostly the window's first half, so a dot comes up to half a
     * window after the sound that made it, and not before it: on the
     * loopback fake a tone's dots begin about 540 frames into it and end
     * up to 440 past it.  So a sound's dots lie from its start to half a
     * window past its end, give or take kDotHops of the tracker's hops;
     * and those on a tone are within kDotCents of its pitch.
     *
     * Two kinds of dot are counted apart, their pitch not judged.  The
     * end of each sweep, near 8 kHz, makes a dot or two at a subharmonic
     * just under the tracker's 1 kHz ceiling.  And a dot within one
     * window after a tone's start, or after the punch-in's, comes of a
     * window that straddles that start: on the fake it is on pitch, but
     * through a real speaker, room and microphone it wanders, 50 to 75
     * cents on the user's runs, at the same places on every driver.
     */
    constexpr int kDotHops = 1;
    constexpr double kDotCents = 50.0;

    /// Where a live dot lies among the reference's sounds
    enum class DotPlace {
        OnPitch,    ///< on a tone, within kDotCents of its pitch
        OffPitch,   ///< on a tone, further from its pitch
        AtOnset,    ///< within a window after a tone's or the punch-in's start
        OnSweep,    ///< on a sweep
        OnNothing   ///< on none of the reference's sounds
    };

    struct LiveDot {
        DotPlace place;

        /// The tone it lies on, or at the start of, and how far the dot
        /// is from its pitch in cents; 0 for a dot on no tone
        double toneHz;
        double cents;

        LiveDot() : place(DotPlace::OnNothing), toneHz(0), cents(0) { }
    };

    /**
     * How far a sound's dots reach, in seconds: before its start, and
     * past its end; and how long after a start they are not judged.
     * The tracker's frames, counted at the rate given: the reference's,
     * as the dots are placed on its timeline.
     */
    struct DotReach {
        double before;
        double after;
        double onset;
        DotReach() : before(0), after(0), onset(0) { }
    };
    DotReach dotReach(sv::sv_samplerate_t rate);

    /**
     * Where a dot, at the given seconds and pitch on the reference's
     * timeline, lies among the sounds of the layout, for a punch-in
     * starting at punchInStart seconds.  The reach of one sound never
     * meets the next's; a dot on a sweep near the punch-in's start is
     * on the sweep.
     */
    LiveDot placeLiveDot(const LatencyCheck::Layout &layout,
                         double punchInStart, double seconds, double hz);
}

#endif
