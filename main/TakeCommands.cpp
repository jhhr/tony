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

#include "TakeCommands.h"

#include "MainWindow.h"

SingingTakeCommand::SingingTakeCommand(MainWindow *window, QString name,
                                      QString pathBefore,
                                      const Coverage &coverageBefore,
                                      QString pathAfter,
                                      const Coverage &coverageAfter) :
    m_window(window),
    m_name(name),
    m_pathBefore(pathBefore),
    m_pathAfter(pathAfter),
    m_coverageBefore(coverageBefore),
    m_coverageAfter(coverageAfter)
{
}

SingingTakeCommand::~SingingTakeCommand()
{
}

void
SingingTakeCommand::setEventChanges(const TakeEvents::Change &pitch,
                                   const TakeEvents::Change &notes)
{
    m_pitch = pitch;
    m_notes = notes;

    // The analysis has landed, so its result is in the lists now and a
    // redo has no reason to run it again
    m_pendingAnalysis = Coverage::Range();
}

void
SingingTakeCommand::execute()
{
    TakeState state;
    state.path = m_pathAfter;
    state.coverage = m_coverageAfter;
    state.pitchRemove = m_pitch.removed;
    state.pitchAdd = m_pitch.added;
    state.notesRemove = m_notes.removed;
    state.notesAdd = m_notes.added;
    state.analyse = m_pendingAnalysis;

    if (m_window) m_window->applyTakeState(this, state);
}

void
SingingTakeCommand::unexecute()
{
    // The other way round: what the change added goes, and what it
    // removed comes back.  Nothing is analysed -- a range whose analysis
    // is still running belongs to the state being left behind, and
    // MainWindow abandons that run
    TakeState state;
    state.path = m_pathBefore;
    state.coverage = m_coverageBefore;
    state.pitchRemove = m_pitch.added;
    state.pitchAdd = m_pitch.removed;
    state.notesRemove = m_notes.added;
    state.notesAdd = m_notes.removed;

    if (m_window) m_window->applyTakeState(this, state);
}
