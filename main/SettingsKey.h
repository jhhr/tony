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

#ifndef TONY_SETTINGS_KEY_H
#define TONY_SETTINGS_KEY_H

#include <QString>

/**
 * A driver's or a device's name as part of a settings key, as the
 * things kept per device are kept (LatencyCalibration, InputChannel,
 * InputDevice).
 */
namespace SettingsKey
{
    /**
     * QSettings takes "/" and "\" as the start of a subgroup, so they
     * are percent-encoded, and so is "%" itself, and the "|" that
     * separates the names in a key.  Nothing else is: every settings
     * format keeps any other character, non-ASCII ones included, and the
     * Windows registry allows a key name 255 characters at most, which a
     * name encoded whole could run past
     */
    QString encoded(QString name);

    /// The other way
    QString decoded(QString name);
}

#endif
