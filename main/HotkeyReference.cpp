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

#include "HotkeyReference.h"

#include <QDialog>

#include <algorithm>

void
HotkeyReference::setKeys(QString category, QString name,
                         const QStringList &keys)
{
    auto c = m_map.find(category);
    if (c != m_map.end()) {
        for (KeyDetails &details : c->second) {
            if (details.actionName != name) continue;
            details.shortcut = keys.value(0);
            details.alternatives.clear();
            for (int i = 1; i < keys.size(); ++i) {
                details.alternatives.push_back(keys[i]);
            }
            rebuild();
            return;
        }
    }

    if (keys.isEmpty()) return;

    // As KeyReference registers it, under the category given, and then
    // registering goes on where it was
    const QString current = m_currentCategory;
    setCategory(category);
    registerShortcutVerbatim(name, keys[0], QString());
    for (int i = 1; i < keys.size(); ++i) {
        registerAlternativeShortcutVerbatim(name, keys[i]);
    }
    if (current != "") setCategory(current);
    rebuild();
}

void
HotkeyReference::show()
{
    // A command with no key now has an entry with none, which would be
    // listed with a blank where its key goes
    const CategoryMap all = m_map;
    for (auto &c : m_map) {
        KeyList &list = c.second;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [](const KeyDetails &d) {
                                      return d.shortcut == "";
                                  }),
                   list.end());
    }
    KeyReference::show();
    m_map = all;
}

QStringList
HotkeyReference::listed(QString category, QString name) const
{
    auto c = m_map.find(category);
    if (c == m_map.end()) return {};
    for (const KeyDetails &details : c->second) {
        if (details.actionName != name) continue;
        if (details.shortcut == "") return {};
        QStringList keys { details.shortcut };
        for (const QString &alternative : details.alternatives) {
            keys << alternative;
        }
        return keys;
    }
    return {};
}

void
HotkeyReference::rebuild()
{
    if (!m_dialog) return;
    const bool shown = m_dialog->isVisible();
    // The text is the dialog's
    delete m_dialog;
    m_dialog = nullptr;
    m_text = nullptr;
    if (shown) show();
}
