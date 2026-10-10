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

#ifndef TONY_CHECK_INPUT_LEVEL_DIALOG_H
#define TONY_CHECK_INPUT_LEVEL_DIALOG_H

#include <QDialog>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

#include <cstdint>
#include <vector>

class InputLevelFeed;
class InputLevelMeter;
class QLabel;
class QPushButton;

/**
 * Playback > Check Input Level: the input meter, large, with the input
 * open and nothing recorded.  The singer keeps quiet for kQuietMs, which
 * is read as the noise floor, then sings their loudest phrase and presses
 * Done.  The result: the loudest peak, how many dB to change the
 * interface's gain by to put such peaks near
 * InputLevel::kTargetPeakDbfs, the noise floor, and a voice threshold
 * suggested from it, which a button sets.
 *
 * It reads the feed's readings (InputLevelFeed::levelRead) and nothing
 * else: the window opens the input and runs the device before showing
 * it, and leaves both as they are after it, as after a take
 * (MainWindow::checkInputLevel()).  Shown with open(), never exec(): no
 * event loop of its own.
 *
 * Play starts and stops the music, for a singer who has it on speakers:
 * the window behind is shut while the dialog is open.  The window plays
 * it (playPressed()) and says whether it is playing (setPlaying()); a
 * change while the silence is read starts the silence again, so that
 * all of it is read with the music as the singer has it.
 */
class CheckInputLevelDialog : public QDialog
{
    Q_OBJECT

public:
    /// How long the silence is read for
    static constexpr int kQuietMs = 2000;

    /// How long the input may deliver nothing before it is said
    static constexpr int kNoInputMs = 3000;

    enum class Stage { Quiet, Singing, Result, NoInput };

    CheckInputLevelDialog(InputLevelFeed *feed, QWidget *parent = nullptr);
    virtual ~CheckInputLevelDialog();

    /// Begin again from the silence, the threshold now in use given
    void start(double currentThreshold);

    /// The device opened again under the check: what was read is of
    /// another stream, perhaps another microphone.  A check under way
    /// starts again from the silence; a result stays
    void deviceReopened();

    /// Whether the music is playing, and whether there is any to play
    void setPlaying(bool playing);
    void setCanPlay(bool canPlay);

    Stage stage() const { return m_stage; }

    /// What the page says now, as plain text
    QString text() const;

    /// Done's figures, once there is a result
    double peakDbfs() const;
    bool clipped() const { return m_clipped; }
    double noiseFloorDbfs() const;
    double suggestedThreshold() const;

    QPushButton *doneButton() const { return m_done; }
    QPushButton *useThresholdButton() const { return m_useThreshold; }
    QPushButton *againButton() const { return m_again; }
    QPushButton *playButton() const { return m_play; }

signals:
    /// The suggested threshold, to be set
    void thresholdChosen(double dbfs);

    /// Play or Stop pressed
    void playPressed();

private:
    QPointer<InputLevelFeed> m_feed;
    InputLevelMeter *m_meter;
    QLabel *m_label;
    QPushButton *m_done;
    QPushButton *m_useThreshold;
    QPushButton *m_again;
    QPushButton *m_play;
    QPushButton *m_close;
    QTimer m_timer;
    QElapsedTimer m_clock;

    Stage m_stage;
    double m_currentThreshold;
    std::int64_t m_firstReadingMs;
    std::vector<double> m_quietPeaks;
    float m_loudest;
    bool m_clipped;
    bool m_playing;

    void levelRead(float peak);
    void tick();
    void finish();
    void showStage(Stage stage);
    QString resultText() const;
};

#endif
