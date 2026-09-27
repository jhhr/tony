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

#ifndef TONY_ANDROID_SCREEN_H
#define TONY_ANDROID_SCREEN_H

/**
 * The phone's screen kept on while Tony works on its own. A check of
 * the audio records and judges for minutes with no one touching the
 * phone, whose screen then goes off after its timeout: Android stops
 * Tony's window, Tony stops the take being recorded as Stop does, and
 * Qt holds the event loop until the window is back, so the check ends
 * wrongly. Android only.
 */
namespace AndroidScreen
{
    /// The window's FLAG_KEEP_SCREEN_ON, set or cleared, on Android's
    /// main thread, where a window's flags are changed; why is said in
    /// the log
    void keepOn(bool on, const char *why);
}

#endif
