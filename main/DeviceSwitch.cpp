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

#include "DeviceSwitch.h"

void
DeviceSwitch::chosen(QString key, QString from, QString to)
{
    // The one in use chosen again leaves nothing to go back to that was
    // not there before
    if (from == to) return;
    m_key = key;
    m_device = from;
    m_known = true;
}

bool
DeviceSwitch::previous(QString key, QString &device) const
{
    if (!m_known || key != m_key) return false;
    device = m_device;
    return true;
}
