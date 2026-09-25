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

#ifndef TONY_LATENCY_CHECK_H
#define TONY_LATENCY_CHECK_H

#include "base/BaseTypes.h"

#include <vector>

/**
 * The test reference that Calibrate Audio plays, and the finder that
 * locates its sweeps in what the microphone recorded.
 *
 * The reference is a row of events: a short sweep, which is what the
 * finder looks for, then a tone that pYIN can track, then silence.  A
 * take recorded against it through a loopback (an earcup held to the
 * mic) holds every sweep where the take path put it, so how far a
 * sweep is found from where the reference has it is how far the take
 * was misplaced.
 *
 * Pure functions over sample buffers: no model, no window and no
 * device, so all of it is tested without them (TestLatencyCheck).
 * Judging a take from its events is a later step.
 */
namespace LatencyCheck
{
    /// The session's rate, which the reference is made at unless asked
    constexpr sv::sv_samplerate_t kReferenceRate = 44100;

    /**
     * The sweep: linear from 1 to 8 kHz.  Linear rather than
     * exponential for two reasons.  Its spectrum is flat over the
     * band, so its matched filter gives the narrowest, most symmetric
     * peak that band allows.  And the harmonics a small speaker adds
     * to a linear sweep rise at a different rate from it, so they do
     * not match it; those of an exponential sweep match the sweep
     * itself shifted earlier (by 67 ms for the second harmonic and
     * 106 ms for the third, here), which is just where the
     * earliest-peak rule below looks.
     */
    constexpr double kSweepStartHz = 1000.0;
    constexpr double kSweepEndHz = 8000.0;
    constexpr double kSweepSeconds = 0.2;

    /// Raised-cosine fades at both ends of every sweep and tone, so
    /// that no edge is a click
    constexpr double kFadeSeconds = 0.01;

    /// The peak of every sweep and tone: well clear of room noise, and
    /// no louder than that, since the earcups are held near the ears
    constexpr double kPeakDbfs = -12.0;

    /// Silence between a sweep and its tone, so that the tone begins
    /// as a note of its own
    constexpr double kPauseSeconds = 0.1;

    /// How long the tone after a sweep lasts; the dev layout's held
    /// tones are long enough for two punch-ins to meet inside one
    constexpr double kToneSeconds = 0.8;
    constexpr double kHeldToneSeconds = 3.0;

    /**
     * The finder's thresholds.  Starting values: the report of every
     * real run gives the numbers they are to be tuned from.
     */

    /// How far either side of the expected time a sweep is looked
    /// for.  Less than half the smallest spacing between events, so a
    /// neighbour's sweep cannot be taken for this one
    constexpr double kSearchSeconds = 0.8;

    /// A peak this much below the largest can still be the direct
    /// sound, the largest being a reflection of it
    constexpr double kEarliestPeakDb = 6.0;

    /// An earlier peak counts as an arrival of its own only if it is
    /// at least this far before the largest; see findSweep()
    constexpr double kSeparateArrivalSeconds = 0.001;

    /// The sweep is found when its peak stands this far above the
    /// median of the window ...
    constexpr double kMinPeakOverMedianDb = 15.0;

    /// ... and this far above anything in the window more than
    /// kSecondPeakSeconds away from it
    constexpr double kMinPeakOverSecondDb = 6.0;
    constexpr double kSecondPeakSeconds = 0.01;

    /**
     * One event of the reference, in frames of its layout's rate: a
     * sweep, a pause, a tone, then silence until the next event.
     */
    struct Event {
        sv::sv_frame_t sweepStart;
        sv::sv_frame_t toneStart;
        sv::sv_frame_t toneLength;
        double toneHz;

        Event() : sweepStart(0), toneStart(0), toneLength(0), toneHz(0) { }
    };

    /**
     * A reference's length and events, at a rate.  The events are in
     * order, and each ends before the next begins.
     */
    struct Layout {
        sv::sv_samplerate_t rate;
        sv::sv_frame_t length;
        std::vector<Event> events;

        Layout() : rate(kReferenceRate), length(0) { }
    };

    /**
     * The fixed layouts.  The events are at irregular spacings (sweep
     * to sweep) between 1.6 and 2.6 s, each spacing used once, so that
     * no stretch of the reference matches another one some events
     * along: a take misplaced by whole events cannot look right.  The
     * spacing is also what keeps the finder off a neighbour's sweep
     * (kSearchSeconds).  The tones take turns at 196, 220.5, 245 and
     * 294 Hz: a whole number of samples per period at 44.1 kHz, which
     * pYIN needs to report the pitch rather than a subharmonic
     * (docs/testing.md).
     *
     * All three give their events the same times in seconds at any
     * rate, so a reference made at another rate is the same reference.
     */

    /// 26 s: twelve events, for the four punch-ins of a calibration
    Layout calibrationLayout(sv::sv_samplerate_t rate = kReferenceRate);

    /// 40 s: the calibration events, then three with held tones.  The
    /// spacings into and between those are longer than any calibration
    /// spacing, so all still differ, and a held tone's event (3.3 s)
    /// ends before the next sweep
    Layout devLayout(sv::sv_samplerate_t rate = kReferenceRate);

    /// 4 minutes of events at the calibration spacings, over and over.
    /// No more than eleven spacings can differ by 0.1 s inside 1.6 to
    /// 2.6 s, so here any eleven in a row do
    Layout longLayout(sv::sv_samplerate_t rate = kReferenceRate);

    /// One sweep at the given rate, as the reference has it
    std::vector<float> sweep(sv::sv_samplerate_t rate);

    /// The reference: the layout's events, silence everywhere else
    std::vector<float> generate(const Layout &layout);

    /**
     * Where a sweep was found in a take, and how sure the finder is.
     * The error and the levels describe the best candidate even when
     * it was not found; they mean something only when it was.
     */
    struct Arrival {
        /// Both confidences cleared their thresholds
        bool found;

        /// Where the sweep arrived minus where it was expected, in
        /// whole frames of the take counted from the frame nearest the
        /// expected time.  Positive is late
        sv::sv_frame_t errorFrames;

        /// The same, in seconds, from the expected time itself
        double errorSeconds;

        /// The two confidences, in dB
        double peakOverMedianDb;
        double peakOverSecondDb;

        /// How loud the sweep arrived against how it was generated:
        /// 0 dB if the take holds the reference's sweep unchanged
        double levelDb;

        /// The largest sample of the take in the stretch searched,
        /// full scale being 1
        double inputPeak;

        Arrival() : found(false), errorFrames(0), errorSeconds(0),
                    peakOverMedianDb(0), peakOverSecondDb(0),
                    levelDb(0), inputPeak(0) { }
    };

    /**
     * Look for the reference's sweep in a take, within kSearchSeconds
     * of where it is expected; the window is cut short at the ends of
     * the take.  The take's samples are at the given rate, which need
     * not be the reference's: the expected time is in seconds, and
     * the sweep is matched at the take's own rate.
     */
    Arrival findSweep(const float *take, sv::sv_frame_t count,
                      sv::sv_samplerate_t rate, double expectedSeconds);
}

#endif
