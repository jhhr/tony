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

#include "AudioDriverMenus.h"
#include "AudioDriverSettings.h"

#include <bqaudioio/AudioFactory.h>

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QSettings>

#include <cmath>

AudioDriverMenus::AudioDriverMenus(QMenu *menu,
                                   std::function<QStringList()> implementations,
                                   QObject *parent) :
    QObject(parent),
    m_implementations(implementations),
    m_appliedLatency(0.0)
{
    m_driverMenu = menu->addMenu(tr("Audio Dri&ver"));
    m_driverMenu->setStatusTip
        (tr("Choose which audio driver Tony plays and records through"));
    m_driverGroup = new QActionGroup(this);
    m_driverGroup->setExclusive(true);

    m_latencyMenu = menu->addMenu(tr("Audio &Latency"));
    m_latencyMenu->setStatusTip
        (tr("Choose how much latency Tony asks the audio driver for: less "
            "answers sooner, more is safer from dropouts"));
    m_latencyGroup = new QActionGroup(this);
    m_latencyGroup->setExclusive(true);

    // The ticks follow the Preferences, which a choice in the other menu,
    // or the driver named by default, may have changed since
    for (QMenu *m : { m_driverMenu, m_latencyMenu }) {
        connect(m, &QMenu::aboutToShow, this, &AudioDriverMenus::rebuild);
    }

    rebuild();
}

AudioDriverMenus::~AudioDriverMenus()
{
}

void
AudioDriverMenus::rebuild()
{
    const QStringList drivers =
        AudioDriverSettings::drivers(m_implementations());

    for (QActionGroup *group : { m_driverGroup, m_latencyGroup }) {
        for (QAction *a : group->actions()) group->removeAction(a);
    }
    m_driverMenu->clear();
    m_latencyMenu->clear();

    // One driver is no choice: the menus are for Windows, where there
    // are three, and elsewhere the device menus are all there is
    const bool shown = (drivers.size() >= 2);
    m_driverMenu->menuAction()->setVisible(shown);
    m_latencyMenu->menuAction()->setVisible(shown);
    if (!shown) return;

    QSettings settings;
    const QString current =
        AudioDriverSettings::currentImplementation(settings);
    const double latency = AudioDriverSettings::latency(settings, current);

    for (const QString &name : drivers) {
        QAction *action = m_driverMenu->addAction(driverName(name));
        action->setCheckable(true);
        action->setChecked(name == current);
        action->setData(name);
        m_driverGroup->addAction(action);
        connect(action, &QAction::triggered,
                this, [this, name]() { driverTriggered(name); });
    }

    for (double seconds : AudioDriverSettings::latencyChoices()) {
        QAction *action = m_latencyMenu->addAction
            (tr("%1 ms").arg(int(std::lround(seconds * 1000.0))));
        action->setCheckable(true);
        action->setChecked(AudioDriverSettings::sameLatency(seconds, latency));
        action->setData(seconds);
        m_latencyGroup->addAction(action);
        connect(action, &QAction::triggered,
                this, [this, seconds]() { latencyTriggered(seconds); });
    }
}

void
AudioDriverMenus::setEnabled(bool enabled)
{
    m_driverMenu->menuAction()->setEnabled(enabled);
    m_latencyMenu->menuAction()->setEnabled(enabled);
}

double
AudioDriverMenus::applyLatency()
{
    QSettings settings;
    const double seconds = AudioDriverSettings::latency
        (settings, AudioDriverSettings::currentImplementation(settings));
    breakfastquay::AudioFactory::setSuggestedLatency(seconds);
    m_appliedLatency = seconds;
    return seconds;
}

QString
AudioDriverMenus::driverName(QString implementation)
{
    return QString::fromStdString
        (breakfastquay::AudioFactory::getImplementationDescription
         (implementation.toStdString()));
}

void
AudioDriverMenus::driverTriggered(QString implementation)
{
    QSettings settings;
    if (AudioDriverSettings::currentImplementation(settings) ==
        implementation) {
        return;
    }
    AudioDriverSettings::setCurrentImplementation(settings, implementation);
    emit driverChosen(implementation);
}

void
AudioDriverMenus::latencyTriggered(double seconds)
{
    QSettings settings;
    const QString current =
        AudioDriverSettings::currentImplementation(settings);
    if (AudioDriverSettings::sameLatency
        (AudioDriverSettings::latency(settings, current), seconds)) {
        return;
    }
    AudioDriverSettings::setLatency(settings, current, seconds);
    emit latencyChosen(seconds);
}
