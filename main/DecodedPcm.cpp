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

#include "DecodedPcm.h"

#include <algorithm>
#include <cstring>

bool
DecodedPcm::isKnownEncoding(int encoding)
{
    return bytesPerSample(encoding) > 0;
}

int
DecodedPcm::bytesPerSample(int encoding)
{
    switch (encoding) {
    case Int16: return 2;
    case Int8: return 1;
    case Float: return 4;
    case Int24Packed: return 3;
    case Int32: return 4;
    default: return 0;
    }
}

std::string
DecodedPcm::describe(int encoding)
{
    switch (encoding) {
    case Int16: return "16-bit integer";
    case Int8: return "8-bit integer";
    case Float: return "float";
    case Int24Packed: return "24-bit integer";
    case Int32: return "32-bit integer";
    default: return "unknown encoding " + std::to_string(encoding);
    }
}

// One sample, from the device's byte order, which is little-endian on
// every Android ABI; memcpy, as a buffer need not be aligned for it
static float
sampleAt(const uint8_t *p, int encoding)
{
    switch (encoding) {
    case DecodedPcm::Int16: {
        int16_t v;
        std::memcpy(&v, p, sizeof(v));
        return float(v) / 32768.f;
    }
    case DecodedPcm::Int8:
        return (float(p[0]) - 128.f) / 128.f;
    case DecodedPcm::Float: {
        float v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }
    case DecodedPcm::Int24Packed: {
        // Into the top three bytes of an int32, which carries the sign
        uint32_t u = (uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) |
            (uint32_t(p[2]) << 24);
        int32_t v;
        std::memcpy(&v, &u, sizeof(v));
        return float(double(v) / 2147483648.0);
    }
    case DecodedPcm::Int32: {
        int32_t v;
        std::memcpy(&v, p, sizeof(v));
        return float(double(v) / 2147483648.0);
    }
    default:
        return 0.f;
    }
}

void
DecodedPcm::convert(const uint8_t *data, int encoding,
                    int sourceChannels, size_t frames,
                    float *out, int targetChannels)
{
    const int bytes = bytesPerSample(encoding);
    if (bytes == 0 || sourceChannels < 1 || targetChannels < 1) return;

    const size_t frameBytes = size_t(bytes) * size_t(sourceChannels);

    for (size_t f = 0; f < frames; ++f) {

        const uint8_t *frame = data + f * frameBytes;
        float *target = out + f * size_t(targetChannels);

        if (sourceChannels <= targetChannels) {
            for (int c = 0; c < targetChannels; ++c) {
                int s = c % sourceChannels;
                target[c] = sampleAt(frame + s * bytes, encoding);
            }
            continue;
        }

        for (int c = 0; c < targetChannels; ++c) {
            float sum = 0.f;
            int n = 0;
            for (int s = c; s < sourceChannels; s += targetChannels) {
                sum += sampleAt(frame + s * bytes, encoding);
                ++n;
            }
            target[c] = sum / float(n);
        }
    }
}

DecodedPcm::DecodedPcm(int channels) :
    m_channels(std::max(channels, 1)),
    m_readPosition(0),
    m_added(0),
    m_read(0),
    m_dropped(0)
{
}

size_t
DecodedPcm::add(const void *data, size_t bytes, int encoding, int channels)
{
    const int sampleBytes = bytesPerSample(encoding);
    if (sampleBytes == 0 || channels < 1 || !data) {
        m_dropped += int64_t(bytes);
        return 0;
    }

    const size_t frameBytes = size_t(sampleBytes) * size_t(channels);
    const size_t frames = bytes / frameBytes;
    m_dropped += int64_t(bytes - frames * frameBytes);
    if (frames == 0) return 0;

    // What has been read goes before more is added, once it is at least
    // half of what is held, so that the buffer does not grow for ever
    if (m_readPosition > 0 && m_readPosition * 2 >= m_samples.size()) {
        m_samples.erase(m_samples.begin(),
                        m_samples.begin() + ptrdiff_t(m_readPosition));
        m_readPosition = 0;
    }

    size_t start = m_samples.size();
    m_samples.resize(start + frames * size_t(m_channels));
    convert(static_cast<const uint8_t *>(data), encoding, channels, frames,
            m_samples.data() + start, m_channels);

    m_added += int64_t(frames);
    return frames;
}

size_t
DecodedPcm::getAvailable() const
{
    return (m_samples.size() - m_readPosition) / size_t(m_channels);
}

size_t
DecodedPcm::read(float *out, size_t frames)
{
    size_t n = std::min(frames, getAvailable());
    if (n == 0) return 0;

    size_t samples = n * size_t(m_channels);
    std::memcpy(out, m_samples.data() + m_readPosition,
                samples * sizeof(float));
    m_readPosition += samples;

    if (m_readPosition == m_samples.size()) {
        m_samples.clear();
        m_readPosition = 0;
    }

    m_read += int64_t(n);
    return n;
}
