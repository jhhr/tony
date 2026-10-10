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

#ifndef TONY_RECORDING_ALIGNMENT_H
#define TONY_RECORDING_ALIGNMENT_H

#include "base/BaseTypes.h"

#include <QString>

#include <functional>
#include <memory>
#include <vector>

/**
 * Where a stretch of a take's audio sits in a longer recording of the
 * same singing: a wireless transmitter's own backup recording, which
 * started whenever it was started, runs on as long as it was left
 * recording, holds the take whole where the radio link dropped out, and
 * may be at another rate (docs/takes.md, "Replace Take Audio from
 * Recording").
 *
 * The search, in three stages:
 *
 *  1. Coarse: the level of each 10 ms of the take's audio against each
 *     10 ms of the whole recording, as a correlation at every offset
 *     (through the FFT). The offsets where it peaks are the candidates.
 *  2. Fine: for each candidate, the samples of the take's loudest half
 *     second against the recording's, at every offset within 25 ms of
 *     it. The best is the anchor.
 *  3. The walk: quarter seconds end to end over the whole take, each
 *     against the recording within 0.5 ms of the offset of the last that
 *     matched,
 *     out from the anchor both ways, so that a slow drift between two
 *     clocks is followed. The confidence is the median of how alike they
 *     are; the offsets near the two ends say how far the clocks drifted.
 *
 * Alike is a correlation coefficient of the samples, 1 for the same
 * waveform at any gain. Both are pre-emphasised first (a first
 * difference), so that the low notes' broad waveforms, which another
 * take of the same note at the same pitch matches nearly as well, count
 * for less than the detail that only the same take has.
 *
 * Dropout gaps in the take, digital silence where the radio link
 * dropped, are left out of every comparison: the recording has the
 * singing there, which the take does not, and counting it would read a
 * take with many gaps as unlike its own recording.
 *
 * Offsets are counted in the take's frames: the recording's frame that
 * holds the take's frame f is (f + offset) * recording rate / take
 * rate. The recording is read at the take's rate, by linear
 * interpolation, where the rates differ; at the same rate it is read as
 * it is, and an offset is exact to the frame.
 *
 * Pure: it reads its two sources and nothing else, and may run on any
 * thread.
 */
namespace RecordingAlignment
{
    /// The level envelope's hop, in seconds
    constexpr double kHopSeconds = 0.01;

    /// Below this confidence the take's audio is not taken to be in the
    /// recording. The same singing reads 0.9 or more; another singing
    /// of the same song, and anything else, reads much less (the tests'
    /// synthetic takes: under 0.3)
    constexpr double kMinConfidence = 0.5;

    /// Ends whose offsets differ by more than this have drifted apart
    constexpr double kDriftSeconds = 0.002;

    /// The shortest take range that can be looked for
    constexpr double kMinRangeSeconds = 0.5;

    /// The shortest stretch of another recording session found inside a
    /// session: loud windows unlike the recording for this long
    constexpr double kMinSessionSeconds = 0.1;

    /// Audio read a stretch at a time, its channels averaged
    class Source
    {
    public:
        virtual ~Source() { }
        virtual double rate() const = 0;
        virtual sv::sv_frame_t frames() const = 0;

        /// count frames from frame "from", mixed to one channel; zero
        /// where there is nothing (before 0, past the end)
        virtual std::vector<float> read(sv::sv_frame_t from,
                                        sv::sv_frame_t count) const = 0;
    };

    /// Samples in memory, mono
    class MemorySource : public Source
    {
    public:
        MemorySource(std::vector<float> samples, double rate) :
            m_samples(std::move(samples)), m_rate(rate) { }
        double rate() const override { return m_rate; }
        sv::sv_frame_t frames() const override {
            return sv::sv_frame_t(m_samples.size());
        }
        std::vector<float> read(sv::sv_frame_t from,
                                sv::sv_frame_t count) const override;
    private:
        std::vector<float> m_samples;
        double m_rate;
    };

    /// A WAV file, its channels averaged; null, with a message for the
    /// user in error, if it cannot be read
    std::unique_ptr<Source> openWav(QString path, QString &error);

    struct Match {
        /// Whether the take's audio was found, with a confidence of
        /// kMinConfidence or more; if not, error says why
        bool found;
        QString error;

        /// A stretch not found that is left as the take has it, rather
        /// than refusing the replacement, and why: too short to look
        /// for; another recording session too short to look for; or
        /// before or after what the recording holds
        enum class Left { No, TooShort, ShortSession, OutsideRecording };
        Left left;

        /// Given up, as progress asked
        bool cancelled;

        /// The offset to use, in the take's frames: halfway between the
        /// two ends', so that a drift is shared out
        double offset;

        /// The median of how alike the walk's pieces were, 0 to 1
        double confidence;

        /// The pieces that confidence is the median of (none for a take
        /// too short for the walk: then it is the anchor's)
        int pieces;

        /// The offsets found near the start and the end of the range,
        /// in the take's frames, where both could be measured
        bool endsMeasured;
        double startOffset;
        double endOffset;

