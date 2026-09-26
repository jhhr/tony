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

#ifndef TEST_TOUCH_GESTURES_H
#define TEST_TOUCH_GESTURES_H

// Tier 5: touch on the panes of the real MainWindow (TouchGestures).
// Pinch, two fingers dragged and a long press; one finger and the
// mouse as they were.
//
// The touch goes in where a platform's does (QTest::touchEvent goes
// through QWindowSystemInterface), so Qt makes mouse events of it as it
// does on a phone, and a mouse button left pressed shows in
// QGuiApplication::mouseButtons(). Qt finds the widget under a touch
// only among widgets on show, so the window is shown here.

#include "TestSignals.h"

#include "../MainWindow.h"
#include "../Analyser.h"
#include "../PinchZoom.h"
#include "../TouchGestures.h"

#include "version.h"

#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/ViewManager.h"
#include "data/fileio/WavFileWriter.h"
#include "data/model/RelativelyFineZoomConstraint.h"
#include "transform/ModelTransformerFactory.h"

#include <QObject>
#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPointingDevice>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>

#include <cstdlib>
#include <vector>

/**
 * MainWindow without an audio device, and with the pane's right-button
 * menu counted instead of shown: a menu on show would take the touch
 * events that come after it. A test that wants a menu there gives one.
 */
class TouchTestWindow : public MainWindow
{
public:
    TouchTestWindow() : MainWindow(AUDIO_PLAYBACK_AND_RECORD, true, false) { }

    sv::PaneStack *paneStack() { return m_paneStack; }
    sv::ViewManager *viewManager() { return m_viewManager; }
    Analyser *analyser() { return m_analyser; }

    void discardModifications() { m_documentModified = false; }
    void doCloseSession() { discardModifications(); closeSession(); }

    int menuRequests = 0;
    QPoint menuPosition;
    QMenu *menu = nullptr;

protected:
    void createAudioIO() override { }

    // As MainWindow's own does, with the menu given
    void paneRightButtonMenuRequested(sv::Pane *, QPoint position) override {
        ++menuRequests;
        menuPosition = position;
        if (menu) menu->popup(position);
    }
};

