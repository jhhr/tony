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

#include "version.h"

#include "framework/Document.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/ViewManager.h"
#include "layer/Layer.h"
#include "layer/ColourDatabase.h"
#include "layer/SingleColourLayer.h"
#include "layer/TimeValueLayer.h"
#include "layer/WaveformLayer.h"
#include "audio/AudioCallbackPlaySource.h"
#include "audio/AudioCallbackRecordTarget.h"
#include "data/model/WritableWaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/fileio/WavFileWriter.h"
#include "base/PlayParameters.h"
#include "base/RecordDirectory.h"
#include "transform/ModelTransformerFactory.h"
#include "widgets/InteractiveFileFinder.h"

#include <QObject>
#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
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
    void doAnalyseNow() { analyseNow(); }
    void doLoadBackgroundMusic(QString path) { loadBackgroundMusic(path); }

    // As answering "No" to "do you want to save?"
    void discardModifications() { m_documentModified = false; }
    void doCloseSession() { discardModifications(); closeSession(); }

    void setPlayReferenceWhileRecording(bool on) {
        m_playRefWhileRecording->setChecked(on);
    }

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
    sv::ModelId pendingSingingModelId() { return m_pendingSingingModelId; }
    sv::ModelId backgroundMusicModelId() { return m_backgroundMusicModelId; }
    sv::WaveformLayer *backgroundMusicLayer() { return m_backgroundMusicLayer; }
    bool recordingInProgress() { return m_recordingInProgress; }
    bool recordingAsSingingTrack() { return m_recordingAsSingingTrack; }
    sv::sv_frame_t recordingLatencyFrames() { return m_recordingLatencyFrames; }
    int pendingExtraPaneCount() { return int(m_pendingExtraPanes.size()); }

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

    // The base class deleteAudioIO() deletes m_audioIO, which is right
    // for the fake as well

private:
    FakeAudioIO::Config m_fakeConfig;
    bool m_installDevice;
};

