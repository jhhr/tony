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

#include "InputLevelMeter.h"
#include "InputLevelFeed.h"
#include "InputLevel.h"
#include "VoiceThreshold.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolBar>

#include <algorithm>
#include <cmath>

namespace {

// The bar's colours: green where a singer's peaks belong (the advice
// puts them near -10 dBFS), amber close to full scale, red at it
const double amberFromDb = -6.0;
const double redFromDb = -1.0;

// Labels on a meter tall enough for them
const double labelledLevels[] = { -60.0, -40.0, -20.0, -10.0, -6.0, 0.0 };

}

InputLevelMeter::InputLevelMeter(InputLevelFeed *feed, QWidget *parent) :
    QWidget(parent),
    m_feed(feed),
    m_labelled(false)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setToolTip(tr("Input level, the peak in dBFS. The tick is the voice "
                  "threshold. The light at the end turns red when the "
                  "input reaches full scale: a click puts it out"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    if (m_feed) {
        connect(m_feed, &InputLevelFeed::changed,
                this, QOverload<>::of(&QWidget::update));
    }
}

InputLevelMeter::~InputLevelMeter()
{
}

QSize
InputLevelMeter::sizeHint() const
{
    // As tall as its toolbar's buttons
    int height = fontMetrics().height() + 8;
    if (auto toolBar = qobject_cast<QToolBar *>(parentWidget())) {
        height = std::max(height, toolBar->iconSize().height() + 6);
    }
    return QSize(std::max(80, height * 3), height);
}

QSize
InputLevelMeter::minimumSizeHint() const
{
    return QSize(60, 12);
}

QRect
InputLevelMeter::clipLightRect() const
{
    // Beside the bar, as tall as it, and no bigger than a large letter
    int bar = height() - 4;
    if (labelled()) bar = height() - fontMetrics().height() - 6;
    const int side = std::max(6, std::min({ bar, width() / 6,
                                            fontMetrics().height() * 2 }));
    return QRect(width() - side - 2, 2 + (bar - side) / 2, side, side);
}

void
InputLevelMeter::setLabelled(bool labelled)
{
    m_labelled = labelled;
    update();
}

bool
InputLevelMeter::labelled() const
{
    return m_labelled && height() >= 2 * fontMetrics().height() + 8;
}

QRect
InputLevelMeter::scaleRect() const
{
    const QRect light = clipLightRect();
    int top = 2, bottom = height() - 2;
    if (labelled()) bottom = height() - fontMetrics().height() - 4;
    return QRect(2, top, std::max(1, light.left() - 6), bottom - top);
}

int
InputLevelMeter::xFor(double dbfs) const
{
    const QRect scale = scaleRect();
    const double floor = InputLevel::Meter::kFloorDb;
    const double share = (std::min(0.0, std::max(floor, dbfs)) - floor) /
        (0.0 - floor);
    return scale.left() + int(std::lround(share * scale.width()));
}

QColor
InputLevelMeter::clipColour()
{
    return QColor(225, 0, 0);
}

QColor
InputLevelMeter::barColour(double dbfs)
{
    if (dbfs > redFromDb) return clipColour();
    if (dbfs > amberFromDb) return QColor(240, 160, 30);
    return QColor(50, 170, 60);
}

QColor
InputLevelMeter::thresholdColour()
{
    return QColor(30, 60, 190);
}

void
InputLevelMeter::paintEvent(QPaintEvent *)
{
    QPainter paint(this);
    paint.fillRect(rect(), palette().color(QPalette::Window));

    const QRect scale = scaleRect();
    paint.fillRect(scale, QColor(40, 40, 40));

    const InputLevelFeed *feed = m_feed;
    const std::int64_t ms = feed ? feed->now() : 0;
    const double bar = feed ? feed->meter().bar(ms) :
        InputLevel::Meter::kFloorDb;
    const double hold = feed ? feed->meter().hold(ms) :
        InputLevel::Meter::kFloorDb;

    // The bar, in its zones' colours
    const double floor = InputLevel::Meter::kFloorDb;
    const double zones[][2] = { { floor, amberFromDb },
                                { amberFromDb, redFromDb },
                                { redFromDb, 0.0 } };
    for (const auto &zone : zones) {
        if (bar <= zone[0]) break;
        const int from = xFor(zone[0]);
        const int to = xFor(std::min(bar, zone[1]));
        if (to > from) {
            paint.fillRect(QRect(from, scale.top(), to - from, scale.height()),
                           barColour((zone[0] + zone[1]) / 2.0));
        }
    }

    // The hold: the highest peak lately, falling behind the bar
    if (hold > floor) {
        const int x = xFor(hold);
        paint.fillRect(QRect(std::max(scale.left(), x - 2), scale.top(),
                             2, scale.height()), barColour(hold));
    }

    // The voice threshold
    const double threshold = feed ? feed->getThreshold() : VoiceThreshold::kOff;
    if (VoiceThreshold::isOn(threshold)) {
        const int x = xFor(threshold);
        paint.fillRect(QRect(x - 1, 0, 2, scale.bottom() + 2),
                       thresholdColour());
    }

    // The clip light
    const QRect light = clipLightRect();
    paint.setRenderHint(QPainter::Antialiasing, true);
    paint.setPen(QPen(palette().color(QPalette::Mid), 1));
    paint.setBrush((feed && feed->isClipped()) ? clipColour() :
                   QColor(85, 85, 85));
    paint.drawRoundedRect(QRectF(light).adjusted(0.5, 0.5, -0.5, -0.5),
                          2.0, 2.0);
    paint.setRenderHint(QPainter::Antialiasing, false);

    if (labelled()) {
        paint.setPen(palette().color(QPalette::WindowText));
        const int top = scale.bottom() + 3;
        const int gap = fontMetrics().averageCharWidth();
        int lastRight = -gap;
        for (double db : labelledLevels) {
            const QString text = (db < 0.0 ? QString(QChar(0x2212)) : QString()) +
                QString::number(std::fabs(db));
            const int w = fontMetrics().horizontalAdvance(text);
            int x = xFor(db) - w / 2;
            x = std::max(0, std::min(x, scale.right() + 1 - w));
            // A label with no room left beside the one before is left out
            if (x < lastRight + gap) continue;
            paint.drawText(QRect(x, top, w, fontMetrics().height()),
                           Qt::AlignCenter, text);
            lastRight = x + w;
        }
    }
}

void
InputLevelMeter::mousePressEvent(QMouseEvent *e)
{
    // Anywhere on it, not only on the light: a finger is wider than that
    if (m_feed) m_feed->setClipped(false);
    e->accept();
}

InputLevelMeterAction::InputLevelMeterAction(InputLevelFeed *feed,
                                             QObject *parent) :
    QWidgetAction(parent),
    m_feed(feed)
{
    setText(tr("Input Level"));
}

QWidget *
InputLevelMeterAction::createWidget(QWidget *parent)
{
    return new InputLevelMeter(m_feed, parent);
}
