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

#include "LyricsSize.h"

#include <QAction>
#include <QActionGroup>
#include <QSettings>

static const char *settingsGroup = "MainWindow";
static const char *settingsKey = "lyricssize";

LyricsSize::LyricsSize(QObject *parent) :
    QObject(parent),
    m_group(new QActionGroup(this)),
    m_percent(0)
{
    m_group->setExclusive(true);
    for (int percent : getSteps()) {
        QAction *action = new QAction(tr("%1%").arg(percent), m_group);
        action->setCheckable(true);
        action->setData(percent);
        action->setStatusTip
            (tr("Draw the words of the lyrics at %1% of their normal size")
             .arg(percent));
        connect(action, &QAction::triggered,
                this, [this, percent]() { setPercent(percent); });
    }

    QSettings settings;
    settings.beginGroup(settingsGroup);
    int percent = settings.value(settingsKey, getDefaultPercent()).toInt();
    settings.endGroup();

    // A value from elsewhere that is not a step gives the default
    if (!getSteps().contains(percent)) {
        percent = getDefaultPercent();
    }
    apply(percent);
}

QList<QAction *>
LyricsSize::getActions() const
{
    return m_group->actions();
}

int
LyricsSize::getDefaultPercent()
{
#ifdef Q_OS_ANDROID
    return 50;
#else
    return 100;
#endif
}

void
LyricsSize::setPercent(int percent)
{
    if (!getSteps().contains(percent)) return;
    apply(percent);

    QSettings settings;
    settings.beginGroup(settingsGroup);
    settings.setValue(settingsKey, percent);
    settings.endGroup();
}

void
LyricsSize::apply(int percent)
{
    bool changed = (percent != m_percent);
    m_percent = percent;
    for (QAction *action : m_group->actions()) {
        if (action->data().toInt() == percent) action->setChecked(true);
    }
    if (changed) emit textScaleChanged(getTextScale());
}
