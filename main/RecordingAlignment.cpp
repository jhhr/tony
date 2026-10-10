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

#include "RecordingAlignment.h"

#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"

#include <bqfft/FFT.h>

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

using namespace sv;

namespace RecordingAlignment {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("RecordingAlignment", text);
}

// -100 dBFS: no microphone's noise is this quiet for a moment, and a
// radio link that drops out gives exact zeros
const float silentLevel = 1e-5f;

// Frames read from a file at once
const sv_frame_t blockFrames = 65536;

// The coarse stage's candidates, at least this many hops apart. Several,
// as the recording may hold other singings of the same song, whose
// levels rise and fall much as the take's do
const int candidateCount = 16;
const int candidateSpacingHops = 10;

// The fine stage: the take's loudest stretch this long, against the
// recording this many hops either side of each candidate (the coarse
// stage is good to half a hop or so, its levels smoothed by a hop)
const double anchorSeconds = 0.5;
const double fineReachHops = 2.5;

// The walk: pieces this long, end to end over the whole range, so that
// a punch-in half a second long is two of them. Each is looked for this
// far either side of the offset of the last that matched: a tenth of a
// period of the sung fundamental or less, as another singing of a held
// note finds a match within half a period of anywhere, and the same
// singing needs none. After pieces that did not match (a long dropout,
// another session) the reach grows by as much as two clocks 100 ppm
// apart drift since, to at most 2 ms
const double pieceSeconds = 0.25;
const double walkReachSeconds = 0.0005;
const double driftPerSecond = 100e-6;
const double maxReachSeconds = 0.002;

// A piece counts if this much of it is no dropout, and its level is
// within this of the take's loud level (the 90th percentile of its
// hops'): a piece of silence has nothing to be alike in
const double presentFraction = 0.5;
const double quietDb = 30.0;

// The pieces near each end whose offsets are that end's
const int endPieces = 3;

// Ends nearer than this cannot say how far the clocks drifted
const double endsApartSeconds = 1.0;

// The first difference, nearly: weighs the detail of the waveform over
// its broad swings
const double emphasis = 0.95;

class WavSource : public Source
{
public:
    WavSource(QString path) : m_reader(FileSource(path)) { }

    bool isOK() const {
        return m_reader.isOK() && m_reader.getChannelCount() > 0 &&
            m_reader.getSampleRate() > 0;
    }
    QString error() const { return m_reader.getError(); }

    double rate() const override { return m_reader.getSampleRate(); }
    sv_frame_t frames() const override { return m_reader.getFrameCount(); }

    std::vector<float> read(sv_frame_t from, sv_frame_t count) const override {
        std::vector<float> out(size_t(std::max<sv_frame_t>(count, 0)), 0.f);
        const sv_frame_t start = std::max<sv_frame_t>(from, 0);
        const sv_frame_t end = std::min(frames(), from + count);
        if (end <= start) return out;
        const int channels = m_reader.getChannelCount();
        const floatvec_t data = m_reader.getInterleavedFrames
            (start, end - start);
        const sv_frame_t got = sv_frame_t(data.size()) / channels;
        for (sv_frame_t i = 0; i < got; ++i) {
            float sum = 0.f;
            for (int c = 0; c < channels; ++c) {
                sum += data[size_t(i * channels + c)];
            }
            out[size_t(start - from + i)] = sum / float(channels);
        }
        return out;
    }

private:
    WavFileReader m_reader;
};

// The recording at the take's rate: element k is the recording at the
// take's frame start + k, read at its own rate and interpolated where
// the two differ
std::vector<double> readAt(const Source &recording, double ratio,
                           sv_frame_t start, sv_frame_t count)
{
    std::vector<double> out(size_t(count), 0.0);
    if (count <= 0) return out;
    if (ratio == 1.0) {
        const std::vector<float> v = recording.read(start, count);
        for (size_t k = 0; k < out.size(); ++k) out[k] = v[k];
        return out;
    }
    const sv_frame_t first = sv_frame_t(std::floor(double(start) * ratio));
    const sv_frame_t last =
        sv_frame_t(std::floor(double(start + count - 1) * ratio)) + 2;
    const std::vector<float> v = recording.read(first, last - first);
    for (sv_frame_t k = 0; k < count; ++k) {
        const double pos = double(start + k) * ratio - double(first);
        const sv_frame_t i = sv_frame_t(std::floor(pos));
        const double frac = pos - double(i);
        if (i < 0 || i + 1 >= sv_frame_t(v.size())) continue;
        out[size_t(k)] = v[size_t(i)] * (1.0 - frac) + v[size_t(i + 1)] * frac;
    }
    return out;
}

// Pre-emphasised, the first sample of which has nothing before it and
// is 0
std::vector<double> emphasised(const std::vector<double> &x)
{
    std::vector<double> e(x.size(), 0.0);
    for (size_t i = 1; i < x.size(); ++i) e[i] = x[i] - emphasis * x[i - 1];
    return e;
}

// Where a pre-emphasised piece of the take counts: neither its sample
// nor the one before in a dropout
std::vector<double> emphasisMask(const std::vector<char> &gaps,
                                 sv_frame_t start, sv_frame_t count)
{
    std::vector<double> mask(size_t(count), 0.0);
    for (sv_frame_t i = 1; i < count; ++i) {
        mask[size_t(i)] = (gaps[size_t(start + i)] || gaps[size_t(start + i - 1)])
            ? 0.0 : 1.0;
    }
    return mask;
}

sv_frame_t hopStart(sv_frame_t k, double hop)
{
    return sv_frame_t(std::llround(double(k) * hop));
}

