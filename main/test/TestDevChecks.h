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

#ifndef TEST_DEV_CHECKS_H
#define TEST_DEV_CHECKS_H

#ifdef TONY_DEV_CHECKS

// Tier 5, as TestAudioCheck: the development checks (DevChecks) on the
// real MainWindow, recording from the fake device with its output
// looped back into its input. A run records two punch-ins against the
// 40 s dev reference, then saves the session and opens it again: about
// 20 s of real time.
//
// The fixture is TestAudioCheck's, copied rather than shared. The
// application's data directory, where the check writes its references,
// is Qt's test location while this class runs; the report and the
// scratch folders go to directories of the tests' own, so that a
// failing run's report never lands among the suites' results.

#include "TestRecordWorkflow.h"

#include "../AudioCheckRunner.h"
#include "../CalibrateAudioDialog.h"
#include "../LatencyCheck.h"
#include "../dev/DevChecks.h"

#include <QStandardPaths>

class TestDevChecks : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;

    // What the device reports, and what the round trip really is: as
    // TestAudioCheck's, 123 frames (2.8 ms) more than reported
    static constexpr int reportedOut = 2 * 4096;
    static constexpr int reportedIn = 4096;
    static constexpr int roundTrip = 3 * 4096 + 123;

    QTemporaryDir m_dir;
    TestMainWindow *m_window = nullptr;
    QTimer m_watchdog;
    QStringList m_dialogs;

    // What the dev checks said when the run ended, and how often; the
    // stages they began; and every result of the runner's
    DevReport m_report;
    int m_finished = 0;
    QStringList m_stages;
    std::vector<AudioCheckResult> m_checks;

    void makeWindow(FakeAudioIO::Config config) {
        delete m_window;
        m_window = new TestMainWindow(config);
        m_report = DevReport();
        m_finished = 0;
        m_stages.clear();
        m_checks.clear();
        connect(m_window->devChecks(), &DevChecks::finished,
                this, [this](const DevReport &report) {
                    m_report = report;
                    ++m_finished;
                });
        connect(m_window->devChecks(), &DevChecks::progress,
                this, [this](QString stage, int n, int of) {
                    m_stages << QString("%1 of %2: %3").arg(n).arg(of)
                        .arg(stage);
                });
        connect(m_window->audioCheck(), &AudioCheckRunner::finished,
                this, [this](const AudioCheckResult &result) {
                    m_checks.push_back(result);
                });
    }

    static FakeAudioIO::Config loopback() {
        FakeAudioIO::Config config;
        config.playbackLatency = reportedOut;
        config.recordLatency = reportedIn;
        config.inputDelay = roundTrip;
        config.loopback = true;
        return config;
    }

    QString reportDirectory() { return m_dir.filePath("report"); }
    QString scratchDirectory() { return m_dir.filePath("scratch"); }

    DevChecks::Options options(double roundTripSeconds) {
        DevChecks::Options o;
        o.roundTrip = roundTripSeconds;
        o.reportDirectory = reportDirectory();
        o.scratchDirectory = scratchDirectory();
        return o;
    }

    // TestAudioCheck's short calibration, with its reference where the
    // check writes one of its own: in the application's data directory
    AudioCheckRunner::Plan shortPlan() {
        AudioCheckRunner::Plan plan;
        plan.layout = LatencyCheck::calibrationLayout();
        plan.layout.length = plan.layout.events[5].sweepStart;
        plan.layout.events.resize(5);
        plan.punchIns = 2;
        plan.eventsEach = 2;
        return plan;
    }

    void startDevChecks(double roundTripSeconds) {
        m_window->discardModifications();
        QVERIFY(m_window->devChecks()->start(options(roundTripSeconds)));
        QVERIFY(m_window->devChecks()->isRunning());
    }

    void runDevChecks(double roundTripSeconds) {
        startDevChecks(roundTripSeconds);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_finished > 0, 120000);
        QCOMPARE(m_finished, 1);
        QVERIFY(!m_window->devChecks()->isRunning());
        QVERIFY(!m_window->audioCheck()->isRunning());
    }

    const CheckResult *check(int item) const {
        for (const CheckResult &c : m_report.checks) {
            if (c.item == item) return &c;
        }
        return nullptr;
    }

    static QString number(const CheckResult &c, QString label) {
        for (const auto &n : c.numbers) {
            if (n.first == label) return n.second;
        }
        return QString();
    }

    // "-20.1 ms" as -20.1; NaN for anything else
    static double milliseconds(QString text) {
        bool ok = false;
        double ms = text.endsWith(" ms") ? text.chopped(3).toDouble(&ok) : 0.0;
        return ok ? ms : std::nan("");
    }

    QString reportText() {
        QFile file(m_report.reportPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        return QString::fromUtf8(file.readAll());
    }

    QString lastReportLine() {
        const QStringList lines = reportText().trimmed().split('\n');
        return lines.isEmpty() ? QString() : lines.last();
    }

    QByteArray describe() {
        QStringList words;
        words << "failure: " + m_report.failure;
        for (const CheckResult &c : m_report.checks) {
            QStringList numbers;
            for (const auto &n : c.numbers) numbers << n.first + ": " + n.second;
            words << QString("item %1 %2 %3 (%4) [%5]").arg(c.item).arg(c.name)
                .arg(CheckResult::verdictName(c.verdict)).arg(c.message)
                .arg(numbers.join("; "));
        }
        return words.join(" | ").toUtf8();
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

    // A round trip of 300 ms kept for the fake device, as Use this
    // latency keeps one, and the user's toggles set otherwise than the
    // check sets them for its takes
    void setUserState() {
        LatencyCalibration::InUse reported = m_window->latencyInUse();
        LatencyCalibration::Figure figure;
        figure.roundTrip = 0.3;
        figure.date = QDateTime::currentDateTimeUtc();
        figure.reportedOutput = reported.reportedOutput;
        figure.reportedInput = reported.reportedInput;
        LatencyCalibration::Key key;
        key.rate = rate;
        QSettings settings;
        LatencyCalibration::store(settings, key, figure);
        settings.setValue("MainWindow/prerollseconds", 3.0);
        m_window->setPlayReferenceWhileRecording(false);
        m_window->setPreRoll(true);
        m_window->setRecordIntoSelection(false);
    }

    // A run ended early: finished once, every check skipped, nothing
    // recording, and none of the user's state changed
    void verifyEndedEarly(QStringList togglesBefore,
                          QStringList storedBefore) {
        QCOMPARE(m_finished, 1);
        QVERIFY(m_report.failure != "");
        QVERIFY(!m_window->devChecks()->isRunning());
        QVERIFY(!m_window->audioCheck()->isRunning());
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(!m_window->audioCheckTakes());
        QCOMPARE(int(m_report.checks.size()), 2);
        for (const CheckResult &c : m_report.checks) {
            QVERIFY2(c.verdict == CheckResult::Verdict::Skipped, describe());
            QVERIFY2(c.message.contains(m_report.failure), describe());
        }
        QCOMPARE(lastReportLine(),
                 QString("Totals: 0 passed, 0 failed, 0 measured, 2 skipped"));

        QTest::qWait(500);
        QCOMPARE(m_finished, 1);
        QVERIFY(!m_window->recordTarget()->isRecording());
        QCOMPARE(toggles(), togglesBefore);
        QCOMPARE(storedLatency(), storedBefore);
    }

    // Not a slot: QtTest would run it as a test. As TestAudioCheck's
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

        // The check's references go where Qt keeps the tests' data
        QStandardPaths::setTestModeEnabled(true);

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
        QStandardPaths::setTestModeEnabled(false);
    }

    // Each run gets a folder of its own, and the folders of earlier runs
    // go, but the one the session open now lives in, and anything not
    // named as they are
    void dev_checks_scratch_folders() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString one = dir.filePath("dev-checks-1");
        const QString two = dir.filePath("dev-checks-2");

        QCOMPARE(DevChecks::nextScratchFolder(dir.path(), ""), one);
        QVERIFY(QFileInfo(one).isDir());
        {
            QFile session(QDir(one).filePath("dev-checks.ton"));
            QVERIFY(session.open(QIODevice::WriteOnly));
        }
        QVERIFY(QDir().mkpath(dir.filePath("dev-checks-x")));
        QVERIFY(QDir().mkpath(dir.filePath("mine")));

        // With the first run's session open
        const QString session = QDir(one).filePath("dev-checks.ton");
        QCOMPARE(DevChecks::nextScratchFolder(dir.path(), session), two);
        QVERIFY(QFileInfo::exists(session));

        // With the second's, whose folder holds a takes folder
        QVERIFY(QDir().mkpath(QDir(two).filePath("dev-checks.takes")));
        QCOMPARE(DevChecks::nextScratchFolder
                 (dir.path(), QDir(two).filePath("dev-checks.ton")), one);
        QVERIFY(!QFileInfo::exists(session));
        QVERIFY(QFileInfo(QDir(two).filePath("dev-checks.takes")).isDir());
        QCOMPARE(QDir(dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                            QDir::Name),
                 QStringList() << "dev-checks-1" << "dev-checks-2"
                 << "dev-checks-x" << "mine");
    }

    // The loopback fake, placed with its true round trip: every sweep of
    // both punch-ins lands within 2 ms, the session saved and opened
    // again holds the same take, and each punch-in measured its own start
    // gap. The report ends with its totals, and the session open
    // afterwards is the one saved in the scratch folder
    void dev_checks_pass_with_the_true_round_trip() {
        makeWindow(loopback());

        runDevChecks(roundTrip / rate);
        if (QTest::currentTestFailed()) return;

        // Line by line, so that none begins a line of the suite's log:
        // its Totals would be taken for the suite's
        for (const QString &line : reportText().split('\n')) {
            qDebug().noquote() << "report:" << line;
        }
        QVERIFY2(m_report.failure == "", describe());
        QCOMPARE(int(m_report.checks.size()), 2);
        const CheckResult *latency = check(1);
        const CheckResult *phrases = check(2);
        QVERIFY(latency && phrases);
        QCOMPARE(latency->name, QString("latency_on_this_machine"));
        QCOMPARE(phrases->name, QString("several_phrases_in_one_take"));
        QVERIFY2(latency->verdict == CheckResult::Verdict::Pass, describe());
        QVERIFY2(phrases->verdict == CheckResult::Verdict::Pass, describe());
        QCOMPARE(number(*latency, "round trip used"),
                 QString("%1 ms").arg(roundTrip * 1000.0 / rate, 0, 'f', 1));
        QVERIFY2(number(*latency, "pitch after reopening")
                 .endsWith("pitch events, the same"), describe());

        QCOMPARE(m_stages, QStringList() << "1 of 2: Fresh punch-ins"
                 << "2 of 2: Save and reopen");

        // Two punch-ins into a reference of the dev layout, recorded in
        // the order given
        QCOMPARE(int(m_checks.size()), 1);
        const LatencyCheck::TakeSummary &s = m_checks[0].summary;
        QCOMPARE(int(s.punchIns.size()), 2);
        QCOMPARE(s.judged, 4);
        QCOMPARE(s.found, 4);

        QVERIFY(QFileInfo(m_report.reportPath).fileName() == "DevChecks.txt");
        QVERIFY(TakesFile::isInFolder(reportDirectory(), m_report.reportPath));
        QCOMPARE(lastReportLine(),
                 QString("Totals: 2 passed, 0 failed, 0 measured, 0 skipped"));

        QVERIFY(m_report.sessionPath != "");
        QCOMPARE(m_window->sessionFile(), m_report.sessionPath);
        QVERIFY2(TakesFile::isInFolder(scratchDirectory(), m_report.sessionPath),
                 qPrintable(m_report.sessionPath));
        QVERIFY2(TakesFile::isInFolder
                 (TakesFile::takesFolder(m_report.sessionPath),
                  m_window->takes()->getAudioPath()),
                 qPrintable(m_window->takes()->getAudioPath()));
        QVERIFY(!m_window->isDocumentModified());
        QVERIFY(!m_window->audioCheckTakes());
        QVERIFY(m_window->recordAction()->isEnabled());
    }

    // The same with a round trip 20 ms too long: every take is spliced
    // from 20 ms too late, and lands 20 ms early. Both items fail, and
    // the report shows where the sweeps landed
    void dev_checks_fail_with_the_round_trip_off() {
        makeWindow(loopback());

        runDevChecks(roundTrip / rate + 0.020);
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_report.failure == "", describe());
        const CheckResult *latency = check(1);
        const CheckResult *phrases = check(2);
        QVERIFY(latency && phrases);
        QVERIFY2(latency->verdict == CheckResult::Verdict::Fail, describe());
        QVERIFY2(phrases->verdict == CheckResult::Verdict::Fail, describe());

        const QString largest = number(*latency, "largest offset");
        QVERIFY2(std::fabs(milliseconds(largest) + 20.0) <= 1.0, describe());
        for (QString label : { QString("punch-in 1, median offset"),
                               QString("punch-in 2, median offset") }) {
            QVERIFY2(std::fabs(milliseconds(number(*phrases, label)) + 20.0)
                     <= 1.0, describe());
        }

        // What the save and reopen kept is still right: only the placing
        QCOMPARE(number(*latency, "offsets after reopening"),
                 QString("the same"));

        const QString text = reportText();
        for (QString words : { QString("offsets, punch-in 1 (6.30 to 10.20 "
                                       "s): "),
                               QString("offsets, punch-in 2 (16.80 to 21.20 "
                                       "s): "),
                               "largest offset: " + largest,
                               "punch-in 1, median offset: " +
                               number(*phrases, "punch-in 1, median offset"),
                               QString("latency_on_this_machine: FAIL"),
                               QString("several_phrases_in_one_take: FAIL") }) {
            QVERIFY2(text.contains(words), qPrintable(words + " not in:\n" +
                                                      text));
        }
        QCOMPARE(lastReportLine(),
                 QString("Totals: 0 passed, 2 failed, 0 measured, 0 skipped"));
    }

    // Cancelled during a take: the take stops, the run ends once with
    // every check skipped, and the user's toggles and stored round trip
    // are as they were
    void dev_checks_cancelled() {
        makeWindow(loopback());
        setUserState();
        const QStringList togglesBefore = toggles();
        const QStringList storedBefore = storedLatency();
        QVERIFY(!storedBefore.isEmpty());

        startDevChecks(roundTrip / rate);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        QVERIFY(m_window->audioCheckTakes());
        QVERIFY(!m_window->recordAction()->isEnabled());
        QTest::qWait(500);

        m_window->devChecks()->cancel();
        verifyEndedEarly(togglesBefore, storedBefore);
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->recordAction()->isEnabled());
    }

    // The same when the session is closed during a take
    void dev_checks_end_when_the_session_closes() {
        makeWindow(loopback());
        setUserState();
        const QStringList togglesBefore = toggles();
        const QStringList storedBefore = storedLatency();

        startDevChecks(roundTrip / rate);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        QTest::qWait(500);

        m_window->doCloseSession();
        verifyEndedEarly(togglesBefore, storedBefore);
    }

    // Deleted during a take, as the window's destructor deletes them:
    // the audio check's run they started ends with them, silently, and
    // the take in progress is left to the window, which stops it at the
    // end of its range as any take into a selection. Then the window
    // itself, deleted during a take of theirs
    void dev_checks_deleted_during_a_run() {
        makeWindow(loopback());
        startDevChecks(roundTrip / rate);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);

        m_window->doDeleteDevChecks();
        QVERIFY(!m_window->audioCheck()->isRunning());
        QVERIFY(!m_window->audioCheckTakes());
        QCOMPARE(m_finished, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!m_window->recordTarget()->isRecording(),
                                 30000);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordAction()->isEnabled(),
                                 10000);
        QTRY_VERIFY_WITH_TIMEOUT
            (!sv::ModelTransformerFactory::getInstance()
             ->haveRunningTransformers(), 30000);

        makeWindow(loopback());
        startDevChecks(roundTrip / rate);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        delete m_window;
        m_window = nullptr;
        QCOMPARE(m_finished, 0);
    }

    // Playback > Calibrate Audio with the checkbox on, as it is by
    // default. A calibration that cannot be used (cancelled) is shown
    // with a word that the dev checks did not run. Check Again, run to
    // the end: the dev checks carry on from it, with the round trip it
    // measured, and Cancel then ends them; the result page shows the
    // calibration, a line for each check and the report's path
    void dev_checks_after_calibrating_from_the_dialog() {
        makeWindow(loopback());
        m_window->calibrateAudioAction()->trigger();
        CalibrateAudioDialog *dialog = m_window->calibrateAudioDialog();
        QVERIFY(dialog);
        QVERIFY(dialog->devChecksWanted());
        dialog->setPlan(shortPlan());
        dialog->setDevOptions(options(-1.0));

        m_window->discardModifications();
        dialog->startCheck();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        dialog->cancelCheck();
        QVERIFY(dialog->page() == CalibrateAudioDialog::Page::Result);
        QVERIFY2(dialog->pageText().contains("The dev checks did not run"),
                 qPrintable(dialog->pageText()));
        QVERIFY(!m_window->devChecks()->isRunning());
        QCOMPARE(m_finished, 0);

        dialog->startCheck();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->devChecks()->isRunning(), 60000);
        QCOMPARE(int(m_checks.size()), 2);
        const AudioCheckResult calibration = m_checks.back();
        QVERIFY(calibration.calibrationUsable());
        QVERIFY(dialog->page() == CalibrateAudioDialog::Page::Progress);
        QTRY_VERIFY_WITH_TIMEOUT
            (dialog->pageText().contains("Dev checks, stage 1 of 2"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->isRecording(),
                                 30000);
        QVERIFY(!m_window->calibrateAudioAction()->isEnabled());
        QVERIFY(!m_window->recordAction()->isEnabled());

        dialog->cancelCheck();
        QCOMPARE(m_finished, 1);
        QVERIFY(!m_window->devChecks()->isRunning());
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(dialog->page() == CalibrateAudioDialog::Page::Result);

        const QString words = dialog->pageText();
        for (QString w : { QString("came back steadily"),
                           QString("They ended early: The dev checks were "
                                   "cancelled."),
                           QString("Item 1, latency_on_this_machine: Skipped"),
                           QString("Item 2, several_phrases_in_one_take: "
                                   "Skipped"),
                           "Report: " + m_report.reportPath }) {
            QVERIFY2(words.contains(w), qPrintable(w + " not in: " + words));
        }
        QVERIFY(dialog->canUseLatency());

        // The dev checks were given the round trip the calibration measured
        QVERIFY2(reportText().contains
                 (QString("Round trip for the run: %1 ms")
                  .arg(calibration.calibratedRoundTrip * 1000.0, 0, 'f', 1)),
                 qPrintable(reportText()));
    }
};

#endif
#endif
