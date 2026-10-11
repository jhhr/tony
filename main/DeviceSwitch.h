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

#ifndef TONY_DEVICE_SWITCH_H
#define TONY_DEVICE_SWITCH_H

#include <QString>

/**
 * The output device chosen before the one in use, which the hotkey
 * Switch to Previous Output Device goes back to: for a singer who
 * records in headphones and plays the take back on speakers.
 *
 * A device is a name under a Preferences key, and each driver has a key
 * of its own, so the device is kept with the key it was chosen under:
 * under another driver there is none.  "" is (System Default), a device
 * like any other.  Not kept between launches: until a second device is
 * chosen in this one there is none, and the hotkey does nothing.
 */
class DeviceSwitch
{
public:
    /// The device under this key chosen, in place of the one before it
    void chosen(QString key, QString from, QString to);

    /**
     * The device chosen before the one in use under this key, if one
     * was.  Choosing it makes the other the one to go back to
     */
    bool previous(QString key, QString &device) const;

private:
    QString m_key;
    QString m_device;
    bool m_known = false;
};

#endif
