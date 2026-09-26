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

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>

#include <cmath>

TouchMenuStyle::TouchMenuStyle(QStyle *base) :
    QProxyStyle(base),
    m_pressY(0),
    m_scrolledToY(0),
    m_dragging(false)
{
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

    return value;
}

void
TouchMenuStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    if (QMenu *menu = qobject_cast<QMenu *>(widget)) {
        menu->installEventFilter(this);
    }
}

void
TouchMenuStyle::unpolish(QWidget *widget)
{
    if (QMenu *menu = qobject_cast<QMenu *>(widget)) {
        menu->removeEventFilter(this);
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
