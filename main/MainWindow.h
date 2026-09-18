/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006-2012 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef TONY_MAIN_WINDOW_H
#define TONY_MAIN_WINDOW_H

#include "framework/MainWindowBase.h"
#include "Analyser.h"
#include "RealtimePitchTracker.h"

#include <vector>
#include <atomic>

#include "data/model/SparseTimeValueModel.h"

namespace sv {
class VersionTester;
class ActivityLog;
class LevelPanToolButton;
class TimeValueLayer;
}

class MainWindow : public sv::MainWindowBase
{
    Q_OBJECT

public:
    MainWindow(AudioMode audioMode,
               bool withSonification = true, 
               bool withSpectrogram = true);
    virtual ~MainWindow();

    /**
     * Load a second audio file as the "singing" track whose pitch
     * will be analysed alongside the primary reference track.
     */
    void loadSingingTrack(QString path);

signals:
    void canExportPitchTrack(bool);
    void canExportNotes(bool);
    void canSnapNotes(bool);
    void canPlayWaveform(bool);
    void canPlayPitch(bool);
    void canPlayNotes(bool);
    void canLoadSingingTrack(bool);
    void canShowRealtimePitch(bool);

public slots:
    virtual bool commitData(bool mayAskUser); // on session shutdown

protected slots:
    // Override record() so that when a reference track is already loaded we
    // can switch to RecordCreateAdditionalModel before starting the capture,
    // causing the recording to be treated as the singing track.
    virtual void record();

protected slots:
    virtual void openFile();
    virtual void openSingingTrack();
    virtual void openBackgroundMusic();
    virtual void analyseNewSingingModel();
    virtual void openLocation();
    virtual void openRecentFile();
    virtual void saveSession();
    virtual void saveSessionInAudioPath();
    virtual void saveSessionAs();
    virtual void exportPitchLayer();
    virtual void exportNoteLayer();
    virtual void importPitchLayer();
    virtual void browseRecordedAudio();
    virtual void newSession();
    virtual void closeSession();

    virtual void toolNavigateSelected();
    virtual void toolEditSelected();
    virtual void toolFreeEditSelected();

    virtual void clearPitches();
    virtual void togglePitchCandidates();
    virtual void switchPitchUp();
    virtual void switchPitchDown();

    virtual void snapNotesToPitches();
    virtual void splitNote();
    virtual void mergeNotes();
    virtual void deleteNotes();
    virtual void formNoteFromSelection();

    virtual void showAudioToggled();
    virtual void showSpectToggled();
    virtual void showPitchToggled();
    virtual void showNotesToggled();

    virtual void playAudioToggled();
    virtual void playPitchToggled();
    virtual void playNotesToggled();
    virtual void playSingingAudioToggled();
    virtual void backgroundMusicToggled();
    virtual void backgroundMusicGainChanged(float gain);
    virtual void backgroundMusicPanChanged(float pan);

    virtual void editDisplayExtents();

    virtual void analyseNow();
    virtual void resetAnalyseOptions();
    virtual void autoAnalysisToggled();
    virtual void precisionAnalysisToggled();
    virtual void lowampAnalysisToggled();
    virtual void onsetAnalysisToggled();
    virtual void pruneAnalysisToggled();
    virtual void updateAnalyseStates();

    virtual void doubleClickSelectInvoked(sv::sv_frame_t);
    virtual void abandonSelection();

    virtual void paneAdded(sv::Pane *);
    virtual void paneHidden(sv::Pane *);
    virtual void paneAboutToBeDeleted(sv::Pane *);

    virtual void paneDropAccepted(sv::Pane *, QStringList);
    virtual void paneDropAccepted(sv::Pane *, QString);

    virtual void playSpeedChanged(int);
    virtual void playSharpenToggled();
    virtual void playMonoToggled();

    virtual void speedUpPlayback();
    virtual void slowDownPlayback();
    virtual void restoreNormalPlayback();

    virtual void monitoringLevelsChanged(float, float);

    virtual void audioGainChanged(float);
    virtual void pitchGainChanged(float);
    virtual void notesGainChanged(float);

