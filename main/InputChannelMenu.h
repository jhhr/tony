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

#ifndef TONY_INPUT_CHANNEL_MENU_H
#define TONY_INPUT_CHANNEL_MENU_H

#include "InputChannel.h"

#include <QObject>
#include <QString>

#include <functional>

class QAction;
class QActionGroup;
class QMenu;

/**
 * Playback > Input Channel: which input of the device a take is made
 * from (InputChannel), for a microphone on one input of an interface
 * with two.  A line naming the input device the choice is kept for,
 * then one entry for each of InputChannel::choices(), with the one kept
 * for that device ticked.
 *
 * A phone before its first take may not know which input it will
 * record from (InputChannel::Key::known): the line says so, and the
 * entries are shut until a take has opened it.
 *
 * A choice is written to the settings, for the device the line names,
 * then signalled: record() reads the settings as each take starts, and
 * a phone, whose input is opened otherwise for one input than for both,
 * opens its device again.
 */
class InputChannelMenu : public QObject
{
    Q_OBJECT

public:
    /// The device a choice is kept for now, and its name for the user
    typedef std::function<InputChannel::Key()> KeyFunction;
    typedef std::function<QString(const InputChannel::Key &)> NameFunction;

    /// Add the submenu to the end of the menu given
    InputChannelMenu(QMenu *menu, KeyFunction key, NameFunction name,
                     QObject *parent = nullptr);
    virtual ~InputChannelMenu();

    QMenu *menu() const { return m_menu; }

    /// Name the device and tick its choice.  Done as the menu opens
    void tick();

    /// Not while a take is being recorded or an audio check runs
    void setEnabled(bool enabled);

signals:
    void channelChosen(int channel);

private:
    QMenu *m_menu;
    QAction *m_deviceLine;
    QActionGroup *m_group;
    KeyFunction m_key;
    NameFunction m_name;

    void chosen(int channel);
};

#endif
