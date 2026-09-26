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
#include "LyricsTrack.h"
#include "LyricsEditor.h"
#include "SingingTakes.h"
#include "TakeCommands.h"
#include "TakeTiming.h"
#include "LatencyUtils.h"
#include "LatencyCalibration.h"
#include "LiveDotsFeed.h"

#include <vector>
#include <string>
#include <atomic>

#include "data/model/SparseTimeValueModel.h"

#ifdef Q_OS_ANDROID
#include <QElapsedTimer>
class AndroidStorage;
#endif

class QTimer;
class QComboBox;
class QActionGroup;
class QToolBar;
class CompactLayout;
class PlotSize;

class AudioCheckRunner;
struct AudioCheckResult;
class AudioDriverMenus;
class CalibrateAudioDialog;
#ifdef TONY_DEV_CHECKS
class DevChecks;
#endif

namespace sv {
class VersionTester;
class ActivityLog;
class LevelPanToolButton;
class TimeValueLayer;
}

class MainWindow : public sv::MainWindowBase
{
    Q_OBJECT

    // The audio check drives the take path of the window, and reads what
    // each take was placed with; see AudioCheckRunner
    friend class AudioCheckRunner;
#ifdef TONY_DEV_CHECKS
    // The development checks save and reopen the session, and read the
    // take's pitch and notes; see DevChecks
    friend class DevChecks;
    // What the development checks see of a take, only looking; see
    // TakeObserver
    friend class TakeObserver;
#endif

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

    /**
     * Put the take into the state a SingingTakeCommand holds: its audio
     * file, its coverage, the events of its pitch track and notes, and, if
     * the state names a range whose analysis never finished, that
     * analysis started again.  Called by the command on undo and on redo;
     * public only for that.  False if the audio could not be shown, in
     * which case the user has been told.
     */
    bool applyTakeState(SingingTakeCommand *command, const TakeState &state);

    // A session that has been saved names the audio file of every take as
    // it was at the time, so those files must outlive every other
    // reference to them.  This is also where a save waits for the analysis
    // of a recorded range, so that no session holds the state half way
    // through one (see waitForRangedAnalysis())
    bool saveSessionFile(QString path) override;

    // The takes of the session are written into the <sv> document as one
    // element of Tony's own (spec 6.4), which the base class knows nothing
    // of; and after the base class has read a session back, the same
    // element is read from the file and the takes put back from it
    void toXml(QTextStream &out, bool asTemplate) override;
    FileOpenStatus openSession(sv::FileSource source) override;

    // Switch the layout for a phone on or off (CompactLayout), as View >
    // Compact Layout does: main() switches it on at start on Android and
    // with --compact
    void setCompactLayout(bool on);
    // The round trip takes are placed with (see LatencyCalibration).
    // Keep the one an audio check measured, for the devices it started
    // on and the rate it recorded at (AudioCheckResult::key); false, with
    // nothing kept, unless the check's figure is usable.  The Playback
    // menu's line about the latency follows
    bool storeMeasuredLatency(const AudioCheckResult &result);

    // Drop the figure latencyInUse() describes, and say so in the menu
    void forgetMeasuredLatency();

    // What the next take will be placed with, as far as it is known
    // before the take starts: the device's rate is known only once it
    // has recorded, so until a take has been recorded on these devices
    // this assumes the session's rate, the only one a usable check
    // stores a figure at.  The reported pair is 0 until the device is
    // open
    LatencyCalibration::InUse latencyInUse() const;

signals:
    void canExportPitchTrack(bool);
    void canExportNotes(bool);
    void canSnapNotes(bool);
    void canPlayWaveform(bool);
    void canPlayPitch(bool);
    void canPlayNotes(bool);
    void canLoadSingingTrack(bool);
    void canShowRealtimePitch(bool);
    void canEraseSinging(bool);
    void canSelectRecording(bool);
    // Switching and making takes: not while a take is being recorded or a
    // recorded range analysed.  The second is the same with a take there
    // to be copied, renamed or deleted
    void canChangeTakes(bool);
    void canActOnTake(bool);

public slots:
    virtual bool commitData(bool mayAskUser); // on session shutdown

protected slots:
    // Override record() so that when a reference track is already loaded we
    // can switch to RecordCreateAdditionalModel before starting the capture,
    // causing the recording to be treated as the singing track.
    virtual void record();

