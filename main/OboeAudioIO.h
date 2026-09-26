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

#ifndef TONY_OBOE_AUDIO_IO_H
#define TONY_OBOE_AUDIO_IO_H

#include "AudioRoute.h"
#include "StreamLatency.h"

#include <bqaudioio/SystemAudioIO.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

namespace oboe {
class AudioStream;
}

/**
 * The audio device on Android, through Google's Oboe (AAudio): a
 * bqaudioio SystemAudioIO, as PortAudioIO is on the desktop, which
 * MainWindow::createAudioIO() installs; bqaudioio has no Android
 * backend. Built for Android only.
 *
 * A stereo output stream at the device's own rate and, given a record
 * target, a mono input stream at the same rate, read in the output
 * stream's callback (oboe::FullDuplexStream). Every callback hands the
 * target its input before it asks the source for output, as the
 * start-gap measurement assumes (recording.md, "Latency"). Without a
 * record target, or with the record side suppressed, the output runs
 * alone.
 *
 * FullDuplexStream spends its first 50 or so callbacks after each
 * start draining and discarding input, with the output silent, so that
 * input is read as soon as it comes in: output starts some 100 ms
 * after resume(), and the first input the target gets is from then.
 *
 * Latency, in frames at the device's rate like PortAudioIO's, is
 * worked out from the streams' timestamps (StreamLatency): when the
 * device is opened, for which the constructor runs the streams until
 * they have timestamps and leaves them suspended, and again each time
 * it is suspended after running, so that the next take is compensated
 * by what the device did last. Timestamps are read on the calling
 * thread, never in the callback.
 *
 * The route, the devices Android opened (the speaker and the phone's
 * microphone, a headset, Bluetooth) and how their streams were opened,
 * is looked up once, when they are, and logged: a round trip measured
 * through them is kept for that route (LatencyCalibration).
 *
 * A stream that fails (a device disconnected: headphones plugged in
 * or out) is stopped by Oboe; hasFailed() then says so, and the owner
 * must delete this and open another. Every method but the callback's
 * is for the GUI thread, which is the only one that logs, to stderr.
 */
class OboeAudioIO : public breakfastquay::SystemAudioIO,
                    public AudioRouteReporter
{
public:
    /**
     * Open the output and, if target is not null, the input. Check
     * isOK() afterwards: without the input it is false, and the caller
     * is expected to try again without a target, for playback only.
     */
    OboeAudioIO(breakfastquay::ApplicationRecordTarget *target,
                breakfastquay::ApplicationPlaybackSource *source);
    ~OboeAudioIO() override;

    bool isSourceOK() const override;
    bool isTargetOK() const override;
    double getCurrentTime() const override;

    void suspend() override;
    void resume() override;
    void suppressRecordSide(bool suppress) override;

    void setOutputGain(float gain) override;
    void setOutputBalance(float balance) override;

    /// Why a stream could not be opened, or "" if both were
    std::string getStartupError() const { return m_startupError; }

    /// Whether a stream has failed, so that this must be replaced
    bool hasFailed() const;

    /// The devices the streams were opened on, as Android's AudioManager
    /// names them, and how the streams were opened (the audio API, MMAP,
    /// sharing and performance mode, burst, buffer, input preset)
    AudioRoute::Route getAudioRoute() const override { return m_route; }

private:
    class Engine;
    class ErrorFlag;

    std::shared_ptr<oboe::AudioStream> m_output;
    std::shared_ptr<oboe::AudioStream> m_input;
    std::unique_ptr<Engine> m_engine;
    std::shared_ptr<ErrorFlag> m_errors;
    std::string m_startupError;

    std::chrono::steady_clock::time_point m_epoch;
    int m_rate;
    int m_sourceChannels;
    int m_outputChannels;
    int m_inputChannels;
    int m_maxFrames;
    int m_outBufferChannels;
    float **m_outBuffers;
    float **m_inBuffers;

    std::atomic<float> m_gain;
    std::atomic<float> m_balance;

    bool m_suspended;
    bool m_inputRunning;
    bool m_recordSuppressed;
    bool m_startFailed;
    StreamLatency::Estimate m_latency;
    int m_outputXRuns;
    AudioRoute::Route m_route;

    // The callback: the input first, then the output
    friend class Engine;
    void process(const float *input, int inputFrames,
                 float *output, int outputFrames);

    void stopStreams();
    bool waitUntilMeasurable(int maxMillis) const;
    bool measureLatency(StreamLatency::Estimate &latency) const;
    void report(StreamLatency::Estimate latency, bool withInput);
    void logStream(std::string name, oboe::AudioStream *stream) const;
    void findRoute();

    OboeAudioIO(const OboeAudioIO &) = delete;
    OboeAudioIO &operator=(const OboeAudioIO &) = delete;
};

#endif
