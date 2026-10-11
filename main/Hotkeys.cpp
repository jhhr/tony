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

#include "Hotkeys.h"

namespace Hotkeys
{

QString
joined(const QStringList &keys)
{
    return keys.join("; ");
}

QStringList
split(QString text)
{
    // As QKeySequence::listFromString() splits it.  "" is no key at all,
    // where splitting it would give one empty key
    if (text == "") return {};
    return text.split("; ");
}

QStringList
keys(QSettings &settings, QString id, const QStringList &defaults)
{
    settings.beginGroup(kGroup);
    const bool chosen = settings.contains(id);
    const QString text = settings.value(id).toString();
    settings.endGroup();
    return chosen ? split(text) : defaults;
}

void
setKeys(QSettings &settings, QString id, const QStringList &keys,
        const QStringList &defaults)
{
    settings.beginGroup(kGroup);
    if (keys == defaults) {
        settings.remove(id);
    } else {
        settings.setValue(id, joined(keys));
    }
    settings.endGroup();
}

void
forgetAll(QSettings &settings)
{
    settings.beginGroup(kGroup);
    settings.remove("");
    settings.endGroup();
}

std::map<QString, QStringList>
clashes(const std::vector<Command> &commands)
{
    // Who has each key, each command once however often it lists it
    std::map<QString, QStringList> holders;
    for (const Command &c : commands) {
        for (const QString &key : c.second) {
            if (key == "") continue;
            QStringList &ids = holders[key];
            if (!ids.contains(c.first)) ids << c.first;
        }
    }

    std::map<QString, QStringList> result;
    for (const Command &c : commands) {
        QStringList others;
        for (const Command &d : commands) {
            if (d.first == c.first) continue;
            for (const QString &key : c.second) {
                auto i = holders.find(key);
                if (i != holders.end() && i->second.contains(d.first)) {
                    others << d.first;
                    break;
                }
            }
        }
        if (!others.isEmpty()) result[c.first] = others;
    }
    return result;
}

}
