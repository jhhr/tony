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

#include "InputChannelMenu.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QSettings>

InputChannelMenu::InputChannelMenu(QMenu *menu, KeyFunction key,
                                   NameFunction name, QObject *parent) :
    QObject(parent),
    m_key(key),
    m_name(name)
{
    // One line: the status bar shows no more
    const QString tip =
        tr("For a microphone on one input of two: make takes from that "
           "input alone, in both ears and at its own level");

    m_menu = menu->addMenu(tr("Input &Channel"));
    m_menu->menuAction()->setStatusTip(tip);
    m_menu->menuAction()->setToolTip(tip);

    // Which device the choice is kept for: each input device keeps its own
    m_deviceLine = m_menu->addAction(QString());
    m_deviceLine->setEnabled(false);
    m_menu->addSeparator();

    m_group = new QActionGroup(this);
    m_group->setExclusive(true);

    for (int channel : InputChannel::choices()) {
        QAction *action = m_menu->addAction(InputChannel::label(channel));
        action->setCheckable(true);
        action->setData(channel);
        action->setStatusTip(tip);
        m_group->addAction(action);
        connect(action, &QAction::triggered,
                this, [this, channel]() { chosen(channel); });
    }

    // The device may have changed since the menu was last open
    connect(m_menu, &QMenu::aboutToShow, this, &InputChannelMenu::tick);

    tick();
}

InputChannelMenu::~InputChannelMenu()
{
}

void
InputChannelMenu::tick()
{
    // Until a phone's first take it cannot say which input it will
    // record from, and a choice kept then would be read for none
    const InputChannel::Key key = m_key();
    m_deviceLine->setText(key.known ?
                          tr("For: %1").arg(m_name(key)) :
                          tr("For: the input, known once a take has "
                             "started"));

    QSettings settings;
    const int current = InputChannel::channel(settings, key);
    for (QAction *action : m_group->actions()) {
        action->setChecked(action->data().toInt() == current);
        action->setEnabled(key.known);
    }
}

void
InputChannelMenu::setEnabled(bool enabled)
{
    m_menu->menuAction()->setEnabled(enabled);
}

void
InputChannelMenu::chosen(int channel)
{
    QSettings settings;
    const InputChannel::Key key = m_key();
    if (!key.known) return;
    if (InputChannel::channel(settings, key) == channel) return;
    InputChannel::setChannel(settings, key, channel);
    emit channelChosen(channel);
}