// The RMS level of each whole hop of the source, read a block at a time
// from start to end; false if progress gave up
bool envelopeOf(const Source &source, std::vector<double> &levels,
                std::function<bool(int)> progress, int fromPercent,
                int toPercent)
{
    const double hop = source.rate() * kHopSeconds;
    const sv_frame_t frames = source.frames();
    const sv_frame_t hops = sv_frame_t(std::floor(double(frames) / hop));
    levels.assign(size_t(std::max<sv_frame_t>(hops, 0)), 0.0);
    if (hops <= 0) return true;

    sv_frame_t k = 0;
    sv_frame_t hopEnd = hopStart(1, hop);
    double sum = 0.0;
    sv_frame_t count = 0;
    for (sv_frame_t pos = 0; pos < frames && k < hops; pos += blockFrames) {
        const std::vector<float> block =
            source.read(pos, std::min(blockFrames, frames - pos));
        for (size_t i = 0; i < block.size() && k < hops; ++i) {
            sum += double(block[i]) * block[i];
            ++count;
            if (pos + sv_frame_t(i) + 1 >= hopEnd) {
                levels[size_t(k)] = std::sqrt(sum / double(count));
                ++k;
                sum = 0.0;
                count = 0;
                hopEnd = hopStart(k + 1, hop);
            }
        }
        if (progress) {
            const int percent = fromPercent + int
                ((toPercent - fromPercent) * double(pos) / double(frames));
            if (!progress(percent)) return false;
        }
    }
    return true;
}

// The offset within a window that is most alike, and how alike: the
// piece a (pre-emphasised, masked) against the window b, as long as a
// and twice reach more, starting reach frames before the guess
struct Best {
    sv_frame_t lag = 0;
    double r = -1.0;
};

Best bestIn(const std::vector<double> &a, const std::vector<double> &mask,
            const std::vector<double> &b)
{
    Best best;
    const std::vector<double> r = maskedCorrelation(a, mask, b);
    for (size_t i = 0; i < r.size(); ++i) {
        if (r[i] > best.r) {
            best.r = r[i];
            best.lag = sv_frame_t(i);
        }
    }
    return best;
}

double median(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

}

std::vector<float>
MemorySource::read(sv_frame_t from, sv_frame_t count) const
{
    std::vector<float> out(size_t(std::max<sv_frame_t>(count, 0)), 0.f);
    for (sv_frame_t i = 0; i < count; ++i) {
        const sv_frame_t j = from + i;
        if (j >= 0 && j < sv_frame_t(m_samples.size())) {
            out[size_t(i)] = m_samples[size_t(j)];
        }
    }
    return out;
}

std::unique_ptr<Source>
openWav(QString path, QString &error)
{
    error = "";
    std::unique_ptr<WavSource> source(new WavSource(path));
    if (!source->isOK()) {
        error = tr("\"%1\" could not be read: %2").arg(path, source->error());
        return {};
    }
    return source;
}

std::vector<char>
dropouts(const std::vector<float> &audio, double rate, double gapSeconds)
{
    std::vector<char> gap(audio.size(), 0);
    const size_t minRun = size_t(std::max<long long>
                                 (1, std::llround(gapSeconds * rate)));
    size_t runStart = 0;
    bool inRun = false;
    for (size_t i = 0; i <= audio.size(); ++i) {
        const bool silent = (i < audio.size() &&
                             std::fabs(audio[i]) <= silentLevel);
        if (silent && !inRun) {
            runStart = i;
            inRun = true;
        } else if (!silent && inRun) {
            if (i - runStart >= minRun) {
                std::fill(gap.begin() + long(runStart), gap.begin() + long(i), 1);
            }
            inRun = false;
        }
    }
    return gap;
}

std::vector<double>
maskedCorrelation(const std::vector<double> &a,
                  const std::vector<double> &mask,
                  const std::vector<double> &b)
{
    const size_t n = a.size(), m = b.size();
    if (n == 0 || m < n || mask.size() != n) return {};
    const size_t lags = m - n + 1;

    // A circular correlation of a size that holds b: a is as long as b
    // or shorter, so no offset up to m - n wraps round
    size_t size = 16;
    while (size < m) size <<= 1;
    const size_t bins = size / 2 + 1;
    breakfastquay::FFT fft(static_cast<int>(size));

    std::vector<double> buffer(size);
    auto forward = [&](const std::vector<double> &x,
                       std::vector<double> &re, std::vector<double> &im) {
        std::fill(buffer.begin(), buffer.end(), 0.0);
        std::copy(x.begin(), x.end(), buffer.begin());
        re.assign(bins, 0.0);
        im.assign(bins, 0.0);
        fft.forward(buffer.data(), re.data(), im.data());
    };

    std::vector<double> ma(n), b2(m);
    double sm = 0.0, sa = 0.0, saa = 0.0, sbbAll = 0.0;
    for (size_t j = 0; j < n; ++j) {
        ma[j] = mask[j] * a[j];
        sm += mask[j];
        sa += ma[j];
        saa += ma[j] * a[j];
    }
    for (size_t k = 0; k < m; ++k) {
        b2[k] = b[k] * b[k];
        sbbAll += b2[k];
    }
    std::vector<double> result(lags, 0.0);
    const double va = saa - sa * sa / std::max(sm, 1.0);
    if (sm < 2.0 || !(va > 0.0)) return result;

    // A stretch of b a millionth as varied as b is on the whole is
    // silence, whatever the rounding of the transforms makes of it
    const double vbFloor = 1e-6 * sbbAll / double(m) * sm;
    auto coefficient = [&](double sab, double sb, double sbb) {
        const double cov = sab - sa * sb / sm;
        const double vb = sbb - sb * sb / sm;
        if (!(vb > vbFloor) || !(vb > 0.0)) return 0.0;
        return std::max(-1.0, std::min(1.0, cov / std::sqrt(va * vb)));
    };

    // A few offsets, as the walk looks at, are cheaper summed as they are
    if (lags <= 64) {
        for (size_t L = 0; L < lags; ++L) {
            double sab = 0.0, sb = 0.0, sbb = 0.0;
            for (size_t j = 0; j < n; ++j) {
                if (mask[j] == 0.0) continue;
                const double y = b[j + L];
                sab += ma[j] * y;
                sb += mask[j] * y;
                sbb += mask[j] * y * y;
            }
            result[L] = coefficient(sab, sb, sbb);
        }
        return result;
    }

    std::vector<double> maRe, maIm, mRe, mIm, bRe, bIm, b2Re, b2Im;
    forward(ma, maRe, maIm);
    forward(mask, mRe, mIm);
    forward(b, bRe, bIm);
    forward(b2, b2Re, b2Im);

    // sum_j x[j] y[j + L] is the inverse of conj(X) Y
    std::vector<double> pr(bins), pi(bins);
    auto correlate = [&](const std::vector<double> &xr,
                         const std::vector<double> &xi,
                         const std::vector<double> &yr,
                         const std::vector<double> &yi,
                         std::vector<double> &out) {
        for (size_t i = 0; i < bins; ++i) {
            pr[i] = xr[i] * yr[i] + xi[i] * yi[i];
            pi[i] = xr[i] * yi[i] - xi[i] * yr[i];
        }
        out.assign(size, 0.0);
        fft.inverse(pr.data(), pi.data(), out.data());
        for (double &v : out) v /= double(size);
    };
    std::vector<double> sab, sb, sbb;
    correlate(maRe, maIm, bRe, bIm, sab);
    correlate(mRe, mIm, bRe, bIm, sb);
    correlate(mRe, mIm, b2Re, b2Im, sbb);

    for (size_t L = 0; L < lags; ++L) {
        result[L] = coefficient(sab[L], sb[L], sbb[L]);
    }
    return result;
}

