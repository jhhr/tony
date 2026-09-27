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

#ifndef TONY_VOICE_THRESHOLD_MENU_H
#define TONY_VOICE_THRESHOLD_MENU_H

#include <QObject>

class QMenu;
class QActionGroup;

/**
 * Playback > Voice Threshold: the level under which what the microphone
 * hears is not taken for singing (VoiceThreshold), for a singer who has
 * the music on speakers.  One entry for each of VoiceThreshold::choices(),
 * with the one kept in the settings ticked.
 *
 * A choice is only written to the settings.  record() reads them as each
 * take starts, and Analyse Now as it runs, so nothing else needs to hear
 * of it.
 */
class VoiceThresholdMenu : public QObject
{
    Q_OBJECT

public:
    /// Add the submenu to the end of the menu given
    VoiceThresholdMenu(QMenu *menu, QObject *parent = nullptr);
    virtual ~VoiceThresholdMenu();

    QMenu *menu() const { return m_menu; }

    /**
     * Tick the threshold kept in the settings, or none where that is not
     * one of the choices.  Done as the menu opens.
     */
    void tick();

    /// Not while a take is being recorded or an audio check runs
    void setEnabled(bool enabled);

private:
    QMenu *m_menu;
    QActionGroup *m_group;

    void chosen(double dbfs);
};

#endif
