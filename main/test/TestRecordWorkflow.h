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
#include "FakeAudioIO.h"

#include "../MainWindow.h"
#include "../Analyser.h"
#include "../CoverageStrip.h"
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
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

/**
 * MainWindow with the fake device in place of a real one, and the
 * protected state of the singing workflow opened up for inspection.
 */
class TestMainWindow : public MainWindow
{
public:
    TestMainWindow(FakeAudioIO::Config config, bool installDevice = true) :
        MainWindow(AUDIO_PLAYBACK_AND_RECORD, true, false),
        m_fakeConfig(config),
        m_installDevice(installDevice) { }

    FakeAudioIO *fake() { return dynamic_cast<FakeAudioIO *>(m_audioIO); }

    void doRecord() { record(); }
    void doPlay() { play(); } // and again to stop
    void doAnalyseNow() { analyseNow(); }
    void doLoadBackgroundMusic(QString path) { loadBackgroundMusic(path); }

    // Another audio file under the take's pitch and notes layers
    QString doSwapSingingAudio(QString path) {
        return swapSingingAudio(path);
    }

    // Editing the singing of a take, as the two Edit menu actions do
    void doEraseSingingInSelection() { eraseSingingInSelection(); }
    void doSelectRecordingAtPlayhead() { selectRecordingAtPlayhead(); }
    QAction *eraseSingingAction() { return m_eraseSingingAction; }
    QAction *selectRecordingAction() { return m_selectRecordingAction; }
    void doUpdateMenuStates() { updateMenuStates(); }

    // The takes of the session, as the Takes menu and the combo box do
    bool doSwitchToTake(int index) { return switchToTake(index); }
    void doChooseTakeInCombo(int index) { m_takeCombo->setCurrentIndex(index); }
    void doNewEmptyTake() { newEmptyTake(); }
    void doDuplicateTake() { duplicateTake(); }
    void doRenameTake() { renameTake(); }
    void doDeleteTake() { deleteTake(); }
    bool doDeleteTakeAt(int index) { return deleteTakeAt(index); }
    QComboBox *takeCombo() { return m_takeCombo; }
    QAction *newTakeAction() { return m_newTakeAction; }
    QAction *duplicateTakeAction() { return m_duplicateTakeAction; }
    QAction *renameTakeAction() { return m_renameTakeAction; }
    QAction *deleteTakeAction() { return m_deleteTakeAction; }

    // The two questions the take operations ask, answered from here: the
    // suite cannot answer a dialog
    void setDeleteTakeAnswer(bool yes) { m_deleteTakeAnswer = yes; }
    int deleteTakeQuestions() const { return m_deleteTakeQuestions; }
    void setTakeNameAnswer(QString name) { m_takeNameAnswer = name; }

    // True between the start of the analysis of a recorded range and the
    // merge of its result into the take's pitch and notes
    bool analysingRange() {
        return m_analyser2 && m_analyser2->isAnalysingRange();
    }
    sv::sv_frame_t analysedRangeStart() { return m_takeAnalysisRange.start; }
    sv::sv_frame_t analysedRangeEnd() { return m_takeAnalysisRange.end; }

    // Save As, with the file name given here instead of by a dialog: the
    // session's own file is set, so that what is recorded next goes into
    // its takes folder
    bool doSaveSessionAs(QString path) { return saveSessionToPath(path); }
    QString sessionFile() { return m_sessionFile; }

    // As answering "No" to "do you want to save?"
    void discardModifications() { m_documentModified = false; }
    bool isDocumentModified() { return m_documentModified; }
    void doCloseSession() { discardModifications(); closeSession(); }

    void setPlayReferenceWhileRecording(bool on) {
        m_playRefWhileRecording->setChecked(on);
    }
    void setPreRoll(bool on) { m_preRoll->setChecked(on); }
    void setRecordIntoSelection(bool on) {
        m_recordIntoSelection->setChecked(on);
    }
    QAction *playReferenceWhileRecordingAction() {
        return m_playRefWhileRecording;
    }
    QAction *preRollAction() { return m_preRoll; }
    QAction *recordIntoSelectionAction() { return m_recordIntoSelection; }

