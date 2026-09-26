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

#ifndef TONY_CALIBRATE_AUDIO_DIALOG_H
#define TONY_CALIBRATE_AUDIO_DIALOG_H

#include "AudioCheckRunner.h"
#include "LatencyCalibration.h"

#ifdef TONY_DEV_CHECKS
#include "dev/DevChecks.h"
#include <QPointer>
class QCheckBox;
#endif

#include <QDialog>

class MainWindow;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;

/**
 * Playback > Calibrate Audio: what the user sees of the audio check.
 * Three pages: what to do before it starts, with the devices and the
 * latency takes are placed with now; how far it has got, with Cancel;
 * and what it found, in plain words, with the fix for whatever went
 * wrong, the figures, and Use this latency when the round trip it
 * measured can be used.
 *
 * Not modal: the check opens a session of its own in the window, which
 * can be looked at while the dialog is up.  A view over the runner and
 * nothing more: it starts and cancels runs, shows what the runner
 * reports of the runs it started (not of any other), and hands a result
 * to MainWindow::storeMeasuredLatency().  It touches no model, layer or
 * take.  Closing it while its check runs cancels the check, since
 * nothing else would show how the run ended.
 *
 * The window owns it, and makes it the first time it is asked for.
 *
 * In development builds the instructions page has a checkbox, on by
 * default and not remembered, to carry on into the dev checks
 * (DevChecks) once the calibration is done and usable, with the round
 * trip it measured.  The progress page then follows them, Cancel ends
 * whichever is running, and the result page adds a line for each check
 * and the report file's path to the calibration's.
 */
class CalibrateAudioDialog : public QDialog
{
    Q_OBJECT

public:
    CalibrateAudioDialog(MainWindow *window, AudioCheckRunner *runner);
    virtual ~CalibrateAudioDialog();

    /// A rough time for each analysis a run waits for (the reference's,
    /// and each take's), added to the recording still to come for the
    /// progress shown: the runner cannot know how long pYIN takes
    static constexpr double kSecondsPerAnalysis = 3.0;

    enum class Page { Instructions, Progress, Result };
    Page page() const;

    /// The words of the page on show, as plain text
    QString pageText() const;

    /// Whether Use this latency is offered, and not yet pressed
    bool canUseLatency() const;

    /// What a check started here records: the calibration
    /// (AudioCheckRunner::calibrationPlan()) unless set otherwise, as
    /// the tests set a shorter one
    void setPlan(const AudioCheckRunner::Plan &plan);
    const AudioCheckRunner::Plan &plan() const { return m_plan; }

    /// The latency a take is placed with, and where it came from, in a
    /// few words: "measured 187 ms, 25 Sep" or "driver's figure, 400
    /// ms".  The Playback menu's line says the same
    static QString describeLatency(const LatencyCalibration::InUse &inUse);

#ifdef TONY_DEV_CHECKS
    /// The dev checks a calibration carries on into; none until given
    void setDevChecks(DevChecks *devChecks);

    /// "Run the dev checks after calibrating", on the instructions page
    bool devChecksWanted() const;
    void setDevChecksWanted(bool wanted);

    /// Where the dev checks write their report and scratch folders, as
    /// the tests set them; the round trip is always the calibration's
    void setDevOptions(const DevChecks::Options &options);
#endif

public slots:
    /// Show the dialog and bring it to the front: on the instructions,
    /// with the devices and the latency as they are now, unless its
    /// check is running
    void present();

    /// Start, and Check Again
    void startCheck();

    /// Cancel: the run ends, and how it ended is the result shown.  In
    /// its dev checks, they end, and the calibration is shown with them
    void cancelCheck();

    /// Keep the round trip the check measured, for the devices it ran on
    void useLatency();

    /// The result page for this result
    void showResult(const AudioCheckResult &result);

    /// Escape, the title bar's close button, and Close.  A check still
    /// running is cancelled first
    void reject() override;

private:
    MainWindow *m_window;
    AudioCheckRunner *m_runner;
    AudioCheckRunner::Plan m_plan;

    /// A run this dialog started is going on
    bool m_running;

    AudioCheckResult m_result;
    bool m_latencyKept;

    /// The time the run was expected to take when it began, and the
    /// share of it the progress bar has shown, which never goes back
    double m_expectedSeconds;
    int m_shownPermille;

    QStackedWidget *m_pages;
    QLabel *m_instructions;
    QLabel *m_step;
    QProgressBar *m_bar;
    QLabel *m_timeLeft;
    QLabel *m_resultText;

    QPushButton *m_startButton;
    QPushButton *m_cancelButton;
    QPushButton *m_useButton;
    QPushButton *m_againButton;
    QPushButton *m_closeButton;

    void runnerProgress(const AudioCheckRunner::Progress &progress);
    void runnerFinished(const AudioCheckResult &result);

    void showPage(Page page);

    /// The result page for m_result as it stands
    void showResultPage();

    QString instructionsHtml() const;
    QString resultHtml() const;
    QString calibrationHtml() const;

#ifdef TONY_DEV_CHECKS
    QPointer<DevChecks> m_devChecks;
    QCheckBox *m_devChecksBox;
    DevChecks::Options m_devOptions;

    /// The run this dialog started is in its dev checks
    bool m_devRunning;

    /// What they reported, or why they did not run
    bool m_haveDevReport;
    DevReport m_devReport;
    QString m_devNote;

    /// Carry a calibration on into the dev checks, if they are wanted
    /// and it can be used; false if the run ends here, with m_devNote
    /// saying why when they were wanted
    bool startDevChecks(const AudioCheckResult &calibration);

    void devProgress(QString stage, int stageNumber, int stages);
    void devFinished(const DevReport &report);

    QString devHtml() const;
#endif

    /// Seconds in milliseconds, for reading: tenths below 10 ms, where
    /// they say something, whole ones from there on
    static QString milliseconds(double seconds);
    static QString signedMilliseconds(double seconds);

    /// A device as the Preferences name it, "" being the default
    static QString deviceName(QString name);

    static QString hertz(sv::sv_samplerate_t rate);
};

#endif