    // The Record button, and its shortcut: record() or Stop, except
    // while the audio check runs, which records and stops takes of its
    // own through record(); the button is shut then, and a press that
    // gets here all the same is ignored
    virtual void recordPressed();

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

    // Save the session to this file and make it the session's own file:
    // what Save As and Save In Audio Path do once they have a path.  False
    // if it could not be saved, in which case the user has been told
    bool saveSessionToPath(QString path);
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

    // Editing the singing of a take: what is in the selection is erased
    // from its audio, and the coverage range at the playhead can be
    // selected to erase a whole recording (spec 5.2)
    virtual void eraseSingingInSelection();
    virtual void selectRecordingAtPlayhead();

    // The Takes menu and the "Take:" combo box (spec 5.3)
    virtual void takeChosenInCombo(int index);
    virtual void newEmptyTake();
    virtual void duplicateTake();
    virtual void renameTake();
    virtual void deleteTake();

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

    virtual void importLyrics();
    virtual void exportLyrics();
    virtual void removeLyrics();
    virtual void showLyricsToggled();
    virtual void editLyricsToggled();
    virtual void shiftLyrics();

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

    virtual void rescanAudioDevices();
    virtual void audioDeviceSelected(QAction *);

    // Playback > Audio Driver or Audio Latency chosen, and written to the
    // Preferences: the device is opened again, with it
    void audioDriverChosen(QString implementation);
    void audioLatencyChosen(double seconds);

    // Playback > Calibrate Audio: the audio check's dialog, not modal
    virtual void calibrateAudio();

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

    // Brings the tracker's estimates to onRealtimePitchDetected() in
    // batches, and reports once a second what they cost
    LiveDotsFeed          m_liveDotsFeed;

    sv::Overview  *m_overview;

    // The layout for a phone: one toolbar of touch-sized buttons in place
    // of the menu bar and the other toolbars.  MainWindow only hands it
    // the parts (setupCompactLayout()): the actions below, which are made
    // with the menus and toolbars, and others that have members already
    CompactLayout *m_compactLayout;

    // View > Plot Size: how large the panes draw pitch and notes
    PlotSize      *m_plotSize;

    QAction       *m_playAction;
    QAction       *m_recordAction;
    QAction       *m_zoomInAction;
    QAction       *m_zoomOutAction;
    QAction       *m_navigateToolAction;
    QAction       *m_noteEditToolAction;
    QToolBar      *m_playbackControlsToolBar;
    QToolBar      *m_showAndPlayToolBar;
    void setupCompactLayout();

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

    // The take's own pitch and notes are put out of sight while it is
    // being recorded into, as the reference is for the alternate above:
    // the pitch is the orange of the live dots and the notes are drawn
    // over the same part of the pane, and a singer recording over
    // singing that is there cannot tell either of them from what they
    // are singing now.  The two flags say which of them we hid.
    bool           m_singingPitchHiddenForTake;
    bool           m_singingNotesHiddenForTake;
    void updateSingingTrackForTake();

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

    // The timed lyrics of the session, drawn along the bottom of pane 0
    // and stored in the session with the layer that draws them.  They
    // belong to the song, not to a take.  Display only
    LyricsTrack   *m_lyrics;
    QAction       *m_importLyricsAction;
    QAction       *m_exportLyricsAction;
    QAction       *m_removeLyricsAction;
    QAction       *m_showLyrics;

    // Edit > Edit Lyrics: the mouse moves the words' starts and ends in
    // the lyrics' box row while it is on, and changes, adds and deletes
    // words there.  Off, and not to be had, without lyrics on show or
    // while a take is being recorded, which updateMenuStates() sees to;
    // and off after an import, which importLyricsFrom() sees to
    LyricsEditor  *m_lyricsEditor;
    QAction       *m_editLyricsAction;

