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

#ifndef TEST_DECODED_PCM_H
#define TEST_DECODED_PCM_H

// Tier 2: the output of Android's decoders as svcore's readers give
// audio. Bytes in the encodings Android names, float frames out; the
// decoder itself can only be tried on a phone.

#include "../DecodedPcm.h"

#include <QObject>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

class TestDecodedPcm : public QObject
{
    Q_OBJECT

    // The samples as bytes in an encoding, little-endian as on every
    // Android ABI: the inverse of the conversion, for the tests that go
    // through every encoding. The byte-level tests below do not use it,
    // so that a mistake made the same way both ways is still caught
    static std::vector<uint8_t> encode(const std::vector<float> &samples,
                                       int encoding) {
        std::vector<uint8_t> bytes;
        for (float v : samples) {
            switch (encoding) {
            case DecodedPcm::Int16: {
                long i = std::lrint(double(v) * 32768.0);
                i = std::min(32767L, std::max(-32768L, i));
                bytes.push_back(uint8_t(i & 0xff));
                bytes.push_back(uint8_t((i >> 8) & 0xff));
                break;
            }
            case DecodedPcm::Int8: {
                long i = std::lrint(double(v) * 128.0) + 128;
                i = std::min(255L, std::max(0L, i));
                bytes.push_back(uint8_t(i));
                break;
            }
            case DecodedPcm::Float: {
                uint8_t b[4];
                std::memcpy(b, &v, 4);
                bytes.insert(bytes.end(), b, b + 4);
                break;
            }
            case DecodedPcm::Int24Packed: {
                long i = std::lrint(double(v) * 8388608.0);
                i = std::min(8388607L, std::max(-8388608L, i));
                bytes.push_back(uint8_t(i & 0xff));
                bytes.push_back(uint8_t((i >> 8) & 0xff));
                bytes.push_back(uint8_t((i >> 16) & 0xff));
                break;
            }
            case DecodedPcm::Int32: {
                long long i = std::llrint(double(v) * 2147483648.0);
                i = std::min(2147483647LL, std::max(-2147483648LL, i));
                for (int k = 0; k < 4; ++k) {
                    bytes.push_back(uint8_t((i >> (8 * k)) & 0xff));
                }
                break;
            }
            }
        }
        return bytes;
    }

    // The smallest step of an encoding, as a float
    static double step(int encoding) {
        switch (encoding) {
        case DecodedPcm::Int16: return 1.0 / 32768.0;
        case DecodedPcm::Int8: return 1.0 / 128.0;
        case DecodedPcm::Int24Packed: return 1.0 / 8388608.0;
        case DecodedPcm::Int32: return 1.0 / 2147483648.0;
        default: return 0.0;
        }
    }

    // Frame f, channel c, of a signal whose every sample says where it
    // came from, within [-1, 1)
    static float ramp(size_t f, int c) {
        return float((double(f % 200) - 100.0) / 128.0 + 0.001 * c);
    }

    static std::vector<float> ramps(size_t frames, int channels, size_t from = 0) {
        std::vector<float> v;
        for (size_t f = from; f < from + frames; ++f) {
            for (int c = 0; c < channels; ++c) v.push_back(ramp(f, c));
        }
        return v;
    }

    static std::vector<float> convertBytes(std::vector<uint8_t> bytes,
                                           int encoding, int channels,
                                           int targetChannels) {
        size_t frames = bytes.size() /
            size_t(DecodedPcm::bytesPerSample(encoding) * channels);
        std::vector<float> out(frames * size_t(targetChannels), -99.f);
        DecodedPcm::convert(bytes.data(), encoding, channels, frames,
                            out.data(), targetChannels);
        return out;
    }

private slots:

