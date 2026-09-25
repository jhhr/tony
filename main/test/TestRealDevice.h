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

#ifndef TEST_REAL_DEVICE_H
#define TEST_REAL_DEVICE_H

// The checks that need the real audio device: the real MainWindow,
// opening the device the user chose in Tony, recording what its
// microphone hears of its own speakers.
//
// The reference is four minutes of a repeating two-second pattern: a
// second of a tone, for the live dots and pYIN, then a second holding
// three clicks at uneven spacing. Two recordings are made into one take
// with Play Reference While Recording on, and each recorded range of
// the take is lined up against the reference by its clicks. A take
// whose latency was compensated right has its clicks exactly where the
// reference has them.
//
// Not part of any automated run: see docs/manual-checklist.md for how
// to set it up. With no device to record from, or one that delivers
// nothing, the one check that runs is that Record does no harm.

#include "TestSignals.h"
#include "TestMainWindow.h"

#include "version.h"

#include "layer/Layer.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/fileio/WavFileReader.h"
#include "data/fileio/WavFileWriter.h"
#include "data/fileio/FileSource.h"
#include "base/RecordDirectory.h"
#include "transform/ModelTransformerFactory.h"
#include "widgets/InteractiveFileFinder.h"

#include <bqaudioio/AudioFactory.h>

#include <QObject>
#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

