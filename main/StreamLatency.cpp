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

#include "StreamLatency.h"

#include <algorithm>
#include <cmath>

namespace StreamLatency
{

static double
framesSince(int64_t thenNanos, int64_t nowNanos, double rate)
{
    return double(nowNanos - thenNanos) * rate / 1.0e9;
}

double
outputLatency(Position output, int64_t nowNanos, double rate)
{
    // The hardware has played up to hardwareFrame plus whatever has
    // gone out since its timestamp; the rest of what was written is
    // still to be heard
    return double(output.appFrames - output.hardwareFrame) -
        framesSince(output.hardwareNanos, nowNanos, rate);
}

double
inputLatency(Position input, int64_t nowNanos, double rate)
{
    // The next frame to read came in (appFrames - hardwareFrame) frames
    // after the timestamp's
    return framesSince(input.hardwareNanos, nowNanos, rate) -
        double(input.appFrames - input.hardwareFrame);
}

Estimate
fromReading(double output, double input)
{
    // The sum is rounded once, so that it is the nearest to the exact
    // round trip whichever way the parts round
    Estimate e;
    int roundTrip = int(std::lround(output + input));
    e.output = int(std::lround(output));
    e.input = roundTrip - e.output;
    if (e.input < 0) {
        e.output = roundTrip;
        e.input = 0;
    }
    if (e.output < 0) {
        e.input = roundTrip;
        e.output = 0;
    }
    if (e.input < 0) {
        e.input = 0; // nothing to keep: the round trip is negative
    }
    return e;
}

bool
isPlausible(Estimate estimate, double rate)
{
    int roundTrip = estimate.roundTrip();
    return roundTrip > 0 && roundTrip <= rate;
}

bool
median(std::vector<Estimate> readings, double rate, Estimate &chosen)
{
    readings.erase(std::remove_if(readings.begin(), readings.end(),
                                  [rate](const Estimate &e) {
                                      return !isPlausible(e, rate);
                                  }),
                   readings.end());
    if (readings.empty()) return false;
    std::sort(readings.begin(), readings.end(),
              [](const Estimate &a, const Estimate &b) {
                  return a.roundTrip() < b.roundTrip();
              });
    chosen = readings[(readings.size() - 1) / 2];
    return true;
}

Estimate
guess(int outputBufferFrames, int inputBurstFrames)
{
    Estimate e;
    e.output = std::max(0, outputBufferFrames);
    e.input = std::max(0, inputBurstFrames);
    return e;
}

int
inputFramesToRead(int asked, int available, int room)
{
    return std::max(0, std::min(room, std::max(asked, available)));
}

bool
inputKeptUp(int backlogFrames, int outputBufferFrames, int inputBurstFrames)
{
    return backlogFrames <= std::max(0, outputBufferFrames) +
        2 * std::max(0, inputBurstFrames);
}

}
