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

#ifndef TEST_INPUT_LEVEL_H
#define TEST_INPUT_LEVEL_H

// Tier 2: the level of what the microphone gives. A recording's peak
// and where it clipped, through files as the take's scan reads them;
// the input meter's hold, fall and clip light; and Check Input Level's
// advice. No window and no device.

#include "../InputChannel.h"
#include "../InputLevel.h"
#include "../VoiceThreshold.h"

#include "TestSignals.h"

#include "data/fileio/WavFileWriter.h"

#include <QObject>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <vector>

class TestInputLevel : public QObject
{
    Q_OBJECT

    static constexpr double kRate = 48000.0;

    QTemporaryDir m_dir;
    int m_counter = 0;

    // A 24-bit WAV, as a take is recorded at: samples over full scale
    // are held at its largest code, as a converter clips
    QString writeWav(const std::vector<float> &interleaved, int channels) {
        QString path = m_dir.filePath(QString("rec-%1.wav").arg(++m_counter));
        sv::WavFileWriter writer(path, kRate, channels,
                                 sv::WavFileWriter::WriteToTarget);
        std::vector<float> clipped(interleaved);
        for (float &v : clipped) v = std::max(-1.f, std::min(1.f, v));
        sv::floatvec_t data(clipped.begin(), clipped.end());
        if (!writer.isOK() || !writer.putInterleavedFrames(data) ||
            !writer.close()) {
            return {};
        }
        return path;
    }

    // A tone of hz at amplitude, overdriven by a factor of drive (1: as
    // it is), for frames
    static std::vector<float> tone(double hz, double amplitude,
                                   double drive, int frames) {
        auto s = TestSignals::sine(hz, kRate, frames, amplitude * drive);
        for (float &v : s) v = std::max(-1.f, std::min(1.f, v));
        return s;
    }

