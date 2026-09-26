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

#ifndef TEST_TOUCH_MENU_STYLE_H
#define TEST_TOUCH_MENU_STYLE_H

// Tier 5: menus taller than the screen, as a phone has them
// (TouchMenuStyle): on the screen in one column, scrolled by a finger
// dragged over them, every item reachable, and a tap still a choice;
// menus and combo box lists clear of the system bars and of the top and
// bottom of the screen. The menus are real popups on the offscreen
// platform's screen; the mouse events are what Qt makes of a finger on
// Android.

#include "../TouchMenuStyle.h"

#include <QObject>
#include <QtTest>
#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QSignalSpy>

#include <memory>

class TestTouchMenuStyle : public QObject
{
    Q_OBJECT

    // Taller than the screen by some way: the phone's Edit menu is about
    // two screens high in landscape
    static constexpr int itemCount = 80;

    std::unique_ptr<TouchMenuStyle> m_style;
    std::unique_ptr<QMenu> m_menu;

    QRect screen() const {
        return QGuiApplication::primaryScreen()->availableGeometry();
    }

    // A menu of itemCount items with the touch style, popped up at the
    // top of the screen. The items are as wide as Tony's longest: laid
    // out in columns, as Qt's styles lay out a menu taller than the
    // screen, the columns would not fit across it either
    QMenu *longMenu() {
        m_menu.reset(new QMenu);
        m_menu->setStyle(m_style.get());
        for (int i = 0; i < itemCount; ++i) {
            m_menu->addAction(QString("Item %1: Save Session to Audio File "
                                      "Path").arg(i));
        }
        m_menu->popup(screen().topLeft());
        return m_menu.get();
    }

    QAction *item(int i) { return m_menu->actions().at(i); }

    int top(int i) { return m_menu->actionGeometry(item(i)).top(); }

    // On show: inside the menu, and across the screen, not off its side
    // as columns that do not fit are. (The menu itself fits the screen's
    // height, or nearly: the offscreen platform puts a popup two pixels
    // in from where Qt asks)
    bool shows(int i) {
        QRect r = m_menu->actionGeometry(item(i));
        QRect global(m_menu->mapToGlobal(r.topLeft()), r.size());
        return r.top() >= 0 && r.bottom() < m_menu->height() &&
            global.left() >= screen().left() &&
            global.right() <= screen().right();
    }

    // A finger put down at from and moved to to, a few pixels at a time,
    // then lifted there
    void drag(QPoint from, QPoint to) {
        QTest::mousePress(m_menu.get(), Qt::LeftButton, {}, from);
        QPoint step = (to - from) / 20;
        for (int i = 1; i <= 20; ++i) {
            QTest::mouseMove(m_menu.get(), from + step * i);
        }
        QTest::mouseRelease(m_menu.get(), Qt::LeftButton, {}, from + step * 20);
    }

private slots:
    void init() {
        m_style.reset(new TouchMenuStyle);
    }

    void cleanup() {
        m_menu.reset();
        m_style.reset();
    }

    void a_long_menu_is_on_the_screen_in_one_column() {
        QMenu *menu = longMenu();
        QVERIFY(QTest::qWaitForWindowExposed(menu));

        QVERIFY2(menu->height() <= screen().height(),
                 qPrintable(QString("%1 high on a screen %2 high")
                            .arg(menu->height()).arg(screen().height())));

        // One column: every item at the same place across, and the ones
        // that do not fit below, to be scrolled to
        for (int i = 1; i < itemCount; ++i) {
            QCOMPARE(menu->actionGeometry(item(i)).left(),
                     menu->actionGeometry(item(0)).left());
        }
        QVERIFY(shows(0));
        QVERIFY(!shows(itemCount - 1));
        QVERIFY(top(itemCount - 1) >= menu->height());

        // Arrows a finger can hit
        QVERIFY(m_style->pixelMetric(QStyle::PM_MenuScrollerHeight) >= 30);
    }

    void a_finger_dragged_up_scrolls_the_menu_and_chooses_nothing() {
        QMenu *menu = longMenu();
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        QSignalSpy triggered(menu, &QMenu::triggered);

        int row = menu->actionGeometry(item(0)).height();
        QVERIFY(row > 0);
        int before = top(10);

        QPoint centre = menu->rect().center();
        drag(centre, centre - QPoint(0, 10 * row));

        // Ten rows' drag, ten rows further on (give or take the arrow
        // strip that appears at the top once it has scrolled)
        int moved = before - top(10);
        QVERIFY2(moved >= 8 * row && moved <= 11 * row,
                 qPrintable(QString("moved %1 for rows of %2")
                            .arg(moved).arg(row)));
        QCOMPARE(triggered.count(), 0);
        QVERIFY(menu->isVisible());
        QVERIFY(!shows(0));
    }

    void every_item_can_be_reached_and_the_way_back_too() {
        QMenu *menu = longMenu();
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        QSignalSpy triggered(menu, &QMenu::triggered);

        QPoint low(menu->width() / 2, menu->height() * 3 / 4);
        QPoint high(menu->width() / 2, menu->height() / 4);

        for (int i = 0; i < 10 && !shows(itemCount - 1); ++i) {
            drag(low, high);
        }
        QVERIFY(shows(itemCount - 1));

        for (int i = 0; i < 10 && !shows(0); ++i) {
            drag(high, low);
        }
        QVERIFY(shows(0));

        QCOMPARE(triggered.count(), 0);
        QVERIFY(menu->isVisible());
    }

