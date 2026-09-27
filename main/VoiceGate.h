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

#ifndef TONY_VOICE_GATE_H
#define TONY_VOICE_GATE_H

#include "base/BaseTypes.h"
#include "base/Event.h"

#include <functional>
#include <map>
#include <vector>

/**
 * The voice threshold (VoiceThreshold) applied to what pYIN found in a
 * stretch of audio: a pitch event found in a window quieter than the
 * threshold goes, and a note loses the ends of it that were, or goes if
 * all of it was.  Its value is left as pYIN gave it.
 *
 * pYIN puts each pitch, and each note's onset, on a stamp: a frame a
 * fixed offset into the block it was found in.  The level compared with
 * the threshold is that of the half window whose frames YIN compared to
 * find the pitch, as the live tracker measures its first half for its
 * own floor: the frames [stamp - offset, stamp - offset + half window),
 * where the offset is windowOffset().
 * A note covers the stamps from its onset, one hop apart, up to its end.
 * A level at the threshold counts as voice, as it does in the tracker.
 *
 * This reads no model.  The audio comes in through a function the caller
 * gives, one block of the mixdown at a time, so that a take of any
 * length is measured without all of its audio being held at once:
 *
 *     VoiceGate gate(threshold, VoiceGate::windowOffset());
 *     VoiceGate::LevelOf levelOf = VoiceGate::lookup
 *         (gate.measureLevels(gate.stampsOf(pitch, notes), channels,
 *                             read));
 *     pitch = gate.gatePitch(pitch, levelOf);
 *     notes = gate.gateNotes(notes, levelOf);
 *
 * With the threshold Off, the events come back as they were, and no
 * level is measured or asked for.
 */
class VoiceGate
{
public:
    /// Tony's pYIN step and block, which the live tracker has too
    static constexpr int kHop = 256;
    static constexpr int kBlock = 2048;

    /// How many frames of the mixdown measureLevels() reads at once
    static constexpr sv::sv_frame_t kReadFrames = 65536;

    /// The level of the half window compared for each stamp, in dBFS,
    /// by stamp
    typedef std::map<sv::sv_frame_t, double> Levels;

    /// The level of the half window compared for a stamp, in dBFS
    typedef std::function<double(sv::sv_frame_t stamp)> LevelOf;

    /**
     * Gives count frames of the mixdown from start: the channels' sum,
     * as Model::getData(-1, start, count) does.  Fewer, or none, where
     * the audio ends.  It is never asked for frames before 0 (a model's
     * getData() would give the ones from 0 in their place).
     */
    typedef std::function<sv::floatvec_t(sv::sv_frame_t start,
                                         sv::sv_frame_t count)> Reader;

    /**
     * How far before its stamp the frames YIN compared for a pitch
     * begin: a quarter of a block, in either of pYIN's timings
     * (PYinVamp::process()).  Without "precisetime" pYIN stamps a block
     * a quarter of it in and compares the block's first half
     * (YinUtil::fastDifference()), as the live tracker does.  With it
     * ("precision-analysis" in Tony's settings) pYIN stamps the block
     * half of it in, but compares its middle half (slowDifference(), a
     * little wider at longer lags), which begins a quarter of a block
     * before that stamp too
     */
    static int windowOffset(int block = kBlock);

    VoiceGate(double threshold, int windowOffset,
              int hop = kHop, int halfWindow = kBlock / 2);

    /// Whether the threshold does anything (VoiceThreshold::isOn())
    bool isOn() const;

    /**
     * Every stamp that gatePitch() and gateNotes() will ask the level of
     * for these events, in order, each once
     */
    std::vector<sv::sv_frame_t> stampsOf(const sv::EventVector &pitch,
                                         const sv::EventVector &notes) const;

    /**
     * The levels of the stamps given, from a mixdown of the channel count
     * given, read through \a read in blocks of \a readFrames (or of a
     * half window, if that is more).  Stamps in order are read in one
     * pass, and a stretch with no stamps is not read at all.  Frames
     * before 0 and past the end of the audio are silence.  Off: no
     * levels, and nothing is read.
     */
    Levels measureLevels(const std::vector<sv::sv_frame_t> &stamps,
                         int channels, const Reader &read,
                         sv::sv_frame_t readFrames = kReadFrames) const;

    /**
     * The levels as a LevelOf, which holds a copy of them.  A stamp that
     * was not measured reads as silence.
     */
    static LevelOf lookup(const Levels &levels);

    /// The pitch events whose stamp is at or above the threshold
    sv::EventVector gatePitch(const sv::EventVector &events,
                              const LevelOf &levelOf) const;

    /**
     * The notes, each trimmed to begin at its first stamp at or above
     * the threshold and to end a hop after its last (or where it ended,
     * if that is sooner).  A note with no such stamp goes; one that is
     * voiced at both ends is returned as it was.
     */
    sv::EventVector gateNotes(const sv::EventVector &events,
                              const LevelOf &levelOf) const;

private:
    double m_threshold;
    int m_windowOffset;
    int m_hop;
    int m_halfWindow;

    bool isVoiced(double level) const { return level >= m_threshold; }

    /// A note's stamps: its onset, and each hop after it before its end
    std::vector<sv::sv_frame_t> noteStamps(const sv::Event &note) const;
};

#endif
