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

#include "TakeDiff.h"

#include "RealtimePitchTracker.h"
#include "TakeEvents.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>

using namespace sv;

namespace {

sv_frame_t
framesOf(double seconds, sv_samplerate_t rate)
{
    return sv_frame_t(std::llround(seconds * rate));
}

bool
sameBits(float a, float b)
{
    uint32_t x, y;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

// A ratio in dB that stays finite, as LatencyCheck has it: silence
// against silence is 0 dB, and anything against silence is 200
double
decibels(double value, double against)
{
    const double limit = 200.0;
    if (value <= 0.0 && against <= 0.0) return 0.0;
    if (value <= 0.0) return -limit;
    if (against <= 0.0) return limit;
    double db = 20.0 * std::log10(value / against);
    return std::max(-limit, std::min(limit, db));
}

// The events no part of which lies in the window, sorted.  What
// erasing the window would touch is what lies in it: a note that
// crosses its edge goes with it, and an event with no duration, a
// pitch event, goes by its frame alone
EventVector
outside(const EventVector &events, const Coverage::Range &window)
{
    EventVector all(events);
    EventVector in = TakeEvents::eraseNotes
        (events, Coverage::Ranges { window }).removed;
    std::sort(all.begin(), all.end());
    std::sort(in.begin(), in.end());

    EventVector result;
    std::set_difference(all.begin(), all.end(), in.begin(), in.end(),
                        std::back_inserter(result));
    return result;
}

} // namespace

TakeDiff::AudioDiff
TakeDiff::audioOutside(const float *before, sv_frame_t beforeFrames,
                       const float *after, sv_frame_t afterFrames,
                       int channels, const Coverage::Range &range)
{
    AudioDiff result;
    if (channels < 1) return result;

    auto compare = [&](sv_frame_t from, sv_frame_t to) {
        for (sv_frame_t i = from; i < to; ++i) {
            bool differs = false;
            for (int c = 0; c < channels; ++c) {
                float a = (i < beforeFrames) ? before[i * channels + c] : 0.f;
                float b = (i < afterFrames) ? after[i * channels + c] : 0.f;
                if (sameBits(a, b)) continue;
                differs = true;
                result.largestDifference = std::max
                    (result.largestDifference,
                     std::fabs(double(a) - double(b)));
            }
            if (differs) {
                if (result.firstDifference < 0) result.firstDifference = i;
                ++result.differences;
            }
        }
    };

    // An empty or backward range leaves out nothing
    sv_frame_t total = std::max(beforeFrames, afterFrames);
    sv_frame_t skipFrom = std::max(sv_frame_t(0), std::min(range.start, total));
    sv_frame_t skipTo = std::max(skipFrom, std::min(range.end, total));
    compare(0, skipFrom);
    compare(skipTo, total);

    result.pass = (result.differences == 0);
    return result;
}

TakeDiff::EventDiff
TakeDiff::eventsOutside(const EventVector &before, const EventVector &after,
                        const Coverage::Range &range, sv_samplerate_t rate,
                        double marginSeconds)
{
    EventDiff result;

    sv_frame_t margin = framesOf(marginSeconds, rate);
    result.window = Coverage::Range(range.start - margin, range.end + margin);

    EventVector was = outside(before, result.window);
    EventVector is = outside(after, result.window);

    // What only one side has, event for event: the very same event on
    // both sides is no change, however many of it there are
    EventVector onlyWas, onlyIs;
    std::set_difference(was.begin(), was.end(), is.begin(), is.end(),
                        std::back_inserter(onlyWas));
    std::set_difference(is.begin(), is.end(), was.begin(), was.end(),
                        std::back_inserter(onlyIs));

    // Both sorted by frame first: pair what is left at one frame as
    // changed, in order, and the rest was removed or added
    size_t i = 0, j = 0;
    while (i < onlyWas.size() || j < onlyIs.size()) {
        if (j == onlyIs.size() ||
            (i < onlyWas.size() &&
             onlyWas[i].getFrame() < onlyIs[j].getFrame())) {
            result.removed.push_back(onlyWas[i++]);
        } else if (i == onlyWas.size() ||
                   onlyIs[j].getFrame() < onlyWas[i].getFrame()) {
            result.added.push_back(onlyIs[j++]);
        } else {
            result.changed.push_back({ onlyWas[i++], onlyIs[j++] });
        }
    }

    auto first = [&](sv_frame_t frame) {
        if (result.firstDifference < 0 || frame < result.firstDifference) {
            result.firstDifference = frame;
        }
    };
    for (const Event &e : result.removed) first(e.getFrame());
    for (const Event &e : result.added) first(e.getFrame());
    for (const auto &p : result.changed) first(p.first.getFrame());

    result.pass = result.added.empty() && result.removed.empty() &&
        result.changed.empty();
    return result;
}

TakeDiff::PitchJoin
TakeDiff::pitchAcross(const EventVector &pitch, sv_frame_t join,
                      sv_samplerate_t rate, double windowSeconds,
                      int maxGapHops)
{
    PitchJoin result;

    sv_frame_t reach = framesOf(windowSeconds, rate);
    result.window = Coverage::Range(join - reach, join + reach);
    const sv_frame_t from = result.window.start, to = result.window.end;

    // Order, in the order given
    std::vector<sv_frame_t> inside;
    for (const Event &e : pitch) {
        sv_frame_t f = e.getFrame();
        if (f < from || f >= to) continue;
        if (!inside.empty() && f < inside.back()) {
            if (result.firstOutOfOrder < 0) result.firstOutOfOrder = f;
            ++result.outOfOrder;
        }
        inside.push_back(f);
    }
    result.events = int(inside.size());

    // Doubles, whatever the order: each frame counted once however
    // many events it holds
    std::sort(inside.begin(), inside.end());
    for (size_t k = 1; k < inside.size(); ++k) {
        if (inside[k] != inside[k-1]) continue;
        if (k >= 2 && inside[k-1] == inside[k-2]) continue;
        if (result.firstDoubled < 0) result.firstDoubled = inside[k];
        ++result.doubled;
    }
    inside.erase(std::unique(inside.begin(), inside.end()), inside.end());

    // Gaps: from the last event before the window, through those in
    // it, to the first after it.  With no event beyond an edge, the
    // edge stands in for one
    sv_frame_t previous = from, next = to;
    bool havePrevious = false, haveNext = false;
    for (const Event &e : pitch) {
        sv_frame_t f = e.getFrame();
        if (f < from && (!havePrevious || f > previous)) {
            previous = f;
            havePrevious = true;
        }
        if (f >= to && (!haveNext || f < next)) {
            next = f;
            haveNext = true;
        }
    }

    std::vector<sv_frame_t> points;
    points.push_back(previous);
    points.insert(points.end(), inside.begin(), inside.end());
    points.push_back(next);

    for (size_t k = 1; k < points.size(); ++k) {
        sv_frame_t gap = points[k] - points[k-1];
        if (gap > result.largestGap) {
            result.largestGap = gap;
            result.largestGapFrom = points[k-1];
        }
    }

    result.pass = result.largestGap <= maxGapHops * kHopFrames &&
        result.doubled == 0 && result.outOfOrder == 0;
    return result;
}

TakeDiff::NoteJoin
TakeDiff::notesAcross(const EventVector &notes, sv_frame_t join,
                      sv_samplerate_t rate, double clearanceSeconds)
{
    NoteJoin result;

    sv_frame_t clearance = framesOf(clearanceSeconds, rate);
    bool haveEdge = false;

    for (const Event &note : notes) {
        sv_frame_t start = note.getFrame();
        sv_frame_t end = start + note.getDuration();

        if (start <= join && join < end) result.spanning.push_back(note);

        bool edgeNear = false;
        for (sv_frame_t edge : { start, end }) {
            sv_frame_t offset = edge - join;
            if (std::llabs(offset) <= clearance) edgeNear = true;
            if (!haveEdge || std::llabs(offset) < std::llabs(result.nearestEdge)) {
                result.nearestEdge = offset;
                haveEdge = true;
            }
        }
        if (edgeNear) result.edgesNear.push_back(note);
    }

    result.pass = result.spanning.size() == 1 && result.edgesNear.empty();
    return result;
}

TakeDiff::SampleStep
TakeDiff::stepAt(const float *samples, sv_frame_t frames, int channels,
                 sv_samplerate_t rate, sv_frame_t join)
{
    SampleStep result;
    if (channels < 1) return result;

    // First difference i is x[i] - x[i - 1], so i runs from 1
    auto clamp = [&](sv_frame_t i) {
        return std::max(sv_frame_t(1), std::min(i, frames));
    };

    sv_frame_t reach = framesOf(kStepSeconds, rate);
    sv_frame_t half = framesOf(kStepContextSeconds / 2.0, rate);
    sv_frame_t stepFrom = clamp(join - reach), stepTo = clamp(join + reach + 1);
    sv_frame_t contextFrom = clamp(join - half), contextTo = clamp(join + half);

    // Nothing to read at the join: it is not in the audio
    if (stepFrom >= stepTo) return result;

    auto difference = [&](sv_frame_t i, int c) {
        return std::fabs(double(samples[i * channels + c]) -
                         double(samples[(i - 1) * channels + c]));
    };

    bool first = true;
    for (int c = 0; c < channels; ++c) {

        double largest = 0.0;
        sv_frame_t largestAt = join;
        for (sv_frame_t i = stepFrom; i < stepTo; ++i) {
            double d = difference(i, c);
            if (d > largest) {
                largest = d;
                largestAt = i;
            }
        }

        double typical = 0.0;
        std::vector<double> context;
        for (sv_frame_t i = contextFrom; i < contextTo; ++i) {
            context.push_back(difference(i, c));
        }
        if (!context.empty()) {
            size_t k = size_t(std::llround
                              (kTypicalQuantile * double(context.size() - 1)));
            std::nth_element(context.begin(), context.begin() + k,
                             context.end());
            typical = context[k];
        }

        double db = decibels(largest, typical);
        if (first || db > result.stepDb) {
            result.stepDb = db;
            result.largest = largest;
            result.typical = typical;
            result.largestAt = largestAt;
            result.channel = c;
            first = false;
        }
    }

    result.pass = result.stepDb <= kMaxStepDb;
    return result;
}

TakeDiff::DotReach
TakeDiff::dotReach(sv_samplerate_t rate)
{
    DotReach reach;
    if (rate <= 0) return reach;
    reach.before = double(kDotHops * RealtimePitchTracker::kHopSize) / rate;
    reach.after = reach.before +
        double(RealtimePitchTracker::kWindowSize / 2) / rate;
    reach.onset = double(RealtimePitchTracker::kWindowSize) / rate;
    return reach;
}

TakeDiff::LiveDot
TakeDiff::placeLiveDot(const LatencyCheck::Layout &layout,
                       double punchInStart, double seconds, double hz)
{
    LiveDot dot;
    const DotReach reach = dotReach(layout.rate);
    if (layout.rate <= 0) return dot;

    auto atOnset = [&](double start) {
        return seconds >= start - reach.before &&
            seconds < start + reach.onset;
    };

    for (const LatencyCheck::Event &e : layout.events) {
        const double sweepAt = double(e.sweepStart) / layout.rate;
        if (seconds >= sweepAt - reach.before &&
            seconds <= sweepAt + LatencyCheck::kSweepSeconds + reach.after) {
            dot.place = DotPlace::OnSweep;
            return dot;
        }
    }

    // The first windows of a punch-in straddle the start of what is
    // kept: before it, what the mic heard during the lead-in
    if (atOnset(punchInStart)) {
        dot.place = DotPlace::AtOnset;
    }

    for (const LatencyCheck::Event &e : layout.events) {
        const double from = double(e.toneStart) / layout.rate;
        const double to = double(e.toneStart + e.toneLength) / layout.rate;
        if (seconds < from - reach.before || seconds > to + reach.after) {
            continue;
        }
        const bool voiced = (hz > 0.0);
        dot.toneHz = e.toneHz;
        dot.cents = (voiced ? 1200.0 * std::log2(hz / e.toneHz) : 0.0);
        if (dot.place == DotPlace::AtOnset || atOnset(from)) {
            dot.place = DotPlace::AtOnset;
        } else {
            dot.place = (voiced && std::fabs(dot.cents) <= kDotCents ?
                         DotPlace::OnPitch : DotPlace::OffPitch);
        }
        return dot;
    }
    return dot;
}
