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
// mouse as they were. Up and down, two fingers zoom and scroll the
// analyser's frequency range, which is checked through svgui's own
// mapping of the pane.
//
// The touch goes in where a platform's does (QTest::touchEvent goes
// through QWindowSystemInterface), so Qt makes mouse events of it as it
// does on a phone, and a mouse button left pressed shows in
// QGuiApplication::mouseButtons(). Qt finds the widget under a touch
// only among widgets on show, so the window is shown here.

#include "TestSignals.h"

#include "../MainWindow.h"
#include "../Analyser.h"
#include "../AlternatePitchTrack.h"
#include "../PinchZoom.h"
#include "../TouchGestures.h"
#include "../VerticalZoom.h"

#include "version.h"

#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/ViewManager.h"
#include "layer/CoordinateScale.h"
#include "layer/Layer.h"
#include "layer/TimeValueLayer.h"
#include "widgets/CommandHistory.h"
#include "widgets/InteractiveFileFinder.h"
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
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>
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
    Analyser *analyser2() { return m_analyser2; }
    AlternatePitchTrack *alternatePitch() { return m_alternatePitch; }
    void toggleAlternatePitch() { alternatePitchToggled(); }

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

    // The reference's pitch, and a low voice's (D2)
    static constexpr double referenceHz = 220.5;
    static constexpr double lowHz = 73.5;

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

    // The analyser's frequency range, which the pane draws pitch on
    VerticalZoom::Range frequencyRange() {
        VerticalZoom::Range r;
        r.log = true;
        m_window->analyser()->getDisplayFrequencyExtents(r.min, r.max);
        return r;
    }

    static double octaves(const VerticalZoom::Range &r) {
        return std::log2(r.max / r.min);
    }

    static QString text(const VerticalZoom::Range &r) {
        return QString("%1 to %2 Hz").arg(r.min).arg(r.max);
    }

    // Where the pane draws a frequency, and what it draws at y: svgui's
    // mapping, not the one under test
    double yForFrequency(double f) {
        return pane()->getEffectiveVerticalExtents("Hz")
            .getCoordForValue(pane(), f);
    }

    double frequencyAtY(double y) {
        return pane()->getEffectiveVerticalExtents("Hz")
            .getValueForCoord(pane(), y);
    }

    // How far the fingers go up or down before that counts
    static int deadZone() {
        return 2 * QGuiApplication::styleHints()->startDragDistance();
    }

    // Two fingers down together, moved in ten even steps, and lifted
    // together
    void twoFingers(sv::Pane *p, QPoint a0, QPoint b0, QPoint a1, QPoint b1) {
        Touch t = touch();
        t.press(0, a0, p).press(1, b0, p).commit();
        for (int i = 1; i <= 10; ++i) {
            t.move(0, a0 + (a1 - a0) * i / 10, p)
                .move(1, b0 + (b1 - b0) * i / 10, p).commit();
        }
        t.release(0, a1, p).release(1, b1, p).commit();
    }

    bool analysed(Analyser *a) {
        return a && a->getLayer(Analyser::PitchTrack) &&
            a->getLayer(Analyser::Notes) &&
            a->getInitialAnalysisCompletion() >= 100 &&
            !a->isAnalysingRange() &&
            !sv::ModelTransformerFactory::getInstance()
            ->haveRunningTransformers();
    }

    bool analysed() { return analysed(m_window->analyser()); }

    // A wav file in the test's directory, written the first time it is
    // asked for; "" if it could not be
    QString wavFile(QString name, const std::vector<float> &data) {
        QString path = m_dir.filePath(name);
        if (QFile::exists(path)) return path;
        sv::WavFileWriter writer(path, rate, 1,
                                 sv::WavFileWriter::WriteToTarget);
        const float *ptr = data.data();
        if (!writer.isOK() ||
            !writer.writeSamples(&ptr, sv::sv_frame_t(data.size())) ||
            !writer.close()) {
            return "";
        }
        return path;
    }

    // Six seconds of a sine
    QString sineFile(QString name, double hz) {
        return wavFile(name, TestSignals::sine(hz, rate, int(6 * rate), 0.5));
    }

    // The view at startLevel about startCentre
    void resetView() {
        pane()->setZoomLevel
            (sv::ZoomLevel(sv::ZoomLevel::FramesPerPixel, startLevel));
        pane()->setCentreFrame(startCentre);
    }

    // The window on show with six seconds of reference, a sine at
    // referenceHz unless another file is given, analysed, and the view at
    // startLevel about startCentre
    void openWindow(QString path = "") {
        m_window = new TouchTestWindow;
        m_window->resize(1000, 700);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));

        if (path == "") path = sineFile("reference.wav", referenceHz);
        QVERIFY(path != "");

        m_window->discardModifications();
        QCOMPARE(m_window->openPath(path, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(), 30000);

        QVERIFY(pane());
        QVERIFY(strip());
        resetView();
        QCOMPARE(framesPerPixel(), double(startLevel));
    }

    // An analyser's pitch and notes hidden or shown again, straight to
    // the layers as the app does for a while: Analyser::setVisible()
    // writes a setting that both analysers share
    void showPitch(Analyser *a, bool shown) {
        a->getLayer(Analyser::PitchTrack)->showLayer(pane(), shown);
        a->getLayer(Analyser::Notes)->showLayer(pane(), shown);
    }

    // Where a zoom by factor about a value at y puts it: towards the
    // middle of the pane, its distance from there divided by the factor
    double pulledTowardsMiddle(double y, double factor) {
        double middle = pane()->height() / 2.0;
        return middle + (y - middle) / factor;
    }

    // How far, in pixels, a frequency on r may be from where the fingers
    // put it: the range is whole Hz (the spectrogram's), so each end may
    // be half a hertz from theirs, which low down is a few pixels
    double slack(const VerticalZoom::Range &r) {
        return 1.0 + pane()->height() *
            (std::log2((r.min + 0.5) / r.min) +
             std::log2((r.max + 0.5) / r.max)) / octaves(r);
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

        // As main() does; without it a .ton file is not a session
        sv::InteractiveFileFinder::getInstance()
            ->setApplicationSessionExtension("ton");

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

    // Fingers spread up the pane: the frequency range narrows by as
    // much as they spread, less what the dead zone took, about the
    // pitch on show, the reference's sine, low in the pane and well
    // below the fingers, which comes towards the middle of the pane by
    // as much. The time axis stays as it was, the pitch layers in the
    // pane are drawn on the new range, and nothing goes into the undo
    // history
    void vertical_pinch_narrows_the_frequency_range() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        m_window->toggleAlternatePitch();
        QVERIFY(m_window->alternatePitch()->isShown());

        sv::Pane *p = pane();
        QVERIFY2(p->height() >= 300,
                 qPrintable(QString("the pane is %1 high").arg(p->height())));
        int x = p->width() / 2 + 100;
        int y = p->height() / 2 - 60;
        sv::sv_frame_t centre = p->getCentreFrame();
        VerticalZoom::Range before = frequencyRange();
        QCOMPARE(before.min, 40.0);
        QCOMPARE(before.max, 1500.0);
        QVERIFY(m_window->analyser()->setDisplayFrequencyExtents(150, 1200));
        before = frequencyRange();
        double pitchY = yForFrequency(referenceHz);
        QVERIFY2(pitchY > 0.75 * p->height(),
                 qPrintable(QString("the pitch is at %1").arg(pitchY)));

        int commands = 0;
        auto counted = connect(sv::CommandHistory::getInstance(),
                               qOverload<>(&sv::CommandHistory::commandExecuted),
                               this, [&commands]() { ++commands; });
        twoFingers(p, QPoint(x, y - 50), QPoint(x, y + 50),
                   QPoint(x, y - 100), QPoint(x, y + 100));
        disconnect(counted);
        QCOMPARE(commands, 0);

        VerticalZoom::Range after = frequencyRange();
        double factor = (200.0 - deadZone()) / 100.0;
        double narrowed = octaves(before) / octaves(after);
        QVERIFY2(std::fabs(narrowed - factor) < 0.01 * factor,
                 qPrintable(QString("%1, then %2: %3 times narrower, not %4")
                            .arg(text(before)).arg(text(after))
                            .arg(narrowed).arg(factor)));
        double expected = pulledTowardsMiddle(pitchY, factor);
        QVERIFY2(std::fabs(yForFrequency(referenceHz) - expected) <=
                 slack(after),
                 qPrintable(QString("the pitch was at %1, then at %2, not %3")
                            .arg(pitchY).arg(yForFrequency(referenceHz))
                            .arg(expected)));

        // As far as the centre goes, to the whole pixel the fingers hold
        QCOMPARE(framesPerPixel(), double(startLevel));
        QVERIFY(std::abs(p->getCentreFrame() - centre) < startLevel);

        // The reference's pitch and notes, and the alternate pitch track
        // that follows them
        Analyser *a = m_window->analyser();
        std::vector<sv::Layer *> layers {
            a->getLayer(Analyser::PitchTrack),
            a->getLayer(Analyser::Notes),
            m_window->alternatePitch()->getLayer()
        };
        for (sv::Layer *layer : layers) {
            QVERIFY(layer);
            sv::CoordinateScale scale =
                p->getEffectiveVerticalExtentsForLayer(layer);
            QVERIFY2(scale.isLogarithmic() &&
                     scale.getDisplayMinimum() == after.min &&
                     scale.getDisplayMaximum() == after.max,
                     qPrintable(QString("%1 is drawn from %2 to %3")
                                .arg(layer->objectName())
                                .arg(scale.getDisplayMinimum())
                                .arg(scale.getDisplayMaximum())));
        }

        QCOMPARE(m_window->menuRequests, 0);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // Fingers brought together up the pane: the range widens, about
    // the pitch on show, which stays where it was
    void vertical_pinch_widens_the_frequency_range() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->analyser()->setDisplayFrequencyExtents(100, 800));

        sv::Pane *p = pane();
        int x = p->width() / 2 - 100;
        int y = p->height() / 2 - 40;
        VerticalZoom::Range before = frequencyRange();
        QCOMPARE(before.min, 100.0);
        double pitchY = yForFrequency(referenceHz);
        QVERIFY(pitchY > y + 20);

        twoFingers(p, QPoint(x, y - 100), QPoint(x, y + 100),
                   QPoint(x, y - 50), QPoint(x, y + 50));

        VerticalZoom::Range after = frequencyRange();
        double factor = (100.0 + deadZone()) / 200.0;
        double narrowed = octaves(before) / octaves(after);
        QVERIFY2(std::fabs(narrowed - factor) < 0.01 * factor,
                 qPrintable(QString("%1, then %2: %3 times narrower, not %4")
                            .arg(text(before)).arg(text(after))
                            .arg(narrowed).arg(factor)));
        QVERIFY2(std::fabs(yForFrequency(referenceHz) - pitchY) <= 1.0,
                 qPrintable(QString("the pitch was at %1, then at %2")
                            .arg(pitchY).arg(yForFrequency(referenceHz))));
        QCOMPARE(framesPerPixel(), double(startLevel));
    }

    // Fingers spread across the pane, not quite level, and drifting down
    // a little: the time axis zooms, and the frequency range is left
    // exactly as it was. Their spread up the pane grows by more than the
    // dead zone, but by less than half as much as across
    void horizontal_pinch_leaves_the_frequency_range() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int x = p->width() / 2;
        int y = p->height() / 2;
        int drift = deadZone() / 2;
        VerticalZoom::Range before = frequencyRange();

        twoFingers(p, QPoint(x - 50, y - 15), QPoint(x + 50, y + 15),
                   QPoint(x - 150, y - 35 + drift),
                   QPoint(x + 150, y + 35 + drift));

        QVERIFY(framesPerPixel() < startLevel / 2.0);
        VerticalZoom::Range after = frequencyRange();
        QCOMPARE(after.min, before.min);
        QCOMPARE(after.max, before.max);
    }

    // Spread along both: both zoom, time about the fingers and the
    // frequency range about the pitch
    void diagonal_pinch_zooms_time_and_frequency() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->analyser()->setDisplayFrequencyExtents(100, 800));

        sv::Pane *p = pane();
        int x = p->width() / 2 - 100;
        int y = p->height() / 2 - 30;
        sv::sv_frame_t frame = p->getFrameForX(x);
        VerticalZoom::Range before = frequencyRange();
        double pitchY = yForFrequency(referenceHz);

        twoFingers(p, QPoint(x - 50, y - 50), QPoint(x + 50, y + 50),
                   QPoint(x - 100, y - 100), QPoint(x + 100, y + 100));

        QCOMPARE(framesPerPixel(), startLevel / 2.0);
        QVERIFY2(std::abs(p->getFrameForX(x) - frame) <= startLevel / 2,
                 qPrintable(QString("frame %1 under the fingers, then %2")
                            .arg(frame).arg(p->getFrameForX(x))));

        VerticalZoom::Range after = frequencyRange();
        double factor = (200.0 - deadZone()) / 100.0;
        double narrowed = octaves(before) / octaves(after);
        QVERIFY2(std::fabs(narrowed - factor) < 0.01 * factor,
                 qPrintable(QString("%1, then %2: %3 times narrower, not %4")
                            .arg(text(before)).arg(text(after))
                            .arg(narrowed).arg(factor)));
        double expected = pulledTowardsMiddle(pitchY, factor);
        QVERIFY2(std::fabs(yForFrequency(referenceHz) - expected) <=
                 slack(after),
                 qPrintable(QString("the pitch was at %1, then at %2, not %3")
                            .arg(pitchY).arg(yForFrequency(referenceHz))
                            .arg(expected)));
    }

    // With no pitch on show, the range zooms about the frequency between
    // the fingers, which stays there: the reference's pitch and notes
    // hidden, and then shown but below the range
    void vertical_pinch_with_no_pitch_on_show_zooms_about_the_fingers() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int x = p->width() / 2 + 50;
        int y = p->height() / 2 - 60;
        Analyser *a = m_window->analyser();

        for (bool hidden : { true, false }) {
            showPitch(a, !hidden);
            QVERIFY(a->setDisplayFrequencyExtents(hidden ? 100 : 300, 1200));
            VerticalZoom::Range before = frequencyRange();
            double held = frequencyAtY(y);

            twoFingers(p, QPoint(x, y - 50), QPoint(x, y + 50),
                       QPoint(x, y - 100), QPoint(x, y + 100));

            QString what = QString(hidden ? "hidden" : "below the range");
            VerticalZoom::Range after = frequencyRange();
            double factor = (200.0 - deadZone()) / 100.0;
            double narrowed = octaves(before) / octaves(after);
            QVERIFY2(std::fabs(narrowed - factor) < 0.01 * factor,
                     qPrintable(what + QString(": %1 times narrower, not %2")
                                .arg(narrowed).arg(factor)));
            QVERIFY2(std::fabs(yForFrequency(held) - y) <= 1.0,
                     qPrintable(what + QString(": %1 Hz was at %2, then at %3")
                                .arg(held).arg(y).arg(yForFrequency(held))));
        }
    }

    // The fifth phone test: a low voice, C2 and G2 by turns, near the
    // bottom of the range as it opens, and the fingers spread up the
    // middle of the pane, far above it. At every step, as the range
    // narrows, both notes stay in view, and they end nearer the middle.
    // Zoomed about the fingers, G2 went below the pane half way through
    void low_pitch_stays_in_view_as_the_range_narrows() {
        std::vector<float> voice;
        for (int i = 0; i < 12; ++i) {
            std::vector<float> note = TestSignals::sine
                (i % 2 ? 98.0 : 65.4, rate, int(rate / 2), 0.5);
            voice.insert(voice.end(), note.begin(), note.end());
        }
        QString path = wavFile("low-voice.wav", voice);
        QVERIFY(path != "");
        openWindow(path);
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int height = p->height();
        int x = p->width() / 2;
        int y = height / 2;
        VerticalZoom::Range before = frequencyRange();
        QCOMPARE(before.min, 40.0);
        QCOMPARE(before.max, 1500.0);
        double lowY = yForFrequency(65.4);
        double highY = yForFrequency(98.0);
        QVERIFY2(highY > 0.7 * height,
                 qPrintable(QString("G2 at %1 of %2").arg(highY).arg(height)));

        int from = 30;
        int to = height / 2 - 20;
        Touch t = touch();
        t.press(0, QPoint(x, y - from), p).press(1, QPoint(x, y + from), p)
            .commit();
        for (int i = 1; i <= 10; ++i) {
            int d = from + (to - from) * i / 10;
            t.move(0, QPoint(x, y - d), p).move(1, QPoint(x, y + d), p)
                .commit();
            double low = yForFrequency(65.4);
            double high = yForFrequency(98.0);
            QVERIFY2(low <= height && high >= 0,
                     qPrintable(QString("step %1, %2: C2 at %3, G2 at %4 "
                                        "of %5")
                                .arg(i).arg(text(frequencyRange()))
                                .arg(low).arg(high).arg(height)));
        }
        t.release(0, QPoint(x, y - to), p).release(1, QPoint(x, y + to), p)
            .commit();

        VerticalZoom::Range after = frequencyRange();
        QVERIFY2(octaves(after) < octaves(before) / 4,
                 qPrintable(text(before) + ", then " + text(after)));
        double middle = height / 2.0;
        double lowAfter = yForFrequency(65.4);
        double highAfter = yForFrequency(98.0);
        QVERIFY2(std::fabs((lowAfter + highAfter) / 2 - middle) <
                 std::fabs((lowY + highY) / 2 - middle) / 4,
                 qPrintable(QString("C2 and G2 at %1 and %2, then at %3 and "
                                    "%4, of %5")
                            .arg(lowY).arg(highY).arg(lowAfter)
                            .arg(highAfter).arg(height)));
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // The singing's pitch counts as the reference's does. A low voice
    // sung to a high reference: with the reference's pitch and notes
    // hidden the zoom keeps the singing's in view, and with both on
    // show it is about the middle between the two
    void singing_pitch_on_show_is_kept_in_view() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        QString singing = sineFile("singing.wav", lowHz);
        QVERIFY(singing != "");
        m_window->loadSingingTrack(singing);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        QVERIFY(m_window->analyser2()->getPane() == pane());
        resetView();

        sv::Pane *p = pane();
        int x = p->width() / 2;
        int y = p->height() / 4;
        Analyser *a = m_window->analyser();

        for (bool reference : { false, true }) {
            showPitch(a, reference);
            QVERIFY(a->setDisplayFrequencyExtents(40, 1500));
            double kept = reference ? std::sqrt(lowHz * referenceHz) : lowHz;
            double keptY = yForFrequency(kept);

            twoFingers(p, QPoint(x, y - 40), QPoint(x, y + 40),
                       QPoint(x, y - 80), QPoint(x, y + 80));

            double factor = (160.0 - deadZone()) / 80.0;
            double expected = pulledTowardsMiddle(keptY, factor);
            VerticalZoom::Range after = frequencyRange();
            QVERIFY2(std::fabs(yForFrequency(kept) - expected) <=
                     slack(after),
                     qPrintable(QString("%1 Hz was at %2, then at %3, not "
                                        "%4, on %5")
                                .arg(kept).arg(keptY)
                                .arg(yForFrequency(kept)).arg(expected)
                                .arg(text(after))));
        }
    }

    // Two fingers side by side dragged down the pane: what was between
    // them goes down with them, as far as they went beyond the dead
    // zone, and the range keeps its height. Time stays where it was
    void vertical_two_finger_drag_scrolls_the_frequency_range() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int x = p->width() / 2;
        int y = p->height() / 2 - 80;
        sv::sv_frame_t centre = p->getCentreFrame();
        VerticalZoom::Range before = frequencyRange();
        double held = frequencyAtY(y);

        twoFingers(p, QPoint(x - 50, y), QPoint(x + 50, y),
                   QPoint(x - 50, y + 120), QPoint(x + 50, y + 120));

        VerticalZoom::Range after = frequencyRange();
        double expected = y + 120 - deadZone();
        QVERIFY2(std::fabs(yForFrequency(held) - expected) <= 1.0,
                 qPrintable(QString("%1 Hz was at %2, then at %3, not %4")
                            .arg(held).arg(y).arg(yForFrequency(held))
                            .arg(expected)));
        QVERIFY2(after.max > before.max &&
                 std::fabs(octaves(after) - octaves(before)) < 0.01,
                 qPrintable(QString("%1, then %2")
                            .arg(text(before)).arg(text(after))));

        // As far as the centre goes, to the whole pixel the fingers hold
        QCOMPARE(framesPerPixel(), double(startLevel));
        QVERIFY(std::abs(p->getCentreFrame() - centre) < startLevel);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // No narrower than a major third, no wider than the piano, and no
    // further up or down: held there, the range stays while the fingers
    // go on, and goes back with them as soon as they turn. The range is
    // whole Hz (the spectrogram's), hence the half hertz allowed
    void frequency_range_stays_within_its_limits() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int x = p->width() / 2;
        int y = p->height() / 2;
        VerticalZoom::Limits limits = VerticalZoom::pitchLimits();

        QVERIFY(m_window->analyser()->setDisplayFrequencyExtents(200, 260));
        twoFingers(p, QPoint(x, y - 50), QPoint(x, y + 50),
                   QPoint(x, y - 120), QPoint(x, y + 120));
        VerticalZoom::Range r = frequencyRange();
        double semitones = 12.0 * octaves(r);
        QVERIFY2(semitones > 3.9 && semitones < 4.1,
                 qPrintable(text(r) + QString(": %1 semitones")
                            .arg(semitones)));

        QVERIFY(m_window->analyser()->setDisplayFrequencyExtents(40, 1500));
        twoFingers(p, QPoint(x, y - 120), QPoint(x, y + 120),
                   QPoint(x, y - 20), QPoint(x, y + 20));
        r = frequencyRange();
        QVERIFY2(std::fabs(r.min - limits.lowest) <= 0.5 &&
                 std::fabs(r.max - limits.highest) <= 0.5,
                 qPrintable(text(r)));

        QVERIFY(m_window->analyser()->setDisplayFrequencyExtents(40, 1500));
        VerticalZoom::Range start = frequencyRange();
        int top = 20;
        int bottom = p->height() - 40;
        Touch t = touch();
        t.press(0, QPoint(x - 50, top), p).press(1, QPoint(x + 50, top), p)
            .commit();
        for (int i = 1; i <= 10; ++i) {
            int at = top + (bottom - top) * i / 10;
            t.move(0, QPoint(x - 50, at), p).move(1, QPoint(x + 50, at), p)
                .commit();
        }
        r = frequencyRange();
        QVERIFY2(std::fabs(r.max - limits.highest) <= 0.5 &&
                 std::fabs(octaves(r) - octaves(start)) < 0.01,
                 qPrintable(text(start) + ", then " + text(r)));

        double atTurn = frequencyAtY(bottom);
        for (int i = 1; i <= 5; ++i) {
            int at = bottom - 10 * i;
            t.move(0, QPoint(x - 50, at), p).move(1, QPoint(x + 50, at), p)
                .commit();
        }
        t.release(0, QPoint(x - 50, bottom - 50), p)
            .release(1, QPoint(x + 50, bottom - 50), p).commit();
        QVERIFY2(std::fabs(yForFrequency(atTurn) - (bottom - 50)) <= 1.0,
                 qPrintable(QString("%1 Hz was at %2, then at %3")
                            .arg(atTurn).arg(bottom)
                            .arg(yForFrequency(atTurn))));
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }

    // The range the fingers leave is the session's: saved with it, and
    // there again when it is opened
    void frequency_range_is_saved_with_the_session() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *p = pane();
        int x = p->width() / 2;
        int y = p->height() / 2;
        twoFingers(p, QPoint(x, y - 50), QPoint(x, y + 50),
                   QPoint(x, y - 100), QPoint(x, y + 120));
        VerticalZoom::Range zoomed = frequencyRange();
        QVERIFY(zoomed.min > 40.0 && zoomed.max < 1500.0);

        QString session = m_dir.filePath("zoomed.ton");
        QVERIFY(m_window->saveSessionFile(session));
        m_window->doCloseSession();
        QCOMPARE(m_window->openPath(session, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(), 30000);

        VerticalZoom::Range restored = frequencyRange();
        QCOMPARE(restored.min, zoomed.min);
        QCOMPARE(restored.max, zoomed.max);
        sv::CoordinateScale scale = pane()->getEffectiveVerticalExtentsForLayer
            (m_window->analyser()->getLayer(Analyser::PitchTrack));
        QCOMPARE(scale.getDisplayMinimum(), zoomed.min);
        QCOMPARE(scale.getDisplayMaximum(), zoomed.max);
    }

    // The selection strip shows no frequencies: two fingers dragged up
    // it leave the range alone
    void frequency_range_is_the_pitch_pane_s_only() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        sv::Pane *s = strip();
        int x = s->width() / 2;
        int y = s->height() - 5;
        VerticalZoom::Range before = frequencyRange();

        twoFingers(s, QPoint(x - 50, y), QPoint(x + 50, y),
                   QPoint(x - 50, y - 60), QPoint(x + 50, y - 60));

        VerticalZoom::Range after = frequencyRange();
        QCOMPARE(after.min, before.min);
        QCOMPARE(after.max, before.max);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
    }
};

#endif