    void a_tap_chooses_an_item_as_before() {
        QMenu *menu = longMenu();
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        QSignalSpy triggered(menu, &QMenu::triggered);

        QPoint at = menu->actionGeometry(item(3)).center();
        QTest::mousePress(menu, Qt::LeftButton, {}, at);
        QTest::mouseRelease(menu, Qt::LeftButton, {}, at);

        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.at(0).at(0).value<QAction *>(), item(3));
    }

    void a_tap_after_scrolling_chooses_the_item_shown_there() {
        QMenu *menu = longMenu();
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        QSignalSpy triggered(menu, &QMenu::triggered);

        QPoint centre = menu->rect().center();
        int row = menu->actionGeometry(item(0)).height();
        int before = menu->actions().indexOf(menu->actionAt(centre));
        drag(centre, centre - QPoint(0, 20 * row));

        QAction *shown = menu->actionAt(centre);
        QVERIFY(shown);
        QVERIFY2(menu->actions().indexOf(shown) >= before + 18,
                 qPrintable(QString("item %1 there before, %2 after")
                            .arg(before)
                            .arg(menu->actions().indexOf(shown))));

        QTest::mousePress(menu, Qt::LeftButton, {}, centre);
        QTest::mouseRelease(menu, Qt::LeftButton, {}, centre);

        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.at(0).at(0).value<QAction *>(), shown);
    }

    // --- Clear of the system bars and the screen's edges ---

    static QRect onScreen(QWidget *w) {
        return QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
    }

    // Inside area, give or take the two pixels the offscreen platform
    // moves a popup by
    static bool within(QRect r, QRect area) {
        return area.adjusted(-2, -2, 2, 2).contains(r);
    }

    static QString describe(QRect r, QRect area) {
        return QString("%1,%2 %3x%4 in %5,%6 %7x%8")
            .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())
            .arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height());
    }

    void a_long_menu_keeps_clear_of_the_top_and_bottom_and_still_scrolls() {
        m_style->setEdgeMargin(60);
        QMenu *menu = longMenu();
        QVERIFY(QTest::qWaitForWindowExposed(menu));

        QRect usable = m_style->usableArea(menu);
        QCOMPARE(usable, screen().adjusted(0, 60, 0, -60));
        QVERIFY2(within(onScreen(menu), usable),
                 qPrintable(describe(onScreen(menu), usable)));

        QPoint low(menu->width() / 2, menu->height() * 3 / 4);
        QPoint high(menu->width() / 2, menu->height() / 4);
        for (int i = 0; i < 10 && !shows(itemCount - 1); ++i) {
            drag(low, high);
        }
        QVERIFY(shows(itemCount - 1));
        QVERIFY2(within(onScreen(menu), usable),
                 qPrintable(describe(onScreen(menu), usable)));
    }

    void a_short_menu_at_the_very_top_is_moved_down() {
        m_style->setEdgeMargin(60);
        m_menu.reset(new QMenu);
        m_menu->setStyle(m_style.get());
        m_menu->addAction("Open...");
        m_menu->addAction("Open Recent");
        m_menu->addAction("Save Session");
        m_menu->popup(screen().topLeft());
        QVERIFY(QTest::qWaitForWindowExposed(m_menu.get()));

        QRect usable = m_style->usableArea(m_menu.get());
        QVERIFY2(within(onScreen(m_menu.get()), usable),
                 qPrintable(describe(onScreen(m_menu.get()), usable)));
        QVERIFY(onScreen(m_menu.get()).top() >= screen().top() + 58);
    }

    void menus_keep_inside_the_main_windows_safe_area() {
        // The offscreen platform has no system bars and no safe area
        // margins: a main window short of the screen on every side
        // stands for the part the bars leave
        QWidget window;
        window.setGeometry(screen().adjusted(40, 80, -40, -100));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        m_style->setSafeAreaWindow(&window);

        QMenu *menu = longMenu();
        QVERIFY(QTest::qWaitForWindowExposed(menu));

        QRect usable = m_style->usableArea(menu);
        QCOMPARE(usable, onScreen(&window) & screen());
        QVERIFY2(within(onScreen(menu), usable),
                 qPrintable(describe(onScreen(menu), usable)));

        m_menu.reset();
    }

    void a_combo_boxs_list_keeps_clear_too() {
        m_style->setEdgeMargin(60);

        // At the top of the screen, as the take box is in the compact
        // layout's toolbar, its list longer than the screen is high
        QComboBox combo;
        combo.setStyle(m_style.get());
        for (int i = 0; i < itemCount; ++i) {
            combo.addItem(QString("Take %1").arg(i));
        }
        combo.setCurrentIndex(itemCount / 2);
        QWidget *list = combo.view()->parentWidget();
        list->setStyle(m_style.get());
        combo.move(screen().topLeft());
        combo.show();
        QVERIFY(QTest::qWaitForWindowExposed(&combo));

        combo.showPopup();
        QVERIFY(QTest::qWaitForWindowExposed(list));

        QRect usable = m_style->usableArea(list);
        QVERIFY2(within(onScreen(list), usable),
                 qPrintable(describe(onScreen(list), usable)));

        combo.hidePopup();
    }

    void without_margins_menus_are_placed_as_before() {
        // As the desktop has it: the style's own margin, and the screen
        TouchMenuStyle plain;
        m_menu.reset(new QMenu);
        m_menu->addAction("Item");
        QCOMPARE(plain.usableArea(m_menu.get()), screen());
        QCOMPARE(plain.pixelMetric(QStyle::PM_MenuDesktopFrameWidth, nullptr,
                                   m_menu.get()),
                 plain.baseStyle()->pixelMetric
                 (QStyle::PM_MenuDesktopFrameWidth, nullptr, m_menu.get()));
    }
};

#endif
