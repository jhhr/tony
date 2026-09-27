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

#ifndef TONY_DECODED_PCM_H
#define TONY_DECODED_PCM_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * What a platform's audio decoder hands out, made into what svcore's
 * readers give (bqaudiostream's AudioReadStream::getInterleavedFrames()):
 * float frames, interleaved, at one channel count, read in pieces of
 * any size.
 *
 * For AndroidMediaReadStream, whose decoder (Android's MediaCodec)
 * gives buffers of PCM in whatever encoding its output format names,
 * 16-bit integers when it names none, and may change that format part
 * way through: the channel count the stream reports is fixed by its
 * first buffer, and a buffer with another count is folded into it.
 *
 * Nothing here touches a decoder, so all of it is tested on the desktop
 * (TestDecodedPcm).
 */
class DecodedPcm
{
public:
    /**
     * Android's PCM encodings, the values of AudioFormat.ENCODING_PCM_*
     * that a MediaFormat's "pcm-encoding" holds. Each sample is in the
     * device's own byte order (little-endian), 24-bit ones as three
     * bytes; 8-bit ones are unsigned about 128.
     */
    enum Encoding {
        Int16 = 2,
        Int8 = 3,
        Float = 4,
        Int24Packed = 21,
        Int32 = 22
    };

    /// Whether the value is one of the encodings above
    static bool isKnownEncoding(int encoding);

    /// Bytes per sample of an encoding, 0 for one not known
    static int bytesPerSample(int encoding);

    /// The encoding in words, for the log: "16-bit integer" and so on
    static std::string describe(int encoding);

    /**
     * Convert frames of interleaved samples in an encoding, and in
     * sourceChannels channels, into float frames in targetChannels.
     * Fewer channels than wanted: channel c is source channel c modulo
     * their number (mono goes to every channel). More: target channel c
     * is the mean of the source channels whose index is c modulo the
     * target's number (all of them for mono), so that no channel, and
     * no voice in a centre channel, is lost.
     */
    static void convert(const uint8_t *data, int encoding,
                        int sourceChannels, size_t frames,
                        float *out, int targetChannels);

    explicit DecodedPcm(int channels);

    int getChannelCount() const { return m_channels; }

    /**
     * Take a buffer of bytes of samples in the encoding, interleaved in
     * the given number of channels. Returns the whole frames taken; a
     * part of a frame at the end, which a decoder should never give, is
     * left out and counted (getBytesDropped()). Nothing is taken in an
     * encoding that is not known, or with no channels.
     */
    size_t add(const void *data, size_t bytes, int encoding, int channels);

    /// Frames taken and not yet read
    size_t getAvailable() const;

    /**
     * Copy up to the given number of frames into out, which has room
     * for that many at getChannelCount(), and return how many there
     * were.
     */
    size_t read(float *out, size_t frames);

    int64_t getFramesAdded() const { return m_added; }
    int64_t getFramesRead() const { return m_read; }
    int64_t getBytesDropped() const { return m_dropped; }

private:
    int m_channels;
    std::vector<float> m_samples;
    size_t m_readPosition; // in samples, into m_samples
    int64_t m_added;
    int64_t m_read;
    int64_t m_dropped;
};

#endif
