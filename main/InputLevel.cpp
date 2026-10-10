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

#include "InputLevel.h"
#include "VoiceThreshold.h"

#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

using namespace sv;

namespace InputLevel {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("InputLevel", text);
}

// How many frames scanFile() reads at once
const sv_frame_t readFrames = 65536;

}

double
dbfs(double amplitude)
{
    amplitude = std::fabs(amplitude);
    if (!(amplitude > 0.0)) return -200.0;
    return std::max(-200.0, 20.0 * std::log10(amplitude));
}

Scanner::Scanner(int channels, int channel) :
    m_channels(std::max(1, channels)),
    m_channel(channel < channels ? channel : -1),
    m_runStart(0),
    m_runLength(0)
{
    m_scan.channelPeaks.assign(size_t(m_channels), 0.f);
}

void
Scanner::add(const float *interleaved, sv_frame_t frames)
{
    if (!interleaved) return;
    for (sv_frame_t i = 0; i < frames; ++i) {
        const float *frame = interleaved + i * m_channels;
        bool atFullScale = false;
        for (int c = 0; c < m_channels; ++c) {
            const float a = std::fabs(frame[c]);
            float &channelPeak = m_scan.channelPeaks[size_t(c)];
            if (a > channelPeak) channelPeak = a;
            if (m_channel >= 0 && c != m_channel) continue;
            if (a > m_scan.peak) m_scan.peak = a;
            if (a >= kFullScale) atFullScale = true;
        }
        if (atFullScale) {
            if (m_runLength == 0) m_runStart = m_scan.frames + i;
            ++m_runLength;
        } else {
            endRun();
        }
    }
    m_scan.frames += frames;
}

void
Scanner::endRun()
{
    if (m_runLength >= kClipRun) {
        m_scan.clips.push_back({ m_runStart, m_runStart + m_runLength });
    }
    m_runLength = 0;
}

Scan
Scanner::finish()
{
    endRun();
    return m_scan;
}

Scan
scanFile(QString path, int channel, sv_frame_t from, sv_frame_t count,
         QString &error)
{
    error = "";
    FileSource source(path);
    WavFileReader reader(source);
    const int channels = reader.getChannelCount();
    if (!reader.isOK() || channels < 1) {
        error = tr("\"%1\" could not be read: %2")
            .arg(path, reader.getError());
        return {};
    }

    if (from < 0) from = 0;
    sv_frame_t to = reader.getFrameCount();
    if (count >= 0) to = std::min(to, from + count);

    Scanner scanner(channels, channel);
    for (sv_frame_t start = from; start < to; start += readFrames) {
        const floatvec_t data = reader.getInterleavedFrames
            (start, std::min(readFrames, to - start));
        const sv_frame_t got = sv_frame_t(data.size()) / channels;
        if (got <= 0) break;
        scanner.add(data.data(), got);
    }

    // Counted from the start of the file, not of what was scanned
    Scan scan = scanner.finish();
    for (Clip &clip : scan.clips) {
        clip.start += from;
        clip.end += from;
    }
    return scan;
}

std::vector<Clip>
places(const std::vector<Clip> &clips, sv_frame_t gap)
{
    std::vector<Clip> result;
    for (const Clip &clip : clips) {
        if (!result.empty() && clip.start - result.back().end < gap) {
            result.back().end = std::max(result.back().end, clip.end);
        } else {
            result.push_back(clip);
        }
    }
    return result;
}

void
Meter::peak(float amplitude, std::int64_t ms)
{
    const double db = std::max(kFloorDb, dbfs(amplitude));
    if (db >= bar(ms)) {
        m_barDb = db;
        m_barMs = ms;
    }
    if (db >= hold(ms)) {
        m_holdDb = db;
        m_holdMs = ms;
    }
    if (std::fabs(amplitude) >= kFullScale) m_clipped = true;
}

double
Meter::bar(std::int64_t ms) const
{
    const double since = double(std::max<std::int64_t>(0, ms - m_barMs));
    return std::max(kFloorDb, m_barDb - kFallDbPerSecond * since / 1000.0);
}

double
Meter::hold(std::int64_t ms) const
{
    const double since =
        double(std::max<std::int64_t>(0, ms - m_holdMs - kHoldMs));
    return std::max(kFloorDb, m_holdDb - kFallDbPerSecond * since / 1000.0);
}

bool
Meter::moving(std::int64_t ms) const
{
    return bar(ms) > kFloorDb || hold(ms) > kFloorDb;
}

double
gainChange(double peakDbfs)
{
    return std::round(kTargetPeakDbfs - peakDbfs);
}

double
noiseFloor(std::vector<double> peaksDbfs)
{
    if (peaksDbfs.empty()) return -200.0;
    std::sort(peaksDbfs.begin(), peaksDbfs.end());
    const size_t n = peaksDbfs.size();
    if (n % 2 == 1) return peaksDbfs[n / 2];
    return (peaksDbfs[n / 2 - 1] + peaksDbfs[n / 2]) / 2.0;
}

double
suggestedThreshold(double noiseFloorDbfs)
{
    // 5 dB over the noise's peaks; Off where that is still 5 dB under
    // the live tracker's own floor, which the noise's level, 3 dB or more
    // under its peaks, is then 8 dB or more under
    const double wanted = noiseFloorDbfs + 5.0;
    if (wanted <= VoiceThreshold::kOff - 5.0) return VoiceThreshold::kOff;
    double highest = VoiceThreshold::kOff;
    for (double choice : VoiceThreshold::choices()) {
        if (!VoiceThreshold::isOn(choice)) continue;
        if (choice >= wanted) return choice;
        highest = std::max(highest, choice);
    }
    return highest;
}

}
