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
 * judgeTake() gathers the finder's results over a take's punch-ins
 * into a verdict, and calibratedRoundTrip() turns the offset it
 * measured into the round trip to use instead.
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

    /// How long the long layout is unless asked otherwise: a song
    constexpr double kLongSeconds = 240.0;

    /// 4 minutes, or the length given, of events at the calibration
    /// spacings, over and over, as many as end in time for the silence
    /// the calibration layout ends with.  No more than eleven spacings
    /// can differ by 0.1 s inside 1.6 to 2.6 s, so here any eleven in a
    /// row do.  A shorter one is the start of the 4-minute one
    Layout longLayout(sv::sv_samplerate_t rate = kReferenceRate,
                      double seconds = kLongSeconds);

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

        /// The second peak, which peakOverSecondDb measures against:
        /// how long after the chosen peak it comes (negative if
        /// before), and its level against the chosen peak's, in dB.
        /// A monitoring echo is a second peak at the same delay after
        /// every sweep.  With nothing more than kSecondPeakSeconds from
        /// the chosen peak, the delay is 0
        double secondDelaySeconds;
        double secondLevelDb;

        Arrival() : found(false), errorFrames(0), errorSeconds(0),
                    peakOverMedianDb(0), peakOverSecondDb(0),
                    levelDb(0), inputPeak(0),
                    secondDelaySeconds(0), secondLevelDb(0) { }
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

    /**
     * Judging a take.  An offset is where a sweep was found minus
     * where the reference has it (Arrival::errorSeconds): positive is
     * late.  Starting values like the finder's, to be tuned from the
     * reports of real runs.
     */

    /// An event is judged in a punch-in only when all that the finder
    /// reads for it (its window, and a sweep's length past the window's
    /// end) lies inside the punch-in's range and this far from its
    /// ends.  The splice cuts the punch-in's audio at the range ends and
    /// crossfades it over 5 ms into what was there, so a sweep near an
    /// end may be cut, which says nothing about the audio path
    constexpr double kJudgeMarginSeconds = 0.05;

    /// NoSignal: fewer than this share of the judged events were found
    constexpr double kMinFoundShare = 2.0 / 3.0;

    /// Clipped: the input reached this close to full scale somewhere
    /// in the punch-ins.  Not at full scale exactly: a device's integer
    /// samples, converted, stop a little short of it
    constexpr double kClippedDbfs = -0.2;

    /// Unsteady: the offsets found disagree by more than this, across
    /// punch-ins or within one ...
    constexpr double kSteadySeconds = 0.005;

    /// ... Scattered: by more than this
    constexpr double kScatteredSeconds = 0.015;

    /// PositionDependent: the punch-ins' offsets lie on a line over
    /// their positions, steeper than this (0.5 %: a 44.1 kHz reference
    /// recorded at 48 kHz and placed frame for frame is 8 %), and
    /// leave less than kSteadySeconds about it ...
    constexpr double kPositionSlope = 0.005;

    /// ... fitted over this many punch-ins at least, since a line
    /// through two always fits
    constexpr int kMinPunchInsForSlope = 3;

    /// Fading: the sweeps' level (Arrival::levelDb) over the judged
    /// events, in the order they were recorded, fell by this much from
    /// the first half to the second, each half taken by its median.
    /// Through a steady path every sweep arrives at the same level,
    /// within 0.5 dB even in noise at -10 dB SNR.  An echo canceller
    /// or noise suppressor taking the sweeps out brings them down
    /// toward the noise, and gain control by whatever it corrects; 10
    /// dB leaves room for an earcup that shifts a little
    constexpr double kFadingDb = 10.0;

    /// ... judged only from this many events on, so that each half has
    /// three and one missing event cannot move its median
    constexpr int kMinFadingEvents = 6;

    /**
     * A monitoring echo: the take's own input played back out and heard
     * again.  It is the second peak (Arrival::secondDelaySeconds), at
     * the same delay within kEchoToleranceSeconds, in more than half of
     * the events whose sweep was heard, and at least kMinEchoEvents of
     * them.  Heard, not found: an echo within kMinPeakOverSecondDb of
     * the direct sound leaves no sweep found, and should still be named.
     *
     * Other things come at a fixed delay after every sweep too, so an
     * echo has to be at least kEchoMinDelaySeconds late and at most
     * kEchoMaxBelowDb down.  A reflection a little closer than
     * kSecondPeakSeconds leaves the tail of its peak just past that, 9
     * to 23 dB down (measured for one 5.5 dB stronger than the direct
     * sound, 9 ms after it, from a 1-2 kHz path to the full band),
     * which is why the delay.  The matched filter's own tail is over
     * 80 dB down, and noise peaks come as loud as 26 dB down at 0 dB
     * SNR but never agree on a delay.
     *
     * The Windows audio engine works in 10 ms periods, so what it
     * monitors comes back two periods late at least: one to capture
     * it and one to play it.  An echo closer than that, such as an
     * interface's direct monitor, is not seen, nor one quieter than
     * the tail of a close reflection.
     */
    constexpr double kEchoMinDelaySeconds = 0.02;
    constexpr double kEchoMaxBelowDb = 30.0;
    constexpr double kEchoToleranceSeconds = 0.003;
    constexpr int kMinEchoEvents = 3;

    /**
     * What a take says about the audio path.  In this order of
     * precedence: when several apply, the first is the verdict, and
     * names what to fix first.  Without the sweeps nothing else can be
     * judged; a clipped or processed input can move where they are
     * found; and a rate that misplaces punch-ins also scatters them.
     */
    enum class Verdict {
        NoSignal,           ///< too few sweeps found
        Clipped,            ///< the input reached full scale
        Fading,             ///< the sweeps' level fell over the run
        PositionDependent,  ///< offset grows with position: a rate
                            ///< other than the reference's
        Scattered,          ///< offsets disagree by more than 15 ms
        Unsteady,           ///< offsets disagree by 5 to 15 ms
        Ok
    };

    /// The verdict's name as the enum spells it, for logs
    const char *verdictName(Verdict verdict);

    /// A range of the timeline that one punch-in recorded, in seconds
    struct PunchIn {
        double start;
        double end;

        PunchIn() : start(0), end(0) { }
        PunchIn(double s, double e) : start(s), end(e) { }
    };

    /// One judged event: which of the layout's, in which punch-in, and
    /// what the finder made of it
    struct EventResult {
        int event;
        int punchIn;
        double expectedSeconds;
        Arrival arrival;

        EventResult() : event(0), punchIn(0), expectedSeconds(0) { }
    };

    /// One punch-in's figures.  The offsets are those of its found
    /// events, in seconds; with none found, both are 0
    struct PunchInResult {
        PunchIn range;
        int judged;
        int found;
        double medianOffset;
        double spread;          ///< largest offset minus smallest

        PunchInResult() : judged(0), found(0), medianOffset(0), spread(0) { }
    };

    /// A monitoring echo, if one was heard; see kEchoMaxBelowDb
    struct Echo {
        bool heard;
        double delaySeconds;    ///< after the direct sound, median
        double levelDb;         ///< against the direct sound, median
        int events;             ///< how many events it was heard in

        Echo() : heard(false), delaySeconds(0), levelDb(0), events(0) { }
    };

    /// What judgeTake() made of a take.  Offsets in seconds
    struct TakeSummary {
        Verdict verdict;

        /// Every verdict that applied, in order of precedence; empty
        /// for Ok
        std::vector<Verdict> flags;

        std::vector<PunchInResult> punchIns;    ///< as given
        std::vector<EventResult> events;        ///< judged, as recorded

        int judged;
        int found;

        /// Across punch-ins, over those with an event found: the
        /// median of their median offsets, which weighs every take
        /// alike, and the largest minus the smallest of them
        double medianOffset;
        double spread;

        /// The line fitted to those medians over the punch-ins' start
        /// times: seconds of offset per second of position, and what
        /// it leaves, as largest minus smallest.  0 and the spread
        /// itself below two punch-ins
        double slope;
        double slopeResidual;

        /// The largest sample in the punch-ins' ranges, full scale 1
        double inputPeak;

        /// How far the sweeps' level fell, first half to second, in
        /// dB; 0 below kMinFadingEvents judged events
        double fadingDb;

        Echo echo;

        bool flagged(Verdict v) const;

        TakeSummary() : verdict(Verdict::NoSignal), judged(0), found(0),
                        medianOffset(0), spread(0), slope(0),
                        slopeResidual(0), inputPeak(0), fadingDb(0) { }
    };

    /**
     * Judge a take recorded against the reference made from this
     * layout.
     *
     * The take is one channel of samples, as its model gives them, at
     * the take's own rate, which need not be the layout's.  The punch-
     * ins are the timeline ranges they recorded, in seconds, in the
     * order they were recorded; where a later one overlaps an earlier
     * one, the take holds the later one's audio there.  So an event is
     * judged in a punch-in that holds all the finder reads for it
     * (kJudgeMarginSeconds) and where no later one overlaps that, and
     * in no other.  Ranges are cut short at the end of the take.
     *
     * NoSignal applies when fewer than kMinFoundShare of the judged
     * events were found, and also when none was, judged or not: there
     * is then nothing to measure.  Unsteady and Scattered look at the
     * spread across punch-ins and within each, whichever is larger.
     */
    TakeSummary judgeTake(const Layout &layout,
                          const float *take, sv::sv_frame_t count,
                          sv::sv_samplerate_t rate,
                          const std::vector<PunchIn> &punchIns);

    /// punchInsFor() leaves this much more room around what the finder
    /// reads than judgeTake() asks for, so that a range rounded to whole
    /// frames, at any rate, still holds all of it
    constexpr double kPunchInSlackSeconds = 0.01;

    /**
     * The ranges of a run of punch-ins against the reference made from
     * this layout: count of them, one after another along the timeline
     * and not overlapping, each holding eventsEach consecutive events
     * that judgeTake() judges in it, and no other.
     *
     * An event is judged only where all that the finder reads for it
     * lies inside one range, so where one range ends and the next
     * begins, that much of the spacing between two events is lost (1.9
     * s): an event whose reading would begin before the previous range
     * ends is left out.  Empty if the layout has no room for them all.
     */
    std::vector<PunchIn> punchInsFor(const Layout &layout,
                                     int count, int eventsEach);

    /**
     * The round trip that would have placed the take right, in seconds:
     * the one it was placed with plus the median offset measured.
     *
     * The offset is found minus expected, so positive means the take
     * landed late: the latency used was too small, the take was spliced
     * from too early a frame of the recording, and the round trip has
     * to grow.
     */
    double calibratedRoundTrip(double usedRoundTripSeconds,
                               double medianOffsetSeconds);
}

#endif