class TestRecordWorkflow : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;
    static constexpr int hop = 256; // as set in Analyser::addAnalyses()

    // Whole numbers of samples per period: see TestSingingAnalysis.h
    static constexpr double lowHz = 220.5;
    static constexpr double highHz = 294.0;

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

    int layersOnModel(sv::ModelId id) {
        int n = 0;
        for (sv::Layer *layer : m_window->document()->getLayers()) {
            if (layer->getModel() == id) ++n;
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

    void verifyRulerIntact() {
        QVERIFY(m_window->timeRuler());
        QVERIFY2(documentHasLayer(m_window->timeRuler()),
                 "the shared time ruler was deleted from the document");
        QVERIFY2(paneHasLayer(1, m_window->timeRuler()),
                 "the shared time ruler is no longer in the ruler pane");
    }

    // Not a slot: QtTest would run it as a test
    void dismissDialog() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) return;
        QString description = modal->windowTitle();
        if (auto box = qobject_cast<QMessageBox *>(modal)) {
            description += ": " + box->text();
        }
        m_dialogs.push_back(description);
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
        settings.endGroup();
    }

    void cleanup() {
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
        auto wave = sv::ModelById::getAs<sv::WritableWaveFileModel>(singing);
        QVERIFY2(wave, "the singing model is not a WritableWaveFileModel");
        QVERIFY(wave->getFrameCount() > sv::sv_frame_t(0.8 * rate));

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
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());

        auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>(liveModel);
        QVERIFY(model);
        int dots = model->getEventCount();
        m_window->setStatusText("after the take");
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

        // Nothing is drawn ahead of the reference
        QVERIFY(dots.empty() || dots.front().getFrame() >= 0);
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

        auto wave = sv::ModelById::getAs<sv::WritableWaveFileModel>
            (m_window->analyser2()->getMainModelId());
        QVERIFY(wave);

        // The shift is the round trip plus what the application took
        // the start gap to be (review finding 14). The device knows
        // what the gap really was. The application counts in whole
        // blocks, in the audio callback, so the two agree to within a
        // few samples; an estimate made on the GUI thread is out by a
        // block or more
        sv::sv_frame_t gap = m_window->fake()->getFramesBeforePlayStart();
        sv::sv_frame_t assumedGap = -wave->getStartFrame() - K;
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
        auto wave = sv::ModelById::getAs<sv::WritableWaveFileModel>
            (m_window->analyser2()->getMainModelId());
        QVERIFY(wave);
        QCOMPARE(wave->getStartFrame(), sv::sv_frame_t(0));
        QCOMPARE(m_window->fake()->getPlayStartFrame(), -1L);
    }

    // The take and the live pitch model are both in the play source
    // while the reference plays (review finding 3), but neither is
    // heard: the play source fills its buffers seconds ahead of the
    // playback position, and the take has no audio that far ahead yet.
    // This test holds that in place.
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

    // As above with the take's own waveform muted, leaving only the
    // synth that follows the live pitch model
    void no_synth_tone() {
        FakeAudioIO::Config config;
        config.input = TestSignals::sine(highHz, rate, int(3 * rate), 0.5);
        makeWindow(config);
        m_window->setPlayReferenceWhileRecording(true);
        openReference(writeWav(TestSignals::sine(lowHz, rate,
                                                 int(3 * rate), 0.5)));
        if (QTest::currentTestFailed()) return;

        startTake();
        if (QTest::currentTestFailed()) return;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->analyser2() &&
                                 m_window->analyser2()->getLayer
                                 (Analyser::Audio), 2000);
        auto params = m_window->analyser2()->getLayer(Analyser::Audio)
            ->getPlayParameters();
        QVERIFY(params);
        params->setPlayAudible(false);
        QTest::qWait(1800);
        stopTake();
        if (QTest::currentTestFailed()) return;

        auto output = m_window->fake()->getCapturedOutput();
        long start = m_window->fake()->getPlayStartFrame();
        QVERIFY2(start >= 0, "the reference was never played");
        size_t from = size_t(start) + size_t(0.8 * rate);
        double synth = amplitudeAt(output, from, 26400, highHz);
        QVERIFY2(synth >= 0.0 && synth < 0.005,
                 qPrintable(QString("the output has a tone at the live "
                                    "pitch, at amplitude %1").arg(synth)));
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

    // No device at all: the base class record() gives up quietly
    void record_failure_resets_flags() {
        makeWindow(FakeAudioIO::Config(), false);
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;

        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());

        QVERIFY(!m_window->recordingAsSingingTrack());

        // so that the next file opened is analysed as usual
        openReference(writeWav(tone(highHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        QVERIFY(std::fabs(TestSignals::centsBetween
                          (medianHz(pitchEvents(m_window->analyser())),
                           highHz)) < 10.0);
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

    void load_singing_track() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        int panes = m_window->paneStack()->getPaneCount();
        sv::Layer *refLayer =
            m_window->analyser()->getLayer(Analyser::PitchTrack);
        sv::ModelId refModel = refLayer->getModel();

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

        // The open half of finding 7: the reference is handed to its
        // analyser a second time, which today changes nothing
        QCOMPARE(m_window->analyser()->getLayer(Analyser::PitchTrack),
                 refLayer);
        QCOMPARE(refLayer->getModel(), refModel);
    }

    void load_background_music() {
        makeWindow(FakeAudioIO::Config());
        openReference(writeWav(tone(lowHz, 1.0)));
        if (QTest::currentTestFailed()) return;
        int panes = m_window->paneStack()->getPaneCount();
        int rulerPaneLayers = m_window->paneStack()->getPane(1)->getLayerCount();

        m_window->doLoadBackgroundMusic(writeWav(tone(highHz, 1.0)));
        QCoreApplication::processEvents();

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
        QCOMPARE(wave->getStartFrame(), -shift);
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
        QVERIFY(m_window->pendingSingingModelId().isNone());
        QVERIFY(!m_window->recordingAsSingingTrack());
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
        m_window->doRecord();
        QVERIFY(!m_window->recordTarget()->isRecording());
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