class TestRealDevice : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;
    static constexpr double songSeconds = 240.0;
    static constexpr double patternSeconds = 2.0;

    // Where in each pattern the clicks are, in seconds: uneven, so that
    // only the right lag lines all three up
    static std::vector<double> clickOffsets() { return { 1.10, 1.37, 1.71 }; }

    // How far a recorded range may sit from the reference and still be
    // called in time: a few milliseconds of sound travelling from the
    // speaker to the microphone, and the rest for the device
    static constexpr double toleranceMs = 10.0;

    // The recordings: where, and for how long
    static std::vector<double> takePositions() { return { 60.0, 150.0 }; }
    static constexpr int takeMs = 5000;

    struct Recorded {
        Coverage::Range range;
        double lagMs = 0.0;       // later than the reference if positive
        double peak = 0.0;        // normalised correlation at that lag
        double echoMs = 0.0;      // strongest later peak, after the first
        double echo = 0.0;        // ... and its height
        double stopSeconds = 0.0; // from Stop to the analysis merged
        int dots = 0;             // live dots drawn during it
        std::vector<double> channelLevels; // dB, of the raw recording
    };

    QTemporaryDir m_dir;
    TestMainWindow *m_window = nullptr;
    QTimer m_watchdog;
    QStringList m_dialogs;
    std::vector<float> m_reference;
    QString m_referencePath;
    double m_wholeSongSeconds = 0.0;
    std::vector<Recorded> m_recorded;
    bool m_deliveredNothing = false;

    static sv::sv_frame_t frames(double seconds) {
        return sv::sv_frame_t(std::lround(seconds * rate));
    }

    // The tone half of each pattern, and the clicks: short bursts of
    // noise, which line up by cross-correlation with no ambiguity of a
    // period
    std::vector<float> makeReference(bool clicksOnly) {
        std::vector<float> data(size_t(frames(songSeconds)), 0.f);
        const int clickFrames = int(0.003 * rate);
        std::mt19937 random(42);
        std::uniform_real_distribution<float> noise(-1.f, 1.f);
        std::vector<float> click(static_cast<size_t>(clickFrames), 0.f);
        for (int i = 0; i < clickFrames; ++i) {
            double window = 0.5 - 0.5 * std::cos(2.0 * TestSignals::kPi * i /
                                                 (clickFrames - 1));
            click[size_t(i)] = float(0.8 * window) * noise(random);
        }
        auto saw = TestSignals::sawtooth(220.5, rate, int(frames(1.0)), 0.25f);

        for (double t = 0.0; t + patternSeconds <= songSeconds;
             t += patternSeconds) {
            size_t at = size_t(frames(t));
            if (!clicksOnly) {
                std::copy(saw.begin(), saw.end(), data.begin() + long(at));
            }
            for (double offset : clickOffsets()) {
                size_t c = size_t(frames(t + offset));
                std::copy(click.begin(), click.end(), data.begin() + long(c));
            }
        }
        return data;
    }

    QString writeWav(const std::vector<float> &data, QString name) {
        QString path = m_dir.filePath(name);
        sv::WavFileWriter writer(path, rate, 1,
                                 sv::WavFileWriter::WriteToTarget);
        const float *ptr = data.data();
        if (!writer.isOK() ||
            !writer.writeSamples(&ptr, sv::sv_frame_t(data.size())) ||
            !writer.close()) {
            return {};
        }
        return path;
    }

    static std::vector<float> readMono(QString path, sv::sv_frame_t from,
                                       sv::sv_frame_t count) {
        sv::WavFileReader reader { sv::FileSource(path) };
        if (!reader.isOK()) return {};
        int ch = reader.getChannelCount();
        auto data = reader.getInterleavedFrames(from, count);
        std::vector<float> mono(data.size() / size_t(ch), 0.f);
        for (size_t i = 0; i < mono.size(); ++i) {
            for (int c = 0; c < ch; ++c) mono[i] += data[i * size_t(ch) + size_t(c)];
        }
        return mono;
    }

    // The lag, in frames within +-maxLag, at which the recorded audio
    // best matches the clicks of the reference, and how well: the peak of
    // the normalised cross-correlation. The tone half of the reference
    // is left out, so that only the clicks decide it
    static std::pair<long, double> bestLag(const std::vector<float> &clicks,
                                           const std::vector<float> &recorded,
                                           long maxLag,
                                           std::vector<double> *curve) {
        // clicks runs from maxLag before the recorded range to maxLag
        // after it
        double er = 0.0;
        for (float v : recorded) er += double(v) * v;
        long best = 0;
        double bestValue = -1.0;
        for (long lag = -maxLag; lag <= maxLag; ++lag) {
            double sum = 0.0, ec = 0.0;
            for (size_t i = 0; i < recorded.size(); ++i) {
                double c = clicks[size_t(long(i) + maxLag - lag)];
                sum += c * recorded[i];
                ec += c * c;
            }
            double value = (ec > 0.0 && er > 0.0) ? sum / std::sqrt(ec * er)
                : 0.0;
            if (curve) curve->push_back(value);
            if (value > bestValue) {
                bestValue = value;
                best = lag;
            }
        }
        return { best, bestValue };
    }

    static double levelDb(const std::vector<float> &data) {
        double sum = 0.0;
        for (float v : data) sum += double(v) * v;
        double rms = data.empty() ? 0.0 : std::sqrt(sum / double(data.size()));
        return rms > 0.0 ? 20.0 * std::log10(rms) : -200.0;
    }

    static bool analysed(Analyser *a) {
        return a && a->getLayer(Analyser::PitchTrack) &&
            a->getLayer(Analyser::Notes) &&
            a->getInitialAnalysisCompletion() >= 100 &&
            !a->isAnalysingRange() &&
            !sv::ModelTransformerFactory::getInstance()
            ->haveRunningTransformers();
    }

    // The newest raw recording: the file the device was recorded into,
    // with every channel it had
    QString newestRecording() {
        QString newest;
        QDateTime newestTime;
        QDirIterator it(sv::RecordDirectory::getRecordContainerDirectory(),
                        { "recorded-*.wav" }, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString path = it.next();
            QDateTime t = QFileInfo(path).lastModified();
            if (newest == "" || t > newestTime) {
                newest = path;
                newestTime = t;
            }
        }
        return newest;
    }

    // Not a slot: QtTest would run it as a test
    void dismissDialog() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) return;
        QString description = modal->windowTitle();
        if (auto box = qobject_cast<QMessageBox *>(modal)) {
            description += ": " + box->text();
            QList<QAbstractButton *> buttons = box->buttons();
            m_dialogs.push_back(description);
            if (!buttons.isEmpty()) {
                buttons.last()->click();
                return;
            }
        } else {
            m_dialogs.push_back(description);
        }
        if (auto dialog = qobject_cast<QDialog *>(modal)) dialog->reject();
        else modal->close();
    }

    bool haveDevice() { return m_window && m_window->haveAudioDevice(); }
    bool canRecord() { return m_window && m_window->haveRecordingDevice(); }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        QSettings().clear();

        // The devices Tony uses, from Tony's own settings: whatever was
        // chosen in its Playback menu is what is checked here. The rest of
        // Tony's settings are not read, and nothing is written to them
        {
            QSettings tony("sonic-visualiser", "Tony");
            tony.beginGroup("Preferences");
            QSettings mine;
            mine.beginGroup("Preferences");
            for (QString key : tony.childKeys()) {
                if (key.startsWith("audio-")) {
                    mine.setValue(key, tony.value(key));
                    qInfo("Tony's setting %s = \"%s\"", qPrintable(key),
                          qPrintable(tony.value(key).toString()));
                }
            }
            mine.setValue(QString("network-permission-%1").arg(TONY_VERSION),
                          false);
            mine.endGroup();
        }

        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.setValue("playrefwhilerecording", true);
        settings.setValue("preroll", false);
        settings.setValue("recordintoselection", false);
        settings.endGroup();
        SingingTakes::setOverwriteConfirmationWanted(true);

        sv::InteractiveFileFinder::getInstance()
            ->setApplicationSessionExtension("ton");
        sv::RecordDirectory::setRecordContainerDirectory
            (m_dir.filePath("recorded"));

        connect(&m_watchdog, &QTimer::timeout,
                this, [this]() { dismissDialog(); });
        m_watchdog.start(50);

        m_reference = makeReference(false);
        m_referencePath = writeWav(m_reference, "reference.wav");
        QVERIFY(m_referencePath != "");

        // TONY_DEVICE_CHECK_FAKE=n checks the check itself, with no
        // hardware: the fake device, its output fed back into its input as
        // speakers into a microphone, reporting a round trip of 2048
        // frames and taking 2048 + n. With n = 0 everything passes; the
        // recordings come out n frames late otherwise
        FakeAudioIO::Config fake;
        bool useFake = qEnvironmentVariableIsSet("TONY_DEVICE_CHECK_FAKE");
        if (useFake) {
            fake.playbackLatency = 1024;
            fake.recordLatency = 1024;
            fake.inputDelay = 2048 +
                qEnvironmentVariableIntValue("TONY_DEVICE_CHECK_FAKE");
            fake.loopback = true;
            qInfo("the fake device, %d frames from output to input",
                  fake.inputDelay);
        }
        m_window = new TestMainWindow(fake);
        m_window->setUseRealDevice(!useFake);
        m_window->setPlayReferenceWhileRecording(true);
        m_window->resize(1200, 800);
        m_window->show();

        // The whole song's analysis, timed: what Stop must take much less
        // time than
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(m_window->openPath(m_referencePath,
                                    MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 300000);
        m_wholeSongSeconds = double(timer.elapsed()) / 1000.0;
        qInfo("analysis of the whole %.0f s reference took %.1f s",
              songSeconds, m_wholeSongSeconds);
    }

    void cleanupTestCase() {
        m_watchdog.stop();
        if (m_window) {
            if (m_window->recordTarget()->isRecording()) m_window->doRecord();
            QTRY_VERIFY_WITH_TIMEOUT
                (!sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(), 30000);
            m_window->doCloseSession();
            delete m_window;
            m_window = nullptr;
        }
        sv::RecordDirectory::setRecordContainerDirectory("");
    }

    // Which device, and what it says of itself
    void device() {
        std::vector<std::string> names =
            breakfastquay::AudioFactory::getImplementationNames();
        QStringList implementations;
        for (auto n : names) implementations << QString::fromStdString(n);
        qInfo("audio drivers built in: %s", qPrintable(implementations.join(", ")));

        if (!haveDevice()) {
            qInfo("no audio device could be opened: %s",
                  qPrintable(m_dialogs.join(" | ")));
            m_dialogs.clear();
            QSKIP("no audio device: only no_input_does_no_harm applies");
        }
        qInfo("playback latency reported: %lld frames (%.1f ms)",
              (long long)m_window->playSource()->getTargetPlayLatency(),
              double(m_window->playSource()->getTargetPlayLatency()) * 1000.0 / rate);
        qInfo("record latency reported: %d frames (%.1f ms)",
              m_window->recordTarget()->getSystemRecordLatency(),
              m_window->recordTarget()->getSystemRecordLatency() * 1000.0 / rate);
        QVERIFY2(canRecord(), "the device can play but not record: is a "
                 "microphone connected and allowed?");
    }

    // Two recordings into one take, at two places in the song, with the
    // reference playing: the microphone hears it and the take records it
    void record_the_reference_through_the_air() {
        if (!canRecord()) QSKIP("no device to record from");

        for (double at : takePositions()) {
            Recorded r;
            m_window->seekTo(frames(at));
            m_window->doRecord();
            QVERIFY2(m_window->recordTarget()->isRecording(),
                     qPrintable("Record did not start: " + m_dialogs.join(" | ")));
            QTest::qWait(takeMs);
            if (auto dots = sv::ModelById::getAs<sv::SparseTimeValueModel>
                (m_window->realtimeModelId())) {
                r.dots = dots->getEventCount();
            }
            sv::sv_frame_t received =
                m_window->recordTarget()->getFramesReceived();
            QElapsedTimer timer;
            timer.start();
            m_window->doRecord();
            QVERIFY(!m_window->recordTarget()->isRecording());
            if (received == 0) {
                m_deliveredNothing = true;
                QTRY_VERIFY(!m_window->recordingInProgress());
                QFAIL(qPrintable(QString("the device opened, but in %1 s it "
                                         "delivered no input at all")
                                 .arg(takeMs / 1000)));
            }
            QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 60000);
            r.stopSeconds = double(timer.elapsed()) / 1000.0;

            // Each channel of what the device delivered, to see which
            // input the microphone is on
            sv::WavFileReader reader { sv::FileSource(newestRecording()) };
            QVERIFY(reader.isOK());
            int channels = reader.getChannelCount();
            auto data = reader.getInterleavedFrames(0, reader.getFrameCount());
            for (int c = 0; c < channels; ++c) {
                std::vector<float> one;
                for (size_t i = size_t(c); i < data.size(); i += size_t(channels)) {
                    one.push_back(data[i]);
                }
                r.channelLevels.push_back(levelDb(one));
            }
            m_recorded.push_back(r);
        }

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(ranges.size(), m_recorded.size());
        for (size_t i = 0; i < ranges.size(); ++i) {
            m_recorded[i].range = ranges[i];
        }
        QVERIFY(m_dialogs.isEmpty());
    }

    // Checklist: no input device, or one that records nothing: Record
    // does nothing harmful, and the next file opened is analysed as usual
    void no_input_does_no_harm() {
        if (canRecord() && !m_deliveredNothing) {
            QSKIP("the device records: this is for one that does not");
        }
        m_dialogs.clear();
        m_window->seekTo(frames(30.0));
        m_window->doRecord();
        QTest::qWait(1000);
        if (m_window->recordTarget()->isRecording()) m_window->doRecord();
        QTest::qWait(500);
        qInfo("Record with no input: %s",
              m_dialogs.isEmpty() ? "no dialog"
              : qPrintable("dialog " + m_dialogs.join(" | ")));
        m_dialogs.clear();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(!m_window->recordingInProgress());
        QVERIFY(!m_window->recordingAsSingingTrack());
        QVERIFY2(!m_window->takes()->haveTake(),
                 "a recording of nothing was kept as a take");

        auto other = TestSignals::sawtooth(294.0, rate, int(frames(3.0)), 0.5f);
        m_window->discardModifications();
        QCOMPARE(m_window->openPath(writeWav(other, "next.wav"),
                                    MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        auto pitch = sv::ModelById::getAs<sv::SparseTimeValueModel>
            (m_window->analyser()->getLayer(Analyser::PitchTrack)->getModel());
        QVERIFY(pitch && pitch->getEventCount() > 10);

        // With no device at all, MainWindowBase tries again to open one
        // for every file opened, and says so each time when it cannot.
        // That warning is expected; anything else is not
        QStringList others;
        for (QString d : m_dialogs) {
            if (!d.startsWith("Couldn't open audio device")) others << d;
        }
        qInfo("%d warnings about the device while opening the next file",
              int(m_dialogs.size() - others.size()));
        m_dialogs.clear();
        QVERIFY2(others.isEmpty(), qPrintable(others.join(" | ")));
    }

    // Checklist: the take lines up with the reference (latency on this
    // machine), and every recording in one take does (several phrases)
    void takes_line_up_with_the_reference() {
        if (m_recorded.empty()) QSKIP("nothing was recorded");
        const std::vector<float> clicks = makeReference(true);
        const long maxLag = long(frames(0.3));
        QString audio = m_window->takes()->getAudioPath();

        for (Recorded &r : m_recorded) {
            // Leave out the edges of the range: the start is where the
            // reference only began to play
            sv::sv_frame_t from = r.range.start + frames(0.5);
            sv::sv_frame_t to = r.range.end - frames(0.2);
            QVERIFY(to - from > frames(2.0));
            auto recorded = readMono(audio, from, to - from);
            QCOMPARE(sv::sv_frame_t(recorded.size()), to - from);

            // Only the half of each pattern that holds the clicks: the
            // tone of the other half is nothing to line up by, and would
            // count against how well the clicks match
            for (size_t i = 0; i < recorded.size(); ++i) {
                double t = std::fmod(double(from + sv::sv_frame_t(i)) / rate,
                                     patternSeconds);
                if (t < 1.0) recorded[i] = 0.f;
            }
            std::vector<float> ref(clicks.begin() + long(from) - maxLag,
                                   clicks.begin() + long(to) + maxLag);
            std::vector<double> curve;
            auto best = bestLag(ref, recorded, maxLag, &curve);
            r.lagMs = double(best.first) * 1000.0 / rate;
            r.peak = best.second;

            // The strongest match later than the first, beyond the length
            // of a click and its ringing: an echo of the take played back
            // out of the speakers would put the clicks there a second time
            for (long lag = best.first + long(frames(0.015)); lag <= maxLag;
                 ++lag) {
                double v = curve[size_t(lag + maxLag)];
                if (v > r.echo) {
                    r.echo = v;
                    r.echoMs = double(lag - best.first) * 1000.0 / rate;
                }
            }

            qInfo("recording at %.1f s: clicks %+.1f ms from the reference "
                  "(match %.2f); stop took %.1f s; %d live dots; channels "
                  "(dB) %s",
                  double(r.range.start) / rate, r.lagMs, r.peak,
                  r.stopSeconds, r.dots,
                  qPrintable([&]() {
                      QStringList l;
                      for (double d : r.channelLevels) {
                          l << QString::number(d, 'f', 1);
                      }
                      return l.join(" ");
                  }()));
        }

        for (const Recorded &r : m_recorded) {
            QVERIFY2(r.peak > 0.1,
                     qPrintable(QString("the clicks of the reference are "
                                        "hardly in the recording at %1 s "
                                        "(match %2): can the microphone hear "
                                        "the speakers?")
                                .arg(double(r.range.start) / rate)
                                .arg(r.peak, 0, 'f', 2)));
            QVERIFY2(std::fabs(r.lagMs) <= toleranceMs,
                     qPrintable(QString("the recording at %1 s is %2 ms %3 "
                                        "the reference")
                                .arg(double(r.range.start) / rate)
                                .arg(std::fabs(r.lagMs), 0, 'f', 1)
                                .arg(r.lagMs > 0 ? "later than" : "earlier "
                                     "than")));
        }
        if (m_recorded.size() > 1) {
            double spread = std::fabs(m_recorded[0].lagMs -
                                      m_recorded[1].lagMs);
            QVERIFY2(spread <= 5.0,
                     qPrintable(QString("the two recordings of one take are "
                                        "%1 ms apart in their timing")
                                .arg(spread, 0, 'f', 1)));
        }
    }

    // Checklist: nothing of the take in the speakers while recording.
    // If the input were played back out, the microphone would hear every
    // click a second time, a round trip later
    void nothing_of_the_take_comes_back_out() {
        if (m_recorded.empty()) QSKIP("nothing was recorded");
        for (const Recorded &r : m_recorded) {
            qInfo("recording at %.1f s: strongest later match %.2f at "
                  "+%.1f ms, against %.2f for the clicks themselves",
                  double(r.range.start) / rate, r.echo, r.echoMs, r.peak);
            QVERIFY2(r.echo < 0.5 * r.peak,
                     qPrintable(QString("the clicks come again %1 ms later "
                                        "at %2 of their strength: is the "
                                        "input being played back out, by "
                                        "Tony or by the system (\"Listen to "
                                        "this device\")?")
                                .arg(r.echoMs, 0, 'f', 1)
                                .arg(r.echo / r.peak, 0, 'f', 2)));
        }
    }

    // Checklist: how long Stop takes on a four-minute song: new pitch only
    // where the singing was, not a whole-song analysis
    void stop_is_quicker_than_a_whole_song() {
        if (m_recorded.empty()) QSKIP("nothing was recorded");
        for (const Recorded &r : m_recorded) {
            QVERIFY2(r.stopSeconds < 0.5 * m_wholeSongSeconds,
                     qPrintable(QString("Stop took %1 s, the whole song's "
                                        "analysis %2 s")
                                .arg(r.stopSeconds, 0, 'f', 1)
                                .arg(m_wholeSongSeconds, 0, 'f', 1)));
        }
    }

    // Checklist: live dots appear, whichever input the microphone is on
    void live_dots_were_drawn() {
        if (m_recorded.empty()) QSKIP("nothing was recorded");
        for (const Recorded &r : m_recorded) {
            QVERIFY2(r.dots > 10,
                     qPrintable(QString("only %1 live dots during the "
                                        "recording at %2 s")
                                .arg(r.dots)
                                .arg(double(r.range.start) / rate)));
        }
    }
};

#endif
