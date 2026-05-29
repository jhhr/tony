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

#include "bqfft/FFT.h"

#include <cmath>
#include <algorithm>
#include <iostream>

using namespace sv;
using namespace breakfastquay;
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
      m_timer(new QTimer(this)),
      m_fft(nullptr)
{
    // Poll for new audio every ~20 ms. Smaller interval reduces the lag
    // between the singer producing a note and the dot appearing on screen.
    m_timer->setInterval(20);
    connect(m_timer, &QTimer::timeout,
            this, &RealtimePitchTracker::pollAndProcess);
}

RealtimePitchTracker::~RealtimePitchTracker()
{
    stop();
    delete m_fft;
}

void
RealtimePitchTracker::start()
{
    m_nextFrameToProcess = 0;
    m_running = true;
    // Reset FFT so it is recreated fresh for the new recording's sample rate.
    delete m_fft;
    m_fft = nullptr;
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

    // Lag range: larger lag → lower frequency
    int minLag = std::max(1, (int)std::floor(sr / m_maxFreq));
    int maxLag = std::min(kWindowSize / 2 - 2, (int)std::ceil(sr / m_minFreq));

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

        // --- FFT-based YIN ---
        vector<double> diff;
        yinDifferenceFFT(raw, diff);
        yinCMND(diff);
        double lagSamples = yinFindPitch(diff, minLag, maxLag, m_threshold);

        double hz = 0.0;
        if (lagSamples > 0.0) {
            double candidate = sr / lagSamples;
            if (candidate >= m_minFreq && candidate <= m_maxFreq)
                hz = candidate;
        }

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
// YIN algorithm — FFT-accelerated difference function
// Reference: de Cheveigné & Kawahara, "YIN, a fundamental frequency
// estimator for speech and music", JASA 111(4), 2002.
// The difference function (Step 2) uses FFT-based autocorrelation
// (O(n log n)) instead of the direct sum (O(n²)).
// Steps 3–5 (CMND, absolute threshold, parabolic interpolation) are
// the same as the original formulation.
// ---------------------------------------------------------------------------

void
RealtimePitchTracker::yinDifferenceFFT(const vector<float> &buf,
                                        vector<double> &diff)
{
    // buf has size kWindowSize (= 2 * halfSize).
    // YIN treats the first halfSize samples as the "signal" and uses
    // lags 0..halfSize-1, requiring access up to buf[halfSize + tau].
    int frameSize = (int)buf.size();  // kWindowSize
    int halfSize  = frameSize / 2;    // yinBufferSize
    int fftBins   = halfSize + 1;     // complex bins from real FFT of frameSize

    // Lazy-create FFT (reused across hops — same size every call).
    if (!m_fft) {
        m_fft = new FFT(frameSize);
    }

    // --- Power terms (iterative, O(n)) ---
    // powerTerms[tau] = sum_{j=tau}^{halfSize+tau-1} buf[j]^2
    vector<double> powerTerms(halfSize);
    powerTerms[0] = 0.0;
    for (int j = 0; j < halfSize; ++j)
        powerTerms[0] += double(buf[j]) * double(buf[j]);
    for (int tau = 1; tau < halfSize; ++tau) {
        powerTerms[tau] = powerTerms[tau-1]
            - double(buf[tau-1])          * double(buf[tau-1])
            + double(buf[tau + halfSize]) * double(buf[tau + halfSize]);
    }

    // --- Forward FFT of the full input ---
    vector<float> audioReal(fftBins), audioImag(fftBins);
    m_fft->forward(buf.data(), audioReal.data(), audioImag.data());

    // --- Kernel: reversed first half, zero-padded to frameSize ---
    // Convolving x[0..frameSize-1] with this kernel gives the
    // YIN-style autocorrelation via the overlap at lag+halfSize-1.
    vector<float> kernel(frameSize, 0.0f);
    for (int j = 0; j < halfSize; ++j)
        kernel[j] = buf[halfSize - 1 - j];
    vector<float> kernelReal(fftBins), kernelImag(fftBins);
    m_fft->forward(kernel.data(), kernelReal.data(), kernelImag.data());

    // --- Complex multiply in frequency domain ---
    vector<float> acfReal(fftBins), acfImag(fftBins);
    for (int j = 0; j < fftBins; ++j) {
        acfReal[j] = audioReal[j]*kernelReal[j] - audioImag[j]*kernelImag[j];
        acfImag[j] = audioReal[j]*kernelImag[j] + audioImag[j]*kernelReal[j];
    }

    // --- Inverse FFT → time-domain autocorrelation ---
    vector<float> acfOut(frameSize);
    m_fft->inverse(acfReal.data(), acfImag.data(), acfOut.data());

    // bqfft inverse is unnormalized (unlike vamp FFT which divides by n).
    const double scale = 1.0 / frameSize;

    // --- Compute difference function ---
    // d[tau] = powerTerms[0] + powerTerms[tau] - 2*r[tau]
    // r[tau] lives at acfOut[tau + halfSize - 1] after the convolution.
    diff.assign(halfSize, 0.0);
    diff[0] = 0.0;
    for (int tau = 1; tau < halfSize; ++tau) {
        diff[tau] = powerTerms[0] + powerTerms[tau]
                    - 2.0 * double(acfOut[tau + halfSize - 1]) * scale;
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

