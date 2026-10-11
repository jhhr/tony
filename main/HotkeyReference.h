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

#ifndef TONY_HOTKEY_REFERENCE_H
#define TONY_HOTKEY_REFERENCE_H

#include "widgets/KeyReference.h"

#include <QString>
#include <QStringList>

/**
 * Help > Key and Mouse Reference, which can list a command's keys again
 * once they have been changed in Edit > Configure Hotkeys.  svgui's
 * KeyReference writes an action's keys down as it is registered, and
 * builds its window once, from those.
 *
 * Its own show() hides KeyReference's, which is not virtual: MainWindow
 * holds it as a HotkeyReference for that.
 */
class HotkeyReference : public sv::KeyReference
{
public:
    /// The category registered commands go into now
    QString category() const { return m_currentCategory; }

    /**
     * The command of this name, in this category, has these keys now,
     * as the platform writes them (QKeySequence::NativeText): the first
     * is listed as its key, the others as alternatives.  One with none
     * is not listed.  A command not registered is added, at the end of
     * its category
     */
    void setKeys(QString category, QString name, const QStringList &keys);

    /// As KeyReference's, with the keys as they are now
    void show();

    /// The keys listed for a command, first its key, as show() lists
    /// them: none for one not listed
    QStringList listed(QString category, QString name) const;

private:
    /// The window, built with the keys as they were, built again
    void rebuild();
};

#endif
