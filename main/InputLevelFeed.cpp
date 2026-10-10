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

#include "InputLevelFeed.h"
#include "InputChannel.h"
#include "VoiceThreshold.h"

#include "audio/AudioCallbackRecordTarget.h"
#include "view/ViewManager.h"

#include <algorithm>
#include <cmath>

using namespace sv;

InputLevelFeed::InputLevelFeed(ViewManager *viewManager,
                               AudioCallbackRecordTarget *target,
                               QObject *parent) :
    QObject(parent),
    m_viewManager(viewManager),
    m_target(target),
    m_channel(InputChannel::kBoth),
    m_threshold(VoiceThreshold::kOff),
    m_takeLeft(0.f),
    m_takeRight(0.f),
    m_takeHighestLeft(0.f),
    m_takeHighestRight(0.f),
    m_haveTakeLevels(false),
    m_shownBar(InputLevel::Meter::kFloorDb),
    m_shownHold(InputLevel::Meter::kFloorDb),
    m_shownClipped(false)
{
    m_clock.start();

    // During a take the view manager is the record target's reader
    if (m_viewManager) {
        connect(m_viewManager, &ViewManager::monitoringLevelsChanged,
                this, &InputLevelFeed::monitoringLevels);
    }

    connect(&m_timer, &QTimer::timeout, this, &InputLevelFeed::poll);
}

InputLevelFeed::~InputLevelFeed()
{
}

void
InputLevelFeed::setChannel(int channel)
{
    if (channel == m_channel) return;
    m_channel = channel;
}

void
InputLevelFeed::setThreshold(double dbfs)
{
    if (dbfs == m_threshold) return;
    m_threshold = dbfs;
    emit changed();
}

void
InputLevelFeed::setClipped(bool clipped)
{
    if (clipped == m_meter.clipped()) return;
    m_meter.setClipped(clipped);
    m_shownClipped = clipped;
    emit changed();
}

void
InputLevelFeed::setRunning(bool running)
{
    if (running == m_timer.isActive()) return;
    if (running) {
        m_timer.start(kIntervalMs);
        return;
    }
    m_timer.stop();

    // What was read last would stay drawn, as though the input were
    // still coming in
    const bool clipped = m_meter.clipped();
    m_meter = InputLevel::Meter();
    m_meter.setClipped(clipped);
    showMeter();
}

void
InputLevelFeed::monitoringLevels(float left, float right)
{
    // The view manager signals the output's levels while playing: only
    // a take's are the input's
    if (!m_target || !m_target->isRecording()) return;
    m_takeLeft = left;
    m_takeRight = right;
    m_takeHighestLeft = std::max(m_takeHighestLeft, left);
    m_takeHighestRight = std::max(m_takeHighestRight, right);
    m_haveTakeLevels = true;
}

void
InputLevelFeed::poll()
{
    const std::int64_t ms = now();

    float left = 0.f, right = 0.f;
    bool read = false;
    if (m_target && m_target->isRecording()) {
        // The highest said since the last reading, which goes on from
        // what was said last
        read = m_haveTakeLevels;
        left = m_takeHighestLeft;
        right = m_takeHighestRight;
        m_takeHighestLeft = m_takeLeft;
        m_takeHighestRight = m_takeRight;
    } else {
        if (m_target) read = m_target->getInputLevels(left, right);
        m_takeLeft = m_takeRight = 0.f;
        m_takeHighestLeft = m_takeHighestRight = 0.f;
        m_haveTakeLevels = false;
    }

    if (read) {
        const float peak = InputChannel::peakOf(m_channel, left, right);
        m_meter.peak(peak, ms);
        emit levelRead(peak);
    }

    showMeter();
}

void
InputLevelFeed::showMeter()
{
    const std::int64_t ms = now();

    // Drawn only when what is shown has moved: a quiet input that stays
    // under the scale, as between takes, costs nothing more
    const double bar = m_meter.bar(ms), hold = m_meter.hold(ms);
    const auto moved = [](double now, double shown) {
        return std::fabs(now - shown) >= kStepDb ||
            (now != shown && now <= InputLevel::Meter::kFloorDb);
    };
    if (moved(bar, m_shownBar) || moved(hold, m_shownHold) ||
        m_meter.clipped() != m_shownClipped) {
        m_shownBar = bar;
        m_shownHold = hold;
        m_shownClipped = m_meter.clipped();
        emit changed();
    }
}
