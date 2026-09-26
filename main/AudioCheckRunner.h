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

#ifndef TONY_AUDIO_CHECK_RUNNER_H
#define TONY_AUDIO_CHECK_RUNNER_H

#include "LatencyCalibration.h"
#include "LatencyCheck.h"
#include "LatencyUtils.h"

#include "base/BaseTypes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <vector>

class MainWindow;
class Analyser;
class QTimer;

/**
 * What an audio check found.  Times are in seconds.
 */
struct AudioCheckResult
{
    /// Why the run ended before it judged the take; empty if it did.
    /// The rest is filled in only as far as the run got
    QString failure;

    LatencyCheck::TakeSummary summary;

    /// What each punch-in was placed with, in the order recorded
    std::vector<TakeLatency> takes;

    /// The round trip the first punch-in was placed with, and the two
    /// latencies the device reported then, as the take path had them
    /// (see TakeLatency)
    double usedRoundTrip;
    double reportedOutputLatency;
    double reportedInputLatency;

    /// The rate the device recorded at, and the session's, which the
    /// reference was made at.  They may differ: a recording is converted
    /// to the reference's rate before it is spliced, and the round trip
    /// is counted in seconds, so a check at another rate is judged, and
    /// its figure kept, like any other
    sv::sv_samplerate_t recordingRate;
    sv::sv_samplerate_t referenceRate;

    /// The round trip that would have placed the takes right
    /// (LatencyCheck::calibratedRoundTrip()); see calibrationUsable()
    double calibratedRoundTrip;

    /// What the figure is kept under (MainWindow::storeMeasuredLatency()):
    /// the devices as the Preferences named them when the run started,
    /// and the rate the takes were recorded at.  Not the devices named
    /// when the figure is kept: the result is on show for as long as the
    /// user likes, and another device may have been chosen by then
    LatencyCalibration::Key key;

    /// Whether calibratedRoundTrip means anything: the run was judged
    /// Ok or Unsteady
    bool calibrationUsable() const;

    AudioCheckResult() : usedRoundTrip(0), reportedOutputLatency(0),
                         reportedInputLatency(0), recordingRate(0),
                         referenceRate(0), calibratedRoundTrip(0) { }
};

/**
 * The audio check: a test reference, written out and opened as a
 * session of its own, then punch-ins recorded against it through the
 * ordinary take path, and the take that comes out of them judged by
 * LatencyCheck::judgeTake().  Every punch-in is a take as the user's
 * are (on desktop the stream kept running between them, on Android
 * started again for each), and is placed with the latency the window
 * uses for any take, which is what is being measured.
 *
 * The takes are recorded with Record into Selection, Play Reference
 * While Recording and the plan's pre-roll (kPreRollSeconds unless it
 * says otherwise), whatever the toolbar says: MainWindow::record() and
 * the rest consult an override the runner sets for each of its takes,
 * since the toolbar's toggles write the user's settings.  A punch-in's
 * range is made the selection
 * (the previous one cleared, then this one selected: "Select" steps in
 * the history, as when the user selects), and record() is called; the
 * take stops itself at the end of the selection, through the same path
 * as the Stop button.
 *
 * The check's session plays the reference centred and at the level it
 * was made at, LatencyCheck::kPeakDbfs, and leaves the pitch and notes
 * sonification silent: an earcup is held to the microphone, and Tony
 * otherwise plays the reference in the left channel only, normalised
 * to full scale, with the sonification in the right.  This is set on
 * the play parameters of the session's own models, never through
 * Analyser::setAudible() and the like, which write the settings every
 * session reads.  It stays so after the run, as the session does, and
 * a session opened afterwards plays as before.
 *
 * Driven by a polling timer, like MainWindow's own take polling, and
 * never by a nested event loop: this runs in every build, and the
 * window can be closed at any moment.  MainWindow owns it, deletes it
 * in its destructor after those who drive it (the dialog, the dev
 * checks) and before anything it reads, and tells it when the session
 * closes.
 * A friend of MainWindow: it drives the window's take path, reads what
 * the take was placed with, and sets the playback of the session it
 * opened, but changes nothing else there.
 */
class AudioCheckRunner : public QObject
{
    Q_OBJECT

public:
    /// The lead-in of the check's takes, unless the plan asks for another
    static constexpr double kPreRollSeconds = 1.0;