bool
levels(const Source &recording, std::vector<double> &levels,
       std::function<bool(int)> progress)
{
    return envelopeOf(recording, levels, progress, 0, 100);
}

Span
recordingSpan(sv_frame_t start, sv_frame_t end, double offset,
              double takeRate, double recordingRate,
              sv_frame_t recordingFrames)
{
    Span span { start, start, 0, 0, 0 };
    if (!(takeRate > 0.0) || !(recordingRate > 0.0) || recordingFrames < 2) {
        return span;
    }
    const double ratio = recordingRate / takeRate;

    // The stretch's first frame may not lie before the recording's
    // first, nor the frame after its end after the recording's last
    const sv_frame_t first = sv_frame_t(std::ceil(-offset));
    const sv_frame_t past = sv_frame_t
        (std::floor(double(recordingFrames - 1) / ratio - offset));
    span.start = std::max(start, first);
    span.end = std::min(end, past);
    if (span.end <= span.start) {
        span.end = span.start;
        return span;
    }

    // From the frame before to the frame after, at the recording's rate;
    // begun up to a frame early, which is left off the front in the
    // take's frames
    const double at = (double(span.start) + offset) * ratio;
    span.from = sv_frame_t(std::floor(at));
    span.count = sv_frame_t(std::ceil((double(span.end) + offset) * ratio)) -
        span.from + 1;
    span.lead = sv_frame_t(std::llround((at - double(span.from)) / ratio));
    return span;
}

namespace {

// Whether the range can be looked for at all; why not in match.error
bool lookable(const Source &take, sv_frame_t from, sv_frame_t to,
              const Source &recording, Match &match)
{
    if (!(take.rate() > 0.0) || !(recording.rate() > 0.0)) {
        match.error = tr("The audio has no sample rate");
        return false;
    }
    from = std::max<sv_frame_t>(from, 0);
    to = std::min(to, take.frames());
    const sv_frame_t n = to - from;
    if (double(n) < kMinRangeSeconds * take.rate()) {
        match.error = tr("The take's recorded range is too short to look "
                         "for: %1 s").arg(double(std::max<sv_frame_t>(n, 0)) /
                                          take.rate(), 0, 'f', 2);
        return false;
    }
    return true;
}

}

Match
find(const Source &take, sv_frame_t from, sv_frame_t to,
     const Source &recording, std::function<bool(int)> progress)
{
    // Reading the recording is the long part
    Match match;
    if (!lookable(take, from, to, recording, match)) return match;
    std::vector<double> recordingLevels;
    if (!levels(recording, recordingLevels, [&](int percent) {
        return !progress || progress(percent * 8 / 10);
    })) {
        match.error = tr("Cancelled");
        match.cancelled = true;
        return match;
    }
    return find(take, from, to, recording, recordingLevels, [&](int percent) {
        return !progress || progress(80 + percent / 5);
    });
}

