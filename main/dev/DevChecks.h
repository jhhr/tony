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

#ifndef TONY_DEV_CHECKS_H
#define TONY_DEV_CHECKS_H

#ifdef TONY_DEV_CHECKS

#include "../Analyser.h"
#include "../AudioCheckRunner.h"
#include "../Coverage.h"
#include "../LatencyCalibration.h"
#include "../LatencyCheck.h"
#include "TakeObserver.h"

#include "base/Event.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <utility>
#include <vector>

class MainWindow;
class QTimer;

/**
 * What one development check found.  Not a QtTest function: the app
 * suite has to show that each check can fail, which a QVERIFY inside
 * another test cannot.
 */
struct CheckResult
{
    enum class Verdict { Pass, Fail, Measured, Skipped };

    /// The item of docs/manual-checklist.md it settles, and its name
    int item;
    QString name;

    Verdict verdict;

    /// The figures behind the verdict, as label and value, in words
    std::vector<std::pair<QString, QString>> numbers;

    /// What the verdict means, or why there is none
    QString message;

    /// "Pass", "Fail", "Measured" or "Skipped"
    static QString verdictName(Verdict verdict);

    CheckResult() : item(0), verdict(Verdict::Skipped) { }
};

/**
 * What a run of the development checks found, however it ended.
 */
struct DevReport
{
    /// Every check, in the order of the checklist's items.  A run that
    /// ended early has those it did not reach Skipped, with the reason
    std::vector<CheckResult> checks;

    /// Why the run ended before its last stage was done; empty if not
    QString failure;

    /// The report file, and the session the run saved in its scratch
    /// folder; empty if they were not written
    QString reportPath;
    QString sessionPath;

    int count(CheckResult::Verdict verdict) const;
};

/**
 * The development checks (development builds only): every item of the
 * manual checklist that a speaker-to-microphone loopback can settle,
 * run on the live window through the audio check's ordinary takes,
 * after a calibration, with the round trip it measured.
 *
 * A run is a list of stages, each of which starts something and then
 * says, when polled, whether it is done.  Driven by a polling timer
 * and the runner's finished(), as the runner itself is, and never by a
 * nested event loop: the window can be closed, and the session
 * replaced, at any moment, and a run ends cleanly then.
 *
 * The stages:
 *  1. Fresh punch-ins: a run of the audio check on the dev layout, in
 *     a reference of its own (replacing the calibration's session
 *     without asking), with the two punch-ins of freshPunchIns() and
 *     the round trip given.
 *  2. Save and reopen: the session saved into a scratch folder of this
 *     run, the way Save As saves once it has a name, then opened again
 *     and its take's file judged again.
 *
 * The checks are worked out when the run ends, from what the stages
 * kept: item 1 (latency, also after save and reopen), item 2 (several
 * phrases in one take), and from what a TakeObserver saw of each of
 * stage 1's punch-ins, items 3 (live dots, and how far behind the
 * cursor they appear), 4 (nothing of the take in the speakers) and 5
 * (the mic on input 2).
 *
 * The session saved stays open afterwards, so that the takes can be
 * looked at; its scratch folder stays with it, and the next run
 * removes every such folder the session open then does not use.  The
 * report goes to a text file, DevChecks.txt, ending with a Totals:
 * line like the suites'.
 *
 * MainWindow owns it, and deletes it after the Calibrate Audio dialog
 * and before the runner.  A friend of MainWindow: it saves and opens
 * sessions, and reads the take's coverage, file and events.
 */
class DevChecks : public QObject
{
    Q_OBJECT

public:
    /// Items 1 and 2: how far from where the reference has it a sweep
    /// may land, either way
    static constexpr double kPlacementSeconds = 0.002;

    /// Item 3: more live dots than this in every punch-in, as
    /// test-tony-device asked
    static constexpr int kMinDots = 10;

    /// Item 3: a dot is on one of the reference's sounds when it lies
    /// from the sound's start to half the live tracker's window past
    /// its end, give or take this many of its hops (liveDotsCheck()),
    /// and on a tone, within this many cents of its pitch
    static constexpr int kDotHops = 1;
    static constexpr double kDotCents = 50.0;

    /// Item 5: an input carries the mic when its peak is no more than
    /// this far below the loudest input's
    static constexpr double kMicChannelDb = 20.0;

    /// How often a stage is looked at
    static constexpr int kPollMs = 50;

    /// How long a stage may take.  A stage that runs the audio check
    /// waits for it, and the runner has limits of its own for each
    /// step, which end its run with a reason: this is a backstop.
    /// Save and reopen is quick
    static constexpr int kCheckStageTimeoutMs = 240000;
    static constexpr int kReopenTimeoutMs = 60000;

    /// The report's file name, in the report directory
    static constexpr const char *kReportFileName = "DevChecks.txt";

    /// The name of the session saved in the scratch folder
    static constexpr const char *kSessionFileName = "dev-checks.ton";

    struct Options {
        /// The round trip the run's takes are placed with, in seconds,
        /// for the run only (AudioCheckRunner::Plan::roundTrip);
        /// negative for the one the window uses
        double roundTrip;

        /// Where the report goes: "" for $TONY_TEST_LOG_DIR if that is
        /// set, else the application's data directory
        QString reportDirectory;

        /// Where the scratch folders go: "" for the application's data
        /// directory
        QString scratchDirectory;

        Options() : roundTrip(-1.0) { }
    };

    DevChecks(MainWindow *window, AudioCheckRunner *runner);
    virtual ~DevChecks();

