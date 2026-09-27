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

#ifndef TONY_PLOT_SIZE_H
#define TONY_PLOT_SIZE_H

#include <QList>
#include <QObject>

class QAction;
class QActionGroup;

namespace sv {
class ViewManager;
}

/**
 * How large the panes draw the pitch tracks, the live dots and the
 * notes, as View > Plot Size sets it: 100%, 150% or 200% of their
 * normal size in logical pixels. It is the view manager's plot scale
 * (svgui fork), which the panes apply together with the screen's pixel
 * ratio, so the steps look alike on a phone and on a desktop. Chosen,
 * a step takes effect at once and is remembered in the settings.
 *
 * The default is 150% on Android, where a thin line on a small screen
 * held at arm's length is hard to follow, and 100% elsewhere, where the
 * panes then draw exactly as they always have.
 */
class PlotSize : public QObject
{
    Q_OBJECT

public:
    /// Reads the setting and applies it to the view manager
    PlotSize(sv::ViewManager *viewManager, QObject *parent);

    /// The checkable steps, one of them checked, for a submenu
    QList<QAction *> getActions() const;

    int getPercent() const { return m_percent; }

    static QList<int> getSteps() { return { 100, 150, 200 }; }

    /// 150 on Android, 100 elsewhere
    static int getDefaultPercent();

public slots:
    /// Applies and remembers a step; anything else is ignored
    void setPercent(int percent);

private:
    void apply(int percent);

    sv::ViewManager *m_viewManager;
    QActionGroup *m_group;
    int m_percent;
};

#endif
