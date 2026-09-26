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
// looped back into its input. A run records two punch-ins far apart
// into a long song of 60 s here, then two against the 40 s dev
// reference, one over the end of the second, one near the start of the
// song, two that meet inside a held tone, then saves the session and
// opens it again: about 50 s of real time. The runs that look at other
// things leave the long song out.
//
// The fixture is TestAudioCheck's, copied rather than shared. The
// application's data directory, where the check writes its references,
// is Qt's test location while this class runs; the report and the
// scratch folders go to directories of the tests' own, so that a
// failing run's report never lands among the suites' results.

#include "TestMainWindow.h"
#include "TestSignals.h"

#include "../AudioCheckRunner.h"
#include "../CalibrateAudioDialog.h"
#include "../LatencyCheck.h"
#include "../TakesFile.h"
#include "../dev/DevChecks.h"

#include "version.h"

#include "base/PlayParameters.h"
#include "base/RecordDirectory.h"
#include "layer/Layer.h"
#include "transform/ModelTransformerFactory.h"
#include "widgets/InteractiveFileFinder.h"

#include <QObject>
#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
#include <vector>

class TestDevChecks : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;

    // A device at another rate, as WASAPI's mixer often runs
    static constexpr double otherRate = 48000.0;

    // What the device reports, and what the round trip really is: as
    // TestAudioCheck's, 123 frames (2.8 ms) more than reported
    static constexpr int reportedOut = 2 * 4096;
    static constexpr int reportedIn = 4096;
    static constexpr int roundTrip = 3 * 4096 + 123;

    // How far the fake's input moves against its output each time its
    // stream starts again: 10 ms, as TestAudioCheck's
    static constexpr int restartShift = 441;

    // The long song of the passing run: a quarter of the real one, long
    // enough that its analysis takes well over twice a punch-in's
    static constexpr double longSeconds = 60.0;

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

    // How often a test's fault was put in
    int m_faults = 0;

    // A stall of the GUI thread during the re-recording's lead-in
    // (stallTheReRecording()): watched for from the runner's first
    // report that it records; from where in the reference it is due, in
    // seconds (negative for none), and how long; how often it came, and
    // where playback was as it began and as it ended
    QTimer m_stallWatch;
    double m_stallAt = -1.0;
    int m_stallMs = 0;
    int m_stalls = 0;
    double m_stalledFrom = 0.0;
    double m_stalledTo = 0.0;

    void makeWindow(FakeAudioIO::Config config) {
        delete m_window;
        m_window = new TestMainWindow(config);
        m_report = DevReport();
        m_finished = 0;
        m_stages.clear();
        m_checks.clear();
        m_faults = 0;
        m_stallWatch.stop();
        m_stallAt = -1.0;
        m_stallMs = 0;
        m_stalls = 0;
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
        config.reportLevels = true;
        return config;
    }

    // The loopback with a room's noise on the input, at -60 dBFS: the
    // takes then hold something where the reference is silent, as they
    // do on a real device, so that a take played back out shows in the
    // output there. Too quiet for the sweep finder, and no pitch for the
    // live tracker or pYIN
    static FakeAudioIO::Config loopbackInARoom() {
        FakeAudioIO::Config config = loopback();
        config.input = TestSignals::whiteNoise(int(10 * rate), 1, 0.001);
        return config;
    }

    QString reportDirectory() { return m_dir.filePath("report"); }
    QString scratchDirectory() { return m_dir.filePath("scratch"); }

    // With the long song, unless a length of 0 leaves it out
    DevChecks::Options options(double roundTripSeconds,
                               double longSongSeconds = longSeconds) {
        DevChecks::Options o;
        o.roundTrip = roundTripSeconds;
        o.longSeconds = longSongSeconds;
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

    void startDevChecks(double roundTripSeconds,
                        double longSongSeconds = longSeconds) {
        m_window->discardModifications();
        QVERIFY(m_window->devChecks()->start(options(roundTripSeconds,
                                                     longSongSeconds)));
        QVERIFY(m_window->devChecks()->isRunning());
    }

    void runDevChecks(double roundTripSeconds,
                      double longSongSeconds = longSeconds) {
        startDevChecks(roundTripSeconds, longSongSeconds);
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

    // Item 3's "279: 262 on the tones, 12 at onsets, 5 on the sweeps, 0
    // elsewhere" as 279; -1 for anything else
    static int dotCount(QString text) {
        bool ok = false;
        int n = text.section(':', 0, 0).toInt(&ok);
        return ok ? n : -1;
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

    // Every check, or only the item given: QtTest cuts a long message
    QByteArray describe(int item = 0) {
        QStringList words;
        words << "failure: " + m_report.failure;
        for (const CheckResult &c : m_report.checks) {
            if (item > 0 && c.item != item) continue;
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
        QCOMPARE(int(m_report.checks.size()), 11);
        for (const CheckResult &c : m_report.checks) {
            QVERIFY2(c.verdict == CheckResult::Verdict::Skipped, describe());
            QVERIFY2(c.message.contains(m_report.failure), describe());
        }
        QCOMPARE(lastReportLine(),
                 QString("Totals: 0 passed, 0 failed, 0 measured, 11 skipped"));

        QTest::qWait(500);
        QCOMPARE(m_finished, 1);
        QVERIFY(!m_window->recordTarget()->isRecording());
        QCOMPARE(toggles(), togglesBefore);
        QCOMPARE(storedLatency(), storedBefore);
    }

    // The GUI thread held up, busy, for ms, as a busy system can hold a
    // window up: no timer fires meanwhile, so the observer takes no
    // look. It begins once the re-recording has played the reference up
    // to "at", in seconds. By default over the reference's silent gap
    // just before the punch-in (18.8 to 19.2 s): no look there lies
    // wholly in the gap
    void stallTheReRecording(double at = DevChecks::reRecording().start - 0.45,
                             int ms = 450) {
        m_stallAt = at;
        m_stallMs = ms;
        connect(m_window->audioCheck(), &AudioCheckRunner::progress,
                this, [this](const AudioCheckRunner::Progress &state) {
                    // Reported again as the seconds left go down
                    if (state.step == AudioCheckRunner::Step::Recording &&
                        m_stalls == 0 && !m_stallWatch.isActive() &&
                        !m_stages.isEmpty() &&
                        m_stages.last().endsWith(": Re-record")) {
                        m_stallWatch.start();
                    }
                });
    }

    // Not a slot: QtTest would run it as a test
    void stallIfDue() {
        sv::AudioCallbackRecordTarget *target =
            m_window ? m_window->recordTarget() : nullptr;
        if (m_stallAt < 0.0 || !target || !target->isRecording()) return;

        // Where the reference being handed out is: where playback
        // started, and the frames come in since, which the audio thread
        // counts. The record duration, which the cursor goes by, is
        // counted on this thread, and would stand still during the stall
        const sv::sv_frame_t start =
            m_window->playbackFrame() - target->getRecordDuration();
        auto played = [&]() {
            return double(start + target->getFramesReceived()) / rate;
        };
        const double at = played();
        if (at < m_stallAt) return;
        m_stallWatch.stop();
        QElapsedTimer held;
        held.start();
        while (held.elapsed() < m_stallMs) { }
        m_stalledFrom = at;
        m_stalledTo = played();
        ++m_stalls;
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

        m_stallWatch.setInterval(5);
        connect(&m_stallWatch, &QTimer::timeout,
                this, [this]() { stallIfDue(); });
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
        m_stallWatch.stop();

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

    // The loopback fake in a room, placed with its true round trip: every
    // sweep of every punch-in lands within 2 ms, the session saved and
    // opened again holds the same take, and each punch-in measured its own
    // start gap. The re-recording changed nothing outside its range and
    // nothing before it, and nothing of the take was heard during its
    // lead-in; the punch-in near the start played from the start of the
    // song; both stopped by themselves. Stop on the long song analysed
    // only the ranges; the joins have no step and the pitch runs through
    // them, but the note does not (expected to fail, a defect of the
    // notes merge). The report ends with its totals,
    // and the session open afterwards is the one saved in the scratch
    // folder
    void dev_checks_pass_with_the_true_round_trip() {
        makeWindow(loopbackInARoom());

        runDevChecks(roundTrip / rate);
        if (QTest::currentTestFailed()) return;

        // Line by line, so that none begins a line of the suite's log:
        // its Totals would be taken for the suite's
        for (const QString &line : reportText().split('\n')) {
            qDebug().noquote() << "report:" << line;
        }
        QVERIFY2(m_report.failure == "", describe());
        QCOMPARE(int(m_report.checks.size()), 11);
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

        // What was seen of each punch-in: the live dots on the
        // reference's sounds, and behind the cursor by the round trip at
        // least, since the cursor runs with what has been recorded;
        // nothing played where the reference is silent, though the
        // reference itself was heard; and the loopback on both inputs
        const CheckResult *dots = check(3);
        const CheckResult *speakers = check(4);
        const CheckResult *mic = check(5);
        QVERIFY(dots && speakers && mic);
        QCOMPARE(dots->name, QString("live_dots"));
        QCOMPARE(speakers->name,
                 QString("nothing_of_the_take_in_the_speakers"));
        QCOMPARE(mic->name, QString("mic_on_input_2"));
        QVERIFY2(dots->verdict == CheckResult::Verdict::Pass, describe());
        QVERIFY2(speakers->verdict == CheckResult::Verdict::Pass, describe());
        QVERIFY2(mic->verdict == CheckResult::Verdict::Measured, describe());
        for (QString label : { QString("dots, punch-in 1 (6.30 to 10.20 s)"),
                               QString("dots, punch-in 2 (16.80 to 21.20 "
                                       "s)") }) {
            QVERIFY2(dotCount(number(*dots, label)) > DevChecks::kMinDots,
                     describe());
            QVERIFY2(number(*dots, label).endsWith(", 0 elsewhere"),
                     describe());
        }
        QVERIFY2(milliseconds(number(*dots, "dots behind the cursor, median"))
                 >= roundTrip * 1000.0 / rate, describe());
        QCOMPARE(number(*speakers, "second arrival"), QString("none heard"));
        const QString gaps =
            number(*speakers, "largest output level in the silent gaps");
        QVERIFY2(gaps.startsWith("silence, over ") &&
                 !gaps.endsWith(" 0 looks"), describe());
        QCOMPARE(number(*speakers, "largest output level"),
                 QString("-12.0 dBFS"));
        QVERIFY2(number(*mic, "the mic is on").startsWith("inputs 1 and 2, "),
                 describe());
        QVERIFY2(mic->message.startsWith("Not applicable here"), describe());

        // What the device says of itself, at the head of the report, and
        // the driver it was opened through with the latency asked of it
        for (QString words : { QString("Audio driver: (auto)\n"),
                               QString("Latency asked for: 200.0 ms\n"),
                               QString("Audio drivers built in: "),
                               QString("Playback latency reported: 8192 "
                                       "frames (185.8 ms)"),
                               QString("Record latency reported: 4096 "
                                       "frames (92.9 ms)") }) {
            QVERIFY2(reportText().contains(words), qPrintable(words));
        }

        QCOMPARE(m_stages, QStringList() << "1 of 6: Long song"
                 << "2 of 6: Fresh punch-ins" << "3 of 6: Re-record"
                 << "4 of 6: Pre-roll near the start" << "5 of 6: Joins"
                 << "6 of 6: Save and reopen");

        // Two punch-ins far apart into the long song, each judging one
        // sweep; two into a reference of the dev layout, recorded in the
        // order given; then one over the end of the second, which judges
        // the sweep at 20.1 s; then one near the start, which judges the
        // sweep at 3.1 s; then two that meet at 28.7 s, judging the sweeps
        // at 26.9 and 30.9 s. Items 1 and 2 count all eight, numbered
        // along the run
        QCOMPARE(int(m_checks.size()), 5);
        for (int i : { 0, 1, 4 }) {
            const LatencyCheck::TakeSummary &s = m_checks[i].summary;
            QCOMPARE(int(s.punchIns.size()), 2);
            QCOMPARE(s.judged, i == 1 ? 4 : 2);
            QCOMPARE(s.found, s.judged);
        }
        for (int i : { 2, 3 }) {
            QCOMPARE(int(m_checks[i].summary.punchIns.size()), 1);
            QCOMPARE(m_checks[i].summary.judged, 1);
            QCOMPARE(m_checks[i].summary.found, 1);
        }
        QVERIFY(std::fabs(m_checks[2].summary.events[0].expectedSeconds -
                          20.1) < 1e-4);
        QVERIFY(std::fabs(m_checks[3].summary.events[0].expectedSeconds -
                          3.1) < 1e-4);
        QVERIFY(std::fabs(m_checks[4].summary.events[1].expectedSeconds -
                          30.9) < 1e-4);

        // The long song's sweeps: the first from a quarter of the way in,
        // and the first from five eighths
        const LatencyCheck::TakeSummary &song = m_checks[0].summary;
        for (int i : { 0, 1 }) {
            const double from = (i == 0 ? 0.25 : 0.625) * longSeconds;
            const double at = song.events[i].expectedSeconds;
            QVERIFY2(at >= from && at < from + 2.6,
                     qPrintable(QString::number(at)));
        }
        const LatencyCheck::PunchIn first = song.punchIns[0].range;
        for (QString label : { QString("offsets, punch-in 1 (%1 to %2 s)")
                               .arg(first.start, 0, 'f', 2)
                               .arg(first.end, 0, 'f', 2),
                               QString("offsets, punch-in 5 (19.20 to 21.20 "
                                       "s)"),
                               QString("offsets, punch-in 6 (1.00 to 4.20 "
                                       "s)"),
                               QString("offsets, punch-in 8 (28.70 to 32.00 "
                                       "s)") }) {
            QVERIFY2(number(*latency, label) != "", describe());
        }
        QVERIFY2(number(*phrases, "punch-in 8, start gap").endsWith("measured"),
                 describe());

        const CheckResult *position = check(7);
        const CheckResult *leadIn = check(12);
        const CheckResult *nearStart = check(13);
        const CheckResult *stops = check(14);
        QVERIFY(position && leadIn && nearStart && stops);
        QCOMPARE(position->name, QString("record_from_a_position"));
        QCOMPARE(leadIn->name,
                 QString("nothing_heard_or_changed_in_the_lead_in"));
        QCOMPARE(nearStart->name, QString("pre_roll_near_the_start"));
        QCOMPARE(stops->name,
                 QString("record_into_selection_stops_by_itself"));
        for (const CheckResult *c : { position, leadIn, nearStart, stops }) {
            QVERIFY2(c->verdict == CheckResult::Verdict::Pass, describe());
        }

        // Outside the re-recording's range, and before it, the take as it
        // was, and something there to compare
        QCOMPARE(number(*position, "audio outside the range"),
                 QString("the same, bit for bit"));
        QCOMPARE(number(*leadIn, "audio before 19.20 s"),
                 QString("the same, bit for bit"));
        for (const CheckResult *c : { position, leadIn }) {
            for (QString label : { QString("pitch"), QString("notes") }) {
                const QString words = number(*c, label);
                QVERIFY2(words.endsWith(", unchanged") &&
                         words.section(' ', 0, 0).toInt() > 0, describe());
            }
        }

        // The take under the lead-in holds the room's noise where the
        // reference is silent, and none of it was heard
        const QString leadInGaps =
            number(*leadIn, "output in the lead-in's silent gaps");
        QVERIFY2(leadInGaps.startsWith("silence, over ") &&
                 !leadInGaps.endsWith(" 0 looks"), describe());

        // A lead-in of all the 1 s there is before the punch-in, from the
        // start of the song, counted down from 2: 1 s and the round trip
        // of 0.28 s, with the start gap. For the moment before the round
        // trip is known the count is of the lead-in alone, 1
        QCOMPARE(number(*nearStart, "lead-in"),
                 QString("1.00 s, of the 3.00 s asked for"));
        QCOMPARE(number(*nearStart, "playback from"), QString("0.00 s"));
        QCOMPARE(number(*nearStart, "cursor, lowest"), QString("0.00 s"));
        QVERIFY2(QRegularExpression("^(1, )?2, 1; at most 2, ")
                 .match(number(*nearStart, "countdown")).hasMatch(),
                 describe());

        // Each stopped within a look of the take timer of when it could,
        // and the one near the start added its range to the take
        QVERIFY2(number(*stops, "coverage after, 1.00 to 4.20 s") ==
                 "1.00 to 4.20 s, 6.30 to 10.20 s, 16.80 to 21.20 s",
                 describe());
        QVERIFY2(number(*stops, "dialogs, 19.20 to 21.20 s")
                 .startsWith("none, over "), describe());

        // Stop on the long song analysed each punch-in's range alone, in
        // under half the time the whole song's analysis took, and the
        // second left the first's pitch as it was
        const CheckResult *longSong = check(9);
        QVERIFY(longSong);
        QCOMPARE(longSong->name, QString("stop_on_a_long_song"));
        QVERIFY2(longSong->verdict == CheckResult::Verdict::Pass, describe());
        const QString kept = number(*longSong, "pitch, punch-in 2");
        QVERIFY2(kept.endsWith(", unchanged") &&
                 kept.section(' ', 0, 0).toInt() > 0, describe());

        // The punch-ins that meet inside the held tone, placed alike: no
        // step in the samples there, the pitch running through, and
        // nothing moved outside the two
        const CheckResult *joins = check(10);
        QVERIFY(joins);
        QCOMPARE(joins->name, QString("the_joins"));
        QCOMPARE(number(*joins, "second punch-in against the first"),
                 QString("0.0 ms"));
        for (QString part : { QString("step: "), QString("pitch: "),
                              QString("outside: ") }) {
            QVERIFY2(!joins->message.contains(part), describe());
        }

        // And one note through the join. The second punch-in's analysis
        // starts 0.5 s before the join, inside the tone, so its note begins
        // before the merge window: the first's note, which ended at the
        // join, is that note going on and takes its end
        QVERIFY2(joins->verdict == CheckResult::Verdict::Pass, describe());

        QVERIFY(QFileInfo(m_report.reportPath).fileName() == "DevChecks.txt");
        QVERIFY(TakesFile::isInFolder(reportDirectory(), m_report.reportPath));
        QCOMPARE(lastReportLine(),
                 QString("Totals: 10 passed, 0 failed, 1 measured, 0 skipped"));

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

    // A whole run on a device at 48 kHz against references at 44.1, as
    // WASAPI's runs were: the takes are recorded at the device's rate
    // and converted as they are spliced, and each check counts every
    // figure at its own rate. The fake's delay counts its own frames, so
    // the true round trip is roundTrip frames at 48 kHz; bqaudioio's
    // ResamplerWrapper holds the output back about 1.1 ms more, which
    // nothing reports, and the sweeps land that late, within item 1's
    // 2 ms. Every item passes. Item 14 finds each take stopping about as
    // far past its selection as at 44.1 kHz, where it reads 0.25 to 0.35
    // s: with the round trip in the device's frames added to the lead-in
    // and the selection in the reference's, it read 0.34 s more, and
    // failed, as it did on the user's runs
    void dev_checks_pass_at_48000() {
        FakeAudioIO::Config config = loopbackInARoom();
        config.sampleRate = int(otherRate);
        makeWindow(config);

        runDevChecks(roundTrip / otherRate);
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_report.failure == "", describe());
        QCOMPARE(int(m_checks.size()), 5);
        for (const AudioCheckResult &r : m_checks) {
            QCOMPARE(r.recordingRate, otherRate);
            QCOMPARE(r.referenceRate, rate);
        }
        QCOMPARE(int(m_report.checks.size()), 11);
        for (const CheckResult &c : m_report.checks) {
            // Item 5 is measured only: the loopback is on both inputs
            const CheckResult::Verdict expected = (c.item == 5 ?
                CheckResult::Verdict::Measured : CheckResult::Verdict::Pass);
            QVERIFY2(c.verdict == expected, describe(c.item));
        }
        QCOMPARE(lastReportLine(),
                 QString("Totals: 10 passed, 0 failed, 1 measured, 0 skipped"));

        const CheckResult *stops = check(14);
        QVERIFY(stops);
        for (QString range : { QString("19.20 to 21.20 s"),
                               QString("1.00 to 4.20 s") }) {
            const QString words = number(*stops, "stopped, " + range);
            bool ok = false;
            const double past = words.section(' ', 0, 0).toDouble(&ok);
            QVERIFY2(ok && past >= 0.2 && past <= 0.45,
                     qPrintable(range + ": " + words));
        }
    }

    // The same with a round trip 20 ms too long: every take is spliced
    // from 20 ms too late, and lands 20 ms early. Items 1, 2, 7 and 13,
    // which ask where it landed, fail, and the report shows where the
    // sweeps landed
    void dev_checks_fail_with_the_round_trip_off() {
        makeWindow(loopback());

        runDevChecks(roundTrip / rate + 0.020, 0.0);
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

        // The re-recording and the punch-in near the start landed 20 ms
        // early as well, and fail for that alone
        for (int item : { 7, 13 }) {
            const CheckResult *c = check(item);
            QVERIFY2(c && c->verdict == CheckResult::Verdict::Fail,
                     describe());
            QVERIFY2(c->message.startsWith("the sweep at ") &&
                     c->message.contains(" landed -") &&
                     !c->message.contains(";"), describe());
            QVERIFY2(std::fabs(milliseconds(number(*c, "offsets")) + 20.0)
                     <= 1.0, describe());
        }

        // Items 4, 5, 12 and 14 do not depend on where the take is
        // placed.  The dots are 20 ms early too, which is about as far as
        // item 3 lets them be: whether they pass depends on where the
        // tracker's hops fall
        for (int item : { 4, 12, 14 }) {
            QVERIFY2(check(item) &&
                     check(item)->verdict == CheckResult::Verdict::Pass,
                     describe());
        }
        QVERIFY2(check(5) &&
                 check(5)->verdict == CheckResult::Verdict::Measured,
                 describe());
        const bool dotsPass =
            check(3) && check(3)->verdict == CheckResult::Verdict::Pass;

        // Without the long song, which this run leaves out; the joins as
        // in any run, placed alike
        QVERIFY2(check(9) &&
                 check(9)->verdict == CheckResult::Verdict::Skipped &&
                 check(9)->message == "The long song was left out of this "
                 "run.", describe());
        QVERIFY2(check(10) &&
                 check(10)->verdict == CheckResult::Verdict::Pass,
                 describe());
        QCOMPARE(lastReportLine(),
                 QString("Totals: %1 passed, %2 failed, 1 measured, 1 skipped")
                 .arg(dotsPass ? 5 : 4).arg(dotsPass ? 4 : 5));
    }

    // A device whose input moves 10 ms against its output each time its
    // stream starts, with the stream kept running between takes, as the
    // application keeps it on desktop: started once, at the run's first
    // take, so that every take shares one alignment, and every item
    // passes. Suspended at each Stop, as svapp does unless told
    // otherwise, the takes land 10 ms apart in turn, and items 1, 2, 7
    // and 13 fail
    void dev_checks_pass_with_the_stream_kept_running() {
        FakeAudioIO::Config config = loopbackInARoom();
        config.restartShift = restartShift;
        makeWindow(config);
        m_window->keepAudioRunning(true);

        runDevChecks(roundTrip / rate);
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_report.failure == "", describe());
        QCOMPARE(int(m_checks.size()), 5);
        QCOMPARE(int(m_report.checks.size()), 11);
        for (const CheckResult &c : m_report.checks) {
            // Item 5 is measured only: the loopback is on both inputs
            const CheckResult::Verdict expected = (c.item == 5 ?
                CheckResult::Verdict::Measured : CheckResult::Verdict::Pass);
            QVERIFY2(c.verdict == expected, describe(c.item));
        }
        QCOMPARE(lastReportLine(),
                 QString("Totals: 10 passed, 0 failed, 1 measured, 0 skipped"));
        QVERIFY(check(10));
        QCOMPARE(number(*check(10), "second punch-in against the first"),
                 QString("0.0 ms"));
        QCOMPARE(m_window->fake()->getResumeCount(), 1);
        QVERIFY(!m_window->fake()->isSuspended());
    }

    // The loopback heard a second time, 50 ms later at half the level, as
    // a mic hears an input that the system plays back out; and the input
    // on channel 2 only, as a mic on input 2 of an interface.  Item 4
    // fails on the second arrival; item 5 names input 2, and passes
    // because the live dots were drawn all the same
    void dev_checks_echo_and_the_mic_on_input_2() {
        FakeAudioIO::Config config = loopback();
        config.echoDelay = 2205;
        config.echoGain = 0.5f;
        config.inputChannel = 1;
        makeWindow(config);

        runDevChecks(roundTrip / rate, 0.0);
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_report.failure == "", describe());
        const CheckResult *speakers = check(4);
        const CheckResult *mic = check(5);
        QVERIFY(speakers && mic);
        QVERIFY2(speakers->verdict == CheckResult::Verdict::Fail, describe());
        QVERIFY2(speakers->message.contains("arrived a second time"),
                 describe());
        const QString echo = number(*speakers, "second arrival");
        const double delayMs = milliseconds(echo.section(" after", 0, 0));
        const double levelDb = echo.section(", ", 1, 1).chopped(3).toDouble();
        QVERIFY2(std::fabs(delayMs - 50.0) <= 1.0, describe());
        QVERIFY2(std::fabs(levelDb + 6.0) <= 1.0, describe());

        // Tony played nothing more than without the echo
        QVERIFY2(number(*speakers, "largest output level in the silent gaps")
                 .startsWith("silence, "), describe());

        QVERIFY2(mic->verdict == CheckResult::Verdict::Pass, describe());
        QVERIFY2(number(*mic, "the mic is on").startsWith("input 2, "),
                 describe());
        QVERIFY2(number(*mic, "input peaks, punch-in 1")
                 .startsWith("input 1 silence, input 2 -"), describe());
    }

    // The take heard during the re-recording's lead-in, as if Tony did
    // not keep it silent: its audio made audible as the runner reports
    // that punch-in recording, before the reference starts to play. The
    // lead-in plays over what stage 1 recorded, which in a room holds
    // noise where the reference is silent, and items 4 and 12 fail on
    // the output there; nothing before the punch-in changed. The window
    // is held up over the lead-in's last silent gap, as in
    // dev_checks_lead_in_through_a_stall(): the take is heard in the gap
    // before it all the same. The run is cancelled as the stage after
    // the re-recording begins: the checks of the stages it got through
    // are worked out all the same
    void dev_checks_take_heard_during_the_lead_in() {
        makeWindow(loopbackInARoom());
        stallTheReRecording();
        connect(m_window->audioCheck(), &AudioCheckRunner::progress,
                this, [this](const AudioCheckRunner::Progress &state) {
                    // Reported again as the seconds left go down
                    if (state.step != AudioCheckRunner::Step::Recording ||
                        m_faults > 0 || m_stages.isEmpty() ||
                        !m_stages.last().endsWith(": Re-record")) {
                        return;
                    }
                    Analyser *take = m_window->analyser2();
                    sv::Layer *audio =
                        take ? take->getLayer(Analyser::Audio) : nullptr;
                    auto params = audio ? audio->getPlayParameters() : nullptr;
                    if (params) {
                        params->setPlayAudible(true);
                        ++m_faults;
                    }
                });
        connect(m_window->devChecks(), &DevChecks::progress,
                this, [this](QString stage, int, int) {
                    if (stage == "Pre-roll near the start") {
                        m_window->devChecks()->cancel();
                    }
                });

        runDevChecks(roundTrip / rate, 0.0);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_faults, 1);
        QCOMPARE(m_stalls, 1);
        QCOMPARE(m_report.failure, QString("The dev checks were cancelled."));
        QCOMPARE(int(m_checks.size()), 2);

        const CheckResult *speakers = check(4);
        const CheckResult *leadIn = check(12);
        QVERIFY(speakers && leadIn);
        QVERIFY2(speakers->verdict == CheckResult::Verdict::Fail, describe(4));
        QVERIFY2(speakers->message.startsWith("Tony played something where "
                                              "the reference is silent: -"),
                 describe(4));
        QVERIFY2(speakers->message.contains(", in punch-in 3."), describe(4));
        QCOMPARE(number(*speakers, "second arrival"), QString("none heard"));

        QVERIFY2(leadIn->verdict == CheckResult::Verdict::Fail, describe(12));
        QVERIFY2(leadIn->message.startsWith("during the lead-in Tony played "
                                            "something where the reference "
                                            "is silent: -"), describe(12));
        QVERIFY2(!leadIn->message.contains(";"), describe(12));
        QCOMPARE(number(*leadIn, "audio before 19.20 s"),
                 QString("the same, bit for bit"));

        // Not reached: the save, and the punch-in near the start
        for (int item : { 1, 13 }) {
            QVERIFY2(check(item) &&
                     check(item)->verdict == CheckResult::Verdict::Skipped,
                     describe());
        }
    }

    // The window held up during the re-recording's lead-in, as a busy
    // system may hold it up: the observer takes no look meanwhile, and
    // the look across the stall reaches over the sounds either side, so
    // it is left out. Held up for 0.45 s over the silent gap just before
    // the punch-in, the lead-in still has the gap from 16.8 to 17.7 s
    // looked at, so item 12 judges what was played, and passes, as do
    // the re-recording's other checks. Held up over the whole lead-in,
    // no look lies in a gap, and that part of item 12 is not judged,
    // with the reason, rather than failed. Cancelled as the stage after
    // the re-recording begins
    void dev_checks_lead_in_through_a_stall_data() {
        QTest::addColumn<double>("at");
        QTest::addColumn<int>("ms");
        QTest::addColumn<bool>("judged");
        const double start = DevChecks::reRecording().start;
        QTest::newRow("over the last gap") << start - 0.45 << 450 << true;
        QTest::newRow("over the whole lead-in")
            << start - DevChecks::kReRecordPreRollSeconds
            << int(1000 * DevChecks::kReRecordPreRollSeconds) + 50 << false;
    }

    void dev_checks_lead_in_through_a_stall() {
        QFETCH(double, at);
        QFETCH(int, ms);
        QFETCH(bool, judged);
        makeWindow(loopbackInARoom());
        stallTheReRecording(at, ms);
        connect(m_window->devChecks(), &DevChecks::progress,
                this, [this](QString stage, int, int) {
                    if (stage == "Pre-roll near the start") {
                        m_window->devChecks()->cancel();
                    }
                });

        runDevChecks(roundTrip / rate, 0.0);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_report.failure, QString("The dev checks were cancelled."));
        QCOMPARE(int(m_checks.size()), 2);

        // Held up from where it was due until the punch-in, and the
        // observer saw it
        QCOMPARE(m_stalls, 1);
        const double start = DevChecks::reRecording().start;
        QVERIFY2(m_stalledFrom < at + 0.05 && m_stalledTo > start - 0.05,
                 qPrintable(QString("stalled from %1 to %2 s")
                            .arg(m_stalledFrom).arg(m_stalledTo)));
        const CheckResult *leadIn = check(12);
        QVERIFY(leadIn);
        QVERIFY2(milliseconds(number(*leadIn, "longest wait between two "
                                     "looks in the lead-in")) >= ms,
                 describe(12));

        QVERIFY2(leadIn->verdict == CheckResult::Verdict::Pass, describe(12));
        const QString gaps =
            number(*leadIn, "output in the lead-in's silent gaps");
        if (judged) {
            QVERIFY2(gaps.startsWith("silence, over ") &&
                     !gaps.endsWith(" 0 looks"), describe(12));
            QVERIFY2(!leadIn->message.contains("not judged"), describe(12));
        } else {
            QCOMPARE(gaps, QString("silence, over 0 looks"));
            QVERIFY2(leadIn->message.contains
                     ("What Tony played during the lead-in was not judged: "
                      "no look at the output lay wholly in one of the "
                      "reference's silent gaps"), describe(12));
            QVERIFY2(!leadIn->message.contains("Tony played nothing"),
                     describe(12));
        }
        for (int item : { 4, 7, 14 }) {
            QVERIFY2(check(item) &&
                     check(item)->verdict == CheckResult::Verdict::Pass,
                     describe(item));
        }
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
        startDevChecks(roundTrip / rate, 0.0);
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
        startDevChecks(roundTrip / rate, 0.0);
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
            (dialog->pageText().contains("Dev checks, stage 1 of 6"), 10000);
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