    /**
     * Stage 1's punch-ins on the dev layout, in seconds: [6.3, 10.2]
     * and [16.8, 21.2], each judging two sweeps (7.2 and 9.1 s; 17.7
     * and 20.1 s) with 50 ms to spare.  In two separate regions of the
     * calibration part, clear of what later stages need: the start
     * (before 4.3 s) for a punch-in at 1 s whose range judges the sweep
     * at 3.1 s; the held tones from 26.9 s on; 10.2 to 16.8 s and 21.2
     * to 25 s for fresh punch-ins; and each range holds two sweeps, so
     * that a re-recording can start inside it, past its first sweep,
     * with its lead-in over what was recorded there, and still judge
     * the second.
     */
    static std::vector<LatencyCheck::PunchIn> freshPunchIns();

    /**
     * A new scratch folder in the directory, made: dev-checks-1,
     * dev-checks-2 and so on, the lowest number free.  Every such
     * folder in the directory is removed first, with all it holds,
     * except the one the session file inUse is in: that session, open
     * now, was saved there by an earlier run.  "" if none could be
     * made.
     */
    static QString nextScratchFolder(QString directory, QString inUse);

    /**
     * Begin a run.  False, with nothing started, if one is running
     * already, if the audio check is, if a take is being recorded, or
     * if no scratch folder could be made.  Otherwise finished() comes
     * once, at the end, however the run ends.
     */
    bool start(const Options &options);

    /// End the run: its take is stopped through the Stop path, as the
    /// audio check's Cancel does, and finished() says why
    void cancel();

    bool isRunning() const;

    /// Called by MainWindow::closeSession(), after the runner has been
    /// told: a run ends then, unless it is replacing the session itself
    /// (its reopen, or its runner opening the dev reference)
    void sessionClosing();

signals:
    /// When a stage begins: its name, and which of how many.  Never
    /// from inside start() or cancel()
    void progress(QString stage, int stageNumber, int stages);

    void finished(const DevReport &report);

private:
    struct Stage {
        QString name;
        std::function<void()> begin;
        std::function<bool()> done;
        int limitMs;
    };

    MainWindow *m_window;
    QPointer<AudioCheckRunner> m_runner;
    QTimer *m_timer;

    Options m_options;
    std::vector<Stage> m_stages;

    /// The stage going on, or -1; whether it has begun, and since when
    int m_stage;
    bool m_begun;
    QElapsedTimer m_stageClock;

    /// Set while poll() runs: a dialog shown from inside a stage runs
    /// an event loop of its own, in which the timer goes on firing
    bool m_inPoll;

    /// A run of the runner's that this run started is going on; and
    /// what it said when it ended
    bool m_runnerRunning;
    AudioCheckResult m_runnerResult;

    /// Set while the run opens the session it saved
    bool m_reopening;

    /// What the run was started with, for the report
    QDateTime m_startedAt;
    LatencyCalibration::Key m_devices;
    QString m_scratchFolder;
    QString m_sessionPath;
    bool m_saved;

    /// What the observer saw of one of the runner's punch-ins, and the
    /// peak of each channel of its raw recording, full scale 1, or why
    /// that could not be read
    struct Watched {
        int punchIn;    ///< counting from 0
        TakeObserver::Observation seen;
        std::vector<float> channelPeaks;
        QString channelError;
        Watched() : punchIn(0) { }
    };

    /// Watches each punch-in of a run of the runner's that this run
    /// started, from its Recording step until its analysis is done;
    /// which punch-in, counting from 1, or 0; and what it saw
    TakeObserver *m_observer;
    int m_observedPunchIn;
    std::vector<Watched> m_watched;

    /// Stage 1: the layout, what the runner found, the take's coverage
    /// straight after, and what was seen of each punch-in
    LatencyCheck::Layout m_layout;
    bool m_haveFresh;
    AudioCheckResult m_fresh;
    Coverage m_coverageAfterFresh;
    std::vector<Watched> m_freshWatched;

    /// Stage 2: the take's pitch and notes before the save and after
    /// the reopen, and its file judged again after it
    sv::EventVector m_pitchBefore;
    sv::EventVector m_notesBefore;
    sv::EventVector m_pitchAfter;
    sv::EventVector m_notesAfter;
    bool m_reopened;
    LatencyCheck::TakeSummary m_rejudged;

    void poll();
    void runnerFinished(const AudioCheckResult &result);
    void runnerProgress(const AudioCheckRunner::Progress &state);

    /// Stop the observer and keep what it saw
    void finishObservation();

    /// What was seen of stage 1's punch-in i, counting from 0, or null
    const Watched *freshWatched(int i) const;

    void beginFreshPunchIns();
    bool freshPunchInsDone();
    void beginReopen();
    bool reopenDone();

    /// The events of the take's pitch track or notes, from its model
    /// as it is now
    sv::EventVector takeEvents(Analyser::Component component) const;

    /// End the run, work the checks out, write the report and say so;
    /// failure is empty for a run that got through every stage
    void end(QString failure);

    std::vector<CheckResult> evaluate(QString reason) const;
    CheckResult latencyCheck(QString reason) const;
    CheckResult phrasesCheck(QString reason) const;
    CheckResult liveDotsCheck(QString reason) const;
    CheckResult speakersCheck(QString reason) const;
    CheckResult micChannelCheck(QString reason) const;

    /// The report file written, or "" if it could not be
    QString writeReport(const DevReport &report) const;

    static QString reportDirectory(QString given);
    static QString scratchDirectory(QString given);
};

#endif
#endif
