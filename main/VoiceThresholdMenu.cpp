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

#include "VoiceThresholdMenu.h"
#include "VoiceThreshold.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QSettings>

VoiceThresholdMenu::VoiceThresholdMenu(QMenu *menu, QObject *parent) :
    QObject(parent)
{
    // One line: the status bar shows no more.  The rest (from the next
    // take on; Analyse Now for the takes there are) is in the README
    const QString tip =
        tr("For music on speakers: sound quieter than this gets no live "
           "dots, and no pitch or notes when the take is analysed. The "
           "audio is recorded as it is");

    m_menu = menu->addMenu(tr("Voice &Threshold"));
    m_menu->menuAction()->setStatusTip(tip);
    m_menu->menuAction()->setToolTip(tip);
    m_group = new QActionGroup(this);
    m_group->setExclusive(true);

    for (double dbfs : VoiceThreshold::choices()) {
        QAction *action = m_menu->addAction(VoiceThreshold::label(dbfs));
        action->setCheckable(true);
        action->setData(dbfs);
        // Else the status bar goes blank as the pointer reaches the
        // entries, which is where the choice is made
        action->setStatusTip(tip);
        m_group->addAction(action);
        connect(action, &QAction::triggered,
                this, [this, dbfs]() { chosen(dbfs); });
    }

    // The tick shows what the next take will use, and the takes read the
    // settings, not this menu
    connect(m_menu, &QMenu::aboutToShow, this, &VoiceThresholdMenu::tick);

    tick();
}

VoiceThresholdMenu::~VoiceThresholdMenu()
{
}

void
VoiceThresholdMenu::tick()
{
    QSettings settings;
    const double current = VoiceThreshold::threshold(settings);

    // A threshold kept that is none of the choices ticks none, and is
    // still the one used.  The exclusive group keeps the user from
    // unticking them all, not this
    for (QAction *action : m_group->actions()) {
        action->setChecked(action->data().toDouble() == current);
    }
}

void
VoiceThresholdMenu::setEnabled(bool enabled)
{
    m_menu->menuAction()->setEnabled(enabled);
}

void
VoiceThresholdMenu::chosen(double dbfs)
{
    QSettings settings;
    VoiceThreshold::setThreshold(settings, dbfs);
    emit thresholdChosen(VoiceThreshold::threshold(settings));
}
