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

#include "TouchGestures.h"
#include "PinchZoom.h"

#include "view/Pane.h"

#include <QApplication>
#include <QEvent>
#include <QGestureRecognizer>
#include <QGuiApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QPointer>
#include <QPointingDevice>
#include <QStyleHints>
#include <QTouchEvent>

#include <cmath>

using namespace sv;

// How Qt 6 turns touch into mouse events, as far as this class depends
// on it (qguiapplication.cpp, processTouchEvent(); qapplication.cpp,
// notify() and translateRawTouchEvent()):
//
// - Each touch event is offered to the widgets first. Only if none
//   accepts it does Qt make a mouse event of it, from the point that
//   came down first, and it goes on doing so for as long as the events
//   are not accepted. Accepting the event that lifts that point would
//   leave Qt's mouse button pressed for good.
//
// - The points of a TouchBegin that no widget accepts go to the first
//   widget, from the one touched up to the window, that is subscribed
//   to a gesture. Otherwise that is QScrollArea's viewport, which
//   subscribes to PanGesture, and it would take the pane's second
//   finger too. Subscribed to a gesture, the pane gets its points
//   itself. The gesture is one that recognises nothing: it is there
//   only to be subscribed to.
//
// - Updates to those points are not delivered to the pane after an
//   unaccepted TouchBegin, except from the event in which another
//   finger comes down: from that one to the TouchEnd, the pane gets
//   every point on it.
//
// So a lone finger is seen only as Qt's mouse events. The pane's first
// touch event with two points in it is where the fingers take over:
// from there each touch event is accepted, so that Qt makes no mouse
// event of it, except the one that lifts the first finger, so that Qt
// makes the release that matches its press. A TouchBegin with two points
// in it is accepted at once, and then Qt makes no mouse events at all.

namespace {

class SubscriptionOnly : public QGestureRecognizer
{
public:
    Result recognize(QGesture *, QObject *, QEvent *) override {
        return Ignore;
    }
};

Qt::GestureType
subscriptionOnlyGesture()
{
    // The gesture manager owns the recognizer
    static Qt::GestureType type =
        QGestureRecognizer::registerRecognizer(new SubscriptionOnly);
    return type;
}

bool
isTouchScreen(const QPointingDevice *device)
{
    return device &&
        device->type() == QInputDevice::DeviceType::TouchScreen;
}

}

TouchGestures::TouchGestures(Pane *pane) :
    QObject(pane),
    m_pane(pane)
{
    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(longPressMs);
    connect(&m_longPressTimer, &QTimer::timeout,
            this, &TouchGestures::longPressed);

    pane->setAttribute(Qt::WA_AcceptTouchEvents);
    pane->grabGesture(subscriptionOnlyGesture());
    pane->installEventFilter(this);
}

TouchGestures::~TouchGestures()
{
    stopWatchingMenu();
}

bool
TouchGestures::eventFilter(QObject *watched, QEvent *event)
{
    if (m_menu && watched == m_menu) return menuEvent(event);

    if (watched != m_pane || m_replaying) return false;

    switch (event->type()) {

    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        // A touch pad is a mouse here
        auto te = static_cast<QTouchEvent *>(event);
        if (!isTouchScreen(te->pointingDevice())) return false;
        return touchEvent(te);
    }

    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
        // Qt's from a touch, and a platform's own (Windows), carry the
        // touch screen
        auto me = static_cast<QMouseEvent *>(event);
        if (!isTouchScreen(me->pointingDevice())) return false;
        return mouseEvent(me);
    }

    default:
        return false;
    }
}

bool
TouchGestures::touchEvent(QTouchEvent *e)
{
    // Returns true for all of them: the pane has no use for touch
    // events. Accepted or not says whether Qt makes mouse events of it

    if (e->type() == QEvent::TouchCancel) {
        // Qt sends a release for the press it made. In Passing it is the
        // pane's; otherwise it is to be eaten
        dropHeld();
        if (m_state != State::Passing && m_state != State::Idle) {
            m_state = m_sourcePressSeen ? State::Ending : State::Idle;
        }
        m_points.clear();
        m_order.clear();
        m_sourceId = -1;
        return true;
    }

    if (e->type() == QEvent::TouchBegin) {

        restart();
        for (const QEventPoint &p : e->points()) {
            setPoint(p.id(), p.position());
        }

        if (m_order.size() > 1) {
            e->accept();
            startTwoFingers();
        } else {
            m_sourceId = m_order.value(0, -1);
            e->ignore();
        }
        return true;
    }

    bool sourceLifted = false;

    for (const QEventPoint &p : e->points()) {
        if (p.state() == QEventPoint::State::Released) {
            removePoint(p.id());
            if (p.id() == m_sourceId) {
                sourceLifted = true;
                m_sourceId = -1;
            }
        } else {
            setPoint(p.id(), p.position());
        }
    }

    if (m_state != State::TwoFingers && m_order.size() > 1) {
        startTwoFingers();
    }

    if (m_state != State::TwoFingers) {
        e->ignore();
        return true;
    }

    if (m_order.size() > 1) {
        updatePinch();
    }

    if (sourceLifted) {
        e->ignore();
    } else {
        e->accept();
    }

    if (e->type() == QEvent::TouchEnd) {
        m_points.clear();
        m_order.clear();
        m_sourceId = -1;
        m_state = (sourceLifted && m_sourcePressSeen) ?
            State::Ending : State::Idle;
    }

    return true;
}

