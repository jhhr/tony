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
    /// reference was made at
    sv::sv_samplerate_t recordingRate;
    sv::sv_samplerate_t referenceRate;

    /// The two rates differ.  Set from the rates, whatever the sweeps
    /// say: a take recorded at another rate is placed frame for frame
    /// (a known bug), so it lands further off the further into the
    /// reference it is, soon further than the finder looks
    bool rateMismatch;

    /// The round trip that would have placed the takes right
    /// (LatencyCheck::calibratedRoundTrip()); see calibrationUsable()
    double calibratedRoundTrip;

    /// Whether calibratedRoundTrip means anything: the run was judged
    /// Ok or Unsteady, at the reference's rate
    bool calibrationUsable() const;

    AudioCheckResult() : usedRoundTrip(0), reportedOutputLatency(0),
                         reportedInputLatency(0), recordingRate(0),
                         referenceRate(0), rateMismatch(false),
                         calibratedRoundTrip(0) { }
};

/**
 * The audio check: a test reference, written out and opened as a
 * session of its own, then punch-ins recorded against it through the
 * ordinary take path, and the take that comes out of them judged by
 * LatencyCheck::judgeTake().  Every punch-in restarts the stream, as a
 * real take does, and is placed with the latency the window uses for
 * any take, which is what is being measured.
 *
 * The takes are recorded with Record into Selection, Play Reference
 * While Recording and a pre-roll of kPreRollSeconds, whatever the
 * toolbar says: MainWindow::record() and the rest consult an override
 * the runner sets for each of its takes, since the toolbar's toggles
 * write the user's settings.  A punch-in's range is made the selection
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
 * first thing in its destructor, and tells it when the session closes.
 * A friend of MainWindow: it drives the window's take path, reads what
 * the take was placed with, and sets the playback of the session it
 * opened, but changes nothing else there.
 */
class AudioCheckRunner : public QObject
{
    Q_OBJECT

public:
    /// The lead-in of the check's takes
    static constexpr double kPreRollSeconds = 1.0;

    /// How often the runner looks at how a step is going
    static constexpr int kPollMs = 50;

    /// How long a step may take before the run gives up on it: the
    /// first analysis of the reference, a take's analysis, and a take
    /// beyond its own length (lead-in and range) to stop itself
    static constexpr int kReferenceTimeoutMs = 60000;
    static constexpr int kTakeAnalysisTimeoutMs = 30000;
    static constexpr int kTakeStopTimeoutMs = 10000;

    /// What a run records
    struct Plan {
        LatencyCheck::Layout layout;
        int punchIns;
        int eventsEach;

        /// Where the reference is written, over whatever is there; ""
        /// for a new file in referenceDirectory() (nextReferencePath())
        QString referencePath;

        Plan() : punchIns(0), eventsEach(0) { }
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
     * already, if a take is being recorded, or if the plan's punch-ins
     * do not fit its layout (LatencyCheck::punchInsFor()).  Otherwise
     * finished() comes once, at the end, however the run ends.
     */
    bool start(const Plan &plan);

    /// End the run: a take in progress is stopped through the Stop
    /// path, and finished() says the run was cancelled
    void cancel();

    bool isRunning() const;

    /// Called by MainWindow::closeSession() once the session is sure to
    /// close: a run ends then, unless it is the run replacing it
    void sessionClosing();

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

    void poll();
    void openReference();
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

    /// Whether the analyser, or any transform, is still at work
    static bool analysing(Analyser *analyser);
};

#endif
