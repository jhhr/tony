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

#include "data/model/WritableWaveFileModel.h"
#include "data/model/Model.h"

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
                                           QObject *parent)
    : QThread(parent),
      m_audioSourceId(audioSourceId),
      m_minFreq(60.0),
      m_maxFreq(1000.0),
      m_threshold(0.15)
{
}

RealtimePitchTracker::~RealtimePitchTracker()
{
    stop();
}

void
RealtimePitchTracker::start()
{
    QThread::start();
}

void
RealtimePitchTracker::stop()
{
    requestInterruption();
    wait();
}

void
RealtimePitchTracker::run()
{
    FFT *fft = nullptr;
    sv_frame_t nextFrameToProcess = 0;

    cerr << "RealtimePitchTracker: background thread started" << endl;

    while (!isInterruptionRequested()) {

        auto audioModel = ModelById::getAs<WritableWaveFileModel>(m_audioSourceId);
        if (!audioModel) {
            msleep(5);
            continue;
        }

        sv_frame_t totalFrames = audioModel->getFrameCount();
        if (totalFrames < kWindowSize) {
            msleep(5);
            continue;
        }

        double sr = audioModel->getSampleRate();

        if (!fft) {
            fft = new FFT(kWindowSize);
        }

        int minLag = std::max(1, (int)std::floor(sr / m_maxFreq));
        int maxLag = std::min(kWindowSize / 2 - 2, (int)std::ceil(sr / m_minFreq));

        bool processedAny = false;

        while (nextFrameToProcess + kWindowSize <= totalFrames) {

            floatvec_t rawFv = audioModel->getData(0, nextFrameToProcess, kWindowSize);

            if ((int)rawFv.size() < kWindowSize) break;

            vector<float> raw(rawFv.begin(), rawFv.end());

            vector<double> diff;
            yinDifferenceFFT(raw, diff, fft);
            yinCMND(diff);
            double lagSamples = yinFindPitch(diff, minLag, maxLag, m_threshold);

            if (lagSamples > 0.0) {
                double hz = sr / lagSamples;
                if (hz >= m_minFreq && hz <= m_maxFreq) {
                    sv_frame_t centreFrame = nextFrameToProcess + kWindowSize / 2;
                    emit pitchDetected(centreFrame, hz);
                    processedAny = true;
                }
            }

            nextFrameToProcess += kHopSize;
        }

        // Sleep only when no new hops were available; otherwise spin
        // immediately to drain any remaining frames.
        if (!processedAny) {
            msleep(5);
        }
    }

    delete fft;
    cerr << "RealtimePitchTracker: background thread stopped" << endl;
}

// ---------------------------------------------------------------------------
// YIN algorithm — FFT-accelerated difference function
// Reference: de Cheveigné & Kawahara, "YIN, a fundamental frequency
// estimator for speech and music", JASA 111(4), 2002.
// ---------------------------------------------------------------------------

void
RealtimePitchTracker::yinDifferenceFFT(const vector<float> &buf,
                                        vector<double> &diff,
                                        FFT *fft)
{
    int frameSize = (int)buf.size();
    int halfSize  = frameSize / 2;
    int fftBins   = halfSize + 1;

    // --- Power terms (iterative, O(n)) ---
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
    fft->forward(buf.data(), audioReal.data(), audioImag.data());

    // --- Kernel: reversed first half, zero-padded to frameSize ---
    vector<float> kernel(frameSize, 0.0f);
    for (int j = 0; j < halfSize; ++j)
        kernel[j] = buf[halfSize - 1 - j];
    vector<float> kernelReal(fftBins), kernelImag(fftBins);
    fft->forward(kernel.data(), kernelReal.data(), kernelImag.data());

    // --- Complex multiply ---
    vector<float> acfReal(fftBins), acfImag(fftBins);
    for (int j = 0; j < fftBins; ++j) {
        acfReal[j] = audioReal[j]*kernelReal[j] - audioImag[j]*kernelImag[j];
        acfImag[j] = audioReal[j]*kernelImag[j] + audioImag[j]*kernelReal[j];
    }

    // --- Inverse FFT ---
    vector<float> acfOut(frameSize);
    fft->inverse(acfReal.data(), acfImag.data(), acfOut.data());

    // bqfft inverse is unnormalized — divide by frameSize.
    const double scale = 1.0 / frameSize;

    diff.assign(halfSize, 0.0);
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

    diff[0] = 1.0;

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

    int bestTau = -1;
    for (int tau = minLag; tau <= maxLag; ++tau) {
        if (cmnd[tau] < threshold) {
            while (tau + 1 <= maxLag && cmnd[tau + 1] < cmnd[tau]) {
                ++tau;
            }
            bestTau = tau;
            break;
        }
    }

    if (bestTau < 1 || bestTau >= halfSize - 1) {
        return -1.0;
    }

    double s0 = cmnd[bestTau - 1];
    double s1 = cmnd[bestTau];
    double s2 = cmnd[bestTau + 1];

    double denom = 2.0 * (2.0 * s1 - s2 - s0);
    if (std::abs(denom) < 1e-12) return double(bestTau);
    return double(bestTau) + (s2 - s0) / denom;
}
