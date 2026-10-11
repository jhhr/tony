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

#ifndef TONY_HOTKEYS_DIALOG_H
#define TONY_HOTKEYS_DIALOG_H

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QString>

#include <map>

class HotkeyTable;
class QDialogButtonBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * Edit > Configure Hotkeys: every command of the window that has
 * hotkeys (HotkeyTable), in the key reference's categories, with its
 * keys.  The command chosen in the list has its key, and another, set
 * underneath: pressed into a box, cleared with the box's button, or its
 * own put back with Default.  Restore Defaults puts back every
 * command's.
 *
 * A key two commands have would do neither: both are shown in red, a
 * line under the list names them, and OK is shut until one is changed.
 * OK keeps the keys in the settings (Hotkeys) and closes; the window
 * then gives them to the commands.  Cancel keeps nothing.
 */
class HotkeysDialog : public QDialog
{
    Q_OBJECT

public:
    HotkeysDialog(HotkeyTable *table, QWidget *parent = nullptr);
    virtual ~HotkeysDialog();

    // For the tests, which drive it as the user does

    /// The command's row chosen in the list
    void choose(QString id);

    /// A key pressed into the first box (0) or the second (1); an empty
    /// one is the box cleared
    void pressKey(int box, QKeySequence key);

    /// The keys the command has here, not yet kept
    QList<QKeySequence> keys(QString id) const;

    /// The keys its row lists
    QString listedKeys(QString id) const;

    /// Whether its row is shown as clashing
    bool shownClashing(QString id) const;

    QString clashText() const;
    QPushButton *okButton() const;
    QPushButton *defaultButton() const { return m_default; }
    QPushButton *restoreDefaultsButton() const;
    QKeySequenceEdit *keyBox(int box) const { return m_edit[box]; }

private:
    HotkeyTable *m_table;
    std::map<QString, QList<QKeySequence>> m_keys;
    std::map<QString, QList<QKeySequence>> m_defaults;
    std::map<QString, QString> m_names;
    QMap<QString, QTreeWidgetItem *> m_items;
    QString m_chosen;

    QTreeWidget *m_list;
    QKeySequenceEdit *m_edit[2];
    QPushButton *m_default;
    QLabel *m_clashes;
    QDialogButtonBox *m_buttons;
    bool m_loading;

    void commandChosen();
    void keyEdited(int box);
    void editFinished(int box);
    void restoreDefault();
    void restoreAllDefaults();
    void keep();

    /// The boxes show the chosen command's keys
    void load();

    /// The rows' keys and clashes, and OK
    void refresh();
};

#endif
