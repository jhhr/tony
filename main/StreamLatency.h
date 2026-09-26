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

#ifndef TONY_STREAM_LATENCY_H
#define TONY_STREAM_LATENCY_H

#include <cstdint>
#include <vector>

/**
 * The arithmetic of a device's latency from its streams' timestamps,
 * for OboeAudioIO: Android has no call that says what the latency is,
 * only timestamps that say when a frame met the hardware.
 *
 * Every figure is in frames at the device's rate, the unit bqaudioio's
 * backends report latency in (setSystemPlaybackLatency() and
 * setSystemRecordLatency()).
 *
 * Nothing here touches a device, so all of it is tested without one
 * (TestStreamLatency).
 */
namespace StreamLatency
{
    /**
     * Where a stream is: the frames the application has written to an
     * output stream, or read from an input stream, so far, and a
     * timestamp: the frame at hardwareFrame left for the speaker, or
     * came in from the microphone, at hardwareNanos (CLOCK_MONOTONIC).
     */
    struct Position {
        int64_t appFrames = 0;
        int64_t hardwareFrame = 0;
        int64_t hardwareNanos = 0;
    };

    /**
     * An output stream's latency at nowNanos: how long the next frame
     * the application writes will take to be heard.
     */
    double outputLatency(Position output, int64_t nowNanos, double rate);

    /**
     * An input stream's latency at nowNanos: how long ago the next
     * frame the application reads came in from the microphone.
     * Negative if it has not come in yet.
     */
    double inputLatency(Position input, int64_t nowNanos, double rate);

    /// Output and input latency, in whole frames
    struct Estimate {
        int output = 0;
        int input = 0;
        int roundTrip() const { return output + input; }
    };

    /**
     * One reading of the two latencies, taken at the same moment, as
     * an estimate: rounded, and neither of them negative.
     *
     * Taken between two callbacks, the output latency comes out high
     * and the input latency low by the same amount, the time to the
     * next callback, so only their sum is exact. The sum is what a
     * recording is compensated by (LatencyUtils.h) and is kept: a
     * negative part is taken off the other.
     */
    Estimate fromReading(double output, double input);

    /**
     * Whether an estimate can be believed: a round trip longer than
     * nothing and no longer than a second at the given rate.
     */
    bool isPlausible(Estimate estimate, double rate);

    /**
     * Of several readings, the plausible one with the median round
     * trip: a reading spoilt by a callback that ran while it was taken
     * is off by a callback's worth of frames, and is left out that
     * way. False if none is plausible.
     */
    bool median(std::vector<Estimate> readings, double rate,
                Estimate &chosen);

    /**
     * A guess for streams with no timestamps: what the output buffer
     * holds, and one burst of input.
     */
    Estimate guess(int outputBufferFrames, int inputBurstFrames);
}

#endif