    // Android's own byte layouts (AudioFormat): signed and little-endian,
    // 8-bit unsigned about 128, 24-bit as three bytes
    void each_encoding_from_its_bytes() {
        QCOMPARE(convertBytes({ 0x00, 0x40, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x80 },
                              DecodedPcm::Int16, 1, 1),
                 (std::vector<float> { 0.5f, -0.5f, 32767.f / 32768.f, -1.f }));
        QCOMPARE(convertBytes({ 0xc0, 0x40, 0x80, 0x00 },
                              DecodedPcm::Int8, 1, 1),
                 (std::vector<float> { 0.5f, -0.5f, 0.f, -1.f }));
        QCOMPARE(convertBytes({ 0x00, 0x00, 0x40,   0x00, 0x00, 0xc0,
                                0xff, 0xff, 0xff,   0x00, 0x00, 0x80,
                                0x01, 0x00, 0x00 },
                              DecodedPcm::Int24Packed, 1, 1),
                 (std::vector<float> { 0.5f, -0.5f, -1.f / 8388608.f, -1.f,
                                       1.f / 8388608.f }));
        QCOMPARE(convertBytes({ 0x00, 0x00, 0x00, 0x40,   0x00, 0x00, 0x00, 0xc0,
                                0x00, 0x00, 0x00, 0x80 },
                              DecodedPcm::Int32, 1, 1),
                 (std::vector<float> { 0.5f, -0.5f, -1.f }));
        float values[] = { 0.25f, -0.75f };
        std::vector<uint8_t> floats(sizeof(values));
        std::memcpy(floats.data(), values, sizeof(values));
        QCOMPARE(convertBytes(floats, DecodedPcm::Float, 1, 1),
                 (std::vector<float> { 0.25f, -0.75f }));
    }

    // The same stereo signal in every encoding comes out the same, to
    // within the encoding's step, and in its frames and channels
    void every_encoding_gives_the_signal() {
        const int encodings[] = {
            DecodedPcm::Int16, DecodedPcm::Int8, DecodedPcm::Float,
            DecodedPcm::Int24Packed, DecodedPcm::Int32
        };
        std::vector<float> signal = ramps(300, 2);
        for (int encoding : encodings) {
            std::vector<float> out =
                convertBytes(encode(signal, encoding), encoding, 2, 2);
            QCOMPARE(out.size(), signal.size());
            for (size_t i = 0; i < signal.size(); ++i) {
                QVERIFY2(std::fabs(out[i] - signal[i]) <= step(encoding) * 0.51 + 1e-9,
                         qPrintable(QString("%1: sample %2 is %3, not %4")
                                    .arg(QString::fromStdString
                                         (DecodedPcm::describe(encoding)))
                                    .arg(i).arg(out[i]).arg(signal[i])));
            }
        }
    }

    void encodings_known_and_described() {
        QVERIFY(DecodedPcm::isKnownEncoding(2));
        QVERIFY(DecodedPcm::isKnownEncoding(4));
        QVERIFY(!DecodedPcm::isKnownEncoding(0));
        QVERIFY(!DecodedPcm::isKnownEncoding(1)); // ENCODING_DEFAULT
        QVERIFY(!DecodedPcm::isKnownEncoding(5)); // AC3: not PCM
        QCOMPARE(DecodedPcm::bytesPerSample(21), 3);
        QCOMPARE(QString::fromStdString(DecodedPcm::describe(4)),
                 QString("float"));
        QVERIFY(QString::fromStdString(DecodedPcm::describe(7))
                .contains("7"));
    }

    // Fewer channels than the stream's: each is used in turn, a mono
    // buffer in both channels of a stereo stream
    void fewer_channels_are_repeated() {
        std::vector<float> mono = { 0.5f, -0.25f };
        std::vector<float> out = convertBytes
            (encode(mono, DecodedPcm::Float), DecodedPcm::Float, 1, 2);
        QCOMPARE(out, (std::vector<float> { 0.5f, 0.5f, -0.25f, -0.25f }));

        std::vector<float> stereo = { 0.5f, -0.25f };
        out = convertBytes(encode(stereo, DecodedPcm::Float),
                           DecodedPcm::Float, 2, 3);
        QCOMPARE(out, (std::vector<float> { 0.5f, -0.25f, 0.5f }));
    }

    // More channels than the stream's: folded in, none dropped; 5.1 to
    // stereo keeps the centre channel, where a voice usually is
    void more_channels_are_folded_in() {
        std::vector<float> stereo = { 0.5f, -0.25f };
        std::vector<float> out = convertBytes
            (encode(stereo, DecodedPcm::Float), DecodedPcm::Float, 2, 1);
        QCOMPARE(out, (std::vector<float> { 0.125f }));

        // FL FR FC LFE BL BR
        std::vector<float> six = { 0.1f, 0.2f, 0.4f, 0.8f, 0.3f, 0.6f };
        out = convertBytes(encode(six, DecodedPcm::Float),
                           DecodedPcm::Float, 6, 2);
        QCOMPARE(out.size(), size_t(2));
        QVERIFY(std::fabs(out[0] - (0.1f + 0.4f + 0.3f) / 3.f) < 1e-6);
        QVERIFY(std::fabs(out[1] - (0.2f + 0.8f + 0.6f) / 3.f) < 1e-6);
    }