    // The audio check, the override it sets for its own takes, and what
    // the last take was placed with
    AudioCheckRunner *audioCheck() { return m_audioCheck; }
    bool audioCheckTakes() { return m_audioCheckTakes; }
    TakeLatency takeLatency() { return m_takeLatency; }
    QAction *playSingingAudioAction() { return m_playSingingAudio; }

    Analyser *analyser() { return m_analyser; }
    Analyser *analyser2() { return m_analyser2; }
    sv::Document *document() { return m_document; }
    sv::PaneStack *paneStack() { return m_paneStack; }
    sv::Layer *timeRuler() { return m_timeRulerLayer; }
    sv::AudioCallbackRecordTarget *recordTarget() { return m_recordTarget; }
    sv::AudioCallbackPlaySource *playSource() { return m_playSource; }
    sv::ModelId mainModelId() { return getMainModelId(); }

    RealtimePitchTracker *realtimeTracker() { return m_realtimePitchTracker; }
    sv::TimeValueLayer *realtimeLayer() { return m_realtimePitchLayer; }
    sv::ModelId realtimeModelId() { return m_realtimePitchModelId; }
    sv::ModelId currentRecordingModelId() { return m_currentRecordingModelId; }
    sv::WaveformLayer *recordingLayer() { return m_recordingLayer; }
    SingingTakes *takes() { return m_takes; }
    sv::sv_frame_t takePosition() { return m_takePosition; }
    sv::sv_frame_t takePreRoll() { return m_takePreRoll; }
    sv::sv_frame_t takeEnd() { return m_takeEnd; }
    bool takeTimerRunning() { return m_takeTimer && m_takeTimer->isActive(); }

    void seekTo(sv::sv_frame_t frame) {
        m_viewManager->setPlaybackFrame(frame);
    }
    sv::sv_frame_t playbackFrame() { return m_viewManager->getPlaybackFrame(); }

    void selectRange(sv::sv_frame_t start, sv::sv_frame_t end) {
        m_viewManager->addSelection(sv::Selection(start, end));
    }
    void clearSelections() { m_viewManager->clearSelections(); }
    sv::MultiSelection::SelectionList selections() {
        return m_viewManager->getSelections();
    }

    // The question about recording over singing that is there is answered
    // from here: the suite cannot answer a dialog
    void setRecordOverAnswer(bool yes) { m_recordOverAnswer = yes; }
    int recordOverQuestions() const { return m_recordOverQuestions; }
    void clearRecordOverQuestions() { m_recordOverQuestions = 0; }

    sv::ModelId pendingSingingModelId() { return m_pendingSingingModelId; }
    sv::ModelId backgroundMusicModelId() { return m_backgroundMusicModelId; }
    sv::WaveformLayer *backgroundMusicLayer() { return m_backgroundMusicLayer; }
    bool recordingInProgress() { return m_recordingInProgress; }
    bool recordingAsSingingTrack() { return m_recordingAsSingingTrack; }
    sv::sv_frame_t recordingLatencyFrames() { return m_recordingLatencyFrames; }
    int pendingExtraPaneCount() { return int(m_pendingExtraPanes.size()); }

    CoverageStrip *coverageStrip() { return m_coverageStrip; }

    AlternatePitchTrack *alternatePitch() { return m_alternatePitch; }
    void doToggleAlternatePitch() { alternatePitchToggled(); }
    void doStepAlternatePitch(bool up) {
        if (up) alternatePitchUp(); else alternatePitchDown();
    }
    QAction *alternatePitchAction() { return m_showAlternatePitch; }
    QAction *alternatePitchUpAction() { return m_alternatePitchUpAction; }
    QAction *alternatePitchDownAction() { return m_alternatePitchDownAction; }

    void doRealtimePitchDetected(sv::sv_frame_t frame, double hz) {
        onRealtimePitchDetected(frame, hz);
    }
    QString statusText() { return getStatusLabel()->text(); }
    void setStatusText(QString text) { getStatusLabel()->setText(text); }

protected:
    void createAudioIO() override {
        if (m_audioIO || m_playTarget) return;
        if (!m_installDevice) return;
        m_fakeConfig.inputIsKept = [this]() {
            return m_recordTarget->isRecording();
        };
        m_audioIO = new FakeAudioIO
            (m_recordTarget, m_playSource->getApplicationPlaybackSource(),
             m_fakeConfig);
        m_playSource->setSystemPlaybackTarget(m_audioIO);
    }

