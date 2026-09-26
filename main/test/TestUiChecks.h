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

#ifndef TEST_UI_CHECKS_H
#define TEST_UI_CHECKS_H

// Tier 6: the window as the user sees and handles it. The same
// MainWindow and fake device as TestRecordWorkflow, but shown, driven
// with key presses, mouse gestures and its own dialogs, and judged by
// the pixels of pane 0 as it is drawn -- the items of the manual
// checklist that are about what is on the screen.
//
// With TONY_TEST_SHOT_DIR set, the suite also saves what it looked at,
// as PNG files, for a person to look over.

#include "TestSignals.h"
#include "TestMainWindow.h"

#include "../AlternatePitchTrack.h"
#include "../TakeLayers.h"

#include "version.h"

#include "framework/Document.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "layer/Layer.h"
#include "layer/ColourDatabase.h"
#include "layer/CoordinateScale.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"
#include "data/model/RegionModel.h"
#include "data/fileio/WavFileReader.h"
#include "data/fileio/WavFileWriter.h"
#include "data/fileio/FileSource.h"
#include "base/RecordDirectory.h"
#include "transform/ModelTransformerFactory.h"
#include "widgets/CommandHistory.h"
#include "widgets/InteractiveFileFinder.h"

#include <QObject>
#include <QtTest>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>

#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <vector>

