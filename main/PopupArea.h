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

#ifndef TONY_POPUP_AREA_H
#define TONY_POPUP_AREA_H

#include <QMargins>
#include <QRect>

/**
 * Where on a phone's screen menus and other popups may go.
 *
 * Qt places a popup inside the screen's available geometry, and on
 * Android that is the whole of the activity, system bars included: from
 * Android 15 on an app draws edge to edge, under the status bar, the
 * navigation bar and the camera's cutout, and is told of them only as
 * its windows' safe area margins (QWindow::safeAreaMargins()), which
 * the main window's layout keeps clear of and a popup's placement does
 * not. A tap on a menu item under the status bar goes to the status
 * bar. Plain arithmetic, for TouchMenuStyle, which applies it.
 */
class PopupArea
{
public:
    /**
     * The part of available (the screen's available geometry) a popup
     * may cover: inside window (the application's main window, in the
     * same coordinates) less safeArea, its safe area margins, and at
     * least edgeMargin from the top and bottom of available, where a
     * finger does not reach well. A null window stands for all of
     * available. Never empty: if the margins leave nothing, available.
     */
    static QRect usable(QRect available, QRect window, QMargins safeArea,
                        int edgeMargin);

    /**
     * The margin QMenu keeps between a menu and every side of the screen
     * (the style's PM_MenuDesktopFrameWidth, one number for all four
     * sides) for its menus to lie inside usable: the widest of the four
     * gaps between available and usable, and at least base, the style's
     * own. QMenu places, sizes and scrolls a menu by that margin alone,
     * so a menu is kept inside by it rather than moved after it is
     * placed, which would leave QMenu's scrolling out of step.
     */
    static int menuFrame(QRect available, QRect usable, int base);

    /**
     * rect moved, and shortened or narrowed where it has to be, to lie
     * inside usable: for a combo box's list, which scrolls whatever its
     * height.
     */
    static QRect fit(QRect rect, QRect usable);

    /**
     * Where a dialog of size goes inside usable: centred in it when it
     * is being shown afresh (centre), else where topLeft puts it, as the
     * user may have moved it; and then fit() into usable. Qt centres a
     * dialog over its parent inside the screen's available geometry,
     * which on a phone takes in the system bars.
     */
    static QRect place(QSize size, QRect usable, QPoint topLeft, bool centre);

    /**
     * A length in pixels: mm millimetres at dotsPerInch.
     */
    static int pixels(double mm, double dotsPerInch);

    /**
     * About a finger's width, 8 mm, in Qt's pixels on a phone whose
     * screen has dotsPerInch of them to the inch. That is near 160
     * (Android's dp, which Qt's pixels are there); a figure far from it,
     * as some phones report, is taken to be 160.
     */
    static int fingerWidth(double dotsPerInch);
};

#endif