    // Edit > Shift Lyrics...: all the words earlier or later by a number
    // of seconds.  To be had whenever editing is, edit mode on or not
    QAction       *m_shiftLyricsAction;

    // The lyrics are there to be edited: shown, visible, and no take
    // being recorded (the singer is reading them)
    bool lyricsEditAllowed() const;

    // Edit mode on, if lyricsEditAllowed(), or off, and the action to
    // match.  Off finishes a drag in progress, pushing its command, so
    // this must not be reached from an undo or a redo
    void setLyricsEditing(bool on);

    // Fade the waveforms of both analysers while the lyrics are on show
    // over them, and not otherwise.  Called after anything that shows or
    // hides the lyrics, and after anything that makes an analyser: a new
    // one starts unfaded, and a session's lyrics are found only after
    // the reference's analyser has taken its waveform over
    void updateWaveformFade();

    // Put the lyrics of this TTML or LRC file on the reference's
    // timeline, in place of any there are.  Not undoable, as loading
    // background music is not, and nothing goes onto the undo stack.
    // False if the file could not be read or holds no timed lyrics,
    // which the user is told in a dialog, or if lyricsImportAllowed()
    // says no; nothing has changed then
    bool importLyricsFrom(QString path);

    // Lyrics can be imported once there is a reference, and not while a
    // take is being recorded
    bool lyricsImportAllowed() const;

    // Ask for the TTML or LRC file to import, "" if the user cancelled.
    // Overridden by the tests, which cannot answer a dialog
    virtual QString askForLyricsFile();

    // Write the lyrics, as the model holds them now, to this TTML file.
    // Not a command, and the session is not changed by it.  False if
    // there are no lyrics, or if the file could not be written, which
    // the user is told in a dialog
    bool exportLyricsTo(QString path);

    // Ask where to export the lyrics to, offering the suggested path;
    // "" if the user cancelled.  Overridden by the tests
    virtual QString askForLyricsExportFile(QString suggested);

    // Ask for the text of a word of the lyrics, offering the one it has
    // ("" for a new word): true with the text typed in its place, false
    // if the user cancelled.  The lyrics editor asks through this.
    // Overridden by the tests
    virtual bool askForLyricsWordText(QString &text, bool isNew);

    // Ask how far to shift all the words of the lyrics, in seconds,
    // negative for earlier: true with the seconds, false if the user
    // cancelled.  Overridden by the tests
    virtual bool askForLyricsShift(double &seconds);

    // --- The audio folder of the session (spec 6.4) ---

    // Where the next combined audio file of a take is to be written: the
    // session's own "<name>.takes" folder once the session has a file, and
    // the record directory before its first save, the save copying what is
    // there into the folder.  "" if neither could be had
    QString takeAudioDirectory();

    // The session's takes folder, made if it is not there yet.  "" if it
    // could not be made.  A folder this run made and left empty is removed
    // again when the session closes
    QString ensureTakesFolder(QString sessionPath);

    // The takes folders this run made, so that an empty one can be taken
    // away again on close.  A folder with anything in it is never removed:
    // what is in it is not necessarily ours
    QStringList m_takeFoldersMade;

    // The session file being written, from the start of saveSessionFile()
    // to its end: toXml() names each take's audio relative to it
    QString m_savingSessionPath;

    // Copy the audio of every take that is not in the folder of the
    // session being saved to into it, and point the takes at the copies
    // (TakesFile::copyTakeAudioInto()).  Copied, not moved: the active
    // take's model has its file open.  False if a copy failed, in which
    // case the user has been told and nothing has changed: the session is
    // not to be saved naming files that are not there
    bool copyTakeAudioForSave(QString sessionPath);

