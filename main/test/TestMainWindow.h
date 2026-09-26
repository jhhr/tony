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

#ifndef TEST_MAIN_WINDOW_H
#define TEST_MAIN_WINDOW_H

// The real MainWindow for the suites that drive it: TestRecordWorkflow,
// TestUiChecks, TestAudioCheck and TestDevChecks

#include "FakeAudioIO.h"

#include "../MainWindow.h"
#include "../Analyser.h"
#include "../CoverageStrip.h"
#include "../SingingTakes.h"

#ifdef TONY_DEV_CHECKS
#include "../dev/DevChecks.h"
#endif

#include "view/ViewManager.h"
#include "audio/AudioCallbackPlaySource.h"
#include "audio/AudioCallbackRecordTarget.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QTimer>

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
    QMenu *takesMenu() { return m_takesMenu; }

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

    // Hold the merge of the analysis of each recorded range until let go,
    // in the take's analyser and in every one made after it, so that a
    // test can act while a range is being analysed: pYIN may analyse a
    // short one before Stop returns (Analyser::setRangedMergeHeld())
    void holdRangedMerges(bool hold) {
        m_holdRangedMerges = hold;
        if (m_analyser2) m_analyser2->setRangedMergeHeld(hold);
    }

    // Save As, with the file name given here instead of by a dialog: the
    // session's own file is set, so that what is recorded next goes into
    // its takes folder
    bool doSaveSessionAs(QString path) { return saveSessionToPath(path); }
    QString sessionFile() { return m_sessionFile; }

    // As answering "No" to "do you want to save?"
    void discardModifications() { m_documentModified = false; }
    bool isDocumentModified() { return m_documentModified; }

    // As any edit does
    void markModified() { documentModified(); }
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

    // The Record button, as the user presses it
    QAction *recordAction() { return m_recordAction; }

#ifdef TONY_DEV_CHECKS
    // The development checks; deleted as the window's destructor deletes
    // them, with the window left, and then as a release build has it
    DevChecks *devChecks() { return m_devChecks; }
    void doDeleteDevChecks() {
        delete m_devChecks;
        m_devChecks = nullptr;
    }
#endif

    // Playback > Calibrate Audio, the dialog it shows once it has been
    // chosen, the lines under it, and the device menus above it
    QAction *calibrateAudioAction() { return m_calibrateAudioAction; }
    CalibrateAudioDialog *calibrateAudioDialog() {
        return m_calibrateAudioDialog;
    }
    QAction *latencyLineAction() { return m_latencyLineAction; }
    QAction *forgetLatencyAction() { return m_forgetLatencyAction; }
    QMenu *playbackMenu() { return m_playbackMenu; }
    QMenu *audioOutputMenu() { return m_audioDeviceMenu; }
    QMenu *audioInputMenu() { return m_audioInputDeviceMenu; }
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

    // ... or by MainWindow's own dialog, for a test that answers it by
    // pressing its buttons
    void setRecordOverAskedInDialog(bool on) { m_recordOverInDialog = on; }

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
        if (m_recordOverInDialog) {
            return MainWindow::confirmRecordingOverTake();
        }
        return m_recordOverAnswer;
    }

    bool confirmDeleteTake(QString) override {
        ++m_deleteTakeQuestions;
        return m_deleteTakeAnswer;
    }

    QString askForTakeName(QString current) override {
        return m_takeNameAnswer == "" ? current : m_takeNameAnswer;
    }

    // Every take analyser is made here, for a take's first recording and
    // for each swap of its audio, before its range is analysed
    void setupSingingTrackAnalyser(sv::ModelId singingModelId,
                                   bool deferAnalysis = false) override {
        MainWindow::setupSingingTrackAnalyser(singingModelId, deferAnalysis);
        if (m_analyser2 && m_holdRangedMerges) {
            m_analyser2->setRangedMergeHeld(true);
        }
    }

    // The base class deleteAudioIO() deletes m_audioIO, which is right
    // for the fake as well

private:
    FakeAudioIO::Config m_fakeConfig;
    bool m_installDevice;
    bool m_recordOverAnswer = true;
    bool m_recordOverInDialog = false;
    int m_recordOverQuestions = 0;
    bool m_deleteTakeAnswer = true;
    int m_deleteTakeQuestions = 0;
    QString m_takeNameAnswer;
    bool m_holdRangedMerges = false;
};

#endif
