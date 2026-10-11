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

#ifndef TONY_HOTKEYS_H
#define TONY_HOTKEYS_H

#include <QSettings>
#include <QString>
#include <QStringList>

#include <map>
#include <utility>
#include <vector>

/**
 * The hotkeys chosen in Edit > Configure Hotkeys, kept in the settings,
 * group "Hotkeys": one key for each command whose keys were chosen,
 * named by the command's id, holding its keys as one text, "; " between
 * two, as QKeySequence::listToString() writes them in its portable
 * format.  A command not there has its own keys, its defaults; one
 * there with "" has none.
 *
 * Keys are text here, as tony_core has no QKeySequence (QtGui); the
 * window converts.  QKeySequence writes a key one way only, so two keys
 * are the same key when their texts are.
 */
namespace Hotkeys
{
    static constexpr const char *kGroup = "Hotkeys";

    /// The command's keys: those chosen for it, else its defaults
    QStringList keys(QSettings &settings, QString id,
                     const QStringList &defaults);

    /// Keep these as the command's keys; nothing, where they are its
    /// defaults
    void setKeys(QSettings &settings, QString id, const QStringList &keys,
                 const QStringList &defaults);

    /// Every command back to its defaults
    void forgetAll(QSettings &settings);

    /// Keys as the settings keep them, and back
    QString joined(const QStringList &keys);
    QStringList split(QString text);

    /// A command's id and its keys
    typedef std::pair<QString, QStringList> Command;

    /**
     * For each command that has a key another has too, those others, in
     * the order given.  Qt does neither command when such a key is
     * pressed
     */
    std::map<QString, QStringList> clashes
    (const std::vector<Command> &commands);
}

#endif