class TestTouchGestures : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;

    // Where each test starts: frames per pixel, and the centre frame
    static constexpr int startLevel = 64;
    static constexpr sv::sv_frame_t startCentre = 3 * 44100;

    QTemporaryDir m_dir;
    TouchTestWindow *m_window = nullptr;
    QPointingDevice *m_touch = nullptr;
    QTimer m_watchdog;
    QStringList m_dialogs;

    typedef QTest::QTouchEventWidgetSequence Touch;

    // Points are given in the coordinates of the pane they are on
    Touch touch() { return QTest::touchEvent(m_window, m_touch, false); }

    sv::Pane *pane() { return m_window->paneStack()->getPane(0); }
    sv::Pane *strip() { return m_window->paneStack()->getPane(1); }

    double framesPerPixel() {
        return PinchZoom::framesPerPixel(pane()->getZoomLevel());
    }

    sv::MultiSelection::SelectionList selections() {
        return m_window->viewManager()->getSelections();
    }

    bool analysed() {
        Analyser *a = m_window->analyser();
        return a && a->getLayer(Analyser::PitchTrack) &&
            a->getLayer(Analyser::Notes) &&
            a->getInitialAnalysisCompletion() >= 100 &&
            !a->isAnalysingRange() &&
            !sv::ModelTransformerFactory::getInstance()
            ->haveRunningTransformers();
    }

    // The window on show with six seconds of reference, analysed, and
    // the view at startLevel about startCentre
    void openWindow() {
        m_window = new TouchTestWindow;
        m_window->resize(1000, 700);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));

        QString path = m_dir.filePath("reference.wav");
        if (!QFile::exists(path)) {
            std::vector<float> data = TestSignals::sine
                (220.5, rate, int(6 * rate), 0.5);
            sv::WavFileWriter writer(path, rate, 1,
                                     sv::WavFileWriter::WriteToTarget);
            const float *ptr = data.data();
            QVERIFY(writer.isOK());
            QVERIFY(writer.writeSamples(&ptr, sv::sv_frame_t(data.size())));
            QVERIFY(writer.close());
        }

        m_window->discardModifications();
        QCOMPARE(m_window->openPath(path, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(), 30000);

        QVERIFY(pane());
        QVERIFY(strip());
        pane()->setZoomLevel
            (sv::ZoomLevel(sv::ZoomLevel::FramesPerPixel, startLevel));
        pane()->setCentreFrame(startCentre);
        QCOMPARE(framesPerPixel(), double(startLevel));
    }

    void dismissDialog() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) return;
        QString description = modal->windowTitle();
        if (auto box = qobject_cast<QMessageBox *>(modal)) {
            description += ": " + box->text();
            m_dialogs.push_back(description);
            QList<QAbstractButton *> buttons = box->buttons();
            if (!buttons.isEmpty()) {
                buttons.last()->click();
                return;
            }
        } else {
            m_dialogs.push_back(description);
        }
        if (auto dialog = qobject_cast<QDialog *>(modal)) {
            dialog->reject();
        } else {
            modal->close();
        }
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());

        // Otherwise the MainWindow constructor asks, in a dialog
        QSettings settings;
        settings.beginGroup("Preferences");
        settings.setValue(QString("network-permission-%1").arg(TONY_VERSION),
                          false);
        settings.endGroup();

        connect(&m_watchdog, &QTimer::timeout,
                this, [this]() { dismissDialog(); });
        m_watchdog.start(50);
    }

    void init() {
        m_dialogs.clear();

        // A device of its own for each test: fingers that a failed test
        // left down stay on the last one
        m_touch = QTest::createTouchDevice();
    }

    void cleanup() {
        if (m_window) {
            // A test that failed with Qt's button pressed must not leave
            // it so for the next, which may make no mouse events to clear it
            if (QGuiApplication::mouseButtons() != Qt::NoButton) {
                QTest::mouseRelease(m_window->windowHandle(), Qt::LeftButton);
            }
            QTRY_VERIFY_WITH_TIMEOUT
                (!sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(), 30000);
            m_window->doCloseSession();
            delete m_window;
            m_window = nullptr;
        }
        QVERIFY2(m_dialogs.isEmpty(),
                 qPrintable("unexpected dialog: " + m_dialogs.join(" | ")));
    }

    void cleanupTestCase() {
        m_watchdog.stop();
    }

    // Fingers twice as far apart: twice as far in, with the frame that was
    // between them still there. The second finger comes down after the
    // first, and the first is lifted first
    void pinch_out_zooms_in_about_the_fingers() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int x = p->width() / 2 - 100; // off centre: not just the centre kept
        int y = p->height() / 2;
        sv::sv_frame_t before = p->getFrameForX(x);

        Touch t = touch();
        t.press(0, QPoint(x - 50, y), p).commit();
        t.stationary(0).press(1, QPoint(x + 50, y), p).commit();
        for (int i = 1; i <= 10; ++i) {
            t.move(0, QPoint(x - 50 - 5 * i, y), p)
                .move(1, QPoint(x + 50 + 5 * i, y), p).commit();
        }
        t.release(0, QPoint(x - 100, y), p).stationary(1).commit();
        t.release(1, QPoint(x + 100, y), p).commit();

        QCOMPARE(framesPerPixel(), startLevel / 2.0);
        sv::sv_frame_t after = p->getFrameForX(x);
        QVERIFY2(std::abs(after - before) <= startLevel / 2,
                 qPrintable(QString("frame %1 under the fingers, then %2")
                            .arg(before).arg(after)));

        QCOMPARE(m_window->menuRequests, 0);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // Fingers half as far apart: twice as far out. Both come down, and
    // go up, together
    void pinch_in_zooms_out_about_the_fingers() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int x = p->width() / 2 + 150;
        int y = p->height() / 2;
        sv::sv_frame_t before = p->getFrameForX(x);

        Touch t = touch();
        t.press(0, QPoint(x - 100, y), p).press(1, QPoint(x + 100, y), p)
            .commit();
        for (int i = 1; i <= 10; ++i) {
            t.move(0, QPoint(x - 100 + 5 * i, y), p)
                .move(1, QPoint(x + 100 - 5 * i, y), p).commit();
        }
        t.release(0, QPoint(x - 50, y), p).release(1, QPoint(x + 50, y), p)
            .commit();

        QCOMPARE(framesPerPixel(), startLevel * 2.0);
        sv::sv_frame_t after = p->getFrameForX(x);
        QVERIFY2(std::abs(after - before) <= startLevel * 2,
                 qPrintable(QString("frame %1 under the fingers, then %2")
                            .arg(before).arg(after)));

        QCOMPARE(m_window->menuRequests, 0);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // Two fingers dragged left by 100 pixels bring 100 pixels' worth of
    // what follows into view, and do not zoom. They come down together:
    // Qt then makes no mouse events at all, so what moves the view can
    // only be the two of them (one finger dragged the same way would
    // move it as far)
    void two_finger_drag_scrolls() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int x = p->width() / 2;
        int y = p->height() / 2;
        sv::sv_frame_t before = p->getCentreFrame();

        Touch t = touch();
        t.press(0, QPoint(x - 50, y), p).press(1, QPoint(x + 50, y), p)
            .commit();
        for (int i = 1; i <= 10; ++i) {
            t.move(0, QPoint(x - 50 - 10 * i, y), p)
                .move(1, QPoint(x + 50 - 10 * i, y), p).commit();
        }
        t.release(0, QPoint(x - 150, y), p).release(1, QPoint(x - 50, y), p)
            .commit();

        QCOMPARE(framesPerPixel(), double(startLevel));
        sv::sv_frame_t expected = before + 100 * startLevel;
        QVERIFY2(std::abs(p->getCentreFrame() - expected) <= startLevel,
                 qPrintable(QString("centre %1, expected %2")
                            .arg(p->getCentreFrame()).arg(expected)));

        QCOMPARE(m_window->menuRequests, 0);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // One finger held still asks for the menu where it is, as a right
    // click there does. It does not drag afterwards, and the pane never
    // had the press: no move of the playhead is scheduled
    void long_press_asks_for_the_menu() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        QPoint at(p->width() / 2 + 120, p->height() / 2);
        sv::sv_frame_t centre = p->getCentreFrame();
        sv::sv_frame_t playhead = m_window->viewManager()->getPlaybackFrame();
        QVERIFY(std::abs(p->getFrameForX(at.x()) - playhead) > 10000);

        Touch t = touch();
        t.press(0, at, p).commit();
        QTest::qWait(TouchGestures::longPressMs / 2);
        QCOMPARE(m_window->menuRequests, 0);
        QTest::qWait(TouchGestures::longPressMs / 2 + 200);
        QCOMPARE(m_window->menuRequests, 1);
        QCOMPARE(m_window->menuPosition, p->mapToGlobal(at));

        for (int i = 1; i <= 5; ++i) {
            t.move(0, at - QPoint(20 * i, 0), p).commit();
        }
        t.release(0, at - QPoint(100, 0), p).commit();
        QTest::qWait(QApplication::doubleClickInterval() + 100);

        QCOMPARE(p->getCentreFrame(), centre);
        QCOMPARE(m_window->viewManager()->getPlaybackFrame(), playhead);
        QCOMPARE(m_window->menuRequests, 1);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // In the selection strip, where a press starts a selection
    void long_press_leaves_no_selection() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *s = strip();
        QPoint at(s->width() / 2, s->height() / 2);
        QVERIFY(selections().empty());

        Touch t = touch();
        t.press(0, at, s).commit();
        QTest::qWait(TouchGestures::longPressMs + 200);
        QCOMPARE(m_window->menuRequests, 1);

        for (int i = 1; i <= 5; ++i) {
            t.move(0, at + QPoint(20 * i, 0), s).commit();
        }
        t.release(0, at + QPoint(100, 0), s).commit();

        QVERIFY(selections().empty());
        QVERIFY(!m_window->viewManager()->haveInProgressSelection());
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // The menu comes up under the finger. The finger wandering onto its
    // first item (Undo, in Tony's) and lifted there does not choose it;
    // a tap on it afterwards does
    void long_press_menu_waits_for_a_tap() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        QMenu *menu = new QMenu(m_window);
        QAction *first = menu->addAction("First");
        menu->addAction("Second");
        int chosen = 0;
        connect(first, &QAction::triggered, this, [&chosen]() { ++chosen; });
        m_window->menu = menu;

        sv::Pane *p = pane();
        QPoint at(p->width() / 2, p->height() / 2);

        Touch t = touch();
        t.press(0, at, p).commit();
        QTest::qWait(TouchGestures::longPressMs + 200);
        QCOMPARE(m_window->menuRequests, 1);
        QVERIFY(menu->isVisible());

        QPoint item = p->mapFromGlobal
            (menu->mapToGlobal(menu->actionGeometry(first).center()));
        for (int i = 1; i <= 10; ++i) {
            t.move(0, at + (item - at) * i / 10, p).commit();
        }
        t.release(0, item, p).commit();

        QCOMPARE(chosen, 0);
        QVERIFY(menu->isVisible());

        QTest::qWait(QApplication::doubleClickInterval() + 100);
        Touch tap = touch();
        tap.press(0, item, p).commit();
        tap.release(0, item, p).commit();

        QTRY_COMPARE(chosen, 1);
        QVERIFY(!menu->isVisible());
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // One finger dragged in Navigate mode moves the view exactly as the
    // mouse dragged the same way does
    void one_finger_drag_pans_as_the_mouse_does() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int y = p->height() / 2;
        QPoint from(p->width() / 2 + 100, y);
        QPoint to(p->width() / 2, y);
        sv::sv_frame_t start = p->getCentreFrame();

        QTest::mousePress(p, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= 10; ++i) {
            QTest::mouseMove(p, from + (to - from) * i / 10);
        }
        QTest::mouseRelease(p, Qt::LeftButton, Qt::NoModifier, to);
        sv::sv_frame_t byMouse = p->getCentreFrame() - start;
        QVERIFY2(byMouse > 90 * startLevel,
                 qPrintable(QString("the mouse moved it by %1")
                            .arg(byMouse)));

        p->setCentreFrame(start);
        QCOMPARE(p->getCentreFrame(), start);

        Touch t = touch();
        t.press(0, from, p).commit();
        for (int i = 1; i <= 10; ++i) {
            t.move(0, from + (to - from) * i / 10, p).commit();
        }
        t.release(0, to, p).commit();
        sv::sv_frame_t byTouch = p->getCentreFrame() - start;

        QCOMPARE(byTouch, byMouse);
        QCOMPARE(m_window->menuRequests, 0);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // A tap is a click: in Navigate mode the playhead goes there
    void one_finger_tap_moves_the_playhead() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        QPoint at(p->width() / 2 + 150, p->height() / 2);
        sv::sv_frame_t target = p->getFrameForX(at.x());
        QVERIFY(std::abs(m_window->viewManager()->getPlaybackFrame() -
                         target) > 10000);

        Touch t = touch();
        t.press(0, at, p).commit();
        t.release(0, at, p).commit();

        QTRY_VERIFY_WITH_TIMEOUT
            (std::abs(m_window->viewManager()->getPlaybackFrame() - target)
             <= startLevel, 2000);
        QCOMPARE(m_window->menuRequests, 0);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // A finger dragging out a selection, and a second finger coming down:
    // the selection ends where the first finger was, and nothing either
    // finger does after that changes it or leaves a button pressed
    void second_finger_ends_a_selection_drag() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *s = strip();
        sv::ViewManager *vm = m_window->viewManager();
        int y = s->height() / 2;
        int x0 = s->width() / 2 - 100;
        int x1 = x0 + 100;
        sv::sv_frame_t f0 = s->getFrameForX(x0);
        sv::sv_frame_t f1 = s->getFrameForX(x1);

        Touch t = touch();
        t.press(0, QPoint(x0, y), s).commit();
        for (int i = 1; i <= 10; ++i) {
            t.move(0, QPoint(x0 + 10 * i, y), s).commit();
        }
        QVERIFY(vm->haveInProgressSelection());

        t.stationary(0).press(1, QPoint(x1 + 80, y), s).commit();
        QVERIFY(!vm->haveInProgressSelection());
        QCOMPARE(int(selections().size()), 1);
        sv::Selection made = *selections().begin();
        QVERIFY2(std::abs(made.getStartFrame() - f0) <= startLevel &&
                 std::abs(made.getEndFrame() - f1) <= startLevel,
                 qPrintable(QString("selected %1 to %2, dragged %3 to %4")
                            .arg(made.getStartFrame())
                            .arg(made.getEndFrame()).arg(f0).arg(f1)));

        for (int i = 1; i <= 5; ++i) {
            t.move(0, QPoint(x1 + 20 * i, y), s)
                .move(1, QPoint(x1 + 80 + 20 * i, y), s).commit();
        }
        t.release(0, QPoint(x1 + 100, y), s)
            .release(1, QPoint(x1 + 180, y), s).commit();

        QCOMPARE(int(selections().size()), 1);
        QVERIFY(*selections().begin() == made);
        QVERIFY(!vm->haveInProgressSelection());
        QCOMPARE(m_window->menuRequests, 0);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // The wheel zooms in one step about the centre, as svgui has it
    void mouse_wheel_zoom_is_unchanged() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        sv::sv_frame_t centre = p->getCentreFrame();
        QPointF at(p->width() / 2 - 100, p->height() / 2);

        QWheelEvent wheel(at, p->mapToGlobal(at), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
        QCoreApplication::sendEvent(p, &wheel);

        sv::ZoomLevel expected = sv::RelativelyFineZoomConstraint()
            .getNearestZoomLevel
            (sv::ZoomLevel(sv::ZoomLevel::FramesPerPixel,
                           startLevel).decremented(),
             sv::ZoomConstraint::RoundDown);
        QVERIFY(framesPerPixel() < startLevel);
        QVERIFY(p->getZoomLevel() == expected);
        QCOMPARE(p->getCentreFrame(), centre);
    }
};

#endif
