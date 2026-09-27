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

#ifndef TONY_LYRICS_SIZE_H
#define TONY_LYRICS_SIZE_H

#include <QList>
#include <QObject>

class QAction;
class QActionGroup;

/**
 * How large the lyrics' words are drawn, as View > Lyrics Size sets
 * it: a share of the size svgui's lyrics layer gives them, which
 * grows with the zoom from twice the view's font to four times.
 * Chosen, a step takes effect at once (textScaleChanged()) and is
 * remembered in the settings.
 *
 * The default is 50% on Android, where the whole size left room for
 * only three or four words in the pane, less than a verse, unless it
 * was zoomed far in; 100% elsewhere.
 */
class LyricsSize : public QObject
{
    Q_OBJECT

public:
    /// Reads the setting; the scale is there from the start
    explicit LyricsSize(QObject *parent);

    /// The checkable steps, one of them checked, for a submenu
    QList<QAction *> getActions() const;

    int getPercent() const { return m_percent; }
    double getTextScale() const { return m_percent / 100.0; }

    static QList<int> getSteps() { return { 35, 50, 65, 80, 100 }; }

    /// 50 on Android, 100 elsewhere
    static int getDefaultPercent();

signals:
    void textScaleChanged(double scale);

public slots:
    /// Applies and remembers a step; anything else is ignored
    void setPercent(int percent);

private:
    void apply(int percent);

    QActionGroup *m_group;
    int m_percent;
};

#endif