        /// How far apart, in seconds, the two ends' samples lie (end
        /// less start: positive if the recording runs slow)
        double drift(double takeRate) const {
            return endsMeasured ? (endOffset - startOffset) / takeRate : 0.0;
        }

        /// How much louder the take is than the recording: the RMS
        /// ratio over the pieces that matched
        double gain;

        /// The take's frames the recording holds at the offset, within
        /// the range looked for: all of it, unless the recording began
        /// after the range did or ended before it did
        sv::sv_frame_t coveredFrom;
        sv::sv_frame_t coveredTo;

        /// The walk's pieces, in order: where each is in the take, the
        /// offset it was found at, and how alike it was there. Only
        /// those that counted: mostly whole, and loud enough
        struct Piece {
            sv::sv_frame_t start;
            sv::sv_frame_t end;
            double offset;
            double r;
        };
        std::vector<Piece> walked;

        Match() : found(false), left(Left::No), cancelled(false),
                  offset(0), confidence(0), pieces(0),
                  endsMeasured(false), startOffset(0), endOffset(0),
                  gain(1), coveredFrom(0), coveredTo(0) { }
    };

    /// A stretch of the take and where it is in the recording
    struct Segment {
        sv::sv_frame_t start;
        sv::sv_frame_t end;
        Match match;
    };

    /**
     * Search the recording for the take's audio over [from, to): placed
     * anywhere that leaves at least the loudest half second of the range
     * on the recording, so that a recording begun after the range began,
     * or stopped before it ended, still holds what it holds. progress
     * is told how far the search is, in percent, from time to time, and
     * may return false to give it up (then error says so).
     */
    Match find(const Source &take, sv::sv_frame_t from, sv::sv_frame_t to,
               const Source &recording,
               std::function<bool(int)> progress = {});

    /**
     * The recording's level, each kHopSeconds, which find() compares the
     * take's with: all of the recording read once, for a take of several
     * ranges, each found with the find() below. progress as find()'s;
     * false if it gave up
     */
    bool levels(const Source &recording, std::vector<double> &levels,
                std::function<bool(int)> progress = {});

    /// find(), with the recording's levels() read already
    Match find(const Source &take, sv::sv_frame_t from, sv::sv_frame_t to,
               const Source &recording,
               const std::vector<double> &recordingLevels,
               std::function<bool(int)> progress = {});

    /**
     * The take's audio over [from, to), as the stretches it is made of,
     * each found where it is in the recording: a take's range can hold
     * more than one recording session, a punch-in over an earlier take,
     * which the transmitter recorded at another time.
     *
     * The range is looked for with find(). What of it lies before or
     * after what the recording holds is left as it is. The rest is
     * scanned in 20 ms windows at the offsets the walk found: where
     * loud windows are unlike the recording for kMinSessionSeconds or
     * more, that stretch is another session, looked for on its own the
     * same way if it is kMinRangeSeconds long, and left as it is if it
     * is shorter. In a range not found as a whole, the punch-ins may be
     * most of it: the longest run of the walk's pieces alike, four in a
     * row or more, is a session of its own, and the rest is looked for
     * the same way. Four sessions deep at most. A segment not found has
     * its match's found false, and its left says whether it is left as
     * it is or refuses the replacement. In order, end to end, over the
     * whole range.
     */
    std::vector<Segment> findSegments(const Source &take, sv::sv_frame_t from,
                                      sv::sv_frame_t to,
                                      const Source &recording,
                                      const std::vector<double> &recordingLevels,
                                      std::function<bool(int)> progress = {});

    /**
     * The recording's frames that hold the take's frames [start, end)
     * found at offset: from the frame before to the frame after, at the
     * recording's rate, every one of them within the recording's frames.
     * A stretch that reaches past either end of the recording is cut at
     * it first: start and end are what is left of it, end not after
     * start if nothing is. lead is how many of the take's frames the
     * span begins early, which the splice leaves off its front.
     */
    struct Span {
        sv::sv_frame_t start;
        sv::sv_frame_t end;
        sv::sv_frame_t from;
        sv::sv_frame_t count;
        sv::sv_frame_t lead;
    };
    Span recordingSpan(sv::sv_frame_t start, sv::sv_frame_t end,
                       double offset, double takeRate,
                       double recordingRate,
                       sv::sv_frame_t recordingFrames);

    /**
     * The correlation coefficient of a, over the samples where mask is
     * not 0, with b at each offset from 0 to b.size() - a.size(): through
     * the FFT, for long ones. 0 where either is constant over them. For
     * the coarse and fine stages; exposed for the tests
     */
    std::vector<double> maskedCorrelation(const std::vector<double> &a,
                                          const std::vector<double> &mask,
                                          const std::vector<double> &b);

    /**
     * Where each sample of audio is part of a dropout gap: a run of at
     * least gapSeconds of digital silence (no sample over -100 dBFS).
     * Exposed for the tests
     */
    std::vector<char> dropouts(const std::vector<float> &audio, double rate,
                               double gapSeconds = 0.005);
}

#endif