    virtual void audioPanChanged(float);
    virtual void pitchPanChanged(float);
    virtual void notesPanChanged(float);

    virtual void sampleRateMismatch(sv::sv_samplerate_t, sv::sv_samplerate_t, bool);
    virtual void audioOverloadPluginDisabled();

    virtual void documentModified();
    virtual void documentRestored();
    virtual void documentReplaced();

    virtual void updateMenuStates();
    virtual void updateDescriptionLabel();
    virtual void updateLayerStatuses();

    virtual void layerRemoved(sv::Layer *);
    virtual void layerInAView(sv::Layer *, bool);

    virtual void mainModelChanged(sv::ModelId);
    virtual void mainModelGainChanged(float);
    virtual void modelAdded(sv::ModelId);

    virtual void modelGenerationFailed(QString, QString);
    virtual void modelGenerationWarning(QString, QString);
    virtual void modelRegenerationFailed(QString, QString, QString);
    virtual void modelRegenerationWarning(QString, QString, QString);
    virtual void alignmentFailed(sv::ModelId, QString);

    virtual void paneRightButtonMenuRequested(sv::Pane *, QPoint point);
    virtual void panePropertiesRightButtonMenuRequested(sv::Pane *, QPoint point);
    virtual void layerPropertiesRightButtonMenuRequested(sv::Pane *, sv::Layer *, QPoint point);

    virtual void setupRecentFilesMenu();

    virtual void handleOSCMessage(const sv::OSCMessage &);

    virtual void mouseEnteredWidget();
    virtual void mouseLeftWidget();

    virtual void help();
    virtual void about();
    virtual void keyReference();
    virtual void whatsNew();

    virtual void betaReleaseWarning();
    
    virtual void newerVersionAvailable(QString);

    virtual void selectionChangedByUser();
    virtual void regionOutlined(QRect);

    virtual void analyseNewMainModel();

    // --- Real-time pitch tracking during microphone recording ---
    virtual void recordingStarted();
    virtual void onRealtimePitchDetected(sv::sv_frame_t frame, double hz);
    virtual void recordingFinishedFull(Analyser *analysing = nullptr);

    void moveOneNoteRight();
    void moveOneNoteLeft();
    void selectOneNoteRight();
    void selectOneNoteLeft();

    void ffwd();
    void rewind();

protected:
    // Primary analyser: the reference/target track loaded by the user.
    Analyser      *m_analyser;

    // Secondary analyser: the singing/recording track.
    // Null until a second audio file is loaded or a recording is completed.
    Analyser      *m_analyser2;

    // Real-time pitch tracker: active only during microphone recording.
    RealtimePitchTracker *m_realtimePitchTracker;

    // The transient layer shown during recording (replaced by the full
    // pYIN analysis once recording is complete).
    sv::TimeValueLayer   *m_realtimePitchLayer;

    // Model backing the realtime layer (owned by the document).
    sv::ModelId           m_realtimePitchModelId;

    sv::Overview  *m_overview;

    // Actions/toolbar items for the singing track
    QAction       *m_showSingingPitch;
    QAction       *m_showSingingNotes;
    QAction       *m_playSingingAudio;
    QAction       *m_playRefWhileRecording;
    QAction       *m_loadSingingTrackAction;

    // Background music track: an additional audio file that plays alongside
    // the reference track but is never analysed.  The toggle enables/disables
    // mixing during both normal playback and recording.
    sv::ModelId        m_backgroundMusicModelId;
    sv::WaveformLayer *m_backgroundMusicLayer;
    QAction           *m_loadBackgroundMusicAction;
    QAction           *m_playBackgroundMusic;
    sv::LevelPanToolButton *m_bgMusicLPW;
    // True while loadBackgroundMusic() is calling openPath() so that
    // modelAdded() can capture the model ID without treating it as a singing
    // track or queuing a secondary analysis.
    bool               m_loadingBackgroundMusic;
    sv::Fader     *m_fader;
    sv::AudioDial *m_playSpeed;
    QPushButton   *m_playSharpen;
    QPushButton   *m_playMono;
    sv::WaveformLayer *m_panLayer;

