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

#include "ModelChangeThrottle.h"

#include <algorithm>

using namespace sv;

ModelChangeThrottle::ModelChangeThrottle(int intervalMs) :
    m_pending(false),
    m_from(0),
    m_to(0)
{
    m_timer.setInterval(intervalMs);
    QObject::connect(&m_timer, &QTimer::timeout,
                     &m_timer, [this]() { intervalUp(); });
}

ModelChangeThrottle::~ModelChangeThrottle()
{
}

void
ModelChangeThrottle::setModel(ModelId model)
{
    m_timer.stop();
    m_pending = false;
    m_model = model;
}

void
ModelChangeThrottle::changed(sv_frame_t from, sv_frame_t to)
{
    if (m_model.isNone()) return;

    if (m_pending) {
        m_from = std::min(m_from, from);
        m_to = std::max(m_to, to);
    } else {
        m_from = from;
        m_to = to;
        m_pending = true;
    }

    // Quiet until now: tell at once, and hold back what comes next for an
    // interval
    if (!m_timer.isActive()) {
        tell();
        m_timer.start();
    }
}

void
ModelChangeThrottle::intervalUp()
{
    // Nothing came in the interval: quiet again
    if (!m_pending) {
        m_timer.stop();
        return;
    }
    tell();
}

void
ModelChangeThrottle::tell()
{
    m_pending = false;
    auto model = ModelById::get(m_model);
    if (!model) return;

    // The model's own notice, as it would have sent it itself for each
    // change: a view redraws only what it is told of
    emit model->modelChangedWithin(m_model, m_from, m_to);
}
