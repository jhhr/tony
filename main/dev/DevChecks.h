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

    /// The item it settles, in the manual checklist's numbering from
    /// before default's rewrite of it (docs/calibrate-audio.md explains),
    /// and its name
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
 *  1. Long song: a run of the audio check on a long reference of its
 *     own (Options::longSeconds; replacing the calibration's session
 *     without asking), with the two punch-ins far apart of
 *     longPunchIns().  First, so that the next stage replaces this
 *     session as a check's own, unsaved, and no saved one is ever
 *     replaced.
 *  2. Fresh punch-ins: a run on the dev layout, in a reference of its
 *     own, with the two punch-ins of freshPunchIns() and the round trip
 *     given.
 *  3. Re-record: a run into the same session and take, with the one
 *     punch-in of reRecording(), over part of stage 2's second.
 *  4. Pre-roll near the start: the same with nearTheStart(), and a
 *     pre-roll longer than the song before it.
 *  5. Joins: the same with the two punch-ins of joinPunchIns(), which
 *     meet in the middle of a held tone.
 *  6. Save and reopen: the session saved into a scratch folder of this
 *     run, the way Save As saves once it has a name, then opened again
 *     and its take's file judged again.
 *
 * The checks are worked out when the run ends, from what the stages
 * kept: items 1 (latency, also after save and reopen) and 2 (several
 * phrases in one take) over every punch-in of the run; from what a
 * TakeObserver saw of each of stage 2's punch-ins, items 3 (live dots,
 * and how far behind the cursor they appear) and 5 (the mic on input
 * 2); from what it saw of every punch-in, item 4 (nothing of the take
 * in the speakers); from stage 1, item 9 (Stop on a long song); from
 * the take before and after stage 3, items 7 (record from a position)
 * and 12 (the lead-in); from stage 4, item 13 (pre-roll near the
 * start); from stages 3 and 4, item 14 (Record into Selection stops by
 * itself); and from stage 5, item 10 (the joins).  A run that ends
 * early works out each check whose stages it got through, and has the
 * rest Skipped.
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

    /// Item 3: more live dots than this in every punch-in, each where
    /// TakeDiff::placeLiveDot() allows
    static constexpr int kMinDots = 10;

    /// Item 5: an input carries the mic when its peak is no more than
    /// this far below the loudest input's
    static constexpr double kMicChannelDb = 20.0;

    /// Stage 3's pre-roll, in seconds: a lead-in from 16.8 s, where
    /// stage 2's second punch-in begins, so that it plays over two of
    /// the reference's silent gaps in what that punch-in recorded (16.8
    /// to 17.7 s and 18.8 to 19.2 s).  Item 12 looks at the output in
    /// both: a window held up over one still has the other to be judged
    /// in
    static constexpr double kReRecordPreRollSeconds = 2.4;

    /// Stage 4's pre-roll, in seconds: more than there is room for
    /// before nearTheStart()
    static constexpr double kNearStartPreRollSeconds = 3.0;

    /// Item 14: how far past the end of its selection a take may record
    /// before it stops itself, besides a look of the window's take timer
    /// and a block of the device, as the checklist asks
    static constexpr double kStopMarginSeconds = 0.25;

    /// Item 9: each punch-in into the long song has its pitch merged
    /// within this share of the time the whole song's analysis took
    static constexpr double kStopShare = 0.5;

    /// How often a stage is looked at
    static constexpr int kPollMs = 50;

    /// How long a stage may take.  A stage that runs the audio check
    /// waits for it, and the runner has limits of its own for each
    /// step, which end its run with a reason: this is a backstop behind
    /// them.  The runner's for the reference's analysis and for those of
    /// two punch-ins, the most a stage records, and two minutes for the
    /// rest (writing and opening the reference, recording, stopping):
    /// 4 minutes on a desktop, 10 on a phone.  Save and reopen is quick
    /// on a desktop; on a phone it may take as many times longer as an
    /// analysis may
    static constexpr int kCheckStageTimeoutMs =
        AudioCheckRunner::kReferenceTimeoutMs +
        2 * AudioCheckRunner::kTakeAnalysisTimeoutMs + 120000;
    static constexpr int kReopenTimeoutMs =
        60000 * AudioCheckRunner::kAnalysisTimeFactor;

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

        /// How long the long song of stage 1 is, in seconds; 0 leaves
        /// the stage out, and item 9 is Skipped (for tests that look at
        /// the other stages, which it would only make longer)
        double longSeconds;

        Options() : roundTrip(-1.0),
                    longSeconds(LatencyCheck::kLongSeconds) { }
    };

    DevChecks(MainWindow *window, AudioCheckRunner *runner);
    virtual ~DevChecks();

    /**
     * Stage 1's punch-ins into a long layout, in seconds: each the
     * shortest range that judges one sweep, the first sweep from a
     * quarter of the way into the song, and the first from five eighths
     * (near 60 and 150 s of 4 minutes), so that each is far from the
     * start and from the other.  Empty if the layout is too short.
     */
    static std::vector<LatencyCheck::PunchIn>
    longPunchIns(const LatencyCheck::Layout &layout);

    /**
     * Stage 2's punch-ins on the dev layout, in seconds: [6.3, 10.2]
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
     * Stage 5's punch-ins, in seconds: [26.0, 28.7] and [28.7, 32.0],
     * meeting at 28.7 s, the middle of the held tone from 27.2 to 30.2
     * s, which reaches 1.5 s either side of the join: further than
     * TakeDiff looks at the pitch and notes around it.  The first
     * judges that tone's sweep at 26.9 s, and the second the next
     * sweep, at 30.9 s; the second's 1 s lead-in plays over what the
     * first recorded.  The shortest pair that does that, and clear of
     * every other stage's range.
     */
    static std::vector<LatencyCheck::PunchIn> joinPunchIns();

    /**
     * Stage 3's punch-in, in seconds: [19.2, 21.2], over the end of
     * stage 2's second and past its first sweep, judging the sweep at
     * 20.1 s.  Its lead-in (kReRecordPreRollSeconds) plays over what
     * stage 2 recorded from its start: a gap where the reference is
     * silent and the take holds only what the mic heard besides (16.8
     * to 17.7 s), the sweep at 17.7 s, the tone from 18 s, and another
     * such gap from 18.8 s.  Starting this late keeps the range short
     * and leaves a whole note of the take (18 to 18.8 s) before it for
     * the ranged analysis to leave alone.
     */
    static LatencyCheck::PunchIn reRecording();

    /**
     * Stage 4's punch-in, in seconds: [1.0, 4.2], judging the sweep at
     * 3.1 s, recorded with a pre-roll of kNearStartPreRollSeconds.
     */
    static LatencyCheck::PunchIn nearTheStart();

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

    /// Stage 1, on a long song of its own: its layout, whether it got
    /// through, what the runner found, the take's coverage straight
    /// after, and what was seen of each punch-in.  The runner's steps
    /// timed: the whole song's analysis, from the session open to the
    /// analysis done (-1 if it was not seen through), and each
    /// punch-in's, from the take stopped to its pitch merged, in the
    /// order recorded; and the take's pitch as each punch-in began to
    /// record, and after the last.  While the stage runs, the step and
    /// punch-in the runner last reported, and when that began
    struct LongSong {
        LatencyCheck::Layout layout;
        bool done;
        AudioCheckResult result;
        Coverage coverage;
        std::vector<Watched> watched;
        double wholeSongSeconds;
        std::vector<double> stopSeconds;
        std::vector<sv::EventVector> pitch;
        bool timing;
        QElapsedTimer clock;
        AudioCheckRunner::Step step;
        int punchIn;
        qint64 stepFrom;
        LongSong() : done(false), wholeSongSeconds(-1.0), timing(false),
                     step(AudioCheckRunner::Step::Idle), punchIn(0),
                     stepFrom(0) { }
    };
    LongSong m_long;

    /// Stage 2: the dev layout, which later stages record into too, what
    /// the runner found, the take's coverage straight after, and what was
    /// seen of each punch-in
    LatencyCheck::Layout m_layout;
    bool m_haveFresh;
    AudioCheckResult m_fresh;
    Coverage m_coverageAfterFresh;
    std::vector<Watched> m_freshWatched;

    /// The take as it was at one moment: its audio as its file holds it,
    /// mixed to one channel (AudioCheckRunner::readTakeFile()), or why
    /// it could not be read; its pitch and notes; and its coverage
    struct Snapshot {
        std::vector<float> audio;
        QString audioError;
        sv::EventVector pitch;
        sv::EventVector notes;
        Coverage coverage;
    };

    /// Stages 3, 4 and 5, each a run of the runner's into the take there
    /// is: whether it got through, what the runner found, the take
    /// before and after, what was seen of the punch-ins, the lead-in the
    /// window gave the last, and how many frames the first one's raw
    /// recording holds (-1, and why, if that could not be read)
    struct PunchInStage {
        bool done;
        AudioCheckResult result;
        Snapshot before;
        Snapshot after;
        std::vector<Watched> watched;
        sv::sv_frame_t preRoll;
        sv::sv_frame_t recorded;
        QString recordedError;
        PunchInStage() : done(false), preRoll(0), recorded(-1) { }
    };
    PunchInStage m_reRecord;
    PunchInStage m_nearStart;
    PunchInStage m_joins;

    /// Stage 6: the take's pitch and notes before the save and after
    /// the reopen, and its file judged, over every punch-in into it,
    /// just before the save and again after the reopen
    sv::EventVector m_pitchBefore;
    sv::EventVector m_notesBefore;
    sv::EventVector m_pitchAfter;
    sv::EventVector m_notesAfter;
    bool m_reopened;
    LatencyCheck::TakeSummary m_beforeSave;
    LatencyCheck::TakeSummary m_rejudged;

    /// One of the runs of the runner's that the run got through, in
    /// order: the layout of the reference it recorded against, what it
    /// found, the take's coverage after it, and what was seen of its
    /// punch-ins
    struct Run {
        const LatencyCheck::Layout *layout;
        const AudioCheckResult *result;
        const Coverage *coverage;
        const std::vector<Watched> *watched;
    };
    std::vector<Run> runs() const;

    /// What was seen of a run's punch-in i, counting from 0, or null
    static const Watched *watchedOf(const Run &run, int i);

    /// What the output levels read at a punch-in's looks say of the
    /// reference's silent gaps (speakersCheck()).  Looks that reach past
    /// "until", in seconds on the reference's timeline, are left out
    struct GapLooks {
        /// The start gap was measured, so that the looks could be placed
        bool placed;

        /// How far either side of a look what it read may lie, seconds
        double margin;

        /// The loudest level read at any look, and at any look lying
        /// wholly in a gap, full scale 1; and how many looks did
        double loudest;
        double loudestInGaps;
        int looks;

        /// The first look in a gap that read anything: its level (0 if
        /// none did) and where it lay, in seconds
        double heard;
        double heardFrom;
        double heardTo;

        /// The longest wait between two looks while recording, in
        /// seconds, of those beginning before "until": the window held
        /// up.  The frames received jump meanwhile, so the look across
        /// the wait reaches over a sound, and is left out
        double longestWait;

        GapLooks() : placed(false), margin(0), loudest(0), loudestInGaps(0),
                     looks(0), heard(0), heardFrom(0), heardTo(0),
                     longestWait(0) { }
    };
    GapLooks gapLooks(const LatencyCheck::Layout &layout,
                      const TakeObserver::Observation &seen,
                      const TakeLatency &latency, sv::sv_samplerate_t rate,
                      double until) const;

    void poll();
    void runnerFinished(const AudioCheckResult &result);
    void runnerProgress(const AudioCheckRunner::Progress &state);

    /// Stop the observer and keep what it saw
    void finishObservation();

    /// Stage 1: the runner has reported a step and punch-in (Idle and 0
    /// when it finished); a step it had reported before ends
    void longSongStep(AudioCheckRunner::Step step, int punchIn);

    /// What was seen of stage 2's punch-in i, counting from 0, or null
    const Watched *freshWatched(int i) const;

    void beginLongSong();
    bool longSongDone();
    void beginFreshPunchIns();
    bool freshPunchInsDone();
    void beginPunchInStage(PunchInStage &stage,
                           std::vector<LatencyCheck::PunchIn> ranges,
                           double preRoll);
    bool punchInStageDone(PunchInStage &stage);
    void beginReopen();
    bool reopenDone();

    /// The take as it is now, its models looked up afresh
    Snapshot snapshot() const;

    /// Every punch-in into the dev reference's take so far, in the order
    /// recorded
    std::vector<LatencyCheck::PunchIn> punchInsSoFar() const;

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
    CheckResult positionCheck(QString reason) const;
    CheckResult longSongCheck(QString reason) const;
    CheckResult joinsCheck(QString reason) const;
    CheckResult leadInCheck(QString reason) const;
    CheckResult nearStartCheck(QString reason) const;
    CheckResult stopsItselfCheck(QString reason) const;

    /// The report file written, or "" if it could not be
    QString writeReport(const DevReport &report) const;

    static QString reportDirectory(QString given);
    static QString scratchDirectory(QString given);
};

#endif
#endif
