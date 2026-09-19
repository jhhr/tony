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

#ifndef TONY_SINGING_TAKES_H
#define TONY_SINGING_TAKES_H

#include "Coverage.h"

#include "base/BaseTypes.h"

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * The state of the singing takes of a session: the audio file each
 * take's singing lives in, and the ranges of that file that hold
 * recorded material.  Until takes proper arrive there is at most one.
 *
 * It knows nothing of layers, models or windows: MainWindow asks it
 * where a recording is to go and what the take is afterwards, and puts
 * the answer on the screen itself.  The audio files are written by
 * TakeAudio.
 */
class SingingTakes : public QObject
{
    Q_OBJECT

public:
    SingingTakes(QObject *parent = nullptr);
    virtual ~SingingTakes();

    /// There is a take, with an audio file to play and analyse
    bool haveTake() const { return m_audioPath != ""; }

    QString getAudioPath() const { return m_audioPath; }
    const Coverage &getCoverage() const { return m_coverage; }

    /// Nothing recorded and nothing to go back to: a new session
    void clear();

    /**
     * The take is the whole of this file: a singing track the user
     * loaded, or one restored from a session saved before coverage was
     * stored with it.
     */
    void setWholeFileTake(QString path, sv::sv_frame_t frames);

    /**
     * Write the next audio file of the take, with the recording in
     * recordingPath spliced into it at "position" on the reference's
     * timeline.  The recording is used from its frame recordingOffset
     * on (the latency: what was recorded before the singer could have
     * heard the reference at "position"), for "length" frames, or to
     * its end if length is negative.  The file is written into
     * directory, under a name that nothing else is using.
     *
     * On success returns "" and the take's audio is the new file, its
     * coverage takes in what was recorded, and the file before is
     * remembered as superseded.  Otherwise the take is as it was and
     * the return is a message for the user.
     */
    QString spliceRecording(QString recordingPath,
                            sv::sv_frame_t recordingOffset,
                            sv::sv_frame_t position,
                            sv::sv_frame_t length,
                            QString directory,
                            Coverage::Range *placed = nullptr);

    /**
     * The audio files of takes that later files have replaced during
     * this run.  They are kept until the session closes, for undo.
     */
    const QStringList &getSupersededPaths() const { return m_superseded; }

    /// Recording from this frame on would record over material that is there
    bool coversPosition(sv::sv_frame_t position) const;

    /// ... and the user has not asked to stop being asked about it
    bool shouldConfirmRecordingAt(sv::sv_frame_t position) const;

    /// The state of "Don't ask again" in the overwrite question
    static bool isOverwriteConfirmationWanted();
    static void setOverwriteConfirmationWanted(bool wanted);

    /**
     * A path in directory for the next audio file of a take, under a
     * name that no file there has.  Empty if every name tried was
     * taken.
     */
    static QString nextAudioPath(QString directory);

private:
    QString m_audioPath;
    Coverage m_coverage;
    QStringList m_superseded;
};

#endif