    // --- Several takes (spec 5.3) ---
    //
    // Every take of the session has its three layers in pane 0, named
    // after it (TakeLayers); only the active take has an audio model and
    // the singing analyser.  The take a menu or the combo box asks for is
    // made the active one by switchToTake(), which is the swap of 6.2
    // with the layers of another take put in place of the ones on show.

    QMenu         *m_takesMenu;
    QComboBox     *m_takeCombo;
    QAction       *m_newTakeAction;
    QAction       *m_duplicateTakeAction;
    QAction       *m_renameTakeAction;
    QAction       *m_deleteTakeAction;

    // Set while updateTakeCombo() fills the combo box, so that the
    // currentIndexChanged it causes is not taken for the user's choice
    bool           m_updatingTakeCombo;

    void setupTakesMenu();

    // The combo box lists the takes with the active one selected
    void updateTakeCombo();

    // Make the take at this index the active one: release the analyser
    // keeping the layers, put the take's layers away, then show the other
    // take's and open its audio underneath them.  Nothing is analysed.
    // The undo history goes, as spec 5.4 says it must: its commands hold
    // the state of a take that is no longer on show
    bool switchToTake(int index);

    // Release the singing analyser and the take's audio with it, and put
    // the active take's layers away: still in the pane and in the
    // session, hidden, silent and owned by nobody
    void deactivateTake();

    // Show the active take: its audio under its layers, an analyser that
    // claims them (no analysis), its coverage strip, and the layers
    // themselves visible, audible as the user asked and within reach of
    // the editing tools.  False if its audio could not be opened, in
    // which case the user has been told -- unless warnIfNoAudio is false,
    // for a session load that has one warning of its own covering all of
    // its takes
    bool activateTake(bool warnIfNoAudio = true);

    // The takes of a session that has just been read back: the <takes>
    // element of the file says which takes there are, what their audio
    // files are and which of them was on show, and the layers the document
    // restored say what is in them (spec 6.4).  Each take's coverage comes
    // from its own coverage strip; the active take is shown by the same
    // path a switch uses, and nothing is analysed.  A session with no
    // <takes> element opens without a singing track at all (spec 3).
    void restoreTakes(QString sessionPath);

    // Drop the audio model of the singing track that a session restored:
    // Document::toXml() writes it because the active take's waveform layer
    // is in pane 0, and it is of no use here -- the take's audio is opened
    // from the path in <takes>, as it is for a switch.  Silently: no undo
    // entry, no pane, nothing left in the play source, and
    // m_pendingSingingModelId cleared so that nothing makes a take of it.
    //
    // withTakeLayers: its pitch, notes and coverage layers as well, for a
    // session that has no <takes> element -- it opens without its singing
    // track, and nothing of one is left in the pane to be mistaken for a
    // take.
    void dropRestoredSingingTrack(bool withTakeLayers);

    // Take a layer and its model out of the document with no trace: no
    // undo entry, and nothing left in the play source
    void dropLayerSilently(sv::Layer *layer);

    // Set while a session is being read, so that the queued call which
    // makes a take of a newly added audio model (modelAdded()) leaves the
    // restored one alone: restoreTakes() deals with it
    bool m_restoringSession;

    // Name the layers the singing analyser holds after the active take,
    // which is what says whose they are (spec 6.4)
    void nameActiveTakeLayers();

    // Every take layer in pane 0 that is not the active take's: hidden,
    // silent, out of the play source and with no source model, so that
    // nothing claims it and nothing hears it.  The one place that
    // enforces it, for a switch and for a session load alike
    void putOtherTakeLayersAway();

    // The active take's pitch and notes to the top of the pane, where the
    // note tool looks for the layer to act on
    void raiseActiveTakeLayers();

    // Forget the take at this index, with its layers; no audio file is
    // touched.  Deleting the active take activates a neighbour
    bool deleteTakeAt(int index);

    // A take may be switched, made, copied or deleted only when nothing
    // is being recorded or analysed
    bool takeOperationsAllowed() const;

    // Every take operation but Rename clears the undo history (spec 5.4)
    void clearTakeHistory();

