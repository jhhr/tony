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

#include "HotkeyTable.h"
#include "HotkeyReference.h"
#include "Hotkeys.h"

#include <QAction>
#include <QSettings>

#include <iostream>

HotkeyTable::HotkeyTable(HotkeyReference *reference, QObject *parent) :
    QObject(parent),
    m_reference(reference)
{
}

HotkeyTable::~HotkeyTable()
{
}

void
HotkeyTable::add(QString id, QAction *action)
{
    // As KeyReference names an action
    QString name = action->text();
    name.remove('&');
    add(id, action, name, action->shortcuts());
}

void
HotkeyTable::add(QString id, QAction *action, QString name,
                 QList<QKeySequence> defaults)
{
    Command command;
    command.id = id;
    command.name = name;
    command.defaults = defaults;
    command.action = action;
    addCommand(command, action->statusTip());
}

void
HotkeyTable::add(QString id, QString name, QString tip,
                 QList<QKeySequence> defaults)
{
    Command command;
    command.id = id;
    command.name = name;
    command.defaults = defaults;
    addCommand(command, tip);
}

void
HotkeyTable::addCommand(Command command, QString tip)
{
    // The settings keep one value for an id
    for (const Command &c : m_commands) {
        if (c.id == command.id) {
            std::cerr << "HotkeyTable::add: \"" << command.id.toStdString()
                      << "\" added twice; the second is left out"
                      << std::endl;
            return;
        }
    }

    // Listed with its defaults until apply() lists the keys it has
    if (m_reference) {
        command.category = m_reference->category();
        if (!command.defaults.isEmpty()) {
            m_reference->registerShortcut
                (command.name, command.defaults[0], tip);
            for (int i = 1; i < command.defaults.size(); ++i) {
                m_reference->registerAlternativeShortcut
                    (command.name, command.defaults[i]);
            }
        }
    }

    m_commands.push_back(command);
}

QList<QKeySequence>
HotkeyTable::keys(QString id) const
{
    for (const Command &c : m_commands) {
        if (c.id != id) continue;
        QSettings settings;
        return fromPortable(Hotkeys::keys(settings, id, portable(c.defaults)));
    }
    return {};
}

void
HotkeyTable::apply()
{
    for (const Command &c : m_commands) {
        const QList<QKeySequence> now = keys(c.id);
        if (c.action) c.action->setShortcuts(now);
        if (m_reference) {
            QStringList texts;
            for (const QKeySequence &key : now) {
                texts << key.toString(QKeySequence::NativeText);
            }
            m_reference->setKeys(c.category, c.name, texts);
        }
    }
    emit applied();
}

QStringList
HotkeyTable::portable(const QList<QKeySequence> &keys)
{
    QStringList texts;
    for (const QKeySequence &key : keys) {
        if (!key.isEmpty()) texts << key.toString(QKeySequence::PortableText);
    }
    return texts;
}

QList<QKeySequence>
HotkeyTable::fromPortable(const QStringList &texts)
{
    // Text no key can be read from, written by hand say, is no key
    QList<QKeySequence> keys;
    for (const QString &text : texts) {
        QKeySequence key = QKeySequence::fromString
            (text, QKeySequence::PortableText);
        if (!key.isEmpty()) keys << key;
    }
    return keys;
}