    /// How often the runner looks at how a step is going
    static constexpr int kPollMs = 50;

    /// How long a step may take before the run gives up on it: the
    /// first analysis of the reference, a take's analysis, and a take
    /// beyond its own length (lead-in and range) to stop itself
    static constexpr int kReferenceTimeoutMs = 60000;
    static constexpr int kTakeAnalysisTimeoutMs = 30000;
    static constexpr int kTakeStopTimeoutMs = 10000;

    /// How long past its own length (lead-in and range) a take may go
    /// without a single frame from the device before the run says the
    /// device delivered no input.  Not sooner: a stream that is only slow
    /// to start (a Bluetooth headset switching to its microphone, say)
    /// delivers its first block within a second or two, and a device
    /// that has sent nothing for the whole length of the take plus this
    /// has recorded none of it anyway
    static constexpr int kNoInputTimeoutMs = 2000;

    /// What a run records
    struct Plan {
        LatencyCheck::Layout layout;
        int punchIns;
        int eventsEach;

        /// The punch-ins themselves, in seconds on the reference's
        /// timeline, in the order they are recorded.  When there are
        /// any, they are what is recorded, and punchIns and eventsEach
        /// are not used.  They may meet but not overlap, and lie within
        /// the layout (punchInsOf())
        std::vector<LatencyCheck::PunchIn> ranges;

        /// Where the reference is written, over whatever is there; ""
        /// for a new file in referenceDirectory() (nextReferencePath())
        QString referencePath;

        /// Record into the session open now, which the caller says is
        /// a reference made from this plan's layout, and into its take:
        /// no reference is written or opened, and nothing is asked.
        /// What earlier runs recorded stays in the take, and only this
        /// run's punch-ins are judged
        bool keepSession;

        /// The round trip this run's takes are placed with, in seconds,
        /// in place of the one the window places every take with;
        /// negative for the window's own.  For the run only: nothing is
        /// stored, and the window goes on saying it uses its own
        double roundTrip;

        /// The pre-roll this run's takes ask for, in seconds.  As the
        /// user's does, it gets shorter near the start of the song
        /// (TakeTiming::preRollBefore())
        double preRoll;

        Plan() : punchIns(0), eventsEach(0), keepSession(false),
                 roundTrip(-1.0), preRoll(kPreRollSeconds) { }
    };

    /// The steps of a run, in order; the last two come once for each
    /// punch-in
    enum class Step {
        Idle,
        OpeningReference,
        AnalysingReference,
        Recording,
        AnalysingTake
    };

    /// How far a run has got
    struct Progress {
        Step step;

        /// Punch-in punchIn of punchIns: the one being recorded, or
        /// whose take is being analysed, counting from 1; 0 before the
        /// first
        int punchIn;
        int punchIns;

        /// Seconds of recording still to come: the rest of the take
        /// being recorded, and the lead-in and range of each one after
        /// it.  The waits for the analyses between them are not in it:
        /// their length is not known
        double secondsLeft;

        Progress() : step(Step::Idle), punchIn(0), punchIns(0),
                     secondsLeft(0) { }
    };

    explicit AudioCheckRunner(MainWindow *window);
    virtual ~AudioCheckRunner();

    /// The plan of Playback > Calibrate Audio: four punch-ins of three
    /// events each on the calibration layout
    static Plan calibrationPlan();

    /// The punch-ins a run of the plan records, in seconds: its ranges
    /// if it has any, else as many as it asks for from its layout
    /// (LatencyCheck::punchInsFor()).  Empty if they cannot be
    /// recorded: ranges that overlap, come out of order, are empty or
    /// reach outside the layout, or a layout with no room for the
    /// punch-ins asked for
    static std::vector<LatencyCheck::PunchIn> punchInsOf(const Plan &plan);

    /// Where the reference is written unless the plan names a file:
    /// the application's data directory
    static QString referenceDirectory();

    /**
     * A file in the directory to write the next reference to, never
     * the one inUse names: that is the session open now, perhaps the
     * check before, and on Windows a file that is open cannot be
     * written over.  The references in the directory that inUse does
     * not name are removed first, as no session holds them; and the
     * lowest free number is taken, so the names go 1, 2, 1, 2 and
     * Recent Files, where each one opened is listed, gets two at most.
     */
    static QString nextReferencePath(QString directory, QString inUse);

