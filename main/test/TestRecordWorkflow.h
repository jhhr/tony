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

#ifndef TEST_RECORD_WORKFLOW_H
#define TEST_RECORD_WORKFLOW_H

// Tier 5: the real MainWindow, recording from a fake audio device
// (FakeAudioIO.h) that runs in real time. Takes are about a second
// each, so this suite is the slow one.
//
// A take that works end to end shows no dialog, and a dialog would
// block the test for ever. A watchdog timer dismisses any modal
// dialog and records it; each test then fails in cleanup().

#include "TestSignals.h"
#include "TestMainWindow.h"

#include "../MainWindow.h"
#include "../Analyser.h"
#include "../CoverageStrip.h"
#include "../Lyrics.h"
#include "../LyricsEditor.h"
#include "../LyricsTrack.h"
#include "../LyricsTtml.h"
#include "../SingingTakes.h"
#include "../TakeLayers.h"
#include "../TakesFile.h"

#include "version.h"

#include "framework/Document.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/ViewManager.h"
#include "layer/Layer.h"
#include "layer/ColourDatabase.h"
#include "layer/SingleColourLayer.h"
#include "layer/TimeValueLayer.h"
#include "layer/FlexiNoteLayer.h"
#include "layer/RegionLayer.h"
#include "layer/WaveformLayer.h"
#include "audio/AudioCallbackPlaySource.h"
#include "audio/AudioCallbackRecordTarget.h"
#include "data/model/WritableWaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"
#include "data/model/RegionModel.h"
#include "data/fileio/BZipFileDevice.h"
#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"
#include "data/fileio/WavFileWriter.h"
#include "base/Command.h"
#include "base/PlayParameters.h"
#include "base/RecordDirectory.h"
#include "transform/ModelTransformerFactory.h"
#include "widgets/CommandHistory.h"
#include "widgets/InteractiveFileFinder.h"

#include <QObject>
#include <QtTest>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

class TestRecordWorkflow : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;
    static constexpr int hop = 256; // as set in Analyser::addAnalyses()

    // Whole numbers of samples per period: see TestSingingAnalysis.h
    static constexpr double lowHz = 220.5;
    static constexpr double highHz = 294.0;

    // A third tone for the audio a swap puts in, no harmonic of either of
    // the other two, so that what is in the output says which file it is
    static constexpr double swapHz = 490.0;

    QTemporaryDir m_dir;
    int m_fileCounter = 0;
    TestMainWindow *m_window = nullptr;
    QTimer m_watchdog;
    QStringList m_dialogs;

    static std::vector<float> tone(double hz, double seconds) {
        return TestSignals::sawtooth(hz, rate, int(seconds * rate), 0.5f);
    }

    // A low note then a high one. The step between them is the event
    // whose position the latency test compares across the two tracks
    static std::vector<float> melody(double secondsPerNote) {
        auto m = tone(lowHz, secondsPerNote);
        auto high = tone(highHz, secondsPerNote);
        m.insert(m.end(), high.begin(), high.end());
        return m;
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

    void makeWindow(FakeAudioIO::Config config, bool installDevice = true) {
        delete m_window;
        m_window = new TestMainWindow(config, installDevice);
    }

    // Complete, and with the transform threads gone as well. A model
    // released while a transform thread still holds it is destroyed
    // on that thread, which is review finding 15; no test but the one
    // about that finding should depend on it
    static bool analysed(Analyser *a) {
        return a && a->getLayer(Analyser::PitchTrack) &&
            a->getLayer(Analyser::Notes) &&
            a->getInitialAnalysisCompletion() >= 100 &&
            !a->isAnalysingRange() &&
            !sv::ModelTransformerFactory::getInstance()
            ->haveRunningTransformers();
    }

    // Open path as the reference, replacing whatever is loaded, and
    // wait for pYIN
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

    // Stop, then wait for pYIN on the take
    void stopTake() {
        QVERIFY(m_window->recordTarget()->isRecording());
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
    }

    // For a test that stops a take as soon as it has looked at something:
    // stopped at once, under load the take can have nothing in it, and
    // then there is no take for stopTake() to wait for
    void waitForSomethingRecorded() {
        QTRY_VERIFY_WITH_TIMEOUT
            (m_window->recordTarget()->getRecordDuration() > rate / 2, 5000);
    }

    void take(int ms) {
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(ms);
        stopTake();
    }

    // A round trip as the audio check would have kept it for the fake
    // device (the default devices, the Preferences naming none) at 44.1
    // kHz, measured while the device reported the given latencies, in
    // frames. cleanup() forgets it
    static void storeRoundTrip(int roundTrip, int reportedOutput,
                               int reportedInput) {
        LatencyCalibration::Figure figure;
        figure.roundTrip = roundTrip / rate;
        figure.date = QDateTime::currentDateTimeUtc();
        figure.reportedOutput = reportedOutput / rate;
        figure.reportedInput = reportedInput / rate;
        QSettings settings;
        LatencyCalibration::store
            (settings, LatencyCalibration::currentKey(settings, rate), figure);
    }

    static sv::EventVector pitchEvents(sv::Layer *layer) {
        if (!layer) return {};
        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
            (layer->getModel());
        if (!model) return {};
        return model->getAllEvents();
    }

    static sv::EventVector pitchEvents(Analyser *a) {
        return a ? pitchEvents(a->getLayer(Analyser::PitchTrack))
            : sv::EventVector();
    }

    static sv::EventVector noteEvents(sv::Layer *layer) {
        if (!layer) return {};
        auto model = sv::ModelById::getAs<sv::NoteModel>(layer->getModel());
        if (!model) return {};
        return model->getAllEvents();
    }

    static sv::EventVector eventsBetween(const sv::EventVector &events,
                                         sv::sv_frame_t from,
                                         sv::sv_frame_t to) {
        sv::EventVector result;
        for (const auto &e : events) {
            if (e.getFrame() >= from && e.getFrame() < to) {
                result.push_back(e);
            }
        }
        return result;
    }

    // The audio of the take: one file, starting at frame 0 of the
    // reference's timeline, silent where nothing has been recorded
    std::shared_ptr<sv::WaveFileModel> takeAudio() {
        Analyser *a2 = m_window->analyser2();
        if (!a2) return nullptr;
        return sv::ModelById::getAs<sv::WaveFileModel>(a2->getMainModelId());
    }

    // What is in the take's audio file itself over [from, to), rather
    // than what a model makes of it
    double takeAudioRms(sv::sv_frame_t from, sv::sv_frame_t to) {
        QString path = m_window->takes()->getAudioPath();
        if (path.isEmpty() || to <= from) return -1.0;
        sv::WavFileReader reader { sv::FileSource(path) };
        if (!reader.isOK()) return -1.0;
        auto data = reader.getInterleavedFrames(from, to - from);
        if (data.empty()) return -1.0;
        double sum = 0.0;
        for (float v : data) sum += double(v) * double(v);
        return std::sqrt(sum / double(data.size()));
    }

    static double medianHz(const sv::EventVector &events) {
        std::vector<double> values;
        for (const auto &e : events) values.push_back(e.getValue());
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }

    // Frame of the first event above the midpoint of the two notes,
    // or -1
    static sv::sv_frame_t stepFrame(const sv::EventVector &events) {
        double mid = std::sqrt(lowHz * highHz);
        for (const auto &e : events) {
            if (e.getValue() > mid) return e.getFrame();
        }
        return -1;
    }

    static int colourOf(sv::Layer *layer) {
        auto scl = qobject_cast<sv::SingleColourLayer *>(layer);
        return scl ? scl->getBaseColour() : -1;
    }

    static int colourNamed(QString name) {
        return sv::ColourDatabase::getInstance()->getColourIndex(name);
    }

    // Amplitude of the hz component of data[from, from+n)
    static double amplitudeAt(const std::vector<float> &data,
                              size_t from, size_t n, double hz) {
        if (from + n > data.size()) return -1.0;
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double phase = 2.0 * TestSignals::kPi * hz * double(i) / rate;
            re += data[from + i] * std::cos(phase);
            im += data[from + i] * std::sin(phase);
        }
        return 2.0 * std::sqrt(re * re + im * im) / double(n);
    }

    // Every model the play source holds is alive and in use by a layer
    // (review finding 6)
    void verifyPlaySourceClean() {
        for (sv::ModelId id : m_window->playSource()->getModels()) {
            QVERIFY2(sv::ModelById::get(id),
                     qPrintable(QString("the play source holds model %1, "
                                        "which no longer exists")
                                .arg(id.untyped)));
            QVERIFY2(layersOnModel(id) > 0,
                     qPrintable(QString("the play source holds model %1, "
                                        "which no layer uses")
                                .arg(id.untyped)));
        }
    }

    int layersOnModel(sv::ModelId id) {
        int n = 0;
        for (sv::Layer *layer : m_window->document()->getLayers()) {
            if (layer->getModel() == id) ++n;
        }
        return n;
    }

    // One for the reference and one for the take: a second pair for the
    // take means its layers were analysed again instead of being claimed
    int noteLayersInPane0() {
        int n = 0;
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        if (!pane) return 0;
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            if (qobject_cast<sv::FlexiNoteLayer *>(pane->getLayer(i))) ++n;
        }
        return n;
    }

    bool paneHasLayer(int paneIndex, sv::Layer *layer) {
        sv::Pane *pane = m_window->paneStack()->getPane(paneIndex);
        if (!pane) return false;
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            if (pane->getLayer(i) == layer) return true;
        }
        return false;
    }

    bool documentHasLayer(sv::Layer *layer) {
        for (sv::Layer *l : m_window->document()->getLayers()) {
            if (l == layer) return true;
        }
        return false;
    }

    // Save a session holding only the reference, and open it again.
    // Only a session load sets MainWindowBase::m_timeRulerLayer, which
    // is what puts the shared ruler into the extra panes
    void reopenAsSession() {
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QString session = m_dir.filePath
            (QString("session-%1.ton").arg(++m_fileCounter));
        QVERIFY(m_window->saveSessionFile(session));
        m_window->doCloseSession();
        openReference(session);
        if (QTest::currentTestFailed()) return;
        QVERIFY2(m_window->timeRuler(),
                 "the session load did not find the time ruler");
        QVERIFY(paneHasLayer(1, m_window->timeRuler()));
    }

    // Open a session that has just been saved, with the reference analysed
    // (claimed, in fact: a restored session has its layers) and the takes
    // of the session read from the file
    void reopenSession(QString path) {
        m_window->doCloseSession();
        m_window->discardModifications();
        QCOMPARE(m_window->openPath(path, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
    }

    // Take the <takes> element out of a saved session file, making it what
    // a .ton written before phase 7b is: the same document with no takes in
    // it.  The file is bzip2, as the session reader and writer leave it
    // "" on success, else what went wrong
    QString removeTakesElement(QString path) {
        QByteArray document;
        {
            sv::BZipFileDevice file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                return "could not read " + path + ": " + file.errorString();
            }
            document = file.readAll();
            file.close();
        }
        if (document.isEmpty()) return "read nothing from " + path;

        int from = document.indexOf("<takes");
        int to = document.indexOf("</takes>");
        if (from < 0 || to < from) return "no takes element in " + path;
        document.remove(from, to + int(strlen("</takes>")) - from);

        sv::BZipFileDevice out(path);
        if (!out.open(QIODevice::WriteOnly)) {
            return "could not write " + path + ": " + out.errorString();
        }
        qint64 written = out.write(document);
        out.close();
        if (written != document.size()) {
            return QString("wrote %1 of %2 bytes to ").arg(written)
                .arg(document.size()) + path;
        }
        return "";
    }

    // A file's bytes, as they are on disk
    static QByteArray fileContents(QString path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    // Put text into a saved session file before the first occurrence of
    // another, as removeTakesElement() takes some out.  "" on success,
    // else what went wrong
    QString insertIntoSession(QString path, QByteArray before,
                              QByteArray text) {
        QByteArray document;
        {
            sv::BZipFileDevice file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                return "could not read " + path + ": " + file.errorString();
            }
            document = file.readAll();
            file.close();
        }
        int at = document.indexOf(before);
        if (at < 0) return "no " + QString(before) + " in " + path;
        document.insert(at, text);

        sv::BZipFileDevice out(path);
        if (!out.open(QIODevice::WriteOnly)) {
            return "could not write " + path + ": " + out.errorString();
        }
        qint64 written = out.write(document);
        out.close();
        if (written != document.size()) {
            return QString("wrote %1 of %2 bytes to ").arg(written)
                .arg(document.size()) + path;
        }
        return "";
    }

    // The dialogs the watchdog has dismissed so far, taken off the list so
    // that cleanup() does not fail the test with them: for a test that
    // expects one
    QStringList takeDialogs() {
        QStringList dialogs = m_dialogs;
        m_dialogs.clear();
        return dialogs;
    }

    // The dialogs seen so far whose text holds this, taken off the list
    // along with the ones before them: for a test that expects one of its
    // own among the dialogs another part of the application showed first
    QStringList dialogsMatching(QString text) {
        QStringList matching;
        for (const QString &dialog : takeDialogs()) {
            if (dialog.contains(text)) matching.push_back(dialog);
        }
        return matching;
    }

    // As dialogsMatching(), for message boxes with this title as well.
    // Not on macOS, which shows no title on a message box, and Qt keeps
    // none there: the text alone must tell the box apart
    QStringList messagesMatching(QString title, QString text) {
        QStringList matching;
        for (const QString &dialog : dialogsMatching(text)) {
#ifdef Q_OS_MACOS
            Q_UNUSED(title);
            matching.push_back(dialog);
#else
            if (dialog.startsWith(title + ": ")) matching.push_back(dialog);
#endif
        }
        return matching;
    }

    // Audio in the session besides the reference: one model per take that
    // is on show, and nothing left over from a session load
    int audioModelsBesidesReference() {
        int n = 0;
        for (sv::ModelId id : m_window->document()->getModels()) {
            if (id == m_window->mainModelId()) continue;
            if (sv::ModelById::isa<sv::WaveFileModel>(id)) ++n;
        }
        return n;
    }

    int waveformLayersInPane0() {
        int n = 0;
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        if (!pane) return 0;
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            if (qobject_cast<sv::WaveformLayer *>(pane->getLayer(i))) ++n;
        }
        return n;
    }

    // The same events after a session round trip. Frames, durations and
    // labels come back exactly; a value goes through QString::arg(float)
    // in Event::toXml(), which keeps six significant figures
    void verifyEventsSurvived(const sv::EventVector &before,
                              const sv::EventVector &after,
                              const char *what) {
        QVERIFY2(before.size() == after.size(),
                 qPrintable(QString("%1: %2 events before, %3 after")
                            .arg(what).arg(before.size()).arg(after.size())));
        for (size_t i = 0; i < before.size(); ++i) {
            QVERIFY2(before[i].getFrame() == after[i].getFrame(),
                     qPrintable(QString("%1: event %2 was at frame %3, is at %4")
                                .arg(what).arg(i)
                                .arg(before[i].getFrame())
                                .arg(after[i].getFrame())));
            QCOMPARE(after[i].getDuration(), before[i].getDuration());
            double value = before[i].getValue();
            QVERIFY2(std::fabs(after[i].getValue() - value) <=
                     1e-5 * std::fabs(value) + 1e-6,
                     qPrintable(QString("%1: event %2 had the value %3, has %4")
                                .arg(what).arg(i).arg(value)
                                .arg(after[i].getValue())));
        }
    }

    void verifyRulerIntact() {
        QVERIFY(m_window->timeRuler());
        QVERIFY2(documentHasLayer(m_window->timeRuler()),
                 "the shared time ruler was deleted from the document");
        QVERIFY2(paneHasLayer(1, m_window->timeRuler()),
                 "the shared time ruler is no longer in the ruler pane");
    }

    // The coverage strip: the layer in pane 0 whose regions are the
    // ranges of the take that hold recorded singing

    sv::RegionLayer *stripLayer() {
        return m_window->coverageStrip()->getLayer();
    }

    // The strips of the active take, and of every take: each take of the
    // session has one, and only the active take's is on show
    int stripLayersInPane0() {
        return stripLayersNamed
            (CoverageStrip::layerName(m_window->takes()->getActiveName()));
    }

    int allStripLayersInPane0() {
        return stripLayersNamed("");
    }

    int stripLayersNamed(QString name) {
        int n = 0;
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        if (!pane) return 0;
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            QString takeName;
            TakeLayers::Kind kind;
            if (!TakeLayers::parse(pane->getLayer(i)->objectName(),
                                   takeName, kind)) continue;
            if (kind != TakeLayers::Coverage) continue;
            if (name != "" && pane->getLayer(i)->objectName() != name) continue;
            ++n;
        }
        return n;
    }

    // The layers of the take of this name, found the way MainWindow and
    // (from 7b) a session restore find them: by their object names
    TakeLayers::Found takeLayers(QString name) {
        return TakeLayers::find(m_window->paneStack()->getPane(0), name);
    }

    // What a take that is not the active one has to be: hidden, silent and
    // out of the play source, so that it is neither seen nor heard and
    // cannot hold playback open past the end of the active take's audio
    void verifyTakeIsPutAway(QString name) {
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        QVERIFY(pane);
        TakeLayers::Found found = takeLayers(name);
        auto models = m_window->playSource()->getModels();
        for (sv::Layer *layer : { static_cast<sv::Layer *>(found.pitch),
                                  static_cast<sv::Layer *>(found.notes),
                                  static_cast<sv::Layer *>(found.coverage) }) {
            if (!layer) continue;
            QVERIFY2(layer->isLayerDormant(pane),
                     qPrintable(QString("%1 is not hidden")
                                .arg(layer->objectName())));
            auto params = layer->getPlayParameters();
            QVERIFY2(!params || !params->isPlayAudible(),
                     qPrintable(QString("%1 can be heard")
                                .arg(layer->objectName())));
            QVERIFY2(!models.count(layer->getModel()),
                     qPrintable(QString("%1 is still in the play source")
                                .arg(layer->objectName())));
            auto model = sv::ModelById::get(layer->getModel());
            QVERIFY2(model && model->getSourceModel().isNone(),
                     qPrintable(QString("%1 still has a source model, so an "
                                        "analyser could claim it")
                                .arg(layer->objectName())));
        }
    }

    // What the strip shows, from the regions of its model rather than
    // from the object that keeps it
    sv::EventVector stripEvents() {
        sv::RegionLayer *layer = stripLayer();
        if (!layer) return {};
        auto model = sv::ModelById::getAs<sv::RegionModel>(layer->getModel());
        if (!model) return {};
        return model->getAllEvents();
    }

    // The topmost note layer of pane 0: what Pane::getTopFlexiNoteLayer()
    // finds, and what NoteEditMode -- the only editing mode Tony sets for
    // that pane -- acts on
    sv::Layer *topNoteLayerInPane0() {
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        if (!pane) return nullptr;
        for (int i = pane->getLayerCount() - 1; i >= 0; --i) {
            if (qobject_cast<sv::FlexiNoteLayer *>(pane->getLayer(i))) {
                return pane->getLayer(i);
            }
        }
        return nullptr;
    }

    // One strip, its regions the take's coverage, and out of reach of the
    // editing tools: not the pane's selected layer, and the layer the
    // note tool acts on is still the take's notes
    void verifyStripMatchesTake() {
        QCOMPARE(stripLayersInPane0(), 1);
        QVERIFY(stripLayer());
        QCOMPARE(stripEvents(),
                 m_window->takes()->getCoverage().toEvents());

        sv::Pane *pane = m_window->paneStack()->getPane(0);
        QVERIFY(pane && pane->getLayerCount() > 0);
        QVERIFY2(pane->getSelectedLayer() != stripLayer(),
                 "the coverage strip is the pane's selected layer");
        QVERIFY(m_window->analyser2());
        QCOMPARE(topNoteLayerInPane0(),
                 m_window->analyser2()->getLayer(Analyser::Notes));
    }

    // The take has its sound: there is audio under it, and the file the
    // take names holds the singing
    void verifyTakeHasSound() {
        QVERIFY(m_window->takes()->haveTake());
        QVERIFY(takeAudio());
        Coverage::Ranges ranges = m_window->takes()->getCoverage().getRanges();
        QVERIFY(!ranges.empty());
        QVERIFY2(takeAudioRms(ranges[0].start + 1000, ranges[0].end - 1000) >
                 0.01, "the file the take names holds no sound");
    }

    // Undo and redo, and what they say they did.  CommandHistory has no
    // accessor for the top of its stack, and the name of the command it
    // unexecutes is the same thing; "" means there was nothing to undo
    QString undoOnce() {
        QString name;
        auto *history = sv::CommandHistory::getInstance();
        auto conn = connect(history, &sv::CommandHistory::commandUnexecuted,
                            this, [&name](sv::Command *c) {
                                if (c) name = c->getName();
                            });
        history->undo();
        disconnect(conn);
        return name;
    }

    QString redoOnce() {
        QString name;
        auto *history = sv::CommandHistory::getInstance();
        auto conn = connect(history,
                            qOverload<sv::Command *>
                            (&sv::CommandHistory::commandExecuted),
                            this, [&name](sv::Command *c) {
                                if (c) name = c->getName();
                            });
        history->redo();
        disconnect(conn);
        return name;
    }

    // Everything about the singing of a take that an undo or a redo has
    // to restore exactly
    struct TakeSnapshot {
        QString path;
        Coverage::Ranges coverage;
        sv::EventVector strip;
        sv::EventVector pitch;
        sv::EventVector notes;
        sv::sv_frame_t frames = -1;
    };

    TakeSnapshot snapshotTake() {
        TakeSnapshot s;
        s.path = m_window->takes()->getAudioPath();
        s.coverage = m_window->takes()->getCoverage().getRanges();
        s.strip = stripEvents();
        Analyser *a2 = m_window->analyser2();
        s.pitch = pitchEvents(a2);
        s.notes = a2 ? noteEvents(a2->getLayer(Analyser::Notes))
            : sv::EventVector();
        // Waited for, as verifyTakeMatches() waits: the model of a file
        // just opened (as after an erase) says 0 frames until it has read
        // the file, and under load that can outlast the call
        if (auto audio = takeAudio()) {
            QElapsedTimer waited;
            waited.start();
            while (!audio->isReady() && waited.elapsed() < 30000) {
                QTest::qWait(10);
            }
            s.frames = audio->getFrameCount();
        }
        return s;
    }

    // The same take again, down to every pitch event and note.  A model
    // of a file that has just been opened takes a moment to say how long
    // it is, so the frame count is the one thing worth waiting for
    void verifyTakeMatches(const TakeSnapshot &s) {
        QCOMPARE(m_window->takes()->getAudioPath(), s.path);
        QCOMPARE(m_window->takes()->getCoverage().getRanges(), s.coverage);
        QCOMPARE(stripEvents(), s.strip);
        Analyser *a2 = m_window->analyser2();
        QCOMPARE(pitchEvents(a2), s.pitch);
        QCOMPARE(a2 ? noteEvents(a2->getLayer(Analyser::Notes))
                 : sv::EventVector(), s.notes);
        if (s.frames >= 0) {
            QTRY_VERIFY(takeAudio() &&
                        takeAudio()->getFrameCount() == s.frames);
        }
    }

    int alternateLayersInDocument() {
        int n = 0, octaves = 0;
        for (sv::Layer *layer : m_window->document()->getLayers()) {
            if (AlternatePitchTrack::octavesFromLayerName
                (layer->objectName(), octaves)) ++n;
        }
        return n;
    }

    // True once the alternate track is the reference's, note for note
    bool alternateMatchesReference() {
        auto alt = pitchEvents(m_window->alternatePitch()->getLayer());
        auto ref = pitchEvents(m_window->analyser());
        int octaves = m_window->alternatePitch()->getOctaves();
        if (ref.empty() || alt.size() != ref.size()) return false;
        for (size_t i = 0; i < ref.size(); ++i) {
            if (alt[i].getFrame() != ref[i].getFrame()) return false;
            double want = AlternatePitchTrack::shifted
                (ref[i].getValue(), octaves);
            if (std::fabs(alt[i].getValue() - want) > 1e-3 * want) {
                return false;
            }
        }
        return true;
    }

    // The timed lyrics.  The layers are counted by the name a session
    // knows the lyrics by, spelled out here: it is part of the file format

    int lyricsLayersInDocument() {
        int n = 0;
        for (sv::Layer *layer : m_window->document()->getLayers()) {
            if (layer->objectName() == "Lyrics") ++n;
        }
        return n;
    }

    int lyricsLayersInPane0() {
        int n = 0;
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        if (!pane) return 0;
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            if (pane->getLayer(i)->objectName() == "Lyrics") ++n;
        }
        return n;
    }

    // Found by name, as a session load finds it, and not from LyricsTrack:
    // the point is often whether that still has the layer the pane shows
    sv::Layer *lyricsLayerInPane0() {
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        if (!pane) return nullptr;
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            if (pane->getLayer(i)->objectName() == "Lyrics") {
                return pane->getLayer(i);
            }
        }
        return nullptr;
    }

    sv::EventVector lyricsEvents() {
        sv::RegionLayer *layer = m_window->lyrics()->getLayer();
        if (!layer) return {};
        auto model = sv::ModelById::getAs<sv::RegionModel>(layer->getModel());
        return model ? model->getAllEvents() : sv::EventVector();
    }

    // The lyrics are the very layer and model they were, with the same
    // words, on show, under the top layer and out of the play source.  The
    // model id is what proves the layer is the same one: a model id is
    // never used twice, unlike the address of a layer
    void verifyLyricsUntouched(sv::Layer *layer, sv::ModelId model,
                               const sv::EventVector &events, QString when) {
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        QVERIFY(pane);
        QVERIFY2(lyricsLayersInDocument() == 1 && lyricsLayersInPane0() == 1,
                 qPrintable(when + ": not one lyrics layer"));
        sv::Layer *found = lyricsLayerInPane0();
        QVERIFY2(found && found == layer && found->getModel() == model,
                 qPrintable(when + ": the lyrics layer was replaced"));
        QVERIFY2(m_window->lyrics()->getLayer() == found,
                 qPrintable(when + ": LyricsTrack has let go of the layer"));
        QVERIFY2(lyricsEvents() == events,
                 qPrintable(when + ": the words have changed"));
        QVERIFY2(!found->isLayerDormant(pane) && m_window->lyrics()->isVisible(),
                 qPrintable(when + ": the lyrics are hidden"));
        QVERIFY2(pane->getTopLayer() != found,
                 qPrintable(when + ": the lyrics are the pane's top layer"));
        QVERIFY2(m_window->playSource()->getModels().count(model) == 0,
                 qPrintable(when + ": the lyrics are in the play source"));

        // and the waveforms under them faded, the take's as well: a new
        // take, a switch and a session load each make the take's analyser
        // and its waveform again
        QString colour = waveformColour(m_window->analyser());
        QVERIFY2(colour == "Pale Grey",
                 qPrintable(when + ": the reference's waveform is " + colour));
        Analyser *a2 = m_window->analyser2();
        if (a2 && a2->getLayer(Analyser::Audio)) {
            colour = waveformColour(a2);
            QVERIFY2(colour == "Pale Grey",
                     qPrintable(when + ": the take's waveform is " + colour));
        }
    }

    // The name of the colour of an analyser's waveform, "" if it has none
    static QString waveformColour(Analyser *a) {
        int colour = colourOf(a ? a->getLayer(Analyser::Audio) : nullptr);
        if (colour < 0) return {};
        return sv::ColourDatabase::getInstance()->getColourName(colour);
    }

    // The word the lyrics layer has highlighted, "" for none
    QString highlightedWord() {
        sv::RegionLayer *layer = m_window->lyrics()->getLayer();
        sv::Event e(0);
        if (!layer || !layer->getHighlightedEvent(e)) return {};
        return e.getLabel();
    }

    // --- Editing the lyrics with the mouse in pane 0 ---

    sv::Pane *pane0() { return m_window->paneStack()->getPane(0); }

    // The lyrics' box row in pane 0, the pane painted first: the layer
    // knows where the row is only once it has painted it there
    QRect lyricsBoxRow() {
        sv::Pane *pane = pane0();
        sv::RegionLayer *layer = m_window->lyrics()->getLayer();
        if (!pane || !layer) return {};
        pane->grab();
        return layer->getLyricsBoxRow(pane);
    }

    // The word with this text as the model holds it now; frame -1 if
    // there is none
    sv::Event lyricsWord(QString text) {
        for (const sv::Event &e : lyricsEvents()) {
            if (e.getLabel() == text) return e;
        }
        return sv::Event(-1);
    }

    static sv::sv_frame_t endOf(const sv::Event &e) {
        return e.getFrame() + e.getDuration();
    }

    // The column of pane 0 that a frame falls in: a word's box runs from
    // the column of its start to the one before the column of its end
    int columnOf(sv::sv_frame_t frame) { return pane0()->getXForFrame(frame); }

    // A mouse event as Qt gives it to the pane: to its event filters
    // first, then to the pane.  Not QTest::mouseMove, which does not
    // carry the buttons held
    void sendMouse(QEvent::Type type, QPoint pos, Qt::MouseButton button,
                   Qt::MouseButtons buttons,
                   Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        sv::Pane *pane = pane0();
        QVERIFY(pane);
        QMouseEvent event(type, QPointF(pos), QPointF(pane->mapToGlobal(pos)),
                          button, buttons, modifiers);
        QApplication::sendEvent(pane, &event);
    }

    void hoverAt(QPoint pos) {
        sendMouse(QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton);
    }
    void pressAt(QPoint pos) {
        sendMouse(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton);
    }
    void moveHeldTo(QPoint pos) {
        sendMouse(QEvent::MouseMove, pos, Qt::NoButton, Qt::LeftButton);
    }
    void releaseAt(QPoint pos) {
        sendMouse(QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton);
    }

    // The same with Shift held
    void shiftPressAt(QPoint pos) {
        sendMouse(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton,
                  Qt::ShiftModifier);
    }
    void shiftMoveHeldTo(QPoint pos) {
        sendMouse(QEvent::MouseMove, pos, Qt::NoButton, Qt::LeftButton,
                  Qt::ShiftModifier);
    }
    void shiftReleaseAt(QPoint pos) {
        sendMouse(QEvent::MouseButtonRelease, pos, Qt::LeftButton,
                  Qt::NoButton, Qt::ShiftModifier);
    }

    // Press at one point, move to the other a few pixels at a time with
    // the button held, and let go there
    void dragFromTo(QPoint from, QPoint to) {
        pressAt(from);
        int steps = std::max(1, std::abs(to.x() - from.x()) / 3);
        for (int i = 1; i <= steps; ++i) {
            moveHeldTo(QPoint(from.x() + (to.x() - from.x()) * i / steps,
                              from.y() + (to.y() - from.y()) * i / steps));
        }
        releaseAt(to);
    }

    // The same with Shift held throughout
    void shiftDragFromTo(QPoint from, QPoint to) {
        shiftPressAt(from);
        int steps = std::max(1, std::abs(to.x() - from.x()) / 3);
        for (int i = 1; i <= steps; ++i) {
            shiftMoveHeldTo(QPoint(from.x() + (to.x() - from.x()) * i / steps,
                                   from.y() + (to.y() - from.y()) * i / steps));
        }
        shiftReleaseAt(to);
    }

    // The words, every one moved by the same number of frames
    static sv::EventVector shiftedBy(const sv::EventVector &words,
                                     sv::sv_frame_t by) {
        sv::EventVector moved;
        for (const sv::Event &e : words) {
            moved.push_back(e.withFrame(e.getFrame() + by));
        }
        return moved;
    }

    // A reference with the gapped lyrics on it, painted.  Yksi and kaksi
    // share an edge at 0.6 s; kolme comes after a gap.  The row's middle
    // is where the tests point, at the columns the words are in.
    //
    // The window is never shown, and its layout gives pane 0 what a
    // 640x480 window leaves over, a few pixels high, or never lays a new
    // pane out at all.  A size of its own, then, and a zoom at which the
    // whole reference is in view and a word is about a hundred pixels
    // wide
    QRect m_row;
    void showEditableLyrics() {
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->doImportLyricsFrom(writeLrc(gappedLyrics())));

        sv::Pane *pane = pane0();
        pane->setFixedSize(1000, 120);
        pane->setZoomLevel(sv::ZoomLevel(sv::ZoomLevel::FramesPerPixel, 128));
        pane->setCentreFrame(sv::sv_frame_t(rate * 1.0));
        m_row = lyricsBoxRow();
        QVERIFY2(!m_row.isEmpty(), "the lyrics were not painted");
        QVERIFY2(m_row.top() > 12 && m_row.bottom() < pane->height(),
                 qPrintable(QString("the box row is at %1 to %2 of %3")
                            .arg(m_row.top()).arg(m_row.bottom())
                            .arg(pane->height())));

        // All in view, and wide enough for the grab not to reach across
        // a word
        int x0 = columnOf(lyricsWord("Yksi").getFrame());
        int x1 = columnOf(endOf(lyricsWord("kolme")));
        QVERIFY2(x0 > 100 && x1 < pane->width() - 100,
                 qPrintable(QString("the words are at %1 to %2")
                            .arg(x0).arg(x1)));
        int width = columnOf(endOf(lyricsWord("kaksi"))) -
            columnOf(lyricsWord("kaksi").getFrame());
        QVERIFY2(width >= 80, qPrintable(QString("kaksi is %1 pixels wide")
                                        .arg(width)));

        m_window->discardModifications();
    }

    // Edit > Edit Lyrics, as the user switches it on
    void switchLyricsEditingOn() {
        QAction *edit = m_window->editLyricsAction();
        QVERIFY(edit->isEnabled());
        QVERIFY(!edit->isChecked());
        edit->trigger();
        QVERIFY(edit->isChecked());
        QVERIFY(m_window->lyricsEditor()->isEnabled());
    }

    void lyricsEditFixture(FakeAudioIO::Config config = FakeAudioIO::Config()) {
        makeWindow(config);
        showEditableLyrics();
        if (QTest::currentTestFailed()) return;
        switchLyricsEditingOn();
    }

    // A drag from the shared edge, which with edit mode off is the pane's
    // own: it moves the view, and no word
    void dragIsThePanes(const sv::EventVector &before) {
        int edge = columnOf(lyricsWord("kaksi").getFrame());
        dragFromTo(inRow(edge), inRow(edge + 30));
        QCOMPARE(lyricsEvents(), before);
        QVERIFY2(columnOf(lyricsWord("kaksi").getFrame()) != edge,
                 "the pane did not get the drag");
    }

    // How far the pane's frames move for a move of the pointer from one
    // column to another
    sv::sv_frame_t framesBetween(int x0, int x1) {
        return pane0()->getFrameForX(x1) - pane0()->getFrameForX(x0);
    }

    QPoint inRow(int x) { return QPoint(x, m_row.center().y()); }

    // A double-click as Qt gives it: a press and a release, then the
    // second press as the double-click, and its release
    void doubleClickAt(QPoint pos) {
        pressAt(pos);
        releaseAt(pos);
        sendMouse(QEvent::MouseButtonDblClick, pos, Qt::LeftButton,
                  Qt::LeftButton);
        releaseAt(pos);
    }

    void rightPressAt(QPoint pos) {
        sendMouse(QEvent::MouseButtonPress, pos, Qt::RightButton,
                  Qt::RightButton);
    }

    // The texts of the entries of the words' menu at a point, with a "-"
    // in front of a disabled one; none where the menu would not open
    QStringList menuAt(QPoint pos) {
        QStringList texts;
        for (const auto &e : m_window->lyricsEditor()->menuEntriesAt(pos)) {
            texts << (e.enabled ? "" : "-") + e.text;
        }
        return texts;
    }

    // The entry of the words' menu at a point with this text, chosen as a
    // click on it chooses it; false if there is none
    bool chooseAt(QPoint pos, QString text) {
        for (const auto &e : m_window->lyricsEditor()->menuEntriesAt(pos)) {
            if (e.text == text) {
                m_window->lyricsEditor()->choose(e);
                return true;
            }
        }
        return false;
    }

    // The words' menu the editor has popped up over pane 0, if it is on
    // show
    QMenu *wordsMenu() {
        for (QMenu *menu : pane0()->findChildren<QMenu *>()) {
            if (menu->isVisible()) return menu;
        }
        return nullptr;
    }

    static QStringList actionTexts(QMenu *menu) {
        QStringList texts;
        if (menu) for (QAction *a : menu->actions()) texts << a->text();
        return texts;
    }

    // Close every menu on show, the words' and Tony's own: both are
    // popped up, with no event loop of their own to end.  How many
    int closeMenus() {
        int n = 0;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            QMenu *menu = qobject_cast<QMenu *>(w);
            if (menu && menu->isVisible()) {
                menu->close();
                ++n;
            }
        }
        return n;
    }

    // The lyrics' model, to change the words straight in it, as a test's
    // setup: no command, and nothing marked modified
    std::shared_ptr<sv::RegionModel> lyricsModel() {
        return sv::ModelById::getAs<sv::RegionModel>
            (m_window->lyrics()->getModelId());
    }

    // One word in the model replaced by another, and the pane painted
    // again: the editor goes by the boxes as painted
    void replaceWord(const sv::Event &from, const sv::Event &to) {
        auto model = lyricsModel();
        QVERIFY(model && model->containsEvent(from));
        model->remove(from);
        model->add(to);
        m_row = lyricsBoxRow();
    }

    // Every key of the settings, with its value, in the form the test
    // messages show
    static QMap<QString, QString> allSettings() {
        QSettings settings;
        QMap<QString, QString> all;
        for (const QString &key : settings.allKeys()) {
            all[key] = settings.value(key).toString();
        }
        return all;
    }

    static QStringList settingsChanged(const QMap<QString, QString> &before,
                                       const QMap<QString, QString> &after) {
        QStringList changed;
        for (auto i = after.begin(); i != after.end(); ++i) {
            if (!before.contains(i.key())) {
                changed << i.key() + " added: " + i.value();
            } else if (before[i.key()] != i.value()) {
                changed << i.key() + ": " + before[i.key()] + " -> " + i.value();
            }
        }
        for (auto i = before.begin(); i != before.end(); ++i) {
            if (!after.contains(i.key())) changed << i.key() + " removed";
        }
        return changed;
    }

    static QString lyricsFixture(const char *name) {
        return QString(TONY_TEST_DATA_DIR) + "/lyrics/" + name;
    }

    // As the import reads it: TTML or LRC
    static Lyrics lyricsIn(QString path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return parseLyrics(file.readAll()).lyrics;
    }

    // What an import of this file has to put in the model: the words on
    // the reference's timeline
    sv::EventVector expectedLyricsEvents(QString path) {
        auto reference = sv::ModelById::get(m_window->mainModelId());
        if (!reference) return {};
        return lyricsToEvents(lyricsIn(path), reference->getSampleRate());
    }

    QString writeLrc(const QByteArray &text, const char *suffix = "lrc") {
        QString path = m_dir.filePath
            (QString("lyrics-%1.%2").arg(++m_fileCounter).arg(suffix));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(text) != text.size()) {
            return {};
        }
        return path;
    }

    // Two words and a gap, then one more: every word with its end given
    static QByteArray gappedLyrics() {
        return "[00:00.20]<00:00.20>Yksi <00:00.60>kaksi <00:00.90>\n"
               "[00:01.40]<00:01.40>kolme <00:01.80>\n";
    }

    // The pre-roll's length has no UI: it is read from the settings when
    // Record is pressed. These are the test suite's own settings
    // (tony-app-test), not the user's
    static void setPreRollSeconds(double seconds) {
        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.setValue("prerollseconds", seconds);
        settings.endGroup();
    }

    // A recording of two notes, the first as long as "first" seconds and
    // the second as long as "then": what is sung during a lead-in, or
    // after the end of a selection, is the one the take must not hold
    static std::vector<float> twoNotes(double first, double then) {
        auto signal = tone(lowHz, first);
        auto second = tone(highHz, then);
        signal.insert(signal.end(), second.begin(), second.end());
        return signal;
    }

    // --- A device that does not run at the reference's rate ---

    // What phones run at. The reference is a model at 44.1 kHz whatever
    // its file was, and the device records at its own rate
    static constexpr int otherDeviceRate = 48000;

    // Sung at the device's rate: lowHz and highHz, as sines. pYIN gets
    // the take at 44.1 kHz, where these two have whole numbers of samples
    // per period; tones without (240 and 320 Hz, sines or sawtooths) came
    // out as subharmonics. Sines, because a sawtooth made at 48 kHz that
    // is not whole there aliases into partials that are not harmonics,
    // and the sawtooths whole at both rates (100, 150, 300 Hz) came out
    // an octave low after a step, or not, as the step fell between frames
    static std::vector<float> toneAt(double hz, double seconds,
                                     int sampleRate) {
        return TestSignals::sine(hz, sampleRate, int(seconds * sampleRate));
    }

    static std::vector<float> twoTonesAt(double firstHz, double first,
                                         double thenHz, double then,
                                         int sampleRate) {
        auto signal = toneAt(firstHz, first, sampleRate);
        auto second = toneAt(thenHz, then, sampleRate);
        signal.insert(signal.end(), second.begin(), second.end());
        return signal;
    }

    // Record from P for ms milliseconds. recordedFrames receives the
    // length of the recording the device made, which must be at its
    // rate
    void recordAt(sv::sv_frame_t P, int ms, int deviceRate,
                  sv::sv_frame_t &recordedFrames) {
        recordedFrames = -1;
        m_window->seekTo(P);
        startTake();
        if (QTest::currentTestFailed()) return;
        QString path;
        {
            // Not held past this: the recording is to be released, and
            // its file closed, when the take stops
            auto recording = sv::ModelById::getAs<sv::WritableWaveFileModel>
                (m_window->currentRecordingModelId());
            QVERIFY(recording);
            path = recording->getLocation();
        }
        QTest::qWait(ms);
        stopTake();
        if (QTest::currentTestFailed()) return;
        sv::WavFileReader reader { sv::FileSource(path) };
        QVERIFY(reader.isOK());
        QCOMPARE(int(reader.getSampleRate()), deviceRate);
        recordedFrames = reader.getFrameCount();
    }

    // The first and the last frame of [from, to) at which the take's
    // audio model is louder than a whisper, or -1 for both. The model,
    // not the file: it is what is played and what pYIN is given, and it
    // is on the reference's timeline whatever the file is
    void soundInTake(sv::sv_frame_t from, sv::sv_frame_t to,
                     sv::sv_frame_t &first, sv::sv_frame_t &last) {
        first = last = -1;
        auto wave = takeAudio();
        if (!wave || to <= from) return;
        auto data = wave->getData(0, from, to - from);
        for (size_t i = 0; i < data.size(); ++i) {
            if (std::fabs(data[i]) > 0.05f) {
                if (first < 0) first = from + sv::sv_frame_t(i);
                last = from + sv::sv_frame_t(i);
            }
        }
    }

    // Where the singing of a recording made at deviceRate has to be:
    // from P, for as long as it was sung, in the take's coverage, audio
    // and pitch alike. The coverage range holding P is returned in range
    void verifyRecordingPlaced(sv::sv_frame_t P, sv::sv_frame_t recordedFrames,
                               int deviceRate, Coverage::Range &range) {
        const sv::sv_frame_t margin = sv::sv_frame_t(0.1 * rate);
        const sv::sv_frame_t close = sv::sv_frame_t(0.01 * rate);

        range = Coverage::Range();
        for (const auto &r : m_window->takes()->getCoverage().getRanges()) {
            if (r.start <= P && P < r.end) range = r;
        }
        QVERIFY2(range.length() > 0,
                 qPrintable(QString("no coverage holds the position %1")
                            .arg(P)));

        // As many seconds as the device recorded, at the reference's rate
        sv::sv_frame_t want = sv::sv_frame_t
            (std::llround(double(recordedFrames) * rate / deviceRate));
        QCOMPARE(range.start, P);
        QVERIFY2(std::llabs(range.length() - want) <= 1,
                 qPrintable(QString("%1 frames were recorded at %2 Hz, which "
                                    "is %3 at %4 Hz; the take covers %5")
                            .arg(recordedFrames).arg(deviceRate).arg(want)
                            .arg(rate).arg(range.length())));

        // The file is at the reference's rate, so its frames are the
        // reference's frames
        QString path = m_window->takes()->getAudioPath();
        sv::WavFileReader file { sv::FileSource(path) };
        QVERIFY(file.isOK());
        QVERIFY2(file.getSampleRate() == rate,
                 qPrintable(QString("the take's audio file is at %1 Hz, not "
                                    "the reference's %2")
                            .arg(file.getSampleRate()).arg(rate)));

        // The sound is where the coverage says it is
        auto wave = takeAudio();
        QVERIFY(wave);
        QTRY_VERIFY(wave->isReady());
        sv::sv_frame_t first = -1, last = -1;
        soundInTake(std::max(sv::sv_frame_t(0), P - margin),
                    range.end + margin, first, last);
        QVERIFY2(std::llabs(first - range.start) <= close &&
                 std::llabs(last - range.end) <= close,
                 qPrintable(QString("the take's audio is loud over [%1,%2]; "
                                    "the recording went into [%3,%4)")
                            .arg(first).arg(last)
                            .arg(range.start).arg(range.end)));

        // and so is the pitch, at the pitch that was sung
        auto events = eventsBetween(pitchEvents(m_window->analyser2()),
                                    range.start - margin, range.end + margin);
        QVERIFY(!events.empty());
        QVERIFY2(std::llabs(events.front().getFrame() - range.start) <= margin &&
                 std::llabs(events.back().getFrame() - range.end) <= margin,
                 qPrintable(QString("the take's pitch runs from %1 to %2; the "
                                    "recording went into [%3,%4)")
                            .arg(events.front().getFrame())
                            .arg(events.back().getFrame())
                            .arg(range.start).arg(range.end)));
    }

    // A first recording at P1 and a second in the gap after it, from a
    // device at deviceRate against the reference at 44.1 kHz
    void verifyTakesPlacedFromDeviceAt(int deviceRate) {
        const double sungHz = highHz;
        FakeAudioIO::Config config;
        config.sampleRate = deviceRate;
        config.input = toneAt(sungHz, 3.0, deviceRate);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 5.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P1 = sv::sv_frame_t(1.0 * rate);
        const sv::sv_frame_t P2 = sv::sv_frame_t(3.0 * rate);
        const sv::sv_frame_t margin = sv::sv_frame_t(0.1 * rate);

        // As recordAt() does, with a look at the cursor on the way: while
        // the take records, it runs from the position at the reference's
        // rate
        m_window->seekTo(P1);
        startTake();
        if (QTest::currentTestFailed()) return;
        QString firstPath;
        {
            auto recording = sv::ModelById::getAs<sv::WritableWaveFileModel>
                (m_window->currentRecordingModelId());
            QVERIFY(recording);
            firstPath = recording->getLocation();
        }
        QTest::qWait(500);
        sv::sv_frame_t during = m_window->playbackFrame();
        sv::sv_frame_t recordedSoFar =
            m_window->recordTarget()->getRecordDuration();
        sv::sv_frame_t cursorWant = P1 + sv::sv_frame_t
            (std::llround(double(recordedSoFar) * rate / deviceRate));
        QVERIFY2(std::llabs(during - cursorWant) <= 1,
                 qPrintable(QString("%1 frames at %2 Hz into a take from "
                                    "frame %3, the cursor is at %4, not %5")
                            .arg(recordedSoFar).arg(deviceRate).arg(P1)
                            .arg(during).arg(cursorWant)));
        QTest::qWait(500);
        stopTake();
        if (QTest::currentTestFailed()) return;

        sv::sv_frame_t firstFrames = -1;
        {
            sv::WavFileReader reader { sv::FileSource(firstPath) };
            QVERIFY(reader.isOK());
            QCOMPARE(int(reader.getSampleRate()), deviceRate);
            firstFrames = reader.getFrameCount();
        }

        Coverage::Range first;
        verifyRecordingPlaced(P1, firstFrames, deviceRate, first);
        if (QTest::currentTestFailed()) return;

        auto wave = takeAudio();
        QVERIFY(wave);
        QCOMPARE(wave->getFrameCount(), first.end);
        QVERIFY2(std::fabs(TestSignals::centsBetween
                           (medianHz(pitchEvents(m_window->analyser2())),
                            sungHz)) < 10.0,
                 qPrintable(QString("the take's median pitch is %1 Hz; it "
                                    "was sung at %2")
                            .arg(medianHz(pitchEvents(m_window->analyser2())))
                            .arg(sungHz)));
        sv::sv_frame_t loudFirst = -1, loudLast = -1;
        soundInTake(0, P1 - margin, loudFirst, loudLast);
        QVERIFY2(loudFirst < 0, "the take's audio is not silent before the "
                 "position it was recorded at");

        // A second recording, into the gap after the first: the take so
        // far is at the reference's rate, whatever the recording is at
        sv::sv_frame_t secondFrames = -1;
        recordAt(P2, 800, deviceRate, secondFrames);
        if (QTest::currentTestFailed()) return;

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 2);
        QCOMPARE(ranges[0], first);

        Coverage::Range second;
        verifyRecordingPlaced(P2, secondFrames, deviceRate, second);
        if (QTest::currentTestFailed()) return;

        wave = takeAudio();
        QVERIFY(wave);
        QTRY_COMPARE(wave->getFrameCount(), second.end);

        soundInTake(first.end + margin, P2 - margin, loudFirst, loudLast);
        QVERIFY2(loudFirst < 0,
                 "the gap between the two recordings is not silent");
        QVERIFY2(eventsBetween(pitchEvents(m_window->analyser2()),
                               first.end + margin, P2 - margin).empty(),
                 "the pitch track has something in the gap between the two "
                 "recordings");
    }

    // Not a slot: QtTest would run it as a test
    void dismissDialog() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) return;
        QString description = modal->windowTitle();
        if (auto box = qobject_cast<QMessageBox *>(modal)) {
            description += ": " + box->text();
            m_dialogs.push_back(description);
            // A question with buttons of its own is not answered by
            // rejecting it: the session reader's "do you want to locate
            // this file?" asks again until one of its buttons is pressed
            // (InteractiveFileFinder::locateInteractive()).  The last
            // button is Cancel in every question Tony can show
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

        QSettings().clear();

        // Otherwise the MainWindow constructor asks, in a dialog
        QSettings settings;
        settings.beginGroup("Preferences");
        settings.setValue(QString("network-permission-%1").arg(TONY_VERSION),
                          false);
        settings.endGroup();

        // As main() does; without it a .ton file is not a session
        sv::InteractiveFileFinder::getInstance()
            ->setApplicationSessionExtension("ton");

        // Takes go here and not among the user's recordings
        sv::RecordDirectory::setRecordContainerDirectory
            (m_dir.filePath("recorded"));

        connect(&m_watchdog, &QTimer::timeout,
                this, [this]() { dismissDialog(); });
        m_watchdog.start(50);
    }

    void init() {
        m_dialogs.clear();
        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.setValue("playrefwhilerecording", false);
        // The toolbar toggles of the recording behaviour, and the length
        // of the pre-roll: a test that switched one on must not leave it
        // on for the next
        settings.setValue("preroll", false);
        settings.setValue("recordintoselection", false);
        settings.remove("prerollseconds");
        settings.endGroup();

        // The audible flags are shared by both analysers; a test that
        // failed half way must not leave the next one's tracks muted
        settings.beginGroup("Analyser");
        settings.remove("");
        settings.endGroup();

        // Asking before recording over existing singing is the default,
        // whatever a test that switched it off did
        SingingTakes::setOverwriteConfirmationWanted(true);
    }

    void cleanup() {
        // A round trip a test stored would place the next test's takes.
        // First, as the waits below return early when they fail
        QSettings().remove("LatencyCalibration");

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

    void record_creates_singing_track() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        int panes = m_window->paneStack()->getPaneCount();
        QCOMPARE(panes, 2); // analysis pane and ruler strip
        QVERIFY(!m_window->analyser2());

        take(1000);
        if (QTest::currentTestFailed()) return;

        QCOMPARE(m_window->paneStack()->getPaneCount(), panes);
        QCOMPARE(m_window->paneStack()->getHiddenPaneCount(), 0);
        QCOMPARE(m_window->pendingExtraPaneCount(), 0);

        Analyser *a2 = m_window->analyser2();
        QVERIFY(a2);
        sv::ModelId singing = a2->getMainModelId();
        QVERIFY(singing != m_window->mainModelId());

        // The singing track is the take's own audio file, which the
        // recording was spliced into: a plain wave file starting at frame
        // 0, not the recording itself
        auto wave = sv::ModelById::getAs<sv::WaveFileModel>(singing);
        QVERIFY2(wave, "the singing model is not a wave file model");
        QVERIFY2(!sv::ModelById::isa<sv::WritableWaveFileModel>(singing),
                 "the singing model is the recording, not the take's audio");
        QCOMPARE(wave->getStartFrame(), sv::sv_frame_t(0));
        QVERIFY(wave->getFrameCount() > sv::sv_frame_t(0.8 * rate));

        // and the recording has been let go of
        QVERIFY(!m_window->recordingLayer());
        QVERIFY(m_window->currentRecordingModelId().isNone());

        // The take covers what was recorded, from frame 0 here
        QVERIFY(m_window->takes()->haveTake());
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, sv::sv_frame_t(0));
        QCOMPARE(ranges[0].end, wave->getFrameCount());

        QCOMPARE(colourOf(a2->getLayer(Analyser::PitchTrack)),
                 colourNamed("Orange"));
        QCOMPARE(colourOf(a2->getLayer(Analyser::Notes)),
                 colourNamed("Bright Purple"));
        QVERIFY(paneHasLayer(0, a2->getLayer(Analyser::PitchTrack)));
        QVERIFY(paneHasLayer(0, a2->getLayer(Analyser::Notes)));

        auto events = pitchEvents(a2);
        QVERIFY(events.size() > 50);
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(events), highHz)) < 10.0);

        // and the reference was left alone
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           lowHz)) < 10.0);
    }

    void live_dots_appear() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(1000);

        QVERIFY(m_window->realtimeTracker());
        QVERIFY(m_window->realtimeLayer());
        QVERIFY(paneHasLayer(0, m_window->realtimeLayer()));
        // Drawn by itself as dots come, not with every layer of the pane
        QVERIFY(!m_window->realtimeLayer()->isCachedInView());
        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
            (m_window->realtimeModelId());
        QVERIFY(model);
        // One dot per hop (review finding 12)
        QCOMPARE(int(model->getResolution()),
                 int(RealtimePitchTracker::kHopSize));
        auto events = model->getAllEvents();
        QVERIFY2(events.size() > 20,
                 qPrintable(QString("only %1 live dots after a second")
                            .arg(events.size())));
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(events), highHz)) < 10.0);

        stopTake();
    }

    // A microphone on input 2 of an interface, nothing on input 1: the
    // dots and the take's pitch come from the mixdown, so they are there
    void live_dots_from_the_second_input() {
        FakeAudioIO::Config config;
        config.channels = 2;
        config.inputChannel = 1;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(1000);
        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
            (m_window->realtimeModelId());
        QVERIFY(model);
        auto events = model->getAllEvents();
        QVERIFY2(events.size() > 20,
                 qPrintable(QString("only %1 live dots after a second of "
                                    "singing into input 2")
                            .arg(events.size())));
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(events), highHz)) < 10.0);

        stopTake();
        if (QTest::currentTestFailed()) return;
        auto pitch = pitchEvents(m_window->analyser2());
        QVERIFY2(pitch.size() > 20, "the take of input 2 has no pitch track");
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitch), highHz)) < 10.0);
    }

    void live_dots_removed() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(800);
        sv::ModelId liveModel = m_window->realtimeModelId();
        QVERIFY(!liveModel.isNone());
        stopTake();
        if (QTest::currentTestFailed()) return;

        // The dots go when pYIN finishes, and the application hears of
        // that an event-loop turn after the models say so
        QTRY_VERIFY(!m_window->realtimeLayer());
        QVERIFY(!m_window->realtimeTracker());
        QVERIFY(m_window->realtimeModelId().isNone());
        QVERIFY2(!sv::ModelById::get(liveModel),
                 "the live pitch model outlived its layer");
        QVERIFY(!m_window->recordingInProgress());
        QVERIFY(!m_window->recordingAsSingingTrack());
    }

    // Review finding 9: the pitch track replaces the dots. Between Stop
    // and the end of pYIN the dots are all the singer has to look at
    void live_dots_stay_until_analysis() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(1000);
        sv::ModelId liveModel = m_window->realtimeModelId();
        QVERIFY(!liveModel.isNone());

        // Stop. pYIN has been started and cannot have finished: its
        // completion arrives through the event loop, which has not run
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(!analysed(m_window->analyser2()));

        QVERIFY2(m_window->realtimeLayer(),
                 "the live dots went at Stop, before pYIN had anything "
                 "to show in their place");
        QVERIFY(paneHasLayer(0, m_window->realtimeLayer()));
        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>(liveModel);
        QVERIFY(model);
        int dots = model->getEventCount();
        QVERIFY(dots > 20);

        // The take is over all the same: nothing is tracking it, and
        // the state is that of a finished take
        QVERIFY(!m_window->realtimeTracker());
        QVERIFY(!m_window->recordingInProgress());
        QVERIFY(!m_window->recordingAsSingingTrack());

        // and they go no sooner than the pitch track is complete
        QTRY_VERIFY_WITH_TIMEOUT(!m_window->realtimeLayer(), 30000);
        QVERIFY(m_window->analyser2()->getInitialAnalysisCompletion() >= 100);
        QVERIFY(!pitchEvents(m_window->analyser2()).empty());
        QVERIFY2(!sv::ModelById::get(liveModel),
                 "the live pitch model outlived its layer");
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
    }

    // Review finding 10: pitch events still queued when the take ends
    // must not draw dots or write to the status bar
    void stale_pitch_event_ignored() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(600);
        sv::ModelId liveModel = m_window->realtimeModelId();

        // The real case: an event queued before the take ends and
        // delivered after it. Frame 20000 is well inside the take, and
        // with no reference playing there is no latency to take off
        // it, so it would make a dot. The tracker may have queued
        // events of its own as well; the same goes for them
        QCOMPARE(m_window->recordingLatencyFrames(), sv::sv_frame_t(0));
        // Queued as a functor: invoking the slot by name depends on the
        // Qt version matching "sv::sv_frame_t" against what moc recorded,
        // and Qt 6.4 does not
        TestMainWindow *window = m_window;
        QVERIFY2(QMetaObject::invokeMethod
                 (m_window, [window]() {
                      window->doRealtimePitchDetected(20000, 440.0);
                  }, Qt::QueuedConnection),
                 "the pitch event could not be queued");
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());

        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>(liveModel);
        QVERIFY(model);
        int dots = model->getEventCount();
        m_window->setStatusText("after the take");
        QCoreApplication::processEvents();
        QVERIFY2(model->getEventCount() == dots,
                 qPrintable(QString("a pitch event queued during the take "
                                    "and delivered after it drew a dot: %1 "
                                    "dots, there were %2")
                            .arg(model->getEventCount()).arg(dots)));
        QVERIFY2(m_window->statusText() == QString("after the take"),
                 qPrintable(QString("a pitch event queued during the take "
                                    "and delivered after it wrote \"%1\" to "
                                    "the status bar")
                            .arg(m_window->statusText())));

        // and one that arrives later still
        m_window->doRealtimePitchDetected(20000, 440.0);
        QCOMPARE(model->getEventCount(), dots);
        QCOMPARE(m_window->statusText(), QString("after the take"));

        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        QTRY_VERIFY(!m_window->realtimeLayer());
        m_window->setStatusText("after the analysis");
        m_window->doRealtimePitchDetected(20000, 440.0);
        QCOMPARE(m_window->statusText(), QString("after the analysis"));
    }

    // A phone's GUI thread is several times slower than this machine's.
    // Made slower than the tracker finds pitch (an estimate a hop, 5.8
    // ms), it must still keep the dots up with the singing: given the
    // dots one at a time it would fall further behind for as long as the
    // take lasted
    void live_dots_keep_up_with_a_slow_gui() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 5.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 5.0)));
        if (QTest::currentTestFailed()) return;

        m_window->setLiveDotsDelay(10);
        startTake();
        if (QTest::currentTestFailed()) return;

        // With no reference playing there is no latency, so a dot is at
        // the frame of the recording its window was centred on. The most
        // the newest may trail what the device has recorded: half a
        // window, a record update, the tracker's poll, the wait for the
        // next batch and the slow GUI thread, with room to spare. Per
        // estimate, this GUI thread is 0.4 s behind after a second, 1.2 s
        // after three
        const sv::sv_frame_t bound = sv::sv_frame_t(0.4 * rate);
        QElapsedTimer timer;
        timer.start();
        sv::sv_frame_t worst = 0;
        int looked = 0;
        QString detail;
        while (timer.elapsed() < 3000) {
            QTest::qWait(100);
            // (the tracker starts an event-loop turn after the take)
            if (timer.elapsed() < 500) continue;
            auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
                (m_window->realtimeModelId());
            QVERIFY(model);
            auto events = model->getAllEvents();
            if (events.empty()) continue;
            ++looked;
            sv::sv_frame_t recorded =
                m_window->recordTarget()->getFramesReceived();
            sv::sv_frame_t behind = recorded - events.back().getFrame();
            if (behind > worst) {
                worst = behind;
                detail = QString("%1 ms into the take the newest dot is at "
                                 "%2 s, %3 ms behind the %4 s recorded")
                    .arg(timer.elapsed())
                    .arg(double(events.back().getFrame()) / rate, 0, 'f', 2)
                    .arg(1000.0 * double(behind) / rate, 0, 'f', 0)
                    .arg(double(recorded) / rate, 0, 'f', 2);
            }
        }
        m_window->setLiveDotsDelay(0);
        qInfo("%s", qPrintable(detail));
        QVERIFY2(worst <= bound, qPrintable(detail));
        QVERIFY2(looked >= 10, "hardly any live dots");

        stopTake();
    }

    // Review finding 8: during the take, the dots sit where the pitch
    // track will. Same device and singer as latency_end_to_end, but
    // looked at before Stop
    void live_dots_compensated() {
        const int K = 3 * 4096;
        FakeAudioIO::Config config;
        config.playbackLatency = 2 * 4096;
        config.recordLatency = 4096;
        config.input = melody(0.75);
        config.inputDelay = K;
        config.inputFollowsPlayback = true;
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(melody(0.75)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(2200);

        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
            (m_window->realtimeModelId());
        QVERIFY(model);
        auto dots = model->getAllEvents();
        stopTake();
        if (QTest::currentTestFailed()) return;

        sv::sv_frame_t refStep = stepFrame(pitchEvents(m_window->analyser()));
        sv::sv_frame_t dotStep = stepFrame(dots);
        QVERIFY(refStep > 0);
        QVERIFY2(dotStep > 0, "the live dots never reached the second note");

        // The dots are coarser than pYIN: allow them a YIN window
        sv::sv_frame_t error = dotStep - refStep;
        QVERIFY2(std::llabs(error) <= 2048,
                 qPrintable(QString("live step at %1, reference step at %2: "
                                    "%3 frames (%4 ms) apart; the round trip "
                                    "is %5 frames and the reference started "
                                    "%6 frames into the take")
                            .arg(dotStep).arg(refStep).arg(error)
                            .arg(1000.0 * double(error) / rate, 0, 'f', 1)
                            .arg(K)
                            .arg(m_window->fake()
                                 ->getFramesBeforePlayStart())));
    }

    // The automated latency test. The device reports a round trip of K
    // frames, and its input is the reference melody starting K frames
    // after the first reference sample is played: a singer exactly on
    // time. After the take the two pitch tracks should line up.
    void latency_end_to_end() {
        const int K = 3 * 4096;
        FakeAudioIO::Config config;
        config.playbackLatency = 2 * 4096;
        config.recordLatency = 4096;
        config.input = melody(0.75);
        config.inputDelay = K;
        config.inputFollowsPlayback = true;
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(melody(0.75)));
        if (QTest::currentTestFailed()) return;

        take(2200);
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_window->fake()->getPlayStartFrame() >= 0,
                 "the reference was never played");

        auto wave = takeAudio();
        QVERIFY(wave);
        // The compensation is in the audio: the recording was read from
        // the frame the latency points at, so the take's own file starts
        // at frame 0 of the reference's timeline
        QCOMPARE(wave->getStartFrame(), sv::sv_frame_t(0));

        // The compensation is the round trip plus what the application
        // took the start gap to be (review finding 14). The device knows
        // what the gap really was. The application counts in whole
        // blocks, in the audio callback, so the two agree to within a
        // few samples; an estimate made on the GUI thread is out by a
        // block or more
        sv::sv_frame_t gap = m_window->fake()->getFramesBeforePlayStart();
        sv::sv_frame_t assumedGap = m_window->recordingLatencyFrames() - K;
        QVERIFY2(std::llabs(assumedGap - gap) <= 16,
                 qPrintable(QString("the application took the start gap to "
                                    "be %1 frames; it was %2")
                            .arg(assumedGap).arg(gap)));

        sv::sv_frame_t refStep = stepFrame(pitchEvents(m_window->analyser()));
        sv::sv_frame_t sungStep = stepFrame(pitchEvents(m_window->analyser2()));
        QVERIFY(refStep > 0);
        QVERIFY2(sungStep > 0, "the take never reached the second note");

        sv::sv_frame_t error = sungStep - refStep;
        QString detail = QString("sung step at %1, reference step at %2: "
                                 "%3 frames (%4 ms) apart; the reference "
                                 "started %5 frames into the take, and the "
                                 "application took that to be %6")
            .arg(sungStep).arg(refStep).arg(error)
            .arg(1000.0 * double(error) / rate, 0, 'f', 1)
            .arg(gap).arg(assumedGap);

        QVERIFY2(std::llabs(error) <= 2 * hop, qPrintable(detail));
    }

    // latency_end_to_end with a device that reports 50 ms less than its
    // round trip of K frames, and the round trip the audio check measured
    // kept for it: the take is placed with the measured figure, and the
    // two pitch tracks line up
    void latency_measured_round_trip_used() {
        const int K = 3 * 4096;
        const int reportedOut = 2 * 4096;
        const int reportedIn = 4096 - 2205;
        FakeAudioIO::Config config;
        config.playbackLatency = reportedOut;
        config.recordLatency = reportedIn;
        config.input = melody(0.75);
        config.inputDelay = K;
        config.inputFollowsPlayback = true;
        storeRoundTrip(K, reportedOut, reportedIn);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(melody(0.75)));
        if (QTest::currentTestFailed()) return;

        take(2200);
        if (QTest::currentTestFailed()) return;

        sv::sv_frame_t refStep = stepFrame(pitchEvents(m_window->analyser()));
        sv::sv_frame_t sungStep = stepFrame(pitchEvents(m_window->analyser2()));
        QVERIFY(refStep > 0);
        QVERIFY2(sungStep > 0, "the take never reached the second note");
        sv::sv_frame_t error = sungStep - refStep;
        QVERIFY2(std::llabs(error) <= 2 * hop,
                 qPrintable(QString("sung step at %1, reference step at %2: "
                                    "%3 frames (%4 ms) apart; the take was "
                                    "placed with a round trip of %5 frames")
                            .arg(sungStep).arg(refStep).arg(error)
                            .arg(1000.0 * double(error) / rate, 0, 'f', 1)
                            .arg(m_window->takeLatency().roundTrip)));

        TakeLatency used = m_window->takeLatency();
        QVERIFY(used.measured);
        QCOMPARE(used.roundTrip, sv::sv_frame_t(K));
        QCOMPARE(used.reportedOutput, reportedOut / rate);
        QCOMPARE(used.reportedInput, reportedIn / rate);
    }

    // A round trip measured while the device reported other latencies
    // (its buffers have been changed since) is stale: the take is placed
    // with the reported pair. With the latencies it was measured with,
    // the same figure would be in use
    void latency_stale_round_trip_ignored() {
        const int reportedOut = 2 * 4096;
        const int reportedIn = 4096;
        FakeAudioIO::Config config;
        config.playbackLatency = reportedOut;
        config.recordLatency = reportedIn;
        config.input = tone(highHz, 2.0);
        const int stale = reportedOut +
            int(2 * LatencyCalibration::kStaleToleranceSeconds * rate);
        storeRoundTrip(15000, stale, reportedIn);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        take(800);
        if (QTest::currentTestFailed()) return;

        TakeLatency used = m_window->takeLatency();
        QVERIFY(!used.measured);
        QCOMPARE(used.roundTrip, sv::sv_frame_t(reportedOut + reportedIn));

        LatencyCalibration::InUse inUse = m_window->latencyInUse();
        QVERIFY(inUse.source == LatencyCalibration::Source::Reported);
        QVERIFY(inUse.stale);
        QCOMPARE(inUse.roundTrip, (reportedOut + reportedIn) / rate);

        storeRoundTrip(15000, reportedOut, reportedIn);
        inUse = m_window->latencyInUse();
        QVERIFY(inUse.source == LatencyCalibration::Source::Measured);
        QCOMPARE(inUse.roundTrip, 15000 / rate);
    }

    // A device at 48 kHz reporting 2 x 4096 frames out and 4096 in: the
    // take is placed with those 3 x 4096 frames of the recording,
    // although the play source has the output latency in frames of the
    // session, converted as it resamples; the take keeps each latency in
    // seconds, the output's from those converted frames
    void latency_reported_at_the_device_rate() {
        const double deviceRate = 48000.0;
        const int reportedOut = 2 * 4096;
        const int reportedIn = 4096;
        FakeAudioIO::Config config;
        config.sampleRate = int(deviceRate);
        config.playbackLatency = reportedOut;
        config.recordLatency = reportedIn;
        config.input = tone(highHz, 2.0);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        take(800);
        if (QTest::currentTestFailed()) return;

        TakeLatency used = m_window->takeLatency();
        QCOMPARE(used.recordingRate, deviceRate);
        QVERIFY(!used.measured);
        QCOMPARE(used.reportedOutput,
                 std::lround(reportedOut * rate / deviceRate) / rate);
        QCOMPARE(used.reportedInput, reportedIn / deviceRate);
        QVERIFY2(std::llabs(used.roundTrip - (reportedOut + reportedIn)) <= 2,
                 qPrintable(QString("placed with %1 frames at %2 Hz; the "
                                    "device reports %3 + %4")
                            .arg(used.roundTrip).arg(used.recordingRate)
                            .arg(reportedOut).arg(reportedIn)));
    }

    // The same device, chosen before any file is open, as from the audio
    // device menu at startup. The play source has no rate yet, so its
    // resampler passes the output latency on as the device counts it,
    // and tells the play source the device's rate is 0: the round trip
    // is the same, and so is the output latency in seconds
    void latency_reported_with_device_opened_first() {
        const double deviceRate = 48000.0;
        const int reportedOut = 2 * 4096;
        const int reportedIn = 4096;
        FakeAudioIO::Config config;
        config.sampleRate = int(deviceRate);
        config.playbackLatency = reportedOut;
        config.recordLatency = reportedIn;
        config.input = tone(highHz, 2.0);
        makeWindow(config);
        m_window->recreateAudioIO();
        QVERIFY(m_window->fake());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        m_window->setPlayReferenceWhileRecording(true);

        take(800);
        if (QTest::currentTestFailed()) return;

        TakeLatency used = m_window->takeLatency();
        QCOMPARE(used.recordingRate, deviceRate);
        QCOMPARE(used.reportedOutput, reportedOut / deviceRate);
        QVERIFY2(std::llabs(used.roundTrip - (reportedOut + reportedIn)) <= 2,
                 qPrintable(QString("placed with %1 frames at %2 Hz; the "
                                    "device reports %3 + %4")
                            .arg(used.roundTrip).arg(used.recordingRate)
                            .arg(reportedOut).arg(reportedIn)));
    }

    void latency_zero_when_toggle_off() {
        FakeAudioIO::Config config;
        config.playbackLatency = 4096;
        config.recordLatency = 4096;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(false);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        take(800);
        if (QTest::currentTestFailed()) return;

        QCOMPARE(m_window->recordingLatencyFrames(), sv::sv_frame_t(0));
        auto wave = takeAudio();
        QVERIFY(wave);
        QCOMPARE(wave->getStartFrame(), sv::sv_frame_t(0));
        QCOMPARE(m_window->fake()->getPlayStartFrame(), -1L);
        // Nothing to compensate for, so the take is where it was recorded
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, sv::sv_frame_t(0));
    }

    // Record starts the take at the playback position: what is sung lands
    // there on the reference's timeline, and the take's audio file is
    // silence up to it
    // The cursor, which the view follows, runs from where the take is being
    // recorded and not from frame 0
    void take_cursor_runs_from_position() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P = sv::sv_frame_t(2.0 * rate);
        m_window->seekTo(P);
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(500);
        sv::sv_frame_t during = m_window->playbackFrame();
        stopTake();
        if (QTest::currentTestFailed()) return;

        QVERIFY2(during > P && during < P + sv::sv_frame_t(2.0 * rate),
                 qPrintable(QString("half a second into a take recorded from "
                                    "frame %1 the cursor was at frame %2")
                            .arg(P).arg(during)));
        QCOMPARE(m_window->playbackFrame(), P);
    }

    void take_at_playback_position() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P = sv::sv_frame_t(2.0 * rate);
        m_window->seekTo(P);
        take(1000);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->takePosition(), P);
        QCOMPARE(m_window->recordOverQuestions(), 0);

        auto wave = takeAudio();
        QVERIFY(wave);
        QCOMPARE(wave->getStartFrame(), sv::sv_frame_t(0));
        QVERIFY2(wave->getFrameCount() > P + sv::sv_frame_t(0.7 * rate) &&
                 wave->getFrameCount() < P + sv::sv_frame_t(1.6 * rate),
                 qPrintable(QString("the take's audio is %1 frames long; a "
                                    "second recorded at frame %2 should make "
                                    "it about %3")
                            .arg(wave->getFrameCount()).arg(P)
                            .arg(P + sv::sv_frame_t(rate))));

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, P);
        QCOMPARE(ranges[0].end, wave->getFrameCount());

        // The file holds the singing where it was sung and nothing before
        const sv::sv_frame_t margin = sv::sv_frame_t(0.1 * rate);
        QVERIFY2(takeAudioRms(0, P - margin) < 0.001,
                 "the take's audio is not silent before the position it was "
                 "recorded at");
        QVERIFY(takeAudioRms(P + margin, ranges[0].end - margin) > 0.05);

        // and so does its pitch track
        auto events = pitchEvents(m_window->analyser2());
        QVERIFY(!events.empty());
        QVERIFY2(std::llabs(events.front().getFrame() - P) < margin,
                 qPrintable(QString("the take's pitch track starts at frame "
                                    "%1; it was recorded from frame %2")
                            .arg(events.front().getFrame()).arg(P)));
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(events), highHz)) < 10.0);

        // and the reference is where it was
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           lowHz)) < 10.0);
    }

    // The latency is taken off the front of the recording as it is
    // spliced in, rather than carried by the model's start frame. The
    // device reports a round trip of K frames and delivers a singer who
    // is exactly that late, singing a low note and then a high one; the
    // step between them must land 0.75 s after the take's position.
    void take_latency_removed_by_splice() {
        const int K = 3 * 4096;
        FakeAudioIO::Config config;
        config.playbackLatency = 2 * 4096;
        config.recordLatency = 4096;
        config.input = melody(0.75);
        config.inputDelay = K;
        config.inputFollowsPlayback = true;
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(tone(lowHz, 5.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P = sv::sv_frame_t(1.5 * rate);
        m_window->seekTo(P);
        take(2200);
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_window->fake()->getPlayStartFrame() >= 0,
                 "the reference was never played");
        QVERIFY(m_window->recordingLatencyFrames() >= K);

        auto wave = takeAudio();
        QVERIFY(wave);
        QVERIFY2(wave->getStartFrame() == sv::sv_frame_t(0),
                 "the take's audio is shifted for the latency instead of "
                 "being spliced with it taken off");

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, P);

        auto events = pitchEvents(m_window->analyser2());
        sv::sv_frame_t step = stepFrame(events);
        sv::sv_frame_t want = P + sv::sv_frame_t(0.75 * rate);
        QVERIFY2(step > 0, "the take never reached the second note");
        sv::sv_frame_t error = step - want;
        QVERIFY2(std::llabs(error) <= 4 * hop,
                 qPrintable(QString("the sung step is at frame %1, %2 frames "
                                    "(%3 ms) from where it was sung (%4); the "
                                    "round trip is %5 frames and the "
                                    "application compensated %6")
                            .arg(step).arg(error)
                            .arg(1000.0 * double(error) / rate, 0, 'f', 1)
                            .arg(want).arg(K)
                            .arg(m_window->recordingLatencyFrames())));

        QVERIFY(!events.empty());
        QVERIFY2(events.front().getFrame() >= P - 4 * hop,
                 qPrintable(QString("the take's pitch track starts at frame "
                                    "%1, before the position %2 it was "
                                    "recorded from")
                            .arg(events.front().getFrame()).arg(P)));
    }

    // Recording into the middle of a take replaces what is there from
    // that point on and leaves the rest. The singer sings a low note and
    // then a high one; recording the melody again from inside the high
    // note leaves the high note only where the second recording did not
    // reach.
    void take_over_existing_replaces_it() {
        FakeAudioIO::Config config;
        config.input = melody(0.75);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(1600);
        if (QTest::currentTestFailed()) return;
        sv::sv_frame_t step = stepFrame(pitchEvents(m_window->analyser2()));
        QVERIFY2(step > 0 && std::llabs(step - sv::sv_frame_t(0.75 * rate)) <
                 sv::sv_frame_t(0.1 * rate),
                 qPrintable(QString("the first recording's step to the high "
                                    "note is at frame %1, not about %2")
                            .arg(step).arg(sv::sv_frame_t(0.75 * rate))));
        QString before = m_window->takes()->getAudioPath();
        sv::sv_frame_t firstEnd =
            m_window->takes()->getCoverage().getEndFrame();

        const sv::sv_frame_t P = sv::sv_frame_t(1.1 * rate);
        m_window->seekTo(P);
        m_window->clearRecordOverQuestions();
        take(700);
        if (QTest::currentTestFailed()) return;

        // The playhead was inside the singing, so the user was asked
        QCOMPARE(m_window->recordOverQuestions(), 1);

        // One range still, and it reaches further than the first take did
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, sv::sv_frame_t(0));
        QVERIFY(ranges[0].end >= P + sv::sv_frame_t(0.6 * rate));
        QVERIFY(ranges[0].end > firstEnd);

        // A new file, with the one before kept for undo
        QVERIFY(m_window->takes()->getAudioPath() != before);
        QCOMPARE(m_window->takes()->getSupersededPaths(),
                 QStringList { before });
        QVERIFY2(QFileInfo::exists(before),
                 "the audio file the take had before was not kept");

        auto events = pitchEvents(m_window->analyser2());
        double mid = std::sqrt(lowHz * highHz);

        auto opening = eventsBetween(events, sv::sv_frame_t(0.1 * rate),
                                     sv::sv_frame_t(0.6 * rate));
        QVERIFY(!opening.empty());
        QVERIFY2(medianHz(opening) < mid,
                 "the low note the first recording opened with is gone");

        auto kept = eventsBetween(events, sv::sv_frame_t(0.85 * rate),
                                  sv::sv_frame_t(1.05 * rate));
        QVERIFY(!kept.empty());
        QVERIFY2(medianHz(kept) > mid,
                 "the high note is gone from before the second recording, "
                 "which should not have touched it");

        auto replaced = eventsBetween(events, P + sv::sv_frame_t(0.1 * rate),
                                      P + sv::sv_frame_t(0.6 * rate));
        QVERIFY(!replaced.empty());
        QVERIFY2(medianHz(replaced) < mid,
                 "the high note is still there where the second recording "
                 "sang a low one over it");
    }

    // A recording in a gap leaves the gap a gap: the file grows to hold
    // it, with silence in between
    void take_in_a_gap_grows_the_file() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 5.0)));
        if (QTest::currentTestFailed()) return;

        take(800);
        if (QTest::currentTestFailed()) return;
        sv::sv_frame_t firstEnd =
            m_window->takes()->getCoverage().getEndFrame();
        QVERIFY(firstEnd > sv::sv_frame_t(0.6 * rate));

        const sv::sv_frame_t P = sv::sv_frame_t(2.5 * rate);
        m_window->seekTo(P);
        m_window->clearRecordOverQuestions();
        take(800);
        if (QTest::currentTestFailed()) return;

        // Recording in a gap takes nothing away, so nothing is asked
        QCOMPARE(m_window->recordOverQuestions(), 0);

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 2);
        QCOMPARE(ranges[0], Coverage::Range(0, firstEnd));
        QCOMPARE(ranges[1].start, P);

        auto wave = takeAudio();
        QVERIFY(wave);
        QCOMPARE(wave->getFrameCount(), ranges[1].end);

        const sv::sv_frame_t margin = sv::sv_frame_t(0.1 * rate);
        QVERIFY(takeAudioRms(margin, firstEnd - margin) > 0.05);
        QVERIFY2(takeAudioRms(firstEnd + margin, P - margin) < 0.001,
                 "the gap between the two recordings is not silent");
        QVERIFY(takeAudioRms(P + margin, ranges[1].end - margin) > 0.05);

        auto events = pitchEvents(m_window->analyser2());
        QVERIFY(!eventsBetween(events, 0, firstEnd).empty());
        QVERIFY(!eventsBetween(events, P, ranges[1].end).empty());
        QVERIFY2(eventsBetween(events, firstEnd + margin, P - margin).empty(),
                 "the pitch track has something in the gap between the two "
                 "recordings");
    }

    // A device at 48 kHz, as phones are, against a reference that is a
    // 44.1 kHz model: the recording is made at the device's rate and the
    // take is on the reference's timeline. Two recordings, the second in
    // the gap after the first, must each land where and be as long as
    // they were sung. The same at 44.1 kHz, where nothing is converted
    void takes_placed_from_a_device_at_44100() {
        verifyTakesPlacedFromDeviceAt(44100);
    }

    void takes_placed_from_a_device_at_48000() {
        verifyTakesPlacedFromDeviceAt(otherDeviceRate);
    }

    // The singer of take_latency_removed_by_splice, exactly on time, on a
    // device at 48 kHz: the round trip and the start gap are counted in
    // the device's frames, the reference in its own, and the step the
    // singer sings has to land where the reference steps all the same,
    // in the live dots and in the pitch track
    void latency_with_a_device_at_48000() {
        const int K = 3 * 4096;
        FakeAudioIO::Config config;
        config.sampleRate = otherDeviceRate;
        config.playbackLatency = 2 * 4096;
        config.recordLatency = 4096;
        config.input = twoTonesAt(lowHz, 0.75, highHz, 0.75,
                                  otherDeviceRate);
        config.inputDelay = K;
        config.inputFollowsPlayback = true;
        // The play source hands the device the reference in blocks counted
        // at the reference's rate, and a block of the device's is 8% more
        // of them at 48 kHz: big enough a block for that to show in the
        // start gap
        config.blockSize = 2048;
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);

        // The reference steps 0.75 s after the take's position, as the
        // singer does 0.75 s after hearing it there
        const sv::sv_frame_t P = sv::sv_frame_t(1.5 * rate);
        openReference(writeWav(twoNotes(1.5 + 0.75, 0.75)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(P);
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(2200);
        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
            (m_window->realtimeModelId());
        QVERIFY(model);
        // The dots are on the reference's timeline, and at its rate
        QCOMPARE(model->getSampleRate(), sv::sv_samplerate_t(rate));
        auto dots = model->getAllEvents();
        stopTake();
        if (QTest::currentTestFailed()) return;

        QVERIFY2(m_window->fake()->getPlayStartFrame() >= 0,
                 "the reference was never played");

        // The compensation is in the device's frames: the round trip it
        // reports, and the start gap it knows the real length of. Not to
        // within a few frames, as at 44.1 kHz: bqaudioio's ResamplerWrapper,
        // which brings the reference to the device's rate, holds it back by
        // some tens of frames of its own that nothing reports (about 25 by
        // the first audible sample, half a millisecond)
        sv::sv_frame_t gap = m_window->fake()->getFramesBeforePlayStart();
        sv::sv_frame_t assumedGap = m_window->recordingLatencyFrames() - K;
        QVERIFY2(std::llabs(assumedGap - gap) <= 64,
                 qPrintable(QString("the application took the start gap to "
                                    "be %1 frames; it was %2")
                            .arg(assumedGap).arg(gap)));

        sv::sv_frame_t refStep = stepFrame(pitchEvents(m_window->analyser()));
        sv::sv_frame_t dotStep = stepFrame(dots);
        sv::sv_frame_t sungStep =
            stepFrame(pitchEvents(m_window->analyser2()));
        QVERIFY(refStep > P);
        QVERIFY2(dotStep > 0, "the live dots never reached the second note");
        QVERIFY2(sungStep > 0, "the take never reached the second note");

        // The dots are coarser than pYIN: allow them a YIN window
        QVERIFY2(std::llabs(dotStep - refStep) <= 2048,
                 qPrintable(QString("live step at %1, reference step at %2: "
                                    "%3 frames apart")
                            .arg(dotStep).arg(refStep)
                            .arg(dotStep - refStep)));
        QVERIFY2(std::llabs(sungStep - refStep) <= 2 * hop,
                 qPrintable(QString("sung step at %1, reference step at %2: "
                                    "%3 frames (%4 ms) apart")
                            .arg(sungStep).arg(refStep)
                            .arg(sungStep - refStep)
                            .arg(1000.0 * double(sungStep - refStep) / rate,
                                 0, 'f', 1)));

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, P);
        auto events = pitchEvents(m_window->analyser2());
        QVERIFY(!events.empty());
        QVERIFY(events.front().getFrame() >= P - 4 * hop);
    }

    // Record into Selection with a pre-roll, on a device at 48 kHz: the
    // lead-in and the selection are on the reference's timeline and the
    // frames the device delivers are at its own rate. The take has to
    // wait for the whole of the selection before it stops itself, and
    // keep exactly the selection
    void preroll_and_punch_out_with_a_device_at_48000() {
        const double leadIn = 0.5;
        FakeAudioIO::Config config;
        config.sampleRate = otherDeviceRate;
        // High during the lead-in, low for a little longer than the
        // selection, then high again: only the low note belongs in the take
        config.input = twoTonesAt(highHz, leadIn, lowHz, 0.85,
                                  otherDeviceRate);
        auto after = toneAt(highHz, 0.8, otherDeviceRate);
        config.input.insert(config.input.end(), after.begin(), after.end());
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(false);
        m_window->setRecordIntoSelection(true);
        setPreRollSeconds(leadIn);
        m_window->setPreRoll(true);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P = sv::sv_frame_t(1.0 * rate);
        const sv::sv_frame_t E = sv::sv_frame_t(1.8 * rate);
        m_window->selectRange(P, E);
        m_window->seekTo(P);
        startTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->takePosition(), P);
        QCOMPARE(m_window->takeEnd(), E);
        QCOMPARE(m_window->takePreRoll(), sv::sv_frame_t(leadIn * rate));
        QString path;
        {
            auto recording = sv::ModelById::getAs<sv::WritableWaveFileModel>
                (m_window->currentRecordingModelId());
            QVERIFY(recording);
            path = recording->getLocation();
        }

        // Once the lead-in is over, the cursor has run through it from
        // its start at the reference's pace, not at the device's faster
        // one. The record target's duration is the GUI thread's, as the
        // cursor's is, so the two are read at the same point
        QTRY_VERIFY_WITH_TIMEOUT(m_window->recordTarget()->getRecordDuration()
                                 > sv::sv_frame_t(leadIn * otherDeviceRate),
                                 5000);
        QVERIFY(m_window->recordTarget()->isRecording());
        sv::sv_frame_t during = m_window->playbackFrame();
        sv::sv_frame_t recordedSoFar =
            m_window->recordTarget()->getRecordDuration();
        sv::sv_frame_t cursorWant = P - m_window->takePreRoll() +
            sv::sv_frame_t(std::llround(double(recordedSoFar) * rate /
                                        otherDeviceRate));
        QVERIFY2(std::llabs(during - cursorWant) <= 1,
                 qPrintable(QString("%1 frames at %2 Hz into a take whose "
                                    "lead-in starts at frame %3, the cursor "
                                    "is at %4, not %5")
                            .arg(recordedSoFar).arg(otherDeviceRate)
                            .arg(P - m_window->takePreRoll())
                            .arg(during).arg(cursorWant)));
        QVERIFY(during >= P);

        QTRY_VERIFY_WITH_TIMEOUT(!m_window->recordTarget()->isRecording(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        // It stopped once the lead-in, the selection and most of the
        // margin had been recorded, counted at the device's rate
        sv::WavFileReader reader { sv::FileSource(path) };
        QVERIFY(reader.isOK());
        sv::sv_frame_t needed = sv::sv_frame_t
            ((leadIn + double(E - P) / rate +
              0.8 * TakeTiming::autoStopMarginSeconds()) * otherDeviceRate);
        QVERIFY2(reader.getFrameCount() >= needed,
                 qPrintable(QString("the take stopped itself after %1 frames "
                                    "at %2 Hz; the lead-in, the selection "
                                    "and the margin are %3")
                            .arg(reader.getFrameCount()).arg(otherDeviceRate)
                            .arg(needed)));

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0], Coverage::Range(P, E));

        auto wave = takeAudio();
        QVERIFY(wave);
        QTRY_VERIFY(wave->isReady());
        QCOMPARE(wave->getFrameCount(), E);
        sv::sv_frame_t first = -1, last = -1;
        soundInTake(0, E + sv::sv_frame_t(0.1 * rate), first, last);
        const sv::sv_frame_t close = sv::sv_frame_t(0.01 * rate);
        QVERIFY2(std::llabs(first - P) <= close &&
                 std::llabs(last - E) <= close,
                 qPrintable(QString("the take's audio is loud over [%1,%2]; "
                                    "the selection is [%3,%4)")
                            .arg(first).arg(last).arg(P).arg(E)));

        // What it kept is the note sung inside the selection, not the one
        // of the lead-in or the one after its end
        auto events = pitchEvents(m_window->analyser2());
        QVERIFY(!events.empty());
        QVERIFY2(std::fabs(TestSignals::centsBetween
                           (medianHz(events), lowHz)) < 10.0,
                 qPrintable(QString("the take's median pitch is %1 Hz; the "
                                    "selection was sung at %2, the lead-in "
                                    "and what came after at %3")
                            .arg(medianHz(events)).arg(lowHz).arg(highHz)));
    }

    // Stop no longer analyses the whole of the take's audio: the new
    // audio goes under the pitch and notes layers that are there and only
    // the range the recording went into is analysed and merged into them.
    // So a second take elsewhere leaves the first recording's events
    // exactly as they were, in the very same layers and models.
    void take_analyses_only_the_new_range() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 5.0)));
        if (QTest::currentTestFailed()) return;

        take(900);
        if (QTest::currentTestFailed()) return;

        Analyser *a2 = m_window->analyser2();
        QVERIFY(a2);
        sv::Layer *pitch = a2->getLayer(Analyser::PitchTrack);
        sv::Layer *notes = a2->getLayer(Analyser::Notes);
        QVERIFY(pitch && notes);
        sv::ModelId pitchModel = pitch->getModel();
        sv::ModelId notesModel = notes->getModel();
        sv::ModelId audio = a2->getMainModelId();
        auto before = pitchEvents(pitch);
        auto notesBefore = noteEvents(notes);
        QVERIFY(before.size() > 50);
        QVERIFY(!notesBefore.empty());

        // Two seconds on: further than the half second of context the
        // analysis of the second recording takes for itself
        const sv::sv_frame_t P = sv::sv_frame_t(2.5 * rate);
        m_window->seekTo(P);
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(700);

        // Stop splices the recording in and asks for the analysis of the
        // range it went into.  The range is read here, while that run is
        // held: it is remembered only until the merge
        m_window->holdRangedMerges(true);
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY(m_window->analysingRange());
        QCOMPARE(m_window->analysedRangeStart(), P);
        m_window->holdRangedMerges(false);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        // The analysis of the take was not thrown away and run again: the
        // layers, and the models under them, are the same objects
        Analyser *after = m_window->analyser2();
        QVERIFY(after);
        QCOMPARE(after->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(after->getLayer(Analyser::Notes), notes);
        QCOMPARE(pitch->getModel(), pitchModel);
        QCOMPARE(notes->getModel(), notesModel);
        QVERIFY2(after->getMainModelId() != audio,
                 "the take's audio is not the file the splice wrote");
        QVERIFY2(!sv::ModelById::get(audio),
                 "the audio the take had before was not released");

        // and every event before the second recording is the very same
        // event, frame and value: nothing there was analysed again
        auto keptPitch = eventsBetween(pitchEvents(pitch), 0, P);
        auto wasPitch = eventsBetween(before, 0, P);
        QCOMPARE(keptPitch.size(), wasPitch.size());
        for (size_t i = 0; i < wasPitch.size(); ++i) {
            QCOMPARE(keptPitch[i].getFrame(), wasPitch[i].getFrame());
            QCOMPARE(keptPitch[i].getValue(), wasPitch[i].getValue());
        }

        auto keptNotes = eventsBetween(noteEvents(notes), 0, P);
        auto wasNotes = eventsBetween(notesBefore, 0, P);
        QCOMPARE(keptNotes.size(), wasNotes.size());
        for (size_t i = 0; i < wasNotes.size(); ++i) {
            QCOMPARE(keptNotes[i].getFrame(), wasNotes[i].getFrame());
            QCOMPARE(keptNotes[i].getDuration(), wasNotes[i].getDuration());
            QCOMPARE(keptNotes[i].getValue(), wasNotes[i].getValue());
        }

        // The second recording was analysed, and in the right place
        QVERIFY(!eventsBetween(pitchEvents(pitch), P,
                               P + sv::sv_frame_t(0.6 * rate)).empty());
        verifyPlaySourceClean();
    }

    // Two punch-ins that meet at J inside a note held through both. The
    // first's note ends at J, where its recording stopped; the analysis
    // of the second starts half a second before J, inside the note, so
    // the note it finds begins before the window the merge replaces. It
    // is the same note going on, and the take has one note through J.
    // Undoing the second punch-in gives the first's note back exactly
    void join_inside_a_held_note_keeps_one_note() {
        FakeAudioIO::Config config;
        config.input = tone(lowHz, 3.0);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(false);
        m_window->setRecordIntoSelection(true);
        openReference(writeWav(tone(highHz, 3.5)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P = sv::sv_frame_t(0.5 * rate);
        const sv::sv_frame_t J = sv::sv_frame_t(1.5 * rate);
        const sv::sv_frame_t E = sv::sv_frame_t(2.5 * rate);

        // Record into Selection, so that each punch-in stops exactly at
        // the end of its selection and the two meet at J
        auto punchIn = [this](sv::sv_frame_t from, sv::sv_frame_t to) {
            m_window->clearSelections();
            m_window->selectRange(from, to);
            m_window->seekTo(from);
            startTake();
            if (QTest::currentTestFailed()) return;
            QTRY_VERIFY_WITH_TIMEOUT
                (!m_window->recordTarget()->isRecording(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        };
        auto describe = [](const sv::EventVector &notes) {
            QStringList out;
            for (const auto &e : notes) {
                out << QString("%1 to %2 s at %3 Hz")
                    .arg(double(e.getFrame()) / rate, 0, 'f', 3)
                    .arg(double(e.getFrame() + e.getDuration()) / rate,
                         0, 'f', 3)
                    .arg(double(e.getValue()), 0, 'f', 1);
            }
            return "[" + out.join(", ") + "]";
        };

        punchIn(P, J);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot first = snapshotTake();
        QCOMPARE(int(first.coverage.size()), 1);
        QCOMPARE(first.coverage[0], Coverage::Range(P, J));

        // The first punch-in's note runs to the join
        QVERIFY2(first.notes.size() == 1, qPrintable(describe(first.notes)));
        const sv::Event held = first.notes[0];
        QVERIFY2(std::llabs(held.getFrame() - P) <= 4 * hop &&
                 std::llabs(held.getFrame() + held.getDuration() - J)
                 <= 4 * hop, qPrintable(describe(first.notes)));

        punchIn(J, E);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot second = snapshotTake();
        QCOMPARE(int(second.coverage.size()), 1);
        QCOMPARE(second.coverage[0], Coverage::Range(P, E));

        // One note, the first's carried on through J to the end of the
        // second punch-in: its onset, pitch and label as they were
        QVERIFY2(second.notes.size() == 1 &&
                 second.notes[0].getFrame() == held.getFrame() &&
                 second.notes[0].getValue() == held.getValue() &&
                 second.notes[0].getLabel() == held.getLabel() &&
                 std::llabs(second.notes[0].getFrame() +
                            second.notes[0].getDuration() - E) <= 4 * hop,
                 qPrintable(QString("the notes after the second punch-in "
                                    "are %1; the first's was %2, the join "
                                    "is at %3 s")
                            .arg(describe(second.notes))
                            .arg(describe(first.notes))
                            .arg(double(J) / rate, 0, 'f', 3)));

        // Undo gives the first punch-in's note back, exactly as it was,
        // and redo the one note again
        QCOMPARE(undoOnce(), QString("Record Singing"));
        verifyTakeMatches(first);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(redoOnce(), QString("Record Singing"));
        verifyTakeMatches(second);
    }

    // Recording again while the analysis of the range just recorded is
    // still running. That analysis is lost -- the swap releases the models
    // it was to be merged into -- so the analysis that follows has to
    // cover both ranges, or the first recording would have no pitch track
    // at all (the first recording of a take starts from empty models).
    void take_analysis_covers_the_range_it_lost() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 5.0)));
        if (QTest::currentTestFailed()) return;

        // The merges are held until both takes have stopped: pYIN may
        // analyse the first range before its Stop returns, and then there
        // is nothing left to lose
        m_window->holdRangedMerges(true);

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(900);

        // Stop splices the recording in and starts the analysis of the
        // range it went into there and then
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY2(m_window->analysingRange(),
                 "the race was not set up: no range was being analysed when "
                 "the second take started");
        QCOMPARE(m_window->analysedRangeStart(), sv::sv_frame_t(0));
        sv::sv_frame_t firstEnd = m_window->analysedRangeEnd();
        QVERIFY(firstEnd > sv::sv_frame_t(0.7 * rate));

        // A second take in a gap, recorded without letting the event
        // loop run, as it was before the merges could be held. (The
        // device records from a thread of its own, and the record
        // target's ring buffer holds ten seconds.)
        const sv::sv_frame_t P = sv::sv_frame_t(3.0 * rate);
        m_window->seekTo(P);
        startTake();
        if (QTest::currentTestFailed()) return;
        QThread::msleep(250);
        QVERIFY2(m_window->analysingRange(),
                 "the first range's analysis finished before the second take "
                 "stopped");
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());

        // The analysis now running covers both recordings
        QVERIFY(m_window->analysingRange());
        QCOMPARE(m_window->analysedRangeStart(), sv::sv_frame_t(0));
        QVERIFY(m_window->analysedRangeEnd() > P);

        m_window->holdRangedMerges(false);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        auto events = pitchEvents(m_window->analyser2());
        QVERIFY2(!eventsBetween(events, 0, firstEnd).empty(),
                 "the first recording was left without a pitch track when "
                 "the second one interrupted its analysis");
        QVERIFY(!eventsBetween(events, P,
                               P + sv::sv_frame_t(0.2 * rate)).empty());
        verifyPlaySourceClean();
    }

    // The models the analysis of a recorded range is to be merged into,
    // torn down while it is still running: by another singing track, and
    // by the session going. A regression guard for the area this fork has
    // crashed in before -- a crash is the failure.
    void range_analysis_torn_down_while_running() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 3.0)));
        if (QTest::currentTestFailed()) return;

        // Held, so that each merge is still to come when its models go,
        // however quickly pYIN analyses the range. Never let go: it is
        // torn down each time
        m_window->holdRangedMerges(true);

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(900);
        m_window->doRecord();
        QVERIFY2(m_window->analysingRange(),
                 "the race was not set up: nothing was being analysed after "
                 "Stop");

        // Another singing track over it
        m_window->loadSingingTrack(writeWav(tone(lowHz, 1.0)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        QVERIFY(!m_window->analysingRange());
        verifyPlaySourceClean();
        if (QTest::currentTestFailed()) return;

        // and the same again, with the session closed under it
        m_window->seekTo(0);
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(700);
        m_window->doRecord();
        QVERIFY(m_window->analysingRange());

        m_window->doCloseSession();
        QVERIFY(!m_window->analyser2());
        QVERIFY(!m_window->takes()->haveTake());
        QCOMPARE(m_window->paneStack()->getPaneCount(), 0);
        QCOMPARE(m_window->paneStack()->getHiddenPaneCount(), 0);
        QTest::qWait(300);  // anything still queued arrives here

        // and the window still works
        openReference(writeWav(tone(highHz, 1.0)));
    }

    // The question asked before recording over singing that is there, and
    // what the answer does. The dialog itself is not shown here:
    // TestMainWindow answers it (the real one has "Don't ask again").
    void record_over_existing_question() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        // There was nothing to record over
        QCOMPARE(m_window->recordOverQuestions(), 0);
        sv::sv_frame_t end = m_window->takes()->getCoverage().getEndFrame();

        // In a gap after it: nothing asked
        m_window->seekTo(end + sv::sv_frame_t(1.0 * rate));
        take(400);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->recordOverQuestions(), 0);

        // Inside it, answered no: no recording, and nothing changed
        QString audio = m_window->takes()->getAudioPath();
        m_window->setRecordOverAnswer(false);
        m_window->seekTo(sv::sv_frame_t(0.2 * rate));
        m_window->doRecord();
        QCOMPARE(m_window->recordOverQuestions(), 1);
        QVERIFY2(!m_window->recordTarget()->isRecording(),
                 "the recording started although the question was answered "
                 "with no");
        QVERIFY(!m_window->recordingAsSingingTrack());
        QVERIFY(!m_window->realtimeLayer());
        QCOMPARE(m_window->takes()->getAudioPath(), audio);

        // Answered yes: it records
        m_window->setRecordOverAnswer(true);
        take(400);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->recordOverQuestions(), 2);
        QVERIFY(m_window->takes()->getAudioPath() != audio);

        // "Don't ask again", and it is not asked
        SingingTakes::setOverwriteConfirmationWanted(false);
        m_window->seekTo(sv::sv_frame_t(0.2 * rate));
        take(400);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->recordOverQuestions(), 2);
    }

    // Pre-roll: the reference is played from before the take's position
    // and the singer comes in with it, but nothing sung during that
    // lead-in goes into the take. The singer here sings a low note
    // through the lead-in and a high one after it: the take is to hold
    // the high note only, and to be the lead-in shorter than what the
    // device recorded.
    void preroll_lead_is_not_recorded() {
        const double leadIn = 0.5;
        FakeAudioIO::Config config;
        config.input = twoNotes(0.6, 1.0);
        makeWindow(config);
        // Nothing to hear, so the round trip is zero and the lead-in is
        // the whole of what the splice skips (spec 5.1)
        m_window->setPlayReferenceWhileRecording(false);
        setPreRollSeconds(leadIn);
        m_window->setPreRoll(true);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P = sv::sv_frame_t(2.0 * rate);
        const sv::sv_frame_t R = sv::sv_frame_t(leadIn * rate);
        m_window->seekTo(P);
        take(1300);
        if (QTest::currentTestFailed()) return;

        QCOMPARE(m_window->takePosition(), P);
        QCOMPARE(m_window->takePreRoll(), R);
        QCOMPARE(m_window->recordOverQuestions(), 0);

        // The take starts where it was told to, whatever came before
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, P);

        // and holds the lead-in less than the 1.3 s that was recorded
        sv::sv_frame_t length = ranges[0].length();
        QVERIFY2(length > sv::sv_frame_t(0.45 * rate) &&
                 length < sv::sv_frame_t(0.95 * rate),
                 qPrintable(QString("1.3 s was recorded with a lead-in of "
                                    "%1 frames, and %2 frames of it went "
                                    "into the take")
                            .arg(R).arg(length)));

        const sv::sv_frame_t margin = sv::sv_frame_t(0.1 * rate);
        QVERIFY2(takeAudioRms(0, P - margin) < 0.001,
                 "the take's audio is not silent before the position it was "
                 "recorded at");

        // The note sung during the lead-in is not in the take: what is at
        // its position is the note that came after the lead-in
        auto events = pitchEvents(m_window->analyser2());
        QVERIFY(!events.empty());
        auto sung = eventsBetween(events, P + margin, ranges[0].end - margin);
        QVERIFY(!sung.empty());
        QVERIFY2(std::fabs(TestSignals::centsBetween
                           (medianHz(sung), highHz)) < 10.0,
                 qPrintable(QString("the take's median pitch at its position "
                                    "is %1 Hz; the lead-in was sung at %2 and "
                                    "the take itself at %3")
                            .arg(medianHz(sung)).arg(lowHz).arg(highHz)));
    }

    // What the lead-in looks and sounds like while it runs: the reference
    // is played from S, the cursor runs with it from there, the status bar
    // counts down, and no dot is drawn until the take's position
    void preroll_counts_down_before_the_position() {
        const double leadIn = 1.2;
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        setPreRollSeconds(leadIn);
        m_window->setPreRoll(true);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P = sv::sv_frame_t(2.0 * rate);
        const sv::sv_frame_t R = sv::sv_frame_t(leadIn * rate);
        m_window->seekTo(P);
        startTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->takePreRoll(), R);
        QTest::qWait(400);

        QVERIFY2(m_window->fake()->getPlayStartFrame() >= 0,
                 "the reference was never played");

        sv::sv_frame_t during = m_window->playbackFrame();
        QVERIFY2(during >= P - R && during < P,
                 qPrintable(QString("0.4 s into a take at frame %1 with a "
                                    "lead-in of %2 frames, the cursor is at "
                                    "frame %3; it should be running through "
                                    "the lead-in")
                            .arg(P).arg(R).arg(during)));

        QVERIFY2(m_window->statusText().startsWith("Recording in "),
                 qPrintable(QString("the status bar says \"%1\" during the "
                                    "lead-in, rather than counting down")
                            .arg(m_window->statusText())));

        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
            (m_window->realtimeModelId());
        QVERIFY(model);
        QVERIFY2(model->getEventCount() == 0,
                 qPrintable(QString("%1 live dots were drawn during the "
                                    "lead-in, before the take's position")
                            .arg(model->getEventCount())));

        // Past the lead-in the dots come, none of them before the take's
        // position, and there is nothing left to count down
        QTest::qWait(1400);
        QVERIFY(model->getEventCount() > 10);
        for (const auto &e : model->getAllEvents()) {
            QVERIFY2(e.getFrame() >= P,
                     qPrintable(QString("a live dot at frame %1, before the "
                                        "take's position %2")
                                .arg(e.getFrame()).arg(P)));
        }
        QVERIFY(!m_window->statusText().startsWith("Recording in "));

        stopTake();
        if (QTest::currentTestFailed()) return;
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, P);
    }

    // Record into Selection: the selection says where the take starts and
    // where it ends, the take stops by itself there, and nothing sung
    // afterwards is used. Then a second one over the first, which is not
    // asked about, into the selection the playhead is in, and with a
    // pre-roll as well.
    void punch_out_stops_at_the_end_of_the_selection() {
        FakeAudioIO::Config config;
        // Low note for a little longer than the selection, then high: the
        // high note is what must not reach the take
        config.input = twoNotes(0.85, 0.8);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(false);
        m_window->setRecordIntoSelection(true);
        openReference(writeWav(tone(highHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P = sv::sv_frame_t(1.0 * rate);
        const sv::sv_frame_t E = sv::sv_frame_t(1.8 * rate);
        m_window->selectRange(P, E);
        m_window->seekTo(sv::sv_frame_t(0.3 * rate)); // outside the selection
        startTake();
        if (QTest::currentTestFailed()) return;

        // The selection is recorded into whatever the playhead says
        QCOMPARE(m_window->takePosition(), P);
        QCOMPARE(m_window->takeEnd(), E);
        QVERIFY(m_window->takeTimerRunning());

        // Nobody presses Stop: the take ends itself once the singing for
        // the end of the selection has arrived
        QTRY_VERIFY_WITH_TIMEOUT(!m_window->recordTarget()->isRecording(), 5000);
        QVERIFY2(!m_window->takeTimerRunning(),
                 "the timer that watches the take is still running after it");
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        // What it kept is exactly the selection, margin and all discarded
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0], Coverage::Range(P, E));

        auto wave = takeAudio();
        QVERIFY(wave);
        QCOMPARE(wave->getFrameCount(), E);

        // and what it kept is the note sung inside the selection, not the
        // one that came after its end
        auto events = pitchEvents(m_window->analyser2());
        QVERIFY(!events.empty());
        QVERIFY2(std::fabs(TestSignals::centsBetween
                           (medianHz(events), lowHz)) < 10.0,
                 qPrintable(QString("the take's median pitch is %1 Hz; the "
                                    "selection was sung at %2 and what came "
                                    "after its end at %3")
                            .arg(medianHz(events)).arg(lowHz).arg(highHz)));

        // A second punch, over the singing that is now there: the
        // selection is the consent, so nothing is asked (spec 5.1). It is
        // the selection the playhead is in that is recorded into, and the
        // lead-in of the pre-roll is not taken out of it.
        const sv::sv_frame_t P2 = sv::sv_frame_t(1.2 * rate);
        const sv::sv_frame_t E2 = sv::sv_frame_t(1.9 * rate);
        setPreRollSeconds(0.5);
        m_window->setPreRoll(true);
        m_window->clearSelections();
        m_window->selectRange(sv::sv_frame_t(0.2 * rate),
                              sv::sv_frame_t(0.5 * rate));
        m_window->selectRange(P2, E2);
        m_window->seekTo(sv::sv_frame_t(1.5 * rate)); // in the second one
        m_window->clearRecordOverQuestions();
        startTake();
        if (QTest::currentTestFailed()) return;

        QCOMPARE(m_window->recordOverQuestions(), 0);
        QCOMPARE(m_window->takePosition(), P2);
        QCOMPARE(m_window->takeEnd(), E2);
        QCOMPARE(m_window->takePreRoll(), sv::sv_frame_t(0.5 * rate));

        QTRY_VERIFY_WITH_TIMEOUT(!m_window->recordTarget()->isRecording(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        // The two ranges join, and the second one reached its end: had the
        // lead-in been counted as part of what was recorded, the take
        // would have stopped short of it
        ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0], Coverage::Range(P, E2));
    }

    // Stop can still be pressed before the end of the selection; the take
    // is then as short as what was sung, and nothing is left watching it
    void punch_early_stop_is_shorter() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(false);
        m_window->setRecordIntoSelection(true);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        const sv::sv_frame_t P = sv::sv_frame_t(1.0 * rate);
        const sv::sv_frame_t E = sv::sv_frame_t(3.0 * rate);
        m_window->selectRange(P, E);
        m_window->seekTo(P + sv::sv_frame_t(0.5 * rate)); // inside it
        startTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->takePosition(), P);
        QCOMPARE(m_window->takeEnd(), E);

        QTest::qWait(700);
        QVERIFY2(m_window->recordTarget()->isRecording(),
                 "the take stopped by itself well before the end of the "
                 "selection");
        stopTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(!m_window->takeTimerRunning());

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, P);
        QVERIFY2(ranges[0].end > P + sv::sv_frame_t(0.4 * rate) &&
                 ranges[0].end < E,
                 qPrintable(QString("0.7 s was recorded into the selection "
                                    "[%1,%2) before Stop, and the take covers "
                                    "[%3,%4)")
                            .arg(P).arg(E)
                            .arg(ranges[0].start).arg(ranges[0].end)));

        auto wave = takeAudio();
        QVERIFY(wave);
        QCOMPARE(wave->getFrameCount(), ranges[0].end);
    }

    // The take and the live pitch model are both in the play source
    // while the reference plays (review finding 3), and neither is to be
    // heard. This test holds that in place for the whole path, from the
    // input to the device output. It does not by itself show that the
    // take is muted: the play source fills its buffers seconds ahead of
    // the playback position, the take has no audio that far ahead yet,
    // and so a take this short is silent even unmuted. For the mute see
    // take_muted_while_recording and take_silent_in_output_after_reseek.
    //
    // Pure tones here, so that the reference has nothing at the
    // frequency of the input. 26400 samples is a whole number of
    // periods of both.
    void no_self_monitoring() {
        FakeAudioIO::Config config;
        config.input = TestSignals::sine(highHz, rate, int(3 * rate), 0.5);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(TestSignals::sine(lowHz, rate,
                                                 int(3 * rate), 0.5)));
        if (QTest::currentTestFailed()) return;

        take(1500);
        if (QTest::currentTestFailed()) return;

        auto output = m_window->fake()->getCapturedOutput();
        long start = m_window->fake()->getPlayStartFrame();
        QVERIFY2(start >= 0, "the reference was never played");
        size_t from = size_t(start) + size_t(0.5 * rate);
        double reference = amplitudeAt(output, from, 26400, lowHz);
        double input = amplitudeAt(output, from, 26400, highHz);
        QVERIFY2(reference > 0.1,
                 qPrintable(QString("reference amplitude in the output is %1")
                            .arg(reference)));
        QVERIFY2(input < 0.005,
                 qPrintable(QString("the output has the input's frequency "
                                    "in it, at amplitude %1").arg(input)));
    }

    // Review finding 3. The test above finds the take silent in the
    // output, but that much is true even unmuted, because the play
    // source reads ahead of what has been recorded. Nothing of the take
    // is to be audible while it is being recorded, whatever the buffers
    // do: neither the recording, nor the live pitch model, nor the
    // singing that is already there. This test checks the play
    // parameters, and the next one the output.
    void take_muted_while_recording() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        auto takeParams = [this]() {
            return m_window->analyser2()->getLayer(Analyser::Audio)
                ->getPlayParameters();
        };

        // The first recording: there is no singing track yet, only the
        // recording itself and the dots
        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY2(m_window->recordingLayer(),
                 "the recording has no layer to hold it in the document");
        auto recordingParams =
            m_window->recordingLayer()->getPlayParameters();
        QVERIFY(recordingParams);
        QVERIFY2(!recordingParams->isPlayAudible(),
                 "the recording is audible while it is being made");
        QTRY_VERIFY_WITH_TIMEOUT(m_window->realtimeLayer(), 2000);
        auto liveParams = m_window->realtimeLayer()->getPlayParameters();
        QVERIFY(liveParams);
        QVERIFY2(!liveParams->isPlayAudible(),
                 "the live pitch model is audible during the take");

        // The button goes on saying what the user asked for
        QVERIFY(m_window->playSingingAudioAction()->isChecked());

        QTest::qWait(800);
        stopTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY2(!m_window->recordingLayer(),
                 "the recording was still held after it had been spliced in");
        QVERIFY2(takeParams()->isPlayAudible(),
                 "the take was left muted after recording");
        QVERIFY(m_window->playSingingAudioAction()->isChecked());

        // Recording into the take that is now there: its pitch and notes
        // are kept (put out of sight, see the next test), and its audio
        // is kept out of the mix
        auto events = pitchEvents(m_window->analyser2());
        QVERIFY(!events.empty());

        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY2(m_window->analyser2(),
                 "the singing track was torn down for the take");
        QVERIFY2(pitchEvents(m_window->analyser2()).size() == events.size(),
                 "the singing pitch track lost its events during the take");
        QVERIFY2(!takeParams()->isPlayAudible(),
                 "the singing that is there is audible while it is being "
                 "recorded into");

        // Switched off during a take, it stays muted afterwards
        m_window->playSingingAudioAction()->trigger();
        QVERIFY(!m_window->playSingingAudioAction()->isChecked());
        QVERIFY(!takeParams()->isPlayAudible());
        QTest::qWait(800);
        stopTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(!takeParams()->isPlayAudible());
        QVERIFY(!m_window->playSingingAudioAction()->isChecked());

        // The reference was not touched by any of this
        QVERIFY(m_window->analyser()->isAudible(Analyser::Audio));
    }

    // The take's stored pitch track and notes sit over the same part of
    // the pane as what is being sung now, the pitch in the same orange
    // as the live dots. While a take is being recorded they are out of
    // sight, so that the only pitch the singer sees beside the track
    // they are following is the one they are singing now; they come back
    // when the take stops.
    void singing_track_hidden_while_recording() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(800);
        if (QTest::currentTestFailed()) return;

        Analyser *a2 = m_window->analyser2();
        QVERIFY(a2);
        sv::Layer *pitch = a2->getLayer(Analyser::PitchTrack);
        sv::Layer *notes = a2->getLayer(Analyser::Notes);
        sv::Pane *pane = a2->getPane();
        QVERIFY(pitch && notes && pane);
        QVERIFY2(!pitch->isLayerDormant(pane),
                 "the take's pitch track is not shown after the take");
        QVERIFY2(!notes->isLayerDormant(pane),
                 "the take's notes are not shown after the take");
        QVERIFY(!noteEvents(notes).empty());

        m_window->setRecordOverAnswer(true);
        m_window->seekTo(0);

        startTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::Notes), notes);
        QVERIFY2(pitch->isLayerDormant(pane),
                 "the take's own orange pitch track is still shown while it "
                 "is being recorded over");
        QVERIFY2(notes->isLayerDormant(pane),
                 "the take's own notes are still shown while it is being "
                 "recorded over");
        // The live dots are the pitch the singer does see
        QTRY_VERIFY_WITH_TIMEOUT(m_window->realtimeLayer(), 2000);
        QVERIFY(!m_window->realtimeLayer()->isLayerDormant(pane));

        QTest::qWait(800);
        stopTake();
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->analyser2());
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::Notes), notes);
        QVERIFY2(!pitch->isLayerDormant(pane),
                 "the take's pitch track did not come back when the take "
                 "stopped");
        QVERIFY2(!notes->isLayerDormant(pane),
                 "the take's notes did not come back when the take stopped");
    }

    // The recording is held in the pane by a hidden waveform layer
    // (setupRecordingLayer), because the document needs a layer to hold
    // the model. A hidden layer must not be the pane's work model: the
    // pane blocks off everything past that model's end frame with a pale
    // wash and a vertical line, and a recording's end frame crawls along
    // behind the playback cursor as it is written, so the pane would be
    // greyed out from there to the right for the whole take.
    void recording_does_not_grey_out_the_pane() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        sv::Pane *pane = m_window->paneStack()->getPane(0);
        QVERIFY(pane);
        QCOMPARE(pane->getWorkModel(), m_window->mainModelId());

        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->recordingLayer());
        QVERIFY(!m_window->currentRecordingModelId().isNone());
        QVERIFY2(pane->getWorkModel() != m_window->currentRecordingModelId(),
                 "the pane blocks itself off at the end of the recording");
        QCOMPARE(pane->getWorkModel(), m_window->mainModelId());

        QTest::qWait(600);
        stopTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(pane->getWorkModel(), m_window->mainModelId());

        // And again with a take already there, whose own audio layer is
        // in the pane as well
        m_window->setRecordOverAnswer(true);
        m_window->seekTo(0);
        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY2(pane->getWorkModel() != m_window->currentRecordingModelId(),
                 "the pane blocks itself off at the end of the recording");

        // Even with the pin taken off, the hidden layer that holds the
        // recording must not be the one the pane chooses: a layer that
        // is not shown is no part of what the pane is showing
        pane->setWorkModel({});
        QVERIFY2(pane->getWorkModel() != m_window->currentRecordingModelId(),
                 "the model of a hidden layer was chosen as the work model");
        pane->setWorkModel(m_window->mainModelId());

        QTest::qWait(600);
        stopTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(pane->getWorkModel(), m_window->mainModelId());
    }

    // Review finding 3, at the device. The read-ahead that keeps the
    // take out of the output in no_self_monitoring is taken away here:
    // once more has been recorded than the play source buffers, playback
    // is sent back to the start, so that everything the fill thread now
    // reads is audio the take already has, and pitches the live model
    // already has. Only their being muted keeps them out.
    void take_silent_in_output_after_reseek() {
        FakeAudioIO::Config config;
        config.input = TestSignals::sine(highHz, rate, int(8 * rate), 0.5);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(TestSignals::sine(lowHz, rate,
                                                 int(6 * rate), 0.5)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(3600);

        auto wave = sv::ModelById::getAs<sv::WritableWaveFileModel>
            (m_window->currentRecordingModelId());
        QVERIFY2(wave, "there is no take being recorded");
        sv::sv_frame_t recorded = wave->getFrameCount();
        QVERIFY2(recorded > sv::sv_frame_t(3.2 * rate),
                 qPrintable(QString("only %1 frames of the take exist, not "
                                    "enough to outrun the read-ahead")
                            .arg(recorded)));

        size_t reseek = m_window->fake()->getCapturedOutput().size();
        m_window->playSource()->play(0);
        QTest::qWait(1500);
        stopTake();
        if (QTest::currentTestFailed()) return;

        auto output = m_window->fake()->getCapturedOutput();
        size_t from = reseek + size_t(0.3 * rate);
        double reference = amplitudeAt(output, from, 26400, lowHz);
        double input = amplitudeAt(output, from, 26400, highHz);
        QVERIFY2(reference > 0.1,
                 qPrintable(QString("reference amplitude in the output after "
                                    "the reseek is %1").arg(reference)));
        QVERIFY2(input >= 0.0 && input < 0.005,
                 qPrintable(QString("the output has the input's frequency in "
                                    "it, at amplitude %1 (reference %2): the "
                                    "take, or a tone at the live pitch, is "
                                    "played while it is being recorded")
                            .arg(input).arg(reference)));
    }

    // Recording again before the analysis of the take just made has
    // finished. The second take's splice tears that analyser down in the
    // middle of its pYIN, which is the area this fork has crashed in
    // before; cancelAnalyses() is what keeps it safe. A regression guard,
    // not a new behaviour: run it under load, a crash is the failure.
    // The analysis is held, so that it is unmerged when the second take
    // stops however quick the machine; whether its thread is still going
    // then as well depends on the machine and its load.
    void rerecord_during_analysis() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 3.0)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(700);

        // Stop splices the recording in and starts the analysis of the
        // result there and then
        m_window->holdRangedMerges(true);
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY2(sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(),
                 "the race was not set up: no analysis was running when the "
                 "second take started");
        QVERIFY(m_window->analysingRange());
        QString first = m_window->takes()->getAudioPath();
        QVERIFY(!first.isEmpty());

        // In a gap, so nothing is asked
        m_window->seekTo(sv::sv_frame_t(1.5 * rate));
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(500);
        QVERIFY2(m_window->analysingRange(),
                 "the first take's analysis was over before the second one "
                 "stopped");
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        m_window->holdRangedMerges(false);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        QCOMPARE(m_window->recordOverQuestions(), 0);

        QVERIFY(m_window->analyser2());
        QVERIFY(!pitchEvents(m_window->analyser2()).empty());
        QVERIFY(m_window->takes()->getAudioPath() != first);
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 2);
        QCOMPARE(ranges[1].start, sv::sv_frame_t(1.5 * rate));

        QVERIFY(!m_window->recordingLayer());
        QCOMPARE(m_window->pendingExtraPaneCount(), 0);
        QCOMPARE(m_window->paneStack()->getHiddenPaneCount(), 0);
        verifyPlaySourceClean();
    }

    void rerecord_cleans_up() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        int panes = m_window->paneStack()->getPaneCount();

        startTake();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->realtimeLayer(), 2000);
        sv::ModelId firstLive = m_window->realtimeLayer()->getModel();
        QTest::qWait(800);
        stopTake();
        if (QTest::currentTestFailed()) return;
        Analyser *first = m_window->analyser2();
        sv::ModelId firstModel = first->getMainModelId();
        QVERIFY(sv::ModelById::get(firstModel));

        startTake();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->realtimeLayer(), 2000);
        sv::ModelId secondLive = m_window->realtimeLayer()->getModel();
        QVERIFY(secondLive != firstLive);
        QTest::qWait(800);
        stopTake();
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->analyser2());
        sv::ModelId secondModel = m_window->analyser2()->getMainModelId();
        QVERIFY(secondModel != firstModel);
        QVERIFY2(!sv::ModelById::get(firstModel),
                 "the first take's model was not released");
        QCOMPARE(layersOnModel(firstModel), 0);
        QCOMPARE(layersOnModel(secondModel), 1);

        QCOMPARE(m_window->paneStack()->getPaneCount(), panes);
        QVERIFY(m_window->paneStack()->getHiddenPaneCount() <= 1);
        QCOMPARE(m_window->pendingExtraPaneCount(), 0);

        auto events = pitchEvents(m_window->analyser2());
        QVERIFY(events.size() > 50);

        // Nothing of the first take is left in the play source: not
        // its audio and not its live pitch model (review finding 6)
        QTRY_VERIFY_WITH_TIMEOUT(!m_window->realtimeLayer(), 5000);
        auto playing = m_window->playSource()->getModels();
        QVERIFY(!playing.count(firstModel));
        QVERIFY(!playing.count(firstLive));
        QVERIFY(!playing.count(secondLive));
        for (sv::ModelId id : playing) {
            QVERIFY2(sv::ModelById::get(id),
                     qPrintable(QString("the play source holds model %1, "
                                        "which no longer exists")
                                .arg(id.untyped)));
            QVERIFY2(layersOnModel(id) > 0,
                     qPrintable(QString("the play source holds model %1, "
                                        "which no layer uses")
                                .arg(id.untyped)));
        }
        // audio, pitch track and notes, of the reference and of the take
        QCOMPARE(int(playing.size()), 6);
    }

    // With nothing loaded the take is not a singing track: it becomes
    // the main model and the primary analyser gets it
    void record_without_reference() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);

        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(!m_window->recordingAsSingingTrack());
        QTRY_VERIFY_WITH_TIMEOUT(m_window->realtimeLayer(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT
            (!pitchEvents(m_window->realtimeLayer()).empty(), 3000);
        QTest::qWait(800);

        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);

        QVERIFY(!m_window->analyser2());
        QVERIFY(!m_window->mainModelId().isNone());
        QCOMPARE(m_window->analyser()->getMainModelId(),
                 m_window->mainModelId());
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           highHz)) < 10.0);

        QTRY_VERIFY_WITH_TIMEOUT(!m_window->realtimeLayer(), 5000);
        QVERIFY(!m_window->realtimeTracker());
        QVERIFY(!m_window->recordingInProgress());
        QCOMPARE(m_window->pendingExtraPaneCount(), 0);
        verifyPlaySourceClean();
    }

    // The latency of a compensated take must not outlive it. A take
    // with nothing loaded has no reference to line up with: its dots
    // belong where they were heard, the first of them at the centre
    // of the first YIN window
    void standalone_take_after_compensated_take() {
        FakeAudioIO::Config config;
        config.playbackLatency = 4096;
        config.recordLatency = 4096;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        take(800);
        if (QTest::currentTestFailed()) return;
        QVERIFY2(m_window->recordingLatencyFrames() >= 8192,
                 qPrintable(QString("the first take was compensated by %1 "
                                    "frames only; the device reports 8192")
                            .arg(m_window->recordingLatencyFrames())));
        QTRY_VERIFY_WITH_TIMEOUT(!m_window->realtimeLayer(), 5000);
        m_window->doCloseSession();

        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(!m_window->recordingAsSingingTrack());
        QTRY_VERIFY_WITH_TIMEOUT(m_window->realtimeLayer(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT
            (!pitchEvents(m_window->realtimeLayer()).empty(), 3000);
        QTest::qWait(800);

        sv::sv_frame_t latency = m_window->recordingLatencyFrames();
        auto dots = pitchEvents(m_window->realtimeLayer());
        QVERIFY(!dots.empty());
        sv::sv_frame_t firstDot = dots.front().getFrame();

        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);

        QVERIFY2(latency == 0 &&
                 firstDot >= sv::sv_frame_t(RealtimePitchTracker::kWindowSize / 2),
                 qPrintable(QString("the standalone take ran with a latency "
                                    "of %1 frames and its first dot at frame "
                                    "%2; there is nothing to compensate for, "
                                    "and no dot can come before frame %3")
                            .arg(latency).arg(firstDot)
                            .arg(RealtimePitchTracker::kWindowSize / 2)));
    }

    // No device at all: the base class record() gives up quietly
    void record_failure_resets_flags() {
        makeWindow(FakeAudioIO::Config(), false);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());

        QVERIFY(!m_window->recordingAsSingingTrack());
        QVERIFY(!m_window->recordingInProgress());

        // The reference is still there, and Analyse Now is about it.
        // With the flag left set it would be routed to a singing track
        // that does not exist, and the reference left as it was. (Not
        // "the next file opened": opening one closes the session, which
        // clears the flag whatever record() did.)
        QVERIFY(analysed(m_window->analyser()));
        sv::ModelId pitchBefore =
            m_window->analyser()->getLayer(Analyser::PitchTrack)->getModel();
        QSignalSpy relayered(m_window->analyser(), SIGNAL(layersChanged()));

        m_window->doAnalyseNow();

        // The misrouted Analyse Now waits 200 ms for the singing track's
        // analyser before it gives up. Wait that out before failing, so
        // that it does not fire after this test has ended
        if (relayered.isEmpty()) QTest::qWait(300);

        sv::Layer *pitchNow =
            m_window->analyser()->getLayer(Analyser::PitchTrack);
        QVERIFY2(!relayered.isEmpty() &&
                 pitchNow && pitchNow->getModel() != pitchBefore,
                 "Analyse Now after a failed record did not re-analyse "
                 "the reference");
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           lowHz)) < 10.0);
    }

    void analyse_now_during_take() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        sv::Layer *refLayer =
            m_window->analyser()->getLayer(Analyser::PitchTrack);
        sv::ModelId refModel = refLayer->getModel();

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(600);
        m_window->doAnalyseNow();
        QTest::qWait(600);
        QVERIFY(m_window->recordTarget()->isRecording());

        // The take carries on as if nothing had been asked
        QVERIFY(m_window->recordingAsSingingTrack());
        QVERIFY(m_window->recordingInProgress());
        QVERIFY(m_window->realtimeTracker());
        QVERIFY(m_window->realtimeLayer());
        QVERIFY(pitchEvents(m_window->analyser2()).empty());

        m_window->doRecord();
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        sv::Layer *refLayerNow =
            m_window->analyser()->getLayer(Analyser::PitchTrack);
        QVERIFY2(refLayerNow == refLayer && refLayerNow->getModel() == refModel,
                 "the reference pitch track was replaced");
    }

    // Analyse Now takes in the singing of the take as well (spec 7): all
    // of its coverage is analysed again and merged into the pitch track
    // and notes it has, which are not thrown away and made afresh
    void analyse_now_reanalyses_the_take() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        const sv::sv_frame_t P = sv::sv_frame_t(2.0 * rate);
        m_window->seekTo(P);
        take(600);
        if (QTest::currentTestFailed()) return;

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 2);

        Analyser *a2 = m_window->analyser2();
        sv::Layer *pitch = a2->getLayer(Analyser::PitchTrack);
        QVERIFY(pitch);
        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
            (pitch->getModel());
        QVERIFY(model);

        // Emptied by hand, so that what comes back can only have come
        // from the analysis Analyse Now asks for
        for (const auto &e : model->getAllEvents()) model->remove(e);
        QVERIFY(pitchEvents(pitch).empty());

        // Held, so that the range is still there to be read however quick
        // the run: it is remembered only until the merge
        m_window->holdRangedMerges(true);
        m_window->doAnalyseNow();

        // One run over the span of the coverage, not one per range
        QVERIFY(m_window->analysingRange());
        QCOMPARE(m_window->analysedRangeStart(), ranges[0].start);
        QCOMPARE(m_window->analysedRangeEnd(), ranges[1].end);

        m_window->holdRangedMerges(false);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        QCOMPARE(m_window->analyser2()->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(noteLayersInPane0(), 2);

        auto events = pitchEvents(pitch);
        QVERIFY2(!eventsBetween(events, ranges[0].start, ranges[0].end).empty(),
                 "the first range of the coverage was not analysed again");
        QVERIFY2(!eventsBetween(events, ranges[1].start, ranges[1].end).empty(),
                 "the second range of the coverage was not analysed again");
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(events), highHz)) < 10.0);
    }

    void load_singing_track() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        int panes = m_window->paneStack()->getPaneCount();
        sv::Layer *refLayer =
            m_window->analyser()->getLayer(Analyser::PitchTrack);
        sv::ModelId refModel = refLayer->getModel();
        QSignalSpy refSetUp(m_window->analyser(), SIGNAL(layersChanged()));
        QVERIFY(refSetUp.isValid());

        m_window->loadSingingTrack(writeWav(tone(highHz, 1.0)));

        // Set up exactly once: there and then, and not again when the
        // queued calls run
        Analyser *a2 = m_window->analyser2();
        QVERIFY(a2);
        sv::ModelId singing = a2->getMainModelId();
        QVERIFY(m_window->pendingSingingModelId().isNone());
        QCoreApplication::processEvents();
        QCOMPARE(m_window->analyser2(), a2);
        QVERIFY(sv::ModelById::get(singing));
        QCOMPARE(layersOnModel(singing), 1);
        QCOMPARE(m_window->paneStack()->getPaneCount(), panes);

        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        QCOMPARE(m_window->analyser2(), a2);
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(a2)), highHz)) < 10.0);

        // The other half of finding 7: the reference is not handed to
        // its analyser a second time. That would keep its layers, but
        // forget its pitch candidates and leave them in the pane
        QCOMPARE(int(refSetUp.count()), 0);
        QCOMPARE(m_window->analyser()->getLayer(Analyser::PitchTrack),
                 refLayer);
        QCOMPARE(refLayer->getModel(), refModel);

        // The second pass used to end by marking the document unmodified
        QVERIFY(m_window->isDocumentModified());

        // A track loaded whole is a take whose singing is all of it
        QVERIFY(m_window->takes()->haveTake());
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0],
                 Coverage::Range(0, sv::ModelById::getAs<sv::WaveFileModel>
                                 (singing)->getFrameCount()));
    }

    // Loading a singing track over one that is already there.  Since
    // phase 7a each load is a take of its own (spec 5.3): the first
    // track's audio is released, but its pitch and notes are kept as the
    // layers of the take that has been put away
    void reload_singing_track() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        int panes = m_window->paneStack()->getPaneCount();

        m_window->loadSingingTrack(writeWav(tone(highHz, 2.0)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        sv::ModelId first = m_window->analyser2()->getMainModelId();
        sv::ModelId firstPitch = m_window->analyser2()
            ->getLayer(Analyser::PitchTrack)->getModel();

        m_window->loadSingingTrack(writeWav(tone(highHz, 1.0)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        sv::ModelId second = m_window->analyser2()->getMainModelId();
        QVERIFY(second != first);

        // Two takes, and the first one's audio is gone: only the active
        // take has an audio model (spec 6.4)
        QCOMPARE(m_window->takes()->getTakeNames(),
                 QStringList({ "Take 1", "Take 2" }));
        QVERIFY2(!sv::ModelById::get(first),
                 "the first singing track's model was not released");
        QVERIFY2(sv::ModelById::get(firstPitch),
                 "the first take's pitch track went with its audio");
        QCOMPARE(layersOnModel(second), 1);
        QCOMPARE(m_window->paneStack()->getPaneCount(), panes);

        verifyTakeIsPutAway("Take 1");
        if (QTest::currentTestFailed()) return;

        auto playing = m_window->playSource()->getModels();
        QVERIFY(!playing.count(first));
        QVERIFY(!playing.count(firstPitch));
        verifyPlaySourceClean();
        if (QTest::currentTestFailed()) return;
        // audio, pitch track and notes, of the reference and of the take
        // that is on show
        QCOMPARE(int(playing.size()), 6);

        // A stale id used to keep the end of playback where the longest
        // model ever loaded had ended, and the pitch track of the take
        // that has been put away -- of the 2 s file -- would do the same
        QVERIFY(m_window->playSource()->getPlayEndFrame() <
                sv::sv_frame_t(1.2 * rate));
    }

    // Another audio file under the take's pitch and notes layers: the
    // layers, their models and everything in them are the same objects
    // afterwards, and no analysis is run
    void swap_keeps_layers_and_events() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        int panes = m_window->paneStack()->getPaneCount();

        m_window->loadSingingTrack(writeWav(tone(highHz, 1.0)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        Analyser *before = m_window->analyser2();
        sv::Layer *pitch = before->getLayer(Analyser::PitchTrack);
        sv::Layer *notes = before->getLayer(Analyser::Notes);
        sv::ModelId oldAudio = before->getMainModelId();
        sv::ModelId pitchModel = pitch->getModel();
        sv::ModelId notesModel = notes->getModel();
        auto events = pitchEvents(pitch);
        QVERIFY(!events.empty());
        auto coverage = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(coverage.size()), 1);

        // Twice as long as the file it replaces, so that a coverage reset
        // to "the whole of this file" would show
        QString error = m_window->doSwapSingingAudio(writeWav(tone(lowHz, 2.0)));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        Analyser *after = m_window->analyser2();
        QVERIFY(after);
        // Not "after != before": the old analyser has been deleted, and a
        // new one may legitimately be built at the same address (which it
        // is, once another suite has churned the heap first). That the
        // analyser was rebuilt shows in the model it is on, below
        QCOMPARE(after->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(after->getLayer(Analyser::Notes), notes);
        QCOMPARE(pitch->getModel(), pitchModel);
        QCOMPARE(notes->getModel(), notesModel);
        auto kept = pitchEvents(pitch);
        QCOMPARE(kept.size(), events.size());
        for (size_t i = 0; i < events.size(); ++i) {
            QCOMPARE(kept[i].getFrame(), events[i].getFrame());
            QCOMPARE(kept[i].getValue(), events[i].getValue());
        }

        // The new audio is the analyser's, and the old one is released
        sv::ModelId newAudio = after->getMainModelId();
        QVERIFY(newAudio != oldAudio);
        auto wfm = sv::ModelById::getAs<sv::WaveFileModel>(newAudio);
        QVERIFY(wfm);
        // svcore reads a file this small on a thread of its own, and its
        // length grows from 0 until it is ready: nothing here has waited,
        // as no analysis follows the swap (CI's macOS found 0 frames)
        QTRY_VERIFY(wfm->isReady());
        QCOMPARE(wfm->getFrameCount(), sv::sv_frame_t(2.0 * rate));
        QVERIFY2(!sv::ModelById::get(oldAudio),
                 "the audio that was swapped out was not released");
        QCOMPARE(layersOnModel(newAudio), 1);
        QCOMPARE(m_window->paneStack()->getPaneCount(), panes);
        QCOMPARE(m_window->paneStack()->getHiddenPaneCount(), 0);
        auto playing = m_window->playSource()->getModels();
        QVERIFY(!playing.count(oldAudio));
        QVERIFY(playing.count(newAudio));
        verifyPlaySourceClean();
        if (QTest::currentTestFailed()) return;

        // The two layers' models come from the new audio now: that is what
        // let the new analyser claim them
        QCOMPARE(sv::ModelById::get(pitchModel)->getSourceModel(), newAudio);
        QCOMPARE(sv::ModelById::get(notesModel)->getSourceModel(), newAudio);

        // Nothing was analysed, then or when the queued calls ran
        QVERIFY(!sv::ModelTransformerFactory::getInstance()
                ->haveRunningTransformers());
        QCoreApplication::processEvents();
        QCOMPARE(m_window->analyser2(), after);
        QVERIFY(!sv::ModelTransformerFactory::getInstance()
                ->haveRunningTransformers());
        QCOMPARE(pitchEvents(pitch).size(), events.size());

        // Colours, the toggles and the take's coverage as they were
        QCOMPARE(colourOf(pitch), colourNamed("Orange"));
        QCOMPARE(colourOf(notes), colourNamed("Bright Purple"));
        QVERIFY(after->isVisible(Analyser::Audio));
        QVERIFY(after->isVisible(Analyser::PitchTrack));
        QVERIFY(after->isVisible(Analyser::Notes));
        QVERIFY(after->isAudible(Analyser::Audio));
        QVERIFY(m_window->playSingingAudioAction()->isChecked());
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0], coverage[0]);
    }

    // The swapped-in audio is what is heard afterwards, and the audio it
    // replaced is not
    void swap_plays_the_new_audio() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(TestSignals::sine(lowHz, rate,
                                                 int(2 * rate), 0.5)));
        if (QTest::currentTestFailed()) return;

        m_window->loadSingingTrack
            (writeWav(TestSignals::sine(highHz, rate, int(2 * rate), 0.5)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        QString error = m_window->doSwapSingingAudio
            (writeWav(TestSignals::sine(swapHz, rate, int(2 * rate), 0.5)));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        m_window->seekTo(0);
        m_window->doPlay();
        QTest::qWait(1500);
        m_window->doPlay();

        auto output = m_window->fake()->getCapturedOutput();
        long start = m_window->fake()->getPlayStartFrame();
        QVERIFY2(start >= 0, "nothing was played");
        size_t from = size_t(start) + size_t(0.3 * rate);
        double swapped = amplitudeAt(output, from, 22050, swapHz);
        double replaced = amplitudeAt(output, from, 22050, highHz);
        QVERIFY2(swapped > 0.05,
                 qPrintable(QString("the audio swapped in is not in the "
                                    "output: amplitude %1").arg(swapped)));
        QVERIFY2(replaced >= 0.0 && replaced < 0.005,
                 qPrintable(QString("the audio swapped out is still in the "
                                    "output, at amplitude %1 (the new audio "
                                    "is at %2)").arg(replaced).arg(swapped)));
    }

    // Closing the session after a swap: the layers the released analyser
    // used to own are deleted by the analyser that claimed them
    void swap_then_close_session() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        m_window->loadSingingTrack(writeWav(tone(highHz, 1.0)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        sv::ModelId pitchModel = m_window->analyser2()
            ->getLayer(Analyser::PitchTrack)->getModel();

        QString error = m_window->doSwapSingingAudio(writeWav(tone(lowHz, 1.0)));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        sv::ModelId audio = m_window->analyser2()->getMainModelId();

        m_window->doCloseSession();

        QVERIFY(!m_window->analyser2());
        QVERIFY2(!sv::ModelById::get(audio),
                 "the swapped-in audio outlived the session");
        QVERIFY2(!sv::ModelById::get(pitchModel),
                 "the swapped-over pitch track outlived the session");
        QCOMPARE(m_window->paneStack()->getPaneCount(), 0);
        QCOMPARE(m_window->paneStack()->getHiddenPaneCount(), 0);
        QCOMPARE(m_window->pendingExtraPaneCount(), 0);

        // and the window still works
        openReference(writeWav(tone(highHz, 1.0)));
    }

    // Recording again after a swap.  The layers the first analyser
    // released are deleted while the one that claimed them is alive: what
    // the next take does with them is not the released analyser's business
    void swap_then_record_again() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;
        int panes = m_window->paneStack()->getPaneCount();

        take(600);
        if (QTest::currentTestFailed()) return;
        QString first = m_window->takes()->getAudioPath();
        QVERIFY(!first.isEmpty());

        // Standing in for the file a splice will write in phase 4c
        QString error = m_window->doSwapSingingAudio(writeWav(tone(lowHz, 2.0)));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!pitchEvents(m_window->analyser2()).empty());

        m_window->seekTo(0);
        take(600);
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->analyser2());
        QVERIFY(!pitchEvents(m_window->analyser2()).empty());
        QVERIFY(m_window->takes()->getAudioPath() != first);
        QVERIFY(!m_window->recordingLayer());
        QCOMPARE(m_window->paneStack()->getPaneCount(), panes);
        QCOMPARE(m_window->pendingExtraPaneCount(), 0);
        verifyPlaySourceClean();
    }

    // The swap while pYIN is still running on the audio it replaces: the
    // transforms are cancelled before any model changes hands.  A
    // regression guard, a crash is the failure
    void swap_during_analysis() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        m_window->loadSingingTrack(writeWav(tone(highHz, 4.0)));
        QVERIFY2(sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(),
                 "the race was not set up: no analysis was running when the "
                 "swap started");
        Analyser *before = m_window->analyser2();
        QVERIFY(before);
        sv::Layer *pitch = before->getLayer(Analyser::PitchTrack);
        sv::Layer *notes = before->getLayer(Analyser::Notes);
        QVERIFY(pitch && notes);
        sv::ModelId oldAudio = before->getMainModelId();

        QString error = m_window->doSwapSingingAudio(writeWav(tone(lowHz, 1.0)));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // The transform threads are gone by the time the swap returns, so
        // nothing writes into the pitch track any more -- whatever pYIN
        // had got to is what stays.  (The factory only strikes a
        // transformer off its list when the event loop next runs)
        auto events = pitchEvents(pitch);
        QTRY_VERIFY_WITH_TIMEOUT(!sv::ModelTransformerFactory::getInstance()
                                 ->haveRunningTransformers(), 10000);
        QTest::qWait(300);
        QCOMPARE(pitchEvents(pitch).size(), events.size());

        Analyser *after = m_window->analyser2();
        QVERIFY(after);
        // Not "after != before": the old analyser has been deleted, and a
        // new one may legitimately be built at the same address (which it
        // is, once another suite has churned the heap first). That the
        // analyser was rebuilt shows in the model it is on, below
        QCOMPARE(after->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(after->getLayer(Analyser::Notes), notes);
        QVERIFY(after->getMainModelId() != oldAudio);
        QVERIFY2(!sv::ModelById::get(oldAudio),
                 "the audio that was swapped out was not released");
        QCoreApplication::processEvents();
        verifyPlaySourceClean();
    }

    // Finding 7, the scenario itself: pitch candidates on the reference
    // are still the analyser's after another track has been loaded
    void reference_candidates_survive_load() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;
        Analyser *a = m_window->analyser();

        QString error = a->reAnalyseSelection
            (sv::Selection(sv::sv_frame_t(0.5 * rate),
                           sv::sv_frame_t(1.5 * rate)),
             Analyser::FrequencyRange());
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(a->haveHigherPitchCandidate(), 30000);

        m_window->loadSingingTrack(writeWav(tone(highHz, 1.0)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        QVERIFY2(a->haveHigherPitchCandidate(),
                 "the reference analyser forgot its pitch candidates");

        m_window->doLoadBackgroundMusic(writeWav(tone(highHz, 1.0)));
        QCoreApplication::processEvents();
        QVERIFY2(a->haveHigherPitchCandidate(),
                 "the reference analyser forgot its pitch candidates");
    }

    // Bug 1: the analyser was never told of a layer deleted by someone
    // else, and went on listing deleted pitch candidates
    void candidates_deleted_from_outside() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;
        Analyser *a = m_window->analyser();

        QString error = a->reAnalyseSelection
            (sv::Selection(sv::sv_frame_t(0.5 * rate),
                           sv::sv_frame_t(1.5 * rate)),
             Analyser::FrequencyRange());
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(a->haveHigherPitchCandidate(), 30000);

        std::vector<sv::Layer *> candidates;
        sv::Pane *pane = a->getPane();
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            sv::Layer *layer = pane->getLayer(i);
            if (layer->getLayerPresentationName() == "candidate") {
                candidates.push_back(layer);
            }
        }
        QVERIFY(!candidates.empty());

        for (sv::Layer *layer : candidates) {
            m_window->document()->deleteLayer(layer, true);
        }
        QVERIFY2(!a->haveHigherPitchCandidate() &&
                 !a->haveLowerPitchCandidate(),
                 "the analyser still lists deleted pitch candidates");

        // The same for one of its own layers
        sv::Layer *notes = a->getLayer(Analyser::Notes);
        QVERIFY(notes);
        m_window->document()->deleteLayer(notes, true);
        QVERIFY2(!a->getLayer(Analyser::Notes),
                 "the analyser still points at its deleted note layer");
    }

    // Pitch candidates are the analyser's own layers: they come and go
    // with no entry in the undo history, an undo leaves them alone, and
    // the next re-analysis deletes them. As undoable layers they could be
    // undone out of the pane while the analyser went on listing them, and
    // the next re-analysis then made a command of each that a later undo
    // crashed on
    void candidates_make_no_undo_entries() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;
        Analyser *a = m_window->analyser();
        sv::Pane *pane = a->getPane();
        auto candidates = [&]() {
            std::vector<QPointer<sv::Layer>> found;
            for (int i = 0; i < pane->getLayerCount(); ++i) {
                sv::Layer *layer = pane->getLayer(i);
                if (layer->getLayerPresentationName() == "candidate") {
                    found.push_back(layer);
                }
            }
            return found;
        };
        sv::CommandHistory::getInstance()->clear();

        std::vector<QPointer<sv::Layer>> earlier;
        for (double start : { 0.5, 0.8 }) {
            QString error = a->reAnalyseSelection
                (sv::Selection(sv::sv_frame_t(start * rate),
                               sv::sv_frame_t((start + 0.7) * rate)),
                 Analyser::FrequencyRange());
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(a->haveHigherPitchCandidate(), 30000);
            QVERIFY(!candidates().empty());
            for (const QPointer<sv::Layer> &layer : earlier) {
                QVERIFY2(!layer, "a re-analysis left the candidates of the "
                         "one before it alive");
            }
            QString undone = undoOnce();
            QVERIFY2(undone.isEmpty(),
                     qPrintable("the re-analysis left \"" + undone +
                                "\" in the undo history"));
            QVERIFY2(a->haveHigherPitchCandidate() && !candidates().empty(),
                     "an undo took the pitch candidates away");
            earlier = candidates();
        }

        a->clearReAnalysis();
        QVERIFY2(candidates().empty(),
                 "clearing the re-analysis left candidates in the pane");
        for (const QPointer<sv::Layer> &layer : earlier) {
            QVERIFY2(!layer, "clearing the re-analysis left its candidates "
                     "alive");
        }
    }

    void load_background_music() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        int panes = m_window->paneStack()->getPaneCount();
        int rulerPaneLayers = m_window->paneStack()->getPane(1)->getLayerCount();
        QSignalSpy refSetUp(m_window->analyser(), SIGNAL(layersChanged()));
        QVERIFY(refSetUp.isValid());

        m_window->doLoadBackgroundMusic(writeWav(tone(highHz, 1.0)));
        QCoreApplication::processEvents();
        QCOMPARE(int(refSetUp.count()), 0);
        QVERIFY(m_window->isDocumentModified());

        sv::ModelId music = m_window->backgroundMusicModelId();
        QVERIFY(!music.isNone());
        QVERIFY(sv::ModelById::get(music));
        QVERIFY(m_window->backgroundMusicLayer());
        QVERIFY(paneHasLayer(0, m_window->backgroundMusicLayer()));
        QCOMPARE(layersOnModel(music), 1);
        auto params = m_window->backgroundMusicLayer()->getPlayParameters();
        QVERIFY(params && params->isPlayAudible());

        QVERIFY2(!m_window->analyser2(),
                 "the background music was analysed as a singing track");
        QCOMPARE(m_window->paneStack()->getPaneCount(), panes);
        QCOMPARE(m_window->paneStack()->getPane(1)->getLayerCount(),
                 rulerPaneLayers);
    }

    // Only after a session load is the shared ruler put into the extra
    // pane, so only here does pruning that pane touch it. The take
    // afterwards is what used to read the freed ruler.
    void load_singing_track_after_session() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        reopenAsSession();
        if (QTest::currentTestFailed()) return;

        m_window->loadSingingTrack(writeWav(tone(highHz, 1.0)));
        verifyRulerIntact();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        take(600);
        if (QTest::currentTestFailed()) return;
        verifyRulerIntact();
    }

    void load_background_music_after_session() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        reopenAsSession();
        if (QTest::currentTestFailed()) return;

        m_window->doLoadBackgroundMusic(writeWav(tone(highHz, 1.0)));
        verifyRulerIntact();
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->backgroundMusicLayer());

        take(600);
        if (QTest::currentTestFailed()) return;
        verifyRulerIntact();
    }

    void session_round_trip() {
        const int K = 8192;
        FakeAudioIO::Config config;
        config.playbackLatency = 4096;
        config.recordLatency = 4096;
        config.input = tone(highHz, 3.0);
        config.inputDelay = K;
        config.inputFollowsPlayback = true;
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(1200);
        if (QTest::currentTestFailed()) return;
        // the round trip plus the start gap, which varies
        sv::sv_frame_t shift = m_window->recordingLatencyFrames();
        QVERIFY(shift >= K);

        // The compensation is in the take's audio, so what has to survive
        // the round trip is the pitch track, not a start frame
        auto before = pitchEvents(m_window->analyser2());
        QVERIFY(!before.empty());
        sv::sv_frame_t firstEventBefore = before.front().getFrame();
        QCOMPARE(noteLayersInPane0(), 2);

        QString session = m_dir.filePath("round-trip.ton");
        QVERIFY(m_window->saveSessionFile(session));
        m_window->doCloseSession();
        QVERIFY(!m_window->analyser2());

        m_window->discardModifications();
        QCOMPARE(m_window->openPath(session, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        Analyser *a2 = m_window->analyser2();
        QVERIFY(a2->getMainModelId() != m_window->mainModelId());
        QCOMPARE(colourOf(a2->getLayer(Analyser::PitchTrack)),
                 colourNamed("Orange"));
        QCOMPARE(colourOf(a2->getLayer(Analyser::Notes)),
                 colourNamed("Bright Purple"));
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(a2)), highHz)) < 10.0);
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           lowHz)) < 10.0);

        auto wave = sv::ModelById::getAs<sv::WaveFileModel>
            (a2->getMainModelId());
        QVERIFY(wave);
        QCOMPARE(wave->getStartFrame(), sv::sv_frame_t(0));

        // The take's layers were restored and claimed, not analysed
        // again: a re-analysis would have left the restored pair in the
        // pane and put a second one beside it
        QCOMPARE(noteLayersInPane0(), 2);

        auto after = pitchEvents(a2);
        QCOMPARE(after.size(), before.size());
        QVERIFY(!after.empty());
        QVERIFY2(std::llabs(after.front().getFrame() - firstEventBefore) <=
                 2 * hop,
                 qPrintable(QString("the reloaded take's pitch track starts "
                                    "at frame %1; before saving it started "
                                    "at %2")
                            .arg(after.front().getFrame())
                            .arg(firstEventBefore)));

        // The take is the audio file the session pointed at, and its
        // coverage comes from the coverage strip the session kept: one
        // recording from frame 0, so all of the file
        QVERIFY(m_window->takes()->haveTake());
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0], Coverage::Range(0, wave->getFrameCount()));
    }

    void close_session_resets() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;
        m_window->doCloseSession();

        QVERIFY(!m_window->analyser2());
        QVERIFY(!m_window->realtimeTracker());
        QVERIFY(!m_window->realtimeLayer());
        QVERIFY(m_window->realtimeModelId().isNone());
        QVERIFY(m_window->currentRecordingModelId().isNone());
        QVERIFY(!m_window->recordingLayer());
        QVERIFY(m_window->pendingSingingModelId().isNone());
        QVERIFY(!m_window->recordingAsSingingTrack());
        QVERIFY2(!m_window->takes()->haveTake(),
                 "the take outlived the session it was recorded in");
        QCOMPARE(m_window->pendingExtraPaneCount(), 0);
        QCOMPARE(m_window->paneStack()->getPaneCount(), 0);
        QCOMPARE(m_window->paneStack()->getHiddenPaneCount(), 0);

        // and the window still works
        openReference(writeWav(tone(highHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           highHz)) < 10.0);
    }

    // The coverage strip: one region per range of the take that holds
    // recorded singing, drawn in pane 0 and stored in the session

    void coverage_strip_follows_takes() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 5.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        // Nothing recorded, nothing to show
        QVERIFY(!m_window->coverageStrip()->isShown());
        QCOMPARE(stripLayersInPane0(), 0);

        take(700);
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->coverageStrip()->isShown());
        QCOMPARE(int(stripEvents().size()), 1);
        verifyStripMatchesTake();
        if (QTest::currentTestFailed()) return;

        // A second recording in a gap: a second bar, and the first
        // exactly where it was
        sv::sv_frame_t firstEnd =
            m_window->takes()->getCoverage().getEndFrame();
        const sv::sv_frame_t P = sv::sv_frame_t(2.0 * rate);
        m_window->seekTo(P);
        take(700);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(int(stripEvents().size()), 2);
        QCOMPARE(stripEvents()[0].getFrame(), sv::sv_frame_t(0));
        QCOMPARE(stripEvents()[0].getDuration(), firstEnd);
        QCOMPARE(stripEvents()[1].getFrame(), P);
        verifyStripMatchesTake();
        if (QTest::currentTestFailed()) return;

        // What it looks like: a filled band along the bottom of the pane
        // where there is singing, and nothing in the gap (the svgui
        // fork's PlotStrip style).  The layer is painted on its own: what
        // else is in the pane is not the point here
        {
            sv::Pane *pane = m_window->paneStack()->getPane(0);
            QVERIFY(pane);
            pane->resize(400, 200);
            pane->setZoomLevel(sv::ZoomLevel
                               (sv::ZoomLevel::FramesPerPixel, 512));
            pane->setCentreFrame(sv::sv_frame_t(2.0 * rate));

            QImage image(pane->width(), pane->height(), QImage::Format_RGB32);
            image.fill(Qt::black);
            {
                QPainter painter(&image);
                stripLayer()->paint(pane, painter, image.rect());
            }

            QRgb strip = sv::ColourDatabase::getInstance()->getColour
                (stripLayer()->getBaseColour()).rgb();
            QRgb black = QColor(Qt::black).rgb();
            int y = image.height() - 3;
            int inFirst = pane->getXForFrame(firstEnd / 2);
            int inGap = pane->getXForFrame((firstEnd + P) / 2);
            int inSecond = pane->getXForFrame(P + sv::sv_frame_t(0.3 * rate));
            QVERIFY(inFirst >= 0 && inSecond < image.width());
            QCOMPARE(image.pixel(inFirst, y), strip);
            QCOMPARE(image.pixel(inSecond, y), strip);
            QCOMPARE(image.pixel(inGap, y), black);
            // a band, not a block, and nothing else of a region
            for (int yy = 0; yy < image.height() - 12; ++yy) {
                QCOMPARE(image.pixel(inFirst, yy), black);
            }
        }

        // A third that runs from inside the first range into the second:
        // the two become one bar
        m_window->seekTo(sv::sv_frame_t(0.5 * rate));
        take(2000);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(int(stripEvents().size()), 1);
        QCOMPARE(stripEvents()[0].getFrame(), sv::sv_frame_t(0));
        QVERIFY2(stripEvents()[0].getDuration() > P,
                 "the recording that joined the two ranges did not reach "
                 "the second of them");
        verifyStripMatchesTake();
    }

    // Coverage has no file format of its own: the strip's regions are
    // where it is stored, and where it comes from when a session is
    // opened again.  Before this a restored take covered all of its file
    void coverage_strip_survives_a_session() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;

        Coverage::Ranges before = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(before.size()), 2);
        QVERIFY(before[0].end < before[1].start);

        QString session = m_dir.filePath("coverage.ton");
        QVERIFY(m_window->saveSessionFile(session));
        m_window->doCloseSession();
        QVERIFY2(!m_window->coverageStrip()->isShown(),
                 "the coverage strip outlived the session it was made in");

        m_window->discardModifications();
        QCOMPARE(m_window->openPath(session, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        // The gap survived: a take restored before this phase covered all
        // of its file, which would have been one range and no gap
        QVERIFY(m_window->takes()->haveTake());
        QCOMPARE(m_window->takes()->getCoverage().getRanges(), before);

        // ... and the layer showing it is the one the session restored,
        // not a second one made beside it
        QVERIFY(m_window->coverageStrip()->isShown());
        verifyStripMatchesTake();
    }

    // Nothing of a take's strip is left over for the next singing track
    void coverage_strip_replaced_by_load_singing_track() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(int(stripEvents().size()), 1);
        QVERIFY(stripEvents()[0].getFrame() > 0);

        // A track loaded whole is a take whose singing is all of it: one
        // bar, from frame 0, and no second strip beside the first
        m_window->loadSingingTrack(writeWav(tone(highHz, 1.0)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        auto wave = takeAudio();
        QVERIFY(wave);
        QCOMPARE(int(stripEvents().size()), 1);
        QCOMPARE(stripEvents()[0].getFrame(), sv::sv_frame_t(0));
        QCOMPARE(stripEvents()[0].getDuration(), wave->getFrameCount());
        verifyStripMatchesTake();
        if (QTest::currentTestFailed()) return;

        // And the session closes without leaving the layer behind
        m_window->doCloseSession();
        QVERIFY(!m_window->coverageStrip()->isShown());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QCOMPARE(stripLayersInPane0(), 0);
    }

    // Erase Singing in Selection and Select Recording at Playhead
    // (spec 5.2).  An erase takes the singing out of the take's audio,
    // out of its coverage and strip, and out of the pitch and the notes
    // that were analysed there.  Nothing is analysed again: the layers
    // on screen are the very ones that were there before

    void erase_a_whole_recording() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(sv::sv_frame_t(1.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        Coverage::Range recorded = ranges[0];
        QString audioBefore = m_window->takes()->getAudioPath();
        sv::Layer *pitch = m_window->analyser2()->getLayer(Analyser::PitchTrack);
        sv::Layer *notes = m_window->analyser2()->getLayer(Analyser::Notes);
        QVERIFY(pitch && notes);
        QVERIFY(!pitchEvents(pitch).empty());
        QVERIFY(!noteEvents(notes).empty());
        QVERIFY(takeAudioRms(recorded.start, recorded.end) > 0.01);
        QVERIFY(takeAudio());
        sv::sv_frame_t frames = takeAudio()->getFrameCount();

        // The range to erase is what the other new action selects
        m_window->seekTo(recorded.start + recorded.length() / 2);
        m_window->doSelectRecordingAtPlayhead();
        QCOMPARE(int(m_window->selections().size()), 1);
        QCOMPARE(m_window->selections().begin()->getStartFrame(),
                 recorded.start);
        QCOMPARE(m_window->selections().begin()->getEndFrame(), recorded.end);

        m_window->doEraseSingingInSelection();

        // Nothing is covered any more and the strip has gone with it.
        // The take stays, in a file of its own, as long as the one it
        // replaces and silent where the singing was
        QVERIFY(m_window->takes()->haveTake());
        QVERIFY(m_window->takes()->getCoverage().isEmpty());
        QVERIFY(m_window->takes()->getAudioPath() != audioBefore);
        QCOMPARE(m_window->takes()->getSupersededPaths().last(), audioBefore);
        QVERIFY(!m_window->coverageStrip()->isShown());
        QCOMPARE(stripLayersInPane0(), 0);
        QVERIFY(takeAudioRms(recorded.start + 1000, recorded.end - 1000)
                < 1e-6);

        // The same two layers, with nothing left in them where the
        // singing was
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::Notes), notes);
        QVERIFY(eventsBetween(pitchEvents(pitch),
                              recorded.start, recorded.end).empty());
        QVERIFY(eventsBetween(noteEvents(notes),
                              recorded.start, recorded.end).empty());

        // and the erase started no analysis of the take.  (A transformer
        // may well be running: making a selection sets Tony's own
        // re-analysis of the *reference* going, which has nothing to do
        // with the take.  Wait for it, and see that the take's pitch and
        // notes are still empty when everything has settled)
        QVERIFY(!m_window->analysingRange());
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::Notes), notes);
        QVERIFY(eventsBetween(pitchEvents(pitch),
                              recorded.start, recorded.end).empty());
        QVERIFY(eventsBetween(noteEvents(notes),
                              recorded.start, recorded.end).empty());

        // The audio under them is the new file, as long as the one it
        // replaced (a model of a file just opened takes a moment to know
        // how long it is)
        QTRY_VERIFY(takeAudio() && takeAudio()->getFrameCount() == frames);
        verifyPlaySourceClean();
    }

    // One end of a recording erased: what is left is the rest of it, and
    // a note whose onset went with the audio begins where the audio does
    void erase_trims_one_end() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(sv::sv_frame_t(1.0 * rate));
        take(1000);
        if (QTest::currentTestFailed()) return;

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        Coverage::Range recorded = ranges[0];
        sv::sv_frame_t cut = recorded.start + sv::sv_frame_t(0.4 * rate);
        QVERIFY(cut < recorded.end);
        sv::Layer *pitch = m_window->analyser2()->getLayer(Analyser::PitchTrack);
        sv::Layer *notes = m_window->analyser2()->getLayer(Analyser::Notes);
        QVERIFY(!eventsBetween(pitchEvents(pitch), recorded.start,
                               cut).empty());

        m_window->selectRange(recorded.start, cut);
        m_window->doEraseSingingInSelection();

        auto left = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(left.size()), 1);
        QCOMPARE(left[0], Coverage::Range(cut, recorded.end));
        QCOMPARE(int(stripEvents().size()), 1);
        verifyStripMatchesTake();
        if (QTest::currentTestFailed()) return;

        // Silent where the erase went, and as it was after that
        QVERIFY(takeAudioRms(recorded.start + 1000, cut - 1000) < 1e-6);
        QVERIFY(takeAudioRms(cut + 1000, recorded.end - 1000) > 0.01);

        QCOMPARE(m_window->analyser2()->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::Notes), notes);
        QVERIFY(eventsBetween(pitchEvents(pitch), recorded.start, cut).empty());
        QVERIFY(!eventsBetween(pitchEvents(pitch), cut, recorded.end).empty());

        auto notesLeft = noteEvents(notes);
        QVERIFY(!notesLeft.empty());
        for (const auto &e : notesLeft) {
            QVERIFY2(e.getFrame() >= cut,
                     "a note was left starting inside the erased range");
        }
        QVERIFY(!m_window->analysingRange());
    }

    // A range erased from the middle: the coverage, the strip and the
    // note that ran through it are each in two parts afterwards
    void erase_splits_a_recording() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(sv::sv_frame_t(1.0 * rate));
        take(1200);
        if (QTest::currentTestFailed()) return;

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        Coverage::Range recorded = ranges[0];
        sv::sv_frame_t from = recorded.start + sv::sv_frame_t(0.4 * rate);
        sv::sv_frame_t to = recorded.start + sv::sv_frame_t(0.8 * rate);
        QVERIFY(to < recorded.end);
        sv::Layer *pitch = m_window->analyser2()->getLayer(Analyser::PitchTrack);
        sv::Layer *notes = m_window->analyser2()->getLayer(Analyser::Notes);

        m_window->selectRange(from, to);
        m_window->doEraseSingingInSelection();

        auto left = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(left.size()), 2);
        QCOMPARE(left[0], Coverage::Range(recorded.start, from));
        QCOMPARE(left[1], Coverage::Range(to, recorded.end));
        QCOMPARE(int(stripEvents().size()), 2);
        verifyStripMatchesTake();
        if (QTest::currentTestFailed()) return;

        QVERIFY(takeAudioRms(recorded.start + 1000, from - 1000) > 0.01);
        QVERIFY(takeAudioRms(from + 1000, to - 1000) < 1e-6);
        QVERIFY(takeAudioRms(to + 1000, recorded.end - 1000) > 0.01);

        QCOMPARE(m_window->analyser2()->getLayer(Analyser::PitchTrack), pitch);
        QCOMPARE(m_window->analyser2()->getLayer(Analyser::Notes), notes);
        QVERIFY(!eventsBetween(pitchEvents(pitch), recorded.start,
                               from).empty());
        QVERIFY(eventsBetween(pitchEvents(pitch), from, to).empty());
        QVERIFY(!eventsBetween(pitchEvents(pitch), to, recorded.end).empty());

        // Nothing of a note is left over the erased audio
        auto notesLeft = noteEvents(notes);
        QVERIFY(!notesLeft.empty());
        for (const auto &e : notesLeft) {
            QVERIFY2(e.getFrame() + e.getDuration() <= from ||
                     e.getFrame() >= to,
                     "a note still runs through the erased range");
        }
        QVERIFY(!m_window->analysingRange());
    }

    // The coverage range the playhead is in becomes the selection; in a
    // gap there is nothing to select, and what is selected is left alone
    void select_recording_at_playhead() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 2);
        m_window->clearSelections();

        m_window->seekTo(ranges[0].start + ranges[0].length() / 2);
        m_window->doSelectRecordingAtPlayhead();
        QCOMPARE(int(m_window->selections().size()), 1);
        QCOMPARE(m_window->selections().begin()->getStartFrame(),
                 ranges[0].start);
        QCOMPARE(m_window->selections().begin()->getEndFrame(), ranges[0].end);

        // In the gap between the two recordings
        m_window->seekTo((ranges[0].end + ranges[1].start) / 2);
        m_window->doSelectRecordingAtPlayhead();
        QCOMPARE(int(m_window->selections().size()), 1);
        QCOMPARE(m_window->selections().begin()->getStartFrame(),
                 ranges[0].start);

        m_window->seekTo(ranges[1].start + ranges[1].length() / 2);
        m_window->doSelectRecordingAtPlayhead();
        QCOMPARE(int(m_window->selections().size()), 1);
        QCOMPARE(m_window->selections().begin()->getStartFrame(),
                 ranges[1].start);
        QCOMPARE(m_window->selections().begin()->getEndFrame(), ranges[1].end);
    }

    // Neither action is to be had without a take with singing in it, nor
    // while one is being recorded, and erasing needs a selection as well
    void erase_actions_enabled_when_they_apply() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        m_window->selectRange(0, sv::sv_frame_t(0.5 * rate));
        QVERIFY(!m_window->eraseSingingAction()->isEnabled());
        QVERIFY(!m_window->selectRecordingAction()->isEnabled());

        m_window->clearSelections();
        take(700);
        if (QTest::currentTestFailed()) return;

        // A take, but nothing selected to erase from it
        m_window->clearSelections();
        QVERIFY(m_window->selectRecordingAction()->isEnabled());
        QVERIFY(!m_window->eraseSingingAction()->isEnabled());

        m_window->selectRange(0, sv::sv_frame_t(0.5 * rate));
        QVERIFY(m_window->selectRecordingAction()->isEnabled());
        QVERIFY(m_window->eraseSingingAction()->isEnabled());

        // and neither of them while the next take is being recorded
        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(!m_window->eraseSingingAction()->isEnabled());
        QVERIFY(!m_window->selectRecordingAction()->isEnabled());
        QTest::qWait(300);
        stopTake();
    }

    // Undo and redo of the singing of a take (spec 5.4)

    // A recording over material that is already there, undone and redone:
    // the audio file, the coverage, the strip and every pitch event and
    // note come back exactly as they were
    void undo_redo_a_take() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 8.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        // The material the second recording is undone back to
        take(1000);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot before = snapshotTake();
        QVERIFY(!before.pitch.empty());
        QVERIFY(!before.notes.empty());
        QVERIFY(before.frames > 0);

        // A second recording, into the silence after the first
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;

        TakeSnapshot after = snapshotTake();
        QVERIFY(after.path != before.path);
        QCOMPARE(int(after.coverage.size()), 2);
        QVERIFY(after.frames > before.frames);
        Coverage::Range recorded = after.coverage[1];
        QVERIFY(takeAudioRms(recorded.start + 1000, recorded.end - 1000)
                > 0.01);

        QCOMPARE(undoOnce(), QString("Record Singing"));
        verifyTakeMatches(before);
        if (QTest::currentTestFailed()) return;

        // The file the take plays is the one from before, in which the
        // second recording's range was never anything but silence
        QVERIFY(takeAudioRms(recorded.start + 1000,
                             std::min(recorded.end, before.frames) - 1000)
                < 1e-6);
        QVERIFY(!m_window->analysingRange());
        verifyPlaySourceClean();

        QCOMPARE(redoOnce(), QString("Record Singing"));
        verifyTakeMatches(after);
        if (QTest::currentTestFailed()) return;
        QVERIFY(takeAudioRms(recorded.start + 1000, recorded.end - 1000)
                > 0.01);

        // The redo restored the analysis from the command; it did not run
        // pYIN again
        QVERIFY(!m_window->analysingRange());
        verifyPlaySourceClean();
    }

    // An erase undone and redone, the same way
    void undo_redo_an_erase() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(sv::sv_frame_t(1.0 * rate));
        take(1200);
        if (QTest::currentTestFailed()) return;

        TakeSnapshot before = snapshotTake();
        QCOMPARE(int(before.coverage.size()), 1);
        QVERIFY(!before.pitch.empty());
        QVERIFY(!before.notes.empty());

        Coverage::Range recorded = before.coverage[0];
        sv::sv_frame_t from = recorded.start + sv::sv_frame_t(0.4 * rate);
        sv::sv_frame_t to = recorded.start + sv::sv_frame_t(0.8 * rate);
        m_window->selectRange(from, to);
        m_window->doEraseSingingInSelection();

        TakeSnapshot after = snapshotTake();
        QVERIFY(after.path != before.path);
        QCOMPARE(int(after.coverage.size()), 2);
        QVERIFY(eventsBetween(after.pitch, from, to).empty());
        QVERIFY(takeAudioRms(from + 1000, to - 1000) < 1e-6);

        QCOMPARE(undoOnce(), QString("Erase Singing"));
        verifyTakeMatches(before);
        if (QTest::currentTestFailed()) return;
        QVERIFY(takeAudioRms(from + 1000, to - 1000) > 0.01);

        QCOMPARE(redoOnce(), QString("Erase Singing"));
        verifyTakeMatches(after);
        if (QTest::currentTestFailed()) return;
        QVERIFY(takeAudioRms(from + 1000, to - 1000) < 1e-6);

        // The selection is still there and the reference's own
        // re-analysis of it may still be running: let it finish before
        // the window goes
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);
        verifyPlaySourceClean();
    }

    // Undo pressed while the analysis of the recorded range is still
    // running: the run is abandoned, so nothing of it lands on the take
    // that has been put back.  The redo has to run it again -- that
    // result never existed to be restored
    void undo_during_analysis_then_redo() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 8.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(1000);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot before = snapshotTake();
        QVERIFY(!before.pitch.empty());

        // Stop splices the recording in and starts the analysis of it
        // there and then.  Held, so that it is still running when Undo is
        // pressed, and the redo's run when its range is read
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(700);
        m_window->holdRangedMerges(true);
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY2(m_window->analysingRange(),
                 "the race was not set up: no analysis was running after Stop");
        sv::sv_frame_t analysedStart = m_window->analysedRangeStart();
        sv::sv_frame_t analysedEnd = m_window->analysedRangeEnd();
        QVERIFY(analysedEnd > analysedStart);

        QCOMPARE(undoOnce(), QString("Record Singing"));
        QVERIFY2(!m_window->analysingRange(),
                 "the analysis was left running over the undone take");
        verifyTakeMatches(before);
        if (QTest::currentTestFailed()) return;

        // The live dots were waiting for that analysis; nothing is
        QVERIFY(!m_window->realtimeLayer());
        QVERIFY(m_window->eraseSingingAction()->isEnabled() ||
                m_window->selections().empty());

        // Redo: the range is analysed again, and this time the result
        // reaches the take's pitch track
        QCOMPARE(redoOnce(), QString("Record Singing"));
        QVERIFY(m_window->analysingRange());
        QCOMPARE(m_window->analysedRangeStart(), analysedStart);
        QCOMPARE(m_window->analysedRangeEnd(), analysedEnd);
        m_window->holdRangedMerges(false);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);

        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 2);
        QVERIFY2(!eventsBetween(pitchEvents(m_window->analyser2()),
                                ranges[1].start, ranges[1].end).empty(),
                 "the redone recording was never analysed");
        verifyPlaySourceClean();
    }

    // The first recording of a take undone: there is no take and no
    // singing track at all, as before it, and Record still works
    void undo_a_first_take_then_record_again() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 8.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 3.0)));
        if (QTest::currentTestFailed()) return;

        int panes = m_window->paneStack()->getPaneCount();
        take(700);
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->takes()->haveTake());

        QCOMPARE(undoOnce(), QString("Record Singing"));
        QVERIFY(!m_window->takes()->haveTake());
        QVERIFY(m_window->takes()->getCoverage().isEmpty());
        QVERIFY2(!m_window->analyser2(),
                 "the singing track was left behind with no audio");
        QVERIFY(!m_window->coverageStrip()->isShown());
        QCOMPARE(stripLayersInPane0(), 0);
        QCOMPARE(noteLayersInPane0(), 1); // the reference's
        QCOMPARE(m_window->paneStack()->getPaneCount(), panes);
        verifyPlaySourceClean();

        // and the reference is untouched
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           lowHz)) < 10.0);

        // Recording again makes a take from nothing, as the first
        // recording did
        m_window->seekTo(sv::sv_frame_t(1.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->takes()->haveTake());
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QCOMPARE(ranges[0].start, sv::sv_frame_t(1.0 * rate));
        QVERIFY(m_window->analyser2());
        QVERIFY(!pitchEvents(m_window->analyser2()).empty());
        QVERIFY(!noteEvents(m_window->analyser2()
                            ->getLayer(Analyser::Notes)).empty());
        verifyStripMatchesTake();
        verifyPlaySourceClean();
    }

    // What one take leaves on the undo stack: the take, and nothing of
    // the layers and panes Tony makes for itself along the way
    void undo_stack_top_after_a_take() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        // Nothing of loading and analysing the reference is undoable
        // either: those layers are Tony's, not the user's
        QCOMPARE(undoOnce(), QString());

        take(700);
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->isDocumentModified());

        QCOMPARE(undoOnce(), QString("Record Singing"));
        QVERIFY(!m_window->takes()->haveTake());
        QCOMPARE(undoOnce(), QString());

        QCOMPARE(redoOnce(), QString("Record Singing"));
        QVERIFY(m_window->takes()->haveTake());
        QCOMPARE(redoOnce(), QString());
    }

    // A take does not cost the takes before it their undo: each is an
    // entry of its own, undone and redone in order
    void undo_two_takes_in_order() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 5.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        if (QTest::currentTestFailed()) return;
        QString firstPath = m_window->takes()->getAudioPath();
        Coverage firstCoverage = m_window->takes()->getCoverage();

        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        if (QTest::currentTestFailed()) return;
        QString secondPath = m_window->takes()->getAudioPath();
        QCOMPARE(int(m_window->takes()->getCoverage().getRanges().size()), 2);

        QCOMPARE(undoOnce(), QString("Record Singing"));
        QCOMPARE(m_window->takes()->getAudioPath(), firstPath);
        QVERIFY(m_window->takes()->getCoverage() == firstCoverage);

        QCOMPARE(undoOnce(), QString("Record Singing"));
        QVERIFY(!m_window->takes()->haveTake());
        QCOMPARE(undoOnce(), QString());

        QCOMPARE(redoOnce(), QString("Record Singing"));
        QCOMPARE(m_window->takes()->getAudioPath(), firstPath);
        QCOMPARE(redoOnce(), QString("Record Singing"));
        QCOMPARE(m_window->takes()->getAudioPath(), secondPath);
        QCOMPARE(int(m_window->takes()->getCoverage().getRanges().size()), 2);
    }

    // The audio files a take has been through are kept until the session
    // closes, and then only the ones nothing refers to any more go
    void take_files_deleted_on_close() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;

        QStringList written = m_window->takes()->getWrittenPaths();
        QCOMPARE(int(written.size()), 2);
        QCOMPARE(written.last(), m_window->takes()->getAudioPath());
        for (const QString &path : written) {
            QVERIFY2(QFileInfo::exists(path), qPrintable(path));
        }

        // Only the superseded one is unused: the take's own file is kept
        // even though this session was never saved
        QCOMPARE(m_window->takes()->unusedWrittenFiles(),
                 QStringList { written.first() });

        m_window->doCloseSession();

        QVERIFY2(!QFileInfo::exists(written.first()),
                 "a superseded take audio file was left behind");
        QVERIFY2(QFileInfo::exists(written.last()),
                 "the take's own audio file was deleted");

        // The commands that held those paths went with the session
        QCOMPARE(undoOnce(), QString());
    }

    // Several takes (spec 5.3).  Every take of the session has its three
    // layers in pane 0, named after it; only the active take has an audio
    // model and the singing analyser, and the rest are hidden and silent

    void first_recording_makes_take_1() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        // No take, nothing in the combo box, and nothing to act on
        QCOMPARE(m_window->takes()->getTakeCount(), 0);
        QCOMPARE(m_window->takeCombo()->count(), 0);
        m_window->doUpdateMenuStates();
        QVERIFY(m_window->newTakeAction()->isEnabled());
        QVERIFY(!m_window->duplicateTakeAction()->isEnabled());
        QVERIFY(!m_window->deleteTakeAction()->isEnabled());

        take(600);
        if (QTest::currentTestFailed()) return;

        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { "Take 1" });
        QCOMPARE(m_window->takes()->getActiveIndex(), 0);

        // The layers of the take carry its name, and they are the ones the
        // analyser holds
        Analyser *a2 = m_window->analyser2();
        QVERIFY(a2);
        TakeLayers::Found found = takeLayers("Take 1");
        QCOMPARE(static_cast<sv::Layer *>(found.pitch),
                 a2->getLayer(Analyser::PitchTrack));
        QCOMPARE(static_cast<sv::Layer *>(found.notes),
                 a2->getLayer(Analyser::Notes));
        QCOMPARE(static_cast<sv::Layer *>(found.coverage),
                 static_cast<sv::Layer *>(stripLayer()));

        QCOMPARE(m_window->takeCombo()->count(), 1);
        QCOMPARE(m_window->takeCombo()->currentText(), QString("Take 1"));
        m_window->doUpdateMenuStates();
        QVERIFY(m_window->duplicateTakeAction()->isEnabled());
        QVERIFY(m_window->deleteTakeAction()->isEnabled());
    }

    // New Empty Take leaves the take that was on show as it is, and the
    // next recording goes into the new one
    void new_empty_take_then_record() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot first = snapshotTake();
        QVERIFY(!first.pitch.empty());
        QVERIFY(!first.notes.empty());
        TakeLayers::Found one = takeLayers("Take 1");
        QVERIFY(one.pitch && one.notes && one.coverage);

        m_window->doNewEmptyTake();

        // Two takes, the new one active with nothing in it and nothing to
        // show it with
        QCOMPARE(m_window->takes()->getTakeNames(),
                 QStringList({ "Take 1", "Take 2" }));
        QCOMPARE(m_window->takes()->getActiveIndex(), 1);
        QVERIFY(!m_window->takes()->haveTake());
        QVERIFY2(!m_window->analyser2(),
                 "the empty take was given the singing analyser");
        QVERIFY(!m_window->coverageStrip()->isShown());
        QCOMPARE(m_window->takeCombo()->currentText(), QString("Take 2"));

        // The first take's layers are the same objects, with their events,
        // and they are put away
        QCOMPARE(takeLayers("Take 1").pitch, one.pitch);
        QCOMPARE(pitchEvents(one.pitch), first.pitch);
        QCOMPARE(noteEvents(one.notes), first.notes);
        verifyTakeIsPutAway("Take 1");
        if (QTest::currentTestFailed()) return;

        // Recording into the new take: its own audio file, its own layers
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;

        QCOMPARE(m_window->takes()->getActiveIndex(), 1);
        QVERIFY(m_window->takes()->getAudioPath() != first.path);
        auto ranges = m_window->takes()->getCoverage().getRanges();
        QCOMPARE(int(ranges.size()), 1);
        QVERIFY(ranges[0].start >= sv::sv_frame_t(2.0 * rate));

        TakeLayers::Found two = takeLayers("Take 2");
        QVERIFY(two.pitch && two.notes && two.coverage);
        QVERIFY(two.pitch != one.pitch && two.notes != one.notes);
        QCOMPARE(static_cast<sv::Layer *>(two.notes),
                 m_window->analyser2()->getLayer(Analyser::Notes));
        QVERIFY(!pitchEvents(two.pitch).empty());

        // The first take is untouched by all of it
        QCOMPARE(pitchEvents(one.pitch), first.pitch);
        QCOMPARE(noteEvents(one.notes), first.notes);
        QCOMPARE(m_window->takes()->getTake(0)->audioPath, first.path);
        verifyTakeIsPutAway("Take 1");
        if (QTest::currentTestFailed()) return;

        // Each take has its own strip, and one of them is on show
        QCOMPARE(allStripLayersInPane0(), 2);
        verifyStripMatchesTake();
        verifyPlaySourceClean();
    }

    // Switching back and forth: the take's audio, coverage, strip, pitch
    // and notes come back exactly, the layers are the very same objects
    // and nothing is analysed
    void switch_between_takes() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot first = snapshotTake();
        TakeLayers::Found one = takeLayers("Take 1");

        m_window->doNewEmptyTake();
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot second = snapshotTake();
        TakeLayers::Found two = takeLayers("Take 2");
        QVERIFY(second.path != first.path);
        QVERIFY(second.frames > 0 && first.frames > 0);

        // Back to the first take, through the combo box as the user does
        m_window->doChooseTakeInCombo(0);
        QCOMPARE(m_window->takes()->getActiveIndex(), 0);

        verifyTakeMatches(first);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(takeLayers("Take 1").pitch, one.pitch);
        QCOMPARE(takeLayers("Take 1").notes, one.notes);
        QCOMPARE(static_cast<sv::Layer *>(one.pitch),
                 m_window->analyser2()->getLayer(Analyser::PitchTrack));
        verifyTakeIsPutAway("Take 2");
        if (QTest::currentTestFailed()) return;

        // Nothing was analysed, then or when the queued calls ran
        QVERIFY(!m_window->analysingRange());
        QVERIFY(!sv::ModelTransformerFactory::getInstance()
                ->haveRunningTransformers());
        QCoreApplication::processEvents();
        QVERIFY(!sv::ModelTransformerFactory::getInstance()
                ->haveRunningTransformers());

        // The note tool acts on the pane's topmost note layer: it has to
        // be the take that is on show, not the one put away
        QCOMPARE(topNoteLayerInPane0(), static_cast<sv::Layer *>(one.notes));
        verifyStripMatchesTake();
        if (QTest::currentTestFailed()) return;

        // The undo history goes with a switch (spec 5.4)
        QCOMPARE(undoOnce(), QString());

        // ... and back to the second
        m_window->doChooseTakeInCombo(1);
        QCOMPARE(m_window->takes()->getActiveIndex(), 1);
        verifyTakeMatches(second);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(takeLayers("Take 2").notes, two.notes);
        QCOMPARE(topNoteLayerInPane0(), static_cast<sv::Layer *>(two.notes));
        verifyTakeIsPutAway("Take 1");
        if (QTest::currentTestFailed()) return;
        QVERIFY(!m_window->analysingRange());
        verifyStripMatchesTake();
        verifyPlaySourceClean();
    }

    // A take that is not on show is silent and cannot hold playback open
    // past the end of the audio that is
    void inactive_take_does_not_play() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        // A take that reaches well past the end of the reference
        take(1500);
        if (QTest::currentTestFailed()) return;
        sv::sv_frame_t longest = m_window->takes()->getCoverage().getEndFrame();
        QVERIFY(longest > sv::sv_frame_t(1.2 * rate));
        QVERIFY(m_window->playSource()->getPlayEndFrame() >= longest);

        // ... and a short one beside it
        m_window->doNewEmptyTake();
        m_window->seekTo(0);
        take(400);
        if (QTest::currentTestFailed()) return;

        verifyTakeIsPutAway("Take 1");
        if (QTest::currentTestFailed()) return;

        // Playback ends with the longest of what can be heard: the
        // reference and the take on show, not the take put away
        sv::sv_frame_t end = m_window->playSource()->getPlayEndFrame();
        QVERIFY2(end < longest,
                 qPrintable(QString("playback still runs to frame %1, the end "
                                    "of the take that was put away (%2)")
                            .arg(end).arg(longest)));
        QVERIFY(end >= sv::sv_frame_t(1.0 * rate));
        verifyPlaySourceClean();
    }

    // Duplicate Take: a copy of the take, sharing its audio file, and
    // recording into the copy leaves the original alone
    void duplicate_take_then_record() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot first = snapshotTake();
        QVERIFY(!first.pitch.empty());
        QVERIFY(!first.notes.empty());
        TakeLayers::Found one = takeLayers("Take 1");

        m_window->doDuplicateTake();

        QCOMPARE(m_window->takes()->getTakeNames(),
                 QStringList({ "Take 1", "Take 2" }));
        QCOMPARE(m_window->takes()->getActiveIndex(), 1);

        // The same audio file and coverage, and the same events in layers
        // of its own
        QCOMPARE(m_window->takes()->getAudioPath(), first.path);
        QCOMPARE(m_window->takes()->getCoverage().getRanges(), first.coverage);
        TakeLayers::Found two = takeLayers("Take 2");
        QVERIFY(two.pitch && two.notes && two.coverage);
        QVERIFY(two.pitch != one.pitch && two.notes != one.notes);
        QCOMPARE(pitchEvents(two.pitch), first.pitch);
        QCOMPARE(noteEvents(two.notes), first.notes);
        verifyStripMatchesTake();
        if (QTest::currentTestFailed()) return;
        verifyTakeIsPutAway("Take 1");
        if (QTest::currentTestFailed()) return;

        // No audio file was written, and the shared one is not up for
        // deletion however thoroughly either take supersedes it
        QCOMPARE(m_window->takes()->getWrittenPaths(),
                 QStringList { first.path });

        // Recording into the copy: its own file from now on, and the take
        // it was copied from is exactly as it was
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->takes()->getAudioPath() != first.path);
        QCOMPARE(int(m_window->takes()->getCoverage().getRanges().size()), 2);
        QCOMPARE(m_window->takes()->getTake(0)->audioPath, first.path);
        QCOMPARE(m_window->takes()->getTake(0)->coverage.getRanges(),
                 first.coverage);
        QCOMPARE(pitchEvents(one.pitch), first.pitch);
        QCOMPARE(noteEvents(one.notes), first.notes);
        QVERIFY2(m_window->takes()->unusedWrittenFiles().isEmpty(),
                 "the file the first take still plays was up for deletion");
        QVERIFY(QFileInfo::exists(first.path));
        verifyPlaySourceClean();
    }

    // Delete Take asks first, and deletes the take's layers and nothing
    // else: never an audio file
    void delete_the_active_take() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot first = snapshotTake();
        TakeLayers::Found one = takeLayers("Take 1");

        m_window->doNewEmptyTake();
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;
        QString secondPath = m_window->takes()->getAudioPath();
        TakeLayers::Found two = takeLayers("Take 2");

        // Asked, and answered no: nothing happens
        m_window->setDeleteTakeAnswer(false);
        m_window->doDeleteTake();
        QCOMPARE(m_window->deleteTakeQuestions(), 1);
        QCOMPARE(m_window->takes()->getTakeCount(), 2);

        m_window->setDeleteTakeAnswer(true);
        m_window->doDeleteTake();
        QCOMPARE(m_window->deleteTakeQuestions(), 2);

        // The take before it is the active one, as it was
        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { "Take 1" });
        QCOMPARE(m_window->takes()->getActiveIndex(), 0);
        verifyTakeMatches(first);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(takeLayers("Take 1").pitch, one.pitch);
        QCOMPARE(static_cast<sv::Layer *>(one.notes),
                 m_window->analyser2()->getLayer(Analyser::Notes));

        // The deleted take's layers are gone from the pane and from the
        // document, and its audio file is still on disk
        QVERIFY(!takeLayers("Take 2").pitch);
        QVERIFY(!takeLayers("Take 2").notes);
        QVERIFY(!takeLayers("Take 2").coverage);
        QVERIFY(!documentHasLayer(two.pitch));
        QVERIFY(!documentHasLayer(two.notes));
        QVERIFY(!documentHasLayer(two.coverage));
        QCOMPARE(noteLayersInPane0(), 2); // the reference's and Take 1's
        QVERIFY2(QFileInfo::exists(secondPath),
                 "deleting a take deleted its audio file");
        verifyStripMatchesTake();
        verifyPlaySourceClean();
        if (QTest::currentTestFailed()) return;

        // And the last take can go too, leaving no take at all
        m_window->doDeleteTake();
        QCOMPARE(m_window->takes()->getTakeCount(), 0);
        QCOMPARE(m_window->takes()->getActiveIndex(), -1);
        QVERIFY(!m_window->analyser2());
        QCOMPARE(allStripLayersInPane0(), 0);
        QCOMPARE(noteLayersInPane0(), 1); // the reference's
        QCOMPARE(m_window->takeCombo()->count(), 0);
        QVERIFY(QFileInfo::exists(first.path));

        // Recording again makes a take, as the first recording did
        m_window->seekTo(0);
        take(600);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { "Take 3" });
        QVERIFY(!pitchEvents(m_window->analyser2()).empty());
        verifyStripMatchesTake();
    }

    // Deleting a take that is not the active one: the one on show does not
    // move
    void delete_an_inactive_take() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        TakeLayers::Found one = takeLayers("Take 1");

        m_window->doNewEmptyTake();
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot second = snapshotTake();

        QVERIFY(m_window->doDeleteTakeAt(0));

        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { "Take 2" });
        QCOMPARE(m_window->takes()->getActiveIndex(), 0);
        verifyTakeMatches(second);
        if (QTest::currentTestFailed()) return;
        QVERIFY(!takeLayers("Take 1").pitch);
        QVERIFY(!documentHasLayer(one.pitch));
        QCOMPARE(noteLayersInPane0(), 2);
        verifyStripMatchesTake();
        verifyPlaySourceClean();
    }

    // Rename Take: the take and its layers, and not the undo history
    void rename_take() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(700);
        if (QTest::currentTestFailed()) return;
        TakeLayers::Found before = takeLayers("Take 1");
        QVERIFY(before.pitch && before.notes && before.coverage);

        m_window->setTakeNameAnswer("Chorus");
        m_window->doRenameTake();

        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { "Chorus" });
        QCOMPARE(m_window->takeCombo()->currentText(), QString("Chorus"));

        // The same layers, named after the take again
        TakeLayers::Found after = takeLayers("Chorus");
        QCOMPARE(after.pitch, before.pitch);
        QCOMPARE(after.notes, before.notes);
        QCOMPARE(after.coverage, before.coverage);
        QVERIFY(!takeLayers("Take 1").pitch);
        QCOMPARE(m_window->coverageStrip()->getTakeName(), QString("Chorus"));
        verifyStripMatchesTake();
        if (QTest::currentTestFailed()) return;

        // A rename is not a change to the singing, so the recording can
        // still be undone (spec 5.4)
        QCOMPARE(undoOnce(), QString("Record Singing"));
        QCOMPARE(redoOnce(), QString("Record Singing"));

        // The take's audio is still under its layers after all that.  The
        // layers are looked up again: an undo of the first recording of a
        // take takes them away, and the redo makes them afresh
        QVERIFY(m_window->analyser2());
        QCOMPARE(static_cast<sv::Layer *>(takeLayers("Chorus").pitch),
                 m_window->analyser2()->getLayer(Analyser::PitchTrack));
        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { "Chorus" });
    }

    // The take operations are not to be had while a take is being
    // recorded, and a new take then record is not the same as a switch
    void take_actions_disabled_while_recording() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;

        m_window->doUpdateMenuStates();
        QVERIFY(m_window->takeCombo()->isEnabled());
        QVERIFY(m_window->newTakeAction()->isEnabled());

        startTake();
        if (QTest::currentTestFailed()) return;
        m_window->doUpdateMenuStates();
        QVERIFY2(!m_window->takeCombo()->isEnabled(),
                 "takes could be switched while one was being recorded");
        QVERIFY(!m_window->newTakeAction()->isEnabled());
        QVERIFY(!m_window->duplicateTakeAction()->isEnabled());
        QVERIFY(!m_window->deleteTakeAction()->isEnabled());

        // And the calls themselves refuse, in case a script reaches them
        QVERIFY(!m_window->doSwitchToTake(0));
        m_window->doNewEmptyTake();
        QCOMPARE(m_window->takes()->getTakeCount(), 1);

        waitForSomethingRecorded();
        stopTake();
        if (QTest::currentTestFailed()) return;
        m_window->doUpdateMenuStates();
        QVERIFY(m_window->takeCombo()->isEnabled());
    }

    // The takes of a session in the .ton (spec 6.4): the <takes> element
    // says which takes there are, where their audio is and which was on
    // show; their pitch, notes and coverage come back as the layers they
    // are stored in, and nothing is analysed

    // Two takes, the second of them active, saved and opened again
    void two_takes_survive_a_session_opening() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        // A take with a gap in its coverage, and a second take beside it
        take(700);
        if (QTest::currentTestFailed()) return;
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot first = snapshotTake();
        QCOMPARE(int(first.coverage.size()), 2);
        QVERIFY(!first.pitch.empty() && !first.notes.empty());

        m_window->doNewEmptyTake();
        m_window->seekTo(sv::sv_frame_t(1.0 * rate));
        take(700);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot second = snapshotTake();
        QVERIFY(second.path != first.path);
        QVERIFY(!second.pitch.empty() && !second.notes.empty());

        QString session = m_dir.filePath("two-takes.ton");
        QVERIFY(m_window->saveSessionFile(session));

        // The save copied both takes' audio into the session's own folder
        // and the file names it there (spec 6.4), so that is where the
        // takes come back from
        first.path = m_window->takes()->getTake(0)->audioPath;
        second.path = m_window->takes()->getTake(1)->audioPath;
        QVERIFY2(TakesFile::isInFolder(TakesFile::takesFolder(session),
                                       first.path), qPrintable(first.path));

        reopenSession(session);
        if (QTest::currentTestFailed()) return;

        // Both takes, under their own names, with the one that was on show
        // active again
        QCOMPARE(m_window->takes()->getTakeNames(),
                 QStringList({ "Take 1", "Take 2" }));
        QCOMPARE(m_window->takes()->getActiveIndex(), 1);
        QCOMPARE(m_window->takes()->getAudioPath(), second.path);
        QCOMPARE(m_window->takes()->getCoverage().getRanges(), second.coverage);
        QCOMPARE(m_window->takes()->getTake(0)->audioPath, first.path);
        QCOMPARE(m_window->takes()->getTake(0)->coverage.getRanges(),
                 first.coverage);

        // The active take's own layers are the ones the analyser holds, and
        // its audio is under them
        Analyser *a2 = m_window->analyser2();
        QVERIFY(a2);
        QCOMPARE(static_cast<sv::Layer *>(takeLayers("Take 2").pitch),
                 a2->getLayer(Analyser::PitchTrack));
        QVERIFY(takeAudio());
        QCOMPARE(takeAudio()->getStartFrame(), sv::sv_frame_t(0));

        // The pitch and the notes of both takes came back as they were: a
        // session file keeps a value to six figures, nothing else changes
        verifyEventsSurvived(second.pitch, pitchEvents(a2), "the active take's pitch");
        verifyEventsSurvived(second.notes,
                             noteEvents(a2->getLayer(Analyser::Notes)),
                             "the active take's notes");
        verifyEventsSurvived(first.pitch, pitchEvents(takeLayers("Take 1").pitch),
                             "the stored take's pitch");
        verifyEventsSurvived(first.notes, noteEvents(takeLayers("Take 1").notes),
                             "the stored take's notes");
        if (QTest::currentTestFailed()) return;

        // Nothing was analysed on the way in, then or when the queued
        // calls of the load ran
        QVERIFY(!m_window->analysingRange());
        QCoreApplication::processEvents();
        QTest::qWait(100);
        QVERIFY2(!sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(),
                 "the session load started an analysis of a take");
        QCOMPARE(noteLayersInPane0(), 3); // the reference's and the two takes'

        // The take that is not on show is hidden, silent and owned by
        // nobody; the one that is can be heard, and playback runs to the
        // end of it
        verifyTakeIsPutAway("Take 1");
        if (QTest::currentTestFailed()) return;
        QVERIFY(a2->isAudible(Analyser::Audio));
        QVERIFY(m_window->playSource()->getModels()
                .count(a2->getMainModelId()));
        QVERIFY(m_window->playSource()->getPlayEndFrame() >=
                m_window->takes()->getCoverage().getEndFrame());

        // Each take has its strip; the active one's is on show and is the
        // coverage the take came back with
        QCOMPARE(allStripLayersInPane0(), 2);
        verifyStripMatchesTake();
        if (QTest::currentTestFailed()) return;

        // One copy of the take's audio and no leftovers of the load: the
        // waveform layer and model that Document::toXml() wrote for the
        // active take were dropped (spec 6.4)
        QCOMPARE(m_window->paneStack()->getPaneCount(), 2);
        QCOMPARE(m_window->paneStack()->getHiddenPaneCount(), 0);
        QCOMPARE(audioModelsBesidesReference(), 1);
        QCOMPARE(waveformLayersInPane0(), 2); // the reference's and the take's
        verifyPlaySourceClean();
        QVERIFY2(!m_window->isDocumentModified(),
                 "opening a session left it looking modified");
        QCOMPARE(undoOnce(), QString()); // and nothing on the undo stack
        if (QTest::currentTestFailed()) return;

        // Switching takes works after a load like any other time
        QVERIFY(m_window->doSwitchToTake(0));
        QCOMPARE(m_window->takes()->getAudioPath(), first.path);
        verifyEventsSurvived(first.pitch, pitchEvents(m_window->analyser2()),
                             "the take switched to after a load");
        if (QTest::currentTestFailed()) return;
        verifyTakeIsPutAway("Take 2");
        if (QTest::currentTestFailed()) return;
        verifyStripMatchesTake();
        QVERIFY(!m_window->analysingRange());
        QVERIFY2(m_window->isDocumentModified(),
                 "switching take did not mark the session modified");

        // A take made now carries the numbering on from the session
        m_window->doNewEmptyTake();
        QCOMPARE(m_window->takes()->getActiveName(), QString("Take 3"));
    }

    // A saved session names the audio file of every take, so none of them
    // is deleted on close, however thoroughly it has been superseded since
    void saved_session_protects_every_take_audio() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;
        QString firstRecorded = m_window->takes()->getAudioPath();

        m_window->doNewEmptyTake();
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(600);
        if (QTest::currentTestFailed()) return;

        QString session = m_dir.filePath("protected.ton");
        QVERIFY(m_window->doSaveSessionAs(session));

        // What the saved session names is the copy of each take's audio in
        // its own folder (spec 6.4)
        QString firstSaved = m_window->takes()->getTake(0)->audioPath;
        QString secondSaved = m_window->takes()->getTake(1)->audioPath;
        QVERIFY(firstSaved != firstRecorded);

        // Recording into the first take again supersedes the file the saved
        // session names for it
        QVERIFY(m_window->doSwitchToTake(0));
        m_window->seekTo(sv::sv_frame_t(1.0 * rate));
        take(600);
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->takes()->getAudioPath() != firstSaved);

        QStringList unused = m_window->takes()->unusedWrittenFiles();
        QVERIFY2(!unused.contains(firstSaved) && !unused.contains(secondSaved),
                 "an audio file the saved session names was up for deletion");
        QVERIFY2(unused.contains(firstRecorded),
                 "the recording the save copied into the folder is still "
                 "referred to by something");

        m_window->doCloseSession();

        QVERIFY2(QFileInfo::exists(firstSaved),
                 "the audio the saved session names for the first take was "
                 "deleted when the session closed");
        QVERIFY2(QFileInfo::exists(secondSaved),
                 "the audio the saved session names for the second take was "
                 "deleted when the session closed");

        // The recording the save copied into the folder is nobody's now:
        // that copy is the take's audio, and no undo is left to want this
        QVERIFY2(!QFileInfo::exists(firstRecorded),
                 "the recording that the save copied into the session's "
                 "folder was left behind in the record directory");
    }

    // A take name with characters that XML cares about
    void take_name_with_entities_survives_a_session() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;

        const QString name = "Rock & \"Roll\" <2>";
        m_window->setTakeNameAnswer(name);
        m_window->doRenameTake();
        QCOMPARE(m_window->takes()->getActiveName(), name);
        TakeSnapshot before = snapshotTake();

        QString session = m_dir.filePath("entities.ton");
        QVERIFY(m_window->saveSessionFile(session));
        // The take's audio is in the session's folder now (spec 6.4)
        before.path = m_window->takes()->getAudioPath();
        reopenSession(session);
        if (QTest::currentTestFailed()) return;

        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { name });
        QCOMPARE(m_window->takes()->getAudioPath(), before.path);
        QVERIFY(m_window->analyser2());
        QCOMPARE(static_cast<sv::Layer *>(takeLayers(name).pitch),
                 m_window->analyser2()->getLayer(Analyser::PitchTrack));
        verifyEventsSurvived(before.pitch, pitchEvents(m_window->analyser2()),
                             "the pitch of a take with an escaped name");
        if (QTest::currentTestFailed()) return;
        verifyStripMatchesTake();
    }

    // A session from before the takes were stored opens without its
    // singing track, and nothing of one is left in the pane (spec 3)
    void session_without_takes_opens_without_singing() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;

        QString session = m_dir.filePath("old.ton");
        QVERIFY(m_window->saveSessionFile(session));
        // The save copied the take's audio into the session's folder; that
        // copy is what the file names, and what must be left alone below
        QString takePath = m_window->takes()->getAudioPath();

        // As a .ton written before this phase: the same document without
        // the element Tony's own pass reads
        QString stripped = removeTakesElement(session);
        QVERIFY2(stripped == "", qPrintable(stripped));

        reopenSession(session);
        if (QTest::currentTestFailed()) return;

        // No take, and no analyser or layers of one
        QCOMPARE(m_window->takes()->getTakeCount(), 0);
        QVERIFY(!m_window->takes()->haveTake());
        QVERIFY2(!m_window->analyser2(),
                 "the singing track of an old session was set up as a take");
        QVERIFY(!m_window->coverageStrip()->isShown());
        QCOMPARE(allStripLayersInPane0(), 0);
        QCOMPARE(noteLayersInPane0(), 1);       // the reference's
        QCOMPARE(waveformLayersInPane0(), 1);   // the reference's
        QCOMPARE(audioModelsBesidesReference(), 0);
        QCOMPARE(m_window->paneStack()->getPaneCount(), 2);
        verifyPlaySourceClean();
        QVERIFY(!m_window->isDocumentModified());

        // The audio file was not touched, and the reference is intact
        QVERIFY(QFileInfo::exists(takePath));
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           lowHz)) < 10.0);

        // Recording makes the session's first take, as in a new session
        m_window->seekTo(0);
        take(600);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { "Take 1" });
        verifyStripMatchesTake();
    }

    // The audio file of a take is gone when the session is opened: one
    // warning, and the take is shown without its sound (spec 6.4)
    void session_take_with_missing_audio() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot before = snapshotTake();
        QVERIFY(!before.pitch.empty());

        QString session = m_dir.filePath("missing.ton");
        QVERIFY(m_window->saveSessionFile(session));
        // The save copied the take's audio into the session's folder, and
        // that copy is the file the session names (spec 6.4)
        before.path = m_window->takes()->getAudioPath();

        // The session names the take's audio once, in the takes element:
        // the document does not carry the audio model as well (the take's
        // waveform layer is not saved), which the session reader would
        // ask the user to locate when the file has gone
        {
            sv::BZipFileDevice file(session);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QByteArray document = file.readAll();
            file.close();
            // (one wave file model: the reference)
            QCOMPARE(int(document.count("type=\"wavefile\"")), 1);
            QVERIFY(document.contains("<take name=\"Take 1\""));
        }
        m_window->doCloseSession();
        QVERIFY(QFile::remove(before.path));

        m_window->discardModifications();
        QCOMPARE(m_window->openPath(session, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);

        // One warning of ours for the session, naming the folder the takes'
        // audio was expected in rather than one warning per take (spec 6.4)
        QStringList ours = dialogsMatching("without their audio");
        QCOMPARE(ours.size(), 1);
        QVERIFY2(ours[0].contains(QFileInfo(TakesFile::takesFolder(session))
                                  .fileName()),
                 qPrintable(ours[0]));

        // The take is there, with its pitch, its notes and its coverage,
        // and no audio
        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { "Take 1" });
        QCOMPARE(m_window->takes()->getAudioPath(), before.path);
        QCOMPARE(m_window->takes()->getCoverage().getRanges(), before.coverage);
        QVERIFY(!m_window->analyser2());
        QVERIFY(!takeAudio());
        TakeLayers::Found found = takeLayers("Take 1");
        QVERIFY(found.pitch && found.notes && found.coverage);
        verifyEventsSurvived(before.pitch, pitchEvents(found.pitch),
                             "the pitch of a take whose audio is missing");
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->coverageStrip()->isShown());
        QCOMPARE(stripEvents(), before.strip);
        verifyPlaySourceClean();

        // Recording into it is refused, with one warning, and the take is
        // left exactly as it was: splicing needs the file it adds to
        m_window->seekTo(0);
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(400);
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());

        QStringList refusals = dialogsMatching("could not be added");
        QCOMPARE(refusals.size(), 1);
        QVERIFY2(refusals[0].contains(QFileInfo(before.path).fileName()),
                 qPrintable(refusals[0]));
        QVERIFY(takeDialogs().isEmpty());
        QCOMPARE(m_window->takes()->getAudioPath(), before.path);
        QCOMPARE(m_window->takes()->getCoverage().getRanges(), before.coverage);
        QCOMPARE(stripEvents(), before.strip);
    }

    // The reference of a session cannot be read when it is opened (on the
    // phone: a format it has no decoder for), and svapp says the session
    // loaded incomplete. Saved over, its file would lose the reference: it
    // is saved only when the user asks and then says yes, and a save no
    // one asked for (Android's on suspend) passes it by
    void incomplete_session_not_saved_unasked() {
        FakeAudioIO::Config config;
        makeWindow(config);
        QString reference = writeWav(tone(lowHz, 1.0));
        openReference(reference);
        if (QTest::currentTestFailed()) return;

        QString session = m_dir.filePath("no-reference.ton");
        QVERIFY(m_window->doSaveSessionAs(session));
        QVERIFY(!m_window->isSessionIncomplete());
        m_window->markModified();
        QVERIFY2(m_window->doMaySaveUnasked(),
                 "a whole session, changed, with a file of its own, may not "
                 "be saved unasked");
        m_window->doCloseSession();
        QByteArray saved = fileContents(session);
        QVERIFY(!saved.isEmpty());

        QVERIFY(QFile::remove(reference));
        m_window->discardModifications();
        QCOMPARE(m_window->openPath(session, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QCOMPARE(messagesMatching("Incomplete session loaded",
                                  "referred to by the original session "
                                  "file could not be loaded").size(), 1);
        QVERIFY2(m_window->isSessionIncomplete(),
                 "the session is not known to have loaded incomplete");

        // svapp gives such a session no file of its own; nor is it saved
        // unasked if it has one
        QCOMPARE(m_window->sessionFile(), QString());
        m_window->markModified();
        QVERIFY(!m_window->doMaySaveUnasked());
        m_window->setSessionFile(session);
        QVERIFY2(!m_window->doMaySaveUnasked(),
                 "an incomplete session may be saved over its file unasked");

        // Save, over that file: asked, and "no" saves nothing
        m_window->setSaveIncompleteAnswer(false);
        m_window->doSaveSession();
        QCOMPARE(m_window->saveIncompleteQuestions(), 1);
        QCOMPARE(fileContents(session), saved);
        QVERIFY(m_window->isDocumentModified());

        // Save with no file of its own is Save As, which asks before the
        // file is picked (on Android the picker makes the file)
        m_window->setSessionFile("");
        QString other = m_dir.filePath("no-reference-saved.ton");
        m_window->setSaveFileNameAnswer(other);
        m_window->doSaveSession();
        QCOMPARE(m_window->saveIncompleteQuestions(), 2);
        m_window->doSaveSessionAsAsked();
        QCOMPARE(m_window->saveIncompleteQuestions(), 3);
        QCOMPARE(m_window->saveFileNameQuestions(), 0);
        QVERIFY(!QFileInfo::exists(other));
        QVERIFY(m_window->isSessionIncomplete());
        QVERIFY(m_window->isDocumentModified());
        QVERIFY(takeDialogs().isEmpty());
    }

    // The same, with the reference there and other audio the session names
    // missing, so that it can be saved: once the user says yes, the session
    // is what its new file says, and is neither asked about nor passed by
    // again
    void incomplete_session_saved_when_the_user_says_so() {
        FakeAudioIO::Config config;
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        QString session = m_dir.filePath("missing-audio.ton");
        QVERIFY(m_window->doSaveSessionAs(session));
        m_window->doCloseSession();
        QString missing = m_dir.filePath("missing-audio.wav");
        QString error = insertIntoSession
            (session, "</data>",
             QString("<model id=\"999\" name=\"missing-audio.wav\" "
                     "sampleRate=\"44100\" start=\"0\" end=\"44100\" "
                     "type=\"wavefile\" file=\"%1\"/>\n").arg(missing)
             .toUtf8());
        QVERIFY2(error == "", qPrintable(error));
        QByteArray saved = fileContents(session);

        reopenSession(session);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(messagesMatching("Incomplete session loaded",
                                  "referred to by the original session "
                                  "file could not be loaded").size(), 1);
        QVERIFY(m_window->isSessionIncomplete());
        QCOMPARE(m_window->sessionFile(), QString());
        m_window->markModified();
        QVERIFY(!m_window->doMaySaveUnasked());

        // Saved over the file it came from, as the user may choose: which
        // then no longer names the audio that was missing
        m_window->setSaveIncompleteAnswer(true);
        m_window->setSaveFileNameAnswer(session);
        m_window->doSaveSession();
        QCOMPARE(m_window->saveIncompleteQuestions(), 1);
        QCOMPARE(m_window->saveFileNameQuestions(), 1);
        QVERIFY2(fileContents(session) != saved, "the session was not saved");
        QCOMPARE(m_window->sessionFile(), session);
        QVERIFY(!m_window->isDocumentModified());
        QVERIFY(!m_window->isSessionIncomplete());
        {
            sv::BZipFileDevice file(session);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QByteArray document = file.readAll();
            file.close();
            QVERIFY(!document.contains("missing-audio.wav"));
            QCOMPARE(int(document.count("type=\"wavefile\"")), 1);
        }

        m_window->markModified();
        QVERIFY(m_window->doMaySaveUnasked());
        m_window->doSaveSession();
        QCOMPARE(m_window->saveIncompleteQuestions(), 1);
        QCOMPARE(m_window->saveFileNameQuestions(), 1);
        QVERIFY(!m_window->isDocumentModified());
        QVERIFY(takeDialogs().isEmpty());
    }

    // --- The takes folder of a session (spec 6.4) ---
    //
    // A take's combined audio belongs to the song: it lives in
    // "<session>.takes" beside the .ton, which names it relative to
    // itself, so that the two can be moved together.  Before the first
    // save there is no folder, so the files are written among the raw
    // recordings and the save copies them across.

    // The first save copies the take's audio into the session's folder and
    // names it there, relative to the .ton.  The recording it was copied
    // from belongs to nobody afterwards, and goes when the session closes
    void first_save_copies_the_take_audio() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;
        QString recorded = m_window->takes()->getAudioPath();

        QString session = m_dir.filePath("copied.ton");
        QString folder = TakesFile::takesFolder(session);
        QVERIFY(!QFileInfo::exists(folder));

        QVERIFY(m_window->doSaveSessionAs(session));
        QCOMPARE(m_window->sessionFile(), session);

        // The take's audio is the copy in the folder, under the name it had
        QString copied = m_window->takes()->getAudioPath();
        QVERIFY2(TakesFile::isInFolder(folder, copied), qPrintable(copied));
        QCOMPARE(QFileInfo(copied).fileName(), QFileInfo(recorded).fileName());
        QVERIFY(QFileInfo::exists(copied));

        // Copied and not moved: the audio model has the old file open and
        // goes on reading it
        QVERIFY2(QFileInfo::exists(recorded),
                 "the recording was moved out from under the open model");
        QVERIFY(takeAudio());

        // And the file names it relative to itself
        {
            sv::BZipFileDevice file(session);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QByteArray document = file.readAll();
            file.close();
            QByteArray wanted = "audio=\"copied.takes/" +
                QFileInfo(copied).fileName().toUtf8() + "\"";
            QVERIFY2(document.contains(wanted), qPrintable(document));
        }

        // Nothing refers to the recording now, and it is up for deletion
        // when the session closes
        QCOMPARE(m_window->takes()->unusedWrittenFiles(),
                 QStringList { recorded });

        // An undo and a redo after the save still find the files the
        // commands hold: the recordings are there until the session closes.
        // The redo leaves the take pointing outside the folder again, so the
        // next save copies it in again -- beside the copy that is there
        // already, never over it
        QCOMPARE(undoOnce(), QString("Record Singing"));
        QVERIFY(!m_window->takes()->haveTake());
        QCOMPARE(redoOnce(), QString("Record Singing"));
        QCOMPARE(m_window->takes()->getAudioPath(), recorded);

        QVERIFY(m_window->doSaveSessionAs(session));
        QString again = m_window->takes()->getAudioPath();
        QVERIFY2(TakesFile::isInFolder(folder, again), qPrintable(again));
        QVERIFY2(again != copied, "the second copy was written over the first");
        QVERIFY(QFileInfo::exists(copied));

        m_window->doCloseSession();
        QVERIFY2(!QFileInfo::exists(recorded),
                 "the recording the save copied was left behind");
        QVERIFY2(QFileInfo::exists(copied) && QFileInfo::exists(again),
                 "audio that a saved session named was deleted on close");
    }

    // Once the session has a file, the next recording is written straight
    // into its folder
    void record_after_saving_writes_into_the_folder() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;

        QString session = m_dir.filePath("recorded-into.ton");
        QVERIFY(m_window->doSaveSessionAs(session));
        QString folder = TakesFile::takesFolder(session);

        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(600);
        if (QTest::currentTestFailed()) return;

        QString path = m_window->takes()->getAudioPath();
        QVERIFY2(TakesFile::isInFolder(folder, path), qPrintable(path));
        verifyTakeHasSound();
        if (QTest::currentTestFailed()) return;

        // The raw recordings are still where they were: only the combined
        // files moved house
        QVERIFY(QDir(sv::RecordDirectory::getRecordDirectory())
                .entryList(QStringList { "*.wav" }, QDir::Files).size() > 0);
    }

    // Save As to another place copies the takes into the new session's
    // folder and leaves the old one alone: the old .ton is still good
    void save_as_copies_into_the_new_folder() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;

        QString first = m_dir.filePath("first.ton");
        QVERIFY(m_window->doSaveSessionAs(first));
        QString firstAudio = m_window->takes()->getAudioPath();

        QVERIFY(QDir().mkpath(m_dir.filePath("other")));
        QString second = QDir(m_dir.filePath("other")).filePath("second.ton");
        QVERIFY(m_window->doSaveSessionAs(second));
        QString secondAudio = m_window->takes()->getAudioPath();

        QVERIFY2(TakesFile::isInFolder(TakesFile::takesFolder(second),
                                       secondAudio), qPrintable(secondAudio));
        QVERIFY2(QFileInfo::exists(firstAudio),
                 "Save As took the audio of the session it was saved from");

        // Both sessions open, with their own copy of the singing
        for (QString session : { first, second }) {
            reopenSession(session);
            if (QTest::currentTestFailed()) return;
            verifyTakeHasSound();
            if (QTest::currentTestFailed()) return;
            QVERIFY2(TakesFile::isInFolder(TakesFile::takesFolder(session),
                                           m_window->takes()->getAudioPath()),
                     qPrintable(m_window->takes()->getAudioPath()));
        }
    }

    // Two takes sharing one audio file (a duplicate) share the copy of it
    void duplicated_take_copies_its_audio_once() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;
        m_window->doDuplicateTake();
        QCOMPARE(m_window->takes()->getTakeCount(), 2);
        QCOMPARE(m_window->takes()->getTake(1)->audioPath,
                 m_window->takes()->getTake(0)->audioPath);

        QString session = m_dir.filePath("shared.ton");
        QVERIFY(m_window->doSaveSessionAs(session));

        QString folder = TakesFile::takesFolder(session);
        QString copied = m_window->takes()->getTake(0)->audioPath;
        QCOMPARE(m_window->takes()->getTake(1)->audioPath, copied);
        QVERIFY2(TakesFile::isInFolder(folder, copied), qPrintable(copied));
        QCOMPARE(QDir(folder).entryList(QStringList { "*.wav" },
                                        QDir::Files).size(), 1);
    }

    // The .ton and its folder moved together: the paths in the file are
    // relative, so the takes are found in the new place
    void session_folder_moved_as_a_whole() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;

        QString home = m_dir.filePath("in-place");
        QVERIFY(QDir().mkpath(home));
        QString session = QDir(home).filePath("song.ton");
        QVERIFY(m_window->doSaveSessionAs(session));
        m_window->doCloseSession();

        // The .ton and its "song.takes" folder, moved as one
        QString moved = m_dir.filePath("moved-house");
        QVERIFY2(QDir().rename(home, moved), "could not move the session");
        QString movedSession = QDir(moved).filePath("song.ton");

        m_window->discardModifications();
        QCOMPARE(m_window->openPath(movedSession, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);

        verifyTakeHasSound();
        if (QTest::currentTestFailed()) return;
        QVERIFY2(TakesFile::isInFolder(TakesFile::takesFolder(movedSession),
                                       m_window->takes()->getAudioPath()),
                 qPrintable(m_window->takes()->getAudioPath()));
        QVERIFY(!pitchEvents(m_window->analyser2()).empty());
    }

    // The .ton moved without its folder: one warning for the session,
    // naming the folder, and the take shows its pitch and notes with no
    // sound
    void session_moved_without_its_folder() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;
        TakeSnapshot before = snapshotTake();

        QString session = m_dir.filePath("left-behind.ton");
        QVERIFY(m_window->doSaveSessionAs(session));
        m_window->doCloseSession();

        // The file on its own, in a directory with no takes folder
        QString elsewhere = m_dir.filePath("without-folder");
        QVERIFY(QDir().mkpath(elsewhere));
        QString moved = QDir(elsewhere).filePath("left-behind.ton");
        QVERIFY(QFile::copy(session, moved));

        m_window->discardModifications();
        QCOMPARE(m_window->openPath(moved, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);

        // One warning, for the session and not for the take, naming the
        // folder the audio was expected in
        QStringList warnings = dialogsMatching("without their audio");
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings[0].contains("left-behind.takes"),
                 qPrintable(warnings[0]));
        QVERIFY(takeDialogs().isEmpty());

        // The take is there with everything but its sound
        QCOMPARE(m_window->takes()->getTakeNames(), QStringList { "Take 1" });
        QCOMPARE(m_window->takes()->getCoverage().getRanges(), before.coverage);
        QVERIFY(!m_window->analyser2());
        QVERIFY(!takeAudio());
        TakeLayers::Found found = takeLayers("Take 1");
        QVERIFY(found.pitch && found.notes && found.coverage);
        verifyEventsSurvived(before.pitch, pitchEvents(found.pitch),
                             "the pitch of a take whose folder was left "
                             "behind");
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->coverageStrip()->isShown());
    }

    // A copy that cannot be made fails the save: the session is not written
    // at all, and nothing about the takes changes
    void a_failed_copy_fails_the_save() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 6.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 4.0)));
        if (QTest::currentTestFailed()) return;

        take(600);
        if (QTest::currentTestFailed()) return;
        QString firstAudio = m_window->takes()->getAudioPath();

        m_window->doNewEmptyTake();
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        take(600);
        if (QTest::currentTestFailed()) return;
        QString secondAudio = m_window->takes()->getAudioPath();

        QString session = m_dir.filePath("blocked.ton");
        QString folder = TakesFile::takesFolder(session);

        // There is a file where the folder would go, so it cannot be made
        {
            QFile blocker(folder);
            QVERIFY(blocker.open(QIODevice::WriteOnly));
            blocker.write("not a folder");
            blocker.close();
        }

        QVERIFY2(!m_window->saveSessionFile(session),
                 "the session was saved although its takes' audio could not "
                 "be put beside it");
        QCOMPARE(dialogsMatching("was not saved").size(), 1);
        QVERIFY2(!QFileInfo::exists(session),
                 "a session file was written that names audio which is not "
                 "in its folder");
        QCOMPARE(m_window->takes()->getTake(0)->audioPath, firstAudio);
        QCOMPARE(m_window->takes()->getTake(1)->audioPath, secondAudio);

        // Now the folder can be made, but the audio of the take that is not
        // on show has gone from under us: the copy of the first take, which
        // was made before the failure, is taken back again and the folder
        // made for them is left empty
        QVERIFY(QFile::remove(folder));
        QVERIFY(m_window->doSwitchToTake(0));
        QVERIFY2(QFile::remove(secondAudio),
                 "the audio of the take that was put away is still open");

        QVERIFY2(!m_window->saveSessionFile(session),
                 "the session was saved although one take's audio was gone");
        QCOMPARE(dialogsMatching("was not saved").size(), 1);
        QVERIFY(!QFileInfo::exists(session));
        QCOMPARE(m_window->takes()->getTake(0)->audioPath, firstAudio);
        QCOMPARE(m_window->takes()->getTake(1)->audioPath, secondAudio);
        QVERIFY(QDir(folder).exists());
        QVERIFY2(QDir(folder).isEmpty(),
                 "the copy made before the failure was left in the folder");

        // An empty folder this run made goes with the session
        m_window->doCloseSession();
        QVERIFY2(!QFileInfo::exists(folder),
                 "the empty takes folder was left behind");
    }

    // Saving while the analysis of a recorded range runs: the save waits
    // for the merge, so the session holds the take's own pitch and notes
    // and neither the state before the merge nor the run's two temporary
    // models
    void save_during_ranged_analysis() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        // Held, however quickly pYIN analyses the range, until the save
        // runs the event loop
        m_window->holdRangedMerges(true);

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(700);

        // Stop splices the recording in and starts the analysis of it there
        // and then, so it is running when the session is saved
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY2(m_window->analysingRange(),
                 "the test shows nothing: no analysis was running when the "
                 "session was saved");

        // Let go from the event loop, which only a save that waits runs
        QTimer::singleShot(0, m_window, [this]() {
            m_window->holdRangedMerges(false);
        });
        QString session = m_dir.filePath("mid-analysis.ton");
        QVERIFY(m_window->saveSessionFile(session));

        QVERIFY2(!m_window->analysingRange(),
                 "the save did not wait for the analysis of the recording");
        TakeSnapshot before = snapshotTake();
        QVERIFY(!before.pitch.empty());

        reopenSession(session);
        if (QTest::currentTestFailed()) return;

        // The merged pitch and notes are what the session holds, in the
        // take's own layers and no others
        QVERIFY(m_window->analyser2());
        verifyEventsSurvived(before.pitch, pitchEvents(m_window->analyser2()),
                             "the pitch saved during an analysis");
        verifyEventsSurvived(before.notes,
                             noteEvents(m_window->analyser2()
                                        ->getLayer(Analyser::Notes)),
                             "the notes saved during an analysis");
        if (QTest::currentTestFailed()) return;
        QCOMPARE(noteLayersInPane0(), 2);
        QCOMPARE(audioModelsBesidesReference(), 1);
        verifyStripMatchesTake();
        verifyPlaySourceClean();
    }

    // The alternate pitch track: the reference pitch track moved by
    // whole octaves, as a layer of its own

    void alternate_pitch_layer_names() {
        int octaves = 99;
        QVERIFY(AlternatePitchTrack::octavesFromLayerName
                (AlternatePitchTrack::layerNameFor(-2), octaves));
        QCOMPARE(octaves, -2);
        QVERIFY(AlternatePitchTrack::octavesFromLayerName
                (AlternatePitchTrack::layerNameFor(3), octaves));
        QCOMPARE(octaves, 3);
        // zero is the reference itself, and the rest are not ours
        for (QString name : { AlternatePitchTrack::layerNameFor(0),
                              AlternatePitchTrack::layerNameFor(4),
                              QString("Alternate Pitch Track"),
                              QString("Alternate Pitch Track x"),
                              QString("Pitch Track -1") }) {
            QVERIFY2(!AlternatePitchTrack::octavesFromLayerName(name, octaves),
                     qPrintable(name));
        }
        QCOMPARE(octaves, 3);
        QCOMPARE(AlternatePitchTrack::shifted(220.0, -1), 110.0);
        QCOMPARE(AlternatePitchTrack::shifted(220.0, 2), 880.0);
    }

    void alternate_pitch_track() {
        makeWindow(FakeAudioIO::Config());
        QVERIFY(!m_window->alternatePitchAction()->isEnabled());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        AlternatePitchTrack *alt = m_window->alternatePitch();
        QVERIFY(m_window->alternatePitchAction()->isEnabled());
        QVERIFY(!m_window->alternatePitchAction()->isChecked());
        QVERIFY(!m_window->alternatePitchUpAction()->isEnabled());
        QVERIFY(!alt->isShown());
        m_window->discardModifications();

        m_window->doToggleAlternatePitch();
        QVERIFY(alt->isShown());
        QVERIFY(m_window->alternatePitchAction()->isChecked());
        QVERIFY(m_window->alternatePitchUpAction()->isEnabled());
        QVERIFY(m_window->isDocumentModified());
        QCOMPARE(alt->getOctaves(), -1);
        QVERIFY(paneHasLayer(0, alt->getLayer()));
        QCOMPARE(colourOf(alt->getLayer()), colourNamed("Faded Brown"));
        QVERIFY(alternateMatchesReference());
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(alt->getLayer())),
                           lowHz / 2)) < 10.0);

        // never heard, and never the layer that gets edited
        QVERIFY(!alt->getLayer()->getPlayParameters()->isPlayAudible());
        QVERIFY(m_window->paneStack()->getPane(0)->getSelectedLayer()
                != alt->getLayer());

        // up from one below is one above: none is the reference itself
        m_window->discardModifications();
        m_window->doStepAlternatePitch(true);
        QCOMPARE(alt->getOctaves(), 1);
        QVERIFY(m_window->isDocumentModified());
        QVERIFY(alternateMatchesReference());
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(alt->getLayer())),
                           lowHz * 2)) < 10.0);

        m_window->doStepAlternatePitch(true);
        m_window->doStepAlternatePitch(true);
        m_window->doStepAlternatePitch(true);
        QCOMPARE(alt->getOctaves(), int(AlternatePitchTrack::maxOctaves));
        QVERIFY(!m_window->alternatePitchUpAction()->isEnabled());
        QVERIFY(m_window->alternatePitchDownAction()->isEnabled());

        // the reference was left alone
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           lowHz)) < 10.0);

        sv::Layer *layer = alt->getLayer();
        m_window->doToggleAlternatePitch();
        QVERIFY(!alt->isShown());
        QVERIFY(!documentHasLayer(layer));
        QCOMPARE(alternateLayersInDocument(), 0);
        verifyPlaySourceClean();

        // and it comes back where it was
        m_window->doToggleAlternatePitch();
        QCOMPARE(alt->getOctaves(), int(AlternatePitchTrack::maxOctaves));
        QVERIFY(alternateMatchesReference());
    }

    // Analyse Now gives the reference a new pitch layer and model
    void alternate_pitch_follows_reanalysis() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        AlternatePitchTrack *alt = m_window->alternatePitch();
        m_window->doToggleAlternatePitch();
        sv::ModelId before = alt->getSource();
        QVERIFY(!before.isNone());

        m_window->doAnalyseNow();
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser()), 30000);

        QVERIFY(alt->getSource() != before);
        QCOMPARE(alt->getSource(),
                 m_window->analyser()->getLayer(Analyser::PitchTrack)
                 ->getModel());
        QTRY_VERIFY_WITH_TIMEOUT(alternateMatchesReference(), 5000);

        // and an edit to the reference reaches it
        auto ref = pitchEvents(m_window->analyser());
        m_window->analyser()->shiftOctave
            (sv::Selection(ref.front().getFrame(),
                           ref.back().getFrame() + 1), true);
        QTRY_VERIFY_WITH_TIMEOUT
            (std::fabs(TestSignals::centsBetween
                       (medianHz(pitchEvents(alt->getLayer())), lowHz)) < 10.0,
             5000);
        QVERIFY(alternateMatchesReference());
    }

    void alternate_pitch_followed_during_take() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        AlternatePitchTrack *alt = m_window->alternatePitch();
        m_window->doToggleAlternatePitch();
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        sv::Layer *reference =
            m_window->analyser()->getLayer(Analyser::PitchTrack);
        QVERIFY(!reference->isLayerDormant(pane));

        startTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(colourOf(alt->getLayer()), colourNamed("Dark Brown"));
        QVERIFY(!alt->getLayer()->isLayerDormant(pane));
        QVERIFY(reference->isLayerDormant(pane));
        QVERIFY(!m_window->alternatePitchAction()->isEnabled());
        QVERIFY(!m_window->alternatePitchDownAction()->isEnabled());
        QTest::qWait(600);
        QVERIFY(reference->isLayerDormant(pane));

        stopTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(colourOf(alt->getLayer()), colourNamed("Faded Brown"));
        QVERIFY(!reference->isLayerDormant(pane));
        QVERIFY(m_window->alternatePitchAction()->isEnabled());
        QVERIFY(alternateMatchesReference());

        // The setting that Show Pitch Track keeps was not touched
        QVERIFY(m_window->analyser()->isVisible(Analyser::PitchTrack));
        QSettings settings;
        settings.beginGroup("Analyser");
        QVERIFY(settings.value
                (QString("visible-%1").arg(int(Analyser::PitchTrack)),
                 true).toBool());
        settings.endGroup();

        // A take with no alternate track hides nothing
        m_window->doToggleAlternatePitch();
        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(!reference->isLayerDormant(pane));
        waitForSomethingRecorded();
        stopTake();
    }

    void alternate_pitch_session_round_trip() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        AlternatePitchTrack *alt = m_window->alternatePitch();
        m_window->doToggleAlternatePitch();
        m_window->doStepAlternatePitch(false);
        QCOMPARE(alt->getOctaves(), -2);

        QString session = m_dir.filePath("alternate.ton");
        QVERIFY(m_window->saveSessionFile(session));
        m_window->doCloseSession();
        QVERIFY(!alt->isShown());

        // A session without one must not be given the last one's
        openReference(writeWav(tone(highHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(!alt->isShown());
        QCOMPARE(alternateLayersInDocument(), 0);

        openReference(session);
        if (QTest::currentTestFailed()) return;
        QVERIFY(alt->isShown());
        QCOMPARE(alt->getOctaves(), -2);
        QVERIFY(m_window->alternatePitchAction()->isChecked());
        QCOMPARE(alternateLayersInDocument(), 1);
        QVERIFY(paneHasLayer(0, alt->getLayer()));
        QCOMPARE(colourOf(alt->getLayer()), colourNamed("Faded Brown"));
        QVERIFY(!alt->getLayer()->getPlayParameters()->isPlayAudible());
        QTRY_VERIFY_WITH_TIMEOUT(alternateMatchesReference(), 5000);
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(alt->getLayer())),
                           lowHz / 4)) < 10.0);

        // The reference is still the black one, and still the one edited
        QCOMPARE(colourOf(m_window->analyser()->getLayer(Analyser::PitchTrack)),
                 colourNamed("Black"));
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           lowHz)) < 10.0);
        QVERIFY(m_window->paneStack()->getPane(0)->getSelectedLayer()
                != alt->getLayer());
    }

    // Timed lyrics: an LRC file imported onto the reference's timeline,
    // drawn along the bottom of pane 0 by LyricsTrack's layer

    void lyrics_import_shows_words() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        QString path = lyricsFixture("moises-exporter-words.lrc");
        QVERIFY(m_window->doImportLyricsFrom(path));

        LyricsTrack *lyrics = m_window->lyrics();
        QVERIFY(lyrics->isShown());
        QVERIFY(lyrics->isVisible());
        QCOMPARE(lyricsLayersInDocument(), 1);
        QCOMPARE(lyricsLayersInPane0(), 1);
        sv::RegionLayer *layer = lyrics->getLayer();
        QVERIFY(paneHasLayer(0, layer));
        QCOMPARE(int(layer->getPlotStyle()), int(sv::RegionLayer::PlotLyrics));
        QCOMPARE(int(layer->getVerticalScale()),
                 int(sv::RegionLayer::EqualSpaced));
        QCOMPARE(colourOf(layer), colourNamed("Grey"));
        QVERIFY(!layer->isLayerEditable());
        QCOMPARE(layer->getLayerPresentationName(),
                 QString("Kesäyön testilaulu"));

        // Every word, at its frame, with its label
        sv::EventVector expected = expectedLyricsEvents(path);
        QCOMPARE(int(expected.size()), 13);
        QCOMPARE(lyricsEvents(), expected);

        Lyrics parsed = lyricsIn(path);
        QString counts = QString("Imported %1 words in %2 lines.")
            .arg(parsed.words.size()).arg(parsed.lineCount());
        QVERIFY2(m_window->statusText().startsWith(counts),
                 qPrintable(m_window->statusText()));

        QVERIFY(m_window->removeLyricsAction()->isEnabled());
        QVERIFY(m_window->showLyricsAction()->isEnabled());
        QVERIFY(m_window->showLyricsAction()->isChecked());
    }

    // Like loading background music: not undoable, and nothing goes onto
    // the undo stack or comes off it, but the session has changed
    void lyrics_import_leaves_history_alone() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        auto *history = sv::CommandHistory::getInstance();
        history->addCommand(new sv::GenericCommand
                            ("Earlier Edit", []() {}, []() {}), false);
        m_window->discardModifications();
        QSignalSpy commands(history, qOverload<>
                            (&sv::CommandHistory::commandExecuted));

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        QCOMPARE(int(commands.count()), 0);
        QVERIFY(m_window->isDocumentModified());

        sv::Layer *layer = m_window->lyrics()->getLayer();
        QCOMPARE(undoOnce(), QString("Earlier Edit"));
        QCOMPARE(undoOnce(), QString());
        QVERIFY(m_window->lyrics()->isShown());
        QVERIFY(m_window->lyrics()->getLayer() == layer);
        QVERIFY(paneHasLayer(0, layer));
    }

    // The play source takes in the model of every layer in a view, and
    // the last end of them is where playback ends
    void lyrics_do_not_extend_playback() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        sv::AudioCallbackPlaySource *playSource = m_window->playSource();
        sv::sv_frame_t playEnd = playSource->getPlayEndFrame();
        QVERIFY(playEnd > 0);

        // Two words within the second of reference, and one well after it
        QVERIFY(m_window->doImportLyricsFrom
                (writeLrc("[00:00.20]<00:00.20>Yksi <00:00.60>kaksi\n"
                          "[00:03.00]<00:03.00>Kolme\n")));
        sv::ModelId model = m_window->lyrics()->getModelId();
        QVERIFY(!model.isNone());
        QVERIFY2(sv::ModelById::get(model)->getEndFrame() > playEnd,
                 "the lyrics end within the reference: this shows nothing");

        QVERIFY2(playSource->getModels().count(model) == 0,
                 "the play source holds the lyrics");
        // Not equal: taking a model out works the end out again from the
        // models' ends as they are now, and one of pYIN's may be shorter
        // than it was when it came in
        QVERIFY2(playSource->getPlayEndFrame() <= playEnd,
                 qPrintable(QString("playback ends at frame %1, later than "
                                    "%2 before the import")
                            .arg(playSource->getPlayEndFrame()).arg(playEnd)));
        verifyPlaySourceClean();

        QVERIFY2(m_window->statusText().contains
                 ("1 word starts after the end of the reference."),
                 qPrintable(m_window->statusText()));

        // The same once the session is opened again: the load puts the
        // model of every layer it adds to a view into the play source
        QString session = m_dir.filePath("lyrics-past-the-end.ton");
        QVERIFY(m_window->saveSessionFile(session));
        reopenSession(session);
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->lyrics()->isShown());
        model = m_window->lyrics()->getModelId();
        QVERIFY(!model.isNone());
        sv::sv_frame_t lyricsEnd = sv::ModelById::get(model)->getEndFrame();
        playSource = m_window->playSource();
        QVERIFY2(playSource->getModels().count(model) == 0,
                 "the play source holds the lyrics of the session");
        QVERIFY2(playSource->getPlayEndFrame() < lyricsEnd,
                 qPrintable(QString("playback ends at frame %1, with the "
                                    "lyrics, which end at %2")
                            .arg(playSource->getPlayEndFrame())
                            .arg(lyricsEnd)));
        verifyPlaySourceClean();
    }

    void lyrics_reimport_replaces() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        LyricsTrack *lyrics = m_window->lyrics();
        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::ModelId first = lyrics->getModelId();

        QString path = lyricsFixture("lrc-with-ends.lrc");
        QVERIFY(m_window->doImportLyricsFrom(path));
        QCOMPARE(lyricsLayersInDocument(), 1);
        QCOMPARE(lyricsLayersInPane0(), 1);
        QVERIFY(lyrics->getModelId() != first);
        QCOMPARE(lyricsEvents(), expectedLyricsEvents(path));
        QCOMPARE(lyrics->getLayer()->getLayerPresentationName(),
                 QString("Päivä [Live]"));

        // A model id is never used twice, unlike the address of a layer
        QVERIFY2(!sv::ModelById::get(first),
                 "the first lyrics' model was not released");
        QVERIFY(m_window->document()->getModels().count(first) == 0);
        verifyPlaySourceClean();
    }

    void lyrics_remove() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        LyricsTrack *lyrics = m_window->lyrics();
        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::ModelId model = lyrics->getModelId();
        m_window->discardModifications();

        m_window->removeLyricsAction()->trigger();
        QVERIFY(!lyrics->isShown());
        QCOMPARE(lyricsLayersInDocument(), 0);
        QVERIFY2(!sv::ModelById::get(model), "the lyrics model was not released");
        QVERIFY(m_window->document()->getModels().count(model) == 0);
        QVERIFY(m_window->isDocumentModified());
        QCOMPARE(undoOnce(), QString());
        verifyPlaySourceClean();

        QVERIFY(!m_window->removeLyricsAction()->isEnabled());
        QVERIFY(!m_window->showLyricsAction()->isEnabled());
        QVERIFY(!m_window->showLyricsAction()->isChecked());
        QVERIFY(m_window->importLyricsAction()->isEnabled());
    }

    // Hidden, not removed; not in the settings, and no command
    void lyrics_show_toggle() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        LyricsTrack *lyrics = m_window->lyrics();
        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::Layer *layer = lyrics->getLayer();
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        m_window->discardModifications();
        auto *history = sv::CommandHistory::getInstance();
        QSignalSpy commands(history, qOverload<>
                            (&sv::CommandHistory::commandExecuted));

        QAction *show = m_window->showLyricsAction();
        show->trigger();
        QVERIFY(layer->isLayerDormant(pane));
        QVERIFY(!lyrics->isVisible());
        QVERIFY(!show->isChecked());
        QVERIFY(show->isEnabled());
        QVERIFY(lyrics->getLayer() == layer);
        QVERIFY(paneHasLayer(0, layer));
        QVERIFY(m_window->isDocumentModified());

        show->trigger();
        QVERIFY(!layer->isLayerDormant(pane));
        QVERIFY(lyrics->isVisible());
        QVERIFY(show->isChecked());

        QCOMPARE(int(commands.count()), 0);
        QCOMPARE(undoOnce(), QString());
        for (const QString &key : QSettings().allKeys()) {
            QVERIFY2(!key.contains("lyrics", Qt::CaseInsensitive),
                     qPrintable("in the settings: " + key));
        }
    }

    // The pane takes its hover readout and its vertical scale from its
    // top layer, and the lyrics have neither
    void lyrics_keep_top_layer() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        sv::Pane *pane = m_window->paneStack()->getPane(0);
        sv::Layer *top = pane->getTopLayer();
        QVERIFY(top);
        sv::Layer *reference =
            m_window->analyser()->getLayer(Analyser::PitchTrack);
        double r0 = 0, r1 = 0;
        bool referenceScale = reference->getDisplayExtents(r0, r1);

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::Layer *layer = m_window->lyrics()->getLayer();
        QVERIFY2(pane->getTopLayer() == top,
                 "the lyrics are the pane's top layer");
        QVERIFY(pane->getLayer(pane->getLayerCount() - 2) == layer);

        double s0 = 0, s1 = 0;
        QCOMPARE(reference->getDisplayExtents(s0, s1), referenceScale);
        QCOMPARE(s0, r0);
        QCOMPARE(s1, r1);
    }

    // The layer over the lyrics can go while they stay: the alternate
    // pitch track when it is turned off, a take's layers when the take
    // is deleted.  The lyrics must not be left on top then either
    void lyrics_stay_under_top_when_the_layer_above_goes() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        m_window->doToggleAlternatePitch();
        QVERIFY(m_window->alternatePitch()->isShown());
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        QVERIFY(pane->getTopLayer() == m_window->alternatePitch()->getLayer());
        sv::Layer *under = pane->getLayer(pane->getLayerCount() - 2);

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::Layer *layer = m_window->lyrics()->getLayer();
        QVERIFY(pane->getLayer(pane->getLayerCount() - 2) == layer);

        // The alternate track's layer is deleted, and the lyrics were
        // just under it
        m_window->doToggleAlternatePitch();
        QVERIFY(!m_window->alternatePitch()->isShown());
        QTRY_VERIFY2(pane->getTopLayer() != layer,
                     "the lyrics were left the pane's top layer");
        QVERIFY2(pane->getTopLayer() == under,
                 "the layer that was under the lyrics is not on top");
        QVERIFY(pane->getLayer(pane->getLayerCount() - 2) == layer);
        QVERIFY(m_window->lyrics()->isShown());
        QVERIFY(m_window->lyrics()->isVisible());
    }

    // One dialog each, and nothing changes: not even lyrics that are there
    void lyrics_import_failure() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        m_window->discardModifications();

        LyricsTrack *lyrics = m_window->lyrics();
        QString untimed = writeLrc("Just some words\nwith no times at all\n");
        QVector<QPair<QString, QString>> failures {
            { untimed, "No timed lyrics were found" },
            { m_dir.filePath("no-such-lyrics.lrc"), "could not be found" },
            { writeLrc(QByteArray(int(Lyrics::maxFileBytes) + 1, 'a')),
              "over 1 MB, too big to be a lyrics file" },
            { writeLrc("<tt>\n<body><p begin=\"0:01.000\">Sana</body>\n</tt>\n",
                       "ttml"),
              "not well-formed XML" },
        };
        for (const auto &f : failures) {
            QVERIFY2(!m_window->doImportLyricsFrom(f.first),
                     qPrintable(f.first));
            QStringList dialogs =
                messagesMatching("Could not import lyrics", f.second);
            QCOMPARE(dialogs.size(), 1);
            QVERIFY2(dialogs[0].contains(f.second), qPrintable(dialogs[0]));
            QVERIFY(!lyrics->isShown());
            QCOMPARE(lyricsLayersInDocument(), 0);
            QVERIFY(!m_window->isDocumentModified());
        }

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::ModelId model = lyrics->getModelId();
        sv::EventVector events = lyricsEvents();
        m_window->discardModifications();

        QVERIFY(!m_window->doImportLyricsFrom(untimed));
        QCOMPARE(messagesMatching("Could not import lyrics",
                                  "No timed lyrics were found").size(), 1);
        QVERIFY(lyrics->getModelId() == model);
        QCOMPARE(lyricsEvents(), events);
        QCOMPARE(lyricsLayersInDocument(), 1);
        QVERIFY(!m_window->isDocumentModified());
    }

    // File > Import Lyrics..., with the file dialog answered from here
    void lyrics_import_through_the_menu() {
        makeWindow(FakeAudioIO::Config());
        QAction *import = m_window->importLyricsAction();
        QVERIFY2(!import->isEnabled(), "there is no reference yet");
        QVERIFY(!m_window->removeLyricsAction()->isEnabled());
        QVERIFY(!m_window->showLyricsAction()->isEnabled());

        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(import->isEnabled());

        // Line timing, as the exporter writes it
        QString path = lyricsFixture("moises-exporter-lines.lrc");
        m_window->setLyricsFileAnswer(path);
        import->trigger();
        QCOMPARE(m_window->lyricsFileQuestions(), 1);
        QVERIFY(m_window->lyrics()->isShown());
        QCOMPARE(lyricsEvents(), expectedLyricsEvents(path));
        QVERIFY(m_window->isDocumentModified());
    }

    void lyrics_import_cancelled() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        LyricsTrack *lyrics = m_window->lyrics();
        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::ModelId model = lyrics->getModelId();
        sv::EventVector events = lyricsEvents();
        m_window->discardModifications();

        m_window->setLyricsFileAnswer("");
        m_window->importLyricsAction()->trigger();
        QCOMPARE(m_window->lyricsFileQuestions(), 1);
        QVERIFY(lyrics->getModelId() == model);
        QCOMPARE(lyricsEvents(), events);
        QCOMPARE(lyricsLayersInDocument(), 1);
        QVERIFY(!m_window->isDocumentModified());
    }

    // The singer is reading the lyrics there are
    void lyrics_import_disabled_while_recording() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        QAction *import = m_window->importLyricsAction();
        QVERIFY(import->isEnabled());
        QString path = lyricsFixture("moises-exporter-words.lrc");
        m_window->setLyricsFileAnswer(path);

        startTake();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(!import->isEnabled(), 2000);
        import->trigger();
        QCOMPARE(m_window->lyricsFileQuestions(), 0);
        QVERIFY(!m_window->doImportLyricsFrom(path));
        QVERIFY(!m_window->lyrics()->isShown());

        waitForSomethingRecorded();
        stopTake();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(import->isEnabled(), 2000);
    }

    // TTML as the exporter writes it word by word: every word ends where
    // the file says, not where the next one starts
    void lyrics_import_ttml() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        m_window->discardModifications();

        QString path = lyricsFixture("moises-exporter-words.ttml");
        QVERIFY(m_window->doImportLyricsFrom(path));
        QVERIFY(m_window->lyrics()->isShown());
        QVERIFY(m_window->isDocumentModified());
        QCOMPARE(m_window->lyrics()->getLayer()->getLayerPresentationName(),
                 QString("Lyrics"));

        sv::EventVector expected = expectedLyricsEvents(path);
        QCOMPARE(int(expected.size()), 12);
        sv::EventVector events = lyricsEvents();
        QCOMPARE(events, expected);

        // "Tämä" is sung from 0.52 s to 0.80 s and "on" starts at 0.84 s,
        // where an inferred end would be.  The syllables of "keksitty" are
        // one word, and the second <p> is the second line
        auto frameAt = [](double seconds) {
            return sv::sv_frame_t(std::llround(seconds * rate));
        };
        QCOMPARE(events[0].getLabel(), QString("Tämä"));
        QCOMPARE(events[0].getFrame(), frameAt(0.52));
        QCOMPARE(events[0].getFrame() + events[0].getDuration(), frameAt(0.80));
        QCOMPARE(events[1].getFrame(), frameAt(0.84));
        QCOMPARE(events[2].getLabel(), QString("keksitty"));
        QCOMPARE(events[2].getFrame(), frameAt(1.10));
        QCOMPARE(events[2].getFrame() + events[2].getDuration(), frameAt(1.90));
        QCOMPARE(events[3].getValue(), 0.f);
        QCOMPARE(events[4].getLabel(), QString("Yö"));
        QCOMPARE(events[4].getValue(), 1.f);

        QString status = m_window->statusText();
        QVERIFY2(status.startsWith("Imported 12 words in 3 lines."),
                 qPrintable(status));
    }

    // File > Export Lyrics... writes the words as the model holds them
    // now, not as the file they came from had them, as TTML that reads
    // back to the same words.  Not a change to the session
    void lyrics_export() {
        makeWindow(FakeAudioIO::Config());
        QString reference = writeWav(tone(lowHz, 1.0));
        openReference(reference);
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        auto model = sv::ModelById::getAs<sv::RegionModel>
            (m_window->lyrics()->getModelId());
        QVERIFY(model);

        // One word changed in the model since: its text, and its end
        sv::EventVector imported = lyricsEvents();
        auto word = std::find_if(imported.begin(), imported.end(),
                                 [](const sv::Event &e) {
                                     return e.getLabel() == "hämärä";
                                 });
        QVERIFY(word != imported.end());
        model->remove(*word);
        model->add(word->withLabel("hämärämpi")
                   .withDuration(word->getDuration() / 2));
        sv::EventVector events = lyricsEvents();
        QVERIFY(events != imported);

        m_window->discardModifications();
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));

        QString path = m_dir.filePath("exported-lyrics.ttml");
        m_window->setLyricsExportAnswer(path);
        QVERIFY(m_window->exportLyricsAction()->isEnabled());
        m_window->exportLyricsAction()->trigger();
        QCOMPARE(m_window->lyricsExportQuestions(), 1);

        // The name offered is the reference's, beside it
        QFileInfo info(reference);
        QCOMPARE(m_window->lyricsExportSuggestion(),
                 QDir(info.absolutePath())
                 .filePath(info.completeBaseName() + ".ttml"));

        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), "nothing was written");
        LyricsParseResult parsed = parseTtml(file.readAll());
        QVERIFY2(parsed.error == "", qPrintable(parsed.error));

        // The same words, texts and lines, at the same times to the
        // nearest millisecond, and the title the layer was named after
        Lyrics now = lyricsFromEvents(events, rate);
        const QVector<LyricWord> &back = parsed.lyrics.words;
        QCOMPARE(back.size(), now.words.size());
        for (int i = 0; i < back.size(); ++i) {
            QString what = QString("word %1, \"%2\"").arg(i)
                .arg(now.words[i].text);
            QVERIFY2(back[i].text == now.words[i].text,
                     qPrintable(what + " came back as " + back[i].text));
            QVERIFY2(back[i].line == now.words[i].line,
                     qPrintable(what + ": another line"));
            QVERIFY2(std::fabs(back[i].start - now.words[i].start) < 0.0005001,
                     qPrintable(what + ": another start"));
            QVERIFY2(std::fabs(back[i].end - now.words[i].end) < 0.0005001,
                     qPrintable(what + ": another end"));
        }
        QStringList texts;
        for (const LyricWord &w : back) texts << w.text;
        QVERIFY2(texts.contains("hämärämpi") && !texts.contains("hämärä"),
                 qPrintable(texts.join(" ")));
        QCOMPARE(parsed.lyrics.title, QString("Kesäyön testilaulu"));

        QCOMPARE(m_window->statusText(),
                 QString("Exported 13 words in %1 lines.")
                 .arg(now.lineCount()));
        QCOMPARE(int(commands.count()), 0);
        QVERIFY(!m_window->isDocumentModified());
        QCOMPARE(lyricsEvents(), events);
    }

    // Only when there are lyrics, shown or hidden
    void lyrics_export_needs_lyrics() {
        makeWindow(FakeAudioIO::Config());
        QAction *exportAction = m_window->exportLyricsAction();
        QVERIFY2(!exportAction->isEnabled(), "there is no reference yet");

        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY2(!exportAction->isEnabled(), "there are no lyrics yet");

        QString lrc = lyricsFixture("moises-exporter-words.lrc");
        QVERIFY(m_window->doImportLyricsFrom(lrc));
        QVERIFY(exportAction->isEnabled());
        m_window->showLyricsAction()->trigger();
        QVERIFY(!m_window->lyrics()->isVisible());
        QVERIFY2(exportAction->isEnabled(), "hidden lyrics are lyrics too");
        m_window->showLyricsAction()->trigger();

        m_window->removeLyricsAction()->trigger();
        QVERIFY(!m_window->lyrics()->isShown());
        QVERIFY(!exportAction->isEnabled());
        QString path = m_dir.filePath("removed-lyrics.ttml");
        m_window->setLyricsExportAnswer(path);
        exportAction->trigger();
        QCOMPARE(m_window->lyricsExportQuestions(), 0);
        QVERIFY(!m_window->doExportLyricsTo(path));
        QVERIFY(!QFileInfo::exists(path));

        QVERIFY(m_window->doImportLyricsFrom(lrc));
        QVERIFY(exportAction->isEnabled());
        m_window->doCloseSession();
        QVERIFY2(!exportAction->isEnabled(), "the session has gone");
    }

    void lyrics_export_cancelled() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        QString status = m_window->statusText();

        const QStringList ttml { "*.ttml" };
        QStringList before = QDir(m_dir.path()).entryList(ttml, QDir::Files);
        m_window->setLyricsExportAnswer("");
        m_window->exportLyricsAction()->trigger();
        QCOMPARE(m_window->lyricsExportQuestions(), 1);
        QVERIFY(m_window->lyricsExportSuggestion() != "");
        QVERIFY2(!QFileInfo::exists(m_window->lyricsExportSuggestion()),
                 "written where the dialog would have suggested");
        QCOMPARE(QDir(m_dir.path()).entryList(ttml, QDir::Files), before);
        QCOMPARE(m_window->statusText(), status);
    }

    // One dialog each, and nothing written or changed
    void lyrics_export_failure() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::EventVector events = lyricsEvents();
        QString status = m_window->statusText();
        m_window->discardModifications();

        // Not a file made read-only: root can write to that.  A folder
        // that is not there, and the name of a folder
        QString noFolder = m_dir.filePath("no-such-folder/lyrics.ttml");
        QString folder = m_dir.filePath("a-folder.ttml");
        QVERIFY(QDir().mkpath(folder));

        for (QString path : { noFolder, folder }) {
            m_window->setLyricsExportAnswer(path);
            m_window->exportLyricsAction()->trigger();
            QStringList dialogs =
                messagesMatching("Could not export lyrics", path);
            QCOMPARE(dialogs.size(), 1);
            QVERIFY2(dialogs[0].contains(path), qPrintable(dialogs[0]));
            QCOMPARE(m_window->statusText(), status);
            QVERIFY(!m_window->isDocumentModified());
            QCOMPARE(lyricsEvents(), events);
        }
        QCOMPARE(m_window->lyricsExportQuestions(), 2);
        QVERIFY(!QFileInfo::exists(m_dir.filePath("no-such-folder")));
        QVERIFY(QFileInfo(folder).isDir());
        QVERIFY(QDir(folder).isEmpty());
    }

    // Saved with the session and found again by name when it is opened,
    // as it was: hidden if it was hidden
    void lyrics_session_round_trip() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        LyricsTrack *lyrics = m_window->lyrics();
        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::EventVector before = lyricsEvents();
        QCOMPARE(int(before.size()), 13);
        m_window->showLyricsAction()->trigger();
        QVERIFY(!lyrics->isVisible());

        QString session = m_dir.filePath("lyrics-hidden.ton");
        QVERIFY(m_window->saveSessionFile(session));
        reopenSession(session);
        if (QTest::currentTestFailed()) return;

        // One layer, and LyricsTrack has it
        QVERIFY2(lyrics->isShown(), "the lyrics of the session were not adopted");
        QCOMPARE(lyricsLayersInDocument(), 1);
        QCOMPARE(lyricsLayersInPane0(), 1);
        sv::RegionLayer *layer = lyrics->getLayer();
        QCOMPARE(static_cast<sv::Layer *>(layer), lyricsLayerInPane0());

        // Every word at its frame and for as long, and its label letter for
        // letter: the file is UTF-8, and a label is escaped in it
        sv::EventVector after = lyricsEvents();
        verifyEventsSurvived(before, after, "the lyrics");
        if (QTest::currentTestFailed()) return;
        QStringList labels;
        for (size_t i = 0; i < after.size(); ++i) {
            QCOMPARE(after[i].getLabel(), before[i].getLabel());
            labels << after[i].getLabel();
        }
        QVERIFY2(labels.contains(QString("Tämä")) &&
                 labels.contains(QString("hämärä")) &&
                 labels.contains(QString("Yö")), qPrintable(labels.join(" ")));

        // Still hidden, and the menu says so
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        QVERIFY(layer->isLayerDormant(pane));
        QVERIFY(!lyrics->isVisible());
        QVERIFY(m_window->showLyricsAction()->isEnabled());
        QVERIFY(!m_window->showLyricsAction()->isChecked());
        QVERIFY(m_window->removeLyricsAction()->isEnabled());

        // Called what the file's [ti:] tag says, and drawn as before
        QCOMPARE(layer->getLayerPresentationName(),
                 QString("Kesäyön testilaulu"));
        QCOMPARE(int(layer->getPlotStyle()), int(sv::RegionLayer::PlotLyrics));
        QCOMPARE(colourOf(layer), colourNamed("Grey"));
        QVERIFY2(pane->getTopLayer() != layer,
                 "the lyrics are the pane's top layer");

        // Opening a session is not a change to it
        QVERIFY(!m_window->isDocumentModified());

        // Shown again, saved and opened again: shown
        m_window->showLyricsAction()->trigger();
        QVERIFY(lyrics->isVisible());
        session = m_dir.filePath("lyrics-shown.ton");
        QVERIFY(m_window->saveSessionFile(session));
        reopenSession(session);
        if (QTest::currentTestFailed()) return;

        QVERIFY(lyrics->isShown());
        QCOMPARE(lyricsLayersInDocument(), 1);
        pane = m_window->paneStack()->getPane(0);
        QVERIFY(!lyrics->getLayer()->isLayerDormant(pane));
        QVERIFY(lyrics->isVisible());
        QVERIFY(m_window->showLyricsAction()->isChecked());
        verifyEventsSurvived(before, lyricsEvents(), "the lyrics, saved twice");
    }

    // A session can have the lyrics on top of pane 0, if the layer that
    // was above them went before it was saved.  They do not stay on top
    // once it is opened
    void lyrics_not_top_after_reopen() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::Pane *pane = m_window->paneStack()->getPane(0);
        sv::Layer *layer = m_window->lyrics()->getLayer();
        TakeLayers::raise(pane, layer);
        QVERIFY(pane->getTopLayer() == layer);
        int count = pane->getLayerCount();
        QString below = pane->getLayer(count - 2)->objectName();
        QVERIFY(below != "");

        QString session = m_dir.filePath("lyrics-on-top.ton");
        QVERIFY(m_window->saveSessionFile(session));
        reopenSession(session);
        if (QTest::currentTestFailed()) return;

        // The layer that was just under them is over them, and nothing
        // else has moved
        pane = m_window->paneStack()->getPane(0);
        layer = m_window->lyrics()->getLayer();
        QVERIFY(layer);
        QCOMPARE(pane->getLayerCount(), count);
        QVERIFY2(pane->getTopLayer() != layer,
                 "the lyrics are the pane's top layer");
        QCOMPARE(pane->getTopLayer()->objectName(), below);
        QVERIFY(pane->getLayer(count - 2) == layer);
    }

    // The lyrics belong to the song, not to a take: nothing a take does
    // touches them, and the singer reads them while recording
    void lyrics_survive_takes() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 4.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::Layer *layer = lyricsLayerInPane0();
        QVERIFY(layer);
        sv::ModelId model = m_window->lyrics()->getModelId();
        sv::EventVector events = lyricsEvents();
        QVERIFY(!events.empty());

        startTake();
        if (QTest::currentTestFailed()) return;
        verifyLyricsUntouched(layer, model, events, "at the start of the take");
        if (QTest::currentTestFailed()) return;
        QTest::qWait(600);
        verifyLyricsUntouched(layer, model, events, "while recording");
        if (QTest::currentTestFailed()) return;
        stopTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->takes()->haveTake());
        verifyLyricsUntouched(layer, model, events, "after the take");
        if (QTest::currentTestFailed()) return;

        QCOMPARE(undoOnce(), QString("Record Singing"));
        verifyLyricsUntouched(layer, model, events, "after the undo");
        if (QTest::currentTestFailed()) return;
        QCOMPARE(redoOnce(), QString("Record Singing"));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        verifyLyricsUntouched(layer, model, events, "after the redo");
        if (QTest::currentTestFailed()) return;

        // The first take's layers are put away for the new one, and it has
        // its audio swapped in under them on the way back
        m_window->doNewEmptyTake();
        QCOMPARE(m_window->takes()->getActiveIndex(), 1);
        verifyLyricsUntouched(layer, model, events, "in a new take");
        if (QTest::currentTestFailed()) return;
        m_window->doChooseTakeInCombo(0);
        QCOMPARE(m_window->takes()->getActiveIndex(), 0);
        verifyLyricsUntouched(layer, model, events, "back in the first take");
        if (QTest::currentTestFailed()) return;
        verifyPlaySourceClean();

        // A session with takes is restored after its lyrics are found,
        // and that leaves them alone as well
        QString session = m_dir.filePath("lyrics-takes.ton");
        QVERIFY(m_window->saveSessionFile(session));
        reopenSession(session);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->takes()->getTakeCount(), 2);
        QCOMPARE(m_window->takes()->getActiveIndex(), 0);
        sv::EventVector reopened = lyricsEvents();
        verifyEventsSurvived(events, reopened, "the lyrics");
        if (QTest::currentTestFailed()) return;
        verifyLyricsUntouched(lyricsLayerInPane0(),
                              m_window->lyrics()->getModelId(), reopened,
                              "after the session was opened");
        if (QTest::currentTestFailed()) return;
        verifyPlaySourceClean();
    }

    // Load Singing Track opens its file with openPath(), prunes the pane
    // that makes and clears the history: none of it is the lyrics' business
    void lyrics_survive_load_singing_track() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::Layer *layer = lyricsLayerInPane0();
        sv::ModelId model = m_window->lyrics()->getModelId();
        sv::EventVector events = lyricsEvents();

        m_window->loadSingingTrack(writeWav(tone(highHz, 1.0)));
        QTRY_VERIFY_WITH_TIMEOUT(analysed(m_window->analyser2()), 30000);
        verifyLyricsUntouched(layer, model, events, "after Load Singing Track");
        if (QTest::currentTestFailed()) return;
        verifyPlaySourceClean();
    }

    // The lyrics go with their session, and no other session is given them
    void lyrics_gone_with_session() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QString plain = m_dir.filePath("no-lyrics.ton");
        QVERIFY(m_window->saveSessionFile(plain));

        LyricsTrack *lyrics = m_window->lyrics();
        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        sv::ModelId model = lyrics->getModelId();
        QString withLyrics = m_dir.filePath("with-lyrics.ton");
        QVERIFY(m_window->saveSessionFile(withLyrics));

        m_window->doCloseSession();
        QVERIFY(!lyrics->isShown());
        QVERIFY(!lyrics->getLayer());
        QVERIFY2(!sv::ModelById::get(model),
                 "the lyrics model outlived its session");

        auto noLyrics = [&](const char *what) {
            QVERIFY2(!lyrics->isShown(), what);
            QVERIFY2(lyricsLayersInDocument() == 0, what);
            QVERIFY2(!m_window->removeLyricsAction()->isEnabled(), what);
            QVERIFY2(!m_window->showLyricsAction()->isEnabled(), what);
            QVERIFY2(!m_window->showLyricsAction()->isChecked(), what);
            QVERIFY2(m_window->importLyricsAction()->isEnabled(), what);
        };

        openReference(writeWav(tone(highHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        noLyrics("a new reference was given the lyrics");
        if (QTest::currentTestFailed()) return;

        // A session without lyrics, opened after one with them
        openReference(withLyrics);
        if (QTest::currentTestFailed()) return;
        QVERIFY(lyrics->isShown());
        openReference(plain);
        if (QTest::currentTestFailed()) return;
        noLyrics("a session without lyrics was given the last one's");
    }

    // The word at the playback position is highlighted with playback
    // stopped as well: at once when the lyrics are imported with the
    // cursor in a word, and wherever a seek puts the cursor
    void lyrics_highlight_follows_seek() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        m_window->seekTo(sv::sv_frame_t(0.7 * rate));
        QVERIFY(m_window->doImportLyricsFrom(writeLrc(gappedLyrics())));
        QCOMPARE(highlightedWord(), QString("kaksi"));

        // Before the first word, in each word, and in the gaps after them
        const struct { double seconds; const char *word; } seeks[] = {
            { 0.30, "Yksi" }, { 1.10, "" }, { 1.50, "kolme" },
            { 0.10, "" }, { 0.80, "kaksi" }, { 1.90, "" },
        };
        for (const auto &s : seeks) {
            m_window->seekTo(sv::sv_frame_t(s.seconds * rate));
            QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString(s.word), 1000);
        }

        // A highlight is not a change to the session
        m_window->discardModifications();
        m_window->seekTo(sv::sv_frame_t(0.3 * rate));
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString("Yksi"), 1000);
        QVERIFY(!m_window->isDocumentModified());
    }

    // While the reference plays, the highlight moves on from word to word
    void lyrics_highlight_follows_playback() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->doImportLyricsFrom
                (writeLrc("[00:00.00]<00:00.00>Yksi <00:00.25>kaksi "
                          "<00:00.50>kolme <00:00.75>neljä <00:01.00>viisi "
                          "<00:01.25>kuusi <00:01.50>\n")));
        QCOMPARE(m_window->playbackFrame(), sv::sv_frame_t(0));
        QCOMPARE(highlightedWord(), QString("Yksi"));

        m_window->doPlay();
        QTRY_VERIFY_WITH_TIMEOUT(highlightedWord() == "neljä", 2000);
        m_window->doPlay();
        QVERIFY2(m_window->fake()->getPlayStartFrame() >= 0,
                 "nothing was played");
    }

    // During a take the highlight follows the cursor, which runs with the
    // reference: through the lead-in of a pre-roll, while the status bar
    // counts down, and from the take's position on the reference's
    // timeline, not the recording's own.  The take's waveform is faded
    // under the lyrics like the reference's
    void lyrics_highlight_and_fade_in_a_take() {
        const double leadIn = 0.6;
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        setPreRollSeconds(leadIn);
        m_window->setPreRoll(true);
        openReference(writeWav(tone(lowHz, 3.0)));
        if (QTest::currentTestFailed()) return;

        // A word in the lead-in, a gap at the take's position, and two
        // words after it
        QVERIFY(m_window->doImportLyricsFrom
                (writeLrc("[00:01.50]<00:01.50>Alku <00:01.90>\n"
                          "[00:02.10]<00:02.10>kaksi <00:02.40>kolme "
                          "<00:03.00>\n")));
        const sv::sv_frame_t P = sv::sv_frame_t(2.0 * rate);
        m_window->seekTo(P);
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString(), 1000);

        startTake();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->takePreRoll(), sv::sv_frame_t(leadIn * rate));
        QTRY_VERIFY_WITH_TIMEOUT(highlightedWord() == "Alku", 1000);
        QVERIFY2(m_window->statusText().startsWith("Recording in "),
                 qPrintable(QString("the word in the lead-in is lit, but the "
                                    "status bar says \"%1\" rather than "
                                    "counting down")
                            .arg(m_window->statusText())));
        QTRY_VERIFY_WITH_TIMEOUT(highlightedWord() == "kolme", 1500);
        stopTake();
        if (QTest::currentTestFailed()) return;

        // Back at the take's position, in the gap
        QCOMPARE(m_window->playbackFrame(), P);
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString(), 1000);

        // The take's analyser was made when the take stopped, with a
        // waveform of its own, and that is faded too
        Analyser *a2 = m_window->analyser2();
        QVERIFY(a2 && a2->getLayer(Analyser::Audio));
        QCOMPARE(waveformColour(a2), QString("Pale Grey"));
        QCOMPARE(waveformColour(m_window->analyser()), QString("Pale Grey"));
    }

    // The waveform is faded while the lyrics are on show over it, and
    // only then.  It is no setting of the user's: nothing goes into the
    // settings, as Analyser::setVisible() and setAudible() would put it,
    // no command is made, and the fade marks nothing modified
    void lyrics_fade_the_waveform() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        Analyser *a = m_window->analyser();
        QCOMPARE(waveformColour(a), QString("Grey"));

        // Values the waveform does not have, which a write of its state to
        // the settings would put right
        QSettings settings;
        settings.beginGroup("Analyser");
        settings.setValue(QString("visible-%1").arg(int(Analyser::Audio)),
                          !a->isVisible(Analyser::Audio));
        settings.setValue(QString("audible-%1").arg(int(Analyser::Audio)),
                          !a->isAudible(Analyser::Audio));
        settings.endGroup();
        settings.sync();
        auto before = allSettings();
        auto *history = sv::CommandHistory::getInstance();
        QSignalSpy commands(history, qOverload<>
                            (&sv::CommandHistory::commandExecuted));

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        QCOMPARE(waveformColour(a), QString("Pale Grey"));

        QAction *show = m_window->showLyricsAction();
        show->trigger();
        QVERIFY(!m_window->lyrics()->isVisible());
        QCOMPARE(waveformColour(a), QString("Grey"));
        show->trigger();
        QVERIFY(m_window->lyrics()->isVisible());
        QCOMPARE(waveformColour(a), QString("Pale Grey"));

        m_window->removeLyricsAction()->trigger();
        QVERIFY(!m_window->lyrics()->isShown());
        QCOMPARE(waveformColour(a), QString("Grey"));

        QCOMPARE(int(commands.count()), 0);
        QStringList changed = settingsChanged(before, allSettings());
        QVERIFY2(changed.isEmpty(),
                 qPrintable("changed in the settings: " + changed.join(", ")));

        // The import, Show Lyrics and Remove Lyrics change the session;
        // the fade on its own does not
        m_window->discardModifications();
        a->setWaveformFaded(true);
        QCOMPARE(waveformColour(a), QString("Pale Grey"));
        a->setWaveformFaded(false);
        QCOMPARE(waveformColour(a), QString("Grey"));
        QVERIFY(!m_window->isDocumentModified());
        QCOMPARE(int(commands.count()), 0);
    }

    // The session saves the waveform's colour with its layer, faded or
    // not.  Opened, it is faded under lyrics on show and grey otherwise,
    // whatever it was saved as, and the word at the cursor is lit at once
    void lyrics_fade_survives_a_session() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        // Saved faded with no lyrics, as a session whose lyrics have gone
        // since would be
        m_window->analyser()->setWaveformFaded(true);
        QString fadedWithout = m_dir.filePath("faded-without-lyrics.ton");
        QVERIFY(m_window->saveSessionFile(fadedWithout));
        m_window->analyser()->setWaveformFaded(false);

        QVERIFY(m_window->doImportLyricsFrom
                (lyricsFixture("moises-exporter-words.lrc")));
        m_window->seekTo(sv::sv_frame_t(0.5 * rate));
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString("nollaa"), 1000);
        QString shown = m_dir.filePath("lyrics-shown-faded.ton");
        QVERIFY(m_window->saveSessionFile(shown));
        m_window->showLyricsAction()->trigger();
        QCOMPARE(waveformColour(m_window->analyser()), QString("Grey"));
        QString hidden = m_dir.filePath("lyrics-hidden-grey.ton");
        QVERIFY(m_window->saveSessionFile(hidden));

        reopenSession(shown);
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->lyrics()->isVisible());
        QCOMPARE(waveformColour(m_window->analyser()), QString("Pale Grey"));
        sv::RegionLayer *layer = m_window->lyrics()->getLayer();
        QCOMPARE(layer->getHighlightFrame(), m_window->playbackFrame());

        reopenSession(hidden);
        if (QTest::currentTestFailed()) return;
        QVERIFY(m_window->lyrics()->isShown());
        QVERIFY(!m_window->lyrics()->isVisible());
        QCOMPARE(waveformColour(m_window->analyser()), QString("Grey"));

        // Opened after one with lyrics on show, and saved faded itself.
        // The close in between takes the fade away: the analyser of the
        // reference stays for the next file
        reopenSession(shown);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(waveformColour(m_window->analyser()), QString("Pale Grey"));
        QVERIFY(m_window->analyser()->isWaveformFaded());
        m_window->doCloseSession();
        QVERIFY2(!m_window->analyser()->isWaveformFaded(),
                 "the fade outlived the session");
        openReference(fadedWithout);
        if (QTest::currentTestFailed()) return;
        QVERIFY(!m_window->lyrics()->isShown());
        QCOMPARE(waveformColour(m_window->analyser()), QString("Grey"));
    }

    // Without Edit Lyrics the mouse is the pane's everywhere: drags from
    // either side of the shared edge move no word
    void lyrics_edit_off_leaves_words_alone() {
        makeWindow(FakeAudioIO::Config());
        showEditableLyrics();
        if (QTest::currentTestFailed()) return;
        sv::EventVector before = lyricsEvents();

        QAction *edit = m_window->editLyricsAction();
        QVERIFY(edit->isEnabled());
        QVERIFY(!edit->isChecked());
        QVERIFY(!m_window->lyricsEditor()->isEnabled());

        // The pane's navigate drags, which move the view
        int edge = columnOf(lyricsWord("kaksi").getFrame());
        dragFromTo(inRow(edge - 1), inRow(edge - 30));
        QVERIFY2(columnOf(lyricsWord("kaksi").getFrame()) != edge,
                 "the pane did not get the drag");
        edge = columnOf(lyricsWord("kaksi").getFrame());
        dragFromTo(inRow(edge), inRow(edge + 30));
        QCOMPARE(lyricsEvents(), before);
        QVERIFY(!m_window->isDocumentModified());
        QCOMPARE(undoOnce(), QString());

        // and a double-click on a word asks nothing.  The pane's own may
        // open the edit dialog of the pitch point there, which the
        // watchdog closes
        doubleClickAt(inRow(columnOf(lyricsWord("kaksi").getFrame()) + 40));
        takeDialogs();
        QCOMPARE(m_window->wordTextQuestions(), 0);
        QCOMPARE(lyricsEvents(), before);
    }

    // Yksi's end and kaksi's start are one edge on screen.  The column
    // left of it is Yksi's, the one right of it kaksi's, and the side the
    // pointer is on picks the one word that moves (decision 3)
    void lyrics_edit_shared_edge_each_side() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::Event yksi = lyricsWord("Yksi");
        sv::Event kaksi = lyricsWord("kaksi");
        QCOMPARE(endOf(yksi), kaksi.getFrame());
        int edge = columnOf(kaksi.getFrame());

        dragFromTo(inRow(edge - 1), inRow(edge - 21));
        sv::Event moved = lyricsWord("Yksi");
        QCOMPARE(endOf(moved), endOf(yksi) + framesBetween(edge - 1, edge - 21));
        QCOMPARE(moved.getFrame(), yksi.getFrame());
        QCOMPARE(moved.getValue(), yksi.getValue());
        QCOMPARE(moved.getLabel(), yksi.getLabel());
        QCOMPARE(lyricsWord("kaksi"), kaksi);
        QCOMPARE(int(lyricsEvents().size()), 3);
        QCOMPARE(undoOnce(), QString("Move Word End"));
        QCOMPARE(lyricsWord("Yksi"), yksi);

        dragFromTo(inRow(edge), inRow(edge + 20));
        moved = lyricsWord("kaksi");
        QCOMPARE(moved.getFrame(), kaksi.getFrame() + framesBetween(edge, edge + 20));
        QCOMPARE(endOf(moved), endOf(kaksi));
        QCOMPARE(moved.getValue(), kaksi.getValue());
        QCOMPARE(lyricsWord("Yksi"), yksi);
        QCOMPARE(int(lyricsEvents().size()), 3);
        QCOMPARE(undoOnce(), QString("Move Word Start"));
        QCOMPARE(lyricsWord("kaksi"), kaksi);
    }

    // An edge stops at the neighbouring word and 20 ms from the word's
    // other edge, however far the pointer goes (decision 6)
    void lyrics_edit_clamps() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::Event yksi = lyricsWord("Yksi");
        sv::Event kaksi = lyricsWord("kaksi");
        sv::Event kolme = lyricsWord("kolme");
        sv::EventVector before = lyricsEvents();

        // kaksi's end, over the gap and past the whole of kolme
        int kaksiEnd = columnOf(endOf(kaksi)) - 1;
        dragFromTo(inRow(kaksiEnd), inRow(columnOf(endOf(kolme)) + 30));
        QCOMPARE(endOf(lyricsWord("kaksi")), kolme.getFrame());
        QCOMPARE(lyricsWord("kolme"), kolme);
        QCOMPARE(undoOnce(), QString("Move Word End"));

        // kolme's start, back over the gap and past Yksi
        int kolmeStart = columnOf(kolme.getFrame());
        dragFromTo(inRow(kolmeStart), inRow(columnOf(yksi.getFrame()) - 30));
        QCOMPARE(lyricsWord("kolme").getFrame(), endOf(kaksi));
        QCOMPARE(endOf(lyricsWord("kolme")), endOf(kolme));
        QCOMPARE(lyricsWord("kaksi"), kaksi);
        QCOMPARE(undoOnce(), QString("Move Word Start"));

        // Yksi's end, back past its own start: 20 ms after it
        int yksiEnd = columnOf(endOf(yksi)) - 1;
        dragFromTo(inRow(yksiEnd), inRow(columnOf(yksi.getFrame()) - 30));
        QCOMPARE(endOf(lyricsWord("Yksi")),
                 yksi.getFrame() + LyricsEdit::minWordFrames(rate));
        QCOMPARE(lyricsWord("Yksi").getFrame(), yksi.getFrame());
        QCOMPARE(undoOnce(), QString("Move Word End"));

        // kaksi's start, on past its own end
        int kaksiStart = columnOf(kaksi.getFrame());
        dragFromTo(inRow(kaksiStart), inRow(columnOf(endOf(kaksi)) + 30));
        QCOMPARE(lyricsWord("kaksi").getFrame(),
                 endOf(kaksi) - LyricsEdit::minWordFrames(rate));
        QCOMPARE(undoOnce(), QString("Move Word Start"));

        // Nothing after the last word: kolme's end goes where it is taken
        int kolmeEnd = columnOf(endOf(kolme)) - 1;
        dragFromTo(inRow(kolmeEnd), inRow(kolmeEnd + 40));
        QCOMPARE(endOf(lyricsWord("kolme")),
                 endOf(kolme) + framesBetween(kolmeEnd, kolmeEnd + 40));
        QCOMPARE(undoOnce(), QString("Move Word End"));

        // and a drag past the neighbour and back leaves the edge where the
        // pointer is: the limit is not where the drag got to
        pressAt(inRow(kaksiEnd));
        moveHeldTo(inRow(columnOf(endOf(kolme)) + 30));
        QCOMPARE(endOf(lyricsWord("kaksi")), kolme.getFrame());
        moveHeldTo(inRow(kaksiEnd + 20));
        releaseAt(inRow(kaksiEnd + 20));
        QCOMPARE(endOf(lyricsWord("kaksi")),
                 endOf(kaksi) + framesBetween(kaksiEnd, kaksiEnd + 20));
        QCOMPARE(undoOnce(), QString("Move Word End"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(undoOnce(), QString());
    }

    // A drag edits the words as it goes and is one step on the history
    // when let go (decision 12); a click, or a drag back to where it
    // began, is none
    void lyrics_edit_one_step_per_drag() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        auto *history = sv::CommandHistory::getInstance();
        QSignalSpy commands(history, qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        sv::EventVector before = lyricsEvents();
        sv::Event kaksi = lyricsWord("kaksi");
        int end = columnOf(endOf(kaksi)) - 1;

        pressAt(inRow(end));
        QVERIFY(editor->isDragging());
        releaseAt(inRow(end));
        QVERIFY(!editor->isDragging());

        pressAt(inRow(end));
        for (int x = end; x <= end + 40; x += 4) moveHeldTo(inRow(x));
        QVERIFY(endOf(lyricsWord("kaksi")) > endOf(kaksi));
        for (int x = end + 40; x >= end; x -= 4) moveHeldTo(inRow(x));
        releaseAt(inRow(end));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(int(commands.count()), 0);
        QVERIFY(!m_window->isDocumentModified());
        QCOMPARE(undoOnce(), QString());

        // Many moves, each one seen in the model at once, and nothing on
        // the history until the button is let go
        pressAt(inRow(end));
        for (int x = end + 2; x <= end + 60; x += 2) {
            moveHeldTo(inRow(x));
            QCOMPARE(endOf(lyricsWord("kaksi")),
                     endOf(kaksi) + framesBetween(end, x));
        }
        QCOMPARE(int(commands.count()), 0);
        releaseAt(inRow(end + 60));
        QCOMPARE(int(commands.count()), 1);
        QVERIFY(m_window->isDocumentModified());
        sv::EventVector afterFirst = lyricsEvents();

        int yksiStart = columnOf(lyricsWord("Yksi").getFrame());
        dragFromTo(inRow(yksiStart), inRow(yksiStart + 30));
        QCOMPARE(int(commands.count()), 2);
        sv::EventVector afterSecond = lyricsEvents();
        QVERIFY(afterSecond != afterFirst);

        QCOMPARE(undoOnce(), QString("Move Word Start"));
        QCOMPARE(lyricsEvents(), afterFirst);
        QCOMPARE(undoOnce(), QString("Move Word End"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(undoOnce(), QString());
        QCOMPARE(redoOnce(), QString("Move Word End"));
        QCOMPARE(lyricsEvents(), afterFirst);
        QCOMPARE(redoOnce(), QString("Move Word Start"));
        QCOMPARE(lyricsEvents(), afterSecond);
        QCOMPARE(redoOnce(), QString());

        // Still the lyrics' own model, out of the play source
        QVERIFY(m_window->playSource()->getModels().count
                (m_window->lyrics()->getModelId()) == 0);
        verifyPlaySourceClean();
    }

    // An edit is a change to the session, and the session keeps it
    void lyrics_edit_saved_with_the_session() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::EventVector before = lyricsEvents();

        int end = columnOf(endOf(lyricsWord("kaksi"))) - 1;
        dragFromTo(inRow(end), inRow(end + 50));
        int start = columnOf(lyricsWord("kolme").getFrame());
        dragFromTo(inRow(start), inRow(start + 25));
        QVERIFY(m_window->isDocumentModified());
        sv::EventVector edited = lyricsEvents();
        QVERIFY(edited != before);

        QString session = m_dir.filePath("lyrics-edited.ton");
        QVERIFY(m_window->saveSessionFile(session));
        reopenSession(session);
        if (QTest::currentTestFailed()) return;

        QVERIFY(m_window->lyrics()->isShown());
        verifyEventsSurvived(edited, lyricsEvents(), "the edited lyrics");
        if (QTest::currentTestFailed()) return;
        QCOMPARE(lyricsWord("kaksi").getLabel(), QString("kaksi"));

        // Edit mode is not part of the session
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
        QVERIFY(m_window->editLyricsAction()->isEnabled());
        QVERIFY(!m_window->editLyricsAction()->isChecked());
        QVERIFY(m_window->playSource()->getModels().count
                (m_window->lyrics()->getModelId()) == 0);
        verifyPlaySourceClean();
    }

    // The word at the cursor is found again as its edge moves, while the
    // button is held as well
    void lyrics_edit_highlight_follows() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::Event kaksi = lyricsWord("kaksi");
        sv::Event kolme = lyricsWord("kolme");
        sv::sv_frame_t gap = (endOf(kaksi) + kolme.getFrame()) / 2;
        m_window->seekTo(gap);
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString(), 1000);

        int end = columnOf(endOf(kaksi)) - 1;
        int past = columnOf(gap) + 10;
        pressAt(inRow(end));
        moveHeldTo(inRow(past));
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString("kaksi"), 1000);
        releaseAt(inRow(past));
        QCOMPARE(highlightedWord(), QString("kaksi"));

        QCOMPARE(undoOnce(), QString("Move Word End"));
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString(), 1000);

        int start = columnOf(kolme.getFrame());
        dragFromTo(inRow(start), inRow(columnOf(gap) - 10));
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString("kolme"), 1000);
    }

    // Nothing to edit: the action is there, disabled, and its trigger
    // does nothing
    void lyrics_edit_needs_lyrics() {
        makeWindow(FakeAudioIO::Config());
        QAction *edit = m_window->editLyricsAction();
        QVERIFY(!edit->isEnabled());
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(!edit->isEnabled());
        QVERIFY(!edit->isChecked());
        edit->trigger();
        QVERIFY(!edit->isChecked());
        QVERIFY(!m_window->lyricsEditor()->isEnabled());

        QVERIFY(m_window->doImportLyricsFrom(writeLrc(gappedLyrics())));
        QVERIFY(edit->isEnabled());
        QVERIFY(!edit->isChecked());
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
    }

    // Turned off, and not to be had, with the lyrics hidden; shown again,
    // it is to be had but stays off, and the mouse is the pane's
    void lyrics_edit_off_when_hidden() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QAction *edit = m_window->editLyricsAction();
        QAction *show = m_window->showLyricsAction();
        sv::EventVector before = lyricsEvents();

        show->trigger();
        QVERIFY(!m_window->lyrics()->isVisible());
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
        QVERIFY(!edit->isEnabled());
        QVERIFY(!edit->isChecked());

        show->trigger();
        QVERIFY(m_window->lyrics()->isVisible());
        QVERIFY(edit->isEnabled());
        QVERIFY(!edit->isChecked());
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
        dragIsThePanes(before);
    }

    void lyrics_edit_off_on_remove() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QAction *edit = m_window->editLyricsAction();

        m_window->removeLyricsAction()->trigger();
        QVERIFY(!m_window->lyrics()->isShown());
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
        QVERIFY(!edit->isEnabled());
        QVERIFY(!edit->isChecked());
    }

    // New lyrics are not what edit mode was switched on for
    void lyrics_edit_off_on_import() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QAction *edit = m_window->editLyricsAction();

        QVERIFY(m_window->doImportLyricsFrom(writeLrc(gappedLyrics())));
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
        QVERIFY(edit->isEnabled());
        QVERIFY(!edit->isChecked());
        m_row = lyricsBoxRow();
        dragIsThePanes(lyricsEvents());
    }

    // Off with the session, and off in the next one until switched on,
    // which then edits in the new pane 0
    void lyrics_edit_off_on_close() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QAction *edit = m_window->editLyricsAction();

        m_window->doCloseSession();
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
        QVERIFY(!edit->isEnabled());
        QVERIFY(!edit->isChecked());

        showEditableLyrics();
        if (QTest::currentTestFailed()) return;
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
        QVERIFY(!edit->isChecked());
        sv::EventVector before = lyricsEvents();
        dragIsThePanes(before);
        if (QTest::currentTestFailed()) return;

        switchLyricsEditingOn();
        int edge = columnOf(lyricsWord("kaksi").getFrame());
        dragFromTo(inRow(edge), inRow(edge + 30));
        QCOMPARE(lyricsWord("kaksi").getFrame(),
                 before[1].getFrame() + framesBetween(edge, edge + 30));
    }

    // Off while a take is recorded, when the singer is reading the words.
    // A drag going on when the take starts is finished first, and so is
    // on the history before the take
    void lyrics_edit_off_while_recording() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        lyricsEditFixture(config);
        if (QTest::currentTestFailed()) return;
        QAction *edit = m_window->editLyricsAction();
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::EventVector before = lyricsEvents();

        int end = columnOf(endOf(lyricsWord("kaksi"))) - 1;
        pressAt(inRow(end));
        moveHeldTo(inRow(end + 30));
        sv::EventVector dragged = lyricsEvents();
        QVERIFY(dragged != before);

        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(!editor->isEnabled());
        QVERIFY(!editor->isDragging());
        QVERIFY(!edit->isEnabled());
        QVERIFY(!edit->isChecked());

        // The rest of that drag moves nothing
        moveHeldTo(inRow(end + 60));
        releaseAt(inRow(end + 60));
        QCOMPARE(lyricsEvents(), dragged);

        QTest::qWait(300);
        stopTake();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(edit->isEnabled(), 2000);
        QVERIFY(!edit->isChecked());
        QVERIFY(!editor->isEnabled());

        QCOMPARE(undoOnce(), QString("Record Singing"));
        QCOMPARE(undoOnce(), QString("Move Word End"));
        QCOMPARE(lyricsEvents(), before);
    }

    // Edit mode switched off in the middle of a drag: the drag ends as a
    // release would end it, and the rest of it is not an edit
    void lyrics_edit_off_in_the_middle_of_a_drag() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::EventVector before = lyricsEvents();

        int end = columnOf(endOf(lyricsWord("kaksi"))) - 1;
        pressAt(inRow(end));
        moveHeldTo(inRow(end + 30));
        sv::EventVector dragged = lyricsEvents();
        QVERIFY(dragged != before);

        m_window->editLyricsAction()->trigger();
        QVERIFY(!editor->isEnabled());
        QVERIFY(!editor->isDragging());
        QVERIFY(!m_window->editLyricsAction()->isChecked());
        QVERIFY(m_window->isDocumentModified());

        moveHeldTo(inRow(end + 60));
        releaseAt(inRow(end + 60));
        QCOMPARE(lyricsEvents(), dragged);

        QCOMPARE(undoOnce(), QString("Move Word End"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(undoOnce(), QString());
    }

    // The words change under a drag: removed, or an undo (Ctrl+Z with the
    // button held) takes away the word being dragged.  The drag ends, the
    // model is left as that made it, and nothing goes on the history
    void lyrics_edit_words_change_under_a_drag() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::EventVector before = lyricsEvents();
        sv::Event yksi = lyricsWord("Yksi");

        // An earlier drag of Yksi's end, undone after a press on that end
        int end = columnOf(endOf(yksi)) - 1;
        dragFromTo(inRow(end), inRow(end - 20));
        sv::EventVector moved = lyricsEvents();
        end = columnOf(endOf(lyricsWord("Yksi"))) - 1;
        pressAt(inRow(end));
        QVERIFY(editor->isDragging());
        QCOMPARE(undoOnce(), QString("Move Word End"));
        QCOMPARE(lyricsEvents(), before);
        moveHeldTo(inRow(end - 10));
        releaseAt(inRow(end - 10));
        QVERIFY(!editor->isDragging());
        QCOMPARE(lyricsEvents(), before);

        // Nothing was pushed over the undone drag, which can be redone
        QCOMPARE(redoOnce(), QString("Move Word End"));
        QCOMPARE(lyricsEvents(), moved);
        QCOMPARE(redoOnce(), QString());

        // Removed in the middle of a drag
        int start = columnOf(lyricsWord("kolme").getFrame());
        pressAt(inRow(start));
        moveHeldTo(inRow(start + 20));
        m_window->removeLyricsAction()->trigger();
        QVERIFY(!editor->isDragging());
        QVERIFY(!editor->isEnabled());
        moveHeldTo(inRow(start + 40));
        releaseAt(inRow(start + 40));

        // On top of the history is still the redone drag of Yksi, whose
        // model has gone with the lyrics (and which does nothing now), not
        // a Move Word Start
        QCOMPARE(undoOnce(), QString("Move Word End"));
        QVERIFY(!m_window->lyrics()->isShown());
        verifyPlaySourceClean();
    }

    // The resize cursor over an edge, from either side of a shared one,
    // and the pane's own back wherever else the pointer goes; the status
    // bar says what the mouse does in the row
    void lyrics_edit_cursor_and_help() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::Pane *pane = pane0();
        Qt::CursorShape own = pane->cursor().shape();
        QVERIFY(own != Qt::SizeHorCursor);
        int edge = columnOf(lyricsWord("kaksi").getFrame());
        QPoint above(edge + 40, m_row.top() - 8);

        hoverAt(inRow(edge - 1));
        QCOMPARE(pane->cursor().shape(), Qt::SizeHorCursor);
        QCOMPARE(m_window->statusText(),
                 QString("Drag to move the end of \"Yksi\", "
                         "Shift-drag to move all the words"));
        hoverAt(inRow(edge + 2));
        QCOMPARE(pane->cursor().shape(), Qt::SizeHorCursor);
        QCOMPARE(m_window->statusText(),
                 QString("Drag to move the start of \"kaksi\", "
                         "Shift-drag to move all the words"));

        hoverAt(inRow(edge + 40));
        QCOMPARE(pane->cursor().shape(), own);
        QCOMPARE(m_window->statusText(),
                 QString("Double-click to change the text of \"kaksi\", "
                         "right-click to delete it, "
                         "Shift-drag to move all the words"));
        int gap = columnOf(endOf(lyricsWord("kaksi"))) + 20;
        hoverAt(inRow(gap));
        QCOMPARE(pane->cursor().shape(), own);
        QCOMPARE(m_window->statusText(),
                 QString("Right-click to add a word, "
                         "drag a word's start or end to move it, "
                         "Shift-drag to move all the words"));

        // A closed hand through a drag of all the words (decision 19),
        // from a word or from an edge, and the pane's own or the edge's
        // back after
        shiftPressAt(inRow(edge + 40));
        QCOMPARE(pane->cursor().shape(), Qt::ClosedHandCursor);
        shiftMoveHeldTo(inRow(edge + 50));
        QCOMPARE(pane->cursor().shape(), Qt::ClosedHandCursor);
        shiftReleaseAt(inRow(edge + 50));
        QCOMPARE(pane->cursor().shape(), own);
        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        edge = columnOf(lyricsWord("kaksi").getFrame());
        shiftPressAt(inRow(edge));
        QCOMPARE(pane->cursor().shape(), Qt::ClosedHandCursor);
        shiftReleaseAt(inRow(edge));
        QCOMPARE(pane->cursor().shape(), Qt::SizeHorCursor);
        hoverAt(inRow(gap));
        QCOMPARE(pane->cursor().shape(), own);

        hoverAt(inRow(edge));
        QCOMPARE(pane->cursor().shape(), Qt::SizeHorCursor);
        hoverAt(above);
        QCOMPARE(pane->cursor().shape(), own);
        QVERIFY2(!m_window->statusText().startsWith("Drag"),
                 qPrintable(m_window->statusText()));

        // Kept through a drag that leaves the row, and given back after
        pressAt(inRow(edge));
        moveHeldTo(above);
        QCOMPARE(pane->cursor().shape(), Qt::SizeHorCursor);
        releaseAt(above);
        QCOMPARE(pane->cursor().shape(), own);
        QCOMPARE(undoOnce(), QString("Move Word Start"));

        // Given back when edit mode goes off over an edge, with the help
        hoverAt(inRow(edge));
        QCOMPARE(pane->cursor().shape(), Qt::SizeHorCursor);
        m_window->editLyricsAction()->trigger();
        QCOMPARE(pane->cursor().shape(), own);
        QVERIFY2(!m_window->statusText().startsWith("Drag"),
                 qPrintable(m_window->statusText()));
        hoverAt(inRow(edge - 1));
        QCOMPARE(pane->cursor().shape(), own);
    }

    // Everything the editor does not act on is the pane's: a click in a
    // word moves the playback cursor as before, a click on an edge does
    // not.  A double-click on an edge is a press there
    void lyrics_edit_clicks_elsewhere_are_the_panes() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::Event kolme = lyricsWord("kolme");
        int edge = columnOf(kolme.getFrame());
        int inside = edge + 50;
        sv::sv_frame_t insideFrame = pane0()->getFrameForX(inside);
        sv::sv_frame_t start = m_window->playbackFrame();
        QVERIFY(std::abs(start - insideFrame) > 10000);

        // The pane moves the cursor a double-click interval after a
        // click, unless a second press comes first and takes its place:
        // so the edge's click has its time to show it did nothing
        pressAt(inRow(edge));
        releaseAt(inRow(edge));
        QTest::qWait(QApplication::doubleClickInterval() + 200);
        QCOMPARE(m_window->playbackFrame(), start);

        pressAt(inRow(inside));
        releaseAt(inRow(inside));
        QTRY_VERIFY_WITH_TIMEOUT
            (std::abs(m_window->playbackFrame() - insideFrame) < 1000, 3000);
        QCOMPARE(lyricsWord("kolme"), kolme);
        QCOMPARE(undoOnce(), QString());

        pressAt(inRow(edge));
        releaseAt(inRow(edge));
        sendMouse(QEvent::MouseButtonDblClick, inRow(edge),
                  Qt::LeftButton, Qt::LeftButton);
        moveHeldTo(inRow(edge + 20));
        releaseAt(inRow(edge + 20));
        QCOMPARE(lyricsWord("kolme").getFrame(),
                 kolme.getFrame() + framesBetween(edge, edge + 20));
        QCOMPARE(m_window->wordTextQuestions(), 0);
        QCOMPARE(undoOnce(), QString("Move Word Start"));
        QCOMPARE(undoOnce(), QString());
    }

    // A double-click on a word, away from its edges, asks for its text
    // and changes it, and nothing else of the word: one step on the
    // history (decisions 8, 12).  A double-click between words is the
    // pane's, and asks nothing
    void lyrics_edit_text_by_double_click() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        sv::EventVector before = lyricsEvents();
        sv::Event kaksi = lyricsWord("kaksi");
        int inside = columnOf(kaksi.getFrame()) + 40;

        m_window->answerWordText("kaksikko");
        doubleClickAt(inRow(inside));
        QCOMPARE(m_window->wordTextQuestions(), 1);
        QCOMPARE(m_window->wordTextOffered(), QString("kaksi"));
        QVERIFY(!m_window->wordTextWasNew());
        QCOMPARE(lyricsWord("kaksikko"), kaksi.withLabel("kaksikko"));
        QCOMPARE(lyricsWord("kaksi").getFrame(), sv::sv_frame_t(-1));
        QCOMPARE(int(lyricsEvents().size()), 3);
        QCOMPARE(int(commands.count()), 1);
        QVERIFY(m_window->isDocumentModified());
        sv::EventVector changed = lyricsEvents();

        QCOMPARE(undoOnce(), QString("Change Word Text"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(undoOnce(), QString());
        QCOMPARE(redoOnce(), QString("Change Word Text"));
        QCOMPARE(lyricsEvents(), changed);

        // The menu's Edit Word Text... is the same edit
        m_window->discardModifications();
        m_window->answerWordText("kaksi");
        QVERIFY(chooseAt(inRow(inside), "Edit Word Text..."));
        QCOMPARE(m_window->wordTextQuestions(), 2);
        QCOMPARE(m_window->wordTextOffered(), QString("kaksikko"));
        QCOMPARE(lyricsEvents(), before);
        QVERIFY(m_window->isDocumentModified());
        QCOMPARE(undoOnce(), QString("Change Word Text"));
        QCOMPARE(lyricsEvents(), changed);

        // Last, as the pane's double-click moves the view.  It may open
        // the edit dialog of the pitch point there, which the watchdog
        // closes
        int gap = columnOf(endOf(kaksi)) + 20;
        doubleClickAt(inRow(gap));
        takeDialogs();
        QCOMPARE(m_window->wordTextQuestions(), 2);
        QCOMPARE(lyricsEvents(), changed);
    }

    // Cancelled, nothing left of the text once cleaned, or the same text:
    // the word stays as it was, and nothing goes on the history
    // (decision 11).  A new word likewise is not added
    void lyrics_edit_text_refused() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        sv::EventVector before = lyricsEvents();
        int inside = columnOf(lyricsWord("kaksi").getFrame()) + 40;

        m_window->cancelWordText();
        m_window->answerWordText("");
        m_window->answerWordText("   ");
        m_window->answerWordText(QString("\t") + QChar(0x07) + " \r\n");
        m_window->answerWordText(" kaksi\t");
        for (int i = 1; i <= 5; ++i) {
            doubleClickAt(inRow(inside));
            QCOMPARE(m_window->wordTextQuestions(), i);
            QCOMPARE(lyricsEvents(), before);
        }

        m_window->cancelWordText();
        QVERIFY(chooseAt(inRow(inside), "Edit Word Text..."));
        QCOMPARE(m_window->wordTextQuestions(), 6);

        int gap = columnOf(endOf(lyricsWord("kaksi"))) + 20;
        m_window->cancelWordText();
        m_window->answerWordText(QString(" ") + QChar(0x7F) + "\t");
        QVERIFY(chooseAt(inRow(gap), "Add Word..."));
        QVERIFY(chooseAt(inRow(gap), "Add Word..."));
        QCOMPARE(m_window->wordTextQuestions(), 8);
        QVERIFY(m_window->wordTextWasNew());
        QCOMPARE(m_window->wordTextOffered(), QString());

        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(int(commands.count()), 0);
        QVERIFY(!m_window->isDocumentModified());
        QCOMPARE(undoOnce(), QString());
    }

    // The text is cleaned as the parsers clean a label: control
    // characters out, a tab a space, blanks at the ends trimmed, at most
    // 200 characters (decision 11)
    void lyrics_edit_text_cleaned() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::Event kaksi = lyricsWord("kaksi");
        int inside = columnOf(kaksi.getFrame()) + 40;

        m_window->answerWordText(QString("  kak") + QChar(0x07) +
                                 "si\tvaan \r\n");
        doubleClickAt(inRow(inside));
        QCOMPARE(lyricsWord("kaksi vaan"), kaksi.withLabel("kaksi vaan"));

        m_window->answerWordText(QString(250, QChar('a')));
        doubleClickAt(inRow(inside));
        QCOMPARE(lyricsWord(QString(200, QChar('a'))),
                 kaksi.withLabel(QString(200, QChar('a'))));

        int gap = columnOf(endOf(kaksi)) + 20;
        m_window->answerWordText(QString(" ja") + QChar(0x7F) + "\t");
        QVERIFY(chooseAt(inRow(gap), "Add Word..."));
        QVERIFY(lyricsWord("ja").getFrame() >= 0);
        QCOMPARE(int(lyricsEvents().size()), 4);

        QCOMPARE(undoOnce(), QString("Add Word"));
        QCOMPARE(undoOnce(), QString("Change Word Text"));
        QCOMPARE(undoOnce(), QString("Change Word Text"));
        QCOMPARE(lyricsWord("kaksi"), kaksi);
    }

    // The menu at a point: on a word, anywhere in its box, its edges as
    // well, the word's two entries; between words Add Word..., disabled
    // left of frame 0; outside the box row, or with edit mode off, none
    // (decision 8)
    void lyrics_edit_menu_entries() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::Event yksi = lyricsWord("Yksi");
        sv::Event kaksi = lyricsWord("kaksi");
        sv::Event kolme = lyricsWord("kolme");
        const QStringList onWord = { "Edit Word Text...", "Delete Word" };
        const QStringList add = { "Add Word..." };

        int start = columnOf(kaksi.getFrame());
        int last = columnOf(endOf(kaksi)) - 1;
        for (int x : { start, start + 2, start + 40, last - 2, last }) {
            QCOMPARE(menuAt(inRow(x)), onWord);
            auto entries = editor->menuEntriesAt(inRow(x));
            QCOMPARE(entries[0].word, kaksi);
            QCOMPARE(entries[1].word, kaksi);
        }
        QCOMPARE(menuAt(inRow(start - 1)), onWord);
        QCOMPARE(editor->menuEntriesAt(inRow(start - 1))[0].word, yksi);

        // Between words.  The first column after kaksi's box shows the
        // end of kaksi too, and where it starts is inside kaksi: a new word
        // goes after it all the same
        int after = columnOf(endOf(kaksi));
        int before = columnOf(kolme.getFrame()) - 1;
        QVERIFY2(pane0()->getFrameForX(after) < endOf(kaksi),
                 "the column after kaksi starts after kaksi's end: "
                 "the case of this test is not set up");
        for (int x : { after, after + 2, (after + before) / 2, before }) {
            QCOMPARE(menuAt(inRow(x)), add);
        }
        QCOMPARE(menuAt(inRow(columnOf(endOf(kolme)) + 50)), add);
        QCOMPARE(menuAt(inRow(columnOf(0) - 5)), QStringList{ "-Add Word..." });

        QVERIFY(menuAt(QPoint(start + 40, m_row.top() - 3)).isEmpty());
        QVERIFY(menuAt(QPoint(start + 40, m_row.bottom() + 3)).isEmpty());
        m_window->editLyricsAction()->trigger();
        QVERIFY(!editor->isEnabled());
        QVERIFY(menuAt(inRow(start + 40)).isEmpty());
        QVERIFY(menuAt(inRow(after)).isEmpty());
    }

    // A right press in the box row pops the words' menu up, and the pane
    // never sees it, so Tony's own menu stays shut; the entries of the
    // menu shown do what they say.  Outside the row, or with edit mode
    // off, the press is the pane's, and opens Tony's menu
    void lyrics_edit_right_press() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QSignalSpy panes(pane0(), &sv::Pane::rightButtonMenuRequested);
        sv::Event kolme = lyricsWord("kolme");
        int inside = columnOf(kolme.getFrame()) + 40;

        rightPressAt(inRow(inside));
        QCOMPARE(int(panes.count()), 0);
        QMenu *menu = wordsMenu();
        QVERIFY2(menu, "the words' menu is not on show");
        QCOMPARE(actionTexts(menu),
                 (QStringList{ "Edit Word Text...", "Delete Word" }));
        menu->actions()[1]->trigger();
        QCOMPARE(lyricsWord("kolme").getFrame(), sv::sv_frame_t(-1));
        QCOMPARE(int(lyricsEvents().size()), 2);
        QCOMPARE(closeMenus(), 1);
        QCOMPARE(undoOnce(), QString("Delete Word"));
        QCOMPARE(lyricsWord("kolme"), kolme);

        int gap = columnOf(kolme.getFrame()) - 20;
        rightPressAt(inRow(gap));
        QCOMPARE(int(panes.count()), 0);
        menu = wordsMenu();
        QVERIFY2(menu, "the words' menu is not on show");
        QCOMPARE(actionTexts(menu), QStringList{ "Add Word..." });
        QVERIFY(menu->actions()[0]->isEnabled());
        m_window->answerWordText("ja");
        menu->actions()[0]->trigger();
        QCOMPARE(m_window->wordTextQuestions(), 1);
        QVERIFY(lyricsWord("ja").getFrame() >= 0);
        QCOMPARE(closeMenus(), 1);
        QCOMPARE(undoOnce(), QString("Add Word"));

        // Above the row, where the pane's menu is
        rightPressAt(QPoint(inside, m_row.top() - 8));
        QCOMPARE(int(panes.count()), 1);
        QVERIFY(!wordsMenu());
        QCOMPARE(closeMenus(), 1);

        m_window->editLyricsAction()->trigger();
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
        rightPressAt(inRow(inside));
        QCOMPARE(int(panes.count()), 2);
        QVERIFY(!wordsMenu());
        QCOMPARE(closeMenus(), 1);
        QCOMPARE(lyricsWord("kolme"), kolme);
    }

    // Add Word... puts the word at the click: 0.5 s long, or up to the
    // next word, back into the gap as far as that makes it longer
    // (decision 9), and on the line of the nearer neighbour (decision 10)
    void lyrics_edit_add_word() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        sv::EventVector before = lyricsEvents();
        sv::Event kaksi = lyricsWord("kaksi");
        sv::Event kolme = lyricsWord("kolme");
        QVERIFY(kaksi.getValue() != kolme.getValue());
        QCOMPARE(kolme.getFrame() - endOf(kaksi), LyricsEdit::newWordFrames(rate));

        // The gap is 0.5 s, and the word fills it wherever the click is.
        // Its neighbours are as near as each other, and the word before
        // gives it its line
        m_window->answerWordText("ja");
        QVERIFY(chooseAt(inRow(columnOf(kolme.getFrame()) - 20), "Add Word..."));
        QCOMPARE(m_window->wordTextQuestions(), 1);
        QVERIFY(m_window->wordTextWasNew());
        QCOMPARE(m_window->wordTextOffered(), QString());
        QCOMPARE(lyricsWord("ja"),
                 sv::Event(endOf(kaksi), kaksi.getValue(),
                           kolme.getFrame() - endOf(kaksi), "ja"));
        QCOMPARE(int(lyricsEvents().size()), 4);
        QCOMPARE(int(commands.count()), 1);
        QVERIFY(m_window->isDocumentModified());
        sv::EventVector added = lyricsEvents();

        QCOMPARE(undoOnce(), QString("Add Word"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(redoOnce(), QString("Add Word"));
        QCOMPARE(lyricsEvents(), added);
        QCOMPARE(undoOnce(), QString("Add Word"));

        // kaksi 0.2 s shorter, for a wider gap.  Near kolme, the word is
        // moved back from it to be 0.5 s long, takes its line and is the
        // line's first word now, which the layer draws bold
        sv::Event shorter = kaksi.withDuration
            (kaksi.getDuration() - sv::sv_frame_t(0.2 * rate));
        replaceWord(kaksi, shorter);
        if (QTest::currentTestFailed()) return;
        m_window->answerWordText("ja");
        QVERIFY(chooseAt(inRow(columnOf(kolme.getFrame()) - 5), "Add Word..."));
        sv::Event ja = lyricsWord("ja");
        QCOMPARE(endOf(ja), kolme.getFrame());
        QCOMPARE(ja.getDuration(), LyricsEdit::newWordFrames(rate));
        QCOMPARE(ja.getValue(), kolme.getValue());
        Lyrics now = lyricsFromEvents(lyricsEvents(), rate);
        QCOMPARE(now.words.size(), 4);
        QCOMPARE(now.words[1].text, QString("kaksi"));
        QCOMPARE(now.words[2].text, QString("ja"));
        QVERIFY(now.words[2].line != now.words[1].line);
        QCOMPARE(now.words[3].line, now.words[2].line);
        QCOMPARE(undoOnce(), QString("Add Word"));

        // Near kaksi: at the click, 0.5 s long, on kaksi's line
        int x = columnOf(endOf(shorter)) + 10;
        m_window->answerWordText("ja");
        QVERIFY(chooseAt(inRow(x), "Add Word..."));
        ja = lyricsWord("ja");
        QCOMPARE(columnOf(ja.getFrame()), x);
        QCOMPARE(ja.getDuration(), LyricsEdit::newWordFrames(rate));
        QCOMPARE(ja.getValue(), kaksi.getValue());
        QCOMPARE(undoOnce(), QString("Add Word"));
        QCOMPARE(m_window->wordTextQuestions(), 3);
    }

    // Before the first word the gap runs from frame 0, and a word that
    // would be too long for it is cut to fit; after the last word there
    // is no limit.  Each takes the line of its one neighbour
    void lyrics_edit_add_word_at_either_end() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::EventVector before = lyricsEvents();
        sv::Event yksi = lyricsWord("Yksi");
        sv::Event kolme = lyricsWord("kolme");
        QVERIFY(yksi.getFrame() < LyricsEdit::newWordFrames(rate));

        m_window->answerWordText("Nyt");
        QVERIFY(chooseAt(inRow(columnOf(yksi.getFrame() / 2)), "Add Word..."));
        QCOMPARE(lyricsWord("Nyt"),
                 sv::Event(0, yksi.getValue(), yksi.getFrame(), "Nyt"));
        QCOMPARE(lyricsFromEvents(lyricsEvents(), rate).words[0].text,
                 QString("Nyt"));

        int after = columnOf(endOf(kolme)) + 20;
        m_window->answerWordText("loppu");
        QVERIFY(chooseAt(inRow(after), "Add Word..."));
        sv::Event loppu = lyricsWord("loppu");
        QCOMPARE(columnOf(loppu.getFrame()), after);
        QCOMPARE(loppu.getDuration(), LyricsEdit::newWordFrames(rate));
        QCOMPARE(loppu.getValue(), kolme.getValue());
        QCOMPARE(int(lyricsEvents().size()), 5);

        QCOMPARE(undoOnce(), QString("Add Word"));
        QCOMPARE(undoOnce(), QString("Add Word"));
        QCOMPARE(lyricsEvents(), before);
        verifyPlaySourceClean();
    }

    // No room: a gap under 20 ms.  The entry is there, disabled, and
    // chosen asks nothing; a gap of 20 ms is room.  Room taken after the
    // menu was made, or while the text is asked for: nothing is added
    void lyrics_edit_add_word_needs_room() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::Event kaksi = lyricsWord("kaksi");
        sv::Event kolme = lyricsWord("kolme");
        sv::sv_frame_t minimum = LyricsEdit::minWordFrames(rate);

        sv::Event narrow = kaksi.withDuration
            (kolme.getFrame() - (minimum - 1) - kaksi.getFrame());
        replaceWord(kaksi, narrow);
        if (QTest::currentTestFailed()) return;
        int x = columnOf(endOf(narrow)) + 1;
        QVERIFY(x < columnOf(kolme.getFrame()));
        sv::EventVector before = lyricsEvents();
        QCOMPARE(menuAt(inRow(x)), QStringList{ "-Add Word..." });
        m_window->answerWordText("ei");
        QVERIFY(chooseAt(inRow(x), "Add Word..."));
        QCOMPARE(m_window->wordTextQuestions(), 0);
        QCOMPARE(lyricsEvents(), before);

        sv::Event fits = narrow.withDuration(narrow.getDuration() - 1);
        replaceWord(narrow, fits);
        if (QTest::currentTestFailed()) return;
        QCOMPARE(menuAt(inRow(x)), QStringList{ "Add Word..." });
        QVERIFY(chooseAt(inRow(x), "Add Word..."));
        QCOMPARE(m_window->wordTextQuestions(), 1);
        QCOMPARE(lyricsWord("ei"),
                 sv::Event(endOf(fits), kaksi.getValue(), minimum, "ei"));
        QCOMPARE(undoOnce(), QString("Add Word"));

        // The menu made, and the gap closed before its entry is chosen
        auto entries = editor->menuEntriesAt(inRow(x));
        QCOMPARE(int(entries.size()), 1);
        QVERIFY(entries[0].enabled);
        sv::Event closed = fits.withDuration(kolme.getFrame() - fits.getFrame());
        replaceWord(fits, closed);
        if (QTest::currentTestFailed()) return;
        before = lyricsEvents();
        m_window->answerWordText("ei");
        editor->choose(entries[0]);
        QCOMPARE(m_window->wordTextQuestions(), 1);
        QCOMPARE(lyricsEvents(), before);

        // and closed while the text is asked for
        replaceWord(closed, fits);
        if (QTest::currentTestFailed()) return;
        m_window->whileAskingWordText([this, fits, closed]() {
            replaceWord(fits, closed);
        });
        editor->choose(entries[0]);
        QCOMPARE(m_window->wordTextQuestions(), 2);
        QCOMPARE(m_window->wordTextAnswersLeft(), 0);
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(undoOnce(), QString());
    }

    // Delete Word takes the word away: the only word of its line, and so
    // the line; then every word, after which Add Word still adds one.
    // Undone, each comes back as it was
    void lyrics_edit_delete_word() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        sv::EventVector before = lyricsEvents();
        sv::Event kolme = lyricsWord("kolme");
        QCOMPARE(lyricsFromEvents(before, rate).lineCount(), 2);
        int inside = columnOf(kolme.getFrame()) + 40;

        QVERIFY(chooseAt(inRow(inside), "Delete Word"));
        QCOMPARE(m_window->wordTextQuestions(), 0);
        QCOMPARE(lyricsWord("kolme").getFrame(), sv::sv_frame_t(-1));
        QCOMPARE(int(lyricsEvents().size()), 2);
        QCOMPARE(lyricsFromEvents(lyricsEvents(), rate).lineCount(), 1);
        QCOMPARE(int(commands.count()), 1);
        QVERIFY(m_window->isDocumentModified());
        sv::EventVector deleted = lyricsEvents();

        QCOMPARE(undoOnce(), QString("Delete Word"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(redoOnce(), QString("Delete Word"));
        QCOMPARE(lyricsEvents(), deleted);

        QVERIFY(chooseAt(inRow(columnOf(lyricsWord("Yksi").getFrame()) + 30),
                         "Delete Word"));
        QVERIFY(chooseAt(inRow(columnOf(lyricsWord("kaksi").getFrame()) + 30),
                         "Delete Word"));
        QVERIFY(lyricsEvents().empty());
        QVERIFY(m_window->lyrics()->isShown());
        QVERIFY(m_window->lyricsEditor()->isEnabled());
        m_row = lyricsBoxRow();
        QCOMPARE(menuAt(inRow(inside)), QStringList{ "Add Word..." });
        m_window->answerWordText("uusi");
        QVERIFY(chooseAt(inRow(inside), "Add Word..."));
        QCOMPARE(lyricsWord("uusi").getValue(), 0.f);
        QCOMPARE(lyricsWord("uusi").getDuration(), LyricsEdit::newWordFrames(rate));

        QCOMPARE(undoOnce(), QString("Add Word"));
        QCOMPARE(undoOnce(), QString("Delete Word"));
        QCOMPARE(undoOnce(), QString("Delete Word"));
        QCOMPARE(undoOnce(), QString("Delete Word"));
        QCOMPARE(lyricsEvents(), before);
    }

    // The question runs an event loop of its own, and anything can
    // happen while it is open: the word changed by an undo, edit mode
    // switched off, the lyrics removed.  The answer then changes nothing,
    // and nothing is pushed over what is on the history
    void lyrics_edit_words_change_during_question() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::EventVector before = lyricsEvents();
        int inside = columnOf(lyricsWord("kaksi").getFrame()) + 40;

        m_window->answerWordText("kaksikko");
        doubleClickAt(inRow(inside));
        sv::EventVector changed = lyricsEvents();
        QVERIFY(changed != before);

        m_window->whileAskingWordText([this]() { undoOnce(); });
        m_window->answerWordText("kaksikkoko");
        doubleClickAt(inRow(inside));
        QCOMPARE(m_window->wordTextQuestions(), 2);
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(redoOnce(), QString("Change Word Text"));
        QCOMPARE(lyricsEvents(), changed);
        QCOMPARE(redoOnce(), QString());

        // Deleted while its menu entry asks
        m_window->whileAskingWordText([this, inside]() {
            chooseAt(inRow(inside), "Delete Word");
        });
        m_window->answerWordText("kaksi");
        QVERIFY(chooseAt(inRow(inside), "Edit Word Text..."));
        QCOMPARE(m_window->wordTextQuestions(), 3);
        QCOMPARE(lyricsWord("kaksikko").getFrame(), sv::sv_frame_t(-1));
        QCOMPARE(int(lyricsEvents().size()), 2);
        QCOMPARE(undoOnce(), QString("Delete Word"));
        QCOMPARE(lyricsEvents(), changed);

        m_window->whileAskingWordText([this]() {
            m_window->editLyricsAction()->trigger();
        });
        m_window->answerWordText("kaksi");
        doubleClickAt(inRow(inside));
        QCOMPARE(m_window->wordTextQuestions(), 4);
        QVERIFY(!editor->isEnabled());
        QCOMPARE(lyricsEvents(), changed);
        switchLyricsEditingOn();
        if (QTest::currentTestFailed()) return;

        int gap = columnOf(endOf(lyricsWord("kaksikko"))) + 20;
        m_window->whileAskingWordText([this]() {
            m_window->removeLyricsAction()->trigger();
        });
        m_window->answerWordText("ja");
        QVERIFY(chooseAt(inRow(gap), "Add Word..."));
        QCOMPARE(m_window->wordTextQuestions(), 5);
        QVERIFY(!m_window->lyrics()->isShown());
        QVERIFY(!editor->isEnabled());

        // On top of the history, the first change, whose model has gone
        QCOMPARE(undoOnce(), QString("Change Word Text"));
        verifyPlaySourceClean();
    }

    // Export Lyrics after edits writes the words as edited: a text
    // changed, an end moved, a word added on the line of the word after
    // it, and a word deleted
    void lyrics_edit_export_writes_edits() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::Event yksi = lyricsWord("Yksi");
        sv::Event kolme = lyricsWord("kolme");

        m_window->answerWordText("Yksin");
        doubleClickAt(inRow(columnOf(yksi.getFrame()) + 30));
        int end = columnOf(endOf(lyricsWord("kaksi"))) - 1;
        dragFromTo(inRow(end), inRow(end - 30));
        m_window->answerWordText("ja");
        QVERIFY(chooseAt(inRow(columnOf(kolme.getFrame()) - 5), "Add Word..."));
        QVERIFY(chooseAt(inRow(columnOf(kolme.getFrame()) + 40), "Delete Word"));
        QCOMPARE(undoOnce(), QString("Delete Word"));
        QCOMPARE(redoOnce(), QString("Delete Word"));

        sv::EventVector events = lyricsEvents();
        Lyrics now = lyricsFromEvents(events, rate);
        QStringList texts;
        for (const LyricWord &w : now.words) texts << w.text;
        QCOMPARE(texts, (QStringList{ "Yksin", "kaksi", "ja" }));
        QCOMPARE(now.words[2].line, 1);

        m_window->discardModifications();
        QString path = m_dir.filePath("edited-lyrics.ttml");
        m_window->setLyricsExportAnswer(path);
        m_window->exportLyricsAction()->trigger();
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), "nothing was written");
        LyricsParseResult parsed = parseTtml(file.readAll());
        QVERIFY2(parsed.error == "", qPrintable(parsed.error));

        const QVector<LyricWord> &back = parsed.lyrics.words;
        QCOMPARE(back.size(), now.words.size());
        for (int i = 0; i < back.size(); ++i) {
            QString what = QString("word %1, \"%2\"").arg(i)
                .arg(now.words[i].text);
            QVERIFY2(back[i].text == now.words[i].text,
                     qPrintable(what + " came back as " + back[i].text));
            QVERIFY2(back[i].line == now.words[i].line,
                     qPrintable(what + ": another line"));
            QVERIFY2(std::fabs(back[i].start - now.words[i].start) < 0.0005001,
                     qPrintable(what + ": another start"));
            QVERIFY2(std::fabs(back[i].end - now.words[i].end) < 0.0005001,
                     qPrintable(what + ": another end"));
        }
        QVERIFY(std::fabs(back[1].end - 0.9) > 0.05);
        QVERIFY(!m_window->isDocumentModified());
        QCOMPARE(lyricsEvents(), events);
    }

    // Shift held at the press, anywhere in the box row (in a word, in a
    // gap, on an edge), a drag moves all the words together as it goes,
    // starts and ends alike, and is one "Shift Lyrics" step when let go
    // (decisions 17, 18a)
    void lyrics_edit_shift_drag() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        sv::EventVector before = lyricsEvents();
        QCOMPARE(int(before.size()), 3);

        // Later, from inside a word, each move seen in the model at once
        int inside = columnOf(lyricsWord("kaksi").getFrame()) + 40;
        shiftPressAt(inRow(inside));
        QVERIFY(editor->isShifting());
        for (int x = inside + 3; x <= inside + 30; x += 3) {
            shiftMoveHeldTo(inRow(x));
            QCOMPARE(lyricsEvents(), shiftedBy(before, framesBetween(inside, x)));
        }
        QCOMPARE(int(commands.count()), 0);
        shiftReleaseAt(inRow(inside + 30));
        QVERIFY(!editor->isDragging());
        sv::EventVector later =
            shiftedBy(before, framesBetween(inside, inside + 30));
        QCOMPARE(lyricsEvents(), later);
        QCOMPARE(int(commands.count()), 1);
        QVERIFY(m_window->isDocumentModified());

        // Earlier, from the gap between kaksi and kolme
        int gap = columnOf(endOf(lyricsWord("kaksi"))) + 20;
        shiftDragFromTo(inRow(gap), inRow(gap - 20));
        sv::EventVector earlier = shiftedBy(later, framesBetween(gap, gap - 20));
        QCOMPARE(lyricsEvents(), earlier);

        // From the shared edge: all the words, not the one edge
        int edge = columnOf(lyricsWord("kaksi").getFrame());
        shiftDragFromTo(inRow(edge), inRow(edge + 25));
        sv::EventVector fromEdge =
            shiftedBy(earlier, framesBetween(edge, edge + 25));
        QCOMPARE(lyricsEvents(), fromEdge);
        QCOMPARE(int(commands.count()), 3);

        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), earlier);
        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), later);
        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(undoOnce(), QString());
        QCOMPARE(redoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), later);
        QCOMPARE(redoOnce(), QString("Shift Lyrics"));
        QCOMPARE(redoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), fromEdge);
        QCOMPARE(redoOnce(), QString());

        QVERIFY(m_window->playSource()->getModels().count
                (m_window->lyrics()->getModelId()) == 0);
        verifyPlaySourceClean();
    }

    // The first word stops at 0, however far the pointer goes, and the
    // rest keep their places behind it; a drag past and back puts the
    // words where the pointer is.  Already at 0, a drag earlier is no
    // edit at all (decision 17)
    void lyrics_edit_shift_drag_stops_at_zero() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        sv::EventVector before = lyricsEvents();
        sv::sv_frame_t first = lyricsWord("Yksi").getFrame();
        QVERIFY(first > 0);
        int inside = columnOf(first) + 40;
        QVERIFY2(framesBetween(inside, 20) < -first - 1000,
                 "the pointer cannot go far enough left");

        shiftPressAt(inRow(inside));
        shiftMoveHeldTo(inRow(20));
        QCOMPARE(lyricsEvents(), shiftedBy(before, -first));
        shiftMoveHeldTo(inRow(inside - 10));
        shiftReleaseAt(inRow(inside - 10));
        QCOMPARE(lyricsEvents(),
                 shiftedBy(before, framesBetween(inside, inside - 10)));
        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), before);

        shiftDragFromTo(inRow(inside), inRow(20));
        sv::EventVector atZero = shiftedBy(before, -first);
        QCOMPARE(lyricsEvents(), atZero);
        QCOMPARE(lyricsWord("Yksi").getFrame(), sv::sv_frame_t(0));

        // (An undo and a redo are commands executed as well)
        m_window->discardModifications();
        int pushed = int(commands.count());
        int again = columnOf(0) + 40;
        shiftDragFromTo(inRow(again), inRow(again - 30));
        QCOMPARE(lyricsEvents(), atZero);
        QCOMPARE(int(commands.count()), pushed);
        QVERIFY(!m_window->isDocumentModified());

        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(undoOnce(), QString());
    }

    // The one command of a long drag holds the words as they were at the
    // press and as they are at the release, and nothing of the moves
    // between.  CommandHistory does not show what a command holds, but
    // the model says what is done to it: a word taken out is one signal,
    // a word put in two, so the undo and the redo of a command of N words
    // out and N in each give 3N at most, where a command that kept every
    // move would replay all of them
    void lyrics_edit_shift_drag_one_command() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        auto model = lyricsModel();
        QVERIFY(model);
        sv::EventVector before = lyricsEvents();
        int words = int(before.size());

        int inside = columnOf(lyricsWord("kaksi").getFrame()) + 40;
        shiftPressAt(inRow(inside));
        for (int i = 0; i < 40; ++i) {
            shiftMoveHeldTo(inRow(inside + (i % 2 ? 30 : -30) + i / 4));
        }
        shiftMoveHeldTo(inRow(inside + 17));
        shiftReleaseAt(inRow(inside + 17));
        sv::EventVector after =
            shiftedBy(before, framesBetween(inside, inside + 17));
        QCOMPARE(lyricsEvents(), after);

        int changes = 0;
        auto counted = [&changes]() { ++changes; };
        auto within = connect(model.get(), &sv::Model::modelChangedWithin,
                              this, counted);
        auto whole = connect(model.get(), &sv::Model::modelChanged,
                             this, counted);

        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), before);
        QVERIFY2(changes >= words && changes <= 3 * words,
                 qPrintable(QString("the undo changed the model %1 times "
                                    "for %2 words").arg(changes).arg(words)));
        changes = 0;
        QCOMPARE(redoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), after);
        QVERIFY2(changes >= words && changes <= 3 * words,
                 qPrintable(QString("the redo changed the model %1 times "
                                    "for %2 words").arg(changes).arg(words)));
        disconnect(within);
        disconnect(whole);
    }

    // What a press is, an edge's or all the words', is decided at the
    // press: a plain press on an edge moves that edge alone, Shift held
    // or not after, and a Shift press moves all the words, Shift let go
    // or not.  Pressed and let go, or dragged away and back: no edit
    void lyrics_edit_shift_decided_at_the_press() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        sv::EventVector before = lyricsEvents();
        sv::Event yksi = lyricsWord("Yksi");
        sv::Event kaksi = lyricsWord("kaksi");
        sv::Event kolme = lyricsWord("kolme");
        int edge = columnOf(kaksi.getFrame());

        pressAt(inRow(edge));
        QVERIFY(editor->isDragging());
        QVERIFY(!editor->isShifting());
        shiftMoveHeldTo(inRow(edge + 20));
        shiftReleaseAt(inRow(edge + 20));
        QCOMPARE(lyricsWord("kaksi").getFrame(),
                 kaksi.getFrame() + framesBetween(edge, edge + 20));
        QCOMPARE(endOf(lyricsWord("kaksi")), endOf(kaksi));
        QCOMPARE(lyricsWord("Yksi"), yksi);
        QCOMPARE(lyricsWord("kolme"), kolme);
        QCOMPARE(undoOnce(), QString("Move Word Start"));
        QCOMPARE(lyricsEvents(), before);

        shiftPressAt(inRow(edge));
        QVERIFY(editor->isShifting());
        moveHeldTo(inRow(edge + 20));
        releaseAt(inRow(edge + 20));
        QCOMPARE(lyricsEvents(),
                 shiftedBy(before, framesBetween(edge, edge + 20)));
        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), before);

        // (An undo is a command executed as well)
        m_window->discardModifications();
        int pushed = int(commands.count());
        int inside = edge + 40;
        shiftPressAt(inRow(inside));
        shiftReleaseAt(inRow(inside));
        shiftPressAt(inRow(inside));
        for (int x = inside; x <= inside + 30; x += 5) shiftMoveHeldTo(inRow(x));
        QVERIFY(lyricsEvents() != before);
        for (int x = inside + 30; x >= inside; x -= 5) shiftMoveHeldTo(inRow(x));
        shiftReleaseAt(inRow(inside));
        QVERIFY(!editor->isDragging());
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(int(commands.count()), pushed);
        QVERIFY(!m_window->isDocumentModified());
        QCOMPARE(undoOnce(), QString());
    }

    // Edit mode off, a Shift press is the pane's as any press is
    // (decision 19); edit mode on, so is one outside the box row
    void lyrics_edit_shift_press_elsewhere_is_the_panes() {
        makeWindow(FakeAudioIO::Config());
        showEditableLyrics();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::EventVector before = lyricsEvents();
        int inside = columnOf(lyricsWord("kaksi").getFrame()) + 40;

        shiftPressAt(inRow(inside));
        QVERIFY(!editor->isDragging());
        shiftMoveHeldTo(inRow(inside + 30));
        shiftReleaseAt(inRow(inside + 30));
        QCOMPARE(lyricsEvents(), before);

        switchLyricsEditingOn();
        if (QTest::currentTestFailed()) return;
        QPoint above(inside, m_row.top() - 8);
        shiftPressAt(above);
        QVERIFY(!editor->isDragging());
        shiftMoveHeldTo(QPoint(inside + 30, above.y()));
        shiftReleaseAt(QPoint(inside + 30, above.y()));
        QCOMPARE(lyricsEvents(), before);

        // The pane's Shift-drag outlines a region to analyse again
        QTRY_VERIFY_WITH_TIMEOUT(!sv::ModelTransformerFactory::getInstance()
                                 ->haveRunningTransformers(), 30000);
        QVERIFY(undoOnce() != QString("Shift Lyrics"));
    }

    // Edit mode switched off in the middle of a drag of all the words:
    // the drag ends as a release would, one step, and the rest of it is
    // not an edit
    void lyrics_edit_shift_off_in_the_middle_of_a_drag() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::EventVector before = lyricsEvents();

        int inside = columnOf(lyricsWord("kaksi").getFrame()) + 40;
        shiftPressAt(inRow(inside));
        shiftMoveHeldTo(inRow(inside + 30));
        sv::EventVector dragged = lyricsEvents();
        QCOMPARE(dragged, shiftedBy(before, framesBetween(inside, inside + 30)));

        m_window->editLyricsAction()->trigger();
        QVERIFY(!editor->isEnabled());
        QVERIFY(!editor->isDragging());
        QVERIFY(m_window->isDocumentModified());

        shiftMoveHeldTo(inRow(inside + 60));
        shiftReleaseAt(inRow(inside + 60));
        QCOMPARE(lyricsEvents(), dragged);

        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(undoOnce(), QString());
        QCOMPARE(redoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), dragged);
    }

    // The words changed under a drag of them all, by something other
    // than the drag: the drag ends, the model is left as that made it,
    // and nothing goes on the history.  Removed in the middle likewise
    void lyrics_edit_shift_words_change_under_a_drag() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::EventVector before = lyricsEvents();

        int inside = columnOf(lyricsWord("kaksi").getFrame()) + 40;
        shiftPressAt(inRow(inside));
        shiftMoveHeldTo(inRow(inside + 20));
        sv::Event kolme = lyricsWord("kolme");
        replaceWord(kolme, kolme.withLabel("kolmas"));
        sv::EventVector changed = lyricsEvents();
        shiftMoveHeldTo(inRow(inside + 40));
        QCOMPARE(lyricsEvents(), changed);
        shiftReleaseAt(inRow(inside + 40));
        QVERIFY(!editor->isDragging());
        QCOMPARE(lyricsEvents(), changed);
        QCOMPARE(undoOnce(), QString());

        shiftPressAt(inRow(inside));
        shiftMoveHeldTo(inRow(inside + 20));
        m_window->removeLyricsAction()->trigger();
        QVERIFY(!editor->isDragging());
        QVERIFY(!editor->isEnabled());
        shiftMoveHeldTo(inRow(inside + 40));
        shiftReleaseAt(inRow(inside + 40));
        QVERIFY(!m_window->lyrics()->isShown());
        QCOMPARE(undoOnce(), QString());
        verifyPlaySourceClean();
    }

    // Edit > Shift Lyrics...: all the words by the seconds typed, later or
    // earlier, one step each, the status bar saying how far they went;
    // the first word stops at 0, and a shift of 0, or one clamped to 0,
    // or a cancel, is no edit at all (decisions 18b, 20).  Edit mode need
    // not be on
    void lyrics_edit_shift_by_number() {
        makeWindow(FakeAudioIO::Config());
        showEditableLyrics();
        if (QTest::currentTestFailed()) return;
        QAction *shift = m_window->shiftLyricsAction();
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        sv::EventVector before = lyricsEvents();
        QVERIFY(!m_window->lyricsEditor()->isEnabled());
        QVERIFY(shift->isEnabled());

        m_window->answerLyricsShift(0.2);
        shift->trigger();
        QCOMPARE(m_window->lyricsShiftQuestions(), 1);
        sv::EventVector later = shiftedBy(before, 8820);
        QCOMPARE(lyricsEvents(), later);
        QCOMPARE(m_window->statusText(),
                 QString("Shifted the lyrics 0.200 s later."));
        QVERIFY(m_window->isDocumentModified());

        m_window->answerLyricsShift(-0.05);
        shift->trigger();
        sv::EventVector earlier = shiftedBy(later, -2205);
        QCOMPARE(lyricsEvents(), earlier);
        QCOMPARE(m_window->statusText(),
                 QString("Shifted the lyrics 0.050 s earlier."));
        QCOMPARE(int(commands.count()), 2);

        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), later);
        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), before);
        QCOMPARE(redoOnce(), QString("Shift Lyrics"));
        QCOMPARE(redoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), earlier);

        // Yksi starts at 0.35 s now: that far and no further
        QCOMPARE(lyricsWord("Yksi").getFrame(), sv::sv_frame_t(15435));
        m_window->answerLyricsShift(-5.0);
        shift->trigger();
        sv::EventVector atZero = shiftedBy(earlier, -15435);
        QCOMPARE(lyricsEvents(), atZero);
        QCOMPARE(m_window->statusText(),
                 QString("Shifted the lyrics 0.350 s earlier."));

        // Nothing to do: no step, and nothing said.  (An undo and a redo
        // are commands executed as well)
        int pushed = int(commands.count());
        m_window->discardModifications();
        m_window->setStatusText("before");
        m_window->answerLyricsShift(-1.0);
        m_window->answerLyricsShift(0.0);
        m_window->answerLyricsShift(0.00001);
        m_window->cancelLyricsShift();
        for (int i = 0; i < 4; ++i) shift->trigger();
        QCOMPARE(m_window->lyricsShiftQuestions(), 7);
        QCOMPARE(lyricsEvents(), atZero);
        QCOMPARE(int(commands.count()), pushed);
        QVERIFY(!m_window->isDocumentModified());
        QCOMPARE(m_window->statusText(), QString("before"));

        // With edit mode on as well, which stays on
        switchLyricsEditingOn();
        if (QTest::currentTestFailed()) return;
        m_window->answerLyricsShift(1.5);
        shift->trigger();
        QCOMPARE(lyricsEvents(), shiftedBy(atZero, 66150));
        QVERIFY(m_window->lyricsEditor()->isEnabled());
        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), atZero);
    }

    // To be had when editing is (lyricsEditAllowed()), edit mode on or
    // not: not without lyrics, hidden ones, or once they are removed; and
    // a trigger then asks nothing
    void lyrics_edit_shift_needs_lyrics() {
        makeWindow(FakeAudioIO::Config());
        QAction *shift = m_window->shiftLyricsAction();
        QVERIFY(shift);
        QVERIFY(!shift->isEnabled());
        openReference(writeWav(tone(lowHz, 2.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(!shift->isEnabled());
        m_window->answerLyricsShift(0.5);
        shift->trigger();
        QCOMPARE(m_window->lyricsShiftQuestions(), 0);

        QVERIFY(m_window->doImportLyricsFrom(writeLrc(gappedLyrics())));
        QVERIFY(shift->isEnabled());
        m_window->showLyricsAction()->trigger();
        QVERIFY(!shift->isEnabled());
        sv::EventVector before = lyricsEvents();
        shift->trigger();
        QCOMPARE(m_window->lyricsShiftQuestions(), 0);
        QCOMPARE(lyricsEvents(), before);
        m_window->showLyricsAction()->trigger();
        QVERIFY(shift->isEnabled());

        m_window->removeLyricsAction()->trigger();
        QVERIFY(!shift->isEnabled());
        shift->trigger();
        QCOMPARE(m_window->lyricsShiftQuestions(), 0);
    }

    // Not while a take is recorded.  A drag of all the words going on when
    // the take starts is finished first, and so is on the history before
    // the take
    void lyrics_edit_shift_off_while_recording() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        lyricsEditFixture(config);
        if (QTest::currentTestFailed()) return;
        QAction *shift = m_window->shiftLyricsAction();
        LyricsEditor *editor = m_window->lyricsEditor();
        sv::EventVector before = lyricsEvents();

        int inside = columnOf(lyricsWord("kaksi").getFrame()) + 40;
        shiftPressAt(inRow(inside));
        shiftMoveHeldTo(inRow(inside + 30));
        sv::EventVector dragged = lyricsEvents();
        QVERIFY(dragged != before);

        startTake();
        if (QTest::currentTestFailed()) return;
        QVERIFY(!editor->isDragging());
        QVERIFY(!shift->isEnabled());
        m_window->answerLyricsShift(0.5);
        shift->trigger();
        QCOMPARE(m_window->lyricsShiftQuestions(), 0);

        shiftMoveHeldTo(inRow(inside + 60));
        shiftReleaseAt(inRow(inside + 60));
        QCOMPARE(lyricsEvents(), dragged);

        QTest::qWait(300);
        stopTake();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(shift->isEnabled(), 2000);

        QCOMPARE(undoOnce(), QString("Record Singing"));
        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(lyricsEvents(), before);
    }

    // The question has an event loop of its own: lyrics removed, hidden or
    // imported again while it is open are not what the seconds were typed
    // for, and nothing is shifted
    void lyrics_edit_shift_lyrics_change_during_question() {
        makeWindow(FakeAudioIO::Config());
        showEditableLyrics();
        if (QTest::currentTestFailed()) return;
        QAction *shift = m_window->shiftLyricsAction();
        QSignalSpy commands(sv::CommandHistory::getInstance(), qOverload<>
                            (&sv::CommandHistory::commandExecuted));
        QString file = writeLrc(gappedLyrics());
        sv::EventVector before = lyricsEvents();

        m_window->whileAskingLyricsShift([this, file]() {
            QVERIFY(m_window->doImportLyricsFrom(file));
        });
        m_window->answerLyricsShift(0.3);
        shift->trigger();
        QCOMPARE(m_window->lyricsShiftQuestions(), 1);
        QCOMPARE(lyricsEvents(), before);

        m_window->whileAskingLyricsShift([this]() {
            m_window->showLyricsAction()->trigger();
        });
        m_window->answerLyricsShift(0.3);
        shift->trigger();
        QCOMPARE(lyricsEvents(), before);
        m_window->showLyricsAction()->trigger();

        m_window->whileAskingLyricsShift([this]() {
            m_window->removeLyricsAction()->trigger();
        });
        m_window->answerLyricsShift(0.3);
        shift->trigger();
        QCOMPARE(m_window->lyricsShiftQuestions(), 3);
        QVERIFY(!m_window->lyrics()->isShown());
        QCOMPARE(int(commands.count()), 0);
        QCOMPARE(undoOnce(), QString());
    }

    // The word at the cursor is found again as all the words move, by a
    // drag and by a number
    void lyrics_edit_shift_highlight_follows() {
        lyricsEditFixture();
        if (QTest::currentTestFailed()) return;
        sv::Event kaksi = lyricsWord("kaksi");
        sv::Event kolme = lyricsWord("kolme");
        sv::sv_frame_t gap = (endOf(kaksi) + kolme.getFrame()) / 2;
        m_window->seekTo(gap);
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString(), 1000);

        // About 0.29 s later: kaksi, 0.6 to 0.9 s, then covers 1.15 s
        int inside = columnOf(kaksi.getFrame()) + 40;
        shiftPressAt(inRow(inside));
        shiftMoveHeldTo(inRow(inside + 100));
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString("kaksi"), 1000);
        shiftReleaseAt(inRow(inside + 100));
        QCOMPARE(highlightedWord(), QString("kaksi"));

        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString(), 1000);

        m_window->answerLyricsShift(0.3);
        m_window->shiftLyricsAction()->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(highlightedWord(), QString("kaksi"), 1000);
    }

    // Export Lyrics after a shift writes the shifted times
    void lyrics_edit_shift_then_export() {
        makeWindow(FakeAudioIO::Config());
        showEditableLyrics();
        if (QTest::currentTestFailed()) return;
        Lyrics was = lyricsFromEvents(lyricsEvents(), rate);

        m_window->answerLyricsShift(0.25);
        m_window->shiftLyricsAction()->trigger();
        QCOMPARE(undoOnce(), QString("Shift Lyrics"));
        QCOMPARE(redoOnce(), QString("Shift Lyrics"));

        QString path = m_dir.filePath("shifted-lyrics.ttml");
        m_window->setLyricsExportAnswer(path);
        m_window->exportLyricsAction()->trigger();
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), "nothing was written");
        LyricsParseResult parsed = parseTtml(file.readAll());
        QVERIFY2(parsed.error == "", qPrintable(parsed.error));

        const QVector<LyricWord> &back = parsed.lyrics.words;
        QCOMPARE(back.size(), was.words.size());
        for (int i = 0; i < back.size(); ++i) {
            QString what = QString("word %1, \"%2\"").arg(i)
                .arg(was.words[i].text);
            QVERIFY2(back[i].text == was.words[i].text,
                     qPrintable(what + " came back as " + back[i].text));
            QVERIFY2(back[i].line == was.words[i].line,
                     qPrintable(what + ": another line"));
            QVERIFY2(std::fabs(back[i].start - (was.words[i].start + 0.25))
                     < 0.0005001, qPrintable(what + ": another start"));
            QVERIFY2(std::fabs(back[i].end - (was.words[i].end + 0.25))
                     < 0.0005001, qPrintable(what + ": another end"));
        }
    }

    // Closing while pYIN is still running on the take (review finding
    // 15). Unless the analysis is cancelled first, about one run in
    // three under CPU load destroys the take's model on the transform
    // thread ("Timers cannot be stopped from another thread") and the
    // process dies with an access violation soon after. A regression
    // shows up as a crash of the whole test program, and not reliably:
    // run it under load to check.  The analysis is held, so that its run
    // is there, unmerged, at the close however quick the machine; whether
    // its thread is still going then as well depends on the machine and
    // its load.
    void close_session_during_analysis() {
        FakeAudioIO::Config config;
        config.input = tone(highHz, 3.0);
        makeWindow(config);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(600);

        // Stop splices the recording into the take and starts the analysis
        // of the result there and then, so it is running when the session
        // is closed. Otherwise this test shows nothing
        m_window->holdRangedMerges(true);
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY2(sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(),
                 "the race was not set up: no analysis was running when "
                 "the session was about to be closed");
        QVERIFY(m_window->analysingRange());
        m_window->doCloseSession();
        m_window->holdRangedMerges(false);

        QVERIFY(!m_window->analyser2());
        QCOMPARE(m_window->paneStack()->getPaneCount(), 0);

        openReference(writeWav(tone(highHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           highHz)) < 10.0);
    }
};

#endif
