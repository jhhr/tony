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

#ifndef TEST_POPUP_AREA_H
#define TEST_POPUP_AREA_H

// Tier 1: where menus may go on a phone drawn edge to edge (PopupArea):
// inside the main window's safe area, clear of the top and bottom of
// the screen, and the one margin QMenu is given for that. The figures
// are a phone's in landscape, in device-independent pixels: the screen
// 915 by 412, a camera cutout at the left, the status bar at the top,
// gesture navigation at the bottom.

#include "../PopupArea.h"

#include <QObject>
#include <QtTest>

class TestPopupArea : public QObject
{
    Q_OBJECT

    const QRect screen = QRect(0, 0, 915, 412);
    const QMargins bars = QMargins(35, 24, 0, 16);

private slots:
    void a_phone_in_landscape_keeps_popups_off_its_bars_and_edges() {
        QRect usable = PopupArea::usable(screen, screen, bars, 50);

        // Clear of the cutout at the side, and of the top and bottom
        // edges by the margin, which is wider than either bar
        QCOMPARE(usable, QRect(QPoint(35, 50), QPoint(914, 361)));
    }

    void a_bar_wider_than_the_margin_is_kept_clear_of() {
        QRect usable = PopupArea::usable(screen, screen,
                                         QMargins(0, 60, 48, 70), 50);
        QCOMPARE(usable, QRect(QPoint(0, 60), QPoint(914 - 48, 411 - 70)));
    }

    void a_window_short_of_the_screen_is_kept_inside() {
        // In split screen, Tony has part of it
        QRect window(0, 0, 915, 200);
        QRect usable = PopupArea::usable(screen, window, QMargins(0, 24, 0, 0),
                                         10);
        QCOMPARE(usable, QRect(QPoint(0, 24), QPoint(914, 199)));
    }

    void without_a_window_the_screen_less_its_edges() {
        QCOMPARE(PopupArea::usable(screen, QRect(), bars, 50),
                 QRect(QPoint(0, 50), QPoint(914, 361)));
        QCOMPARE(PopupArea::usable(screen, QRect(), QMargins(), 0), screen);

        // As on the desktop, where the screen does not start at 0
        QRect desktop(0, 30, 1920, 1050);
        QCOMPARE(PopupArea::usable(desktop, QRect(), QMargins(), 0), desktop);
        QCOMPARE(PopupArea::usable(desktop, desktop, QMargins(), 20),
                 QRect(QPoint(0, 50), QPoint(1919, 1059)));
    }

    void margins_that_leave_nothing_leave_the_screen() {
        QCOMPARE(PopupArea::usable(screen, screen, bars, 300), screen);
        QCOMPARE(PopupArea::usable(screen, QRect(2000, 0, 10, 10), bars, 0),
                 screen);
    }

    void qmenu_is_given_the_widest_gap_as_its_margin() {
        QRect usable = PopupArea::usable(screen, screen, bars, 50);
        QCOMPARE(PopupArea::menuFrame(screen, usable, 0), 50);

        // The style's own, if wider
        QCOMPARE(PopupArea::menuFrame(screen, usable, 60), 60);

        // A bar at the side wider than the top and bottom margins
        usable = PopupArea::usable(screen, screen, QMargins(0, 24, 80, 0), 50);
        QCOMPARE(PopupArea::menuFrame(screen, usable, 0), 80);

        // Nothing to keep clear of
        QCOMPARE(PopupArea::menuFrame(screen, screen, 0), 0);
        QCOMPARE(PopupArea::menuFrame(screen, QRect(), 3), 3);
    }

    // Where QMenu puts a menu with that margin: inside the usable area
    // (its placement in qmenu.cpp, QMenuPrivate::popup(), for a menu
    // taller than the screen popped up at its top, which scrolls)
    void a_menu_placed_by_that_margin_is_inside() {
        QRect usable = PopupArea::usable(screen, screen, bars, 50);
        int frame = PopupArea::menuFrame(screen, usable, 0);

        int top = screen.top() + frame;
        int height = screen.bottom() - frame * 2 - top;
        QRect menu(screen.left() + frame, top, 300, height);
        QVERIFY(usable.contains(menu));
    }

    void a_combo_boxs_list_is_moved_and_shortened_to_fit() {
        QRect usable(QPoint(35, 50), QPoint(914, 361));

        // Up from the box near the top, under the status bar
        QCOMPARE(PopupArea::fit(QRect(100, 10, 200, 150), usable),
                 QRect(100, 50, 200, 150));
        // Down past the bottom
        QCOMPARE(PopupArea::fit(QRect(100, 300, 200, 150), usable),
                 QRect(100, 212, 200, 150));
        // Taller than the whole area
        QCOMPARE(PopupArea::fit(QRect(100, 0, 200, 500), usable),
                 QRect(100, 50, 200, 312));
        // Off the side, and wider than the area
        QCOMPARE(PopupArea::fit(QRect(0, 100, 200, 100), usable),
                 QRect(35, 100, 200, 100));
        QCOMPARE(PopupArea::fit(QRect(0, 100, 1000, 100), usable),
                 QRect(35, 100, 880, 100));
        // Inside already
        QCOMPARE(PopupArea::fit(QRect(100, 100, 200, 100), usable),
                 QRect(100, 100, 200, 100));
        // Nothing to fit into
        QCOMPARE(PopupArea::fit(QRect(1, 2, 3, 4), QRect()), QRect(1, 2, 3, 4));
    }

    void millimetres_become_pixels() {
        QCOMPARE(PopupArea::pixels(8, 160), 50);
        QCOMPARE(PopupArea::pixels(25.4, 96), 96);
        QCOMPARE(PopupArea::pixels(0, 160), 0);
        QCOMPARE(PopupArea::pixels(8, 0), 0);
        QCOMPARE(PopupArea::pixels(-1, 160), 0);
    }

    void a_fingers_width_is_eight_millimetres() {
        QCOMPARE(PopupArea::fingerWidth(160), 50);
        QCOMPARE(PopupArea::fingerWidth(140), 44);
        // Figures no phone's screen has, in Qt's pixels
        QCOMPARE(PopupArea::fingerWidth(0), 50);
        QCOMPARE(PopupArea::fingerWidth(480), 50);
    }
};

#endif
