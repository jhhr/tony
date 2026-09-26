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

#ifndef TONY_TOUCH_GESTURES_H
#define TONY_TOUCH_GESTURES_H

#include "base/BaseTypes.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <vector>

class QMouseEvent;
class QTouchEvent;

namespace sv {
class Pane;
}

/**
 * Touch on one pane: pinch to zoom the time axis about the fingers,
 * two fingers dragged to scroll it, and a long press for the pane's
 * right-button menu. MainWindow gives every pane one; the pane owns it.
 *
 * One finger is left to Qt, which makes mouse events of a touch that
 * nothing accepts: tapping, dragging in Navigate mode and selecting in
 * the selection strip go through the pane's mouse handling as they
 * always have. What this adds works on that stream of mouse events:
 *
 * - The press is held back until the finger moves, lifts, or has been
 *   down long enough to be a long press. Held back, it cannot start a
 *   selection or a drag, or schedule a move of the playhead, that a
 *   long press or a second finger would leave behind. When the finger
 *   moves or lifts, the pane gets everything held, in order.
 *
 * - When a second finger comes down, a drag the first had started is
 *   ended where it is, and the fingers drive the view until they are
 *   all up. The first finger's mouse events are eaten meanwhile.
 *
 * - The menu a long press opens comes up under the finger, and QMenu
 *   takes the release of a button that has moved over an item for a
 *   choice of it. A resting finger moves, and the first item is Undo:
 *   so the rest of that finger's events are kept from the menu, which
 *   then waits for a tap, as a phone's own menus do.
 *
 * How Qt decides what becomes a mouse event, and why the pane
 * subscribes to a gesture that never happens, is in TouchGestures.cpp.
 * Input from a mouse is not touched.
 */
class TouchGestures : public QObject
{
    Q_OBJECT

public:
    explicit TouchGestures(sv::Pane *pane);
    virtual ~TouchGestures();

    /// How long one finger must rest for a long press, in ms
    static const int longPressMs = 500;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class State {
        Idle,        // no touch, or one whose press has not come
        Holding,     // one finger resting: its press held back
        Passing,     // one finger moving: its events go to the pane
        LongPressed, // the menu asked for: the finger's events eaten
        TwoFingers,  // the fingers drive the view: mouse events eaten
        Ending       // all up: the release Qt makes is still to eat
    };

    bool touchEvent(QTouchEvent *);
    bool mouseEvent(QMouseEvent *);
    bool menuEvent(QEvent *);
    void longPressed();
    void stopWatchingMenu();

    void restart();
    void startTwoFingers();
    void beginPinch();
    void updatePinch();

    void hold(QMouseEvent *);
    void replayHeld();
    void dropHeld();
    void endPaneDrag();
    void sendToPane(QMouseEvent *);

    void setPoint(int id, QPointF position);
    void removePoint(int id);
    QPointF sourcePosition() const;
    int slop() const;

    sv::Pane *m_pane;
    State m_state = State::Idle;

    QTimer m_longPressTimer;
    QPointer<QWidget> m_menu; // the one a long press opened, until lifted
    std::vector<std::unique_ptr<QMouseEvent>> m_held;
    QPointF m_pressPosition;
    QPointF m_lastMousePosition;
    bool m_replaying = false;

    // The touch points down on the pane, in pane coordinates, and the
    // order they came down in
    QHash<int, QPointF> m_points;
    QList<int> m_order;

    // The point Qt makes the mouse events from, while it is down, and
    // whether its press came to this pane
    int m_sourceId = -1;
    bool m_sourcePressSeen = false;

    // The two points of the pinch, and the view when they came down
    int m_pinchA = -1;
    int m_pinchB = -1;
    double m_startSpan = 0.0;
    double m_startFramesPerPixel = 1.0;
    double m_anchorFrame = 0.0;
    bool m_zooming = false;
};

#endif
