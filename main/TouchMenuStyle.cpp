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

#include "TouchMenuStyle.h"
#include "PopupArea.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QWheelEvent>
#include <QWindow>

#include <cmath>
#include <iostream>

TouchMenuStyle::TouchMenuStyle(QStyle *base) :
    QProxyStyle(base),
    m_pressY(0),
    m_scrolledToY(0),
    m_dragging(false),
    m_edgeMargin(0)
{
}

void
TouchMenuStyle::setSafeAreaWindow(QWidget *window)
{
    m_safeAreaWindow = window;
}

void
TouchMenuStyle::setEdgeMargin(int pixels)
{
    m_edgeMargin = pixels;
}

QRect
TouchMenuStyle::usableArea(const QWidget *popup) const
{
    QScreen *screen = (popup ? popup->screen() : nullptr);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect();

    // Android's available geometry is the whole of the activity, bars
    // and all (qandroidplatformscreen.cpp, handleLayoutSizeChanged() in
    // androidjnimain.cpp); the bars are the window's safe area margins
    QRect window;
    QMargins safeArea;
    if (QWidget *w = m_safeAreaWindow.data()) {
        if (w->isVisible()) {
            window = QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            if (QWindow *handle = w->windowHandle()) {
                safeArea = handle->safeAreaMargins();
            }
#endif
        }
    }

    QRect available = screen->availableGeometry();
    QRect usable = PopupArea::usable(available, window, safeArea,
                                     m_edgeMargin);

    // Said when it changes, for a report from a phone
    if (usable != m_lastUsable) {
        m_lastUsable = usable;
        std::cerr << "TouchMenuStyle: popups within " << usable.x() << ","
                  << usable.y() << " " << usable.width() << "x"
                  << usable.height() << " of " << available.width() << "x"
                  << available.height() << ", safe area margins "
                  << safeArea.left() << "," << safeArea.top() << ","
                  << safeArea.right() << "," << safeArea.bottom()
                  << std::endl;
    }
    return usable;
}

bool
TouchMenuStyle::isComboList(const QWidget *widget)
{
    return widget && widget->windowType() == Qt::Popup &&
        qobject_cast<const QComboBox *>(widget->parentWidget());
}

int
TouchMenuStyle::styleHint(StyleHint hint,
                          const QStyleOption *option,
                          const QWidget *widget,
                          QStyleHintReturn *returnData) const
{
    if (hint == SH_Menu_Scrollable) return 1;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

int
TouchMenuStyle::pixelMetric(PixelMetric metric,
                            const QStyleOption *option,
                            const QWidget *widget) const
{
    int value = QProxyStyle::pixelMetric(metric, option, widget);

    // A strip ten pixels high at the menu's edge is where a finger has to
    // press for the arrows to scroll it; missing it chooses the item
    // beside it
    if (metric == PM_MenuScrollerHeight) return 3 * value;

    // QMenu keeps this far from every side of the screen's available
    // geometry, when it places a menu, sizes one too tall to fit and
    // scrolls it (QMenuPrivate::popup(), scrollMenu() in qmenu.cpp): so
    // the whole of what it does stays inside the usable area
    if (metric == PM_MenuDesktopFrameWidth &&
        qobject_cast<const QMenu *>(widget)) {
        QScreen *screen = widget->screen();
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            return PopupArea::menuFrame(screen->availableGeometry(),
                                        usableArea(widget), value);
        }
    }

    return value;
}

void
TouchMenuStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    if (qobject_cast<QMenu *>(widget) || isComboList(widget)) {
        widget->installEventFilter(this);
    }
}

void
TouchMenuStyle::unpolish(QWidget *widget)
{
    if (qobject_cast<QMenu *>(widget) || isComboList(widget)) {
        widget->removeEventFilter(this);
    }
    QProxyStyle::unpolish(widget);
}

int
TouchMenuStyle::rowHeight(QMenu *menu)
{
    // The first item that is not a separator, which are thinner
    for (QAction *action : menu->actions()) {
        if (!action->isVisible() || action->isSeparator()) continue;
        int height = menu->actionGeometry(action).height();
        if (height > 0) return height;
    }
    return std::max(8, menu->fontMetrics().height());
}

bool
TouchMenuStyle::eventFilter(QObject *object, QEvent *event)
{
    // A combo box's list is placed by QComboBox::showPopup() inside the
    // available geometry and then shown: moved into the usable area
    // before it is, as its list scrolls whatever its height
    if (event->type() == QEvent::Show && object->isWidgetType() &&
        isComboList(static_cast<QWidget *>(object))) {
        QWidget *list = static_cast<QWidget *>(object);
        QRect fitted = PopupArea::fit(list->geometry(), usableArea(list));
        if (fitted != list->geometry()) list->setGeometry(fitted);
        return false;
    }

    QMenu *menu = qobject_cast<QMenu *>(object);
    if (!menu) return false;

    switch (event->type()) {

    case QEvent::MouseButtonPress: {
        // Passed on: the press may be a tap, and QMenu takes one on its
        // scroll arrows as its own
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
        if (e->button() != Qt::LeftButton) break;
        m_menu = menu;
        m_pressY = m_scrolledToY = e->globalPosition().y();
        m_dragging = false;
        break;
    }

    case QEvent::MouseMove: {
        QMouseEvent *e = static_cast<QMouseEvent *>(event);
        if (m_menu != menu || !(e->buttons() & Qt::LeftButton)) break;

        double y = e->globalPosition().y();

        if (!m_dragging) {
            // Until the finger has gone this far, QMenu follows it
            if (std::abs(y - m_pressY) < QApplication::startDragDistance()) {
                break;
            }
            m_dragging = true;
            // Nothing is chosen by a drag, and a submenu the press opened
            // is closed
            menu->setActiveAction(nullptr);
        }

        // A row for each row's height the finger has moved: the finger
        // moving down pulls the items above into view, as a wheel turned
        // up does. The wheel event goes where QMenu takes it, inside it
        int row = rowHeight(menu);
        QPointF centre(menu->rect().center());
        while (std::abs(y - m_scrolledToY) >= row) {
            int direction = (y > m_scrolledToY ? 1 : -1);
            QWheelEvent wheel(centre, menu->mapToGlobal(centre), QPoint(),
                              QPoint(0, 120 * direction), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(menu, &wheel);
            m_scrolledToY += direction * row;
        }

        // Not QMenu's: it would pick out the item under the finger
        return true;
    }

    case QEvent::MouseButtonRelease: {
        if (m_menu != menu) break;
        bool dragged = m_dragging;
        m_menu = nullptr;
        m_dragging = false;
        // The end of a drag is not a choice
        if (dragged) return true;
        break;
    }

    case QEvent::Hide:
        if (m_menu == menu) {
            m_menu = nullptr;
            m_dragging = false;
        }
        break;

    default:
        break;
    }

    return false;
}