    // Ask before deleting a take, and for a take's new name.  Overridden
    // by the tests, which cannot answer a dialog
    virtual bool confirmDeleteTake(QString name);
    virtual QString askForTakeName(QString current);

    // Erase Singing in Selection, and selecting the recording the
    // playhead is in
    QAction       *m_eraseSingingAction;
    QAction       *m_selectRecordingAction;

    // Take the pitch events and the notes in these ranges out of the
    // take's layers, the erased audio having taken the singing they
    // describe with it.  Straight on the models, as an analysis result
    // is; what was changed comes back in the two Changes, for the undo
    // command of the erase as a whole
    void eraseTakeEvents(const Coverage::Ranges &erased,
                         TakeEvents::Change *pitchChange = nullptr,
                         TakeEvents::Change *notesChange = nullptr);

    // Undo and redo of the singing of a take (spec 5.4).
    //
    // The command of the operation being made just now, from the splice
    // or the erase until the analysis that follows a splice has been
    // merged into the take's pitch and notes.  That merge arrives seconds
    // after the splice, and amends this command, so that one Undo takes
    // the recording and its analysis back together.  Null when no command
    // is waiting for anything; never left pointing at a command the
    // history may have deleted
    SingingTakeCommand *m_openTakeCommand;

    // Put a finished take operation on the undo stack, its work already
    // done (CommandHistory::addCommand(command, false))
    void addTakeCommand(SingingTakeCommand *command);

    // Stop waiting for a merge into the open command.  With
    // cancelAnalysis, a ranged analysis that is still running is
    // abandoned first: its result belongs to a state that is being left
    // behind, and must not land on the take afterwards
    void closeOpenTakeCommand(bool cancelAnalysis);

    // The ranged analysis has been merged: the events it changed go into
    // the command of the recording that asked for it
    void takeAnalysisMerged();

    // Apply one command's event changes to the take's pitch and notes
    void applyTakeEventChanges(const TakeState &state);

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
    // value MainWindow/prerollseconds (3 s), or 0 with the toggle off;
    // for a take of the audio check, the check's own
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
    QMenu         *m_audioDeviceMenu;
    QActionGroup  *m_audioDeviceGroup;
    QMenu         *m_audioInputDeviceMenu;
    QActionGroup  *m_audioInputDeviceGroup;

    // Playback > Audio Driver and Audio Latency, before the device menus
    AudioDriverMenus *m_audioDriverMenus;

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
    virtual void buildAudioDeviceMenu(QMenu *menu,
                                      QActionGroup *group,
                                      const std::vector<std::string> &names,
                                      QString settingKey);

    // The implementations bqaudioio has, of which the drivers are offered
    // in Playback > Audio Driver.  Virtual so that the tests can give
    // drivers the platform they run on does not have
    virtual QStringList audioImplementationNames() const;

    // Where no driver is named and MME is built in, name it (and carry
    // the devices chosen before over to it): before a device is opened,
    // and before the Playback menu shows the device menus
    void nameDefaultAudioDriver();

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

    // A batch of the live tracker's estimates, everything it has found
    // since the last: dots for them, the pane told of them, the status
    // bar set from the newest.  From m_liveDotsFeed
    virtual void onRealtimePitchDetected
        (const RealtimePitchTracker::Estimates &estimates);

    // The once-a-second log line of a take: how far the recording, the
    // tracker and the dots have got, and what the dots cost
    void logLiveDots(const LiveDotsFeed::Report &report);

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

    // Open a take's own audio file as a model of the document and nothing
    // else: no pane, no layer and, above all, no undo command.  Every
    // change to a take swaps its audio, including an undo, and a command
    // pushed while an undo is running destroys the command that is running
    // (see the comment on the definition)
    FileOpenStatus openTakeAudioFile(QString path, sv::ModelId &modelId);

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

    // Playback constrained to the selection is lifted while the reference
    // plays for a take, and put back when the take is over: true while it
    // is lifted
    bool m_playSelectionLiftedForTake;
    void liftPlaySelectionForTake();
    void restorePlaySelectionAfterTake();

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