    // Buffers of any size in, reads of any size out: every frame once,
    // in order, and the counts agree
    void read_in_pieces_of_any_size() {
        DecodedPcm pcm(2);
        QCOMPARE(pcm.getChannelCount(), 2);
        QCOMPARE(pcm.getAvailable(), size_t(0));

        // Reads that leave a little behind, so that what has been read is
        // cleared away before more is added, and reads of more than is
        // there
        const size_t adds[] = { 1024, 1, 333, 2048, 7, 1024, 500 };
        const size_t reads[] = { 1000, 20, 1, 2000, 300, 37, 5000 };

        size_t added = 0, readFrames = 0;
        size_t r = 0;
        std::vector<float> got;

        for (size_t a : adds) {
            std::vector<uint8_t> bytes =
                encode(ramps(a, 2, added), DecodedPcm::Int16);
            QCOMPARE(pcm.add(bytes.data(), bytes.size(),
                             DecodedPcm::Int16, 2), a);
            added += a;
            QCOMPARE(pcm.getAvailable(), added - readFrames);

            std::vector<float> out(reads[r] * 2, -99.f);
            size_t n = pcm.read(out.data(), reads[r]);
            QCOMPARE(n, std::min(reads[r], added - readFrames));
            ++r;
            readFrames += n;
            got.insert(got.end(), out.begin(), out.begin() + ptrdiff_t(n * 2));
        }

        // The rest, and then nothing
        std::vector<float> out(added * 2);
        size_t n = pcm.read(out.data(), added);
        QCOMPARE(n, added - readFrames);
        readFrames += n;
        got.insert(got.end(), out.begin(), out.begin() + ptrdiff_t(n * 2));
        QCOMPARE(pcm.read(out.data(), 10), size_t(0));

        QCOMPARE(pcm.getFramesAdded(), int64_t(added));
        QCOMPARE(pcm.getFramesRead(), int64_t(added));
        QCOMPARE(pcm.getAvailable(), size_t(0));

        std::vector<float> expected = ramps(added, 2);
        QCOMPARE(got.size(), expected.size());
        for (size_t i = 0; i < got.size(); ++i) {
            QVERIFY2(std::fabs(got[i] - expected[i]) <= 1.0 / 65536.0,
                     qPrintable(QString("sample %1 is %2, not %3")
                                .arg(i).arg(got[i]).arg(expected[i])));
        }
    }

    // A buffer in another channel count is taken at the stream's count:
    // the format changed part way through
    void a_change_of_channels_keeps_the_stream_s_count() {
        DecodedPcm pcm(2);
        std::vector<uint8_t> stereo = encode({ 0.5f, -0.5f }, DecodedPcm::Int16);
        std::vector<uint8_t> mono = encode({ 0.25f, 0.75f }, DecodedPcm::Float);
        QCOMPARE(pcm.add(stereo.data(), stereo.size(), DecodedPcm::Int16, 2),
                 size_t(1));
        QCOMPARE(pcm.add(mono.data(), mono.size(), DecodedPcm::Float, 1),
                 size_t(2));
        std::vector<float> out(6);
        QCOMPARE(pcm.read(out.data(), 3), size_t(3));
        QCOMPARE(out, (std::vector<float> { 0.5f, -0.5f, 0.25f, 0.25f,
                                            0.75f, 0.75f }));
    }

    // What is not whole frames, or not PCM Tony knows, is not taken
    void part_frames_and_unknown_encodings_are_dropped() {
        DecodedPcm pcm(2);
        std::vector<uint8_t> bytes = encode({ 0.5f, -0.5f, 0.25f }, DecodedPcm::Int16);
        QCOMPARE(pcm.add(bytes.data(), bytes.size(), DecodedPcm::Int16, 2),
                 size_t(1));
        QCOMPARE(pcm.getBytesDropped(), int64_t(2));
        QCOMPARE(pcm.add(bytes.data(), bytes.size(), 5, 2), size_t(0));
        QCOMPARE(pcm.add(bytes.data(), bytes.size(), DecodedPcm::Int16, 0),
                 size_t(0));
        QCOMPARE(pcm.getBytesDropped(), int64_t(2 + 6 + 6));
        QCOMPARE(pcm.getFramesAdded(), int64_t(1));
        QCOMPARE(pcm.getAvailable(), size_t(1));
    }
};

#endif