    bool           m_mainMenusCreated;
    QMenu         *m_playbackMenu;
    QMenu         *m_recentFilesMenu;
    QMenu         *m_rightButtonMenu;
    QMenu         *m_rightButtonPlaybackMenu;

    QAction       *m_deleteSelectedAction;
    QAction       *m_ffwdAction;
    QAction       *m_rwdAction;
    QAction       *m_editSelectAction;
    QAction       *m_showCandidatesAction;
    QAction       *m_toggleIntelligenceAction;
    bool           m_intelligentActionOn; // GF: !!! temporary

    QAction       *m_autoAnalyse;
    QAction       *m_precise;
    QAction       *m_lowamp;
    QAction       *m_onset;
    QAction       *m_prune;
        
    QAction       *m_showAudio;
    QAction       *m_showSpect;
    QAction       *m_showPitch;
    QAction       *m_showNotes;
    QAction       *m_playAudio;
    QAction       *m_playPitch;
    QAction       *m_playNotes;
    sv::LevelPanToolButton *m_audioLPW;
    sv::LevelPanToolButton *m_pitchLPW;
    sv::LevelPanToolButton *m_notesLPW;
    
    sv::ActivityLog   *m_activityLog;
    sv::KeyReference  *m_keyReference;
    sv::VersionTester *m_versionTester;
    QString            m_newerVersionIs;

    sv::sv_frame_t m_selectionAnchor;

    bool m_withSonification;
    bool m_withSpectrogram;

    Analyser::FrequencyRange m_pendingConstraint;

    QString exportToSVL(QString path, sv::Layer *layer);
    FileOpenStatus importPitchLayer(sv::FileSource source);

    QString getReleaseText() const;

    virtual void setupMenus();
    virtual void setupFileMenu();
    virtual void setupEditMenu();
    virtual void setupViewMenu();
    virtual void setupAnalysisMenu();
    virtual void setupHelpMenu();
    virtual void setupToolbars();

    // Helpers for the singing / second-track workflow
    // deferAnalysis=true skips pYIN (used when the model is a
    // WritableWaveFileModel still being recorded into).
    virtual void setupSingingTrackAnalyser(sv::ModelId singingModelId,
                                           bool deferAnalysis = false);
    virtual void teardownSingingTrackAnalyser();
    virtual void setupRealtimePitchLayer();
    virtual void teardownRealtimePitchLayer();
    virtual void stopRealtimePitchTracker();

    // Background music helpers: load/tear-down a non-analysed audio track
    // that plays alongside the reference track.
    void loadBackgroundMusic(QString path);
    void teardownBackgroundMusic();

    // Remove an extra pane created by openAudio()/record() in
    // CreateAdditionalModel mode: delete the orphan layer(s) showing
    // ownedModelId, detach shared layers (the time ruler) without deleting
    // them, then delete the pane widget.  Another layer must already
    // reference ownedModelId, or the model is released with the orphan.
    void pruneExtraPane(sv::Pane *extra, sv::ModelId ownedModelId);

    // Drain m_pendingExtraPanes: prune the panes that were deferred from
    // record()'s pane-cleanup step.  Must be called after m_analyser2 has
    // created its WaveformLayer for singingModelId (so deleteLayer won't
    // free the recording model), and while the pane widgets are still alive
    // (so m_layerViewMap iteration in deleteLayer(force=true) is valid).
    void drainPendingExtraPanes(sv::ModelId singingModelId);

    // When loadSingingTrack opens an additional audio file, modelAdded()
    // stores the resulting ModelId here so analyseNewSingingModel() can
    // pick it up on the next event-loop iteration.
    sv::ModelId m_pendingSingingModelId;

    // A take is kept out of the playback mix while it is being recorded:
    // with the reference playing, the singer would otherwise hear
    // themselves late, and on speakers that goes back into the microphone.
    // m_singingAudioAfterTake is what Play Singing Audio asks for, and
    // what the take is given when the recording is over.
    bool m_singingAudioMutedForTake;
    bool m_singingAudioAfterTake;
    void restoreSingingAudioAfterTake();