    // Round-trip hardware latency (the figure the audio check measured,
    // or else output + input as the device reports them, in frames of the
    // recording, at the device's rate; see roundTripAt()) stored when a
    // singing-track recording is made with the "play reference while
    // recording" toggle on.  The recording is read from this frame on when
    // it is spliced into the take's audio, so that what the singer sang in
    // answer to the reference at m_takePosition lands there; and the live
    // dots are placed with it during the take.  TakeTiming converts it to
    // the reference's frames.
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

    // The play source counts at the reference's rate, and the device may
    // run at another: frames of the recording per frame of the play
    // source, set before the reference is started, for the audio
    // callback that measures the start gap
    std::atomic<double> m_recordFramesPerPlayFrame;
    // What the take being recorded, or the last one, was placed with:
    // cleared when a take starts, the round trip and the latencies the
    // device reported (in seconds, as roundTripAt() has them) filled in
    // when the reference starts to play, and the recording's rate when
    // the take is spliced in
    TakeLatency m_takeLatency;

    // The audio check, and the override it sets for each take of its
    // own: Record into Selection, Play Reference While Recording and a
    // pre-roll of its own, whatever the toolbar says.  Not by setting the
    // toggles, which write the user's settings.  record(),
    // recordingStarted() and wantedPreRollFrames() consult it
    AudioCheckRunner *m_audioCheck;
    bool m_audioCheckTakes;

    // The round trip the check's takes are placed with in place of
    // roundTripAt()'s, in seconds, for a run that brings one of its own
    // (AudioCheckRunner::Plan::roundTrip); negative when it brings none.
    // Read with m_audioCheckTakes, and never by latencyInUse()
    double m_audioCheckRoundTrip;

    // The pre-roll the check's takes ask for, in seconds
    // (AudioCheckRunner::Plan::preRoll).  Read with m_audioCheckTakes
    double m_audioCheckPreRoll;

#ifdef TONY_DEV_CHECKS
    // The development checks, which drive the audio check and the
    // session; made with the window, deleted in its destructor after the
    // dialog and before the runner, and told when the session closes
    DevChecks *m_devChecks;
#endif

    // The audio check, or the development checks, are running: the take
    // path and the devices are theirs until they end
    bool audioCheckRunning() const;

    // The Record button (m_recordAction, with the compact layout's parts
    // above) is shut while audioCheckRunning()

    // Playback > Calibrate Audio, made the first time it is chosen, and
    // the lines under it: the latency takes are placed with, and Forget
    // Measured Latency.  Calibrate Audio is shut while a take or a check
    // is being recorded, and so are the device menus while a check runs:
    // the figure it measures is kept for the devices it started on
    CalibrateAudioDialog *m_calibrateAudioDialog;
    QAction *m_calibrateAudioAction;
    QAction *m_latencyLineAction;
    QAction *m_forgetLatencyAction;

    // Say which latency is in use, and let it be forgotten if it is one
    // the check measured
    void updateLatencyMenuLine();

    // The rate the device recorded at, the last time a take was placed
    // with a round trip; 0 until then, and again once another device is
    // chosen
    sv::sv_samplerate_t m_lastRecordingRate;

    // The session's rate, from the play source, or the fixed rate every
    // file is read at before there is one
    sv::sv_samplerate_t sessionRate() const;

    // The rate the next take is expected to record at: the last one's,
    // or before there is one, the session's
    sv::sv_samplerate_t expectedRecordingRate() const;

    // The round trip for a take recorded at the given rate, in seconds,
    // and where it came from: a stored figure for these devices and this
    // rate, unless the latencies the device reports have changed since
    // it was measured; otherwise the reported pair, each converted from
    // the frames it counts
    LatencyCalibration::InUse roundTripAt(sv::sv_samplerate_t recordingRate) const;

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

