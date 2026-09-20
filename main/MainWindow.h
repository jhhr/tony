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
#include "AlternatePitchTrack.h"
#include "CoverageStrip.h"
#include "SingingTakes.h"
#include "TakeTiming.h"

#include <vector>
#include <atomic>

#include "data/model/SparseTimeValueModel.h"

class QTimer;

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
    // The same, for the singing track of a session being restored: the
    // take's layers are handed to the new analyser first
    virtual void analyseRestoredSingingModel();
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

    virtual void alternatePitchToggled();
    virtual void alternatePitchUp();
    virtual void alternatePitchDown();
    virtual void syncAlternatePitchTrack();

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

    // The status bar's "Recording: <duration>" and "Playing: ...", which
    // the lead-in of a pre-roll replaces with its countdown
    virtual void recordDurationChanged(sv::sv_frame_t, sv::sv_samplerate_t);
    virtual void playbackFrameChanged(sv::sv_frame_t);

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
    virtual void finishSingingTake();

    // Watches a take that is to stop at an end of its own; see
    // startTakePolling()
    virtual void pollTakeProgress();

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
    QAction       *m_preRoll;
    QAction       *m_recordIntoSelection;
    QAction       *m_loadSingingTrackAction;

    // The alternate pitch track: the reference pitch track moved by whole
    // octaves, for the singer to follow in place of the reference.  While
    // a singing take is being recorded it is shown in full and the
    // reference pitch track is hidden; m_referencePitchHiddenForTake says
    // that we hid it, and must show it again afterwards.  Not hidden with
    // Analyser::setVisible(), which would write the state to the settings.
    AlternatePitchTrack *m_alternatePitch;
    QAction       *m_showAlternatePitch;
    QAction       *m_alternatePitchUpAction;
    QAction       *m_alternatePitchDownAction;
    bool           m_referencePitchHiddenForTake;
    void stepAlternatePitch(bool up);
    void updateAlternatePitchForTake();

    // The singing takes of the session: the audio file of each take and
    // the ranges of it that hold recorded singing.  MainWindow only
    // wires it: it decides where a recording goes and writes the files.
    SingingTakes  *m_takes;

    // The coverage of the take, drawn in pane 0 and stored in the
    // session with the layer that draws it.  Display only.
    CoverageStrip *m_coverageStrip;

    // Put the strip in step with the take's coverage: make it if there
    // is a take and none yet, take it away when the take goes
    void syncCoverageStrip();

    // Where on the reference's timeline the take being recorded, or the
    // one most recently recorded, starts: the playback position when
    // Record was pressed, or the start of the selection recorded into.
    // The live dots are drawn from here, and this is where the recording
    // is spliced into the take's audio.
    sv::sv_frame_t m_takePosition;

    // The lead-in played before m_takePosition (R, "Pre-roll"), and the
    // frame the take stops itself at (E, "Record into Selection"), or -1
    // when it runs until Stop is pressed.  Both are worked out in
    // record() and used until the take has been spliced in; TakeTiming
    // does the arithmetic.
    sv::sv_frame_t m_takePreRoll;
    sv::sv_frame_t m_takeEnd;

    // Polls the record target while a take that has an end to reach
    // runs, and stops the take once the singing for that end has
    // arrived.  Not running for a take that goes on until Stop.
    QTimer        *m_takeTimer;

    void startTakePolling();
    void stopTakePolling();

    // The take being recorded, or the one just recorded, as TakeTiming
    // sees it: everything the splice, the dots and the automatic stop
    // are worked out from.
    TakeTiming currentTakeTiming() const;

    // The pre-roll asked for, in frames of the reference: the QSettings
    // value MainWindow/prerollseconds (3 s), or 0 with the toggle off
    sv::sv_frame_t wantedPreRollFrames() const;

    // Put the countdown of a pre-roll's lead-in in the status bar, and
    // say so, if that is what belongs there just now.  Everything that
    // writes the status bar while a take runs asks this first.
    bool showTakeCountdown() const;

    // Ask before recording over singing that is already there, unless
    // the user has said not to.  Overridden by the tests, which cannot
    // answer a dialog.  Returns true to go ahead with the recording.
    virtual bool confirmRecordingOverTake();

    // Show and analyse the take's audio file after a recording has been
    // spliced into it over the range "placed": the new file goes under
    // the take's pitch and notes layers (swapSingingAudio(), or
    // loadTakeAudio() and empty layers for the first recording of a
    // take) and only "placed" is analysed, over the take's coverage.
    // Returns true if an analysis is running that will say when it is
    // done; a failure is reported to the user from here.
    bool rebuildSingingTrackFromTake(const Coverage::Range &placed);

    // Open the take's audio as the singing track with no analysis of its
    // own, for a take that has no pitch and notes layers to keep.
    // "" on success, else a message for the user
    QString loadTakeAudio(QString path);

    // Hand the take's pitch and notes layers, if they are in pane 0 and
    // belong to no analyser, to the audio model about to be analysed:
    // that link is what lets an Analyser claim them.  True if both were
    // found and linked
    bool adoptTakeLayers(sv::ModelId audio);

    // Analyse [start, end) of the take's audio and merge the result into
    // its pitch and notes, with the context limited to the coverage
    // range the material sits in.  True if a run was started
    bool startTakeAnalysis(sv::sv_frame_t start, sv::sv_frame_t end);

    // Analyse all of the take's coverage again (Analyse Now, spec 7)
    bool analyseTakeCoverage();

    // Keep the take's existing audio out of the mix while it is being
    // recorded into, and put it back afterwards
    void muteSingingAudioForTake();

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

    // Helpers for the singing / second-track workflow.
    // deferAnalysis=true skips pYIN: swapSingingAudio() uses it, as the
    // pitch and notes layers it hands the new analyser are analysed
    // already.  The scan for existing layers still runs.
    virtual void setupSingingTrackAnalyser(sv::ModelId singingModelId,
                                           bool deferAnalysis = false);
    virtual void teardownSingingTrackAnalyser();
    virtual void setupRealtimePitchLayer();
    virtual void teardownRealtimePitchLayer();
    virtual void stopRealtimePitchTracker();

    // The raw recording of a take needs a layer of its own to hold it in
    // the document: the singing analyser is busy with the take's audio,
    // which stays on show while the recording is made.  The layer is
    // never shown and never heard; it goes when the recording has been
    // spliced into the take, which releases the model and with it the
    // file handles of the recording.
    void setupRecordingLayer();
    void teardownRecordingLayer();

    // Background music helpers: load/tear-down a non-analysed audio track
    // that plays alongside the reference track.
    void loadBackgroundMusic(QString path);
    void teardownBackgroundMusic();

    // Put another audio file under the take's pitch and notes layers,
    // keeping those layers and everything in them.  The new audio is not
    // analysed: what the layers hold is the analysis of all of the take
    // but the range that has just changed, which
    // rebuildSingingTrackFromTake() analyses on its own.  Returns "" on
    // success, or a message for the user.
    QString swapSingingAudio(QString path);

    // Open path as an additional audio model beside the reference, the
    // way Load Singing Track does.  The new model's id comes back in
    // modelId and the extra panes openPath() made in extraPanes; those
    // are the caller's to prune, once a layer of its own holds the model
    FileOpenStatus openSingingAudioFile(QString path, sv::ModelId &modelId,
                                        std::vector<sv::Pane *> &extraPanes);

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

    // The singing track is kept out of the playback mix while a take is
    // being recorded into it: with the reference playing, the singer
    // would otherwise hear their earlier singing along with it, and on
    // speakers that goes back into the microphone.  The recording itself
    // is silent for the same reason (setupRecordingLayer()).
    // m_singingAudioAfterTake is what Play Singing Audio asks for, and
    // what the rebuilt singing track is given when the take is over.
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
    // m_recordingAsSingingTrack is true, cleared by teardownRecordingLayer()
    // when the recording has been spliced into the take, and in
    // closeSession().  Used by setupRealtimePitchLayer() to identify the
    // correct audio source model without scanning all document models — a
    // scan would incorrectly pick up a previous recording's
    // WritableWaveFileModel that is still registered in the document.
    sv::ModelId m_currentRecordingModelId;

    // The layer that holds the raw recording in the document while it is
    // being recorded into; see setupRecordingLayer().
    sv::WaveformLayer *m_recordingLayer;

    // True while the singing track is being rebuilt from an audio file
    // that we have just spliced ourselves, so that the take's coverage —
    // which the splice worked out — is not replaced by "the whole file",
    // as it is for a file the user loads or a session restores.
    bool        m_rebuildingTakeAudio;

    // The range of the take that the ranged analysis now running was
    // asked for; empty when none is running.  It is remembered here and
    // not in the Analyser because the next recording replaces the
    // analyser along with the audio, and the analysis it was running
    // goes with it: the next one has to cover this range as well, or
    // what the singer sang would be left unanalysed.
    Coverage::Range m_takeAnalysisRange;

    // Round-trip hardware latency (output + input, in frames at the model
    // sample rate) stored when a singing-track recording is made with the
    // "play reference while recording" toggle on.  The recording is read
    // from this frame on when it is spliced into the take's audio, so that
    // what the singer sang in answer to the reference at m_takePosition
    // lands there; and the live dots are placed with it during the take.
    // Reset to 0 in record() at the start of every take, standalone ones
    // included, but not by a Stop: the splice needs it after that.
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

    // The best figure for the latency as things stand, measurement
    // included if it has arrived: refineRecordingLatency() is what makes
    // it the stored one, and only it, because the live dots placed with
    // the estimate are thrown away when the figure changes.
    sv::sv_frame_t currentRecordingLatency() const;

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
