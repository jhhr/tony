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

#ifndef TONY_INPUT_LEVEL_FEED_H
#define TONY_INPUT_LEVEL_FEED_H

#include "InputLevel.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <cstdint>

namespace sv {
class AudioCallbackRecordTarget;
class ViewManager;
}

/**
 * What the input meters show (InputLevelMeter, one in each toolbar, and
 * Check Input Level's): the peaks the device reports for its inputs,
 * as the record target keeps them, read on the GUI thread.  Nothing is
 * added to the audio callback: the device already measures each block's
 * peak for the level meters (PortAudioIO, OboeAudioIO).
 *
 * The record target's levels have one reader each: a read takes the peak
 * since the last one and clears it.  While a take is recorded, its lead-in
 * included, the view manager reads them, for its monitoringLevelsChanged
 * signal, and this takes them from that signal, which says only when they
 * change: they are what it said last until it says otherwise, as a
 * steady tone's, or an input held at full scale, are.  At other times this
 * reads them itself, which it can only while the device is open with its
 * input and running (after the first take, or once Check Input Level has
 * opened it).
 *
 * Read every kIntervalMs, which is also as often as the meters are drawn
 * again, and only while something on them moves: a phone's GUI thread
 * has little to spare during a take.
 */
class InputLevelFeed : public QObject
{
    Q_OBJECT

public:
    /// 20 a second
    static constexpr int kIntervalMs = 50;

    /// How far the bar or the hold moves before the meters are drawn
    /// again: about a pixel of a toolbar's meter
    static constexpr double kStepDb = 0.5;

    InputLevelFeed(sv::ViewManager *viewManager,
                   sv::AudioCallbackRecordTarget *target,
                   QObject *parent = nullptr);
    virtual ~InputLevelFeed();

    /// The input shown (InputChannel): the one chosen, or both
    void setChannel(int channel);
    int getChannel() const { return m_channel; }

    /// The voice threshold, drawn as a tick when it is on
    void setThreshold(double dbfs);
    double getThreshold() const { return m_threshold; }

    const InputLevel::Meter &meter() const { return m_meter; }

    /// The meter's clock, in ms
    std::int64_t now() const { return m_clock.elapsed(); }

    /// The clip light: lit by a peak at full scale or by a take's scan,
    /// put out as a take starts or when the meter is clicked
    bool isClipped() const { return m_meter.clipped(); }
    void setClipped(bool clipped);

    /// Whether the input has delivered anything in the last second
    bool hasInput() const;

signals:
    /// Something the meters show has changed
    void changed();

    /// A reading of the input shown, its peak since the reading before
    void levelRead(float peak);

private:
    QPointer<sv::ViewManager> m_viewManager;
    sv::AudioCallbackRecordTarget *m_target;
    QTimer m_timer;
    QElapsedTimer m_clock;
    InputLevel::Meter m_meter;
    int m_channel;
    double m_threshold;
    float m_takeLeft;
    float m_takeRight;
    float m_takeHighestLeft;
    float m_takeHighestRight;
    bool m_haveTakeLevels;
    std::int64_t m_lastInputMs;
    double m_shownBar;
    double m_shownHold;
    bool m_shownClipped;

    void poll();
    void monitoringLevels(float left, float right);
};

#endif
