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

#ifndef TONY_INPUT_LEVEL_METER_H
#define TONY_INPUT_LEVEL_METER_H

#include <QPointer>
#include <QWidget>
#include <QWidgetAction>

class InputLevelFeed;

/**
 * An input meter: the peak of the input in dBFS, from
 * InputLevel::Meter::kFloorDb to 0, as a bar with a hold that falls
 * behind it; a tick at the voice threshold, when it is on; and a clip
 * light at the right end.  A click on it puts the clip light out.
 *
 * It draws what its InputLevelFeed holds, and only when the feed says
 * something has changed.  Its height is its toolbar's buttons'.  Check
 * Input Level's is large, and labels its scale.
 *
 * The threshold compares the level of a half window, which a voice's
 * peaks stand some 10 dB over: a voice whose peaks only reach the tick
 * is under the threshold (docs/recording.md, "The input level").
 */
class InputLevelMeter : public QWidget
{
    Q_OBJECT

public:
    explicit InputLevelMeter(InputLevelFeed *feed, QWidget *parent = nullptr);
    virtual ~InputLevelMeter();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// Label the scale under the bar, given the height for it
    void setLabelled(bool labelled);

    /// Where the clip light is drawn
    QRect clipLightRect() const;

    /// Where a level in dBFS is on the scale
    int xFor(double dbfs) const;

    /// The colours it draws with, for the tests that look at it
    static QColor clipColour();
    static QColor barColour(double dbfs);
    static QColor thresholdColour();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QPointer<InputLevelFeed> m_feed;
    bool m_labelled;

    QRect scaleRect() const;
    bool labelled() const;
};

/**
 * The meter as a toolbar's action: each toolbar it is added to (the
 * window's and the compact layout's) gets a meter of its own, all showing
 * the one feed
 */
class InputLevelMeterAction : public QWidgetAction
{
    Q_OBJECT

public:
    InputLevelMeterAction(InputLevelFeed *feed, QObject *parent);

protected:
    QWidget *createWidget(QWidget *parent) override;

private:
    QPointer<InputLevelFeed> m_feed;
};

#endif
