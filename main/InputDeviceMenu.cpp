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

#include "InputDeviceMenu.h"
#include "InputDevice.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QSettings>
#include <QStringList>

InputDeviceMenu::InputDeviceMenu(QMenu *menu, QString driver,
                                 ListFunction list, InUseFunction inUse,
                                 QObject *parent) :
    QObject(parent),
    m_menu(menu),
    m_driver(driver),
    m_list(list),
    m_inUse(inUse)
{
    m_group = new QActionGroup(this);
    m_group->setExclusive(true);
    connect(m_group, &QActionGroup::triggered,
            this, &InputDeviceMenu::chosen);
    build();
}

InputDeviceMenu::~InputDeviceMenu()
{
}

QAction *
InputDeviceMenu::addDevice(QString text, const AudioRoute::Device &device)
{
    QAction *action = m_menu->addAction(text);
    action->setCheckable(true);
    // None chosen is no type at all
    action->setData(device.type > 0 ?
                    QStringList({ QString::number(device.type),
                                  device.productName }) :
                    QStringList());
    m_group->addAction(action);
    return action;
}

void
InputDeviceMenu::build()
{
    for (QAction *a : m_group->actions()) {
        m_group->removeAction(a);
    }
    m_menu->clear();

    QSettings settings;
    AudioRoute::Device chosen;
    const bool haveChoice = InputDevice::chosen(settings, m_driver, chosen);

    // The input recording now, which with nothing chosen is the one the
    // phone chose: it is open only once recording has been asked for
    const QString inUse = m_inUse();
    m_menu->addAction(tr("In use: %1")
                      .arg(inUse != "" ? inUse :
                           tr("chosen when recording starts")))
        ->setEnabled(false);
    m_menu->addSeparator();

    addDevice(tr("(System Default)"), AudioRoute::Device())
        ->setChecked(!haveChoice);
    m_menu->addSeparator();

    bool haveCurrent = false;
    for (const AudioRoute::Device &device : InputDevice::choices(m_list())) {
        QAction *action = addDevice(AudioRoute::deviceName(device), device);
        if (haveChoice && InputDevice::same(device, chosen)) {
            action->setChecked(true);
            haveCurrent = true;
        }
    }

    if (haveChoice && !haveCurrent) {
        m_menu->addSeparator();
        addDevice(tr("%1 (not connected)")
                  .arg(AudioRoute::deviceName(chosen)), chosen)
            ->setChecked(true);
    }
}

void
InputDeviceMenu::chosen(QAction *action)
{
    AudioRoute::Device device;
    const QStringList data = action->data().toStringList();
    if (data.size() == 2) {
        device.type = data[0].toInt();
        device.productName = data[1];
    }

    QSettings settings;
    AudioRoute::Device before;
    const bool had = InputDevice::chosen(settings, m_driver, before);
    if (had ? InputDevice::same(before, device) : device.type <= 0) return;
    InputDevice::choose(settings, m_driver, device);
    emit deviceChosen();
}