    static std::vector<float> interleave(const std::vector<float> &left,
                                         const std::vector<float> &right) {
        std::vector<float> out;
        for (size_t i = 0; i < left.size(); ++i) {
            out.push_back(left[i]);
            out.push_back(right[i]);
        }
        return out;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void levels_in_dbfs() {
        QCOMPARE(InputLevel::dbfs(1.0), 0.0);
        QVERIFY(std::fabs(InputLevel::dbfs(0.5) + 6.0206) < 1e-3);
        QVERIFY(std::fabs(InputLevel::dbfs(-0.1) + 20.0) < 1e-9);
        QCOMPARE(InputLevel::dbfs(0.0), -200.0);
    }

    // A take that never reaches full scale: its peak, no clip
    void a_peak_under_full_scale() {
        QString path = writeWav(tone(220.0, 0.316, 1.0, 48000), 1);
        QString error;
        auto scan = InputLevel::scanFile(path, -1, 0, -1, error);
        QCOMPARE(error, QString());
        QCOMPARE(scan.frames, sv::sv_frame_t(48000));
        QVERIFY(std::fabs(InputLevel::dbfs(scan.peak) + 10.0) < 0.05);
        QVERIFY(!scan.clipped());
    }

    // An overdriven phrase in the middle of a quiet take: clipped, its
    // runs where the phrase is, and told of as one place
    void a_clipped_phrase_is_found_where_it_is() {
        std::vector<float> signal = tone(220.0, 0.1, 1.0, 48000);
        auto loud = tone(220.0, 0.9, 2.0, 24000);
        std::copy(loud.begin(), loud.end(), signal.begin() + 12000);
        QString path = writeWav(signal, 1);
        QString error;
        auto scan = InputLevel::scanFile(path, -1, 0, -1, error);
        QCOMPARE(error, QString());
        QVERIFY(scan.clipped());
        QVERIFY(scan.peak >= InputLevel::kFullScale);
        for (const auto &clip : scan.clips) {
            QVERIFY(clip.start >= 12000 && clip.end <= 36000);
            QVERIFY(clip.end - clip.start >= InputLevel::kClipRun);
        }
        // Two half periods of 220 Hz clipped in each period
        QVERIFY(scan.clips.size() > 80);
        auto places = InputLevel::places
            (scan.clips, sv::sv_frame_t(InputLevel::kPlaceSeconds * kRate));
        QCOMPARE(int(places.size()), 1);
        QVERIFY(places[0].start >= 12000 && places[0].start < 12200);
        QVERIFY(places[0].end > 35800 && places[0].end <= 36000);
    }

    // A sample or two at full scale is a peak touching it, not a clip:
    // a click or a sibilant's crest
    void a_single_sample_at_full_scale_is_no_clip() {
        std::vector<float> signal = tone(220.0, 0.2, 1.0, 9600);
        signal[1000] = 1.0f;
        signal[5000] = -1.0f;
        signal[5001] = -1.0f;
        QString path = writeWav(signal, 1);
        QString error;
        auto scan = InputLevel::scanFile(path, -1, 0, -1, error);
        QVERIFY(scan.peak >= InputLevel::kFullScale);
        QVERIFY(!scan.clipped());
        // Three in a row are
        signal[5002] = -1.0f;
        path = writeWav(signal, 1);
        scan = InputLevel::scanFile(path, -1, 0, -1, error);
        QCOMPARE(int(scan.clips.size()), 1);
        QCOMPARE(scan.clips[0].start, sv::sv_frame_t(5000));
        QCOMPARE(scan.clips[0].end, sv::sv_frame_t(5003));
    }

    // Only what went into the take is scanned, and its clips are
    // counted from the start of the file
    void a_part_of_the_file() {
        std::vector<float> signal = tone(220.0, 0.1, 1.0, 48000);
        for (int i = 2000; i < 2010; ++i) signal[size_t(i)] = 1.f;
        for (int i = 30000; i < 30010; ++i) signal[size_t(i)] = 1.f;
        QString path = writeWav(signal, 1);
        QString error;
        auto scan = InputLevel::scanFile(path, -1, 24000, 12000, error);
        QCOMPARE(scan.frames, sv::sv_frame_t(12000));
        QCOMPARE(int(scan.clips.size()), 1);
        QCOMPARE(scan.clips[0].start, sv::sv_frame_t(30000));
        QCOMPARE(scan.clips[0].end, sv::sv_frame_t(30010));
        // A run at the very end of what is scanned ends there
        scan = InputLevel::scanFile(path, -1, 24000, 6005, error);
        QCOMPARE(int(scan.clips.size()), 1);
        QCOMPARE(scan.clips[0].end, sv::sv_frame_t(30005));
    }

    // With one input judged, the other's clipping is not the take's,
    // but every input's peak is kept
    void one_input_judged() {
        auto quiet = tone(220.0, 0.2, 1.0, 9600);
        auto clipping = tone(330.0, 0.9, 3.0, 9600);
        QString path = writeWav(interleave(quiet, clipping), 2);
        QString error;
        auto both = InputLevel::scanFile(path, -1, 0, -1, error);
        QVERIFY(both.clipped());
        auto first = InputLevel::scanFile(path, 0, 0, -1, error);
        QVERIFY(!first.clipped());
        QVERIFY(std::fabs(InputLevel::dbfs(first.peak) -
                          InputLevel::dbfs(0.2)) < 0.05);
        QCOMPARE(int(first.channelPeaks.size()), 2);
        QVERIFY(first.channelPeaks[1] >= InputLevel::kFullScale);
        auto second = InputLevel::scanFile(path, 1, 0, -1, error);
        QVERIFY(second.clipped());
    }

    void a_file_that_is_not_there() {
        QString error;
        auto scan = InputLevel::scanFile(m_dir.filePath("none.wav"), -1, 0,
                                         -1, error);
        QVERIFY(error != "");
        QCOMPARE(scan.frames, sv::sv_frame_t(0));
        QVERIFY(!scan.clipped());
    }

    // Given a block at a time, as a long recording is read: the same as
    // given whole, a run across the blocks' edge included
    void blocks_are_one_recording() {
        std::vector<float> signal(1000, 0.1f);
        for (int i = 498; i < 503; ++i) signal[size_t(i)] = -1.f;
        InputLevel::Scanner whole(1);
        whole.add(signal.data(), 1000);
        auto a = whole.finish();
        InputLevel::Scanner parts(1);
        parts.add(signal.data(), 500);
        parts.add(signal.data() + 500, 500);
        auto b = parts.finish();
        QCOMPARE(int(a.clips.size()), 1);
        QCOMPARE(int(b.clips.size()), 1);
        QCOMPARE(b.clips[0].start, sv::sv_frame_t(498));
        QCOMPARE(b.clips[0].end, sv::sv_frame_t(503));
        QCOMPARE(b.frames, sv::sv_frame_t(1000));
    }

    void places_join_near_clips() {
        using C = InputLevel::Clip;
        auto p = InputLevel::places({ C{ 0, 10 }, C{ 100, 110 },
                                      C{ 5000, 5010 }, C{ 5600, 5700 } },
                                    1000);
        QCOMPARE(int(p.size()), 2);
        QCOMPARE(p[0].start, sv::sv_frame_t(0));
        QCOMPARE(p[0].end, sv::sv_frame_t(110));
        QCOMPARE(p[1].start, sv::sv_frame_t(5000));
        QCOMPARE(p[1].end, sv::sv_frame_t(5700));
        QVERIFY(InputLevel::places({}, 1000).empty());
    }

    // The bar goes up at once and falls at 20 dB a second; the hold
    // stays 1.5 s at the highest and then falls as fast; neither goes
    // under the floor
    void the_meter_holds_and_falls() {
        InputLevel::Meter m;
        QCOMPARE(m.bar(0), InputLevel::Meter::kFloorDb);
        QVERIFY(!m.moving(0));
        m.peak(0.1f, 1000);                 // -20 dBFS
        QVERIFY(std::fabs(m.bar(1000) + 20.0) < 1e-6);
        QVERIFY(std::fabs(m.bar(1500) + 30.0) < 1e-6);
        QVERIFY(std::fabs(m.hold(2400) + 20.0) < 1e-6);
        QVERIFY(std::fabs(m.hold(3000) + 30.0) < 1e-6);
        QCOMPARE(m.bar(10000), InputLevel::Meter::kFloorDb);
        QCOMPARE(m.hold(10000), InputLevel::Meter::kFloorDb);
        QVERIFY(m.moving(2000));
        QVERIFY(!m.moving(10000));

        // A quieter peak under the falling bar leaves it falling; the
        // hold stays at the highest
        m.peak(0.1f, 20000);
        m.peak(0.01f, 20200);               // -40, under the bar's -24
        QVERIFY(std::fabs(m.bar(20200) + 24.0) < 1e-6);
        QVERIFY(std::fabs(m.hold(20200) + 20.0) < 1e-6);
        // A louder one starts both again
        m.peak(0.5f, 20300);
        QVERIFY(std::fabs(m.bar(20300) + 6.0206) < 1e-3);
        QVERIFY(std::fabs(m.hold(21700) + 6.0206) < 1e-3);
        QVERIFY(!m.clipped());
    }

    // The light comes on at full scale and stays until it is put out
    void the_clip_light_stays() {
        InputLevel::Meter m;
        m.peak(0.998f, 0);
        QVERIFY(!m.clipped());
        m.peak(InputLevel::kFullScale, 100);
        QVERIFY(m.clipped());
        m.peak(0.01f, 60000);
        QVERIFY(m.clipped());
        m.setClipped(false);
        QVERIFY(!m.clipped());
    }

    // The meter's peak of a choice: the input chosen, or the louder
    void the_meters_input() {
        QCOMPARE(InputChannel::peakOf(InputChannel::kBoth, 0.2f, 0.5f), 0.5f);
        QCOMPARE(InputChannel::peakOf(0, 0.2f, 0.5f), 0.2f);
        QCOMPARE(InputChannel::peakOf(1, 0.2f, 0.5f), 0.5f);
    }

    // The gain that puts the loudest peak at -10 dBFS, to a whole dB
    void the_gain_change() {
        QCOMPARE(InputLevel::kTargetPeakDbfs, -10.0);
        QCOMPARE(InputLevel::gainChange(-3.0), -7.0);
        QCOMPARE(InputLevel::gainChange(-24.4), 14.0);
        QCOMPARE(InputLevel::gainChange(-10.2), 0.0);
    }

    // The silence's level is the middle of its readings: a click in it
    // does not move it
    void the_noise_floor() {
        QCOMPARE(InputLevel::noiseFloor({}), -200.0);
        QCOMPARE(InputLevel::noiseFloor({ -62.0, -61.0, -20.0 }), -61.0);
        QCOMPARE(InputLevel::noiseFloor({ -70.0, -60.0, -62.0, -64.0 }), -63.0);
    }

    // At least 5 dB over the noise's peaks, from the choices; Off where
    // the tracker's own floor keeps the noise out; the highest choice
    // where none is enough
    void the_suggested_threshold() {
        QCOMPARE(InputLevel::suggestedThreshold(-200.0), VoiceThreshold::kOff);
        QCOMPARE(InputLevel::suggestedThreshold(-70.0), VoiceThreshold::kOff);
        QCOMPARE(InputLevel::suggestedThreshold(-69.0), -50.0);
        QCOMPARE(InputLevel::suggestedThreshold(-55.0), -50.0);
        QCOMPARE(InputLevel::suggestedThreshold(-54.0), -45.0);
        QCOMPARE(InputLevel::suggestedThreshold(-35.0), -30.0);
        QCOMPARE(InputLevel::suggestedThreshold(-25.0), -20.0);
        QCOMPARE(InputLevel::suggestedThreshold(-10.0), -15.0);
        for (double noise = -80.0; noise < 0.0; noise += 0.5) {
            const double t = InputLevel::suggestedThreshold(noise);
            bool offered = false;
            for (double c : VoiceThreshold::choices()) offered |= (c == t);
            QVERIFY(offered);
        }
    }
};

#endif