    /**
     * Begin a run.  False, with nothing started, if one is running
     * already, if a take is being recorded, if the plan's punch-ins
     * cannot be recorded (punchInsOf()), if its pre-roll is negative, or
     * if it keeps the session and there is none.  Otherwise finished()
     * comes once, at the end,
     * however the run ends.
     *
     * A run that replaces the session asks the user whether to save it
     * first (MainWindow::checkSaveModified()), unless it is a check's
     * own: never saved, and playing a reference in referenceDirectory().
     * Nothing of the user's is in that, and Check Again would otherwise
     * ask every time, the takes having changed it.
     */
    bool start(const Plan &plan);

    /// End the run: a take in progress is stopped through the Stop
    /// path, and finished() says the run was cancelled
    void cancel();

    /// End the run at once, with no finished() and no Stop path: for
    /// the window's teardown, where whoever waits for the run goes as
    /// well and a take in progress is left to the window, as any take
    /// is when it is deleted.  The destructor does this
    void abandon();

    bool isRunning() const;

    /// Called by MainWindow::closeSession() once the session is sure to
    /// close: a run ends then, unless it is the run replacing it
    void sessionClosing();

    /// Whether the analyser, or any transform, is still at work: what
    /// a run waits for after opening the reference and after each take
    static bool analysing(Analyser *analyser);

    /**
     * A take's audio file, mixed to one channel, at the file's rate
     * (the reference's: a recording at another rate is converted before
     * it is spliced).  The file, and not the take's model: the model is
     * normalised to full scale as it is read (the "normalise audio"
     * preference), which would have every take clipped, and resampled
     * to the session's rate.  "" on success, else what went wrong.
     */
    static QString readTakeFile(QString path, std::vector<float> &mono,
                                sv::sv_samplerate_t &rate);

signals:
    void finished(const AudioCheckResult &result);

    /// When a step begins, and each time the whole seconds left go
    /// down while a take is recorded.  Never from inside start() or
    /// cancel()
    void progress(const AudioCheckRunner::Progress &state);

private:
    MainWindow *m_window;
    QTimer *m_timer;
    Step m_step;
    Plan m_plan;
    AudioCheckResult m_result;

    /// The last progress reported
    Progress m_reported;

    /// The punch-ins in seconds, from the plan, until the reference is
    /// open; from then on as the takes record them, in whole frames of
    /// the session
    std::vector<LatencyCheck::PunchIn> m_punchIns;
    std::vector<sv::sv_frame_t> m_starts;
    std::vector<sv::sv_frame_t> m_ends;
    int m_punchIn;

    /// Set while the runner replaces the session itself
    bool m_openingReference;

    /// Set while poll() runs: a dialog shown from inside a step runs
    /// an event loop of its own, in which the timer goes on firing
    bool m_inPoll;

    QElapsedTimer m_stepClock;
    qint64 m_stepLimitMs;

    /// The length of the take being recorded, lead-in and range, in ms
    qint64 m_takeMs;

    /// The take has gone kNoInputTimeoutMs past its length with not one
    /// frame from the device
    bool deliveredNothing() const;

    void poll();
    void openReference();

    /// The session open now is a check's own (see start()), and can be
    /// replaced without asking
    bool sessionIsACheck() const;

    /// The audio file the session's main model was opened from, or ""
    QString mainModelFile() const;

    /// The session is open with the reference in it: the punch-ins in
    /// frames of the session, its playback, and on to its analysis
    void referenceOpen();

    void startPunchIn();
    void takeStopped();
    void judge();

    /// The check session's playback, set on the play parameters of the
    /// reference and of its pitch and notes: see the class comment
    void setPlayback();

    /// The gain that brings the reference, as the session's model
    /// has it, down to the level it was made at
    static double referenceGain();

    void setStep(Step step, qint64 limitMs);
    bool stepTimedOut() const;

    Progress currentProgress() const;

    /// Emit progress() if the step, the punch-in or the whole seconds
    /// left have changed since it was last emitted
    void reportProgress();

    /// Stop a take the check is recording, through the Stop path
    void stopTake();

    /// End the run and say so; failure is empty for a run judged
    void end(QString failure);

    /// Take the override for the check's takes away from the window
    void clearOverride();
};

#endif
