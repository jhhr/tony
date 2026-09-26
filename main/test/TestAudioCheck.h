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

#ifndef TEST_AUDIO_CHECK_H
#define TEST_AUDIO_CHECK_H

// Tier 5, as TestRecordWorkflow: the audio check (AudioCheckRunner) on
// the real MainWindow, recording from the fake device with its output
// looped back into its input, as an earcup held to the mic. A run is
// two punch-ins of two sweeps each on the first 11 s of the calibration
// reference, about 13 s of real time. The windows have no development
// checks, even in a development build: TestDevChecks has those.
//
// The same dialog watchdog as TestRecordWorkflow's: a dialog would
// block the test for ever, so it is dismissed, and the test fails in
// cleanup().

#include "TestSignals.h"
#include "TestMainWindow.h"

#include "../AudioCheckRunner.h"
#include "../CalibrateAudioDialog.h"
#include "../LatencyCheck.h"

#include "version.h"

#include "layer/Layer.h"
#include "data/fileio/WavFileWriter.h"
#include "base/PlayParameters.h"
#include "base/PlayParameterRepository.h"
#include "base/RecordDirectory.h"
#include "transform/ModelTransformerFactory.h"
#include "widgets/InteractiveFileFinder.h"

#include <QObject>
#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <vector>

class TestAudioCheck : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;

    // What Windows' mixer and phones run at. The reference is made at
    // rate whatever the device runs at, and a take recorded at this one
    // is converted to it as it is spliced
    static constexpr double otherRate = 48000.0;

    // What the device reports, and what the round trip really is
    static constexpr int reportedOut = 2 * 4096;
    static constexpr int reportedIn = 4096;
    static constexpr int roundTrip = 3 * 4096 + 123;

    QTemporaryDir m_dir;
    TestMainWindow *m_window = nullptr;
    QTimer m_watchdog;
    QStringList m_dialogs;

    // What the runner said when the run ended, and how often it said it;
    // and every progress it reported
    AudioCheckResult m_result;
    int m_finished = 0;
    std::vector<AudioCheckRunner::Progress> m_progress;

    void makeWindow(FakeAudioIO::Config config, bool installDevice = true) {
        delete m_window;
        m_window = new TestMainWindow(config, installDevice);
#ifdef TONY_DEV_CHECKS
        // The calibration alone, as a release build has it: with the
        // dev checks there, the dialog carries on into them by default
        // (TestDevChecks)
        m_window->doDeleteDevChecks();
#endif
        m_result = AudioCheckResult();
        m_finished = 0;
        m_progress.clear();
        connect(m_window->audioCheck(), &AudioCheckRunner::finished,
                this, [this](const AudioCheckResult &result) {
                    m_result = result;
                    ++m_finished;
                });
        connect(m_window->audioCheck(), &AudioCheckRunner::progress,
                this, [this](const AudioCheckRunner::Progress &progress) {
                    m_progress.push_back(progress);
                });
    }

    // Speakers into the microphone: the output comes back as input,
    // roundTrip frames late, while the device reports latencies that
    // add up to less
    static FakeAudioIO::Config loopback() {
        FakeAudioIO::Config config;
        config.playbackLatency = reportedOut;
        config.recordLatency = reportedIn;
        config.inputDelay = roundTrip;
        config.loopback = true;
        return config;
    }

    // Two punch-ins of two events each, on the calibration reference cut
    // short after its fifth event. The event at 4.7 s is left out: it is
    // too close to the one before for a punch-in to end between them
    AudioCheckRunner::Plan shortPlan() {
        AudioCheckRunner::Plan plan;
        plan.layout = LatencyCheck::calibrationLayout();
        plan.layout.length = plan.layout.events[5].sweepStart;
        plan.layout.events.resize(5);
        plan.punchIns = 2;
        plan.eventsEach = 2;
        plan.referencePath = m_dir.filePath("reference.wav");
        return plan;
    }

    void startCheck() { startCheck(shortPlan()); }
    void runCheck() { runCheck(shortPlan()); }

    void startCheck(const AudioCheckRunner::Plan &plan,
                    bool discard = true) {
        // As answering "No" to "do you want to save?", which the check
        // asks before it replaces the session
        if (discard) m_window->discardModifications();
        QVERIFY(m_window->audioCheck()->start(plan));
        QVERIFY(m_window->audioCheck()->isRunning());
    }

    void runCheck(const AudioCheckRunner::Plan &plan, bool discard = true) {
        m_result = AudioCheckResult();
        m_finished = 0;
        startCheck(plan, discard);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_finished > 0, 60000);
        QCOMPARE(m_finished, 1);
        QVERIFY(!m_window->audioCheck()->isRunning());
    }

    // The short plan with one punch-in of its own, which judges the sweep
    // at 7.2 s; its lead-in starts in the tone of the sweep before, so
    // that the fake device's first audible sample is the first played
    AudioCheckRunner::Plan onePunchIn() {
        AudioCheckRunner::Plan plan = shortPlan();
        plan.ranges = { LatencyCheck::PunchIn(6.3, 8.3) };
        return plan;
    }

    // Every measured round trip kept, as the settings hold it
    static QStringList storedLatency() {
        QSettings settings;
        settings.beginGroup("LatencyCalibration");
        QStringList all;
        for (const QString &key : settings.allKeys()) {
            all << key + "=" + settings.value(key).toString();
        }
        settings.endGroup();
        all.sort();
        return all;
    }

    // A reference in the directory the check writes its own to, as a
    // check before this one left it, opened and analysed; its path
    QString openEarlierCheckReference() {
        QDir().mkpath(AudioCheckRunner::referenceDirectory());
        const QString path = AudioCheckRunner::nextReferencePath
            (AudioCheckRunner::referenceDirectory(), "");
        const AudioCheckRunner::Plan plan = shortPlan();
        const std::vector<float> samples = LatencyCheck::generate(plan.layout);
        sv::WavFileWriter writer(path, plan.layout.rate, 1,
                                 sv::WavFileWriter::WriteToTarget);
        const float *data = samples.data();
        if (!writer.isOK() ||
            !writer.writeSamples(&data, sv::sv_frame_t(samples.size())) ||
            !writer.close()) {
            return {};
        }
        m_window->discardModifications();
        if (m_window->openPath(path, MainWindow::ReplaceSession) !=
            MainWindow::FileOpenSucceeded) {
            return {};
        }
        Analyser *analyser = m_window->analyser();
        QTest::qWaitFor([analyser]() {
            return analyser->getLayer(Analyser::PitchTrack) &&
                analyser->getInitialAnalysisCompletion() >= 100 &&
                !sv::ModelTransformerFactory::getInstance()
                ->haveRunningTransformers();
        }, 30000);
        return path;
    }

    // The application's data directory, where a check writes its
    // references unless told otherwise, is Qt's test location while one
    // of these lives
    struct TestDataLocation {
        TestDataLocation() { QStandardPaths::setTestModeEnabled(true); }
        ~TestDataLocation() { QStandardPaths::setTestModeEnabled(false); }
    };

    // The devices the Preferences name, as the device menus write them
    // when the driver is left alone. The fake device takes no notice
    static void setDevices(QString output, QString input) {
        QSettings settings;
        settings.beginGroup("Preferences");
        settings.setValue("audio-playback-device", output);
        settings.setValue("audio-record-device", input);
        settings.endGroup();
    }

    static LatencyCalibration::Key key(QString output, QString input) {
        LatencyCalibration::Key key;
        key.playbackDevice = output;
        key.recordDevice = input;
        key.rate = rate;
        return key;
    }

    // A phone's route as OboeAudioIO reports it: the speaker and the
    // phone's own microphone, or a Bluetooth headset's output and the
    // same microphone, each opened as AAudio opens such a device
    static AudioRoute::Route phoneRoute(bool bluetooth = false) {
        AudioRoute::Route route;
        route.driver = "oboe";
        route.output.id = bluetooth ? 41 : 3;
        route.output.type = bluetooth ? 8 : 2;
        route.output.productName = bluetooth ? "Headset X" : "Pixel 7";
        route.hasInput = true;
        route.input.id = 7;
        route.input.type = 15;
        route.input.productName = "Pixel 7";
        route.rate = rate;
        route.outputStreams = bluetooth ?
            "AAudio, 44100 Hz, Shared, burst 240, buffer 480 of 3840" :
            "AAudio (MMAP), 44100 Hz, Exclusive, burst 96, buffer 192 of 1920";
        route.inputStreams = "AAudio (MMAP), 44100 Hz, Exclusive, burst 96, "
            "buffer 11424 of 11520, preset VoicePerformance";
        return route;
    }

    // The Playback menu's line about the latency, as it reads when the
    // menu is opened
    QString latencyLine() {
        emit m_window->playbackMenu()->aboutToShow();
        return m_window->latencyLineAction()->text();
    }

    // Playback > Calibrate Audio chosen, and the dialog it shows, Start
    // pressed on it with the short plan
    CalibrateAudioDialog *startCheckFromMenu() {
        m_window->calibrateAudioAction()->trigger();
        CalibrateAudioDialog *dialog = m_window->calibrateAudioDialog();
        if (!dialog) return nullptr;
        dialog->setPlan(shortPlan());
        m_window->discardModifications();
        dialog->startCheck();
        return dialog;
    }

    // A run judged at the reference's rate, by the fake device's
    // figures, to be given a verdict
    static AudioCheckResult judgedResult(LatencyCheck::Verdict verdict) {
        AudioCheckResult r;
        r.recordingRate = rate;
        r.referenceRate = rate;
        r.reportedOutputLatency = reportedOut / rate;
        r.reportedInputLatency = reportedIn / rate;
        r.usedRoundTrip = (reportedOut + reportedIn) / rate;
        r.takes.resize(4);
        r.summary.verdict = verdict;
        if (verdict != LatencyCheck::Verdict::Ok) {
            r.summary.flags.push_back(verdict);
        }
        r.summary.judged = 12;
        r.summary.found = 12;
        r.summary.punchIns.resize(4);
        for (LatencyCheck::PunchInResult &p : r.summary.punchIns) {
            p.judged = 3;
            p.found = 3;
            p.medianOffset = 0.003;
            p.spread = 0.001;
        }
        r.summary.medianOffset = 0.003;
        r.summary.spread = 0.001;
        r.summary.inputPeak = 0.25;
        r.calibratedRoundTrip = r.usedRoundTrip + 0.003;
        return r;
    }

    // The three toggles of the take path, and the settings they and the
    // pre-roll's length are kept in
    QStringList toggles() {
        QSettings settings;
        settings.beginGroup("MainWindow");
        QStringList state;
        state << QString("play reference %1, setting %2")
            .arg(m_window->playReferenceWhileRecordingAction()->isChecked())
            .arg(settings.value("playrefwhilerecording").toString());
        state << QString("pre-roll %1, setting %2, %3 s")
            .arg(m_window->preRollAction()->isChecked())
            .arg(settings.value("preroll").toString())
            .arg(settings.value("prerollseconds").toString());
        state << QString("record into selection %1, setting %2")
            .arg(m_window->recordIntoSelectionAction()->isChecked())
            .arg(settings.value("recordintoselection").toString());
        settings.endGroup();
        return state;
    }

    static QByteArray describe(const AudioCheckResult &r) {
        const LatencyCheck::TakeSummary &s = r.summary;
        QStringList flags;
        for (LatencyCheck::Verdict v : s.flags) {
            flags << LatencyCheck::verdictName(v);
        }
        return QString("%1 %2 [%3], found %4 of %5, median offset %6 "
                       "frames, spread %7 ms, input peak %8; %9 takes, "
                       "round trip used %10 frames, measured %11 frames; "
                       "recorded at %12 Hz, reference at %13 Hz")
            .arg(r.failure == "" ? "judged:" : "failed: " + r.failure)
            .arg(LatencyCheck::verdictName(s.verdict))
            .arg(flags.join(" "))
            .arg(s.found)
            .arg(s.judged)
            .arg(s.medianOffset * rate)
            .arg(s.spread * 1000.0)
            .arg(s.inputPeak)
            .arg(r.takes.size())
            .arg(r.usedRoundTrip * rate)
            .arg(r.calibratedRoundTrip * rate)
            .arg(r.recordingRate)
            .arg(r.referenceRate)
            .toUtf8();
    }

    // What differs from the check's playback, or "": the reference
    // audible, centred, and brought back from full scale to the level it
    // was made at; its pitch and notes silent
    QString checkPlaybackProblem() {
        const float gain =
            float(std::pow(10.0, LatencyCheck::kPeakDbfs / 20.0));
        QStringList problems;
        auto reference = sv::PlayParameterRepository::getInstance()
            ->getPlayParameters(m_window->mainModelId().untyped);
        if (!reference) {
            problems << "the reference has no play parameters";
        } else {
            if (!reference->isPlayAudible()) {
                problems << "the reference is muted";
            }
            if (reference->getPlayPan() != 0.f) {
                problems << QString("the reference is panned %1")
                    .arg(reference->getPlayPan());
            }
            if (std::fabs(reference->getPlayGain() - gain) > 1e-6f) {
                problems << QString("the reference plays at %1 dB")
                    .arg(20.0 * std::log10(reference->getPlayGain()));
            }
        }
        for (Analyser::Component c : { Analyser::PitchTrack,
                                       Analyser::Notes }) {
            sv::Layer *layer = m_window->analyser()->getLayer(c);
            auto params = layer ? layer->getPlayParameters() : nullptr;
            if (!params) {
                problems << QString("no layer %1").arg(int(c));
            } else if (params->isPlayAudible()) {
                problems << QString("layer %1 is heard").arg(int(c));
            }
        }
        return problems.join(", ");
    }

    // The settings the analysers' show and play toggles are kept in, as
    // Analyser::loadState() reads them
    static QStringList analyserSettings() {
        QSettings settings;
        settings.beginGroup("Analyser");
        QStringList state;
        for (int c = Analyser::Audio; c <= Analyser::Spectrogram; ++c) {
            state << QString("component %1 visible %2 audible %3").arg(c)
                .arg(settings.value(QString("visible-%1").arg(c),
                                    c != Analyser::Spectrogram).toBool())
                .arg(settings.value(QString("audible-%1").arg(c), true)
                     .toBool());
        }
        settings.endGroup();
        return state;
    }

    // The reference muted in the user's own sessions. Its spectrogram's
    // setting as well: both are read for the reference's model, the
    // spectrogram's last
    static void muteReferenceInSettings() {
        QSettings settings;
        settings.beginGroup("Analyser");
        settings.setValue(QString("audible-%1").arg(Analyser::Audio), false);
        settings.setValue(QString("audible-%1").arg(Analyser::Spectrogram),
                          false);
        settings.endGroup();
    }

    // How the session open now plays its reference, and its pitch and
    // notes. Not the gain of those two: the toolbar moves it to a notch
    // of its own level control in the first session of a window only
    QStringList sessionPlayback() {
        QStringList state;
        auto reference = sv::PlayParameterRepository::getInstance()
            ->getPlayParameters(m_window->mainModelId().untyped);
        if (reference) {
            state << QString("reference audible %1 pan %2 gain %3")
                .arg(reference->isPlayAudible())
                .arg(reference->getPlayPan())
                .arg(reference->getPlayGain());
        } else {
            state << "no reference";
        }
        for (Analyser::Component c : { Analyser::PitchTrack,
                                       Analyser::Notes }) {
            sv::Layer *layer = m_window->analyser()->getLayer(c);
            auto params = layer ? layer->getPlayParameters() : nullptr;
            if (params) {
                state << QString("layer %1 audible %2 pan %3").arg(int(c))
                    .arg(params->isPlayAudible())
                    .arg(params->getPlayPan());
            } else {
                state << QString("no layer %1").arg(int(c));
            }
        }
        return state;
    }

    // An ordinary file, opened as the user opens one, and analysed
    void openSong() {
        const QString path = m_dir.filePath("song.wav");
        if (!QFileInfo::exists(path)) {
            const std::vector<float> samples = TestSignals::sawtooth
                (220.5, rate, int(1.5 * rate), 0.5);
            sv::WavFileWriter writer(path, rate, 1,
                                     sv::WavFileWriter::WriteToTarget);
            const float *data = samples.data();
            QVERIFY(writer.isOK());
            QVERIFY(writer.writeSamples(&data,
                                        sv::sv_frame_t(samples.size())));
            QVERIFY(writer.close());
        }
        m_window->discardModifications();
        QCOMPARE(m_window->openPath(path, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        Analyser *analyser = m_window->analyser();
        QTRY_VERIFY_WITH_TIMEOUT
            (analyser->getLayer(Analyser::PitchTrack) &&
             analyser->getLayer(Analyser::Notes) &&
             analyser->getInitialAnalysisCompletion() >= 100 &&
             !sv::ModelTransformerFactory::getInstance()
             ->haveRunningTransformers(), 30000);
    }

    static QString stepName(AudioCheckRunner::Step step) {
        switch (step) {
        case AudioCheckRunner::Step::Idle: return "idle";
        case AudioCheckRunner::Step::OpeningReference: return "opening";
        case AudioCheckRunner::Step::AnalysingReference:
            return "analysing the reference";
        case AudioCheckRunner::Step::Recording: return "recording";
        case AudioCheckRunner::Step::AnalysingTake: return "analysing the take";
        }
        return "";
    }

    // Not a slot: QtTest would run it as a test. As TestRecordWorkflow's
    void dismissDialog() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) return;
        QString description = modal->windowTitle();
        if (auto box = qobject_cast<QMessageBox *>(modal)) {
            description += ": " + box->text();
            m_dialogs.push_back(description);
            QList<QAbstractButton *> buttons = box->buttons();
            if (!buttons.isEmpty()) {
                buttons.last()->click();
                return;
            }
        } else {
            m_dialogs.push_back(description);
        }
        if (auto dialog = qobject_cast<QDialog *>(modal)) {
            dialog->reject();
        } else {
            modal->close();
        }
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());

        QSettings().clear();

        // Otherwise the MainWindow constructor asks, in a dialog
        QSettings settings;
        settings.beginGroup("Preferences");
        settings.setValue(QString("network-permission-%1").arg(TONY_VERSION),
                          false);
        settings.endGroup();

        sv::InteractiveFileFinder::getInstance()
            ->setApplicationSessionExtension("ton");

        // Recordings and takes go here and not among the user's
        sv::RecordDirectory::setRecordContainerDirectory
            (m_dir.filePath("recorded"));

        connect(&m_watchdog, &QTimer::timeout,
                this, [this]() { dismissDialog(); });
        m_watchdog.start(50);
    }

    void init() {
        m_dialogs.clear();
        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.setValue("playrefwhilerecording", false);
        settings.setValue("preroll", false);
        settings.setValue("recordintoselection", false);
        settings.remove("prerollseconds");
        settings.endGroup();
        settings.beginGroup("Analyser");
        settings.remove("");
        settings.endGroup();
        settings.beginGroup("Preferences");
        settings.remove("audio-playback-device");
        settings.remove("audio-record-device");
        settings.endGroup();
        SingingTakes::setOverwriteConfirmationWanted(true);
    }

    void cleanup() {
        // A round trip a test stored would place the next test's takes.
        // First, as the waits below return early when they fail
        QSettings().remove("LatencyCalibration");

        if (m_window) {
            if (m_window->recordTarget()->isRecording()) {
                m_window->doRecord();
            }
            QTRY_VERIFY_WITH_TIMEOUT
                (!sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(), 30000);
            m_window->doCloseSession();
            delete m_window;
            m_window = nullptr;
        }
        QVERIFY2(m_dialogs.isEmpty(),
                 qPrintable("unexpected dialog: " + m_dialogs.join(" | ")));
    }

    void cleanupTestCase() {
        m_watchdog.stop();
        sv::RecordDirectory::setRecordContainerDirectory("");
    }

    // The device reports 2 x 4096 frames out and 4096 in, and the round
    // trip is 123 frames longer. Every take lands 123 frames late, the
    // check finds them there, and the round trip it works out is the
    // real one. Its takes play the reference, record into their ranges
    // and have its own lead-in, and the user's three toggles, set
    // otherwise, are as they were, and so are their settings
    void check_measures_the_round_trip() {
        makeWindow(loopback());
        {
            QSettings settings;
            settings.setValue("MainWindow/prerollseconds", 3.0);
        }
        m_window->setPlayReferenceWhileRecording(false);
        m_window->setPreRoll(true);
        m_window->setRecordIntoSelection(false);
        const QStringList before = toggles();

        runCheck();
        if (QTest::currentTestFailed()) return;

        const AudioCheckResult &r = m_result;
        QVERIFY2(r.failure == "", describe(r).constData());
        QVERIFY2(r.summary.verdict == LatencyCheck::Verdict::Ok,
                 describe(r).constData());
        QCOMPARE(r.summary.judged, 4);
        QCOMPARE(r.summary.found, 4);
        QVERIFY2(std::fabs(r.summary.medianOffset * rate -
                           (roundTrip - reportedOut - reportedIn)) <= 4.0,
                 describe(r).constData());

        QCOMPARE(int(r.takes.size()), 2);
        for (const TakeLatency &t : r.takes) {
            QCOMPARE(t.roundTrip, sv::sv_frame_t(reportedOut + reportedIn));
            QCOMPARE(t.reportedOutput, reportedOut / rate);
            QCOMPARE(t.reportedInput, reportedIn / rate);
            QCOMPARE(t.recordingRate, rate);
        }
        QCOMPARE(r.usedRoundTrip, (reportedOut + reportedIn) / rate);
        QCOMPARE(r.reportedOutputLatency, reportedOut / rate);
        QCOMPARE(r.reportedInputLatency, reportedIn / rate);
        QCOMPARE(r.recordingRate, rate);
        QCOMPARE(r.referenceRate, rate);
        QVERIFY(r.calibrationUsable());
        QVERIFY2(std::fabs(r.calibratedRoundTrip * rate - roundTrip) <= 4.0,
                 describe(r).constData());

        // The second punch-in starts 6.3 s in, with room for all of the
        // check's lead-in
        QCOMPARE(m_window->takePreRoll(),
                 sv::sv_frame_t(AudioCheckRunner::kPreRollSeconds * rate));

        QCOMPARE(toggles(), before);
        QVERIFY(!m_window->audioCheckTakes());
    }

    // The round trip the check measured, kept: a second check places its
    // takes with it, and finds them where they belong. Forgotten, the
    // reported pair is in use again
    void check_stored_round_trip_is_used() {
        makeWindow(loopback());

        runCheck();
        if (QTest::currentTestFailed()) return;
        QVERIFY2(m_result.calibrationUsable(), describe(m_result).constData());
        const double measured = m_result.calibratedRoundTrip;

        QVERIFY(m_window->storeMeasuredLatency(m_result));
        LatencyCalibration::InUse inUse = m_window->latencyInUse();
        QVERIFY(inUse.source == LatencyCalibration::Source::Measured);
        QCOMPARE(inUse.roundTrip, measured);
        QVERIFY(inUse.date.isValid());

        m_result = AudioCheckResult();
        m_finished = 0;
        runCheck();
        if (QTest::currentTestFailed()) return;

        const AudioCheckResult &r = m_result;
        QVERIFY2(r.summary.verdict == LatencyCheck::Verdict::Ok,
                 describe(r).constData());
        QCOMPARE(r.summary.found, 4);
        QVERIFY2(std::fabs(r.summary.medianOffset * rate) <= 4.0,
                 describe(r).constData());
        QCOMPARE(int(r.takes.size()), 2);
        for (const TakeLatency &t : r.takes) {
            QVERIFY(t.measured);
            QCOMPARE(t.roundTrip, sv::sv_frame_t(std::llround(measured * rate)));
        }
        QVERIFY2(std::fabs(r.calibratedRoundTrip * rate - roundTrip) <= 4.0,
                 describe(r).constData());

        m_window->forgetMeasuredLatency();
        inUse = m_window->latencyInUse();
        QVERIFY(inUse.source == LatencyCalibration::Source::Reported);
        QVERIFY(!inUse.stale);
        QCOMPARE(inUse.roundTrip, (reportedOut + reportedIn) / rate);
    }

    // A device at 48 kHz against the reference at 44.1: the takes are
    // recorded at the device's rate and converted as they are spliced, so
    // the check is judged, and its figure kept, like any other. The fake's
    // delay and the latencies it reports count its own frames: the round
    // trip it really has is roundTrip frames at 48 kHz, worked out here in
    // seconds. The figure is kept under the device's rate, not the
    // reference's, and the Playback menu's line then shows it
    void check_measures_the_round_trip_at_48000() {
        FakeAudioIO::Config config = loopback();
        config.sampleRate = int(otherRate);
        makeWindow(config);

        runCheck();
        if (QTest::currentTestFailed()) return;

        const AudioCheckResult &r = m_result;
        QVERIFY2(r.failure == "", describe(r).constData());
        QVERIFY2(r.summary.verdict == LatencyCheck::Verdict::Ok,
                 describe(r).constData());
        QCOMPARE(r.summary.judged, 4);
        QCOMPARE(r.summary.found, 4);
        QCOMPARE(r.recordingRate, otherRate);
        QCOMPARE(r.referenceRate, rate);
        QCOMPARE(r.key.rate, otherRate);

        // Placed with the reported pair, in frames of the recording
        QCOMPARE(int(r.takes.size()), 2);
        for (const TakeLatency &t : r.takes) {
            QCOMPARE(t.recordingRate, otherRate);
            QCOMPARE(t.roundTrip, sv::sv_frame_t(reportedOut + reportedIn));
        }
        QCOMPARE(r.usedRoundTrip, (reportedOut + reportedIn) / otherRate);
        QCOMPARE(r.reportedInputLatency, reportedIn / otherRate);
        // The play source has it at the reference's rate, to a frame
        QVERIFY(std::fabs(r.reportedOutputLatency - reportedOut / otherRate)
                < 1.0 / rate);

        // bqaudioio's ResamplerWrapper, which brings the reference to the
        // device's rate, holds it back by about a millisecond that nothing
        // reports (TestRecordWorkflow's latency_with_a_device_at_48000):
        // part of the round trip the takes need, as on a real device at
        // another rate, and never early. Counted at the wrong rate, the
        // figure would be 8 % off, some 20 ms
        QVERIFY(r.calibrationUsable());
        const double late = r.calibratedRoundTrip - roundTrip / otherRate;
        QVERIFY2(late >= -0.0001 && late <= 0.0015,
                 qPrintable(QString("measured %1 ms, the fake's delay is "
                                    "%2 ms: %3")
                            .arg(r.calibratedRoundTrip * 1000.0)
                            .arg(roundTrip / otherRate * 1000.0)
                            .arg(describe(r).constData())));
        QVERIFY(!m_window->audioCheckTakes());

        QVERIFY(m_window->storeMeasuredLatency(r));
        LatencyCalibration::Key at48 = key("", "");
        at48.rate = otherRate;
        LatencyCalibration::Figure figure;
        {
            QSettings settings;
            QVERIFY(LatencyCalibration::load(settings, at48, figure));
            QCOMPARE(figure.roundTrip, r.calibratedRoundTrip);
            QVERIFY(!LatencyCalibration::load(settings, key("", ""), figure));
        }
        const LatencyCalibration::InUse inUse = m_window->latencyInUse();
        QVERIFY(inUse.source == LatencyCalibration::Source::Measured);
        QCOMPARE(inUse.roundTrip, r.calibratedRoundTrip);
        const QString line = latencyLine();
        QVERIFY2(line.startsWith("Latency: measured"), qPrintable(line));
    }

    // The round trip measured at 48 kHz, kept: a second check places its
    // takes with it, turned into frames at the device's rate, and finds
    // every one where it belongs. The first check is one punch-in, which
    // is enough to measure with and keeps this short
    void check_stored_round_trip_is_used_at_48000() {
        FakeAudioIO::Config config = loopback();
        config.sampleRate = int(otherRate);
        makeWindow(config);

        runCheck(onePunchIn());
        if (QTest::currentTestFailed()) return;
        QVERIFY2(m_result.calibrationUsable(), describe(m_result).constData());
        QVERIFY(m_window->storeMeasuredLatency(m_result));
        const double measured = m_result.calibratedRoundTrip;

        runCheck();
        if (QTest::currentTestFailed()) return;

        const AudioCheckResult &r = m_result;
        QVERIFY2(r.failure == "", describe(r).constData());
        QVERIFY2(r.summary.verdict == LatencyCheck::Verdict::Ok,
                 describe(r).constData());
        QCOMPARE(r.summary.found, 4);

        // Near 0: the wrapper's hold-back moves by a frame or two from one
        // stream start to the next, so not to 4 frames as at 44.1 kHz. A
        // figure turned into frames at the wrong rate lands some 20 ms off,
        // and one without the hold-back 1 ms
        const double allowed = 0.0002;
        QCOMPARE(int(r.summary.punchIns.size()), 2);
        for (const LatencyCheck::PunchInResult &p : r.summary.punchIns) {
            QVERIFY2(std::fabs(p.medianOffset) <= allowed,
                     describe(r).constData());
        }
        QCOMPARE(int(r.takes.size()), 2);
        for (const TakeLatency &t : r.takes) {
            QVERIFY(t.measured);
            QCOMPARE(t.roundTrip,
                     sv::sv_frame_t(std::llround(measured * otherRate)));
        }
        QVERIFY2(std::fabs(r.calibratedRoundTrip - measured) <= allowed,
                 describe(r).constData());
    }

    // A device whose reported latencies move from one start to the next,
    // as Oboe's do on a phone: the second punch-in is placed with 10 ms
    // more than the first, and lands 10 ms earlier, while the path's
    // round trip has not moved. The check measures that round trip, and
    // finds it steady
    void check_measures_the_round_trip_when_reports_move() {
        FakeAudioIO::Config config = loopback();
        config.recordLatencyStep = 441;
        makeWindow(config);

        runCheck();
        if (QTest::currentTestFailed()) return;

        const AudioCheckResult &r = m_result;
        QVERIFY2(r.failure == "", describe(r).constData());
        QCOMPARE(int(r.takes.size()), 2);
        QVERIFY2(r.takes[1].roundTrip - r.takes[0].roundTrip >= 441,
                 qPrintable(QString("placed with %1 and %2 frames")
                            .arg(r.takes[0].roundTrip)
                            .arg(r.takes[1].roundTrip)));
        QCOMPARE(int(r.summary.punchIns.size()), 2);
        QVERIFY2(r.summary.punchIns[0].medianOffset -
                 r.summary.punchIns[1].medianOffset > 0.009,
                 describe(r).constData());

        QVERIFY2(r.summary.verdict == LatencyCheck::Verdict::Ok,
                 describe(r).constData());
        QVERIFY(r.calibrationUsable());
        QVERIFY2(std::fabs(r.calibratedRoundTrip * rate - roundTrip) <= 4.0,
                 describe(r).constData());
    }

    // A phone: the device reports the route it opened. The figure is
    // kept for that route, whatever the Preferences name, with how its
    // streams were opened; its takes are placed with it although the
    // latencies the device reports move by more than a millisecond from
    // take to take. Another route, a Bluetooth headset, has a figure of
    // its own or none, and the menu's line follows the device as it is
    // opened again for each; the same route opened otherwise has its
    // figure out of date
    void check_keeps_the_figure_for_the_route() {
        setDevices("Speakers A", "Microphone A");
        FakeAudioIO::Config config = loopback();
        config.route = phoneRoute();
        config.recordLatencyStep = 441;
        makeWindow(config);

        runCheck(onePunchIn());
        if (QTest::currentTestFailed()) return;
        QVERIFY2(m_result.calibrationUsable(), describe(m_result).constData());

        const LatencyCalibration::Key speaker =
            LatencyCalibration::routeKey(phoneRoute(), rate);
        QCOMPARE(m_result.key.implementation, QString("oboe"));
        QCOMPARE(m_result.key.playbackDevice,
                 QString("Built-in speaker (Pixel 7)"));
        QCOMPARE(m_result.key.recordDevice,
                 QString("Built-in microphone (Pixel 7)"));
        QCOMPARE(m_result.key.rate, rate);
        QCOMPARE(m_result.route.outputStreams, phoneRoute().outputStreams);
        QCOMPARE(m_result.route.inputStreams, phoneRoute().inputStreams);

        QVERIFY(m_window->storeMeasuredLatency(m_result));
        {
            QSettings settings;
            LatencyCalibration::Figure figure;
            QVERIFY(!LatencyCalibration::load
                    (settings, key("Speakers A", "Microphone A"), figure));
            QVERIFY(LatencyCalibration::load(settings, speaker, figure));
            QCOMPARE(figure.roundTrip, m_result.calibratedRoundTrip);
            QCOMPARE(figure.outputStreams, phoneRoute().outputStreams);
            QCOMPARE(figure.inputStreams, phoneRoute().inputStreams);
        }
        QString line = latencyLine();
        QVERIFY2(line.startsWith("Latency: measured"), qPrintable(line));

        // The next check's takes are placed with it, the reported input
        // latency having moved by 10 ms since
        const int reportedBefore =
            int(m_window->recordTarget()->getSystemRecordLatency());
        runCheck(onePunchIn());
        if (QTest::currentTestFailed()) return;
        QVERIFY2(m_window->recordTarget()->getSystemRecordLatency() -
                 reportedBefore >= 441,
                 qPrintable(QString("reported %1 frames, then %2")
                            .arg(reportedBefore)
                            .arg(m_window->recordTarget()
                                 ->getSystemRecordLatency())));
        QCOMPARE(int(m_result.takes.size()), 1);
        QVERIFY(m_result.takes[0].measured);
        QVERIFY2(std::fabs(m_result.summary.medianOffset * rate) <= 4.0,
                 describe(m_result).constData());

        // The headset: nothing kept for it
        m_window->setFakeRoute(phoneRoute(true));
        m_window->doRecreateAudioIO();
        QCOMPARE(m_window->audioRoute().output.productName,
                 QString("Headset X"));
        line = latencyLine();
        QVERIFY2(line.startsWith("Latency: driver's figure"), qPrintable(line));
        QVERIFY2(!line.contains("out of date"), qPrintable(line));
        QVERIFY(!m_window->forgetLatencyAction()->isEnabled());

        // The speaker again
        m_window->setFakeRoute(phoneRoute());
        m_window->doRecreateAudioIO();
        line = latencyLine();
        QVERIFY2(line.startsWith("Latency: measured"), qPrintable(line));
        QVERIFY(m_window->forgetLatencyAction()->isEnabled());

        // Opened for playback only, as a phone's device is until its first
        // take: the input calibrated with the speaker is taken for it
        AudioRoute::Route playbackOnly = phoneRoute();
        playbackOnly.hasInput = false;
        playbackOnly.input = AudioRoute::Device();
        playbackOnly.inputStreams = "";
        m_window->setFakeRoute(playbackOnly);
        m_window->doRecreateAudioIO();
        QCOMPARE(m_window->latencyKey(rate).recordDevice,
                 QString("Built-in microphone (Pixel 7)"));
        line = latencyLine();
        QVERIFY2(line.startsWith("Latency: measured"), qPrintable(line));

        // The speaker, shared rather than exclusive: out of date
        AudioRoute::Route shared = phoneRoute();
        shared.outputStreams = "AAudio, 44100 Hz, Shared, burst 96";
        m_window->setFakeRoute(shared);
        m_window->doRecreateAudioIO();
        line = latencyLine();
        QVERIFY2(line.contains("(the measured one is out of date)"),
                 qPrintable(line));
        QVERIFY(m_window->forgetLatencyAction()->isEnabled());

        // Forgotten for the route it is kept for
        m_window->forgetLatencyAction()->trigger();
        QSettings settings;
        LatencyCalibration::Figure figure;
        QVERIFY(!LatencyCalibration::load(settings, speaker, figure));
    }

    // Cancel during a take stops it through the Stop path, clears the
    // override, ends the run once and starts nothing more
    void check_cancelled_during_a_take() {
        makeWindow(loopback());
        const QStringList before = toggles();

        startCheck();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        QVERIFY(m_window->audioCheckTakes());
        QTest::qWait(1000);

        m_window->audioCheck()->cancel();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(!m_window->recordingAsSingingTrack());
        QVERIFY(!m_window->audioCheckTakes());
        QVERIFY(!m_window->audioCheck()->isRunning());
        QCOMPARE(m_finished, 1);
        QVERIFY(m_result.failure != "");

        QTest::qWait(500);
        QVERIFY(!m_window->recordTarget()->isRecording());
        QCOMPARE(m_finished, 1);
        QCOMPARE(toggles(), before);
    }

    // No device to record from: the take does not start, and the run
    // ends there and says why
    void check_fails_without_a_device() {
        makeWindow(loopback(), false);

        runCheck();
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_result.failure != "", describe(m_result).constData());
        QVERIFY(m_result.takes.empty());
        QVERIFY(!m_result.calibrationUsable());
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(!m_window->recordingAsSingingTrack());
        QVERIFY(!m_window->audioCheckTakes());
    }

    // A device that opens but never calls back: not one frame comes in.
    // Once the take should have been over, and not before, so that a
    // device slow to start is not taken for one that delivers nothing,
    // the run stops the take through the Stop path and ends saying that
    // the device delivered no input, not that the take did not stop. No
    // take is left, and no harm: the next file opened is analysed
    void check_ends_when_the_device_delivers_nothing() {
        FakeAudioIO::Config config = loopback();
        config.neverCallsBack = true;
        makeWindow(config);

        // How long after the take began to record the run ended; the
        // connections go with the guard when the test returns
        QObject guard;
        QElapsedTimer recording;
        qint64 endedAfterMs = -1;
        connect(m_window->audioCheck(), &AudioCheckRunner::progress, &guard,
                [&recording](const AudioCheckRunner::Progress &p) {
                    if (p.step == AudioCheckRunner::Step::Recording &&
                        !recording.isValid()) {
                        recording.start();
                    }
                });
        connect(m_window->audioCheck(), &AudioCheckRunner::finished, &guard,
                [&](const AudioCheckResult &) {
                    if (recording.isValid()) {
                        endedAfterMs = recording.elapsed();
                    }
                });

        runCheck(onePunchIn());
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_result.failure.contains("delivered no input"),
                 describe(m_result).constData());
        const qint64 takeMs =
            qint64((AudioCheckRunner::kPreRollSeconds + 2.0) * 1000.0);
        QVERIFY2(endedAfterMs >= takeMs + AudioCheckRunner::kNoInputTimeoutMs -
                 AudioCheckRunner::kPollMs,
                 qPrintable(QString("ended %1 ms into a take of %2 ms")
                            .arg(endedAfterMs).arg(takeMs)));

        QVERIFY(!m_result.calibrationUsable());
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(!m_window->recordingInProgress());
        QVERIFY(!m_window->recordingAsSingingTrack());
        QVERIFY(!m_window->audioCheckTakes());
        QVERIFY2(!m_window->takes()->haveTake(),
                 "a recording of nothing was kept as a take");

        openSong();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_finished, 1);
    }

    // With automatic analysis switched off the reference is never
    // analysed, and the check does not wait for it: it needs none
    void check_runs_without_automatic_analysis() {
        {
            QSettings settings;
            settings.setValue("Analyser/auto-analysis", false);
        }
        makeWindow(loopback());

        startCheck();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 10000);
        QVERIFY(!m_window->analyser()->getLayer(Analyser::PitchTrack));

        m_window->audioCheck()->cancel();
        QCOMPARE(m_finished, 1);
        QVERIFY(!m_window->recordTarget()->isRecording());
    }

    // Closing the session during a take ends the run the same way
    void check_ends_when_the_session_closes() {
        makeWindow(loopback());

        startCheck();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        QTest::qWait(1000);

        m_window->doCloseSession();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(!m_window->audioCheckTakes());
        QVERIFY(!m_window->audioCheck()->isRunning());
        QCOMPARE(m_finished, 1);
        QVERIFY(m_result.failure != "");

        QTest::qWait(500);
        QVERIFY(!m_window->recordTarget()->isRecording());
        QCOMPARE(m_finished, 1);
    }

    // The check's session plays the reference centred, at the level it
    // was made at, and not its pitch and notes: from the first take to
    // the end of the run, and after it. The user has muted the reference
    // in their own sessions, which the check does not follow; its sweeps
    // reach the speakers at -12 dBFS, in both channels, and come back at
    // the level they were made at. Its progress names each step and
    // punch-in in order, with the recording still to come going down
    void check_plays_the_reference_centred_and_quiet() {
        muteReferenceInSettings();
        const QStringList settingsBefore = analyserSettings();
        makeWindow(loopback());

        QStringList problems;
        int looks = 0;
        bool recorded = false;
        QTimer sampler;
        connect(&sampler, &QTimer::timeout, &sampler, [&]() {
            if (!m_window->audioCheck()->isRunning()) return;
            if (m_window->recordTarget()->isRecording()) recorded = true;
            if (!recorded) return;
            ++looks;
            const QString problem = checkPlaybackProblem();
            if (problem != "" && !problems.contains(problem)) {
                problems << problem;
            }
        });
        sampler.start(20);
        runCheck();
        sampler.stop();
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_result.failure == "", describe(m_result).constData());

        // What reached the speakers, both channels mixed: the reference
        // centred has the same level there as in each channel
        const std::vector<float> played =
            m_window->fake()->getCapturedOutput();
        float peak = 0.f;
        for (float s : played) peak = std::max(peak, std::fabs(s));
        const double peakDb = 20.0 * std::log10(peak);
        QVERIFY2(std::fabs(peakDb - LatencyCheck::kPeakDbfs) <= 0.5,
                 qPrintable(QString("played at %1 dBFS").arg(peakDb)));

        // and each sweep, as the finder heard it come back: 0 dB is the
        // level it was made at
        QVERIFY2(m_result.summary.found == 4, describe(m_result).constData());
        for (const LatencyCheck::EventResult &e : m_result.summary.events) {
            if (!e.arrival.found) continue;
            QVERIFY2(std::fabs(e.arrival.levelDb) <= 1.0,
                     qPrintable(QString("the sweep at %1 s came back at "
                                        "%2 dB")
                                .arg(e.expectedSeconds)
                                .arg(e.arrival.levelDb)));
        }

        // The playback, looked at every 20 ms from the first take on:
        // neither the analyses nor the next take put anything back
        QVERIFY2(looks >= 200,
                 qPrintable(QString("looked %1 times").arg(looks)));
        QVERIFY2(problems.isEmpty(),
                 qPrintable("during the check: " + problems.join(" | ")));
        const QString after = checkPlaybackProblem();
        QVERIFY2(after == "", qPrintable("after the check: " + after));
        QCOMPARE(analyserSettings(), settingsBefore);

        // Each step once as it begins, and more often while a take is
        // recorded, as the seconds of recording to come go down
        QVERIFY(!m_progress.empty());
        const AudioCheckRunner::Plan plan = shortPlan();
        double planned = 0.0;
        for (const LatencyCheck::PunchIn &p : LatencyCheck::punchInsFor
                 (plan.layout, plan.punchIns, plan.eventsEach)) {
            planned += std::min(AudioCheckRunner::kPreRollSeconds, p.start) +
                (p.end - p.start);
        }
        QVERIFY2(std::fabs(m_progress.front().secondsLeft - planned) < 0.01,
                 qPrintable(QString("%1 s to record at first, not %2 s")
                            .arg(m_progress.front().secondsLeft)
                            .arg(planned)));
        QStringList steps;
        int reportsWhileRecording[3] = { 0, 0, 0 };
        double left = m_progress.front().secondsLeft;
        for (const AudioCheckRunner::Progress &p : m_progress) {
            QCOMPARE(p.punchIns, 2);
            QVERIFY(p.punchIn >= 0 && p.punchIn <= 2);
            QVERIFY2(p.secondsLeft <= left + 0.001,
                     qPrintable(QString("%1 s left after %2 s")
                                .arg(p.secondsLeft).arg(left)));
            left = p.secondsLeft;
            const QString step =
                QString("%1 %2").arg(stepName(p.step)).arg(p.punchIn);
            if (steps.isEmpty() || steps.back() != step) steps << step;
            if (p.step == AudioCheckRunner::Step::Recording) {
                ++reportsWhileRecording[p.punchIn];
            }
        }
        QCOMPARE(steps, QStringList()
                 << "opening 0" << "analysing the reference 0"
                 << "recording 1" << "analysing the take 1"
                 << "recording 2" << "analysing the take 2");
        QVERIFY2(reportsWhileRecording[1] >= 3 && reportsWhileRecording[2] >= 3,
                 qPrintable(QString("%1 and %2 reports while recording")
                            .arg(reportsWhileRecording[1])
                            .arg(reportsWhileRecording[2])));
        QCOMPARE(m_progress.back().secondsLeft, 0.0);
    }

    // A session opened after a check plays as one opened before it did,
    // and the settings every session reads are as they were: the
    // check's playback was its session's alone. The user has the
    // reference muted and the sonification on, both the other way round
    // from the check's
    void check_leaves_the_next_session_alone() {
        muteReferenceInSettings();
        makeWindow(loopback());
        openSong();
        if (QTest::currentTestFailed()) return;
        const QStringList before = sessionPlayback();
        const QStringList settingsBefore = analyserSettings();
        QVERIFY2(checkPlaybackProblem() != "", qPrintable(before.join(", ")));

        startCheck();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        QTest::qWait(500);
        QCOMPARE(checkPlaybackProblem(), QString());
        m_window->audioCheck()->cancel();
        QCOMPARE(m_finished, 1);
        QTRY_VERIFY_WITH_TIMEOUT
            (!sv::ModelTransformerFactory::getInstance()
             ->haveRunningTransformers(), 30000);
        QCOMPARE(analyserSettings(), settingsBefore);

        openSong();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(sessionPlayback(), before);
        QCOMPARE(analyserSettings(), settingsBefore);
    }

    // Each check writes its reference to a file of its own, never over
    // the one the open session plays, which may be the check before's;
    // the others are removed, and the lowest free number is taken
    void check_reference_gets_a_file_of_its_own() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto touch = [&](QString name) {
            QFile file(dir.filePath(name));
            return file.open(QIODevice::WriteOnly);
        };
        auto files = [&]() {
            return QDir(dir.path()).entryList(QDir::Files, QDir::Name);
        };
        const QString one = dir.filePath("calibrate-audio-reference-1.wav");
        const QString two = dir.filePath("calibrate-audio-reference-2.wav");

        QCOMPARE(AudioCheckRunner::nextReferencePath(dir.path(), ""), one);
        QVERIFY(touch("calibrate-audio-reference-1.wav"));

        // Check Again, with the first check's session open
        QCOMPARE(AudioCheckRunner::nextReferencePath(dir.path(), one), two);
        QVERIFY(touch("calibrate-audio-reference-2.wav"));
        QVERIFY(touch("calibrate-audio-reference.wav"));
        QVERIFY(touch("song.wav"));

        // and again
        QCOMPARE(AudioCheckRunner::nextReferencePath(dir.path(), two), one);
        QCOMPARE(files(), QStringList()
                 << "calibrate-audio-reference-2.wav" << "song.wav");

        // A check with the user's song open
        QCOMPARE(AudioCheckRunner::nextReferencePath
                 (dir.path(), dir.filePath("song.wav")), one);
        QCOMPARE(files(), QStringList() << "song.wav");
    }

    // Punch-ins given in the plan must be recordable one after the other
    // within the layout: overlapping, out of order, empty or outside it,
    // the run does not start. Ranges that meet are fine. A plan that
    // keeps the session needs one
    void check_refuses_ranges_that_do_not_fit() {
        makeWindow(loopback());
        AudioCheckRunner::Plan plan = shortPlan();
        using P = LatencyCheck::PunchIn;
        const std::vector<std::vector<P>> refused = {
            { P(2.2, 4.2), P(4.0, 6.3) },
            { P(6.3, 8.3), P(2.2, 4.2) },
            { P(-0.1, 2.0) },
            { P(9.0, 11.0) },
            { P(3.0, 3.0) },
            { P(4.0, 3.0) },
        };
        for (const std::vector<P> &ranges : refused) {
            plan.ranges = ranges;
            QVERIFY(AudioCheckRunner::punchInsOf(plan).empty());
            QVERIFY(!m_window->audioCheck()->start(plan));
            QVERIFY(!m_window->audioCheck()->isRunning());
        }

        plan.ranges = { P(2.2, 4.2), P(4.2, 6.3), P(6.3, 10.8) };
        const std::vector<P> given = AudioCheckRunner::punchInsOf(plan);
        QCOMPARE(int(given.size()), 3);
        QCOMPARE(given[1].start, 4.2);
        QCOMPARE(given[2].end, 10.8);

        // Without ranges, as many as it asks for from the layout
        plan.ranges.clear();
        QCOMPARE(int(AudioCheckRunner::punchInsOf(plan).size()), 2);

        plan = onePunchIn();
        plan.keepSession = true;
        QVERIFY(!m_window->audioCheck()->start(plan));
        QVERIFY(!m_window->audioCheck()->isRunning());

        QTest::qWait(200);
        QCOMPARE(m_finished, 0);
    }

    // A round trip of the run's own: its takes are placed with it and
    // land where they belong, although another is kept for the devices
    // and the window places every other take with that. The one kept,
    // and the Playback menu's line about it, are as they were. The take
    // measured its own start gap, as the device says it was
    void check_uses_the_round_trip_it_is_given() {
        makeWindow(loopback());

        // With a file open, the device reports what it will during takes
        openSong();
        if (QTest::currentTestFailed()) return;
        const LatencyCalibration::InUse reported = m_window->latencyInUse();
        LatencyCalibration::Figure figure;
        figure.roundTrip = 0.3;
        figure.date = QDateTime::currentDateTimeUtc();
        figure.reportedOutput = reported.reportedOutput;
        figure.reportedInput = reported.reportedInput;
        {
            QSettings settings;
            LatencyCalibration::store(settings, key("", ""), figure);
        }
        const QString lineBefore = latencyLine();
        const QStringList storedBefore = storedLatency();
        QVERIFY2(lineBefore.startsWith("Latency: measured 300 ms"),
                 qPrintable(lineBefore));

        AudioCheckRunner::Plan plan = onePunchIn();
        plan.roundTrip = roundTrip / rate;
        runCheck(plan);
        if (QTest::currentTestFailed()) return;

        const AudioCheckResult &r = m_result;
        QVERIFY2(r.failure == "", describe(r).constData());
        QVERIFY2(r.summary.verdict == LatencyCheck::Verdict::Ok,
                 describe(r).constData());
        QCOMPARE(r.summary.judged, 1);
        QCOMPARE(r.summary.found, 1);
        QVERIFY2(std::fabs(r.summary.medianOffset * rate) <= 4.0,
                 describe(r).constData());
        QCOMPARE(int(r.takes.size()), 1);
        const TakeLatency &t = r.takes[0];
        QCOMPARE(t.roundTrip, sv::sv_frame_t(roundTrip));
        QVERIFY(t.measured);
        QCOMPARE(t.reportedOutput, reportedOut / rate);
        QCOMPARE(t.reportedInput, reportedIn / rate);
        QCOMPARE(r.usedRoundTrip, roundTrip / rate);

        QVERIFY(t.startGapMeasured);
        const long gap = m_window->fake()->getFramesBeforePlayStart();
        QVERIFY2(gap >= 0 && std::labs(long(t.startGap) - gap) <= 16,
                 qPrintable(QString("start gap %1 frames; the device says %2")
                            .arg(t.startGap).arg(gap)));

        QCOMPARE(latencyLine(), lineBefore);
        QCOMPARE(storedLatency(), storedBefore);
        const LatencyCalibration::InUse inUse = m_window->latencyInUse();
        QVERIFY(inUse.source == LatencyCalibration::Source::Measured);
        QCOMPARE(inUse.roundTrip, 0.3);
    }

    // A plan asks for a pre-roll of its own, the check's unless it says
    // otherwise: its take has that lead-in, and is placed right with it.
    // A negative one is refused
    void check_uses_the_pre_roll_it_is_given() {
        makeWindow(loopback());
        QCOMPARE(AudioCheckRunner::Plan().preRoll,
                 AudioCheckRunner::kPreRollSeconds);

        AudioCheckRunner::Plan refused = onePunchIn();
        refused.preRoll = -0.5;
        QVERIFY(!m_window->audioCheck()->start(refused));
        QVERIFY(!m_window->audioCheck()->isRunning());

        AudioCheckRunner::Plan plan = onePunchIn();
        plan.roundTrip = roundTrip / rate;
        plan.preRoll = 0.5;
        runCheck(plan);
        if (QTest::currentTestFailed()) return;

        const AudioCheckResult &r = m_result;
        QVERIFY2(r.failure == "", describe(r).constData());
        QCOMPARE(r.summary.found, 1);
        QVERIFY2(std::fabs(r.summary.medianOffset * rate) <= 4.0,
                 describe(r).constData());
        QCOMPARE(m_window->takePreRoll(), sv::sv_frame_t(0.5 * rate));
        QVERIFY(!m_window->audioCheckTakes());
    }

    // A run that keeps the session records into the one open, the
    // reference and take of the run before: nothing is written, opened
    // or asked, though the session is modified. The take keeps the
    // earlier punch-in, and only the run's own is judged
    void check_records_into_the_session_open() {
        makeWindow(loopback());

        AudioCheckRunner::Plan first = shortPlan();
        first.ranges = { LatencyCheck::PunchIn(2.2, 4.2) };
        runCheck(first);
        if (QTest::currentTestFailed()) return;
        QVERIFY2(m_result.summary.verdict == LatencyCheck::Verdict::Ok,
                 describe(m_result).constData());
        QCOMPARE(m_result.summary.judged, 1);
        const sv::ModelId reference = m_window->mainModelId();
        QVERIFY(m_window->isDocumentModified());

        AudioCheckRunner::Plan second = onePunchIn();
        second.keepSession = true;
        second.referencePath = m_dir.filePath("not-written.wav");
        runCheck(second, false);
        if (QTest::currentTestFailed()) return;

        const AudioCheckResult &r = m_result;
        QVERIFY2(r.failure == "", describe(r).constData());
        QVERIFY2(r.summary.verdict == LatencyCheck::Verdict::Ok,
                 describe(r).constData());
        QVERIFY(!QFileInfo::exists(second.referencePath));
        QVERIFY(m_window->mainModelId() == reference);
        QCOMPARE(r.referenceRate, rate);

        QCOMPARE(int(r.summary.punchIns.size()), 1);
        QVERIFY(std::fabs(r.summary.punchIns[0].range.start - 6.3) < 1e-4);
        QCOMPARE(r.summary.judged, 1);
        QCOMPARE(int(r.summary.events.size()), 1);
        QVERIFY(std::fabs(r.summary.events[0].expectedSeconds - 7.2) < 1e-4);
        QCOMPARE(int(r.takes.size()), 1);

        const Coverage &coverage = m_window->takes()->getCoverage();
        QCOMPARE(int(coverage.getRanges().size()), 2);
        QVERIFY(coverage.contains(sv::sv_frame_t(2.3 * rate)));
        QVERIFY(coverage.contains(sv::sv_frame_t(6.4 * rate)));
    }

    // The session of an earlier check, never saved and changed by its
    // takes, is replaced without asking; and the new reference goes to a
    // file of its own, the earlier one being open
    void check_replaces_its_own_session_without_asking() {
        TestDataLocation testData;
        makeWindow(loopback());
        const QString earlier = openEarlierCheckReference();
        QVERIFY(earlier != "");
        const sv::ModelId reference = m_window->mainModelId();
        m_window->markModified();
        QVERIFY(m_window->isDocumentModified());

        AudioCheckRunner::Plan plan = onePunchIn();
        plan.referencePath = "";
        startCheck(plan, false);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        QVERIFY2(m_dialogs.isEmpty(), qPrintable(m_dialogs.join(" | ")));
        QVERIFY(m_window->mainModelId() != reference);

        // The earlier one was open, so it stayed, and the new one has a
        // name of its own
        const QStringList references =
            QDir(AudioCheckRunner::referenceDirectory()).entryList
            (QStringList() << "calibrate-audio-reference*.wav", QDir::Files,
             QDir::Name);
        QCOMPARE(references, QStringList()
                 << "calibrate-audio-reference-1.wav"
                 << "calibrate-audio-reference-2.wav");
        QCOMPARE(QFileInfo(earlier).fileName(),
                 QString("calibrate-audio-reference-1.wav"));

        m_window->audioCheck()->cancel();
        QCOMPARE(m_finished, 1);
    }

    // A session of the user's is asked about before it is replaced: their
    // song, and a check's session they saved. The watchdog answers
    void check_asks_before_replacing_the_users_session() {
        TestDataLocation testData;
        makeWindow(loopback());

        openSong();
        if (QTest::currentTestFailed()) return;
        m_window->markModified();
        startCheck(onePunchIn(), false);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(!m_dialogs.isEmpty(), 10000);
        QVERIFY2(m_dialogs.first().startsWith("Session modified"),
                 qPrintable(m_dialogs.join(" | ")));
        QTRY_VERIFY_WITH_TIMEOUT(m_finished > 0 ||
                                 m_window->recordTarget()->isRecording(),
                                 30000);
        m_window->audioCheck()->cancel();
        QCOMPARE(m_finished, 1);
        QTRY_VERIFY_WITH_TIMEOUT
            (!sv::ModelTransformerFactory::getInstance()
             ->haveRunningTransformers(), 30000);
        m_dialogs.clear();

        const QString earlier = openEarlierCheckReference();
        QVERIFY(earlier != "");
        QVERIFY(m_window->doSaveSessionAs(m_dir.filePath("kept.ton")));
        m_window->markModified();
        m_finished = 0;
        startCheck(onePunchIn(), false);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(!m_dialogs.isEmpty(), 10000);
        QVERIFY2(m_dialogs.first().startsWith("Session modified"),
                 qPrintable(m_dialogs.join(" | ")));
        QTRY_VERIFY_WITH_TIMEOUT(m_finished > 0 ||
                                 m_window->recordTarget()->isRecording(),
                                 30000);
        m_window->audioCheck()->cancel();
        QCOMPARE(m_finished, 1);
        m_dialogs.clear();
    }

    // Record is shut while a check runs. A press that reaches the window
    // all the same does not stop the check's take, which records to the
    // end of its range as if nothing had happened
    void check_ignores_the_record_button() {
        makeWindow(loopback());
        QVERIFY(m_window->recordAction()->isEnabled());

        startCheck(onePunchIn());
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        QVERIFY(!m_window->recordAction()->isEnabled());

        m_window->recordAction()->trigger();
        emit m_window->recordAction()->triggered(false);
        QVERIFY(m_window->recordTarget()->isRecording());
        QVERIFY(m_window->recordAction()->isChecked());
        QTest::qWait(300);
        QVERIFY(m_window->recordTarget()->isRecording());

        QTRY_VERIFY_WITH_TIMEOUT(m_finished > 0, 60000);
        QVERIFY2(m_result.summary.verdict == LatencyCheck::Verdict::Ok,
                 describe(m_result).constData());
        QCOMPARE(m_result.summary.found, 1);
        QCOMPARE(int(m_result.takes.size()), 1);
        QVERIFY(m_window->recordAction()->isEnabled());
    }

    // Playback > Calibrate Audio: a dialog, not modal, that names the
    // devices and the latency in use and starts a check. While the check
    // runs, neither it nor the device menus can be chosen. Other devices
    // are named in the Preferences meanwhile; Use this latency keeps the
    // round trip measured for the devices the check started on, and the
    // menu's line says what the devices named now are placed with
    void calibrate_audio_from_the_menu() {
        setDevices("Speakers A", "Microphone A");
        makeWindow(loopback());

        m_window->calibrateAudioAction()->trigger();
        CalibrateAudioDialog *dialog = m_window->calibrateAudioDialog();
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        QVERIFY(!dialog->isModal());
        QVERIFY(dialog->page() == CalibrateAudioDialog::Page::Instructions);
        const QString instructions = dialog->pageText();
        for (QString words : { "Speakers A", "Microphone A", "off your ears",
                               "moderate volume", "room quiet",
                               "replaces the session", "save your work",
                               "driver's figure" }) {
            QVERIFY2(instructions.contains(words),
                     qPrintable(words + " not in: " + instructions));
        }

        // Four punch-ins of three sweeps, which the calibration layout has
        // room for, unless the test says otherwise
        const AudioCheckRunner::Plan plan = dialog->plan();
        QCOMPARE(plan.punchIns, 4);
        QCOMPARE(plan.eventsEach, 3);
        QCOMPARE(int(LatencyCheck::punchInsFor
                     (plan.layout, plan.punchIns, plan.eventsEach).size()), 4);

        dialog->setPlan(shortPlan());
        m_window->discardModifications();
        dialog->startCheck();
        QVERIFY(m_window->audioCheck()->isRunning());
        QVERIFY(dialog->page() == CalibrateAudioDialog::Page::Progress);
        setDevices("Speakers B", "Microphone B");

        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        QVERIFY(!m_window->calibrateAudioAction()->isEnabled());
        QVERIFY(!m_window->audioOutputMenu()->menuAction()->isEnabled());
        QVERIFY(!m_window->audioInputMenu()->menuAction()->isEnabled());
        QVERIFY2(dialog->pageText().contains("Recording punch-in 1 of 2"),
                 qPrintable(dialog->pageText()));

        QTRY_VERIFY_WITH_TIMEOUT
            (dialog->page() == CalibrateAudioDialog::Page::Result, 60000);
        QCOMPARE(m_finished, 1);
        QVERIFY(m_window->calibrateAudioAction()->isEnabled());
        QVERIFY(m_window->audioOutputMenu()->menuAction()->isEnabled());
        QVERIFY(m_window->audioInputMenu()->menuAction()->isEnabled());

        // 12411 frames measured, 12288 reported, at 44.1 kHz
        QVERIFY2(m_result.calibrationUsable(), describe(m_result).constData());
        QString words = dialog->pageText();
        for (QString w : { "came back steadily",
                           "281 ms measured; the driver reports 279 ms",
                           "output Speakers A; input Microphone A" }) {
            QVERIFY2(words.contains(w),
                     qPrintable(w + " not in: " + words));
        }
        QVERIFY(dialog->canUseLatency());

        dialog->useLatency();
        QVERIFY(!dialog->canUseLatency());
        QVERIFY2(dialog->pageText().contains("Kept."),
                 qPrintable(dialog->pageText()));

        LatencyCalibration::Figure figure;
        QSettings settings;
        QVERIFY(!LatencyCalibration::load
                (settings, key("Speakers B", "Microphone B"), figure));
        QVERIFY(LatencyCalibration::load
                (settings, key("Speakers A", "Microphone A"), figure));
        QVERIFY2(std::fabs(figure.roundTrip * rate - roundTrip) <= 4.0,
                 qPrintable(QString("kept %1 frames")
                            .arg(figure.roundTrip * rate)));

        QCOMPARE(latencyLine(), QString("Latency: driver's figure, 279 ms"));
        QVERIFY(!m_window->forgetLatencyAction()->isEnabled());
        setDevices("Speakers A", "Microphone A");
        const QString line = latencyLine();
        QVERIFY2(line.startsWith("Latency: measured 281 ms, "),
                 qPrintable(line));
        QVERIFY(m_window->forgetLatencyAction()->isEnabled());
    }

    // Forget Measured Latency: the menu's line goes back to the driver's
    // figure, and the figure kept is gone
    void calibrate_audio_forget_measured_latency() {
        makeWindow(loopback());
        const LatencyCalibration::InUse reported = m_window->latencyInUse();
        QVERIFY(reported.source == LatencyCalibration::Source::Reported);
        QVERIFY2(latencyLine().startsWith("Latency: driver's figure"),
                 qPrintable(latencyLine()));
        QVERIFY(!m_window->forgetLatencyAction()->isEnabled());

        // As the check keeps one, for the devices the device reports now
        LatencyCalibration::Figure figure;
        figure.roundTrip = 0.3;
        figure.date = QDateTime::currentDateTimeUtc();
        figure.reportedOutput = reported.reportedOutput;
        figure.reportedInput = reported.reportedInput;
        QSettings settings;
        LatencyCalibration::store(settings, key("", ""), figure);

        const QString line = latencyLine();
        QCOMPARE(line, QString("Latency: measured 300 ms, %1")
                 .arg(QLocale().toString(QDate::currentDate(), "d MMM")));
        QVERIFY(m_window->forgetLatencyAction()->isEnabled());

        m_window->forgetLatencyAction()->trigger();
        QVERIFY(!LatencyCalibration::load(settings, key("", ""), figure));
        QVERIFY(m_window->latencyInUse().source ==
                LatencyCalibration::Source::Reported);
        QVERIFY2(m_window->latencyLineAction()->text()
                 .startsWith("Latency: driver's figure"),
                 qPrintable(m_window->latencyLineAction()->text()));
        QVERIFY(!m_window->forgetLatencyAction()->isEnabled());
    }

    // What the result page says, for results made here: the verdict in
    // plain words with its fix, and Use this latency only when the figure
    // can be used
    void calibrate_audio_result_words() {
        makeWindow(loopback());
        m_window->calibrateAudioAction()->trigger();
        CalibrateAudioDialog *dialog = m_window->calibrateAudioDialog();
        QVERIFY(dialog);

        auto verify = [&](const AudioCheckResult &result, bool usable,
                          QStringList present, QStringList absent) {
            dialog->showResult(result);
            QVERIFY(dialog->page() == CalibrateAudioDialog::Page::Result);
            const QString words = dialog->pageText();
            for (QString w : present) {
                QVERIFY2(words.contains(w),
                         qPrintable(w + " not in: " + words));
            }
            for (QString w : absent) {
                QVERIFY2(!words.contains(w),
                         qPrintable(w + " in: " + words));
            }
            QCOMPARE(dialog->canUseLatency(), usable);
        };

        // Found one sweep in twelve
        AudioCheckResult silent =
            judgedResult(LatencyCheck::Verdict::NoSignal);
        silent.summary.found = 1;
        verify(silent, false,
               { "could not hear the test sounds: it found 1 of 12",
                 "volume up", "not muted", "Audio enhancements",
                 "\"Hands-Free\"", "not measured; the driver reports 279 ms" },
               { "Use this latency" });
        if (QTest::currentTestFailed()) return;

        // A device at 48 kHz is judged like any other: its rate is in the
        // figures, as a fact
        AudioCheckResult fast = judgedResult(LatencyCheck::Verdict::Ok);
        fast.recordingRate = otherRate;
        fast.key.rate = otherRate;
        verify(fast, true,
               { "The test sounds came back steadily",
                 "Press Use this latency",
                 "282 ms measured; the driver reports",
                 "recorded at 48000 Hz, converted to the reference's "
                 "44100 Hz" },
               { "cannot line up", "reference at 44100 Hz" });
        if (QTest::currentTestFailed()) return;

        // Unsteady, but usable; and the microphone monitored
        AudioCheckResult unsteady =
            judgedResult(LatencyCheck::Verdict::Unsteady);
        unsteady.summary.spread = 0.008;
        unsteady.summary.echo.heard = true;
        unsteady.summary.echo.delaySeconds = 0.045;
        unsteady.summary.echo.levelDb = -12.0;
        verify(unsteady, true,
               { "The driver's timing varies from take to take by 8 ms.",
                 "Your microphone is being played back somewhere",
                 "Listen to this device", "45 ms later",
                 "45 ms after the sound, 12 dB quieter",
                 "recorded at 44100 Hz, reference at 44100 Hz" },
               { "Kept." });
        if (QTest::currentTestFailed()) return;

        // Copy puts all of it on the clipboard, under when it was made
        dialog->copyReport();
        const QString copied = QGuiApplication::clipboard()->text();
        QCOMPARE(copied, dialog->reportText());
        for (QString w : { "Calibrate Audio, ",
                           "The driver's timing varies from take to take",
                           "45 ms after the sound, 12 dB quieter",
                           "output (System Default); input (System Default)" }) {
            QVERIFY2(copied.contains(w), qPrintable(w + " not in: " + copied));
        }
    }

    // On a phone the instructions name the route the device has open and
    // the phone's loopback, and the result's advice is a phone's, with
    // the streams the figure is checked against
    void calibrate_audio_on_a_phone() {
        FakeAudioIO::Config config = loopback();
        config.route = phoneRoute();
        makeWindow(config);
        // The device opens with the first file
        QCOMPARE(m_window->audioRoute().driver, QString());
        openSong();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->audioRoute().driver, QString("oboe"));

        m_window->calibrateAudioAction()->trigger();
        CalibrateAudioDialog *dialog = m_window->calibrateAudioDialog();
        QVERIFY(dialog);
        QVERIFY(dialog->page() == CalibrateAudioDialog::Page::Instructions);
        QString words = dialog->pageText();
        for (QString w : { "Built-in speaker (Pixel 7)",
                           "Built-in microphone (Pixel 7)",
                           "phone's microphone", "off your ears",
                           "speaker and microphone make the loop",
                           "microphone of its own", "calibrated on its own",
                           "driver's figure" }) {
            QVERIFY2(words.contains(w), qPrintable(w + " not in: " + words));
        }
        QVERIFY2(!words.contains("System Default"), qPrintable(words));

        AudioCheckResult silent = judgedResult(LatencyCheck::Verdict::NoSignal);
        silent.summary.found = 1;
        silent.route = phoneRoute();
        silent.key = LatencyCalibration::routeKey(phoneRoute(), rate);
        dialog->showResult(silent);
        words = dialog->pageText();
        for (QString w : { "could not hear the test sounds",
                           "Settings, Apps", "cancelling echo",
                           "output Built-in speaker (Pixel 7); input "
                           "Built-in microphone (Pixel 7)",
                           "Streams:", "Exclusive, burst 96, buffer 192" }) {
            QVERIFY2(words.contains(w), qPrintable(w + " not in: " + words));
        }
        for (QString w : { "Windows", "Hands-Free" }) {
            QVERIFY2(!words.contains(w), qPrintable(w + " in: " + words));
        }

        AudioCheckResult fading = judgedResult(LatencyCheck::Verdict::Fading);
        fading.summary.fadingDb = 12.0;
        fading.route = phoneRoute();
        dialog->showResult(fading);
        words = dialog->pageText();
        QVERIFY2(words.contains("echo cancellation or noise suppression"),
                 qPrintable(words));
        QVERIFY2(!words.contains("Windows"), qPrintable(words));
    }

    // Not while an ordinary take is being recorded: the check records
    // takes of its own
    void calibrate_audio_not_during_a_take() {
        makeWindow(FakeAudioIO::Config());
        openSong();
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->calibrateAudioAction()->isEnabled());

        m_window->doRecord();
        QVERIFY(m_window->recordTarget()->isRecording());
        QVERIFY(!m_window->calibrateAudioAction()->isEnabled());
        QTest::qWait(300);

        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QTRY_VERIFY_WITH_TIMEOUT
            (m_window->calibrateAudioAction()->isEnabled(), 10000);
    }

    // Closing the dialog while its check runs cancels the check; opened
    // again, it starts from the instructions
    void calibrate_audio_closed_during_a_check() {
        makeWindow(loopback());
        CalibrateAudioDialog *dialog = startCheckFromMenu();
        QVERIFY(dialog);
        QVERIFY(m_window->audioCheck()->isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);

        QVERIFY(dialog->close());
        QVERIFY(!dialog->isVisible());
        QVERIFY(!m_window->audioCheck()->isRunning());
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(!m_window->audioCheckTakes());
        QCOMPARE(m_finished, 1);
        QVERIFY(m_result.failure != "");
        QVERIFY(m_window->calibrateAudioAction()->isEnabled());

        m_window->calibrateAudioAction()->trigger();
        QVERIFY(dialog->isVisible());
        QVERIFY(dialog->page() == CalibrateAudioDialog::Page::Instructions);
    }
};

#endif
