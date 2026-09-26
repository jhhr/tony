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

#ifndef TEST_FAKE_AUDIO_IO_H
#define TEST_FAKE_AUDIO_IO_H

// A duplex audio device with no hardware behind it. A worker thread
// runs the callback in real time, as a driver would: each block it
// pushes a programmed input and then pulls the application's output,
// in that order, like PortAudioIO and JACKAudioIO.
//
// The device reports whatever latencies the test asks for, so the
// application computes a known compensation, and the input can be
// made to arrive late by exactly that much.

#include "../AudioRoute.h"

#include <bqaudioio/SystemAudioIO.h>
#include <bqaudioio/ApplicationPlaybackSource.h>
#include <bqaudioio/ApplicationRecordTarget.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

class FakeAudioIO : public breakfastquay::SystemAudioIO,
                    public AudioRouteReporter
{
public:
    struct Config {
        int sampleRate = 44100;
        int blockSize = 512;
        int channels = 2;

        // The input's channels, where they are not as many as the
        // output's: a phone records one (OboeAudioIO) and plays two. -1
        // for as many as channels
        int inputChannels = -1;

        // Reported to the application, and nothing else: the delay the
        // input really has is inputDelay
        int recordLatency = 0;
        int playbackLatency = 0;

        // Added to the reported record latency at every resume after the
        // first: a device that measures its latencies at each start, as
        // Oboe does from its timestamps, reports others every time. The
        // input's real delay stays inputDelay
        int recordLatencyStep = 0;

        // The route reported to the application, as OboeAudioIO reports
        // the one Android opened; with no driver, none, as PortAudioIO
        AudioRoute::Route route;

        // Mono input, delivered once and followed by silence. The
        // input clock restarts whenever the device is resumed
        std::vector<float> input;

        // Frames of silence before the input
        int inputDelay = 0;

        // The one channel the input arrives on, the others silent, as a
        // microphone on input 2 of an interface is channel 1; -1 for
        // every channel
        int inputChannel = -1;

        // Start the input clock at the first audible output sample
        // instead of at resume. With inputDelay equal to the reported
        // round trip, this is a singer who is exactly on time.
        // inputDelay must be at least a block, because the input of
        // the block in which playback starts has already gone
        bool inputFollowsPlayback = false;

        // Add the output to the input, inputDelay frames late:
        // speakers bleeding into the microphone. Again inputDelay
        // must be at least a block
        bool loopback = false;

        // A second arrival of the loopback, echoDelay frames after the
        // first, at echoGain times its level: the input played back out
        // somewhere (Windows' "Listen to this device") and heard again.
        // No second arrival while echoGain is 0
        int echoDelay = 0;
        float echoGain = 0.f;

        // How far the loopback (and its echo) moves at each resume after
        // the first, in frames: restartShift early, then late, then on
        // time, and so on, as a real stream's input moves against its
        // output by several ms each time it starts. inputDelay less this
        // must still be at least a block. 0 for none
        int restartShift = 0;

        // Tell the application the peak of each block's input and output,
        // left and right, as PortAudioIO does for its level meters
        bool reportLevels = false;

        // The device opens, and suspends and resumes as asked, but never
        // calls back: no input comes in and no output is asked for, as
        // with a driver whose stream starts and then delivers nothing
        bool neverCallsBack = false;

        // Whether the application keeps the input it is given just
        // now. It discards input until its recording file is open,
        // which is some time after it resumes the device. If unset,
        // all input counts as kept
        std::function<bool()> inputIsKept;
    };

    FakeAudioIO(breakfastquay::ApplicationRecordTarget *target,
                breakfastquay::ApplicationPlaybackSource *source,
                Config config) :
        SystemAudioIO(target, source),
        m_config(config),
        m_suspended(true),
        m_stop(false),
        m_resumeFrame(0),
        m_frames(0),
        m_playStartFrame(-1),
        m_sinceResume(0),
        m_framesBeforePlayStart(-1),
        m_resumeCount(0),
        m_loopbackDelay(config.inputDelay)
    {
        m_source->setSystemPlaybackBlockSize(m_config.blockSize);
        m_source->setSystemPlaybackSampleRate(m_config.sampleRate);
        m_source->setSystemPlaybackChannelCount(m_config.channels);
        m_source->setSystemPlaybackLatency(m_config.playbackLatency);

        m_target->setSystemRecordBlockSize(m_config.blockSize);
        m_target->setSystemRecordSampleRate(m_config.sampleRate);
        m_target->setSystemRecordChannelCount(inputChannelCount());
        m_target->setSystemRecordLatency(m_config.recordLatency);
        m_reportedRecordLatency = m_config.recordLatency;

        m_thread = std::thread([this]() { run(); });
    }

    ~FakeAudioIO() override {
        m_stop = true;
        m_thread.join();
    }

    bool isSourceOK() const override { return true; }
    bool isTargetOK() const override { return true; }

    double getCurrentTime() const override {
        return double(m_frames.load()) / m_config.sampleRate;
    }

    void suppressRecordSide(bool) override { }

    AudioRoute::Route getAudioRoute() const override { return m_config.route; }

    // No callback is running, or will start, once this returns
    void suspend() override {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_suspended = true;
    }

    void resume() override {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_suspended) return;
        m_suspended = false;
        m_resumeFrame = long(m_captured.size());
        m_playStartFrame = -1;
        m_sinceResume = 0;
        m_framesBeforePlayStart = -1;
        ++m_resumeCount;
        // No callback runs while suspended, and none has started yet
        if (m_config.recordLatencyStep != 0 && m_resumeCount > 1) {
            m_reportedRecordLatency += m_config.recordLatencyStep;
            m_target->setSystemRecordLatency(m_reportedRecordLatency);
        }
        m_loopbackDelay = m_config.inputDelay + restartOffset(m_resumeCount);
    }

    bool isSuspended() const {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_suspended;
    }

    int getResumeCount() const {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_resumeCount;
    }

    /** Everything the application has played, mixed to mono. */
    std::vector<float> getCapturedOutput() const {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_captured;
    }

    /**
     * Index into the captured output of the first audible sample
     * since the last resume, or -1 if there has been none.
     */
    long getPlayStartFrame() const {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_playStartFrame;
    }

    /**
     * Number of input frames the application kept between the last
     * resume and the first audible output sample, or -1: how far into
     * the take the reference started.
     */
    long getFramesBeforePlayStart() const {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_framesBeforePlayStart;
    }

