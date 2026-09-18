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
 * pitch estimates are reported via pitchDetected() signals; the
 * connection to the GUI thread is automatically a QueuedConnection so
 * the slot (which writes to the model and updates the status bar) runs
 * safely on the GUI thread without blocking audio or rendering.
 *
 * Usage:
 *   1. Create a RealtimePitchTracker with the ModelId of the
 *      WritableWaveFileModel being recorded into.
 *   2. Call start() — the background thread starts immediately.
 *   3. Call stop() when recording ends — blocks until the thread exits.
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

signals:
    /**
     * Emitted from the background thread each time a new voiced pitch
     * estimate is available. Via Qt::AutoConnection this arrives in the
     * GUI thread's event loop (QueuedConnection cross-thread).
     *
     * @param frame  Centre frame of the analysis window.
     * @param hz     Pitch in Hz (always > 0 when emitted).
     */
    void pitchDetected(sv::sv_frame_t frame, double hz);

protected:
    /** The background polling loop — do not call directly. */
    void run() override;

private:
    friend class TestRealtimeYin;

    sv::ModelId     m_audioSourceId;

    double          m_minFreq;
    double          m_maxFreq;
    double          m_threshold;

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
