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

#ifndef TONY_TOUCH_MENU_STYLE_H
#define TONY_TOUCH_MENU_STYLE_H

#include <QPointer>
#include <QProxyStyle>

class QMenu;

/**
 * Menus for a touch screen, as the application's style on Android (the
 * style it wraps does everything else).
 *
 * A menu taller than the screen scrolls, one column high, where Qt's
 * styles would lay it out in columns and let the ones that do not fit
 * run off the screen: QMenu scrolls if its style answers
 * SH_Menu_Scrollable. The scroll arrows it then shows are made tall
 * enough for a finger. A finger dragged up or down a menu scrolls it as
 * a phone's lists scroll, a row at a time, through the wheel events
 * QMenu scrolls by; the lift that ends a drag chooses nothing. A tap
 * chooses an item as before.
 *
 * Menus and combo box lists stay inside the part of the screen a popup
 * may use (PopupArea): clear of the system bars an app drawn edge to
 * edge lies under, which the main window's safe area margins give, and
 * of the top and bottom edges of the screen by edgeMargin.
 *
 * Built everywhere and tested on the desktop, whose style is left as it
 * is: only main() on Android installs it.
 */
class TouchMenuStyle : public QProxyStyle
{
    Q_OBJECT

public:
    // The application's style, or base if given, with menus for touch
    TouchMenuStyle(QStyle *base = nullptr);

    // The window whose safe area popups keep inside: the main window,
    // which covers the screen on a phone. None by default
    void setSafeAreaWindow(QWidget *window);

    // How far popups keep from the top and bottom of the screen, in
    // pixels: 0 by default
    void setEdgeMargin(int pixels);

    // Where popup may be, in global coordinates
    QRect usableArea(const QWidget *popup) const;

    int styleHint(StyleHint hint,
                  const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;

    int pixelMetric(PixelMetric metric,
                    const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;

    // Each menu the style is given watches for a finger dragged over it
    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;
    using QProxyStyle::polish;
    using QProxyStyle::unpolish;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    // The height a finger moves to scroll the menu by one row
    static int rowHeight(QMenu *menu);

    // Whether widget is a combo box's list, a popup window of its own
    static bool isComboList(const QWidget *widget);

    // The menu pressed on, where, how far the scrolling has followed the
    // finger, and whether the press has become a drag
    QPointer<QMenu> m_menu;
    double m_pressY;
    double m_scrolledToY;
    bool m_dragging;

    QPointer<QWidget> m_safeAreaWindow;
    int m_edgeMargin;
    mutable QRect m_lastUsable;
};

#endif
