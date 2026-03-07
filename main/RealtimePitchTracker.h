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

#include <QObject>
#include <QTimer>

#include <vector>

#include "base/BaseTypes.h"
#include "data/model/Model.h"
#include "data/model/SparseTimeValueModel.h"

namespace sv {
class WritableWaveFileModel;
}

/**
 * RealtimePitchTracker polls a WritableWaveFileModel (the model being
 * filled during a live microphone recording) for new audio samples and
 * estimates pitch in real time using a simplified YIN autocorrelation
 * algorithm.
 *
 * Results are written into a SparseTimeValueModel so they can be
 * displayed immediately as a TimeValueLayer overlaid on the main pane,
 * giving the singer real-time visual feedback about their pitch.
 *
 * This is intentionally a low-latency, lower-accuracy alternative to
 * the full pYIN analysis that will be run once recording is complete.
 *
 * Usage:
 *   1. Create a RealtimePitchTracker, providing the ModelId of both:
 *        - the WritableWaveFileModel being recorded into (audio source),
 *        - the SparseTimeValueModel to write pitch estimates into.
 *   2. Call start() when recording begins.
 *   3. The tracker polls automatically via an internal QTimer.
 *   4. Call stop() when recording ends.
 */
class RealtimePitchTracker : public QObject
{
    Q_OBJECT

public:
    /**
     * Construct a tracker.
     *
     * @param audioSourceId  ModelId of the WritableWaveFileModel being
     *                       recorded into. The tracker polls this for
     *                       new samples on each timer tick.
     * @param pitchModelId   ModelId of the SparseTimeValueModel to write
     *                       pitch estimates into.
     * @param parent         Optional Qt parent.
     */
    RealtimePitchTracker(sv::ModelId audioSourceId,
                         sv::ModelId pitchModelId,
                         QObject *parent = nullptr);

    virtual ~RealtimePitchTracker();

    /**
     * Start tracking. Resets all internal state.
     * Must be called from the GUI thread.
     */
    void start();

    /**
     * Stop tracking. No more pitch estimates will be written after
     * this returns.
     * Must be called from the GUI thread.
     */
    void stop();

    /**
     * Minimum frequency (Hz) that the tracker will report.
     * Pitches below this are treated as unvoiced. Default: 60 Hz.
     */
    void setMinFrequency(double hz) { m_minFreq = hz; }
    double getMinFrequency() const  { return m_minFreq; }

    /**
     * Maximum frequency (Hz) that the tracker will report.
     * Pitches above this are treated as unvoiced. Default: 1000 Hz.
     */
    void setMaxFrequency(double hz) { m_maxFreq = hz; }
    double getMaxFrequency() const  { return m_maxFreq; }

    /**
     * YIN threshold. Lower values are more selective (fewer voiced
     * detections), higher values yield more detections but more
     * errors. Default: 0.15.
     */
    void setThreshold(double t) { m_threshold = t; }
    double getThreshold() const { return m_threshold; }

signals:
    /**
     * Emitted each time a new pitch estimate is available.
     *
     * @param frame  Sample frame at which the pitch was estimated
     *               (centre of the analysis window), relative to the
     *               start of the recording.
     * @param hz     Estimated pitch in Hz, or 0 if unvoiced.
     */
    void pitchDetected(sv::sv_frame_t frame, double hz);

private slots:
    /// Called by the internal QTimer; polls the audio model and runs YIN.
    void pollAndProcess();

private:
    // --- Model IDs ---
    sv::ModelId         m_audioSourceId;  // WritableWaveFileModel being recorded
    sv::ModelId         m_pitchModelId;   // SparseTimeValueModel for output

    // --- Configuration ---
    double              m_minFreq;
    double              m_maxFreq;
    double              m_threshold;

    // --- State ---
    bool                m_running;

    /// How many input frames we have already processed (exclusive end
    /// of the last complete hop).  We use this to avoid re-processing
    /// samples on the next timer tick.
    sv::sv_frame_t      m_nextFrameToProcess;

    // --- Processing parameters ---
    // Window size and hop size in samples.
    // At 44 100 Hz: window ≈ 46 ms, hop ≈ 12 ms.
    static const int    kWindowSize = 2048;
    static const int    kHopSize    = 512;

    // --- Timer ---
    QTimer             *m_timer;

    // --- YIN helpers ---

    /**
     * Run the YIN algorithm on a single window of audio and return
     * the estimated fundamental frequency in Hz, or 0 if unvoiced.
     */
    static double yinPitch(const std::vector<float> &window,
                           double sr,
                           double minFreq, double maxFreq,
                           double thresh);

    /**
     * Step 2: difference function.
     * d[tau] = sum_{j=0}^{W/2-1} (x[j] - x[j+tau])^2
     */
    static void yinDifference(const std::vector<float> &buf,
                               std::vector<double> &diff);

    /**
     * Step 3: cumulative mean normalised difference (in-place).
     */
    static void yinCMND(std::vector<double> &diff);

    /**
     * Steps 4-5: find first dip below threshold with parabolic
     * interpolation.  Returns fractional lag in samples, or -1 if
     * no dip found.
     */
    static double yinFindPitch(const std::vector<double> &cmnd,
                                int minLag, int maxLag,
                                double threshold);
};

#endif // REALTIME_PITCH_TRACKER_H