    bool confirmRecordingOverTake() override {
        ++m_recordOverQuestions;
        return m_recordOverAnswer;
    }

    bool confirmDeleteTake(QString) override {
        ++m_deleteTakeQuestions;
        return m_deleteTakeAnswer;
    }

    QString askForTakeName(QString current) override {
        return m_takeNameAnswer == "" ? current : m_takeNameAnswer;
    }

    // The base class deleteAudioIO() deletes m_audioIO, which is right
    // for the fake as well

private:
    FakeAudioIO::Config m_fakeConfig;
    bool m_installDevice;
    bool m_recordOverAnswer = true;
    int m_recordOverQuestions = 0;
    bool m_deleteTakeAnswer = true;
    int m_deleteTakeQuestions = 0;
    QString m_takeNameAnswer;
};

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
        if (takeAudio()) s.frames = takeAudio()->getFrameCount();
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
        QCOMPARE(used.reportedOutput, sv::sv_frame_t(reportedOut));
        QCOMPARE(used.reportedInput, sv::sv_frame_t(reportedIn));
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
    // session, converted as it resamples
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
                 sv::sv_frame_t(std::lround(reportedOut * rate / deviceRate)));
        QCOMPARE(used.reportedInput, sv::sv_frame_t(reportedIn));
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
    // is the same
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
        QCOMPARE(used.reportedOutput, sv::sv_frame_t(reportedOut));
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
        // still going: it is remembered only until the merge, so a run that
        // finishes before the splice call returns never records one at all
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        if (m_window->analysingRange()) {
            QCOMPARE(m_window->analysedRangeStart(), P);
        }
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
        // loop run: the result of a ranged analysis is merged from a
        // queued call, so the first one cannot have finished by the time
        // this one stops, however quick the machine is. (The device
        // records from a thread of its own, and the record target's ring
        // buffer holds ten seconds.)
        const sv::sv_frame_t P = sv::sv_frame_t(3.0 * rate);
        m_window->seekTo(P);
        startTake();
        if (QTest::currentTestFailed()) return;
        QThread::msleep(250);
        QVERIFY2(m_window->analysingRange(),
                 "the first range's analysis finished before the second take "
                 "stopped: something ran the event loop");
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());

        // The analysis now running covers both recordings
        QVERIFY(m_window->analysingRange());
        QCOMPARE(m_window->analysedRangeStart(), sv::sv_frame_t(0));
        QVERIFY(m_window->analysedRangeEnd() > P);

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
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY2(sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(),
                 "the race was not set up: no analysis was running when the "
                 "second take started");
        QString first = m_window->takes()->getAudioPath();
        QVERIFY(!first.isEmpty());

        // In a gap, so nothing is asked
        m_window->seekTo(sv::sv_frame_t(1.5 * rate));
        take(500);
        if (QTest::currentTestFailed()) return;
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

        m_window->doAnalyseNow();

        // One run over the span of the coverage, not one per range
        QVERIFY(m_window->analysingRange());
        QCOMPARE(m_window->analysedRangeStart(), ranges[0].start);
        QCOMPARE(m_window->analysedRangeEnd(), ranges[1].end);

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
        // there and then
        m_window->seekTo(sv::sv_frame_t(2.0 * rate));
        startTake();
        if (QTest::currentTestFailed()) return;
        QTest::qWait(700);
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
        QCOMPARE(m_window->analysedRangeStart(), analysedStart);
        QCOMPARE(m_window->analysedRangeEnd(), analysedEnd);
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

    // Closing while pYIN is still running on the take (review finding
    // 15). Unless the analysis is cancelled first, about one run in
    // three under CPU load destroys the take's model on the transform
    // thread ("Timers cannot be stopped from another thread") and the
    // process dies with an access violation soon after. A regression
    // shows up as a crash of the whole test program, and not reliably:
    // run it under load to check.
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
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
        QVERIFY2(sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(),
                 "the race was not set up: no analysis was running when "
                 "the session was about to be closed");
        m_window->doCloseSession();

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
