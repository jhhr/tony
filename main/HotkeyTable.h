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

#ifndef TONY_HOTKEY_TABLE_H
#define TONY_HOTKEY_TABLE_H

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <vector>

class QAction;
class HotkeyReference;

/**
 * The window's commands that have hotkeys, each with the id the
 * settings keep its keys by (Hotkeys), the name and category the key
 * reference lists it under, and its own keys, its defaults.
 *
 * MainWindow adds each command as it makes its action, which also lists
 * it in the key reference, in the category set there.  Once all are
 * added, apply() gives each the keys the settings hold, and lists those;
 * again after Edit > Configure Hotkeys (HotkeysDialog), which writes the
 * settings.
 */
class HotkeyTable : public QObject
{
    Q_OBJECT

public:
    struct Command {
        QString id;
        QString name;
        QString category;
        QList<QKeySequence> defaults;
        /// None for a command whose keys go where applied() takes them
        QPointer<QAction> action;
    };

    HotkeyTable(HotkeyReference *reference, QObject *parent = nullptr);
    virtual ~HotkeyTable();

    /// An action, named by its text, its keys now its defaults
    void add(QString id, QAction *action);

    /**
     * An action whose keys now may not be its defaults, nor its text its
     * name: CommandHistory's Undo and Redo, which every window shares,
     * and whose text names the command to be undone
     */
    void add(QString id, QAction *action, QString name,
             QList<QKeySequence> defaults);

    /// A command without an action of its own (Re-open, the first of
    /// the recent files), listed with this tip
    void add(QString id, QString name, QString tip,
             QList<QKeySequence> defaults);

    const std::vector<Command> &commands() const { return m_commands; }

    /// The command's keys as the settings hold them; none for an id not
    /// added
    QList<QKeySequence> keys(QString id) const;

    /// Give every command its keys from the settings, and list them
    void apply();

    /// Keys as Hotkeys keeps them, and back
    static QStringList portable(const QList<QKeySequence> &keys);
    static QList<QKeySequence> fromPortable(const QStringList &keys);

signals:
    /// After apply(), for the commands without an action of their own
    void applied();

private:
    QPointer<HotkeyReference> m_reference;
    std::vector<Command> m_commands;

    void addCommand(Command command, QString tip);
};

#endif