bool
TouchGestures::mouseEvent(QMouseEvent *e)
{
    QEvent::Type type = e->type();
    bool press = (type == QEvent::MouseButtonPress ||
                  type == QEvent::MouseButtonDblClick);
    bool release = (type == QEvent::MouseButtonRelease);
    bool drag = (type == QEvent::MouseMove &&
                 (e->buttons() & Qt::LeftButton));

    // Hovering, and any button but the one a finger is, are not ours
    if (!press && !release && !drag) return false;
    if ((press || release) && e->button() != Qt::LeftButton) return false;

    // A lone finger's touch updates do not come here, its mouse events do
    if (m_state == State::Idle || m_state == State::Holding ||
        m_state == State::Passing) {
        m_lastMousePosition = e->position();
        if (m_points.contains(m_sourceId)) {
            m_points[m_sourceId] = e->position();
        }
    }

    switch (m_state) {

    case State::Idle:
        if (!press) return false; // Qt's, from a touch that ended elsewhere
        m_sourcePressSeen = true;
        m_pressPosition = e->position();
        hold(e);
        m_state = State::Holding;
        m_longPressTimer.start();
        return true;

    case State::Holding:
        if (drag &&
            (e->position() - m_pressPosition).manhattanLength() <= slop()) {
            hold(e);
            return true;
        }
        // Moved or lifted: the pane has all of it, this one last
        replayHeld();
        m_state = release ? State::Idle : State::Passing;
        return false;

    case State::Passing:
        if (release) m_state = State::Idle;
        return false;

    case State::LongPressed:
    case State::Ending:
        if (release) m_state = State::Idle;
        return true;

    case State::TwoFingers:
        return true;
    }

    return false;
}

bool
TouchGestures::menuEvent(QEvent *event)
{
    switch (event->type()) {

    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
        auto me = static_cast<QMouseEvent *>(event);
        if (!isTouchScreen(me->pointingDevice())) return false;
        if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonDblClick) {
            // Another touch, so that one is over: this is a tap
            stopWatchingMenu();
            return false;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            // Lifted: the menu is the user's now
            stopWatchingMenu();
            if (m_state == State::LongPressed) m_state = State::Idle;
        }
        me->accept();
        return true;
    }

    default:
        return false;
    }
}

void
TouchGestures::longPressed()
{
    if (m_state != State::Holding) return;

    QPointF position = m_pressPosition;
    dropHeld();
    m_state = State::LongPressed;

    // A right press is how the pane is asked for its menu
    // (Pane::mousePressEvent). It is the mouse's, not the touch screen's,
    // so this filter lets it through
    QMouseEvent press(QEvent::MouseButtonPress, position,
                      m_pane->mapToGlobal(position),
                      Qt::RightButton, Qt::RightButton,
                      QGuiApplication::keyboardModifiers());
    QPointer<TouchGestures> self(this);
    sendToPane(&press);
    if (!self) return;

    // While the finger is down, Qt sends what it makes of it to the menu
    // that has just opened (QWindowPrivate::forwardToPopup())
    QWidget *menu = QApplication::activePopupWidget();
    if (menu) {
        m_menu = menu;
        menu->installEventFilter(this);
    }
}

void
TouchGestures::stopWatchingMenu()
{
    if (m_menu) m_menu->removeEventFilter(this);
    m_menu = nullptr;
}

void
TouchGestures::restart()
{
    // What was left of the last touch, if its end did not come here (a
    // menu took it, say). The pane must not be left pressed
    if (m_state == State::Holding) {
        dropHeld();
    } else if (m_state == State::Passing) {
        endPaneDrag();
    }

    stopWatchingMenu();

    m_state = State::Idle;
    m_points.clear();
    m_order.clear();
    m_sourceId = -1;
    m_sourcePressSeen = false;
}

