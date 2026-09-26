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

#ifdef TONY_DEV_CHECKS

#include "TakeObserver.h"

#include "../Analyser.h"
#include "../MainWindow.h"

#include "audio/AudioCallbackPlaySource.h"
#include "audio/AudioCallbackRecordTarget.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/WritableWaveFileModel.h"
#include "view/ViewManager.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QTimer>

#include <algorithm>

using namespace sv;

TakeObserver::TakeObserver(MainWindow *window, QObject *parent) :
    QObject(parent),
    m_window(window),
    m_timer(new QTimer(this)),
    m_playbackFrame(0),
    m_inputSince(false),
    m_inputLeftSince(0.f),
    m_inputRightSince(0.f),
    m_inputLeft(0.f),
    m_inputRight(0.f)
{
    m_timer->setInterval(kPollMs);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &TakeObserver::poll);

    // While recording, the meter is told the input levels the view
    // manager reads; the observer must not read them itself
    if (m_window->m_viewManager) {
        connect(m_window->m_viewManager, &ViewManager::monitoringLevelsChanged,
                this, &TakeObserver::inputLevels);
    }
}

TakeObserver::~TakeObserver()
{
}

void
TakeObserver::start()
{
    m_observation = Observation();
    m_seen.clear();
    m_inputSince = false;
    m_inputLeftSince = m_inputRightSince = 0.f;
    m_inputLeft = m_inputRight = 0.f;
    m_playbackFrame = 0;

    // This take's dots come in a model made once the take is under way
    m_earlierDots = m_window->m_realtimePitchModelId;

    if (m_window->m_viewManager) {
        m_observation.playbackStart =
            m_window->m_viewManager->getRecordStartFrame();
    }
    if (m_window->m_playSingingAudio) {
        m_observation.singingAudioBefore =
            m_window->m_playSingingAudio->isChecked();
    }

    m_clock.start();
    poll();
    m_timer->start();
}

void
TakeObserver::stop()
{
    // No last look: the next take may have begun by now
    m_timer->stop();
}

bool
TakeObserver::isObserving() const
{
    return m_timer->isActive();
}

void
TakeObserver::inputLevels(float left, float right)
{
    // While the take only plays, the view manager tells the meter the
    // output levels instead
    if (!isObserving() || !m_window->m_recordTarget ||
        !m_window->m_recordTarget->isRecording()) {
        return;
    }
    if (!m_inputSince) {
        m_inputSince = true;
        m_inputLeftSince = left;
        m_inputRightSince = right;
    } else {
        m_inputLeftSince = std::max(m_inputLeftSince, left);
        m_inputRightSince = std::max(m_inputRightSince, right);
    }
}

void
TakeObserver::poll()
{
    AudioCallbackRecordTarget *target = m_window->m_recordTarget;
    AudioCallbackPlaySource *source = m_window->m_playSource;

    Sample s;
    s.ms = m_clock.elapsed();
    s.recording = target && target->isRecording();

    // Only while recording: then getPlaybackFrame() works out what the
    // view manager's own poll does.  While the reference plays on after
    // the take, it would take the play source's frame, and could do so
    // between the take's putting the playhead back and stopping playback
    if (s.recording && m_window->m_viewManager) {
        m_playbackFrame = m_window->m_viewManager->getPlaybackFrame();
    }
    s.playbackFrame = m_playbackFrame;

    if (target) s.framesBefore = target->getFramesReceived();
    if (s.recording && source) {
        s.outputRead = true;
        float left = 0.f, right = 0.f;
        if (source->getOutputLevels(left, right)) {
            s.outputLeft = left;
            s.outputRight = right;
        }
    }
    if (target) s.framesAfter = target->getFramesReceived();

    if (s.recording) {
        if (m_inputSince) {
            m_inputLeft = m_inputLeftSince;
            m_inputRight = m_inputRightSince;
            m_inputSince = false;
        }
        s.inputLeft = m_inputLeft;
        s.inputRight = m_inputRight;
    }

    s.status = m_window->getStatusLabel()->text();
    s.modal = (QApplication::activeModalWidget() != nullptr);

    if (s.recording && m_observation.recordingPath == "") {
        if (auto recording = ModelById::getAs<WritableWaveFileModel>
            (m_window->m_currentRecordingModelId)) {
            m_observation.recordingPath = recording->getLocation();
        }
    }

    // Each dot the first time it is there.  The window may throw the
    // dots placed so far away once, when it learns the start gap; any
    // placed again are new dots
    const ModelId dotsId = m_window->m_realtimePitchModelId;
    if (!dotsId.isNone() && dotsId != m_earlierDots) {
        if (auto dots = ModelById::getAs<SparseTimeValueModel>(dotsId)) {
            for (const Event &e : dots->getAllEvents()) {
                const auto key = std::make_pair(e.getFrame(), e.getValue());
                if (!m_seen.insert(key).second) continue;
                Dot d;
                d.ms = s.ms;
                d.frame = e.getFrame();
                d.hz = e.getValue();
                d.playbackFrame = s.playbackFrame;
                m_observation.dots.push_back(d);
            }
        }
    }

    if (!s.recording && m_observation.stoppedMs < 0 &&
        !m_observation.samples.empty() &&
        m_observation.samples.back().recording) {
        m_observation.stoppedMs = s.ms;
    }
    if (!s.recording && m_observation.stoppedMs >= 0) {
        m_observation.sawAfter = true;
        m_observation.singingAudioAfter =
            m_window->m_playSingingAudio &&
            m_window->m_playSingingAudio->isChecked();
        Analyser *take = m_window->m_analyser2;
        m_observation.takeAudibleAfter =
            take && take->isAudible(Analyser::Audio);
    }

    m_observation.samples.push_back(s);
}

#endif
