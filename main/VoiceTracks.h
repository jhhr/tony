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

#ifndef TONY_VOICE_TRACKS_H
#define TONY_VOICE_TRACKS_H

#include "base/BaseTypes.h"

#include <QString>

#include <vector>

class QXmlStreamReader;

/**
 * The reference's voice tracks: its pitch track as the analysis that
 * made it gave it following each voice where two or three sing -- the
 * strongest, the lower and the higher -- over the whole song, from the
 * same run (the pyin fork's voice track outputs). Outside the passages
 * where voices sing together, and in their voicing everywhere, the three
 * are the same track.
 *
 * They are what a selection's pitch candidates are cut from, and what
 * Prefer Lower Voice and Prefer Higher Voice put into the pitch track:
 * the voices as the whole song's analysis found them, with all the
 * context its level floor and its follower's gate need, and at once.
 * The pitch track itself is the user's to edit; these are kept as the
 * analysis made them.
 *
 * A song's session keeps them, as it keeps the options the reference
 * was analysed with (AnalysisSettings), in an element of Tony's own:
 *
 *     <voicetracks>
 *       <voice name="strongest">11264 220.48 11520 220.51 ...</voice>
 *       <voice name="lower">...</voice>
 *       <voice name="higher">...</voice>
 *     </voicetracks>
 *
 * each voice's voiced frames and their pitch in Hz, in pairs. It is
 * written only with all three voices, and read only with all three.
 */
class VoiceTracks
{
public:
    enum Voice { Strongest = 0, Lower = 1, Higher = 2 };
    static constexpr int voiceCount = 3;

    /// A voiced frame of a track and its pitch
    struct Point {
        sv::sv_frame_t frame;
        double hz;
        bool operator==(const Point &p) const {
            return frame == p.frame && hz == p.hz;
        }
    };

    /// A track's voiced frames, in frame order, none twice
    typedef std::vector<Point> Track;

    /// The name a voice has in the element ("strongest", ...)
    static QString name(Voice voice);

    /**
     * Sets a voice's track from the points of an analysis's output:
     * those not voiced (not above 0 Hz) go, and of a frame given twice,
     * as pYIN's fixed-lag decoding gives one frame near the end of a
     * run, the first stays.
     */
    void set(Voice voice, const Track &points);

    const Track &get(Voice voice) const { return m_tracks[voice]; }

    /// Whether all three voices have been set
    bool isComplete() const;

    void clear();

    /// A voice's points from start up to, not including, end
    Track within(Voice voice, sv::sv_frame_t start, sv::sv_frame_t end) const;

    bool operator==(const VoiceTracks &other) const;

    /**
     * The element, indented with indent and ending in a newline; empty
     * if not all three voices are set, for there is then nothing a
     * session could use.
     */
    QString toXml(QString indent = "  ") const;

    /**
     * The `<voicetracks>` element the reader is at the start of, read to
     * its end. Incomplete (isComplete() false) if a voice is missing or
     * its text is not frames and pitches in pairs.
     */
    static VoiceTracks fromElement(QXmlStreamReader &reader);

private:
    Track m_tracks[voiceCount];
    bool m_set[voiceCount] = { false, false, false };
};

#endif
