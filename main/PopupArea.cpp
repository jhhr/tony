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

#include "PopupArea.h"

#include <algorithm>
#include <cmath>

QRect
PopupArea::usable(QRect available, QRect window, QMargins safeArea,
                  int edgeMargin)
{
    if (!available.isValid()) return available;

    QRect area = available;
    if (!window.isNull()) {
        area &= window.marginsRemoved(safeArea);
    }

    edgeMargin = std::max(0, edgeMargin);
    area.setTop(std::max(area.top(), available.top() + edgeMargin));
    area.setBottom(std::min(area.bottom(), available.bottom() - edgeMargin));

    if (!area.isValid()) return available;
    return area;
}

int
PopupArea::menuFrame(QRect available, QRect usable, int base)
{
    int frame = std::max(0, base);
    if (!available.isValid() || !usable.isValid()) return frame;

    frame = std::max(frame, usable.left() - available.left());
    frame = std::max(frame, usable.top() - available.top());
    frame = std::max(frame, available.right() - usable.right());
    frame = std::max(frame, available.bottom() - usable.bottom());
    return frame;
}

QRect
PopupArea::fit(QRect rect, QRect usable)
{
    if (!usable.isValid()) return rect;

    rect.setWidth(std::min(rect.width(), usable.width()));
    rect.setHeight(std::min(rect.height(), usable.height()));

    if (rect.right() > usable.right()) rect.moveRight(usable.right());
    if (rect.left() < usable.left()) rect.moveLeft(usable.left());
    if (rect.bottom() > usable.bottom()) rect.moveBottom(usable.bottom());
    if (rect.top() < usable.top()) rect.moveTop(usable.top());
    return rect;
}

QRect
PopupArea::place(QSize size, QRect usable, QPoint topLeft, bool centre)
{
    QRect rect(topLeft, size);
    if (centre && usable.isValid()) {
        rect.moveTopLeft(QPoint(usable.left() + (usable.width() - size.width()) / 2,
                                usable.top() + (usable.height() - size.height()) / 2));
    }
    return fit(rect, usable);
}

int
PopupArea::pixels(double mm, double dotsPerInch)
{
    if (mm <= 0 || dotsPerInch <= 0) return 0;
    return int(std::lround(mm * dotsPerInch / 25.4));
}

int
PopupArea::fingerWidth(double dotsPerInch)
{
    if (dotsPerInch < 100 || dotsPerInch > 300) dotsPerInch = 160;
    return pixels(8, dotsPerInch);
}