    // A session that loaded without some of the audio it names (svapp's
    // "Incomplete session loaded") is saved without any mention of that
    // audio, so the file it came from, saved over, would lose it. It is
    // saved only when the user asks and then says yes: Save, Save As and
    // Save Session in Audio Path ask first, and a save no one asked for
    // (Android's on suspend) passes it by. Once saved it is what its new
    // file says, and is not asked about again
    bool sessionIsIncomplete() const;
    bool confirmSaveOfIncompleteSession();
    // The question, which the tests answer: they cannot answer a dialog
    virtual bool askToSaveIncompleteSession();

    // Whether the session may be saved with no one asked: it has a file
    // of its own, has been changed, and is not incomplete
    bool maySaveUnasked() const;

#ifdef Q_OS_ANDROID
    // Android's file picker gives content:// URIs, which svcore's readers
    // cannot open. A file in the phone's own storage is opened where it
    // is, by its path, once Tony has All files access (AndroidStorage),
    // so that a session finds its audio and takes beside it. Other audio
    // is copied into the app's own storage and the copy's path returned;
    // other sessions are refused. Tony's own picker, not svgui's dialog:
    // that asks Qt whether the URI's file exists, and Qt's content file
    // engine says no for a name with parentheses (AndroidFiles::
    // grantedUri()); and it offers only the types Qt names for its
    // filters, which are not always Android's
    QString getOpenFileName(sv::FileFinder::FileType type) override;

    // What to tell the user of a picked file that could not be opened or
    // saved, to pass on: its provider, the path looked for, and why
    QString pickDetails(QString uri, QString path, QString why) const;

    // Help > Save Log...: Tony's log (main.cpp) through the save picker,
    // for a user who cannot read the system log to send
    void saveLog();

    // Save Session As: Tony's own picker, which suggests a name (svgui's
    // suggests none), and then the path of the file picked in the phone's
    // own storage, or nothing. The picker has made an empty document by
    // then, which is removed if it is not to be the session. Other files
    // are saved as before
    QString getSaveFileName(sv::FileFinder::FileType type) override;
    AndroidStorage *m_storage;

    // A recent file that has been moved or deleted is said to be so, and
    // is no longer offered (RecentFiles keeps it, having no way to drop
    // one)
    bool recentFileIsThere(QString path);

    // Android sends Tony to the background: playback stops, a take being
    // recorded is finished as Stop finishes it, and the session is saved
    // as Save saves it, if it has a file of its own. Android holds Tony's
    // event loop from the moment this returns until Tony is back, so
    // nothing here may wait on it: a save that has to wait for the
    // analysis of a take is made when that is done
    void applicationStateChanged(Qt::ApplicationState state);
    void saveWhenSuspended();
    QTimer *m_suspendSaveTimer;
    QString m_suspendSavePath;

    // The audio device is Oboe's (OboeAudioIO): bqaudioio has no
    // Android backend. Its input only once the microphone may be used
    void createAudioIO() override;

    // The microphone is asked for when Record is first pressed, and the
    // take is started once it is given
    bool microphoneAllowed() const;
    void askForMicrophone();

    // A device that goes away (headphones in or out) leaves the device
    // failed: looked for on a timer, and the device opened afresh
    void checkAudioDevice();
    QTimer *m_audioDeviceCheck;
    QElapsedTimer m_audioDeviceReopened;
    int m_audioDeviceReopens;
#else
    // Before each device is opened: a driver named where none is, and
    // the latency chosen for it handed to bqaudioio. Then openAudioIO()
    void createAudioIO() override;

    // Opens the device the Preferences name, as MainWindowBase does
    virtual void openAudioIO();
#endif

    // A session must not be saved in the middle of the analysis of a
    // recorded range: the take's pitch and notes still hold the state
    // before the merge, and the two models the run works in are in the
    // document.  Waits for the merge, as waitForInitialAnalysis() waits
    // for the reference's first analysis
    bool waitForRangedAnalysis();

    virtual void updateVisibleRangeDisplay(sv::Pane *p) const;
    virtual void updatePositionStatusDisplays() const;

    void moveByOneNote(bool right, bool doSelect);
};


#endif
