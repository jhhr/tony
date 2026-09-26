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

#ifndef TONY_TAKE_OBSERVER_H
#define TONY_TAKE_OBSERVER_H

#ifdef TONY_DEV_CHECKS

#include "base/BaseTypes.h"
#include "data/model/Model.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <set>
#include <utility>
#include <vector>

class MainWindow;
class QTimer;

/**
 * Watches one take of the window's (development builds only), from
 * just after it starts until it is let go, and keeps what it saw for
 * the development checks to judge afterwards: DevChecks starts one for
 * each punch-in of the audio check when it starts recording, and stops
 * it when that punch-in's analysis is done.
 *
 * It only looks.  It writes no model and no setting, and reads nothing
 * that someone else would then miss, with one exception made on
 * purpose: the play source's output levels.  getOutputLevels() and
 * getInputLevels() each give the peak since the previous call and
 * reset it, so each can have one reader only.  While a take is being
 * recorded, lead-in included, ViewManager::checkPlayStatus() reads the
 * input levels for the meter and never the output levels, so the
 * observer reads the output levels then, and only then, and takes the
 * input levels from the meter's own signal.
 *
 * A friend of MainWindow, as DevChecks is: it reads the take's state.
 */
class TakeObserver : public QObject
{
    Q_OBJECT

public:
    /// How often it looks: as often as ViewManager moves the cursor
    static constexpr int kPollMs = 20;

    /// What one poll saw
    struct Sample {
        /// Since the observer started
        qint64 ms;

        /// The record target was recording
        bool recording;

        /// The ViewManager's playback frame, where the cursor is drawn:
        /// where the take's playback started plus what has been
        /// recorded (ViewManager::getPlaybackFrame()).  Read only while
        /// recording; after that, the last read
        sv::sv_frame_t playbackFrame;

        /// Frames the record target had received, read just before and
        /// just after the output levels.  An audio callback takes in a
        /// block of input and then hands out a block of output, so these
        /// place the output blocks the levels were taken from
        sv::sv_frame_t framesBefore;
        sv::sv_frame_t framesAfter;

        /// The output levels were read at this poll: only while
        /// recording.  They are the loudest sample handed to the device
        /// since the previous read, left and right, full scale 1; 0 if
        /// the device reported none
        bool outputRead;
        float outputLeft;
        float outputRight;

        /// The loudest input the level meter was told of since the
        /// previous poll, or what it was last told if nothing since;
        /// while recording only
        float inputLeft;
        float inputRight;

        /// The status bar's text, and whether a modal dialog was up: a
        /// modal widget, or an event loop run deeper than the one the
        /// observer started in, which is how a dialog Android draws
        /// itself (a message box, the file picker) shows: Qt's widget for
        /// it is never on screen, and is no active modal widget
        QString status;
        bool modal;

        Sample() : ms(0), recording(false), playbackFrame(0),
                   framesBefore(0), framesAfter(0), outputRead(false),
                   outputLeft(0), outputRight(0), inputLeft(0),
                   inputRight(0), modal(false) { }
    };

    /// A live dot, as it first appeared
    struct Dot {
        qint64 ms;

        /// Where it was drawn, on the reference's timeline, and its
        /// pitch
        sv::sv_frame_t frame;
        float hz;

        /// The playback frame at the poll that first saw it
        sv::sv_frame_t playbackFrame;

        Dot() : ms(0), frame(0), hz(0), playbackFrame(0) { }
    };

    /// All it saw of one take
    struct Observation {
        std::vector<Sample> samples;
        std::vector<Dot> dots;

        /// Where the take's playback started (the pre-roll's lead-in
        /// included): the ViewManager's record start frame
        sv::sv_frame_t playbackStart;

        /// The raw recording, with every channel the device delivered;
        /// "" if none was seen
        QString recordingPath;

        /// When a poll first found the take no longer recording, since
        /// the observer started; -1 if none did
        qint64 stoppedMs;

        /// Play Singing Audio as the button showed it when the take had
        /// begun (it goes on showing what was asked for before the take,
        /// while the take's audio is kept silent), and as it showed after
        /// the take, with whether the take's audio was then audible; the
        /// last two from the last poll after the take stopped
        bool singingAudioBefore;
        bool singingAudioAfter;
        bool takeAudibleAfter;
        bool sawAfter;

        Observation() : playbackStart(0), stoppedMs(-1),
                        singingAudioBefore(false), singingAudioAfter(false),
                        takeAudibleAfter(false), sawAfter(false) { }
    };

    explicit TakeObserver(MainWindow *window, QObject *parent = nullptr);
    virtual ~TakeObserver();

    /// Start watching the take just started, afresh; it is looked at at
    /// once, and then every kPollMs
    void start();

    /// Stop watching.  What was seen stays until the next start()
    void stop();

    bool isObserving() const;

    const Observation &observation() const { return m_observation; }

private:
    MainWindow *m_window;
    QTimer *m_timer;
    QElapsedTimer m_clock;
    Observation m_observation;
    sv::sv_frame_t m_playbackFrame;

    /// The dot model there was when the take began: the last take's,
    /// if its dots were still on show.  This take's is made later
    sv::ModelId m_earlierDots;

    /// The dots seen so far, by frame and pitch
    std::set<std::pair<sv::sv_frame_t, float>> m_seen;

    /// How deep in event loops the observer was started: a poll deeper
    /// than that runs inside a dialog's
    int m_loopLevel;

    /// What the level meter was told since the last poll, and last
    bool m_inputSince;
    float m_inputLeftSince;
    float m_inputRightSince;
    float m_inputLeft;
    float m_inputRight;

    void poll();
    void inputLevels(float left, float right);
};

#endif
#endif