void
TouchGestures::startTwoFingers()
{
    if (m_state == State::Holding) {
        dropHeld();
    } else if (m_state == State::Passing) {
        endPaneDrag();
    }

    m_state = State::TwoFingers;
    beginPinch();
}

void
TouchGestures::beginPinch()
{
    if (m_order.size() < 2) return;

    m_pinchA = m_order[0];
    m_pinchB = m_order[1];

    QPointF a = m_points.value(m_pinchA);
    QPointF b = m_points.value(m_pinchB);

    m_startSpan = QLineF(a, b).length();
    m_startFramesPerPixel =
        PinchZoom::framesPerPixel(m_pane->getZoomLevel());
    m_zooming = false;

    m_anchorFrame = PinchZoom::frameAtX
        (m_pane->getCentreFrame(), m_pane->getZoomLevel(),
         m_pane->width(), (a.x() + b.x()) / 2.0);
}

void
TouchGestures::updatePinch()
{
    if (!m_points.contains(m_pinchA) || !m_points.contains(m_pinchB)) {
        // One of the two was lifted and another is down: a new pinch
        // from here
        beginPinch();
        return;
    }

    QPointF a = m_points.value(m_pinchA);
    QPointF b = m_points.value(m_pinchB);
    double span = QLineF(a, b).length();
    double x = (a.x() + b.x()) / 2.0;

    // Two fingers dragged together change their distance a little: that
    // is not yet a pinch. Twice the slop, as Android's own detector has
    if (!m_zooming && std::fabs(span - m_startSpan) > 2 * slop()) {
        m_zooming = true;
    }

    if (m_zooming && m_startSpan > 0.0 && span > 0.0) {
        ZoomLevel current = m_pane->getZoomLevel();
        ZoomLevel level = PinchZoom::pinchedLevel
            (current, m_startFramesPerPixel * m_startSpan / span);
        if (!(level == current)) {
            m_pane->setZoomLevel(level);
        }
    }

    // The frame that was under the middle of the fingers stays there
    ZoomLevel level = m_pane->getZoomLevel();
    int width = m_pane->width();
    sv_frame_t wanted = PinchZoom::centreFor(m_anchorFrame, level, width, x);

    // Within the audio, as a one-finger drag keeps it
    // (Pane::dragTopLayer). Held at an end, the view stays while the
    // fingers go on: what is under them then is what they hold
    sv_frame_t centre = wanted;
    sv_frame_t end = m_pane->getModelsEndFrame();
    if (centre >= end) centre = end - 1;
    if (centre < 0) centre = 0;
    if (centre != wanted) {
        m_anchorFrame = PinchZoom::frameAtX(centre, level, width, x);
    }

    if (centre != m_pane->getCentreFrame()) {
        m_pane->setCentreFrame(centre);
    }
}

void
TouchGestures::hold(QMouseEvent *e)
{
    m_held.push_back(std::unique_ptr<QMouseEvent>(e->clone()));
}

void
TouchGestures::replayHeld()
{
    m_longPressTimer.stop();

    std::vector<std::unique_ptr<QMouseEvent>> held;
    held.swap(m_held);

    QPointer<TouchGestures> self(this);
    for (auto &e : held) {
        sendToPane(e.get());
        if (!self) return;
    }
}

void
TouchGestures::dropHeld()
{
    m_longPressTimer.stop();
    m_held.clear();
}

void
TouchGestures::endPaneDrag()
{
    // A release where the finger is, as a mouse released there would
    // end it
    QPointF position = sourcePosition();
    QMouseEvent release(QEvent::MouseButtonRelease, position,
                        m_pane->mapToGlobal(position),
                        Qt::LeftButton, Qt::NoButton,
                        QGuiApplication::keyboardModifiers());
    sendToPane(&release);
}

void
TouchGestures::sendToPane(QMouseEvent *e)
{
    bool wasReplaying = m_replaying;
    m_replaying = true;
    QPointer<TouchGestures> self(this);
    QCoreApplication::sendEvent(m_pane, e);
    if (self) m_replaying = wasReplaying;
}

void
TouchGestures::setPoint(int id, QPointF position)
{
    if (!m_points.contains(id)) m_order.push_back(id);
    m_points[id] = position;
}

void
TouchGestures::removePoint(int id)
{
    m_points.remove(id);
    m_order.removeAll(id);
}

QPointF
TouchGestures::sourcePosition() const
{
    return m_points.value(m_sourceId, m_lastMousePosition);
}

int
TouchGestures::slop() const
{
    // Qt's distance for a drag to start: in logical pixels, which on a
    // phone are the platform's density-independent ones
    return QGuiApplication::styleHints()->startDragDistance();
}