    // The main model last handed to m_analyser by analyseNewMainModel().
    // audioFileLoaded() is emitted for additional models too (a singing
    // track, background music), and the reference must not be set up again
    // for those.  Cleared in closeSession().
    sv::ModelId m_analysedMainModelId;

    // True while a microphone recording is in progress (set in
    // recordingStarted(), cleared in recordingFinishedFull()).
    bool        m_recordingInProgress;

    // True when the current/most-recent recording was captured as a singing
    // track alongside an existing reference track (RecordCreateAdditionalModel
    // mode).  Set in record(), cleared in recordingFinishedFull() and
    // closeSession().  When true, analyseNow() routes analysis through
    // m_analyser2 rather than re-analysing the primary reference track.
    bool        m_recordingAsSingingTrack;

    // Pane count saved just before MainWindowBase::record() is called in
    // singing-track mode.  After record() returns, any panes above this
    // count are extra panes created by AddPaneCommand for the recording's
    // waveform layer; we remove them so both tracks share pane 0.
    int         m_paneCountBeforeRecording;

    // The WritableWaveFileModel being recorded into in the current (or most
    // recent) singing-track recording.  Set in modelAdded() when
    // m_recordingAsSingingTrack is true, cleared in closeSession() and
    // recordingFinishedFull().  Used by setupRealtimePitchLayer() to
    // identify the correct audio source model without scanning all document
    // models — a scan would incorrectly pick up a previous recording's
    // WritableWaveFileModel that is still registered in the document because
    // its orphan waveform layer (view-detached but still in m_document's
    // layer list) holds a reference that prevents releaseModel() from
    // freeing it.
    sv::ModelId m_currentRecordingModelId;

    // Round-trip hardware latency (output + input, in frames at the model
    // sample rate) stored when a singing-track recording is made with the
    // "play reference while recording" toggle on.  Applied as a negative
    // start-frame offset to the singing model so its timeline aligns with
    // the reference during playback.  Reset to 0 at the start of each recording.
    //
    // It also includes the start gap: the part of the take recorded before
    // the reference began to play.  That starts out as an estimate made just
    // before play() is called, and refineRecordingLatency() replaces the
    // estimate with the measured figure once the audio callback has it.
    sv::sv_frame_t  m_recordingLatencyFrames;
    sv::sv_frame_t  m_recordingStartGapEstimate;

    // Written by the audio callback when the first block of the reference is
    // handed to the device during a take: the number of frames of the take
    // that came before that block.  -1 until then.
    std::atomic<sv::sv_frame_t> m_recordingStartGapMeasured;
    std::atomic<bool> m_awaitingReferenceStart;

    void refineRecordingLatency();

    // Set while the live dots of a finished take wait for pYIN to
    // produce the pitch track that replaces them.
    QMetaObject::Connection m_realtimeLayerTeardownConnection;

    // Extra panes created by MainWindowBase::record() via AddPaneCommand
    // that we want to hide immediately but cannot delete yet because
    // m_analyser2 hasn't been set up yet (it is deferred via QTimer::singleShot).
    // Stored here so that setupSingingTrackAnalyser() can properly delete
    // them — AFTER m_analyser2 has created its own WaveformLayer referencing
    // the recording model, making it safe to call deleteLayer(orphan, true)
    // on the extra pane's waveform layer without releasing the recording model.
    // closeSession() deletes any leftovers via its hidden-pane loop.
    std::vector<sv::Pane *> m_pendingExtraPanes;

    virtual void octaveShift(bool up);

    virtual void auxSnapNotes(sv::Selection s);

    virtual void closeEvent(QCloseEvent *e);
    bool checkSaveModified();
    bool waitForInitialAnalysis();

    virtual void updateVisibleRangeDisplay(sv::Pane *p) const;
    virtual void updatePositionStatusDisplays() const;

    void moveByOneNote(bool right, bool doSelect);
};


#endif
