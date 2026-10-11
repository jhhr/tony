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

#include "HotkeysDialog.h"
#include "HotkeyTable.h"
#include "Hotkeys.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

const int kIdRole = Qt::UserRole;

// A clash, in its row and in the line that names it
const QColor kClashColour(204, 0, 0);

QString
listText(const QList<QKeySequence> &keys)
{
    QStringList texts;
    for (const QKeySequence &key : keys) {
        texts << key.toString(QKeySequence::NativeText);
    }
    return texts.join(", ");
}

}

HotkeysDialog::HotkeysDialog(HotkeyTable *table, QWidget *parent) :
    QDialog(parent),
    m_table(table),
    m_loading(false)
{
    setWindowTitle(tr("Configure Hotkeys"));

    QVBoxLayout *layout = new QVBoxLayout(this);

    QLabel *intro = new QLabel
        (tr("Choose a command, then press its key in the box under the "
            "list."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // The key reference's categories, in its order, always open
    m_list = new QTreeWidget;
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({ tr("Command"), tr("Hotkey") });
    m_list->setRootIsDecorated(false);
    m_list->setItemsExpandable(false);
    m_list->setAllColumnsShowFocus(true);
    QMap<QString, QTreeWidgetItem *> categories;
    for (const HotkeyTable::Command &c : table->commands()) {
        QTreeWidgetItem *category = categories.value(c.category);
        if (!category) {
            category = new QTreeWidgetItem(m_list, { c.category });
            category->setFlags(Qt::ItemIsEnabled);
            QFont font = category->font(0);
            font.setBold(true);
            category->setFont(0, font);
            categories[c.category] = category;
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(category, { c.name });
        item->setData(0, kIdRole, c.id);
        m_items[c.id] = item;
        m_names[c.id] = c.name;
        m_defaults[c.id] = c.defaults;
        m_keys[c.id] = table->keys(c.id);
    }
    m_list->expandAll();
    layout->addWidget(m_list, 1);

    QGridLayout *boxes = new QGridLayout;
    const QString labels[2] = { tr("&Key:"), tr("&Another key:") };
    for (int box = 0; box < 2; ++box) {
        m_edit[box] = new QKeySequenceEdit;
        m_edit[box]->setClearButtonEnabled(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        // One key, not a sequence of them: done as soon as it is pressed
        m_edit[box]->setMaximumSequenceLength(1);
#endif
        QLabel *label = new QLabel(labels[box]);
        label->setBuddy(m_edit[box]);
        boxes->addWidget(label, box, 0);
        boxes->addWidget(m_edit[box], box, 1);
        connect(m_edit[box], &QKeySequenceEdit::keySequenceChanged,
                this, [this, box]() { keyEdited(box); });
        connect(m_edit[box], &QKeySequenceEdit::editingFinished,
                this, [this, box]() { editFinished(box); });
    }
    m_default = new QPushButton(tr("&Default"));
    m_default->setToolTip(tr("Give the command its own keys again"));
    connect(m_default, &QPushButton::clicked,
            this, &HotkeysDialog::restoreDefault);
    boxes->addWidget(m_default, 0, 2);
    layout->addLayout(boxes);

    m_clashes = new QLabel;
    m_clashes->setWordWrap(true);
    QPalette palette = m_clashes->palette();
    palette.setColor(QPalette::WindowText, kClashColour);
    m_clashes->setPalette(palette);
    layout->addWidget(m_clashes);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                     QDialogButtonBox::Cancel |
                                     QDialogButtonBox::RestoreDefaults);
    connect(m_buttons, &QDialogButtonBox::accepted,
            this, &HotkeysDialog::keep);
    connect(m_buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    connect(restoreDefaultsButton(), &QPushButton::clicked,
            this, &HotkeysDialog::restoreAllDefaults);
    layout->addWidget(m_buttons);

    connect(m_list, &QTreeWidget::currentItemChanged,
            this, [this]() { commandChosen(); });

    load();
    refresh();
    m_list->resizeColumnToContents(0);

    const QFontMetrics metrics(font());
    resize(metrics.averageCharWidth() * 72, metrics.height() * 36);
}

HotkeysDialog::~HotkeysDialog()
{
}

void
HotkeysDialog::commandChosen()
{
    QTreeWidgetItem *item = m_list->currentItem();
    m_chosen = item ? item->data(0, kIdRole).toString() : QString();
    load();
}

void
HotkeysDialog::load()
{
    const bool chosen = (m_chosen != "");
    const QList<QKeySequence> keys =
        chosen ? m_keys[m_chosen] : QList<QKeySequence>();
    m_loading = true;
    for (int box = 0; box < 2; ++box) {
        m_edit[box]->setKeySequence(keys.value(box));
        m_edit[box]->setEnabled(chosen);
    }
    m_loading = false;
    m_default->setEnabled(chosen && m_keys[m_chosen] != m_defaults[m_chosen]);
}

void
HotkeysDialog::keyEdited(int)
{
    if (m_loading || m_chosen == "") return;

    // The first key pressed into each box, once: a sequence of keys is
    // not offered, and the same key twice is one
    QList<QKeySequence> keys;
    for (int box = 0; box < 2; ++box) {
        QKeySequence key = m_edit[box]->keySequence();
        if (key.isEmpty()) continue;
        key = QKeySequence(key[0]);
        if (!keys.contains(key)) keys << key;
    }
    m_keys[m_chosen] = keys;
    m_default->setEnabled(keys != m_defaults[m_chosen]);
    refresh();
}

void
HotkeysDialog::editFinished(int box)
{
    // Before Qt 6.5 a box takes up to four keys pressed in a row: it
    // shows the one kept
    const QKeySequence key = m_edit[box]->keySequence();
    if (key.count() > 1) {
        m_loading = true;
        m_edit[box]->setKeySequence(QKeySequence(key[0]));
        m_loading = false;
    }
}

void
HotkeysDialog::restoreDefault()
{
    if (m_chosen == "") return;
    m_keys[m_chosen] = m_defaults[m_chosen];
    load();
    refresh();
}

void
HotkeysDialog::restoreAllDefaults()
{
    m_keys = m_defaults;
    load();
    refresh();
}

void
HotkeysDialog::refresh()
{
    std::vector<Hotkeys::Command> commands;
    for (const HotkeyTable::Command &c : m_table->commands()) {
        commands.push_back({ c.id, HotkeyTable::portable(m_keys[c.id]) });
    }
    const std::map<QString, QStringList> clashes = Hotkeys::clashes(commands);

    for (auto i = m_items.begin(); i != m_items.end(); ++i) {
        const QString id = i.key();
        QTreeWidgetItem *item = i.value();
        item->setText(1, listText(m_keys[id]));
        const bool clashing = (clashes.count(id) > 0);
        for (int column : { 0, 1 }) {
            item->setForeground(column, clashing ? QBrush(kClashColour) :
                                QBrush());
        }
    }

    // A line for each key that two commands have, in the list's order
    QStringList lines;
    QStringList said;
    for (const HotkeyTable::Command &c : m_table->commands()) {
        auto i = clashes.find(c.id);
        if (i == clashes.end()) continue;
        for (const QKeySequence &key : m_keys[c.id]) {
            const QString text = key.toString(QKeySequence::NativeText);
            if (said.contains(text)) continue;
            QStringList names { c.name };
            for (const QString &other : i->second) {
                if (m_keys[other].contains(key)) names << m_names[other];
            }
            if (names.size() < 2) continue;
            said << text;
            lines << tr("%1 is the key of %2.")
                .arg(text, names.join(tr(" and of ")));
        }
    }

    if (lines.isEmpty()) {
        m_clashes->clear();
        m_clashes->hide();
    } else {
        m_clashes->setText
            (tr("A key two commands have does neither: change one of them.") +
             "\n" + lines.join("\n"));
        m_clashes->show();
    }
    okButton()->setEnabled(lines.isEmpty());
}

void
HotkeysDialog::keep()
{
    QSettings settings;
    for (const HotkeyTable::Command &c : m_table->commands()) {
        Hotkeys::setKeys(settings, c.id, HotkeyTable::portable(m_keys[c.id]),
                         HotkeyTable::portable(c.defaults));
    }
    accept();
}

void
HotkeysDialog::choose(QString id)
{
    m_list->setCurrentItem(m_items.value(id));
}

void
HotkeysDialog::pressKey(int box, QKeySequence key)
{
    m_edit[box]->setKeySequence(key);
}

QList<QKeySequence>
HotkeysDialog::keys(QString id) const
{
    auto i = m_keys.find(id);
    return i == m_keys.end() ? QList<QKeySequence>() : i->second;
}

QString
HotkeysDialog::listedKeys(QString id) const
{
    QTreeWidgetItem *item = m_items.value(id);
    return item ? item->text(1) : QString();
}

bool
HotkeysDialog::shownClashing(QString id) const
{
    QTreeWidgetItem *item = m_items.value(id);
    return item && item->foreground(1).color() == kClashColour &&
        item->foreground(1).style() != Qt::NoBrush;
}

QString
HotkeysDialog::clashText() const
{
    return m_clashes->text();
}

QPushButton *
HotkeysDialog::okButton() const
{
    return m_buttons->button(QDialogButtonBox::Ok);
}

QPushButton *
HotkeysDialog::restoreDefaultsButton() const
{
    return m_buttons->button(QDialogButtonBox::RestoreDefaults);
}