Match
find(const Source &take, sv_frame_t from, sv_frame_t to,
     const Source &recording, const std::vector<double> &recordingLevel,
     std::function<bool(int)> progress)
{
    Match match;
    if (!lookable(take, from, to, recording, match)) return match;
    const double rate = take.rate();
    const double ratio = recording.rate() / rate;
    from = std::max<sv_frame_t>(from, 0);
    to = std::min(to, take.frames());
    const sv_frame_t n = to - from;

    // The take's range, and its dropout gaps
    const std::vector<float> audio = take.read(from, n);
    const std::vector<char> gaps = dropouts(audio, rate);

    // Its level, and where it is whole
    const double hop = rate * kHopSeconds;
    const sv_frame_t hops = sv_frame_t(std::floor(double(n) / hop));
    std::vector<double> level(size_t(hops), 0.0), whole(size_t(hops), 0.0);
    for (sv_frame_t k = 0; k < hops; ++k) {
        const sv_frame_t s = hopStart(k, hop), e = hopStart(k + 1, hop);
        double sum = 0.0;
        bool gap = false;
        for (sv_frame_t i = s; i < e; ++i) {
            sum += double(audio[size_t(i)]) * audio[size_t(i)];
            if (gaps[size_t(i)]) gap = true;
        }
        level[size_t(k)] = std::sqrt(sum / double(std::max<sv_frame_t>(e - s, 1)));
        whole[size_t(k)] = gap ? 0.0 : 1.0;
    }

    // Its loud level, which a piece has to be near to count
    std::vector<double> wholeLevels;
    for (sv_frame_t k = 0; k < hops; ++k) {
        if (whole[size_t(k)] > 0.0) wholeLevels.push_back(level[size_t(k)]);
    }
    if (wholeLevels.size() < 10) {
        match.error = tr("The take's recorded range holds nothing but "
                         "dropouts");
        return match;
    }
    std::sort(wholeLevels.begin(), wholeLevels.end());
    const double loud = wholeLevels[wholeLevels.size() * 9 / 10];
    if (!(loud > 0.0)) {
        match.error = tr("The take's recorded range is silent");
        return match;
    }
    const double quiet = loud * std::pow(10.0, -quietDb / 20.0);

    // 1. Coarse: the take's level against the recording's, at every
    // placement that leaves the anchor's length of the take or more on
    // the recording. The recording may have been started after the range
    // began, or stopped before it ended, and still hold the singing: off
    // its ends it is silence, which the take's quiet before and after
    // its singing is alike
    const sv_frame_t anchorHops = sv_frame_t(anchorSeconds / kHopSeconds);
    const sv_frame_t overlapHops = std::min(hops, anchorHops);
    if (sv_frame_t(recordingLevel.size()) < overlapHops) {
        match.error = tr("The recording is too short to hold the take's "
                         "singing");
        return match;
    }
    const sv_frame_t pad = hops - overlapHops;
    std::vector<double> padded(size_t(pad), 0.0);
    padded.insert(padded.end(), recordingLevel.begin(), recordingLevel.end());
    padded.resize(padded.size() + size_t(pad), 0.0);
    const std::vector<double> coarse = maskedCorrelation(level, whole, padded);

    // The candidates: the best offsets, each the best for some way
    // around it, in the recording's hops (before its start, negative)
    std::vector<size_t> order(coarse.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t x, size_t y) {
        return coarse[x] > coarse[y];
    });
    std::vector<sv_frame_t> candidates;
    for (size_t i : order) {
        if (int(candidates.size()) >= candidateCount) break;
        if (!(coarse[i] > 0.0)) break;
        const sv_frame_t c = sv_frame_t(i) - pad;
        bool tooClose = false;
        for (sv_frame_t other : candidates) {
            if (std::llabs(other - c) < candidateSpacingHops) {
                tooClose = true;
            }
        }
        if (!tooClose) candidates.push_back(c);
    }
    if (candidates.empty()) {
        match.error = tr("Nothing in the recording rises and falls as the "
                         "take does");
        return match;
    }
    if (progress && !progress(10)) {
        match.error = tr("Cancelled");
        match.cancelled = true;
        return match;
    }

    // 2. Fine: the loudest stretch of the take, as whole as any. Off the
    // recording's ends it is matched against silence, which is alike to
    // nothing: the anchor has to be on the recording
    const sv_frame_t anchorFrames =
        std::min(n, sv_frame_t(std::llround(anchorSeconds * rate)));
    sv_frame_t anchorHop = 0;
    double anchorScore = -1.0;
    for (sv_frame_t k = 0; k + anchorHops <= hops; ++k) {
        double score = 0.0;
        for (sv_frame_t j = k; j < k + anchorHops; ++j) {
            score += whole[size_t(j)] * level[size_t(j)] * level[size_t(j)];
        }
        if (score > anchorScore) {
            anchorScore = score;
            anchorHop = k;
        }
    }
    const sv_frame_t anchorStart =
        std::min(hopStart(anchorHop, hop), n - anchorFrames);
    std::vector<double> anchor(static_cast<size_t>(anchorFrames));
    for (sv_frame_t i = 0; i < anchorFrames; ++i) {
        anchor[size_t(i)] = audio[size_t(anchorStart + i)];
    }
    const std::vector<double> anchorE = emphasised(anchor);
    const std::vector<double> anchorMask =
        emphasisMask(gaps, anchorStart, anchorFrames);

    const sv_frame_t fineReach = sv_frame_t(std::llround(fineReachHops * hop));
    sv_frame_t anchorOffset = 0;
    double anchorR = -1.0;
    for (sv_frame_t c : candidates) {
        // The offset the candidate stands for, in the take's frames
        const sv_frame_t guess = hopStart(c, hop) - from;
        const std::vector<double> window = emphasised
            (readAt(recording, ratio, from + anchorStart + guess - fineReach,
                    anchorFrames + 2 * fineReach));
        const Best best = bestIn(anchorE, anchorMask, window);
        if (best.r > anchorR) {
            anchorR = best.r;
            anchorOffset = guess - fineReach + best.lag;
        }
    }
    if (progress && !progress(50)) {
        match.error = tr("Cancelled");
        match.cancelled = true;
        return match;
    }

    // What of the range the recording holds at the anchor's offset, in
    // the range's frames: a frame of the take whose frame of the recording
    // exists. The walk keeps to it, and the rest is no part of the match
    const double recordingEnd = double(recording.frames() - 1) / ratio;
    const sv_frame_t covered0 = std::max<sv_frame_t>
        (0, sv_frame_t(std::ceil(-double(from) - double(anchorOffset))));
    const sv_frame_t covered1 = std::min<sv_frame_t>
        (n, sv_frame_t(std::floor(recordingEnd - double(from) -
                                  double(anchorOffset))));
    match.coveredFrom = from + covered0;
    match.coveredTo = from + std::max(covered0, covered1);

    // 3. The walk, out from the anchor both ways
    struct Piece {
        sv_frame_t start;
        sv_frame_t offset;
        double r;
        double takeSquares;
        double recordingSquares;
    };
    const sv_frame_t pieceFrames =
        std::min(n, sv_frame_t(std::llround(pieceSeconds * rate)));
    const sv_frame_t spacing = pieceFrames;

    std::vector<sv_frame_t> starts;
    for (sv_frame_t s = 0; s + pieceFrames <= n; s += spacing) {
        if (s >= covered0 && s + pieceFrames <= covered1) starts.push_back(s);
    }

    std::vector<Piece> pieces;
    auto walk = [&](sv_frame_t start, sv_frame_t &tracked,
                    sv_frame_t &trackedAt) {
        // Only a piece mostly whole and loud enough to be alike in
        double sum = 0.0;
        sv_frame_t present = 0;
        for (sv_frame_t i = start; i < start + pieceFrames; ++i) {
            if (gaps[size_t(i)]) continue;
            sum += double(audio[size_t(i)]) * audio[size_t(i)];
            ++present;
        }
        if (double(present) < presentFraction * double(pieceFrames)) return;
        if (std::sqrt(sum / double(present)) < quiet) return;

        std::vector<double> piece(static_cast<size_t>(pieceFrames));
        for (sv_frame_t i = 0; i < pieceFrames; ++i) {
            piece[size_t(i)] = audio[size_t(start + i)];
        }
        const double since = double(std::llabs(start - trackedAt)) / rate;
        const sv_frame_t reach = std::max<sv_frame_t>
            (1, sv_frame_t(std::llround
                           (std::min(maxReachSeconds, walkReachSeconds +
                                     driftPerSecond * since) * rate)));
        const std::vector<double> raw = readAt
            (recording, ratio, from + start + tracked - reach,
             pieceFrames + 2 * reach);
        const std::vector<double> mask =
            emphasisMask(gaps, start, pieceFrames);
        const Best best = bestIn(emphasised(piece), mask, emphasised(raw));

        Piece p;
        p.start = start;
        p.offset = tracked - reach + best.lag;
        p.r = best.r;
        p.takeSquares = 0.0;
        p.recordingSquares = 0.0;
        for (sv_frame_t i = 0; i < pieceFrames; ++i) {
            if (gaps[size_t(start + i)]) continue;
            const double t = piece[size_t(i)];
            const double r = raw[size_t(best.lag + i)];
            p.takeSquares += t * t;
            p.recordingSquares += r * r;
        }
        pieces.push_back(p);
        // A piece that matched leads the next: the clocks may drift
        if (best.r >= kMinConfidence) {
            tracked = p.offset;
            trackedAt = start;
        }
    };
    size_t walkedSoFar = 0;
    auto going = [&]() {
        if (!progress || ++walkedSoFar % 100) return true;
        return progress(50 + int(50 * walkedSoFar / std::max<size_t>
                                 (starts.size(), 1)));
    };
    sv_frame_t tracked = anchorOffset, trackedAt = anchorStart;
    for (sv_frame_t s : starts) {
        if (s >= anchorStart) walk(s, tracked, trackedAt);
        if (!going()) break;
    }
    tracked = anchorOffset;
    trackedAt = anchorStart;
    for (auto i = starts.rbegin(); i != starts.rend(); ++i) {
        if (*i < anchorStart) walk(*i, tracked, trackedAt);
        if (!going()) break;
    }
    if (progress && !progress(100)) {
        match.error = tr("Cancelled");
        match.cancelled = true;
        return match;
    }
    std::sort(pieces.begin(), pieces.end(),
              [](const Piece &x, const Piece &y) { return x.start < y.start; });

    // How alike, and where each end lies
    std::vector<double> rs;
    std::vector<const Piece *> matched;
    double takeSquares = 0.0, recordingSquares = 0.0;
    for (const Piece &p : pieces) {
        rs.push_back(p.r);
        if (p.r < kMinConfidence) continue;
        matched.push_back(&p);
        takeSquares += p.takeSquares;
        recordingSquares += p.recordingSquares;
    }
    match.pieces = int(pieces.size());
    for (const Piece &p : pieces) {
        match.walked.push_back({ from + p.start, from + p.start + pieceFrames,
                                 double(p.offset), p.r });
    }
    // The anchor alone only where no piece was walked: two pieces unlike
    // the recording say more of a short range than its best half second
    match.confidence = (rs.empty() ? anchorR : median(rs));
    match.confidence = std::max(0.0, match.confidence);

    match.startOffset = match.endOffset = double(anchorOffset);
    if (matched.size() >= 2 &&
        double(matched.back()->start - matched.front()->start) >=
        endsApartSeconds * rate) {
        const size_t ends = std::min<size_t>(endPieces, matched.size() / 2);
        std::vector<double> first, last;
        for (size_t i = 0; i < ends; ++i) {
            first.push_back(double(matched[i]->offset));
            last.push_back(double(matched[matched.size() - 1 - i]->offset));
        }
        match.endsMeasured = true;
        match.startOffset = median(first);
        match.endOffset = median(last);
    }
    match.offset = 0.5 * (match.startOffset + match.endOffset);
    if (takeSquares > 0.0 && recordingSquares > 0.0) {
        match.gain = std::sqrt(takeSquares / recordingSquares);
    }

    match.found = (match.confidence >= kMinConfidence);
    if (!match.found) {
        match.error = tr("The take's audio is not in the recording: the "
                         "best match is %1 alike, and %2 is needed")
            .arg(match.confidence, 0, 'f', 2).arg(kMinConfidence, 0, 'f', 2);
    }
    if (progress) progress(100);
    return match;
}


