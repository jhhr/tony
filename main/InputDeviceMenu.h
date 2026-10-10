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

#ifndef TONY_INPUT_DEVICE_MENU_H
#define TONY_INPUT_DEVICE_MENU_H

#include "AudioRoute.h"

#include <QObject>
#include <QString>

#include <functional>
#include <vector>

class QAction;
class QActionGroup;
class QMenu;

/**
 * Playback > Audio Input Device on a driver that lists its inputs, as a
 * phone's does (InputDevice): a line naming the input in use, then
 * "(System Default)", the driver's own choice, then each input the
 * driver lists that is offered (InputDevice::choices()), with the one
 * chosen ticked.  A device chosen that is not listed (unplugged) is
 * shown too, ticked and marked, as the desktop's menu shows one: the
 * choice is still in force for when it is plugged in again.
 *
 * Built again each time it opens (build()): the driver lists what is
 * plugged in now, and opens nothing to do so.  A choice is written to
 * the settings, for the driver, then signalled: the window opens its
 * device again on the input chosen, if it is open for recording.
 */
class InputDeviceMenu : public QObject
{
    Q_OBJECT

public:
    /// The inputs the driver lists now
    typedef std::function<std::vector<AudioRoute::Device>()> ListFunction;

    /// The name of the input recording now; "" while the device is open
    /// for playback only
    typedef std::function<QString()> InUseFunction;

    /// Fill the menu given, which is cleared first
    InputDeviceMenu(QMenu *menu, QString driver, ListFunction list,
                    InUseFunction inUse, QObject *parent = nullptr);
    virtual ~InputDeviceMenu();

    QMenu *menu() const { return m_menu; }

    /// List the inputs again.  Done as the menu opens
    void build();

signals:
    void deviceChosen();

private:
    QMenu *m_menu;
    QActionGroup *m_group;
    QString m_driver;
    ListFunction m_list;
    InUseFunction m_inUse;

    QAction *addDevice(QString text, const AudioRoute::Device &device);
    void chosen(QAction *action);
};

#endif
