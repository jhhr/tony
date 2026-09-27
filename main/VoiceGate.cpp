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

#include "VoiceGate.h"

#include "RealtimePitchTracker.h"
#include "VoiceThreshold.h"

#include <algorithm>

using namespace sv;

int
VoiceGate::stampOffset(bool preciseTime, int block)
{
    return preciseTime ? block / 2 : block / 4;
}

VoiceGate::VoiceGate(double threshold, int stampOffset,
                     int hop, int halfWindow) :
    m_threshold(threshold),
    m_stampOffset(stampOffset),
    m_hop(std::max(1, hop)),
    m_halfWindow(std::max(1, halfWindow))
{
}

bool
VoiceGate::isOn() const
{
    return VoiceThreshold::isOn(m_threshold);
}

std::vector<sv_frame_t>
VoiceGate::noteStamps(const Event &note) const
{
    // pYIN ends a note at the stamp of the first hop that is not part of
    // it, so its last stamp is a hop before its end.  A note with no
    // length still has its onset
    std::vector<sv_frame_t> stamps { note.getFrame() };
    const sv_frame_t end = note.getFrame() + note.getDuration();
    for (sv_frame_t s = note.getFrame() + m_hop; s < end; s += m_hop) {
        stamps.push_back(s);
    }
    return stamps;
}

std::vector<sv_frame_t>
VoiceGate::stampsOf(const EventVector &pitch, const EventVector &notes) const
{
    std::vector<sv_frame_t> stamps;
    for (const Event &e : pitch) stamps.push_back(e.getFrame());
    for (const Event &e : notes) {
        for (sv_frame_t s : noteStamps(e)) stamps.push_back(s);
    }
    std::sort(stamps.begin(), stamps.end());
    stamps.erase(std::unique(stamps.begin(), stamps.end()), stamps.end());
    return stamps;
}

VoiceGate::Levels
VoiceGate::measureLevels(const std::vector<sv_frame_t> &stamps,
                         int channels, const Reader &read,
                         sv_frame_t readFrames) const
{
    Levels levels;
    if (!isOn()) return levels;

    // One block always holds a whole half window
    const sv_frame_t blockFrames =
        std::max(readFrames, sv_frame_t(m_halfWindow));

    // The block of the mixdown at hand, [blockStart, blockEnd); none yet
    std::vector<float> block;
    sv_frame_t blockStart = 0, blockEnd = 0;

    for (sv_frame_t stamp : stamps) {

        if (levels.find(stamp) != levels.end()) continue;

        const sv_frame_t from = stamp - m_stampOffset;
        const sv_frame_t to = from + m_halfWindow;

        if (from < blockStart || to > blockEnd || block.empty()) {

            // The next block starts where this half does: stamps in order
            // want nothing before it any more, and a stretch with no
            // stamps in it is never read
            blockStart = from;
            blockEnd = from + blockFrames;
            block.assign(size_t(blockFrames), 0.f);

            // What is not read stays silence: frames before 0, which
            // getData() would not give, and those past the end
            const sv_frame_t readFrom = std::max(sv_frame_t(0), blockStart);
            if (readFrom < blockEnd) {
                const floatvec_t got = read(readFrom, blockEnd - readFrom);
                const sv_frame_t n =
                    std::min(sv_frame_t(got.size()), blockEnd - readFrom);
                std::copy(got.begin(), got.begin() + n,
                          block.begin() + (readFrom - blockStart));
            }
        }

        levels[stamp] = RealtimePitchTracker::level
            (block.data() + (from - blockStart), m_halfWindow, channels);
    }

    return levels;
}

VoiceGate::LevelOf
VoiceGate::lookup(const Levels &levels)
{
    // A copy, so that it cannot outlive what it looks in.  Cheap: a
    // minute of audio has about 10000 stamps
    return [levels](sv_frame_t stamp) {
        auto i = levels.find(stamp);
        return i == levels.end() ? -200.0 : i->second;
    };
}

EventVector
VoiceGate::gatePitch(const EventVector &events, const LevelOf &levelOf) const
{
    if (!isOn()) return events;

    EventVector kept;
    for (const Event &e : events) {
        if (isVoiced(levelOf(e.getFrame()))) kept.push_back(e);
    }
    return kept;
}

EventVector
VoiceGate::gateNotes(const EventVector &events, const LevelOf &levelOf) const
{
    if (!isOn()) return events;

    EventVector kept;

    for (const Event &e : events) {

        bool voiced = false;
        sv_frame_t first = 0, last = 0;
        for (sv_frame_t s : noteStamps(e)) {
            if (!isVoiced(levelOf(s))) continue;
            if (!voiced) first = s;
            last = s;
            voiced = true;
        }

        // Nothing of it was sung loudly enough
        if (!voiced) continue;

        // Quiet stamps between voiced ones stay: a note is only trimmed
        // at its ends
        const sv_frame_t end = e.getFrame() + e.getDuration();
        const sv_frame_t newEnd = std::min(end, last + m_hop);

        if (first == e.getFrame() && newEnd == end) {
            kept.push_back(e);
        } else {
            kept.push_back(e.withFrame(first).withDuration(newEnd - first));
        }
    }

    return kept;
}