namespace {

// Where a range is not found as a whole, how many of the walk's pieces
// in a row alike make a session of its own: a second of singing alike
// throughout, more than another singing of the song is alike by chance
// (a stretch of one a second long read 0.53 alike over its four pieces,
// two of them alike). How deep the search goes into the parts that are
// left
const int sessionPieces = 4;
const int maxDepth = 4;

// The windows a switch from one session to another is found in, and
// another session inside one
const double switchWindowSeconds = 0.02;
const double switchStepSeconds = 0.01;

// Where, between the take's frames a and b, the audio stops being alike
// the recording at the offset (from alike to unlike, or the other way
// if backwards): the middle of the stretch between the last loud window
// alike and the first loud one not, searching from a towards b
sv_frame_t switchPoint(const Source &take, const Source &recording,
                       double offset, sv_frame_t a, sv_frame_t b,
                       bool alikeFirst)
{
    const double rate = take.rate();
    const double ratio = recording.rate() / rate;
    if (b <= a) return a;
    const std::vector<float> audio = take.read(a, b - a);
    const std::vector<char> gaps = dropouts(audio, rate);
    const sv_frame_t lag = sv_frame_t(std::llround(offset));
    std::vector<double> raw(audio.begin(), audio.end());
    const std::vector<double> e = emphasised(raw);
    const std::vector<double> r = emphasised
        (readAt(recording, ratio, a + lag, b - a));

    // The level a window has to have to say anything
    double loud = 0.0;
    const sv_frame_t window = sv_frame_t(switchWindowSeconds * rate);
    const sv_frame_t step = sv_frame_t(switchStepSeconds * rate);
    std::vector<sv_frame_t> starts;
    for (sv_frame_t s = 0; s + window <= b - a; s += step) starts.push_back(s);
    std::vector<double> levels, alike;
    for (sv_frame_t s : starts) {
        double sum = 0.0, sab = 0.0, saa = 0.0, sbb = 0.0, sa = 0.0, sb = 0.0;
        double n = 0.0;
        for (sv_frame_t i = s + 1; i < s + window; ++i) {
            if (gaps[size_t(i)] || gaps[size_t(i - 1)]) continue;
            const double x = e[size_t(i)], y = r[size_t(i)];
            sum += raw[size_t(i)] * raw[size_t(i)];
            sa += x; sb += y; sab += x * y; saa += x * x; sbb += y * y;
            n += 1.0;
        }
        const double level = (n > 0 ? std::sqrt(sum / n) : 0.0);
        const double va = saa - sa * sa / std::max(n, 1.0);
        const double vb = sbb - sb * sb / std::max(n, 1.0);
        levels.push_back(n > window / 2 ? level : 0.0);
        alike.push_back((va > 0 && vb > 0) ?
                        (sab - sa * sb / n) / std::sqrt(va * vb) : 0.0);
        loud = std::max(loud, levels.back());
    }
    const double quiet = loud * std::pow(10.0, -quietDb / 20.0);

    const int count = int(starts.size());
    sv_frame_t lastAlike = alikeFirst ? 0 : b - a;
    for (int k = 0; k < count; ++k) {
        const int i = alikeFirst ? k : count - 1 - k;
        if (levels[size_t(i)] < quiet) continue;
        const bool isAlike = alike[size_t(i)] >= kMinConfidence;
        if (isAlike) {
            lastAlike = alikeFirst ? starts[size_t(i)] + window :
                starts[size_t(i)];
            continue;
        }
        const sv_frame_t firstUnlike = alikeFirst ? starts[size_t(i)] :
            starts[size_t(i)] + window;
        return a + (lastAlike + firstUnlike) / 2;
    }
    return alikeFirst ? b : a;
}

// The match over [from, to) alone: its offsets, ends and confidence from
// the walk's pieces there, which a stretch of another session elsewhere
// in the range, alike now and then by chance, cannot pull aside
Match within(const Match &m, sv_frame_t from, sv_frame_t to, double rate)
{
    Match result = m;
    std::vector<Match::Piece> here, alike;
    for (const Match::Piece &p : m.walked) {
        if (p.start < from || p.end > to) continue;
        here.push_back(p);
        if (p.r >= kMinConfidence) alike.push_back(p);
    }
    result.walked = here;
    result.pieces = int(here.size());
    if (here.size() >= 3) {
        std::vector<double> rs;
        for (const Match::Piece &p : here) rs.push_back(p.r);
        result.confidence = median(rs);
    }
    if (alike.empty()) return result;

    const size_t ends = std::min<size_t>
        (endPieces, std::max<size_t>(alike.size() / 2, 1));
    std::vector<double> first, last;
    for (size_t i = 0; i < ends; ++i) {
        first.push_back(alike[i].offset);
        last.push_back(alike[alike.size() - 1 - i].offset);
    }
    result.startOffset = median(first);
    result.endOffset = median(last);
    result.endsMeasured = (alike.size() >= 2 &&
                           double(alike.back().start - alike.front().start) >=
                           endsApartSeconds * rate);
    if (!result.endsMeasured) {
        std::vector<double> all;
        for (const Match::Piece &p : alike) all.push_back(p.offset);
        result.startOffset = result.endOffset = median(all);
    }
    result.offset = 0.5 * (result.startOffset + result.endOffset);
    return result;
}

// The scan for another session inside one found: windows of the take
// at the offset of the nearest of the walk's pieces alike, in blocks of
// this many windows read at once
const int scanBlockWindows = 500;

// Another session inside one is where at least kMinSessionSeconds of
// loud windows are unlike the recording, and it ends where as long a run
// of them is alike: another singing of the same notes is alike now and
// then for a few windows (three in a row, in a punch-in of the tests'
// synthetic singing), and a run that ended there came in pieces

// The stretches of [from, to) whose loud windows are unlike the
// recording at the offsets of the session found as m, end to end with
// the rest of it and in order. Each switch is the middle of the stretch
// between the last window alike and the first unlike, or the other way;
// one with no window alike before or after it runs to from or to
std::vector<std::pair<sv_frame_t, sv_frame_t>>
unlikeRuns(const Source &take, const Source &recording, const Match &m,
           sv_frame_t from, sv_frame_t to)
{
    std::vector<std::pair<sv_frame_t, sv_frame_t>> runs;
    std::vector<const Match::Piece *> alikePieces;
    for (const Match::Piece &p : m.walked) {
        if (p.r >= kMinConfidence) alikePieces.push_back(&p);
    }
    const double rate = take.rate();
    const double ratio = recording.rate() / rate;
    const sv_frame_t window = sv_frame_t(switchWindowSeconds * rate);
    const sv_frame_t step = sv_frame_t(switchStepSeconds * rate);
    if (alikePieces.empty() || window < 2 || step < 1 || to - from < window) {
        return runs;
    }

    // Each window's offset: that of the piece alike nearest its middle
    std::vector<sv_frame_t> starts;
    for (sv_frame_t w = from; w + window <= to; w += step) starts.push_back(w);
    std::vector<sv_frame_t> lags(starts.size());
    size_t p = 0;
    for (size_t k = 0; k < starts.size(); ++k) {
        const sv_frame_t middle = starts[k] + window / 2;
        auto distance = [&](size_t i) -> sv_frame_t {
            const Match::Piece &piece = *alikePieces[i];
            if (middle >= piece.start && middle < piece.end) return 0;
            return sv_frame_t(std::min(std::llabs(middle - piece.start),
                                       std::llabs(middle - piece.end)));
        };
        while (p + 1 < alikePieces.size() && distance(p + 1) <= distance(p)) ++p;
        lags[k] = sv_frame_t(std::llround(alikePieces[p]->offset));
    }

    // Each window's level and how alike it is, a block at a time
    std::vector<double> levels(starts.size(), 0.0), alike(starts.size(), 0.0);
    const sv_frame_t margin = sv_frame_t(std::ceil(0.01 * rate));
    for (size_t k0 = 0; k0 < starts.size(); k0 += scanBlockWindows) {
        const size_t k1 = std::min(starts.size(), k0 + scanBlockWindows);
        const sv_frame_t b0 = starts[k0] - margin;
        const sv_frame_t b1 = starts[k1 - 1] + window + margin;
        const std::vector<float> audio = take.read(b0, b1 - b0);
        const std::vector<char> gaps = dropouts(audio, rate);
        std::vector<double> raw(audio.begin(), audio.end());
        const std::vector<double> e = emphasised(raw);
        sv_frame_t lag0 = lags[k0], lag1 = lags[k0];
        for (size_t k = k0; k < k1; ++k) {
            lag0 = std::min(lag0, lags[k]);
            lag1 = std::max(lag1, lags[k]);
        }
        const std::vector<double> r = emphasised
            (readAt(recording, ratio, b0 + lag0, b1 - b0 + lag1 - lag0));
        for (size_t k = k0; k < k1; ++k) {
            const sv_frame_t s = starts[k] - b0;
            const sv_frame_t shift = lags[k] - lag0;
            double sum = 0.0, sab = 0.0, saa = 0.0, sbb = 0.0, sa = 0.0,
                sb = 0.0, count = 0.0;
            for (sv_frame_t i = s + 1; i < s + window; ++i) {
                if (gaps[size_t(i)] || gaps[size_t(i - 1)]) continue;
                const double x = e[size_t(i)], y = r[size_t(i + shift)];
                sum += raw[size_t(i)] * raw[size_t(i)];
                sa += x; sb += y; sab += x * y; saa += x * x; sbb += y * y;
                count += 1.0;
            }
            if (count <= double(window) / 2) continue;
            const double va = saa - sa * sa / count;
            const double vb = sbb - sb * sb / count;
            levels[k] = std::sqrt(sum / count);
            alike[k] = (va > 0 && vb > 0) ?
                (sab - sa * sb / count) / std::sqrt(va * vb) : 0.0;
        }
    }

    // Loud as the walk counts it: within quietDb of the range's loud level
    std::vector<double> sorted;
    for (double l : levels) if (l > 0.0) sorted.push_back(l);
    if (sorted.empty()) return runs;
    std::sort(sorted.begin(), sorted.end());
    const double quiet = sorted[sorted.size() * 9 / 10] *
        std::pow(10.0, -quietDb / 20.0);
    // 1 alike, -1 unlike, 0 neither: too quiet, or mostly dropouts
    std::vector<int> state(starts.size(), 0);
    for (size_t k = 0; k < starts.size(); ++k) {
        if (levels[k] > 0.0 && levels[k] >= quiet) {
            state[k] = (alike[k] >= kMinConfidence) ? 1 : -1;
        }
    }

    const int minRun = int(std::ceil(kMinSessionSeconds /
                                     switchStepSeconds));
    size_t k = 0;
    while (k < starts.size()) {
        if (state[k] != -1) { ++k; continue; }
        const size_t first = k;
        size_t last = k;
        int unlike = 0, alikeInRow = 0;
        for (size_t j = k; j < starts.size(); ++j) {
            if (state[j] == -1) {
                last = j;
                ++unlike;
                alikeInRow = 0;
            } else if (state[j] == 1 && ++alikeInRow >= minRun) {
                break;
            }
        }
        k = last + 1;
        if (unlike < minRun) continue;

        size_t before = first;
        while (before > 0 && state[before - 1] != 1) --before;
        size_t after = last + 1;
        while (after < starts.size() && state[after] != 1) ++after;
        const sv_frame_t start = (before == 0) ? from :
            (starts[before - 1] + window + starts[first]) / 2;
        const sv_frame_t end = (after == starts.size()) ? to :
            (starts[last] + window + starts[after]) / 2;
        runs.push_back({ std::max(start, from), std::min(end, to) });
    }
    return runs;
}

std::vector<Segment> segmentsOf(const Source &take, sv_frame_t from,
                                sv_frame_t to, const Source &recording,
                                const std::vector<double> &levels,
                                std::function<bool(int)> progress,
                                int depth);

// [from, to) of the session found as m: its stretches of another session
// cut out, each looked for on its own or, too short to look for, left as
// it is, and the rest of it m's
std::vector<Segment> sessionSegments(const Source &take, sv_frame_t from,
                                     sv_frame_t to, const Source &recording,
                                     const std::vector<double> &levels,
                                     std::function<bool(int)> progress,
                                     int depth, const Match &m)
{
    std::vector<Segment> result;
    const double rate = take.rate();
    sv_frame_t at = from;
    for (const auto &run : unlikeRuns(take, recording, m, from, to)) {
        if (run.second <= at) continue;
        const sv_frame_t start = std::max(run.first, at);
        if (start > at) result.push_back({ at, start, within(m, at, start, rate) });
        if (double(run.second - start) < kMinRangeSeconds * rate) {
            Match other;
            other.left = Match::Left::ShortSession;
            other.error = tr("Another recording session, too short to look "
                             "for");
            result.push_back({ start, run.second, other });
        } else if (depth <= 0) {
            Match other;
            other.error = tr("Recording sessions inside one another, too "
                             "many deep to look into");
            result.push_back({ start, run.second, other });
        } else if (start <= from && run.second >= to) {
            // Never the same range again, which would never end: its
            // sessions cannot be told apart
            Match other;
            other.error = tr("Another recording session that could not be "
                             "told apart from the rest");
            result.push_back({ start, run.second, other });
        } else {
            for (const Segment &s : segmentsOf(take, start, run.second,
                                               recording, levels, progress,
                                               depth - 1)) {
                result.push_back(s);
            }
        }
        at = run.second;
    }
    if (at < to) result.push_back({ at, to, within(m, at, to, rate) });
    return result;
}

std::vector<Segment> segmentsOf(const Source &take, sv_frame_t from,
                                sv_frame_t to, const Source &recording,
                                const std::vector<double> &levels,
                                std::function<bool(int)> progress,
                                int depth)
{
    std::vector<Segment> result;
    if (double(to - from) < kMinRangeSeconds * take.rate()) {
        Match m;
        m.left = Match::Left::TooShort;
        m.error = tr("The take's recorded range is too short to look for");
        result.push_back({ from, to, m });
        return result;
    }

    const Match m = find(take, from, to, recording, levels, progress);
    if (m.cancelled) {
        result.push_back({ from, to, m });
        return result;
    }

    const auto &walked = m.walked;
    if (!m.found) {
        // Not alike as a whole: the punch-ins may be most of it. The
        // longest run of pieces alike in a row is a session of its own
        // if it is long and alike enough, and the rest is looked for
        size_t best = 0, bestLength = 0;
        for (size_t i = 0; i < walked.size(); ) {
            size_t j = i;
            while (j < walked.size() && walked[j].r >= kMinConfidence) ++j;
            if (j - i > bestLength) {
                best = i;
                bestLength = j - i;
            }
            i = std::max(j, i + 1);
        }
        std::vector<double> rs;
        for (size_t k = best; k < best + bestLength; ++k) {
            rs.push_back(walked[k].r);
        }
        if (depth <= 0 || int(bestLength) < sessionPieces) {
            result.push_back({ from, to, m });
            return result;
        }
        // Each switch found at the offset of the piece alike beside it,
        // which the walk followed through any drift
        const size_t end = best + bestLength;
        const sv_frame_t runStart = (best == 0) ? from :
            switchPoint(take, recording, walked[best].offset,
                        walked[best - 1].start, walked[best].end, false);
        const sv_frame_t runEnd = (end == walked.size()) ? to :
            switchPoint(take, recording, walked[end - 1].offset,
                        walked[end - 1].start, walked[end].end, true);
        Match run = within(m, runStart, runEnd, take.rate());
        run.found = true;
        run.error = "";
        run.confidence = median(rs);
        if (runStart > from) {
            result = segmentsOf(take, from, runStart, recording, levels,
                                progress, depth - 1);
        }
        for (const Segment &s : sessionSegments(take, runStart, runEnd,
                                                recording, levels, progress,
                                                depth, run)) {
            result.push_back(s);
        }
        if (runEnd < to) {
            for (const Segment &s : segmentsOf(take, runEnd, to, recording,
                                               levels, progress, depth - 1)) {
                result.push_back(s);
            }
        }
        return result;
    }

    // Found: what lies off the recording's ends is left as it is, and
    // the rest is this session's but for any other inside it
    Match outside;
    outside.left = Match::Left::OutsideRecording;
    outside.error = tr("Not in the recording");
    const sv_frame_t in0 = std::max(from, m.coveredFrom);
    const sv_frame_t in1 = std::max(in0, std::min(to, m.coveredTo));
    if (in0 > from) result.push_back({ from, in0, outside });
    for (const Segment &s : sessionSegments(take, in0, in1, recording, levels,
                                            progress, depth, m)) {
        result.push_back(s);
    }
    if (in1 < to) result.push_back({ in1, to, outside });
    return result;
}

}

std::vector<Segment>
findSegments(const Source &take, sv_frame_t from, sv_frame_t to,
             const Source &recording, const std::vector<double> &levels,
             std::function<bool(int)> progress)
{
    from = std::max<sv_frame_t>(from, 0);
    to = std::min(to, take.frames());
    std::vector<Segment> segments = segmentsOf
        (take, from, to, recording, levels, progress, maxDepth);

    // Neighbours found at the same place are one
    std::vector<Segment> merged;
    for (const Segment &s : segments) {
        if (!merged.empty() && merged.back().match.found && s.match.found &&
            merged.back().end == s.start &&
            std::fabs(merged.back().match.offset - s.match.offset) <
            0.0005 * take.rate()) {
            merged.back().end = s.end;
            continue;
        }
        merged.push_back(s);
    }
    return merged;
}

}
