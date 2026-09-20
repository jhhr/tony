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

#ifndef TONY_TAKE_COMMANDS_H
#define TONY_TAKE_COMMANDS_H

#include "Coverage.h"
#include "TakeEvents.h"

#include "base/Command.h"

#include <QString>

class MainWindow;

/**
 * What the take is to be made into: its audio file, its coverage, and
 * the events to take out of and put into its pitch track and its notes.
 * An empty path is the state before the first recording of a take: no
 * audio, and nothing to show it with.
 *
 * "analyse" is a range whose analysis has to be run again rather than
 * restored from events, because its result never existed -- the run was
 * still going when the command was undone.  An empty range means there
 * is nothing to analyse.
 */
struct TakeState
{
    QString path;
    Coverage coverage;
    sv::EventVector pitchRemove;
    sv::EventVector pitchAdd;
    sv::EventVector notesRemove;
    sv::EventVector notesAdd;
    Coverage::Range analyse;
};

/**
 * One undoable change to the singing of a take: a recording spliced into
 * it ("Record Singing", together with the analysis of the range that was
 * recorded) or a part of it erased ("Erase Singing").
 *
 * The command holds values only: the audio file before and after, the
 * coverage before and after, and the events the change took out of and
 * put into the pitch track and the notes.  No layer and no model
 * pointer -- by the time an undo runs, the take's audio model and its
 * analyser have been made again several times over, and MainWindow finds
 * the current ones.  The audio files both stay on disk until the session
 * closes, which is what makes the swap back possible.
 *
 * A recording is on the undo stack from the moment its audio is spliced,
 * which is seconds before the analysis of the recorded range is merged
 * into the take's pitch and notes.  The command is "open" until then:
 * MainWindow amends it with the events of the merge when it lands, and
 * an undo pressed before that abandons the run and leaves the range in
 * setPendingAnalysis(), for a redo to analyse again.
 */
class SingingTakeCommand : public sv::Command
{
public:
    SingingTakeCommand(MainWindow *window, QString name,
                       QString pathBefore, const Coverage &coverageBefore,
                       QString pathAfter, const Coverage &coverageAfter);
    virtual ~SingingTakeCommand();

    QString getName() const override { return m_name; }

    /// Do it again: the take as it was after the change
    void execute() override;

    /// Undo: the take as it was before the change
    void unexecute() override;

    /**
     * The events the change took out of and put into the pitch track and
     * the notes.  For a recording this arrives with the merge of the
     * ranged analysis, which is also the moment the range below stops
     * needing to be analysed again.
     */
    void setEventChanges(const TakeEvents::Change &pitch,
                         const TakeEvents::Change &notes);

    /**
     * This range was being analysed when the command was made, so its
     * result is in no event list: a redo has to analyse it again.
     */
    void setPendingAnalysis(const Coverage::Range &range) {
        m_pendingAnalysis = range;
    }

private:
    MainWindow *m_window;
    QString m_name;
    QString m_pathBefore;
    QString m_pathAfter;
    Coverage m_coverageBefore;
    Coverage m_coverageAfter;
    TakeEvents::Change m_pitch;
    TakeEvents::Change m_notes;
    Coverage::Range m_pendingAnalysis;
};

#endif
