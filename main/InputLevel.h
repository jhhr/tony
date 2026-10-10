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

#ifndef TONY_INPUT_LEVEL_H
#define TONY_INPUT_LEVEL_H

#include "base/BaseTypes.h"

#include <QString>

#include <cstdint>
#include <vector>

/**
 * The level of what the microphone gives: the peak of a recording and
 * where it clipped (Scanner, scanFile()), the input meter's hold and
 * decay (Meter), and what Playback > Check Input Level makes of a
 * silence and a loudest phrase (gainChange(), noiseFloor(),
 * suggestedThreshold()).
 *
 * Only digital clipping can be seen: samples held at the converter's
 * largest value.  A microphone that distorts in its own electronics,
 * before the converter, gives a waveform that looks like any other, and
 * has to be heard; so does a converter's clipping scaled down after it
 * (an operating system's input volume under 100 %), which no longer
 * reaches full scale.
 *
 * Amplitudes are full scale 1; levels are dBFS, the peak of a full scale
 * sine at 0.
 */
namespace InputLevel
{
    /**
     * A sample this near full scale counts as at full scale: within
     * 0.01 dB of it.  A converter that clips holds its largest code, which
     * a 24-bit one gives as 1 - 2^-23 and a 16-bit one as 1 - 2^-15, both
     * well over this; a peak that only touches full scale between two
     * samples gives a sample or two near it, not a run (kClipRun)
     */
    constexpr float kFullScale = 0.999f;

    /**
     * How many samples in a row at full scale are a clip: three, as
     * Audacity's Find Clipping has it by default.  A peak of a sibilant
     * or a click that touches full scale is one sample there; a clipped
     * waveform is flat for as long as it is over, a whole run.  A smooth
     * crest that reaches full scale exactly without going over is a run
     * too (a 100 Hz sine at 48 kHz holds 7 samples within 0.01 dB of its
     * top), and is counted: it came within a hundredth of a dB of
     * clipping, and wants the gain down as much
     */
    constexpr int kClipRun = 3;

    /// Clips nearer each other than this are one place to be told of
    constexpr double kPlaceSeconds = 0.5;

    /**
     * Where the gain should put a singer's loudest peaks: 10 dB under
     * full scale leaves room for a take sung louder than the phrase the
     * level was checked with, and a 24-bit converter's own noise is
     * still over 100 dB under them
     */
    constexpr double kTargetPeakDbfs = -10.0;

    /// The level of an amplitude in dBFS; -200 for silence
    double dbfs(double amplitude);

    /// A run of frames at full scale, [start, end)
    struct Clip {
        sv::sv_frame_t start;
        sv::sv_frame_t end;
    };

    /// What a recording holds
    struct Scan {
        /// The peak of every channel, judged or not
        std::vector<float> channelPeaks;

        /// The peak of the channels judged
        float peak = 0.f;

        /// The runs of kClipRun frames or more at which a channel judged
        /// was at full scale, in order
        std::vector<Clip> clips;

        /// The frames scanned
        sv::sv_frame_t frames = 0;

        bool clipped() const { return !clips.empty(); }
    };

    /**
     * Scans interleaved audio given a block at a time, so that a long
     * recording is never in memory whole
     */
    class Scanner
    {
    public:
        /// channel: the one channel to judge, or -1 for all of them
        Scanner(int channels, int channel = -1);

        void add(const float *interleaved, sv::sv_frame_t frames);

        /// What was scanned, a run at the end included
        Scan finish();

    private:
        int m_channels;
        int m_channel;
        Scan m_scan;
        sv::sv_frame_t m_runStart;
        sv::sv_frame_t m_runLength;

        void endRun();
    };

    /**
     * Scan count frames of a WAV file from \a from (-1: to its end), as
     * the file holds them, not normalised.  On failure the scan is empty
     * and error says why
     */
    Scan scanFile(QString path, int channel, sv::sv_frame_t from,
                  sv::sv_frame_t count, QString &error);

    /// The clips as places to tell of: those nearer each other than gap
    /// frames joined into one
    std::vector<Clip> places(const std::vector<Clip> &clips,
                             sv::sv_frame_t gap);

    /**
     * The input meter's arithmetic: a bar at the latest peak, falling
     * from it; a hold at the highest peak, which stays a while and then
     * falls too; and a clip light, lit by a peak at full scale until it
     * is put out.  Times are milliseconds on any steady clock
     */
    class Meter
    {
    public:
        /// The bottom of the scale
        static constexpr double kFloorDb = -60.0;

        /// How fast the bar and the hold fall: as a programme meter
        /// falls back, slowly enough to read a phrase's level, fast
        /// enough to see the next phrase's
        static constexpr double kFallDbPerSecond = 20.0;

        /// How long the hold stays before it falls
        static constexpr int kHoldMs = 1500;

        void peak(float amplitude, std::int64_t ms);

        double bar(std::int64_t ms) const;
        double hold(std::int64_t ms) const;

        bool clipped() const { return m_clipped; }
        void setClipped(bool clipped) { m_clipped = clipped; }

        /// Whether anything is shown over the floor at ms: what is still
        /// moving needs drawing again
        bool moving(std::int64_t ms) const;

    private:
        double m_barDb = kFloorDb;
        std::int64_t m_barMs = 0;
        double m_holdDb = kFloorDb;
        std::int64_t m_holdMs = 0;
        bool m_clipped = false;
    };

    /**
     * The change in dB that puts a loudest peak at kTargetPeakDbfs,
     * rounded to a whole dB: positive is up
     */
    double gainChange(double peakDbfs);

    /**
     * The level of a silence from the peaks a meter read in it, one a
     * reading, in dBFS: their median, so that a click or a breath in it
     * does not count.  -200 for none
     */
    double noiseFloor(std::vector<double> peaksDbfs);

    /**
     * The voice threshold (VoiceThreshold) to suggest for a noise floor
     * read as peaks: the lowest of VoiceThreshold::choices() at least 5 dB
     * over it, or Off where the live tracker's own floor keeps the noise
     * out.  The threshold compares the level (RMS) of a half window, which
     * a noise's peaks stand over: about 12 dB for a hiss, 3 dB for a
     * steady hum or a fan's lines, so the hum is 8 dB under the threshold
     * at the least.  Off where the noise's peaks are 10 dB or more under
     * the tracker's own floor (-60 dBFS); the highest choice where the
     * noise is louder than every choice allows for
     */
    double suggestedThreshold(double noiseFloorDbfs);
}

#endif