class TestUiChecks : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;

    // Whole numbers of samples per period: see TestSingingAnalysis.h
    static constexpr double lowHz = 220.5;
    static constexpr double highHz = 294.0;

    QTemporaryDir m_dir;
    int m_fileCounter = 0;
    TestMainWindow *m_window = nullptr;
    QTimer m_watchdog;
    QStringList m_dialogs;
    QString m_shotDir;

    // Given each modal dialog before the watchdog dismisses it: true if
    // it answered the dialog itself
    std::function<bool(QWidget *)> m_answerDialog;

    static std::vector<float> tone(double hz, double seconds) {
        return TestSignals::sawtooth(hz, rate, int(seconds * rate), 0.5f);
    }

    static sv::sv_frame_t frames(double seconds) {
        return sv::sv_frame_t(seconds * rate);
    }

    QString writeWav(const std::vector<float> &data) {
        QString path = m_dir.filePath
            (QString("audio-%1.wav").arg(++m_fileCounter));
        sv::WavFileWriter writer(path, rate, 1,
                                 sv::WavFileWriter::WriteToTarget);
        const float *ptr = data.data();
        if (!writer.isOK() ||
            !writer.writeSamples(&ptr, sv::sv_frame_t(data.size())) ||
            !writer.close()) {
            return {};
        }
        return path;
    }

    // Shown at a fixed size and active, as the user has it: key presses
    // reach the window's shortcuts only when it is the active window
    void makeWindow(FakeAudioIO::Config config) {
        delete m_window;
        m_window = new TestMainWindow(config);
        m_window->resize(1200, 800);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_window));
    }

    static bool analysed(Analyser *a) {
        return a && a->getLayer(Analyser::PitchTrack) &&
            a->getLayer(Analyser::Notes) &&
            a->getInitialAnalysisCompletion() >= 100 &&
            !a->isAnalysingRange() &&
            !sv::ModelTransformerFactory::getInstance()
            ->haveRunningTransformers();
    }

    void openReference(QString path) {
        QVERIFY(!path.isEmpty());
        m_window->discardModifications();
        QCOMPARE(m_window->openPath(path, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
    }

    void startTake() {
        QVERIFY(!m_window->recordTarget()->isRecording());
        m_window->doRecord();
        QVERIFY(m_window->recordTarget()->isRecording());
    }

    void stopTake() {
        QVERIFY(m_window->recordTarget()->isRecording());
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
    }

    void take(int ms) {
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(ms);
        stopTake();
    }

    sv::Pane *pane0() { return m_window->paneStack()->getPane(0); }

    // Whatever is seen of the page from start to end, and a little room
    // either side
    void showSeconds(double start, double end) {
        sv::Pane *pane = pane0();
        int width = pane->width();
        int perPixel = int(std::ceil((end - start) * rate / (width * 0.8)));
        pane->setZoomLevel(sv::ZoomLevel(sv::ZoomLevel::FramesPerPixel,
                                         std::max(1, perPixel)));
        pane->setCentreFrame(frames((start + end) / 2.0));
    }

    // Pane 0 as it is on the screen just now: the window's backing store,
    // which holds what the pane's own paint events put there. Not
    // QWidget::grab(), which has the pane paint itself once more for the
    // occasion and so can show what the screen does not. The pane is
    // painted first, as it would be at the next update: a pane that has
    // just turned a page is still the old page on the screen until then,
    // while every position asked of it is on the new one
    QImage grabPane() {
        QCoreApplication::processEvents();
        pane0()->repaint();
        QPixmap window = m_window->screen()->grabWindow(m_window->winId());
        QRect rect(pane0()->mapTo(m_window, QPoint(0, 0)), pane0()->size());
        return window.copy(rect).toImage()
            .convertToFormat(QImage::Format_RGB32);
    }

    // ... and drawn afresh, all of it. After playback the pane's own
    // cache of what it drew holds the translucent note boxes painted a
    // second time over themselves, until the next zoom or scroll; that
    // is how the view composes itself, not what a layer draws, and would
    // otherwise be taken for a change to the layers
    QImage grabPaneRedrawn() {
        sv::Pane *pane = pane0();
        sv::ZoomLevel zoom = pane->getZoomLevel();
        sv::sv_frame_t centre = pane->getCentreFrame();
        // (a zoom level only one step away snaps back to this one)
        pane->setZoomLevel(sv::ZoomLevel(zoom.zone, zoom.level * 2));
        grabPane();
        pane->setZoomLevel(zoom);
        pane->setCentreFrame(centre);
        return grabPane();
    }

    void saveShot(QString name, const QImage &image) {
        if (m_shotDir == "") return;
        QDir().mkpath(m_shotDir);
        QString test = QTest::currentTestFunction();
        image.save(QDir(m_shotDir).filePath(test + "-" + name + ".png"));
    }

    // The whole window, as grabPane() takes the pane
    void saveWindowShot(QString name) {
        if (m_shotDir == "") return;
        QCoreApplication::processEvents();
        saveShot(name, m_window->screen()->grabWindow(m_window->winId())
                 .toImage());
    }

    static bool closeTo(QRgb pixel, QColor colour, int tolerance) {
        return std::abs(qRed(pixel) - colour.red()) <= tolerance &&
            std::abs(qGreen(pixel) - colour.green()) <= tolerance &&
            std::abs(qBlue(pixel) - colour.blue()) <= tolerance;
    }

    static QColor named(QString name) {
        auto cdb = sv::ColourDatabase::getInstance();
        return cdb->getColour(cdb->getColourIndex(name));
    }

    // The orange of the live dots, the take's pitch track and the
    // coverage strip; the purple of the take's notes
    static bool isOrange(QRgb pixel) {
        return closeTo(pixel, named("Orange"), 30);
    }
    static bool isPurple(QRgb pixel) {
        return closeTo(pixel, named("Bright Purple"), 30);
    }

    // Orange, or orange seen through the translucent fill of the take's
    // notes, which are drawn over its pitch track: what is sung, as it is
    // drawn. Not the brighter orange of the pitch candidates
    static bool isSinging(QRgb pixel) {
        return qRed(pixel) >= 200 && qGreen(pixel) >= 90 &&
            qGreen(pixel) <= 170 && qBlue(pixel) <= 140;
    }

    // Pixels of a colour in the columns [x0, x1), above the band of the
    // coverage strip at the bottom of the pane
    static int countAbove(const QImage &image, int x0, int x1,
                          std::function<bool(QRgb)> is) {
        int n = 0;
        x0 = std::max(0, x0);
        x1 = std::min(image.width(), x1);
        for (int x = x0; x < x1; ++x) {
            for (int y = 0; y < image.height() - 10; ++y) {
                if (is(image.pixel(x, y))) ++n;
            }
        }
        return n;
    }

    // Leftmost and rightmost column holding such a pixel, or -1
    static std::pair<int, int> extentAbove(const QImage &image,
                                           std::function<bool(QRgb)> is) {
        int left = -1, right = -1;
        for (int x = 0; x < image.width(); ++x) {
            for (int y = 0; y < image.height() - 10; ++y) {
                if (is(image.pixel(x, y))) {
                    if (left < 0) left = x;
                    right = x;
                    break;
                }
            }
        }
        return { left, right };
    }

    // The play pointer as View::drawPlayPointer() draws it: a line of the
    // background colour between two of the foreground, the whole height
    // of the pane. -1 if there is none
    static int pointerX(const QImage &image) {
        auto dark = [](QRgb p) { return qGray(p) < 80; };
        auto light = [](QRgb p) { return qGray(p) > 200; };
        int h = image.height();
        for (int x = 1; x + 1 < image.width(); ++x) {
            int n = 0;
            for (int y = 1; y + 1 < h; ++y) {
                if (dark(image.pixel(x - 1, y)) && light(image.pixel(x, y)) &&
                    dark(image.pixel(x + 1, y))) ++n;
            }
            if (n > (h * 9) / 10) return x;
        }
        return -1;
    }

    QMenu *menuTitled(QString title) {
        for (QAction *a : m_window->menuBar()->actions()) {
            if (a->menu() && a->text() == title) return a->menu();
        }
        return nullptr;
    }

    // The Undo item of the Edit menu, as the user reads it
    QString undoText() {
        QMenu *edit = menuTitled(tr("&Edit"));
        if (!edit) return "(no Edit menu)";
        for (QAction *a : edit->actions()) {
            if (a->shortcut() == QKeySequence(tr("Ctrl+Z"))) return a->text();
        }
        return "(no Undo item)";
    }

    void press(QKeySequence keys) {
        QVERIFY(QTest::qWaitForWindowActive(m_window));
        QTest::keySequence(m_window, keys);
        QCoreApplication::processEvents();
    }

    // The pointer over pane 0 with no button held. Where it was last over
    // a note decides what the Edit tool does to that note: near its top a
    // drag moves it, near its bottom a click splits it, and with the
    // pointer never over a note a drag moves it. Sent to the pane itself:
    // QTest::mouseMove() with no button held moves the platform's cursor
    // instead, which the offscreen platform need not pass on
    void hover(QPoint pos) {
        sv::Pane *pane = pane0();
        QMouseEvent move(QEvent::MouseMove, QPointF(pos),
                         QPointF(pane->mapToGlobal(pos)),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(pane, &move);
    }

    // A drag across pane 0 with the left button, in ten steps
    void drag(QPoint from, QPoint to) {
        sv::Pane *pane = pane0();
        QTest::mousePress(pane, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= 10; ++i) {
            QTest::mouseMove(pane, from + (to - from) * i / 10);
            QTest::qWait(10);
        }
        QTest::mouseRelease(pane, Qt::LeftButton, Qt::NoModifier, to);
    }

    Coverage::Ranges coverage() {
        return m_window->takes()->getCoverage().getRanges();
    }

    // Everything in pane 0 a gesture could change: the events of every
    // layer's model, how each layer scales itself, the zoom, the
    // selection and the undo history. The centre frame is left out:
    // dragging with the navigate tool is meant to scroll
    struct PaneState {
        std::map<QString, sv::EventVector> events;
        std::map<QString, std::pair<double, double>> extents;
        std::vector<QString> layers;
        sv::ZoomLevel zoom;
        sv::MultiSelection::SelectionList selections;
        QString undo;
        // Those of the layers that are pitch candidates of a
        // re-analysis. Not compared: layers has them
        std::set<QString> candidates;

        bool operator==(const PaneState &s) const {
            return events == s.events && extents == s.extents &&
                layers == s.layers && zoom == s.zoom &&
                selections == s.selections && undo == s.undo;
        }
    };

    PaneState paneState() {
        PaneState s;
        sv::Pane *pane = pane0();
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            sv::Layer *layer = pane->getLayer(i);
            QString key = QString("%1 %2").arg(i).arg(layer->objectName());
            s.layers.push_back(key);
            if (layer->getLayerPresentationName() == "candidate") {
                s.candidates.insert(key);
            }
            sv::ModelId id = layer->getModel();
            if (auto m = sv::ModelById::getAs<sv::SparseTimeValueModel>(id)) {
                s.events[key] = m->getAllEvents();
            } else if (auto m = sv::ModelById::getAs<sv::NoteModel>(id)) {
                s.events[key] = m->getAllEvents();
            } else if (auto m = sv::ModelById::getAs<sv::RegionModel>(id)) {
                s.events[key] = m->getAllEvents();
            }
            double min = 0.0, max = 0.0;
            if (layer->getDisplayExtents(min, max)) {
                s.extents[key] = { min, max };
            }
        }
        s.zoom = pane->getZoomLevel();
        s.selections = m_window->selections();
        s.undo = undoText();
        return s;
    }

    static QString describe(const PaneState &a, const PaneState &b) {
        QStringList what;
        if (a.layers != b.layers) what << "the layers of the pane";
        for (const auto &e : a.events) {
            auto i = b.events.find(e.first);
            if (i == b.events.end() || i->second != e.second) {
                what << QString("the events of \"%1\"").arg(e.first);
            }
        }
        for (const auto &e : a.extents) {
            auto i = b.extents.find(e.first);
            if (i == b.extents.end() || i->second != e.second) {
                what << QString("the scale of \"%1\"").arg(e.first);
            }
        }
        if (!(a.zoom == b.zoom)) what << "the zoom";
        if (a.selections != b.selections) what << "the selection";
        if (a.undo != b.undo) what << "the undo history (" + b.undo + ")";
        return what.join(", ");
    }

    // Not a slot: QtTest would run it as a test
    void dismissDialog() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) return;
        if (m_answerDialog && m_answerDialog(modal)) return;
        QString description = modal->windowTitle();
        if (auto box = qobject_cast<QMessageBox *>(modal)) {
            description += ": " + box->text();
            m_dialogs.push_back(description);
            // The last button is Cancel (or No) in every question Tony
            // asks; see TestRecordWorkflow::dismissDialog()
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

    // Press the button of this role in a message box, ticking its check
    // box first if asked to; the text of the box goes into asked
    static std::function<bool(QWidget *)>
    answerWith(QMessageBox::StandardButton button, bool tick,
               QStringList *asked) {
        return [=](QWidget *modal) {
            auto box = qobject_cast<QMessageBox *>(modal);
            if (!box || !box->button(button)) return false;
            // The text, not the title: macOS shows no title on a message
            // box, and Qt keeps none there
            if (asked) asked->push_back(box->text());
            if (tick && box->checkBox()) box->checkBox()->setChecked(true);
            box->button(button)->click();
            return true;
        };
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        m_shotDir = qEnvironmentVariable("TONY_TEST_SHOT_DIR");

        QSettings().clear();

        // As TestRecordWorkflow: no network question, .ton is a session,
        // takes go into the temporary directory
        QSettings settings;
        settings.beginGroup("Preferences");
        settings.setValue(QString("network-permission-%1").arg(TONY_VERSION),
                          false);
        settings.endGroup();

        sv::InteractiveFileFinder::getInstance()
            ->setApplicationSessionExtension("ton");

        sv::RecordDirectory::setRecordContainerDirectory
            (m_dir.filePath("recorded"));

        connect(&m_watchdog, &QTimer::timeout,
                this, [this]() { dismissDialog(); });
        m_watchdog.start(50);
    }

    void init() {
        m_dialogs.clear();
        m_answerDialog = nullptr;
        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.setValue("playrefwhilerecording", false);
        settings.setValue("preroll", false);
        settings.setValue("recordintoselection", false);
        settings.remove("prerollseconds");
        settings.endGroup();
        settings.beginGroup("Analyser");
        settings.remove("");
        settings.endGroup();
        SingingTakes::setOverwriteConfirmationWanted(true);
    }

    void cleanup() {
        m_answerDialog = nullptr;
        if (m_window) {
            if (m_window->recordTarget()->isRecording()) {
                m_window->doRecord();
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
        sv::RecordDirectory::setRecordContainerDirectory("");
    }

    // Checklist: live dots appear under the playback cursor, not behind
    // it; during a take at P > 0 the cursor starts at P, the pane follows
    // it, and cursor and dots are in the same place. Singing exactly in
    // time with the reference, through a device with latency
    void live_dots_under_the_cursor() {
        FakeAudioIO::Config config;
        config.recordLatency = 512;
        config.playbackLatency = 1024;
        config.inputDelay = 1536;
        config.inputFollowsPlayback = true;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(tone(lowHz, 12.0)));
        if (QTest::currentTestFailed()) return;

        // A page of about two seconds, so that the take runs off it
        const sv::sv_frame_t P = frames(4.0);
        showSeconds(3.5, 5.5);
        m_window->seekTo(P);
        startTake();
        if (QTest::currentTestFailed()) return;

        // The most the newest dot may trail the cursor by: the latency of
        // the device, YIN's window and the delivery of the dots, with room
        // to spare. A dot a whole latency out, or one at the take's own
        // frame rather than the song's, is far more than this
        const sv::sv_frame_t lag = frames(0.3);
        sv::Pane *pane = pane0();
        QElapsedTimer timer;
        timer.start();
        int pages = 0, looked = 0;
        int firstPageStart = -1;
        bool sawFirstDots = false;
        sv::sv_frame_t worstLag = 0;
        QString worst;

        while (timer.elapsed() < 3000) {
            QTest::qWait(150);
            sv::sv_frame_t cursorBefore = m_window->playbackFrame();
            QImage image = grabPane();
            sv::sv_frame_t cursor = m_window->playbackFrame();
            int x = pointerX(image);
            QVERIFY2(x >= 0,
                     qPrintable(QString("%1 ms into the take there is no play "
                                        "pointer on the pane: it has not "
                                        "followed the cursor (frame %2)")
                                .arg(timer.elapsed()).arg(cursor)));
            // The view moves its pointer on a timer of its own (20 ms in
            // the svgui fork), so it may be one tick behind
            int earliest = pane->getXForFrame(cursorBefore - frames(0.05));
            int latest = pane->getXForFrame(cursor);
            QVERIFY2(x >= earliest - 2 && x <= latest + 2,
                     qPrintable(QString("the pointer is drawn at x = %1, the "
                                        "playback frame is between x = %2 "
                                        "and %3")
                                .arg(x).arg(earliest).arg(latest)));

            int start = int(pane->getStartFrame());
            if (firstPageStart < 0) firstPageStart = start;
            else if (start != firstPageStart) ++pages;

            auto dots = extentAbove(image, isSinging);
            if (dots.second < 0) continue;
            ++looked;

            // Never ahead of the cursor; how far behind it, and the
            // newest dot the model has, for the worst moment
            sv::sv_frame_t newest = pane->getFrameForX(dots.second);
            QVERIFY2(newest <= cursor + pane->getZoomLevel().level * 3,
                     qPrintable(QString("%1 ms into the take a dot is drawn at "
                                        "frame %2, ahead of the cursor at %3")
                                .arg(timer.elapsed()).arg(newest)
                                .arg(cursor)));
            if (cursor - newest > worstLag) {
                worstLag = cursor - newest;
                sv::sv_frame_t inModel = -1;
                if (auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
                    (m_window->realtimeModelId())) {
                    auto all = model->getAllEvents();
                    if (!all.empty()) inModel = all.back().getFrame();
                }
                worst = QString("%1 ms into the take the newest dot drawn is "
                                "%2 ms behind the cursor; the newest in the "
                                "model is %3 ms behind it")
                    .arg(timer.elapsed())
                    .arg(double(cursor - newest) * 1000.0 / rate, 0, 'f', 0)
                    .arg(double(cursor - inModel) * 1000.0 / rate, 0, 'f', 0);
            }

            // The first dots of the take are at P, where the singing began
            if (!sawFirstDots && pane->getStartFrame() < P) {
                sawFirstDots = true;
                sv::sv_frame_t oldest = pane->getFrameForX(dots.first);
                QVERIFY2(std::llabs(oldest - P) < frames(0.1),
                         qPrintable(QString("the first dot of a take at frame "
                                            "%1 is at frame %2")
                                    .arg(P).arg(oldest)));
                saveShot("first-dots", image);
                qInfo("newest dot %.0f ms behind the cursor",
                      double(cursor - newest) * 1000.0 / rate);
            }
        }
        QVERIFY2(looked >= 5, "hardly any live dots were drawn");
        QVERIFY(sawFirstDots);
        QVERIFY2(pages >= 1, "the take never ran off the first page, so "
                 "whether the pane follows it was not seen");
        saveWindowShot("during");
        qInfo("%s", qPrintable(worst));
        QVERIFY2(worstLag <= lag, qPrintable(worst));

        // The dots stay until the take's pitch track is there, which is
        // drawn over the same place in the same orange: at no moment is
        // what was sung missing from the pane
        const sv::sv_frame_t end = m_window->playbackFrame();
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        showSeconds(4.0, 4.0 + double(end - P) / rate);
        int sungFrom = pane->getXForFrame(P + frames(0.2));
        int sungTo = pane->getXForFrame(end - frames(0.5));
        QElapsedTimer sinceStop;
        sinceStop.start();
        while (!analysed(m_window->analyser2())) {
            QVERIFY2(countAbove(grabPane(), sungFrom, sungTo, isSinging) > 0,
                     qPrintable(QString("%1 ms after Stop nothing of what was "
                                        "sung is drawn")
                                .arg(sinceStop.elapsed())));
            QVERIFY(sinceStop.elapsed() < 30000);
            QTest::qWait(20);
        }
        QImage after = grabPane();
        QVERIFY(countAbove(after, sungFrom, sungTo, isSinging) > 0);
        saveShot("analysed", after);

        // The status bar is still once nothing is recorded or played
        QTest::qWait(100);
        QString status = m_window->statusText();
        QTest::qWait(400);
        QCOMPARE(m_window->statusText(), status);
    }

    // Checklist: recording over singing that is there, that take's own
    // pitch track and notes are out of sight for the take, so only the
    // dots are drawn, and they are back when the take stops
    void own_pitch_out_of_sight_while_recording_over_it() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 8.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(2200);
        if (QTest::currentTestFailed()) return;
        showSeconds(0.0, 3.0);
        sv::Pane *pane = pane0();

        const sv::sv_frame_t P = frames(0.4);
        int ahead0 = pane->getXForFrame(frames(1.3));
        int ahead1 = pane->getXForFrame(frames(1.9));
        QImage before = grabPane();
        QVERIFY2(countAbove(before, ahead0, ahead1, isSinging) > 0,
                 "the first take's pitch track is not drawn");
        QVERIFY2(countAbove(before, 0, before.width(), isPurple) > 0,
                 "the first take's notes are not drawn");
        saveShot("before", before);

        m_window->seekTo(P);
        m_window->setRecordOverAnswer(true);
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(500);

        // Ahead of the cursor, where the old singing is and nothing new
        // has been sung yet, nothing of the take is drawn. (Record puts
        // the view back on P, so where things are is asked again)
        QImage during = grabPane();
        ahead0 = pane->getXForFrame(frames(1.3));
        ahead1 = pane->getXForFrame(frames(1.9));
        int x = pointerX(during);
        QVERIFY2(x >= 0 && x < ahead0,
                 qPrintable(QString("the pointer is at x = %1, the old "
                                    "singing to look at from x = %2")
                            .arg(x).arg(ahead0)));
        QVERIFY2(countAbove(during, ahead0, ahead1, isSinging) == 0,
                 "the take's own pitch track is drawn during a take over it");
        QVERIFY2(countAbove(during, 0, during.width(), isPurple) == 0,
                 "the take's own notes are drawn during a take over it");
        QVERIFY2(countAbove(during, pane->getXForFrame(P), x, isSinging) > 0,
                 "no live dots behind the cursor");
        saveShot("during", during);

        stopTake();
        if (QTest::currentTestFailed()) return;
        QImage after = grabPane();
        ahead0 = pane->getXForFrame(frames(1.3));
        ahead1 = pane->getXForFrame(frames(1.9));
        QVERIFY2(countAbove(after, ahead0, ahead1, isSinging) > 0,
                 "the rest of the old pitch track did not come back");
        QVERIFY2(countAbove(after, 0, after.width(), isPurple) > 0,
                 "the notes did not come back");
        saveShot("after", after);
    }

    // Checklist: the coverage strip cannot be touched. Clicking, double-
    // clicking and dragging on it with either tool creates, moves, selects
    // and edits nothing of its own and does not change the pane's scale.
    // The Edit tool acts on the take's note at the time in the song under
    // the pointer, at any height in the pane -- the band too, and that is
    // meant -- so an edit of that note is what it may do. A drag moves
    // the note and re-analyses the pitch under it, which may leave more
    // than one entry in the undo history; undoing them takes it all back
    // exactly, but for the pitch candidates, which are the analyser's
    void strip_ignores_the_mouse() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        // One recording, so that the band has an end to drag off
        m_window->seekTo(frames(0.5));
        take(1500);
        if (QTest::currentTestFailed()) return;
        m_window->clearSelections();
        QTRY_VERIFY_WITH_TIMEOUT
            (!sv::ModelTransformerFactory::getInstance()
             ->haveRunningTransformers(), 30000);
        showSeconds(0.0, 3.0);

        sv::Pane *pane = pane0();
        auto range = coverage()[0];
        QImage image = grabPaneRedrawn();
        const int y = image.height() - 3;
        const int inBand = pane->getXForFrame(range.start + frames(0.3));
        const int atEnd = pane->getXForFrame(range.end) - 1;
        const int beyond = pane->getXForFrame(range.end + frames(0.4));
        QVERIFY2(isOrange(image.pixel(inBand, y)) &&
                 isOrange(image.pixel(atEnd - 2, y)) &&
                 !isOrange(image.pixel(beyond, y)),
                 "the strip is not where the gestures are going to be made");
        saveShot("strip", image);

        struct Gesture { QString name; std::function<void()> act; };
        std::vector<Gesture> gestures {
            { "a click in the band", [&]() {
                QTest::mouseClick(pane, Qt::LeftButton, Qt::NoModifier,
                                  QPoint(inBand, y)); } },
            { "a double-click in the band", [&]() {
                QTest::mouseDClick(pane, Qt::LeftButton, Qt::NoModifier,
                                   QPoint(inBand, y)); } },
            { "a click at its end", [&]() {
                QTest::mouseClick(pane, Qt::LeftButton, Qt::NoModifier,
                                  QPoint(atEnd, y)); } },
            { "a drag along it", [&]() {
                drag(QPoint(inBand, y), QPoint(atEnd, y)); } },
            { "a drag off its end", [&]() {
                drag(QPoint(atEnd, y), QPoint(beyond, y)); } },
            { "a drag onto it", [&]() {
                drag(QPoint(beyond, y), QPoint(inBand, y)); } },
        };

        // A drag of the take's note also re-analyses the pitch under it.
        // That puts pitch candidates into the pane, which stay until the
        // next re-analysis whatever is undone: they are the analyser's
        // own layers. Everything but them, the other layers numbered as
        // if they were not there
        auto withoutCandidates = [&](const PaneState &s) {
            PaneState r;
            int n = 0;
            for (const QString &key : s.layers) {
                if (s.candidates.count(key)) continue;
                QString renumbered =
                    QString("%1 %2").arg(n++).arg(key.section(' ', 1));
                r.layers.push_back(renumbered);
                auto e = s.events.find(key);
                if (e != s.events.end()) r.events[renumbered] = e->second;
                auto x = s.extents.find(key);
                if (x != s.extents.end()) r.extents[renumbered] = x->second;
            }
            r.zoom = s.zoom;
            r.selections = s.selections;
            r.undo = s.undo;
            return r;
        };

        // ... and but what an edit of the take's note may change: the
        // note, the undo history, and the take's pitch track, into which
        // the drag may put one of the candidates
        auto apartFromNoteEdit = [&](const PaneState &s) {
            QString take = m_window->takes()->getActiveName();
            QStringList edited {
                TakeLayers::nameFor(take, TakeLayers::Notes),
                TakeLayers::nameFor(take, TakeLayers::Pitch) };
            PaneState r = withoutCandidates(s);
            for (auto i = r.events.begin(); i != r.events.end(); ) {
                if (edited.contains(i->first.section(' ', 1))) {
                    i = r.events.erase(i);
                } else {
                    ++i;
                }
            }
            r.undo = "";
            return r;
        };

        int noteEdits = 0;
        for (QString tool : { QString("navigate"), QString("edit") }) {
            press(QKeySequence(tool == "navigate" ? "1" : "2"));
            for (const Gesture &g : gestures) {
                sv::sv_frame_t centre = pane->getCentreFrame();
                const PaneState before = paneState();
                g.act();
                QTest::qWait(300); // outlast the double-click interval
                // and any re-analysis the gesture started: its candidates
                // arrive when it finishes (Analyser::layersCreated())
                QTRY_VERIFY_WITH_TIMEOUT
                    (!sv::ModelTransformerFactory::getInstance()
                     ->haveRunningTransformers(), 30000);
                // The navigate tool scrolls when dragged, as anywhere
                pane->setCentreFrame(centre);
                PaneState after = paneState();
                if (tool == "navigate") {
                    QVERIFY2(after == before,
                             qPrintable(QString("with the navigate tool, %1 "
                                                "changed %2")
                                        .arg(g.name)
                                        .arg(describe(before, after))));
                    continue;
                }
                QVERIFY2(apartFromNoteEdit(after) == apartFromNoteEdit(before),
                         qPrintable(QString("with the edit tool, %1 changed "
                                            "%2")
                                    .arg(g.name)
                                    .arg(describe(apartFromNoteEdit(before),
                                                  apartFromNoteEdit(after)))));
                if (after.undo != before.undo) {
                    ++noteEdits;
                    for (int i = 0; i < 20 && undoText() != before.undo; ++i) {
                        press(QKeySequence(tr("Ctrl+Z")));
                    }
                    PaneState undone = paneState();
                    QVERIFY2(withoutCandidates(undone) ==
                             withoutCandidates(before),
                             qPrintable(QString("undoing what %1 did with "
                                                "the edit tool left %2")
                                        .arg(g.name)
                                        .arg(describe(withoutCandidates(before),
                                                      withoutCandidates(undone)))));
                }
            }
        }
        qInfo("%d of the gestures with the edit tool edited the take's note",
              noteEdits);
        press(QKeySequence("1"));
    }

    // The Edit tool gives a take's note it has changed the pitch of the
    // take's own pitch track there, not that of the first pitch track in
    // the pane, which is the reference's: a split gives each half the
    // pitch sung in it, a drag the note where it is let go
    void edited_take_notes_keep_the_take_pitch() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(frames(0.5));
        take(1500);
        if (QTest::currentTestFailed()) return;
        m_window->clearSelections();
        QTRY_VERIFY_WITH_TIMEOUT
            (!sv::ModelTransformerFactory::getInstance()
             ->haveRunningTransformers(), 30000);
        showSeconds(0.0, 3.0);

        sv::Pane *pane = pane0();
        sv::Layer *layer = m_window->analyser2()->getLayer(Analyser::Notes);
        QVERIFY(layer);
        auto notes = [&]() {
            auto model = sv::ModelById::getAs<sv::NoteModel>(layer->getModel());
            return model ? model->getAllEvents() : sv::EventVector();
        };
        auto verifySung = [&](QString what) {
            sv::EventVector events = notes();
            QVERIFY2(!events.empty(),
                     qPrintable(what + " left the take no notes"));
            for (const sv::Event &e : events) {
                double cents = 1200.0 * std::log2(e.getValue() / highHz);
                QVERIFY2(std::fabs(cents) < 50.0,
                         qPrintable(QString("after %1 a note of the take is "
                                            "at %2 Hz, not at the %3 Hz sung "
                                            "(the reference is at %4 Hz)")
                                    .arg(what).arg(e.getValue())
                                    .arg(highHz).arg(lowHz)));
            }
        };

        const sv::EventVector original = notes();
        QCOMPARE(int(original.size()), 1);
        const sv::Event note = original[0];
        const int x = pane->getXForFrame(note.getFrame() + frames(0.3));
        const int noteY = pane->getEffectiveVerticalExtentsForLayer(layer)
            .getCoordForValueRounded(pane, note.getValue());

        press(QKeySequence("2"));

        hover(QPoint(x, noteY + 4));
        QTest::mouseClick(pane, Qt::LeftButton, Qt::NoModifier,
                          QPoint(x, noteY + 4));
        QCOMPARE(int(notes().size()), 2);
        verifySung("a split");
        if (QTest::currentTestFailed()) return;
        press(QKeySequence(tr("Ctrl+Z")));
        QVERIFY2(notes() == original, "undo did not take the split back");

        hover(QPoint(x, noteY - 4));
        drag(QPoint(x, noteY - 4), QPoint(x + 60, noteY - 4));
        // The drag re-analyses the pitch under the note
        QTRY_VERIFY_WITH_TIMEOUT
            (!sv::ModelTransformerFactory::getInstance()
             ->haveRunningTransformers(), 30000);
        QVERIFY2(notes() != original, "the drag did not move the note");
        verifySung("a drag");
        press(QKeySequence("1"));
    }

    // Checklist: the band is readable over waveform and dots at every
    // zoom: where there is singing the band is drawn over whatever else
    // is there, band high, and nowhere else
    void strip_band_at_every_zoom() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 6.0)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(frames(1.0));
        take(1500);
        if (QTest::currentTestFailed()) return;
        auto ranges = coverage();
        QCOMPARE(int(ranges.size()), 1);
        const auto range = ranges[0];

        sv::Pane *pane = pane0();
        for (int perPixel : { 16, 128, 1024, 8192 }) {
            // Centred on the end of the singing: some of the band, and
            // some of the pane with nothing recorded, at every zoom
            pane->setZoomLevel(sv::ZoomLevel(sv::ZoomLevel::FramesPerPixel,
                                             perPixel));
            pane->setCentreFrame(range.end);
            QImage image = grabPaneRedrawn();
            saveShot(QString("zoom-%1").arg(perPixel), image);
            const int h = image.height();

            // Not under the vertical scale at the left, nor where the play
            // pointer is drawn over everything
            int pointer = pointerX(image);
            int inRange = 0, outside = 0;
            for (int x = pane->getVerticalScaleWidth() + 1;
                 x < image.width(); ++x) {
                if (pointer >= 0 && std::abs(x - pointer) <= 2) continue;
                sv::sv_frame_t f0 = pane->getFrameForX(x);
                sv::sv_frame_t f1 = pane->getFrameForX(x + 1);
                if (f0 < 0) continue;
                if (f0 >= range.start && f1 <= range.end) {
                    ++inRange;
                    for (int yy = h - 6; yy < h; ++yy) {
                        QVERIFY2(isOrange(image.pixel(x, yy)),
                                 qPrintable(QString("at %1 frames per pixel "
                                                    "the band has a hole at "
                                                    "x = %2, y = %3")
                                            .arg(perPixel).arg(x).arg(yy)));
                    }
                    QVERIFY2(!isOrange(image.pixel(x, h - 9)),
                             qPrintable(QString("at %1 frames per pixel the "
                                                "band is more than a band "
                                                "high at x = %2")
                                        .arg(perPixel).arg(x)));
                } else if (f1 <= range.start || f0 >= range.end) {
                    ++outside;
                    QVERIFY2(!isOrange(image.pixel(x, h - 3)),
                             qPrintable(QString("at %1 frames per pixel the "
                                                "band is drawn at x = %2, "
                                                "where nothing was recorded")
                                        .arg(perPixel).arg(x)));
                }
            }
            QVERIFY2(inRange >= 3 && outside >= 10,
                     qPrintable(QString("at %1 frames per pixel %2 columns "
                                        "of the band and %3 without it are in "
                                        "view")
                                .arg(perPixel).arg(inRange).arg(outside)));
        }
        saveWindowShot("window");
    }

    // The band after a second recording, in a gap of the first: the take's
    // audio is swapped for a file holding both, and the take's waveform
    // layer is made again. The band has to stay on top of it, then and
    // after everything else that swaps the audio or makes the layer again:
    // undo, redo, and opening the session
    void strip_on_top_after_another_recording() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t inFirst = frames(0.4);
        const sv::sv_frame_t inSecond = frames(2.2);

        // Whether the band is drawn at a frame: a few columns either side
        // of it along the bottom row, all orange
        auto band = [&](sv::sv_frame_t frame, QString shot) {
            showSeconds(0.0, 3.0);
            QImage image = grabPaneRedrawn();
            saveShot(shot, image);
            int x = pane0()->getXForFrame(frame);
            for (int dx = -3; dx <= 3; ++dx) {
                if (!isOrange(image.pixel(x + dx, image.height() - 3))) {
                    return false;
                }
            }
            return true;
        };

        take(1000);
        if (QTest::currentTestFailed()) return;
        QVERIFY(band(inFirst, "first"));

        m_window->seekTo(frames(2.0));
        take(700);
        if (QTest::currentTestFailed()) return;
        QVERIFY2(band(inFirst, "second") && band(inSecond, "second"),
                 "after a second recording the band is not drawn");

        // Raised, and still a picture, not sound: out of the play source
        auto stripIsSilent = [this]() {
            return !m_window->playSource()->getModels().count
                (m_window->coverageStrip()->getModelId());
        };
        QVERIFY2(stripIsSilent(), "the strip's model is in the play source");

        QCOMPARE(undoText(), tr("&Undo %1").arg(tr("Record Singing")));
        press(QKeySequence(tr("Ctrl+Z")));
        QTRY_COMPARE(int(coverage().size()), 1);
        QVERIFY2(band(inFirst, "undone") && !band(inSecond, "undone"),
                 "after an undo the band is not what is left of the take");

        press(QKeySequence(tr("Ctrl+Shift+Z")));
        QTRY_COMPARE(int(coverage().size()), 2);
        QVERIFY2(band(inFirst, "redone") && band(inSecond, "redone"),
                 "after a redo the band is not drawn");

        QString session = m_dir.filePath
            (QString("session-%1.ton").arg(++m_fileCounter));
        QVERIFY(m_window->saveSessionFile(session));
        m_window->doCloseSession();
        QCOMPARE(m_window->openPath(session, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        QTRY_VERIFY(m_window->takes()->haveTake() &&
                    int(coverage().size()) == 2);
        QVERIFY2(band(inFirst, "reopened") && band(inSecond, "reopened"),
                 "in the session opened again the band is not drawn");
        QVERIFY2(stripIsSilent(), "the strip's model is in the play source");
    }

    // Checklist: Erase and Select Recording are greyed out with no take,
    // no selection, while recording and during the analysis after Stop;
    // the Takes menu and combo during a take -- and all of them come
    // back by themselves, with nothing but the take's own events to
    // update them
    void menus_follow_the_take_by_themselves() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        QAction *erase = m_window->eraseSingingAction();
        QAction *select = m_window->selectRecordingAction();
        QMenu *takes = m_window->takesMenu();
        QVERIFY(takes);
        QVERIFY(menuTitled(tr("&Edit"))->actions().contains(erase));
        QVERIFY(menuTitled(tr("&Edit"))->actions().contains(select));

        auto takeItemsEnabled = [&]() {
            int n = 0;
            for (QAction *a : takes->actions()) {
                if (!a->isSeparator() && a->isEnabled()) ++n;
            }
            return n;
        };

        // No take
        m_window->selectRange(0, frames(0.5));
        QVERIFY(!erase->isEnabled());
        QVERIFY(!select->isEnabled());
        m_window->clearSelections();

        // The merge of the take's analysis is held until the menus have
        // been looked at: pYIN may analyse the range before Stop returns
        m_window->holdRangedMerges(true);

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(200);
        QVERIFY2(!m_window->takeCombo()->isEnabled(),
                 "the take combo can be used during a take");
        QVERIFY2(takeItemsEnabled() == 0,
                 "items of the Takes menu can be used during a take");
        m_window->selectRange(0, frames(0.3));
        QVERIFY(!erase->isEnabled());
        QVERIFY(!select->isEnabled());
        QTest::qWait(600);

        // Stop, and the moment after it: the recorded range is being
        // analysed, and erasing would throw that away
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(m_window->analysingRange());
        QVERIFY2(!erase->isEnabled(),
                 "Erase can be used while the take is being analysed");

        // ... and everything back, by itself
        m_window->holdRangedMerges(false);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        QTRY_VERIFY2(erase->isEnabled(),
                     "Erase did not come back after the analysis");
        QVERIFY(select->isEnabled());
        QVERIFY(m_window->takeCombo()->isEnabled());
        QVERIFY(takeItemsEnabled() > 0);

        // No selection, nothing to erase
        m_window->clearSelections();
        QVERIFY(!erase->isEnabled());
        QVERIFY(select->isEnabled());
    }

    // Checklist: three recordings, Ctrl+Z three times, each takes back
    // exactly one; the menu says "Record Singing" / "Erase Singing" and
    // never anything about a layer or pane. With the keys, as the user
    // does it
    void ctrl_z_takes_back_one_recording_at_a_time() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 8.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 5.0)));
        if (QTest::currentTestFailed()) return;
        QCOMPARE(undoText(), tr("Nothing to undo"));

        for (double at : { 0.0, 1.5, 3.0 }) {
            m_window->seekTo(frames(at));
            take(600);
            if (QTest::currentTestFailed()) return;
        }
        QCOMPARE(int(coverage().size()), 3);

        // Every name the user can see in the Edit menu and in the undo
        // and redo menus of the toolbar
        auto verifyNames = [this]() {
            QStringList seen;
            for (QAction *a : menuTitled(tr("&Edit"))->actions()) {
                seen << a->text();
            }
            for (QToolBar *bar : m_window->findChildren<QToolBar *>()) {
                for (QAction *a : bar->actions()) {
                    if (!a->menu()) continue;
                    for (QAction *b : a->menu()->actions()) seen << b->text();
                }
            }
            for (QString text : seen) {
                QVERIFY2(!text.contains("Layer", Qt::CaseInsensitive) &&
                         !text.contains("Pane", Qt::CaseInsensitive),
                         qPrintable("the undo history shows \"" + text + "\""));
            }
        };

        for (int left : { 2, 1, 0 }) {
            QCOMPARE(undoText(), tr("&Undo %1").arg(tr("Record Singing")));
            verifyNames();
            if (QTest::currentTestFailed()) return;
            press(QKeySequence(tr("Ctrl+Z")));
            QTRY_VERIFY(m_window->takes()->haveTake() ?
                        int(coverage().size()) == left : left == 0);
        }
        QCOMPARE(undoText(), tr("Nothing to undo"));

        // ... and all three back
        for (int back : { 1, 2, 3 }) {
            press(QKeySequence(tr("Ctrl+Shift+Z")));
            QTRY_VERIFY(m_window->takes()->haveTake() &&
                        int(coverage().size()) == back);
        }
        QVERIFY(!m_window->analysingRange());
        QCOMPARE(undoText(), tr("&Undo %1").arg(tr("Record Singing")));

        // Erase the middle of the second, with its shortcut. A selection
        // has the reference's pitch analysed again in it, and that is an
        // entry of the undo history of its own; it is waited for here, so
        // that it is not what the next Ctrl+Z takes back
        auto second = coverage()[1];
        m_window->selectRange(second.start + frames(0.1),
                              second.start + frames(0.3));
        QTRY_VERIFY_WITH_TIMEOUT
            (!sv::ModelTransformerFactory::getInstance()
             ->haveRunningTransformers(), 30000);
        QTest::qWait(200);
        qInfo("after making a selection the Edit menu says \"%s\"",
              qPrintable(undoText()));
        QTRY_VERIFY(m_window->eraseSingingAction()->isEnabled());
        press(QKeySequence(tr("Ctrl+D")));
        QTRY_COMPARE(int(coverage().size()), 4);
        QCOMPARE(undoText(), tr("&Undo %1").arg(tr("Erase Singing")));
        verifyNames();
        if (QTest::currentTestFailed()) return;

        press(QKeySequence(tr("Ctrl+Z")));
        QTRY_COMPARE(int(coverage().size()), 3);
        QCOMPARE(coverage()[1], second);
    }

    // Checklist: recording again inside singing that is there asks first;
    // No records nothing; "Don't ask again" with Yes holds across
    // sessions. The dialog MainWindow shows, answered with its buttons
    void record_over_question_in_its_dialog() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 8.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        m_window->setRecordOverAskedInDialog(true);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(1500);
        if (QTest::currentTestFailed()) return;
        const Coverage::Ranges first = coverage();
        const QString firstPath = m_window->takes()->getAudioPath();

        // No: nothing recorded, and nothing left running
        QStringList asked;
        m_answerDialog = answerWith(QMessageBox::No, false, &asked);
        m_window->seekTo(frames(0.5));
        m_window->doRecord();
        QCOMPARE(asked.size(), 1);
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(!m_window->recordingInProgress());
        QTest::qWait(300);
        QCOMPARE(coverage(), first);
        QCOMPARE(m_window->takes()->getAudioPath(), firstPath);

        // Yes, and don't ask again
        m_answerDialog = answerWith(QMessageBox::Yes, true, &asked);
        m_window->seekTo(frames(0.5));
        startTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(asked.size(), 2);
        QTest::qWait(500);
        stopTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->takes()->getAudioPath() != firstPath);

        // A session of its own, in a window of its own: not asked
        m_answerDialog = answerWith(QMessageBox::Yes, false, &asked);
        m_window->doCloseSession();
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        m_window->setRecordOverAskedInDialog(true);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;
        take(1200);
        if (QTest::currentTestFailed()) return;
        m_window->seekTo(frames(0.3));
        take(400);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(asked.size(), 2);
        QVERIFY(!SingingTakes::isOverwriteConfirmationWanted());
    }

    // Checklist: pitch and notes outside the recorded range (± about
    // 0.25 s) must not flicker or move at all. The pane before and after a
    // recording into the middle of a take, compared pixel for pixel
    // outside it, and the take's layers the same objects throughout
    void nothing_outside_the_range_moves() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 8.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 5.0)));
        if (QTest::currentTestFailed()) return;

        take(3500);
        if (QTest::currentTestFailed()) return;
        showSeconds(0.0, 3.8);
        const sv::sv_frame_t P = frames(1.4);
        m_window->seekTo(P);
        QImage before = grabPaneRedrawn();
        saveShot("before", before);
        sv::Layer *pitch = m_window->analyser2()->getLayer(Analyser::PitchTrack);
        sv::Layer *notes = m_window->analyser2()->getLayer(Analyser::Notes);

        QSignalSpy relayered(m_window->analyser2(), SIGNAL(layersChanged()));
        m_window->setRecordOverAnswer(true);
        take(700);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::Notes), notes);
        QVERIFY2(relayered.isEmpty(),
                 "the take's layers were replaced: they would have vanished "
                 "from the pane for a moment");

        m_window->seekTo(P);
        showSeconds(0.0, 3.8);
        QImage after = grabPaneRedrawn();
        saveShot("after", after);

        // What was recorded is at most as long as the wait for it; the
        // analysis of it reaches a quarter of a second either side
        sv::Pane *pane = pane0();
        int left = pane->getXForFrame(P - frames(0.3));
        int right = pane->getXForFrame(P + frames(0.8) + frames(0.3));
        int pointer = pane->getXForFrame(P);
        QVERIFY(left > 20 && right < before.width() - 20);

        int differing = 0;
        QString first;
        for (int x = 0; x < before.width(); ++x) {
            if (x >= left && x <= right) continue;
            if (std::abs(x - pointer) <= 3) continue;
            for (int y = 0; y < before.height(); ++y) {
                if (before.pixel(x, y) != after.pixel(x, y)) {
                    if (first == "") {
                        first = QString("x = %1, y = %2").arg(x).arg(y);
                    }
                    ++differing;
                }
            }
        }
        QVERIFY2(differing == 0,
                 qPrintable(QString("%1 pixels outside the recorded range "
                                    "and its margin changed, the first at %2 "
                                    "(range drawn from x = %3 to %4)")
                            .arg(differing).arg(first).arg(left).arg(right)));
    }

    // Checklist: open a .ton, Load Singing Track, then Record: closing
    // afterwards asks whether to save. The window's own close, as the
    // title bar's button does it
    void closing_after_a_take_asks_to_save() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;
        QString session = m_dir.filePath
            (QString("session-%1.ton").arg(++m_fileCounter));
        QVERIFY(m_window->saveSessionFile(session));
        openReference(session);
        if (QTest::currentTestFailed()) return;

        m_window->loadSingingTrack(writeWav(tone(highHz, 1.0)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        m_window->seekTo(frames(1.2));
        take(500);
        if (QTest::currentTestFailed()) return;

        QStringList asked;
        m_answerDialog = answerWith(QMessageBox::Cancel, false, &asked);
        QVERIFY(!m_window->close());
        QCOMPARE(asked.size(), 1);
        QVERIFY2(asked[0].startsWith(tr("The current session has been modified.")),
                 qPrintable(asked[0]));
        QVERIFY2(m_window->isVisible(), "Cancel did not keep the window open");
        QVERIFY(m_window->takes()->haveTake());
    }

    // Checklist: stop a take and close the window at once: no crash.
    // Closed and deleted while the take is still being analysed
    void stop_then_close_the_window_at_once() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 3.0)));
        if (QTest::currentTestFailed()) return;

        // Held, so that the merge is still to come when the window goes,
        // however quickly pYIN analyses the range
        m_window->holdRangedMerges(true);

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(1000);
        m_window->doRecord();
        QVERIFY(m_window->analysingRange());

        QStringList asked;
        m_answerDialog = answerWith(QMessageBox::No, false, &asked);
        QVERIFY(m_window->close());
        QCOMPARE(asked.size(), 1);
        QVERIFY2(asked[0].startsWith(tr("The current session has been modified.")),
                 qPrintable(asked[0]));
        delete m_window;
        m_window = nullptr;

        // Whatever was still running finishes with no window to go to
        QTRY_VERIFY_WITH_TIMEOUT
            (!sv::ModelTransformerFactory::getInstance()
             ->haveRunningTransformers(), 30000);
        QTest::qWait(500);
    }

    // Checklist: log out with unsaved takes: what commitData writes into
    // ~/.sv1 is playable. Only where the home directory can be moved
    // for the test: on Windows it is the profile's, whatever HOME says
    void commit_data_writes_a_playable_session() {
#ifdef Q_OS_WIN
        QSKIP("commitData() writes into the real profile's .sv1 on Windows");
#else
        QByteArray home = qgetenv("HOME");
        QString fakeHome = m_dir.filePath("home");
        QVERIFY(QDir().mkpath(fakeHome));
        qputenv("HOME", fakeHome.toLocal8Bit());
        QCOMPARE(QDir::homePath(), fakeHome);

        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 3.0)));
        if (QTest::currentTestFailed()) return;
        take(1000);
        if (QTest::currentTestFailed()) return;
        const Coverage::Ranges recorded = coverage();

        bool committed = m_window->commitData(false);
        qputenv("HOME", home);
        QVERIFY(committed);

        QStringList written = QDir(fakeHome + "/.sv1")
            .entryList({ "tmp-*" }, QDir::Files);
        QCOMPARE(written.size(), 1);
        QString path = fakeHome + "/.sv1/" + written[0];
        QVERIFY2(path.endsWith(".ton"),
                 qPrintable("the session is written as " + path));

        // It goes on the Recent Files list, which opens it with openPath()
        m_window->doCloseSession();
        QCOMPARE(m_window->openPath(path, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        QVERIFY2(m_window->takes()->haveTake(),
                 "the session written at logout has no take in it");
        QCOMPARE(coverage(), recorded);

        QString audio = m_window->takes()->getAudioPath();
        QVERIFY2(QFileInfo(audio).exists(),
                 qPrintable("the take's audio is not there: " + audio));
        sv::WavFileReader reader { sv::FileSource(audio) };
        QVERIFY(reader.isOK());
        auto data = reader.getInterleavedFrames
            (recorded[0].start + 2000, recorded[0].end - recorded[0].start - 4000);
        double sum = 0.0;
        for (float v : data) sum += double(v) * double(v);
        QVERIFY2(!data.empty() && std::sqrt(sum / double(data.size())) > 0.01,
                 "the take in the session written at logout is silent");
#endif
    }

    // Checklist: the alternate pitch track is faded brown, dark brown
    // during a take, and stays in view after an octave step
    void alternate_pitch_track_colours() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 3.0)));
        if (QTest::currentTestFailed()) return;
        showSeconds(0.0, 3.0);

        QColor faded(164, 146, 136), dark(74, 37, 17);
        auto isFaded = [&](QRgb p) { return closeTo(p, faded, 12); };
        auto isDark = [&](QRgb p) { return closeTo(p, dark, 12); };

        m_window->doToggleAlternatePitch();
        QTRY_VERIFY(m_window->alternatePitch()->getLayer());
        QTest::qWait(200);
        QImage image = grabPane();
        QVERIFY2(countAbove(image, 0, image.width(), isFaded) > 0,
                 "no faded brown track is drawn");
        saveShot("faded", image);

        // Wide enough for the whole of every toolbar, 8vb and 8va included
        QSize size = m_window->size();
        m_window->resize(1920, 1000);
        saveWindowShot("window");
        m_window->resize(size);
        showSeconds(0.0, 3.0);

        // Two octaves either way stay in view of a 220 Hz reference. The
        // third is only reported: 28 Hz and 1.8 kHz are past the range the
        // pane shows, and nothing scrolls to them
        auto inView = [&]() {
            QTest::qWait(300);
            image = grabPane();
            int octaves = m_window->alternatePitch()->getOctaves();
            saveShot(QString("octaves%1").arg(octaves), image);
            return countAbove(image, 0, image.width(), isFaded) > 0;
        };
        QCOMPARE(m_window->alternatePitch()->getOctaves(), -1);
        for (bool up : { false, true, true, true }) {
            m_window->doStepAlternatePitch(up);
            QVERIFY2(inView(),
                     qPrintable(QString("the track is out of view at %1 "
                                        "octaves")
                                .arg(m_window->alternatePitch()->getOctaves())));
        }
        QCOMPARE(m_window->alternatePitch()->getOctaves(), 2);
        m_window->doStepAlternatePitch(true);
        qInfo("at +3 octaves the track is %s", inView() ? "in view" : "out of view");
        for (int i = 0; i < 5; ++i) m_window->doStepAlternatePitch(false);
        QCOMPARE(m_window->alternatePitch()->getOctaves(), -3);
        qInfo("at -3 octaves the track is %s", inView() ? "in view" : "out of view");
        m_window->doStepAlternatePitch(true);
        m_window->doStepAlternatePitch(true);
        QCOMPARE(m_window->alternatePitch()->getOctaves(), -1);

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(400);
        image = grabPane();
        QVERIFY2(countAbove(image, 0, image.width(), isDark) > 0,
                 "the track followed during a take is not dark brown");
        QVERIFY2(countAbove(image, 0, image.width(), isFaded) == 0,
                 "the track is still faded during a take");
        saveShot("dark", image);
        stopTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(countAbove(grabPane(), 0, image.width(), isFaded) > 0);
    }

    // Checklist: pre-roll less than 3 s from the start of the song: a
    // shorter countdown, and no attempt to run from before frame 0
    void preroll_near_the_start_of_the_song() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        m_window->setPlayReferenceWhileRecording(true);
        m_window->setPreRoll(true);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(frames(1.0));
        startTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->takePreRoll(), frames(1.0));
        QStringList counted;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1500) {
            QVERIFY2(m_window->playbackFrame() >= 0,
                     qPrintable(QString("the cursor is at frame %1")
                                .arg(m_window->playbackFrame())));
            QString text = m_window->statusText();
            if (text.startsWith(tr("Recording in ")) &&
                !counted.contains(text)) {
                counted << text;
            }
            QTest::qWait(20);
        }
        QCOMPARE(counted, QStringList({ tr("Recording in %1…").arg(1) }));
        stopTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(coverage()[0].start, frames(1.0));
    }

    // Checklist: Constrain Playback to Selection together with a pre-roll.
    // The device hears its own output here, so the "singer" sings exactly
    // what is played: a take that is in time holds the reference's high
    // note where the reference has it
    void preroll_with_playback_constrained_to_the_selection() {
        for (bool constrained : { false, true }) {
            FakeAudioIO::Config config;
            config.loopback = true;
            config.recordLatency = 512;
            config.playbackLatency = 512;
            config.inputDelay = 1024;
            makeWindow(config);
            if (QTest::currentTestFailed()) return;
            m_window->setPlayReferenceWhileRecording(true);
            m_window->setPreRoll(true);
            m_window->setRecordIntoSelection(true);
            QSettings settings;
            settings.beginGroup("MainWindow");
            settings.setValue("prerollseconds", 1.0);
            settings.endGroup();

            // Low, then high from 2 to 2.75 s, then low again
            auto reference = tone(lowHz, 2.0);
            auto high = tone(highHz, 0.75);
            auto low = tone(lowHz, 1.25);
            reference.insert(reference.end(), high.begin(), high.end());
            reference.insert(reference.end(), low.begin(), low.end());
            openReference(writeWav(reference));
            if (QTest::currentTestFailed()) return;

            m_window->selectRange(frames(2.0), frames(3.5));
            QAction *constrain = nullptr;
            for (QAction *a : m_window->findChildren<QAction *>()) {
                if (a->text() == tr("Constrain Playback to Selection")) {
                    constrain = a;
                }
            }
            QVERIFY(constrain);
            if (constrain->isChecked() != constrained) constrain->trigger();
            QCOMPARE(constrain->isChecked(), constrained);
            QTRY_VERIFY_WITH_TIMEOUT
                (!sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(), 30000);

            startTake();
            if (QTest::currentTestFailed()) return;

            // Lifted for the take, and not to be put back during it
            QTest::qWait(300);
            QVERIFY2(!constrain->isChecked(),
                     "playback is constrained to the selection during a take");
            QVERIFY2(!constrain->isEnabled(),
                     "playback can be constrained to the selection during a "
                     "take");

            QTRY_VERIFY_WITH_TIMEOUT
                (!m_window->recordTarget()->isRecording(), 8000);
            QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

            // ... and as it was afterwards
            QCOMPARE(constrain->isChecked(), constrained);
            QVERIFY(constrain->isEnabled());

            double from = -1.0, to = -1.0;
            for (const auto &e : sv::ModelById::getAs<sv::SparseTimeValueModel>
                     (m_window->analyser2()->getLayer(Analyser::PitchTrack)
                      ->getModel())->getAllEvents()) {
                if (e.getValue() < std::sqrt(lowHz * highHz)) continue;
                if (from < 0.0) from = double(e.getFrame()) / rate;
                to = double(e.getFrame()) / rate;
            }
            qInfo("playback %s: the high note is in the take from %.3f to "
                  "%.3f s", constrained ? "constrained" : "not constrained",
                  from, to);
            QVERIFY2(std::fabs(from - 2.0) < 0.05 && std::fabs(to - 2.75) < 0.05,
                     qPrintable(QString("the high note of the reference, "
                                        "2.000 to 2.750 s, is in the take "
                                        "from %1 to %2 s")
                                .arg(from, 0, 'f', 3).arg(to, 0, 'f', 3)));

            if (constrain->isChecked()) constrain->trigger();
            m_window->clearSelections();
            QTRY_VERIFY_WITH_TIMEOUT
                (!sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(), 30000);
            m_window->doCloseSession();
        }
    }

    // Checklist: switching between takes is fast and analyses nothing
    void switching_takes_is_quick() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        if (QTest::currentTestFailed()) return;
        openReference(writeWav(tone(lowHz, 30.0)));
        if (QTest::currentTestFailed()) return;

        take(800);
        if (QTest::currentTestFailed()) return;
        m_window->doNewEmptyTake();
        m_window->seekTo(frames(2.0));
        take(800);
        if (QTest::currentTestFailed()) return;

        for (int to : { 0, 1, 0, 1 }) {
            QElapsedTimer timer;
            timer.start();
            m_window->doChooseTakeInCombo(to);
            QCoreApplication::processEvents();
            grabPane();
            qint64 ms = timer.elapsed();
            qInfo("switch to take %d took %lld ms", to + 1, ms);
            QVERIFY2(ms < 1000,
                     qPrintable(QString("switching takes took %1 ms").arg(ms)));
            QCOMPARE(m_window->takes()->getActiveIndex(), to);
            QVERIFY(!m_window->analysingRange());
            QVERIFY(!sv::ModelTransformerFactory::getInstance()
                    ->haveRunningTransformers());
        }
    }
};

#endif
