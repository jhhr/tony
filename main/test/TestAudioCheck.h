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
// reference, about 13 s of real time.
//
// The same dialog watchdog as TestRecordWorkflow's: a dialog would
// block the test for ever, so it is dismissed, and the test fails in
// cleanup().

#include "TestRecordWorkflow.h"

#include "../AudioCheckRunner.h"
#include "../LatencyCheck.h"

class TestAudioCheck : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;

    // What the device reports, and what the round trip really is
    static constexpr int reportedOut = 2 * 4096;
    static constexpr int reportedIn = 4096;
    static constexpr int roundTrip = 3 * 4096 + 123;

    QTemporaryDir m_dir;
    TestMainWindow *m_window = nullptr;
    QTimer m_watchdog;
    QStringList m_dialogs;

    // What the runner said when the run ended, and how often it said it
    AudioCheckResult m_result;
    int m_finished = 0;

    void makeWindow(FakeAudioIO::Config config, bool installDevice = true) {
        delete m_window;
        m_window = new TestMainWindow(config, installDevice);
        m_result = AudioCheckResult();
        m_finished = 0;
        connect(m_window->audioCheck(), &AudioCheckRunner::finished,
                this, [this](const AudioCheckResult &result) {
                    m_result = result;
                    ++m_finished;
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

    void startCheck() {
        // As answering "No" to "do you want to save?", which the check
        // asks before it replaces the session
        m_window->discardModifications();
        QVERIFY(m_window->audioCheck()->start(shortPlan()));
        QVERIFY(m_window->audioCheck()->isRunning());
    }

    void runCheck() {
        startCheck();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_finished > 0, 60000);
        QCOMPARE(m_finished, 1);
        QVERIFY(!m_window->audioCheck()->isRunning());
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
        SingingTakes::setOverwriteConfirmationWanted(true);
    }

    void cleanup() {
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
            QCOMPARE(t.reportedOutput, sv::sv_frame_t(reportedOut));
            QCOMPARE(t.reportedInput, sv::sv_frame_t(reportedIn));
            QCOMPARE(t.recordingRate, rate);
        }
        QCOMPARE(r.usedRoundTrip, (reportedOut + reportedIn) / rate);
        QCOMPARE(r.reportedOutputLatency, reportedOut / rate);
        QCOMPARE(r.reportedInputLatency, reportedIn / rate);
        QCOMPARE(r.recordingRate, rate);
        QCOMPARE(r.referenceRate, rate);
        QVERIFY(!r.rateMismatch);
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

    // A device at 48 kHz: the takes are recorded at a rate other than the
    // reference's, which is reported with both rates, whatever the sweeps
    // say. What Tony does with such takes is a known bug of its own, so
    // only what the check reports is asserted, and that it ends
    void check_flags_a_rate_mismatch() {
        FakeAudioIO::Config config = loopback();
        config.sampleRate = 48000;
        makeWindow(config);

        runCheck();
        if (QTest::currentTestFailed()) return;

        qDebug() << "48 kHz:" << describe(m_result).constData();
        QVERIFY2(m_result.rateMismatch, describe(m_result).constData());
        QCOMPARE(m_result.recordingRate, 48000.0);
        QCOMPARE(m_result.referenceRate, rate);
        QVERIFY(!m_result.calibrationUsable());
        QVERIFY(!m_window->audioCheckTakes());
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
};

#endif
