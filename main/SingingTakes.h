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

#include <vector>

/**
 * The state of the singing takes of a session: for each take, the audio
 * file its singing lives in and the ranges of that file that hold
 * recorded material.  One take is the active one, and the single-take
 * calls below -- getAudioPath(), spliceRecording() and the rest -- are
 * all about that one.
 *
 * It knows nothing of layers, models or windows: MainWindow asks it
 * where a recording is to go and what the take is afterwards, and puts
 * the answer on the screen itself.  A take's identity on the screen is
 * its name, which is what the names of its layers are built from; no
 * layer or model is named here.  The audio files are written by
 * TakeAudio.
 */
class SingingTakes : public QObject
{
    Q_OBJECT

public:
    SingingTakes(QObject *parent = nullptr);
    virtual ~SingingTakes();

    /**
     * One take: its name, the audio file that holds its singing (empty
     * before its first recording) and the ranges of that file that hold
     * recorded material.
     */
    struct Take {
        QString name;
        QString audioPath;
        Coverage coverage;
    };

    typedef std::vector<Take> Takes;

    /// There is an active take, with an audio file to play and analyse
    bool haveTake() const;

    QString getAudioPath() const;
    const Coverage &getCoverage() const;

    /// Nothing recorded and nothing to go back to: a new session
    void clear();

    // --- The takes of the session (spec 5.3) ---

    const Takes &getTakes() const { return m_takes; }
    int getTakeCount() const { return int(m_takes.size()); }

    /// Which take is the active one, or -1 when there is no take at all
    int getActiveIndex() const { return m_active; }

    /// The name of the active take, or "" when there is none
    QString getActiveName() const;

    QStringList getTakeNames() const;

    /// The take of this name, or -1
    int indexOf(QString name) const;

    /// The take at this index, or null
    const Take *getTake(int index) const;

    /// Make the take at this index the active one.  False if there is none
    bool setActiveIndex(int index);

    /**
     * Add a take with no audio yet and make it the active one; the
     * return is its name.  With no name given it is called "Take N",
     * with an N that no take of this session has had.
     */
    QString addTake(QString name = "");

    /**
     * Add a take that holds the same audio file and coverage as the
     * active one, and make it the active one; the return is its name, or
     * "" if there was no take to copy.  The audio file is shared: both
     * takes write a new one before they change anything in it.
     */
    QString duplicateActiveTake(QString name = "");

    /**
     * Rename the take at this index.  False if there is no such take, if
     * the name is empty, or if another take has it already.
     */
    bool renameTake(int index, QString name);

    /**
     * Do not give this name to a take that is named by default, although
     * no take of the session has it: the layers of a take that a session
     * held and that this session has not taken up are in the document
     * under it.  A name asked for by name is still given.
     *
     * A name of the "Take N" form also carries the numbering on: after
     * "Take 7" has been reserved the next take named by default is "Take
     * 8", whatever has happened to the takes before it.
     */
    void reserveTakeName(QString name);

    /**
     * Forget the take at this index; no audio file is touched.  If it was
     * the active one, the take before it becomes active, or the one after
     * it if it was the first, or there is no active take left.  False if
     * there is no such take.
     */
    bool removeTake(int index);

    /**
     * The active take is this file, with the coverage a session kept for
     * it in its coverage strip.  With no take at all, one is added.
     */
    void setTake(QString path, const Coverage &coverage);

    /**
     * The take is this file with this coverage again, because an undo or
     * a redo has said so.  Unlike setTake(), nothing is added to the
     * superseded list: both files were already known when the operation
     * being undone was done, and undo and redo may swap between them any
     * number of times.  An empty path is the state before the first
     * recording of a take: no take at all.
     */
    void restoreTake(QString path, const Coverage &coverage);

    /**
     * The audio of the take at this index is this file instead: a copy of
     * the same sound somewhere else, made because the session was saved
     * and its takes' audio belongs beside it (spec 6.4, TakesFile).
     *
     * The file the take had is not superseded -- nothing about the take
     * has changed, and the audio model that is showing it goes on
     * reading it -- but it is no longer the take's, so the cleanup on
     * close may take it away if this run wrote it and no saved session
     * names it.  The copy counts as a file this run wrote, for the same
     * reason.
     */
    void relocateTake(int index, QString path);

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
     * Write the next audio file of the take with the given ranges made
     * silent.  The ranges are clipped to the take's coverage first:
     * silence that was never recorded holds nothing to erase.  The file
     * is written into directory, under a name that nothing else is
     * using; it is as long as the file it came from.
     *
     * On success returns "" and the take's audio is the new file, with
     * the erased ranges gone from its coverage and the file before
     * remembered as superseded.  "erased", if it is not null, receives
     * the ranges that were taken out: empty means that nothing of what
     * was asked for held recorded singing, and nothing was done at all.
     * On failure the take is exactly as it was and the return is a
     * message for the user.
     */
    QString eraseRanges(const Coverage::Ranges &ranges,
                        QString directory,
                        Coverage::Ranges *erased = nullptr);

    /**
     * The audio files of takes that later files have replaced during
     * this run.  They are kept until the session closes, for undo.
     */
    const QStringList &getSupersededPaths() const { return m_superseded; }

    /**
     * The audio files this run wrote itself, oldest first.  A file the
     * user loaded as a singing track is not one of them, however
     * thoroughly it has since been superseded, so this is the list Tony
     * may delete from (see removeUnusedFiles()).
     */
    const QStringList &getWrittenPaths() const { return m_written; }

    /**
     * Keep this file whatever happens, because something outside this
     * object refers to it: a session file that has been saved names the
     * take's audio as it was at the time, and that file must still be
     * there when the session is opened again.
     */
    void protectPath(QString path);

    /**
     * The files this run wrote that nothing refers to any more: every
     * one but the audio of a take -- any take, since a duplicate shares
     * its file with the take it was made from -- and the protected ones.
     * Files the user brought are never in the list.
     */
    QStringList unusedWrittenFiles() const;

    /**
     * Delete the files unusedWrittenFiles() names and return those that
     * really went.  For the close of a session: until then a superseded
     * file may be wanted again by undo.
     */
    QStringList removeUnusedFiles();

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
    Takes m_takes;
    int m_active;

    // Counts the takes this session has named, so that a name is never
    // used twice even after the take that had it has gone
    int m_named;

    // Names no take has, that are not to be given to one all the same
    QStringList m_reserved;

    QStringList m_superseded;
    QStringList m_written;
    QStringList m_protected;

    // The active take, or null.  The non-const one adds a take if there
    // is none: the first recording of a session makes "Take 1"
    const Take *activeTake() const;
    Take &takeForRecording();
};

#endif
