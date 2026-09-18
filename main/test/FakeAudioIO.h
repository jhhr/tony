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

#include <bqaudioio/SystemAudioIO.h>
#include <bqaudioio/ApplicationPlaybackSource.h>
#include <bqaudioio/ApplicationRecordTarget.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

class FakeAudioIO : public breakfastquay::SystemAudioIO
{
public:
    struct Config {
        int sampleRate = 44100;
        int blockSize = 512;
        int channels = 2;

        // Reported to the application, and nothing else: the delay the
        // input really has is inputDelay
        int recordLatency = 0;
        int playbackLatency = 0;

        // Mono input, delivered once and followed by silence. The
        // input clock restarts whenever the device is resumed
        std::vector<float> input;

        // Frames of silence before the input
        int inputDelay = 0;

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
        m_resumeCount(0)
    {
        m_source->setSystemPlaybackBlockSize(m_config.blockSize);
        m_source->setSystemPlaybackSampleRate(m_config.sampleRate);
        m_source->setSystemPlaybackChannelCount(m_config.channels);
        m_source->setSystemPlaybackLatency(m_config.playbackLatency);

        m_target->setSystemRecordBlockSize(m_config.blockSize);
        m_target->setSystemRecordSampleRate(m_config.sampleRate);
        m_target->setSystemRecordChannelCount(m_config.channels);
        m_target->setSystemRecordLatency(m_config.recordLatency);

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
    std::vector<float> m_captured;

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
            if (m_suspended) {
                // don't try to catch up on the time spent suspended
                next = steady_clock::now();
                continue;
            }
            process();
        }
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
                long j = base + i - m_config.inputDelay;
                if (j >= 0 && j < base) in[i] += m_captured[size_t(j)];
            }
        }

        bool kept = !m_config.inputIsKept || m_config.inputIsKept();
        long keptBefore = m_sinceResume;

        std::vector<const float *> inPtrs(ch, in.data());
        m_target->putSamples(inPtrs.data(), ch, n);
        if (kept) m_sinceResume += n;

        std::vector<std::vector<float>> out(ch, std::vector<float>(n, 0.f));
        std::vector<float *> outPtrs;
        for (auto &v : out) outPtrs.push_back(v.data());
        int got = m_source->getSourceSamples(outPtrs.data(), ch, n);

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
