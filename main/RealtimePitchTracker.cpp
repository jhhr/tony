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

#include "RealtimePitchTracker.h"

#include "data/model/SparseTimeValueModel.h"
#include "data/model/WritableWaveFileModel.h"
#include "data/model/Model.h"
#include "base/Event.h"

#include <cmath>
#include <algorithm>
#include <iostream>

using namespace sv;
using std::cerr;
using std::endl;
using std::vector;

RealtimePitchTracker::RealtimePitchTracker(ModelId audioSourceId,
                                           ModelId pitchModelId,
                                           QObject *parent)
    : QObject(parent),
      m_audioSourceId(audioSourceId),
      m_pitchModelId(pitchModelId),
      m_minFreq(60.0),
      m_maxFreq(1000.0),
      m_threshold(0.15),
      m_running(false),
      m_nextFrameToProcess(0),
      m_timer(new QTimer(this))
{
    // Poll for new audio every ~50 ms on the GUI thread.
    // At 44100 Hz this gives us roughly 2205 new samples per tick,
    // enough for 4+ YIN hops (kHopSize = 512).
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout,
            this, &RealtimePitchTracker::pollAndProcess);
}

RealtimePitchTracker::~RealtimePitchTracker()
{
    stop();
}

void
RealtimePitchTracker::start()
{
    m_nextFrameToProcess = 0;
    m_running = true;
    m_timer->start();
    cerr << "RealtimePitchTracker: started" << endl;
}

void
RealtimePitchTracker::stop()
{
    m_timer->stop();
    m_running = false;
    cerr << "RealtimePitchTracker: stopped (processed up to frame "
         << m_nextFrameToProcess << ")" << endl;
}

void
RealtimePitchTracker::pollAndProcess()
{
    if (!m_running) return;

    // --- Get the audio source model ---
    auto audioModel = ModelById::getAs<WritableWaveFileModel>(m_audioSourceId);
    if (!audioModel) {
        // The model may not be ready yet on the very first tick — that's fine.
        return;
    }

    sv_frame_t totalFrames = audioModel->getFrameCount();
    if (totalFrames < kWindowSize) {
        // Not enough data yet
        return;
    }

    // --- Get the pitch output model ---
    auto pitchModel = ModelById::getAs<SparseTimeValueModel>(m_pitchModelId);
    if (!pitchModel) return;

    double sr = audioModel->getSampleRate();

    // Process as many complete windows as are available, starting
    // from where we left off last time.
    sv_frame_t pos = m_nextFrameToProcess;

    while (pos + kWindowSize <= totalFrames) {

        // Fetch one window of audio from channel 0.
        // getData returns floatvec_t (std::vector with a custom allocator),
        // so we copy into a plain std::vector<float> for the YIN functions.
        floatvec_t rawFv = audioModel->getData(0, pos, kWindowSize);

        // getData may return fewer samples if the model hasn't flushed
        // the very last portion yet — skip rather than analyse garbage.
        if ((int)rawFv.size() < kWindowSize) break;

        std::vector<float> raw(rawFv.begin(), rawFv.end());

        double hz = yinPitch(raw, sr, m_minFreq, m_maxFreq, m_threshold);

        // Frame position of the centre of the analysis window
        sv_frame_t centreFrame = pos + kWindowSize / 2;

        if (hz > 0.0) {
            Event e(centreFrame, float(hz), tr(""));
            pitchModel->add(e);
            emit pitchDetected(centreFrame, hz);
        }

        pos += kHopSize;
    }

    m_nextFrameToProcess = pos;
}

// ---------------------------------------------------------------------------
// YIN algorithm
// Reference: de Cheveigné & Kawahara, "YIN, a fundamental frequency
// estimator for speech and music", JASA 111(4), 2002.
// We implement Steps 1–5 (difference function, cumulative mean
// normalised difference, absolute threshold + parabolic interpolation).
// ---------------------------------------------------------------------------

void
RealtimePitchTracker::yinDifference(const vector<float> &buf,
                                     vector<double> &diff)
{
    int windowSize = (int)buf.size();
    int halfSize   = windowSize / 2;
    diff.assign(halfSize, 0.0);

    // d[0] is defined as 0
    diff[0] = 0.0;

    for (int tau = 1; tau < halfSize; ++tau) {
        double sum = 0.0;
        for (int j = 0; j < halfSize; ++j) {
            double delta = double(buf[j]) - double(buf[j + tau]);
            sum += delta * delta;
        }
        diff[tau] = sum;
    }
}

void
RealtimePitchTracker::yinCMND(vector<double> &diff)
{
    int halfSize = (int)diff.size();
    if (halfSize == 0) return;

    diff[0] = 1.0;  // by convention

    double runningSum = 0.0;
    for (int tau = 1; tau < halfSize; ++tau) {
        runningSum += diff[tau];
        if (runningSum > 0.0) {
            diff[tau] = diff[tau] * double(tau) / runningSum;
        } else {
            diff[tau] = 1.0;
        }
    }
}

double
RealtimePitchTracker::yinFindPitch(const vector<double> &cmnd,
                                    int minLag, int maxLag,
                                    double threshold)
{
    int halfSize = (int)cmnd.size();
    if (maxLag >= halfSize - 1) maxLag = halfSize - 2;
    if (minLag < 1)             minLag = 1;
    if (minLag >= maxLag)       return -1.0;

    // Walk forward from minLag looking for the first value below threshold
    // that is also a local minimum.
    int bestTau = -1;
    for (int tau = minLag; tau <= maxLag; ++tau) {
        if (cmnd[tau] < threshold) {
            // Walk to the bottom of the dip
            while (tau + 1 <= maxLag && cmnd[tau + 1] < cmnd[tau]) {
                ++tau;
            }
            bestTau = tau;
            break;
        }
    }

    if (bestTau < 1 || bestTau >= halfSize - 1) {
        return -1.0;  // unvoiced
    }

    // Parabolic interpolation around the minimum
    double s0 = cmnd[bestTau - 1];
    double s1 = cmnd[bestTau];
    double s2 = cmnd[bestTau + 1];

    double denom = 2.0 * (2.0 * s1 - s2 - s0);
    double refined;
    if (std::abs(denom) < 1e-12) {
        refined = double(bestTau);
    } else {
        refined = double(bestTau) + (s2 - s0) / denom;
    }

    return refined;
}

double
RealtimePitchTracker::yinPitch(const vector<float> &window,
                                double sr,
                                double minFreq, double maxFreq,
                                double thresh)
{
    int halfSize = (int)window.size() / 2;

    // Lag range: larger lag → lower frequency
    int minLag = std::max(1, (int)std::floor(sr / maxFreq));
    int maxLag = std::min(halfSize - 2, (int)std::ceil(sr / minFreq));

    if (minLag >= maxLag) return 0.0;

    vector<double> diff;
    yinDifference(window, diff);
    yinCMND(diff);

    double lagSamples = yinFindPitch(diff, minLag, maxLag, thresh);
    if (lagSamples <= 0.0) return 0.0;

    double hz = sr / lagSamples;

    // Final range check (parabolic interpolation can push slightly out)
    if (hz < minFreq || hz > maxFreq) return 0.0;

    return hz;
}