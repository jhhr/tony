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

#ifndef REALTIME_PITCH_TRACKER_H
#define REALTIME_PITCH_TRACKER_H

#include <QThread>

#include <atomic>
#include <mutex>
#include <vector>

#include "base/BaseTypes.h"
#include "data/model/Model.h"

namespace sv {
class WritableWaveFileModel;
}

namespace breakfastquay {
class FFT;
}

/**
 * RealtimePitchTracker runs on a dedicated background QThread and
 * continuously polls a WritableWaveFileModel for new audio samples,
 * estimating pitch in real time using FFT-accelerated YIN.
 *
 * An estimate an octave from the ones either side of it is taken for
 * YIN's octave slip and dropped (OctaveSlips): a run an octave off is
 * held back until the hops after it say which it was.
 *
 * The estimates are kept until the GUI thread takes them, all at once
 * (takeEstimates()): there is one for each hop, about 170 a second, and
 * a signal for each would queue a call on the GUI thread that a slow
 * GUI thread falls behind with for good (LiveDotsFeed).
 *
 * Usage:
 *   1. Create a RealtimePitchTracker with the ModelId of the
 *      WritableWaveFileModel being recorded into.
 *   2. Call start() — the background thread starts immediately.
 *   3. Take what it has found with takeEstimates(), from any thread.
 *   4. Call stop() when recording ends — blocks until the thread exits.
 */
class RealtimePitchTracker : public QThread
{
    Q_OBJECT

public:
    // Window size: 2048 samples @ 44100 Hz ≈ 46 ms.
    // Hop size: 256 samples ≈ 5.8 ms. One estimate per hop, so this is
    // also the resolution of the model the estimates go into.
    static constexpr int kWindowSize = 2048;
    static constexpr int kHopSize    = 256;

    /**
     * @param audioSourceId  ModelId of the WritableWaveFileModel being
     *                       recorded into. Polled from the background thread.
     * @param parent         Optional Qt parent (must live on GUI thread).
     */
    RealtimePitchTracker(sv::ModelId audioSourceId,
                         QObject *parent = nullptr);

    virtual ~RealtimePitchTracker();

    /**
     * Start the background polling thread.
     * Must be called from the GUI thread.
     */
    void start();

    /**
     * Request the background thread to stop and block until it exits.
     * Must be called from the GUI thread.
     */
    void stop();

    /** Minimum frequency (Hz) reported. Default: 60 Hz. */
    void setMinFrequency(double hz) { m_minFreq = hz; }
    double getMinFrequency() const  { return m_minFreq; }

    /** Maximum frequency (Hz) reported. Default: 1000 Hz. */
    void setMaxFrequency(double hz) { m_maxFreq = hz; }
    double getMaxFrequency() const  { return m_maxFreq; }

    /** YIN threshold (0–1). Default: 0.15. */
    void setThreshold(double t) { m_threshold = t; }
    double getThreshold() const { return m_threshold; }

    /**
     * The level a window must reach to be given a pitch, in dBFS: the
     * level() of its first half, which YIN's difference function compares
     * with the window further on, so that the pitch it finds is the first
     * half's. The whole window's would let a window through whose second
     * half reaches into a sound, with the pitch of the quiet before it.
     *
     * YIN is blind to level: it finds a pitch now and then in a quiet
     * steady sound, as it did in the fans a user's microphone heard at
     * -66.5 dBFS between the sounds, 25 dB below the quietest window of
     * a tone it heard (docs/audio-drivers.md, §7). pYIN, as Tony runs
     * it, penalises such soft pitches too.
     */
    static constexpr double kMinLevel = -60.0;

    /** The level floor in dBFS. Default: kMinLevel. */
    void setMinLevel(double dbfs) { m_minLevel = dbfs; }
    double getMinLevel() const { return m_minLevel; }

    /**
     * The one channel of the recording to track, counting from 0, as
     * the take is made from it (InputChannel), or -1 for the mixdown of
     * them all. A channel the recording does not have is the mixdown.
     * Default: -1. Before start(): the thread reads it as a plain value.
     */
    void setChannel(int channel) { m_channel = channel; }
    int getChannel() const { return m_channel; }

    /**
     * The level of \a count frames of the mixdown of \a channels
     * channels, in dBFS: the RMS of their average. The mixdown is their
     * sum, so a microphone on two inputs would read 6 dB louder than on
     * one; the average reads what each input has (and a microphone on
     * one input of two 6 dB below it). Silence reads -200.
     */
    static double level(const float *mixdown, int count, int channels);

    /** A voiced pitch estimate. */
    struct Estimate {
        sv::sv_frame_t frame;   ///< centre frame of the analysis window
        double hz;              ///< always > 0
    };
    typedef std::vector<Estimate> Estimates;

    /**
     * The estimates found since the last call, oldest first; they are
     * not kept any longer. Any thread.
     */
    Estimates takeEstimates();

    /**
     * The frame of the recording the tracker has analysed up to: the
     * end of its latest window, voiced or not. Any thread.
     */
    sv::sv_frame_t getFramesAnalysed() const { return m_framesAnalysed; }

protected:
    /** The background polling loop — do not call directly. */
    void run() override;

private:
    friend class TestRealtimeYin;

    sv::ModelId     m_audioSourceId;

    double          m_minFreq;
    double          m_maxFreq;
    double          m_threshold;
    double          m_minLevel;
    int             m_channel;

    std::mutex      m_estimatesMutex;
    Estimates       m_estimates;
    std::atomic<sv::sv_frame_t> m_framesAnalysed;

    // --- YIN helpers (all called only from run()) ---

    static void yinDifferenceFFT(const std::vector<float> &buf,
                                  std::vector<double> &diff,
                                  breakfastquay::FFT *fft);

    static void yinCMND(std::vector<double> &diff);

    static double yinFindPitch(const std::vector<double> &cmnd,
                                int minLag, int maxLag,
                                double threshold);
};

#endif // REALTIME_PITCH_TRACKER_H