private:
    Config m_config;
    mutable std::mutex m_mutex;
    std::thread m_thread;
    bool m_suspended;
    std::atomic<bool> m_stop;
    long m_resumeFrame;
    std::atomic<long> m_frames;
    long m_playStartFrame;
    long m_sinceResume;
    long m_framesBeforePlayStart;
    int m_resumeCount;
    int m_reportedRecordLatency = 0;
    int m_loopbackDelay;
    std::vector<float> m_captured;

    // Where the loopback lands after the given number of starts, against
    // inputDelay: on time at the first, then -, +, 0 times the shift
    int restartOffset(int starts) const {
        if (starts < 2) return 0;
        static const int cycle[] = { -1, 1, 0 };
        return cycle[(starts - 2) % 3] * m_config.restartShift;
    }

    void run() {
        using namespace std::chrono;
        auto period = duration_cast<steady_clock::duration>
            (duration<double>(double(m_config.blockSize) /
                              m_config.sampleRate));
        auto next = steady_clock::now();
        while (!m_stop) {
            next += period;
            std::this_thread::sleep_until(next);
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_suspended || m_config.neverCallsBack) {
                // don't try to catch up on the time spent suspended
                next = steady_clock::now();
                continue;
            }
            process();
        }
    }

    int inputChannelCount() const {
        return m_config.inputChannels > 0 ? m_config.inputChannels
                                          : m_config.channels;
    }

    static float peak(const float *samples, int count) {
        float p = 0.f;
        for (int i = 0; i < count; ++i) p = std::max(p, std::fabs(samples[i]));
        return p;
    }

    // The input at a position in the captured output's timeline
    float inputAt(long frame) const {
        long origin = m_resumeFrame;
        if (m_config.inputFollowsPlayback) {
            if (m_playStartFrame < 0) return 0.f;
            origin = m_playStartFrame;
        }
        long i = frame - origin - m_config.inputDelay;
        if (i < 0 || i >= long(m_config.input.size())) return 0.f;
        return m_config.input[size_t(i)];
    }

    void process() {
        const int n = m_config.blockSize;
        const int ch = m_config.channels;
        const long base = long(m_captured.size());

        std::vector<float> in(n, 0.f);
        for (int i = 0; i < n; ++i) {
            in[i] = inputAt(base + i);
            if (m_config.loopback) {
                long j = base + i - m_loopbackDelay;
                if (j >= 0 && j < base) in[i] += m_captured[size_t(j)];
                if (m_config.echoGain != 0.f) {
                    j -= m_config.echoDelay;
                    if (j >= 0 && j < base) {
                        in[i] += m_config.echoGain * m_captured[size_t(j)];
                    }
                }
            }
        }

        bool kept = !m_config.inputIsKept || m_config.inputIsKept();
        long keptBefore = m_sinceResume;

        const int inCh = inputChannelCount();
        std::vector<float> silence(n, 0.f);
        std::vector<const float *> inPtrs(inCh, in.data());
        if (m_config.inputChannel >= 0) {
            for (int c = 0; c < inCh; ++c) {
                if (c != m_config.inputChannel) inPtrs[c] = silence.data();
            }
        }
        m_target->putSamples(inPtrs.data(), inCh, n);
        if (kept) m_sinceResume += n;
        if (m_config.reportLevels) {
            m_target->setInputLevels(peak(inPtrs[0], n),
                                     peak(inPtrs[inCh > 1 ? 1 : 0], n));
        }

        std::vector<std::vector<float>> out(ch, std::vector<float>(n, 0.f));
        std::vector<float *> outPtrs;
        for (auto &v : out) outPtrs.push_back(v.data());
        int got = m_source->getSourceSamples(outPtrs.data(), ch, n);
        if (m_config.reportLevels) {
            m_source->setOutputLevels(peak(outPtrs[0], got),
                                      peak(outPtrs[ch > 1 ? 1 : 0], got));
        }

        for (int i = 0; i < n; ++i) {
            float mix = 0.f;
            if (i < got) {
                for (int c = 0; c < ch; ++c) mix += out[c][i];
                mix /= float(ch);
            }
            m_captured.push_back(mix);
            if (m_playStartFrame < 0 && std::fabs(mix) > 1e-4f) {
                m_playStartFrame = base + i;
                m_framesBeforePlayStart = keptBefore + (kept ? i : 0);
            }
        }

        m_frames += n;
    }
};

#endif
