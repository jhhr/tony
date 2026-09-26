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

#ifndef TONY_MODEL_CHANGE_THROTTLE_H
#define TONY_MODEL_CHANGE_THROTTLE_H

#include "base/BaseTypes.h"
#include "data/model/Model.h"

#include <QTimer>

/**
 * Tells the views of a model what has changed in it, at most once an
 * interval: for a model written to faster than it is worth drawing, and
 * made to hold back its own change notices (notifyOnAdd false), such as
 * the live pitch dots of a take.
 *
 * A pane that is told of a change to one of its layers' models draws
 * every layer again, so a notice for each of the ~170 dots a second
 * kept the GUI thread busy for three quarters of its time. Told
 * nothing, a pane never draws the dots at all.
 *
 * The first change after a quiet interval is told at once; changes
 * that follow within the interval are told together at its end. Lives
 * on the GUI thread, as the model's writer and its views do.
 */
class ModelChangeThrottle
{
public:
    explicit ModelChangeThrottle(int intervalMs);
    ~ModelChangeThrottle();

    ModelChangeThrottle(const ModelChangeThrottle &) = delete;
    ModelChangeThrottle &operator=(const ModelChangeThrottle &) = delete;

    /// The model whose views are told. A model of none (the default)
    /// stops, and a change not yet told is forgotten
    void setModel(sv::ModelId model);
    sv::ModelId getModel() const { return m_model; }

    /// Frames [from, to) of the model have changed
    void changed(sv::sv_frame_t from, sv::sv_frame_t to);

private:
    void intervalUp();
    void tell();

    sv::ModelId m_model;
    QTimer m_timer;
    bool m_pending;
    sv::sv_frame_t m_from;
    sv::sv_frame_t m_to;
};

#endif
