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

#include "../version.h"

#include "MainWindow.h"
#include "NetworkPermissionTester.h"
#include "Analyser.h"
#include "AudioCheckRunner.h"
#include "CalibrateAudioDialog.h"
#include "LatencyUtils.h"
#include "PaneUtils.h"
#include "TakeEvents.h"
#include "TakeLayers.h"
#include "TakesFile.h"

#ifdef TONY_DEV_CHECKS
#include "dev/DevChecks.h"
#endif

#include "framework/Document.h"
#include "framework/VersionTester.h"

#include <bqaudioio/AudioFactory.h>

#include "view/Pane.h"
#include "view/PaneStack.h"
#include "data/model/WaveFileModel.h"
#include "data/model/ReadOnlyWaveFileModel.h"
#include "data/model/WritableWaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"
#include "data/model/RegionModel.h"
#include "layer/FlexiNoteLayer.h"
#include "view/ViewManager.h"
#include "base/Preferences.h"
#include "base/RecordDirectory.h"
#include "base/AudioLevel.h"
#include "layer/WaveformLayer.h"
#include "layer/TimeInstantLayer.h"
#include "layer/TimeValueLayer.h"
#include "layer/RegionLayer.h"
#include "layer/SpectrogramLayer.h"
#include "widgets/Fader.h"
#include "view/Overview.h"
#include "widgets/AudioDial.h"
#include "widgets/IconLoader.h"
#include "widgets/KeyReference.h"
#include "widgets/LevelPanToolButton.h"
#include "audio/AudioCallbackPlaySource.h"
#include "audio/AudioCallbackRecordTarget.h"
#include "audio/PlaySpeedRangeMapper.h"
#include "base/Profiler.h"
#include "base/UnitDatabase.h"
#include "layer/ColourDatabase.h"
#include "layer/LayerFactory.h"
#include "base/Selection.h"
#include "data/model/Model.h"

#include "rdf/RDFImporter.h"
#include "data/fileio/DataFileReaderFactory.h"
#include "data/fileio/CSVFormat.h"
#include "data/fileio/CSVFileWriter.h"
#include "data/fileio/MIDIFileWriter.h"
#include "rdf/RDFExporter.h"

#include "widgets/RangeInputDialog.h"
#include "widgets/ActivityLog.h"
#include "widgets/InteractiveFileFinder.h"

// For version information
#include "vamp/vamp.h"
#include "vamp-sdk/PluginBase.h"
#include "plugin/api/ladspa.h"
#include "plugin/api/dssi.h"

#include <bqaudioio/SystemPlaybackTarget.h>
#include <bqaudioio/SystemAudioIO.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QGridLayout>
#include <QLabel>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QInputDialog>
#include <QStatusBar>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QPainter>
#include <QWidgetAction>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QActionGroup>
#include <QRegularExpression>
#include <QTimer>
#include <QEventLoop>
#include <QTextStream>

#include <algorithm>
#include <iostream>
#include <cstdio>
#include <cmath>
#include <errno.h>

using std::vector;

using std::cerr;
using std::endl;

using namespace sv;

MainWindow::MainWindow(AudioMode audioMode,
                       bool withSonification, 
                       bool withSpectrogram) :
    MainWindowBase(audioMode,
                   MainWindowBase::MIDI_NONE,
                   int(PaneStack::Option::NoPropertyStacks) |
                   int(PaneStack::Option::NoPaneAccessories)),
    m_analyser2(nullptr),
    m_realtimePitchTracker(nullptr),
    m_realtimePitchLayer(nullptr),
    m_realtimeDotsNotifier(40),
    m_overview(0),
    m_showSingingPitch(nullptr),
    m_showSingingNotes(nullptr),
    m_playSingingAudio(nullptr),
    m_playRefWhileRecording(nullptr),
    m_preRoll(nullptr),
    m_recordIntoSelection(nullptr),
    m_loadSingingTrackAction(nullptr),
    m_alternatePitch(nullptr),
    m_showAlternatePitch(nullptr),
    m_alternatePitchUpAction(nullptr),
    m_alternatePitchDownAction(nullptr),
    m_referencePitchHiddenForTake(false),
    m_singingPitchHiddenForTake(false),
    m_singingNotesHiddenForTake(false),
    m_takes(nullptr),
    m_coverageStrip(nullptr),
    m_takesMenu(nullptr),
    m_takeCombo(nullptr),
    m_newTakeAction(nullptr),
    m_duplicateTakeAction(nullptr),
    m_renameTakeAction(nullptr),
    m_deleteTakeAction(nullptr),
    m_updatingTakeCombo(false),
    m_restoringSession(false),
    m_eraseSingingAction(nullptr),
    m_selectRecordingAction(nullptr),
    m_openTakeCommand(nullptr),
    m_takePosition(0),
    m_takePreRoll(0),
    m_takeEnd(-1),
    m_takeTimer(nullptr),
    m_backgroundMusicModelId(),
    m_backgroundMusicLayer(nullptr),
    m_loadBackgroundMusicAction(nullptr),
    m_playBackgroundMusic(nullptr),
    m_bgMusicLPW(nullptr),
    m_loadingBackgroundMusic(false),
    m_mainMenusCreated(false),
    m_playbackMenu(0),
    m_recentFilesMenu(0), 
    m_rightButtonMenu(0),
    m_rightButtonPlaybackMenu(0),
    m_audioDeviceMenu(0),
    m_audioDeviceGroup(0),
    m_audioInputDeviceMenu(0),
    m_audioInputDeviceGroup(0),
    m_deleteSelectedAction(0),
    m_ffwdAction(0),
    m_rwdAction(0),
    m_intelligentActionOn(true), //GF: !!! temporary
    m_activityLog(new ActivityLog()),
    m_keyReference(new KeyReference()),
    m_selectionAnchor(0),
    m_withSonification(withSonification),
    m_withSpectrogram(withSpectrogram),
    m_recordingInProgress(false),
    m_recordingAsSingingTrack(false),
    m_singingAudioMutedForTake(false),
    m_singingAudioAfterTake(true),
    m_playSelectionLiftedForTake(false),
    m_paneCountBeforeRecording(0),
    m_currentRecordingModelId(),
    m_recordingLayer(nullptr),
    m_rebuildingTakeAudio(false),
    m_recordingLatencyFrames(0),
    m_recordingStartGapEstimate(0),
    m_recordingStartGapMeasured(-1),
    m_awaitingReferenceStart(false),
    m_takeLatency(),
    m_audioCheck(nullptr),
    m_audioCheckTakes(false),
    m_audioCheckRoundTrip(-1.0),
    m_audioCheckPreRoll(AudioCheckRunner::kPreRollSeconds),
#ifdef TONY_DEV_CHECKS
    m_devChecks(nullptr),
#endif
    m_recordAction(nullptr),
    m_calibrateAudioDialog(nullptr),
    m_calibrateAudioAction(nullptr),
    m_latencyLineAction(nullptr),
    m_forgetLatencyAction(nullptr),
    m_lastRecordingRate(0)
{
    setWindowTitle(QApplication::applicationName());

    // Called from the audio callback: see recordingStarted()
    if (m_playSource) {
        m_playSource->setPlayStartCallback([this](int blockFrames) {
            if (!m_awaitingReferenceStart.exchange(false)) return;
            if (!m_recordTarget || !m_recordTarget->isRecording()) return;
            // The device drivers deliver the input of a block before they
            // ask for its output (PortAudioIO, JACKAudioIO), so the count
            // already includes the input that goes with this first block
            sv_frame_t gap = m_recordTarget->getFramesReceived() - blockFrames;
            m_recordingStartGapMeasured = (gap > 0 ? gap : 0);
        });
    }

#ifdef Q_OS_MAC
#if (QT_VERSION >= QT_VERSION_CHECK(5, 2, 0))
    setUnifiedTitleAndToolBarOnMac(true);
#endif
#endif

    UnitDatabase *udb = UnitDatabase::getInstance();
    udb->registerUnit("Hz");
    udb->registerUnit("dB");
    udb->registerUnit("s");

    ColourDatabase *cdb = ColourDatabase::getInstance();
    cdb->addColour(Qt::black, tr("Black"));
    cdb->addColour(Qt::darkRed, tr("Red"));
    cdb->addColour(Qt::darkBlue, tr("Blue"));
    cdb->addColour(Qt::darkGreen, tr("Green"));
    cdb->addColour(QColor(200, 50, 255), tr("Purple"));
    cdb->addColour(QColor(255, 150, 50), tr("Orange"));
    cdb->addColour(QColor(180, 180, 180), tr("Grey"));
    cdb->setUseDarkBackground(cdb->addColour(Qt::white, tr("White")), true);
    cdb->setUseDarkBackground(cdb->addColour(Qt::red, tr("Bright Red")), true);
    cdb->setUseDarkBackground(cdb->addColour(QColor(30, 150, 255), tr("Bright Blue")), true);
    cdb->setUseDarkBackground(cdb->addColour(Qt::green, tr("Bright Green")), true);
    cdb->setUseDarkBackground(cdb->addColour(QColor(225, 74, 255), tr("Bright Purple")), true);
    cdb->setUseDarkBackground(cdb->addColour(QColor(255, 188, 80), tr("Bright Orange")), true);

    Preferences::getInstance()->setResampleOnLoad(true);
    Preferences::getInstance()->setFixedSampleRate(44100);
    Preferences::getInstance()->setSpectrogramSmoothing
        (Preferences::SpectrogramInterpolated);
    Preferences::getInstance()->setNormaliseAudio(true);

    QSettings settings;

    settings.beginGroup("MainWindow");
    settings.setValue("showstatusbar", false);
    settings.endGroup();

    settings.beginGroup("Transformer");
    settings.setValue("use-flexi-note-model", true);
    settings.endGroup();

    settings.beginGroup("LayerDefaults");
    settings.setValue("waveform",
                      QString("<layer scale=\"%1\" channelMode=\"%2\"/>")
                      .arg(int(WaveformLayer::LinearScale))
                      .arg(int(WaveformLayer::MixChannels)));
    settings.endGroup();

    m_viewManager->setAlignMode(false);
    m_viewManager->setPlaySoloMode(false);
    m_viewManager->setToolMode(ViewManager::NavigateMode);
    m_viewManager->setZoomWheelsEnabled(false);
    m_viewManager->setIlluminateLocalFeatures(true);
    m_viewManager->setShowWorkTitle(false);
    m_viewManager->setShowCentreLine(false);
    m_viewManager->setShowDuration(false);
    m_viewManager->setOverlayMode(ViewManager::GlobalOverlays);

    connect(m_viewManager, SIGNAL(selectionChangedByUser()),
	    this, SLOT(selectionChangedByUser()));

    QFrame *frame = new QFrame;
    setCentralWidget(frame);

    QGridLayout *layout = new QGridLayout;
    
    QScrollArea *scroll = new QScrollArea(frame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    // We have a pane stack: it comes with the territory. However, we
    // have a fixed and known number of panes in it -- it isn't
    // variable
    // By member pointer: the slot takes sv::sv_frame_t, which a SLOT()
    // string saying sv_frame_t does not match under every Qt version
    connect(m_paneStack, &PaneStack::doubleClickSelectInvoked,
            this, &MainWindow::doubleClickSelectInvoked);
    scroll->setWidget(m_paneStack);

    m_overview = new Overview(frame);
    m_overview->setPlaybackFollow(PlaybackScrollPage);
    m_overview->setViewManager(m_viewManager);
    m_overview->setFixedHeight(60);
#ifndef _WIN32
    // For some reason, the contents of the overview never appear if we
    // make this setting on Windows.  I have no inclination at the moment
    // to track down the reason why.
    m_overview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
#endif
    connect(m_overview, SIGNAL(contextHelpChanged(const QString &)),
            this, SLOT(contextHelpChanged(const QString &)));

    m_panLayer = new WaveformLayer;
    m_panLayer->setChannelMode(WaveformLayer::MergeChannels);
    m_panLayer->setAggressiveCacheing(true);
    m_panLayer->setGain(0.5);
    m_overview->addLayer(m_panLayer);

    if (m_viewManager->getGlobalDarkBackground()) {
        m_panLayer->setBaseColour
            (ColourDatabase::getInstance()->getColourIndex(tr("Bright Green")));
    } else {
        m_panLayer->setBaseColour
            (ColourDatabase::getInstance()->getColourIndex(tr("Blue")));
    }        

    m_fader = new Fader(frame, false);
    connect(m_fader, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
    connect(m_fader, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

    m_playSpeed = new AudioDial(frame);
    m_playSpeed->setMeterColor(Qt::darkBlue);
    m_playSpeed->setMinimum(0);
    m_playSpeed->setMaximum(120);
    m_playSpeed->setValue(60);
    m_playSpeed->setFixedWidth(24);
    m_playSpeed->setFixedHeight(24);
    m_playSpeed->setNotchesVisible(true);
    m_playSpeed->setPageStep(10);
    m_playSpeed->setObjectName(tr("Playback Speed"));
    m_playSpeed->setDefaultValue(60);
    m_playSpeed->setRangeMapper(new PlaySpeedRangeMapper);
    m_playSpeed->setShowToolTip(true);
    connect(m_playSpeed, SIGNAL(valueChanged(int)),
        this, SLOT(playSpeedChanged(int)));
    connect(m_playSpeed, SIGNAL(mouseEntered()), this, SLOT(mouseEnteredWidget()));
    connect(m_playSpeed, SIGNAL(mouseLeft()), this, SLOT(mouseLeftWidget()));

    m_audioLPW = new LevelPanToolButton(frame);
    m_audioLPW->setIncludeMute(false);
    m_audioLPW->setObjectName(tr("Audio Track Level and Pan"));
    connect(m_audioLPW, SIGNAL(levelChanged(float)), this, SLOT(audioGainChanged(float)));
    connect(m_audioLPW, SIGNAL(panChanged(float)), this, SLOT(audioPanChanged(float)));

    m_bgMusicLPW = new LevelPanToolButton(frame);
    m_bgMusicLPW->setIncludeMute(false);
    m_bgMusicLPW->setObjectName(tr("Background Music Level and Pan"));
    connect(m_bgMusicLPW, SIGNAL(levelChanged(float)), this, SLOT(backgroundMusicGainChanged(float)));
    connect(m_bgMusicLPW, SIGNAL(panChanged(float)), this, SLOT(backgroundMusicPanChanged(float)));

    if (m_withSonification) {

        m_pitchLPW = new LevelPanToolButton(frame);
        m_pitchLPW->setIncludeMute(false);
        m_pitchLPW->setObjectName(tr("Pitch Track Level and Pan"));
        connect(m_pitchLPW, SIGNAL(levelChanged(float)), this, SLOT(pitchGainChanged(float)));
        connect(m_pitchLPW, SIGNAL(panChanged(float)), this, SLOT(pitchPanChanged(float)));

        m_notesLPW = new LevelPanToolButton(frame);
        m_notesLPW->setIncludeMute(false);
        m_notesLPW->setObjectName(tr("Note Track Level and Pan"));
        connect(m_notesLPW, SIGNAL(levelChanged(float)), this, SLOT(notesGainChanged(float)));
        connect(m_notesLPW, SIGNAL(panChanged(float)), this, SLOT(notesPanChanged(float)));
    }

    layout->setSpacing(4);
    layout->addWidget(m_overview, 0, 1);
    layout->addWidget(scroll, 1, 1);

    layout->setColumnStretch(1, 10);

    frame->setLayout(layout);

    m_analyser = new Analyser();
    connect(m_analyser, SIGNAL(layersChanged()),
            this, SLOT(updateLayerStatuses()));
    connect(m_analyser, SIGNAL(layersChanged()),
            this, SLOT(updateMenuStates()));

    m_alternatePitch = new AlternatePitchTrack(this);
    connect(m_analyser, SIGNAL(layersChanged()),
            this, SLOT(syncAlternatePitchTrack()));

    m_takes = new SingingTakes(this);
    m_coverageStrip = new CoverageStrip(this);
    m_audioCheck = new AudioCheckRunner(this);

    // What may be chosen changes as a check begins and as it ends.  The
    // first progress comes from its first step, before anything is asked
    connect(m_audioCheck, &AudioCheckRunner::progress,
            this, [this]() { updateMenuStates(); });
    connect(m_audioCheck, &AudioCheckRunner::finished,
            this, [this]() { updateMenuStates(); });

#ifdef TONY_DEV_CHECKS
    // Likewise for the development checks, which run the check among
    // stages of their own
    m_devChecks = new DevChecks(this, m_audioCheck);
    connect(m_devChecks, &DevChecks::progress,
            this, [this]() { updateMenuStates(); });
    connect(m_devChecks, &DevChecks::finished,
            this, [this]() { updateMenuStates(); });
#endif

    // Often enough to stop a take that records into a selection well
    // within the margin that follows the selection's end
    m_takeTimer = new QTimer(this);
    m_takeTimer->setInterval(100);
    connect(m_takeTimer, SIGNAL(timeout()), this, SLOT(pollTakeProgress()));

    setupMenus();
    setupToolbars();
    setupHelpMenu();

    statusBar();

    finaliseMenus();

    connect(m_viewManager, SIGNAL(activity(QString)),
            m_activityLog, SLOT(activityHappened(QString)));
    connect(m_playSource, SIGNAL(activity(QString)),
            m_activityLog, SLOT(activityHappened(QString)));
    connect(CommandHistory::getInstance(), SIGNAL(activity(QString)),
            m_activityLog, SLOT(activityHappened(QString)));
    connect(this, SIGNAL(activity(QString)),
            m_activityLog, SLOT(activityHappened(QString)));
    connect(this, SIGNAL(replacedDocument()), this, SLOT(documentReplaced()));
    connect(this, SIGNAL(sessionLoaded()), this, SLOT(analyseNewMainModel()));
    connect(this, SIGNAL(audioFileLoaded()), this, SLOT(analyseNewMainModel()));

    // Connect record target signals for real-time pitch tracking
    if (m_recordTarget) {
        connect(m_recordTarget, SIGNAL(recordStatusChanged(bool)),
                this, SLOT(recordingStarted()));
    }
    m_activityLog->hide();

    setAudioRecordMode(RecordReplaceSession);
    
    newSession();

    settings.beginGroup("MainWindow");
    settings.setValue("zoom-default", 512);
    settings.endGroup();
    zoomDefault();

    NetworkPermissionTester tester;
    bool networkPermission = tester.havePermission();
    if (networkPermission) {
        m_versionTester = new VersionTester
            ("sonicvisualiser.org", "latest-tony-version.txt", TONY_VERSION);
        connect(m_versionTester, SIGNAL(newerVersionAvailable(QString)),
                this, SLOT(newerVersionAvailable(QString)));
    } else {
        m_versionTester = 0;
    }
}

MainWindow::~MainWindow()
{
    // The check's dialog first, as it holds the runner and the dev
    // checks; then the dev checks, which drive the runner; then a check
    // still running ends here, before anything it reads goes
    delete m_calibrateAudioDialog;
    m_calibrateAudioDialog = nullptr;
#ifdef TONY_DEV_CHECKS
    delete m_devChecks;
    m_devChecks = nullptr;
#endif
    delete m_audioCheck;
    m_audioCheck = nullptr;

    // Nothing must poll a take while the window is coming down
    stopTakePolling();

    // The command history is a singleton and outlives the window, and a
    // take's command holds the window it belongs to.  closeSession() has
    // usually cleared it already; this is for the paths that do not go
    // through it.  Before anything is torn down, while a command that has
    // a layer to delete can still find the document
    m_openTakeCommand = nullptr;
    CommandHistory::getInstance()->clear();

    // Clean up secondary state that may not have been torn down if the
    // window was closed without going through closeSession() (e.g. on
    // application exit via the window close button).
    if (m_realtimePitchTracker) {
        m_realtimePitchTracker->stop();
        delete m_realtimePitchTracker;
        m_realtimePitchTracker = nullptr;
    }
    // m_realtimePitchLayer is a Layer* owned by the Document (registered via
    // createEmptyLayer / deleteLayer).  The Document is owned by MainWindowBase
    // and will be destroyed after this destructor returns, so we only null
    // the pointer here — the Document will release the layer.
    m_realtimePitchLayer = nullptr;

    // m_analyser2 is a plain heap-allocated QObject (no Qt parent, not owned
    // by the Document).  We must delete it explicitly.
    if (m_analyser2) {
        delete m_analyser2;
        m_analyser2 = nullptr;
    }
    // Before the base class deletes the document: it only watches the
    // document's layers, it does not own them
    delete m_alternatePitch;
    m_alternatePitch = nullptr;
    delete m_coverageStrip;
    m_coverageStrip = nullptr;
    delete m_analyser;
    delete m_keyReference;
    Profiles::getInstance()->dump();
}

void
MainWindow::setupMenus()
{
    if (!m_mainMenusCreated) {

#ifdef Q_OS_LINUX
        // In Ubuntu 14.04 the window's menu bar goes missing entirely
        // if the user is running any desktop environment other than Unity
        // (in which the faux single-menubar appears). The user has a
        // workaround, to remove the appmenu-qt5 package, but that is
        // awkward and the problem is so severe that it merits disabling
        // the system menubar integration altogether. Like this:
	menuBar()->setNativeMenuBar(false);
#endif

        m_rightButtonMenu = new QMenu();
    }

    if (!m_mainMenusCreated) {
        CommandHistory::getInstance()->registerMenu(m_rightButtonMenu);
        m_rightButtonMenu->addSeparator();
    }

    setupFileMenu();
    setupEditMenu();
    setupViewMenu();
    setupAnalysisMenu();
    setupTakesMenu();

    m_mainMenusCreated = true;
}

void
MainWindow::setupFileMenu()
{
    if (m_mainMenusCreated) return;

    QMenu *menu = menuBar()->addMenu(tr("&File"));
    menu->setTearOffEnabled(true);
    QToolBar *toolbar = addToolBar(tr("File Toolbar"));

    m_keyReference->setCategory(tr("File and Session Management"));

    IconLoader il;
    QIcon icon;
    QAction *action;

    icon = il.load("fileopen");
    action = new QAction(icon, tr("&Open..."), this);
    action->setShortcut(tr("Ctrl+O"));
    action->setStatusTip(tr("Open a session or audio file"));
    connect(action, SIGNAL(triggered()), this, SLOT(openFile()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    toolbar->addAction(action);

    action = new QAction(tr("Open Lo&cation..."), this);
    action->setShortcut(tr("Ctrl+Shift+O"));
    action->setStatusTip(tr("Open a file from a remote URL"));
    connect(action, SIGNAL(triggered()), this, SLOT(openLocation()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    m_recentFilesMenu = menu->addMenu(tr("Open &Recent"));
    m_recentFilesMenu->setTearOffEnabled(true);
    setupRecentFilesMenu();
    connect(&m_recentFiles, SIGNAL(recentChanged()),
            this, SLOT(setupRecentFilesMenu()));

    menu->addSeparator();

    icon = il.load("filesave");
    action = new QAction(icon, tr("&Save Session"), this);
    action->setShortcut(tr("Ctrl+S"));
    action->setStatusTip(tr("Save the current session into a %1 session file").arg(QApplication::applicationName()));
    connect(action, SIGNAL(triggered()), this, SLOT(saveSession()));
    connect(this, SIGNAL(canSave(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    toolbar->addAction(action);
	
    icon = il.load("filesaveas");
    action = new QAction(icon, tr("Save Session &As..."), this);
    action->setShortcut(tr("Ctrl+Shift+S"));
    action->setStatusTip(tr("Save the current session into a new %1 session file").arg(QApplication::applicationName()));
    connect(action, SIGNAL(triggered()), this, SLOT(saveSessionAs()));
    connect(this, SIGNAL(canSaveAs(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    toolbar->addAction(action);

    action = new QAction(tr("Save Session to Audio File &Path"), this);
    action->setShortcut(tr("Ctrl+Alt+S"));
    action->setStatusTip(tr("Save the current session into a %1 session file with the same filename as the audio but a .ton extension.").arg(QApplication::applicationName()));
    connect(action, SIGNAL(triggered()), this, SLOT(saveSessionInAudioPath()));
    connect(this, SIGNAL(canSaveAs(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    menu->addSeparator();

    m_loadSingingTrackAction = new QAction(il.load("fileopen"), tr("Load &Singing Track..."), this);
    m_loadSingingTrackAction->setShortcut(tr("Ctrl+Shift+R"));
    m_loadSingingTrackAction->setStatusTip(tr("Load a second audio file to analyse as the singing track, overlaid on the reference track"));
    connect(m_loadSingingTrackAction, SIGNAL(triggered()), this, SLOT(openSingingTrack()));
    connect(this, SIGNAL(canPlay(bool)), m_loadSingingTrackAction, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(m_loadSingingTrackAction);
    menu->addAction(m_loadSingingTrackAction);

    m_loadBackgroundMusicAction = new QAction(il.load("fileopen"), tr("Load &Background Music..."), this);
    m_loadBackgroundMusicAction->setStatusTip(tr("Load an audio file to play as background music alongside the reference track (not analysed)"));
    connect(m_loadBackgroundMusicAction, SIGNAL(triggered()), this, SLOT(openBackgroundMusic()));
    connect(this, SIGNAL(canPlay(bool)), m_loadBackgroundMusicAction, SLOT(setEnabled(bool)));
    menu->addAction(m_loadBackgroundMusicAction);

    menu->addSeparator();

    action = new QAction(tr("I&mport Pitch Track Data..."), this);
    action->setStatusTip(tr("Import pitch-track data from a CSV, RDF, or layer XML file"));
    connect(action, SIGNAL(triggered()), this, SLOT(importPitchLayer()));
    connect(this, SIGNAL(canImportLayer(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    action = new QAction(tr("E&xport Pitch Track Data..."), this);
    action->setStatusTip(tr("Export pitch-track data to a CSV, RDF, or layer XML file"));
    connect(action, SIGNAL(triggered()), this, SLOT(exportPitchLayer()));
    connect(this, SIGNAL(canExportPitchTrack(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    action = new QAction(tr("&Export Note Data..."), this);
    action->setStatusTip(tr("Export note data to a CSV, RDF, layer XML, or MIDI file"));
    connect(action, SIGNAL(triggered()), this, SLOT(exportNoteLayer()));
    connect(this, SIGNAL(canExportNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    menu->addSeparator();
    
    action = new QAction(tr("Browse Recorded Audio"), this);
    action->setStatusTip(tr("Open the Recorded Audio folder in the system file browser"));
    connect(action, SIGNAL(triggered()), this, SLOT(browseRecordedAudio()));
    menu->addAction(action);

    menu->addSeparator();

    action = new QAction(il.load("exit"), tr("&Quit"), this);
    action->setShortcut(tr("Ctrl+Q"));
    action->setStatusTip(tr("Exit %1").arg(QApplication::applicationName()));
    connect(action, SIGNAL(triggered()), this, SLOT(close()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
}

void
MainWindow::setupEditMenu()
{
    if (m_mainMenusCreated) return;

    QMenu *menu = menuBar()->addMenu(tr("&Edit"));
    menu->setTearOffEnabled(true);
    CommandHistory::getInstance()->registerMenu(menu);
    menu->addSeparator();

    m_keyReference->setCategory
        (tr("Selection Strip Mouse Actions"));
    m_keyReference->registerShortcut
        (tr("Jump"), tr("Left"), 
         tr("Click left button to move the playback position to a time"));
    m_keyReference->registerShortcut
        (tr("Select"), tr("Left"), 
         tr("Click left button and drag to select a region of time"));
    m_keyReference->registerShortcut
        (tr("Select Note Duration"), tr("Double-Click Left"), 
         tr("Double-click left button to select the region of time corresponding to a note"));

    QToolBar *toolbar = addToolBar(tr("Tools Toolbar"));
    
    CommandHistory::getInstance()->registerToolbar(toolbar);

    QActionGroup *group = new QActionGroup(this);

    IconLoader il;

    m_keyReference->setCategory(tr("Tool Selection"));
    QAction *action = toolbar->addAction(il.load("navigate"),
                                         tr("Navigate"));
    action->setCheckable(true);
    action->setChecked(true);
    action->setShortcut(tr("1"));
    action->setStatusTip(tr("Navigate"));
    connect(action, SIGNAL(triggered()), this, SLOT(toolNavigateSelected()));
    connect(this, SIGNAL(replacedDocument()), action, SLOT(trigger()));
    group->addAction(action);
    menu->addAction(action);
    m_keyReference->registerShortcut(action);

    m_keyReference->setCategory
        (tr("Navigate Tool Mouse Actions"));
    m_keyReference->registerShortcut
        (tr("Navigate"), tr("Left"), 
         tr("Click left button and drag to move around"));
    m_keyReference->registerShortcut
        (tr("Re-Analyse Area"), tr("Shift+Left"), 
         tr("Shift-click left button and drag to define a specific pitch and time range to re-analyse"));
    m_keyReference->registerShortcut
        (tr("Edit"), tr("Double-Click Left"), 
         tr("Double-click left button on an item to edit it"));

    m_keyReference->setCategory(tr("Tool Selection"));
    action = toolbar->addAction(il.load("move"),
				tr("Edit"));
    action->setCheckable(true);
    action->setShortcut(tr("2"));
    action->setStatusTip(tr("Edit with Note Intelligence"));
    connect(action, SIGNAL(triggered()), this, SLOT(toolEditSelected()));
    group->addAction(action);
    menu->addAction(action);
    m_keyReference->registerShortcut(action);

    m_keyReference->setCategory
        (tr("Note Edit Tool Mouse Actions"));
    m_keyReference->registerShortcut
        (tr("Adjust Pitch"), tr("Left"), 
        tr("Click left button on the main part of a note and drag to move it up or down"));
    m_keyReference->registerShortcut
        (tr("Split"), tr("Left"), 
        tr("Click left button on the bottom edge of a note to split it at the click point"));
    m_keyReference->registerShortcut
        (tr("Resize"), tr("Left"), 
        tr("Click left button on the left or right edge of a note and drag to change the time or duration of the note"));
    m_keyReference->registerShortcut
        (tr("Erase"), tr("Shift+Left"), 
        tr("Shift-click left button on a note to remove it"));


/* Remove for now...

    m_keyReference->setCategory(tr("Tool Selection"));
    action = toolbar->addAction(il.load("notes"),
				tr("Free Edit"));
    action->setCheckable(true);
    action->setShortcut(tr("3"));
    action->setStatusTip(tr("Free Edit"));
    connect(action, SIGNAL(triggered()), this, SLOT(toolFreeEditSelected()));
    group->addAction(action);
    m_keyReference->registerShortcut(action);
*/

    menu->addSeparator();
    
    m_keyReference->setCategory(tr("Selection"));

    action = new QAction(tr("Select &All"), this);
    action->setShortcut(tr("Ctrl+A"));
    action->setStatusTip(tr("Select the whole duration of the current session"));
    connect(action, SIGNAL(triggered()), this, SLOT(selectAll()));
    connect(this, SIGNAL(canSelect(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    action = new QAction(tr("C&lear Selection"), this);
    action->setShortcuts(QList<QKeySequence>()
                         << QKeySequence(tr("Esc"))
                         << QKeySequence(tr("Ctrl+Esc")));
    action->setStatusTip(tr("Clear the selection and abandon any pending pitch choices in it"));
    connect(action, SIGNAL(triggered()), this, SLOT(abandonSelection()));
    connect(this, SIGNAL(canClearSelection(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    m_keyReference->registerAlternativeShortcut(action, QKeySequence(tr("Ctrl+Esc")));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    menu->addSeparator();
    m_rightButtonMenu->addSeparator();
    
    m_keyReference->setCategory(tr("Pitch Track"));
    
    action = new QAction(tr("Choose Higher Pitch"), this);
    action->setShortcut(tr("Ctrl+Up"));
    action->setStatusTip(tr("Move pitches up an octave, or to the next higher pitch candidate"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(switchPitchUp()));
    connect(this, SIGNAL(canClearSelection(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);
    
    action = new QAction(tr("Choose Lower Pitch"), this);
    action->setShortcut(tr("Ctrl+Down"));
    action->setStatusTip(tr("Move pitches down an octave, or to the next lower pitch candidate"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(switchPitchDown()));
    connect(this, SIGNAL(canClearSelection(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    m_showCandidatesAction = new QAction(tr("Show Pitch Candidates"), this);
    m_showCandidatesAction->setShortcut(tr("Ctrl+Return"));
    m_showCandidatesAction->setStatusTip(tr("Toggle the display of alternative pitch candidates for the selected region"));
    m_keyReference->registerShortcut(m_showCandidatesAction);
    connect(m_showCandidatesAction, SIGNAL(triggered()), this, SLOT(togglePitchCandidates()));
    connect(this, SIGNAL(canClearSelection(bool)), m_showCandidatesAction, SLOT(setEnabled(bool)));
    menu->addAction(m_showCandidatesAction);
    m_rightButtonMenu->addAction(m_showCandidatesAction);
    
    action = new QAction(tr("Remove Pitches"), this);
    action->setShortcut(tr("Ctrl+Backspace"));
    action->setStatusTip(tr("Remove all pitch estimates within the selected region, making it unvoiced"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(clearPitches()));
    connect(this, SIGNAL(canClearSelection(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    menu->addSeparator();
    m_rightButtonMenu->addSeparator();
    
    m_keyReference->setCategory(tr("Note Track"));

    action = new QAction(tr("Split Note"), this);
    action->setShortcut(tr("/"));
    action->setStatusTip(tr("Split the note at the current playback position into two"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(splitNote()));
    connect(this, SIGNAL(canExportNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    action = new QAction(tr("Merge Notes"), this);
    action->setShortcut(tr("\\"));
    action->setStatusTip(tr("Merge all notes within the selected region into a single note"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(mergeNotes()));
    connect(this, SIGNAL(canSnapNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    action = new QAction(tr("Delete Notes"), this);
    action->setShortcut(tr("Backspace"));
    action->setStatusTip(tr("Delete all notes within the selected region"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(deleteNotes()));
    connect(this, SIGNAL(canSnapNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);
    
    action = new QAction(tr("Form Note from Selection"), this);
    action->setShortcut(tr("="));
    action->setStatusTip(tr("Form a note spanning the selected region, splitting any existing notes at its boundaries"));
    m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(formNoteFromSelection()));
    connect(this, SIGNAL(canSnapNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    action = new QAction(tr("Snap Notes to Pitch Track"), this);
    action->setStatusTip(tr("Set notes within the selected region to the median frequency of their underlying pitches, or remove them if there are no underlying pitches"));
    // m_keyReference->registerShortcut(action);
    connect(action, SIGNAL(triggered()), this, SLOT(snapNotesToPitches()));
    connect(this, SIGNAL(canSnapNotes(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);
    m_rightButtonMenu->addAction(action);

    menu->addSeparator();
    m_rightButtonMenu->addSeparator();

    m_keyReference->setCategory(tr("Singing Track"));

    // No shortcut for this one: the keys worth having in this menu are
    // taken, and it is a step on the way to the erase rather than
    // something to reach for on its own
    m_selectRecordingAction =
        new QAction(tr("Select Recording at Playhead"), this);
    m_selectRecordingAction->setStatusTip
        (tr("Select the range of recorded singing that the playback position is in"));
    connect(m_selectRecordingAction, SIGNAL(triggered()),
            this, SLOT(selectRecordingAtPlayhead()));
    connect(this, SIGNAL(canSelectRecording(bool)),
            m_selectRecordingAction, SLOT(setEnabled(bool)));
    m_selectRecordingAction->setEnabled(false);
    menu->addAction(m_selectRecordingAction);
    m_rightButtonMenu->addAction(m_selectRecordingAction);

    m_eraseSingingAction = new QAction(tr("Erase Singing in Selection"), this);
    // Ctrl+Backspace, the obvious partner to the Backspace of Delete
    // Notes, is upstream Tony's Remove Pitches
    m_eraseSingingAction->setShortcut(tr("Ctrl+D"));
    m_eraseSingingAction->setStatusTip
        (tr("Remove the recorded singing within the selected region, leaving silence"));
    m_keyReference->registerShortcut(m_eraseSingingAction);
    connect(m_eraseSingingAction, SIGNAL(triggered()),
            this, SLOT(eraseSingingInSelection()));
    connect(this, SIGNAL(canEraseSinging(bool)),
            m_eraseSingingAction, SLOT(setEnabled(bool)));
    m_eraseSingingAction->setEnabled(false);
    menu->addAction(m_eraseSingingAction);
    m_rightButtonMenu->addAction(m_eraseSingingAction);
}

void
MainWindow::setupViewMenu()
{
    if (m_mainMenusCreated) return;

    IconLoader il;

    QAction *action = 0;

    m_keyReference->setCategory(tr("Panning and Navigation"));

    QMenu *menu = menuBar()->addMenu(tr("&View"));
    menu->setTearOffEnabled(true);
    action = new QAction(tr("Peek &Left"), this);
    action->setShortcut(tr("Alt+Left"));
    action->setStatusTip(tr("Scroll the current pane to the left without changing the play position"));
    connect(action, SIGNAL(triggered()), this, SLOT(scrollLeft()));
    connect(this, SIGNAL(canScroll(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    
    action = new QAction(tr("Peek &Right"), this);
    action->setShortcut(tr("Alt+Right"));
    action->setStatusTip(tr("Scroll the current pane to the right without changing the play position"));
    connect(action, SIGNAL(triggered()), this, SLOT(scrollRight()));
    connect(this, SIGNAL(canScroll(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    menu->addSeparator();

    m_keyReference->setCategory(tr("Zoom"));

    action = new QAction(il.load("zoom-in"),
                         tr("Zoom &In"), this);
    action->setShortcut(tr("Up"));
    action->setStatusTip(tr("Increase the zoom level"));
    connect(action, SIGNAL(triggered()), this, SLOT(zoomIn()));
    connect(this, SIGNAL(canZoom(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    
    action = new QAction(il.load("zoom-out"),
                         tr("Zoom &Out"), this);
    action->setShortcut(tr("Down"));
    action->setStatusTip(tr("Decrease the zoom level"));
    connect(action, SIGNAL(triggered()), this, SLOT(zoomOut()));
    connect(this, SIGNAL(canZoom(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    
    action = new QAction(tr("Restore &Default Zoom"), this);
    action->setStatusTip(tr("Restore the zoom level to the default"));
    connect(action, SIGNAL(triggered()), this, SLOT(zoomDefault()));
    connect(this, SIGNAL(canZoom(bool)), action, SLOT(setEnabled(bool)));
    menu->addAction(action);

    action = new QAction(il.load("zoom-fit"),
                         tr("Zoom to &Fit"), this);
    action->setShortcut(tr("F"));
    action->setStatusTip(tr("Zoom to show the whole file"));
    connect(action, SIGNAL(triggered()), this, SLOT(zoomToFit()));
    connect(this, SIGNAL(canZoom(bool)), action, SLOT(setEnabled(bool)));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    menu->addSeparator();
    
    action = new QAction(tr("Set Displayed Fre&quency Range..."), this);
    action->setStatusTip(tr("Set the minimum and maximum frequencies in the visible display"));
    connect(action, SIGNAL(triggered()), this, SLOT(editDisplayExtents()));
    menu->addAction(action);
}

void
MainWindow::setupAnalysisMenu()
{
    if (m_mainMenusCreated) return;

    IconLoader il;

    QAction *action = 0;

    QMenu *menu = menuBar()->addMenu(tr("&Analysis"));
    menu->setTearOffEnabled(true);

    m_autoAnalyse = new QAction(tr("Auto-Analyse &New Audio"), this);
    m_autoAnalyse->setStatusTip(tr("Automatically trigger analysis upon opening of a new audio file."));
    m_autoAnalyse->setCheckable(true);
    connect(m_autoAnalyse, SIGNAL(triggered()), this, SLOT(autoAnalysisToggled()));
    menu->addAction(m_autoAnalyse);

    action = new QAction(tr("&Analyse Now!"), this);
    action->setStatusTip(tr("Trigger analysis of pitches and notes. (This will delete all existing pitches and notes.)"));
    connect(action, SIGNAL(triggered()), this, SLOT(analyseNow()));
    menu->addAction(action);
    m_keyReference->registerShortcut(action);

    menu->addSeparator();

    m_precise = new QAction(tr("&Unbiased Timing (slow)"), this);
    m_precise->setStatusTip(tr("Use a symmetric window in YIN to remove frequency-dependent timing bias. (This is slow!)"));
    m_precise->setCheckable(true);
    connect(m_precise, SIGNAL(triggered()), this, SLOT(precisionAnalysisToggled()));
    menu->addAction(m_precise);

    m_lowamp = new QAction(tr("&Penalise Soft Pitches"), this);
    m_lowamp->setStatusTip(tr("Reduce the likelihood of detecting a pitch when the signal has low amplitude."));
    m_lowamp->setCheckable(true);
    connect(m_lowamp, SIGNAL(triggered()), this, SLOT(lowampAnalysisToggled()));
    menu->addAction(m_lowamp);

    m_onset = new QAction(tr("&High Onset Sensitivity"), this);
    m_onset->setStatusTip(tr("Increase likelihood of separating notes, especially consecutive notes at the same pitch."));
    m_onset->setCheckable(true);
    connect(m_onset, SIGNAL(triggered()), this, SLOT(onsetAnalysisToggled()));
    menu->addAction(m_onset);

    m_prune = new QAction(tr("&Drop Short Notes"), this);
    m_prune->setStatusTip(tr("Duration-based pruning: automatic note estimator will not output notes of less than 100ms duration."));
    m_prune->setCheckable(true);
    connect(m_prune, SIGNAL(triggered()), this, SLOT(pruneAnalysisToggled()));
    menu->addAction(m_prune);

    menu->addSeparator();

    action = new QAction(tr("Reset Options to Defaults"), this);
    action->setStatusTip(tr("Reset all of the Analyse menu options to their default settings."));
    connect(action, SIGNAL(triggered()), this, SLOT(resetAnalyseOptions()));
    menu->addAction(action);

    updateAnalyseStates();
}

void
MainWindow::setupTakesMenu()
{
    if (m_mainMenusCreated) return;

    // The takes of the session: each is a whole singing performance of
    // its own, with its own audio, pitch track and notes (spec 5.3).
    // Switching between them is the combo box in the playback toolbar;
    // making, copying, renaming and deleting them are here.  None of it
    // is undoable, and all of it clears the undo history but the rename
    m_takesMenu = menuBar()->addMenu(tr("Ta&kes"));
    m_takesMenu->setTearOffEnabled(true);

    m_keyReference->setCategory(tr("Takes"));

    m_newTakeAction = new QAction(tr("&New Empty Take"), this);
    m_newTakeAction->setStatusTip
        (tr("Start another take: an empty one, which the next recording "
            "goes into, leaving this take as it is"));
    connect(m_newTakeAction, SIGNAL(triggered()), this, SLOT(newEmptyTake()));
    connect(this, SIGNAL(canChangeTakes(bool)),
            m_newTakeAction, SLOT(setEnabled(bool)));
    m_newTakeAction->setEnabled(false);
    m_takesMenu->addAction(m_newTakeAction);

    m_duplicateTakeAction = new QAction(tr("&Duplicate Take"), this);
    m_duplicateTakeAction->setStatusTip
        (tr("Start another take holding a copy of this one, and carry on "
            "in the copy"));
    connect(m_duplicateTakeAction, SIGNAL(triggered()),
            this, SLOT(duplicateTake()));
    connect(this, SIGNAL(canActOnTake(bool)),
            m_duplicateTakeAction, SLOT(setEnabled(bool)));
    m_duplicateTakeAction->setEnabled(false);
    m_takesMenu->addAction(m_duplicateTakeAction);

    m_takesMenu->addSeparator();

    m_renameTakeAction = new QAction(tr("&Rename Take..."), this);
    m_renameTakeAction->setStatusTip(tr("Give this take another name"));
    connect(m_renameTakeAction, SIGNAL(triggered()), this, SLOT(renameTake()));
    connect(this, SIGNAL(canActOnTake(bool)),
            m_renameTakeAction, SLOT(setEnabled(bool)));
    m_renameTakeAction->setEnabled(false);
    m_takesMenu->addAction(m_renameTakeAction);

    m_deleteTakeAction = new QAction(tr("De&lete Take"), this);
    m_deleteTakeAction->setStatusTip
        (tr("Delete this take, with its pitch track and notes. Its audio "
            "file is not deleted."));
    connect(m_deleteTakeAction, SIGNAL(triggered()), this, SLOT(deleteTake()));
    connect(this, SIGNAL(canActOnTake(bool)),
            m_deleteTakeAction, SLOT(setEnabled(bool)));
    m_deleteTakeAction->setEnabled(false);
    m_takesMenu->addAction(m_deleteTakeAction);
}

void
MainWindow::resetAnalyseOptions()
{
    QSettings settings;
    settings.beginGroup("Analyser");

    settings.setValue("auto-analysis", true);
    
    auto keyMap = Analyser::getAnalysisSettings();
    for (auto p: keyMap) {
        settings.setValue(p.first, p.second);
    }

    settings.endGroup();
    updateAnalyseStates();
}

void
MainWindow::updateAnalyseStates()
{
    QSettings settings;
    settings.beginGroup("Analyser");

    bool autoAnalyse = settings.value("auto-analysis", true).toBool();
    m_autoAnalyse->setChecked(autoAnalyse);

    std::map<QString, QAction *> actions {
        { "precision-analysis", m_precise },
        { "lowamp-analysis", m_lowamp },
        { "onset-analysis", m_onset },
        { "prune-analysis", m_prune }
    };

    auto keyMap = Analyser::getAnalysisSettings();
    
    for (auto p: actions) {
        auto ki = keyMap.find(p.first);
        if (ki != keyMap.end()) {
            p.second->setChecked(settings.value
                                 (ki->first, ki->second).toBool());
        } else {
            throw std::logic_error("Internal error: One or more analysis settings keys not found in map returned by Analyser: check updateAnalyseStates and getAnalysisSettings");
        }
    }

    settings.endGroup();
}

void
MainWindow::autoAnalysisToggled()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a) return;

    bool set = a->isChecked();

    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue("auto-analysis", set);
    settings.endGroup();

    // make result visible explicitly, in case e.g. we just set the wrong key
    updateAnalyseStates();
}

void
MainWindow::precisionAnalysisToggled()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a) return;

    bool set = a->isChecked();

    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue("precision-analysis", set);
    settings.endGroup();

    // don't run analyseNow() automatically -- it's a destructive operation

    // make result visible explicitly, in case e.g. we just set the wrong key
    updateAnalyseStates();
}

void
MainWindow::lowampAnalysisToggled()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a) return;

    bool set = a->isChecked();

    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue("lowamp-analysis", set);
    settings.endGroup();

    // don't run analyseNow() automatically -- it's a destructive operation

    // make result visible explicitly, in case e.g. we just set the wrong key
    updateAnalyseStates();
}

void
MainWindow::onsetAnalysisToggled()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a) return;

    bool set = a->isChecked();

    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue("onset-analysis", set);
    settings.endGroup();

    // don't run analyseNow() automatically -- it's a destructive operation

    // make result visible explicitly, in case e.g. we just set the wrong key
    updateAnalyseStates();
}

void
MainWindow::pruneAnalysisToggled()
{
    QAction *a = qobject_cast<QAction *>(sender());
    if (!a) return;

    bool set = a->isChecked();

    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue("prune-analysis", set);
    settings.endGroup();

    // don't run analyseNow() automatically -- it's a destructive operation

    // make result visible explicitly, in case e.g. we just set the wrong key
    updateAnalyseStates();
}

void
MainWindow::setupHelpMenu()
{
    QMenu *menu = menuBar()->addMenu(tr("&Help"));
    menu->setTearOffEnabled(true);
    
    m_keyReference->setCategory(tr("Help"));

    IconLoader il;

    QString name = QApplication::applicationName();
    QAction *action;

    action = new QAction(il.load("help"),
                         tr("&Help Reference"), this); 
    action->setShortcut(tr("F1"));
    action->setStatusTip(tr("Open the %1 reference manual").arg(name)); 
    connect(action, SIGNAL(triggered()), this, SLOT(help()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);

    action = new QAction(tr("&Key and Mouse Reference"), this);
    action->setShortcut(tr("F2"));
    action->setStatusTip(tr("Open a window showing the keystrokes you can use in %1").arg(name));
    connect(action, SIGNAL(triggered()), this, SLOT(keyReference()));
    m_keyReference->registerShortcut(action);
    menu->addAction(action);
    
    action = new QAction(tr("What's &New In This Release?"), this); 
    action->setStatusTip(tr("List the changes in this release (and every previous release) of %1").arg(name)); 
    connect(action, SIGNAL(triggered()), this, SLOT(whatsNew()));
    menu->addAction(action);
    
    action = new QAction(tr("&About %1").arg(name), this); 
    action->setStatusTip(tr("Show information about %1").arg(name)); 
    connect(action, SIGNAL(triggered()), this, SLOT(about()));
    menu->addAction(action);
}

void
MainWindow::setupRecentFilesMenu()
{
    m_recentFilesMenu->clear();
    vector<QString> files = m_recentFiles.getRecent();
    for (size_t i = 0; i < files.size(); ++i) {
        QString path = files[i];
        QAction *action = m_recentFilesMenu->addAction(path);
        action->setObjectName(path);
        connect(action, SIGNAL(triggered()), this, SLOT(openRecentFile()));
        if (i == 0) {
            action->setShortcut(tr("Ctrl+R"));
            m_keyReference->registerShortcut
                (tr("Re-open"),
                 action->shortcut().toString(),
                 tr("Re-open the current or most recently opened file"));
        }
    }
}

namespace {

// The audio-device preference keys that MainWindowBase::createAudioIO()
// reads. The key is suffixed with the preferred driver when one has been
// pinned, so that a device name chosen for (say) JACK is not then offered
// to PortAudio.
static QString
audioDeviceSettingKey(QString base)
{
    QSettings settings;
    settings.beginGroup("Preferences");
    QString implementation = settings.value("audio-target", "").toString();
    settings.endGroup();
    if (implementation == "" || implementation == "auto") return base;
    return base + "-" + implementation;
}

// Which bqaudioio implementation the device names should come from. The
// factory can only enumerate devices for a named implementation, so when
// the driver is left on "auto" we ask the only one that was built in --
// which on Windows and macOS is always PortAudio.
static std::string
audioImplementationName()
{
    QSettings settings;
    settings.beginGroup("Preferences");
    QString implementation = settings.value("audio-target", "").toString();
    settings.endGroup();

    if (implementation != "" && implementation != "auto") {
        return implementation.toStdString();
    }

    std::vector<std::string> available =
        breakfastquay::AudioFactory::getImplementationNames();
    if (available.size() == 1) return available[0];
    return {};
}

}

void
MainWindow::buildAudioDeviceMenu(QMenu *menu,
                                 QActionGroup *group,
                                 const std::vector<std::string> &names,
                                 QString settingKey)
{
    QSettings settings;
    settings.beginGroup("Preferences");
    QString current = settings.value(settingKey, "").toString();
    settings.endGroup();

    for (QAction *a: group->actions()) {
        group->removeAction(a);
    }
    menu->clear();

    QAction *defaultAction = menu->addAction(tr("(System Default)"));
    defaultAction->setCheckable(true);
    defaultAction->setData(QString());
    defaultAction->setChecked(current == "");
    group->addAction(defaultAction);

    if (names.empty()) {
        menu->addSeparator();
        menu->addAction(tr("(No devices found)"))->setEnabled(false);
        return;
    }

    menu->addSeparator();

    bool haveCurrent = false;

    for (const std::string &name: names) {
        QString qname = QString::fromStdString(name);
        QAction *action = menu->addAction(qname);
        action->setCheckable(true);
        action->setData(qname);
        if (qname == current) {
            action->setChecked(true);
            haveCurrent = true;
        }
        group->addAction(action);
    }

    if (current != "" && !haveCurrent) {
        // The chosen device has gone away. Show it anyway, marked as
        // absent, rather than silently reverting the tick to the system
        // default -- the setting is still in force and will take effect
        // again when the device comes back.
        menu->addSeparator();
        QAction *missing = menu->addAction
            (tr("%1 (not connected)").arg(current));
        missing->setCheckable(true);
        missing->setChecked(true);
        missing->setData(current);
        group->addAction(missing);
    }
}

void
MainWindow::rescanAudioDevices()
{
    if (!m_audioDeviceMenu || !m_audioInputDeviceMenu) return;

    // PortAudio enumerates the system's devices once, when it is
    // initialised, and bqaudioio keeps it initialised for as long as an
    // audio IO object exists. So a device that appeared after Tony started
    // -- a Bluetooth speaker connected mid-session, typically -- is not in
    // the list at all until the IO is torn down and rebuilt. Do that here,
    // around the enumeration, so that opening either of these menus always
    // shows what is actually connected now.
    //
    // Deleting the IO mid-recording would drop the take, so in that case
    // leave the list as it stands. With no IO open there is nothing to
    // tear down: the enumeration below initialises PortAudio itself and so
    // gets a current list anyway.
    bool canRebuild = (m_playTarget || m_audioIO) &&
        !(m_recordTarget && m_recordTarget->isRecording());

    if (canRebuild) {
        if (m_playSource && m_playSource->isPlaying()) {
            stop();
        }
        deleteAudioIO();
    }

    std::string implementation = audioImplementationName();

    std::vector<std::string> playbackNames =
        breakfastquay::AudioFactory::getPlaybackDeviceNames(implementation);
    std::vector<std::string> recordNames =
        breakfastquay::AudioFactory::getRecordDeviceNames(implementation);

    if (canRebuild) {
        createAudioIO();
    }

    buildAudioDeviceMenu(m_audioDeviceMenu,
                         m_audioDeviceGroup,
                         playbackNames,
                         audioDeviceSettingKey("audio-playback-device"));

    buildAudioDeviceMenu(m_audioInputDeviceMenu,
                         m_audioInputDeviceGroup,
                         recordNames,
                         audioDeviceSettingKey("audio-record-device"));
}

void
MainWindow::audioDeviceSelected(QAction *action)
{
    if (!action) return;

    QString key = audioDeviceSettingKey
        (m_audioInputDeviceGroup->actions().contains(action) ?
         "audio-record-device" : "audio-playback-device");

    QSettings settings;
    settings.beginGroup("Preferences");
    settings.setValue(key, action->data().toString());
    settings.endGroup();

    if (m_playSource && m_playSource->isPlaying()) {
        stop();
    }

    // Another device may record at another rate
    m_lastRecordingRate = 0;

    recreateAudioIO();
}

void
MainWindow::setupToolbars()
{
    m_keyReference->setCategory(tr("Playback and Transport Controls"));

    IconLoader il;

    QMenu *menu = m_playbackMenu = menuBar()->addMenu(tr("Play&back"));
    menu->setTearOffEnabled(true);
    m_rightButtonMenu->addSeparator();
    m_rightButtonPlaybackMenu = m_rightButtonMenu->addMenu(tr("Playback"));

    QToolBar *toolbar = addToolBar(tr("Playback Toolbar"));

    QAction *rwdStartAction = toolbar->addAction(il.load("rewind-start"),
                                                 tr("Rewind to Start"));
    rwdStartAction->setShortcut(tr("Home"));
    rwdStartAction->setStatusTip(tr("Rewind to the start"));
    connect(rwdStartAction, SIGNAL(triggered()), this, SLOT(rewindStart()));
    connect(this, SIGNAL(canPlay(bool)), rwdStartAction, SLOT(setEnabled(bool)));

    QAction *m_rwdAction = toolbar->addAction(il.load("rewind"),
                                              tr("Rewind"));
    m_rwdAction->setShortcut(tr("Left"));
    m_rwdAction->setStatusTip(tr("Rewind to the previous one-second boundary"));
    connect(m_rwdAction, SIGNAL(triggered()), this, SLOT(rewind()));
    connect(this, SIGNAL(canRewind(bool)), m_rwdAction, SLOT(setEnabled(bool)));

    setDefaultFfwdRwdStep(RealTime(1, 0));

    QAction *playAction = toolbar->addAction(il.load("playpause"),
                                             tr("Play / Pause"));
    playAction->setCheckable(true);
    playAction->setShortcut(tr("Space"));
    playAction->setStatusTip(tr("Start or stop playback from the current position"));
    connect(playAction, SIGNAL(triggered()), this, SLOT(play()));
    connect(m_playSource, SIGNAL(playStatusChanged(bool)),
        playAction, SLOT(setChecked(bool)));
    connect(this, SIGNAL(canPlay(bool)), playAction, SLOT(setEnabled(bool)));

    m_ffwdAction = toolbar->addAction(il.load("ffwd"),
                                              tr("Fast Forward"));
    m_ffwdAction->setShortcut(tr("Right"));
    m_ffwdAction->setStatusTip(tr("Fast-forward to the next one-second boundary"));
    connect(m_ffwdAction, SIGNAL(triggered()), this, SLOT(ffwd()));
    connect(this, SIGNAL(canFfwd(bool)), m_ffwdAction, SLOT(setEnabled(bool)));

    QAction *ffwdEndAction = toolbar->addAction(il.load("ffwd-end"),
                                                tr("Fast Forward to End"));
    ffwdEndAction->setShortcut(tr("End"));
    ffwdEndAction->setStatusTip(tr("Fast-forward to the end"));
    connect(ffwdEndAction, SIGNAL(triggered()), this, SLOT(ffwdEnd()));
    connect(this, SIGNAL(canPlay(bool)), ffwdEndAction, SLOT(setEnabled(bool)));

    QAction *recordAction = toolbar->addAction(il.load("record"),
                                               tr("Record"));
    recordAction->setCheckable(true);
    recordAction->setShortcut(tr("Ctrl+Space"));
    recordAction->setStatusTip(tr("Record a new audio file. If a reference track is already loaded, the recording is added as the singing track alongside it."));
    connect(recordAction, &QAction::triggered,
            this, &MainWindow::recordPressed);
    m_recordAction = recordAction;
    connect(m_recordTarget, SIGNAL(recordStatusChanged(bool)),
	    recordAction, SLOT(setChecked(bool)));
    connect(m_recordTarget, SIGNAL(recordCompleted()),
	    this, SLOT(analyseNow()));
    connect(this, SIGNAL(canRecord(bool)),
            recordAction, SLOT(setEnabled(bool)));

    // The takes of the session, beside the recording controls: choosing
    // one shows it, with its audio, pitch track and notes (spec 5.3).
    // Making and deleting takes is the Takes menu
    {
        QLabel *takeLabel = new QLabel(tr(" Take:"));
        QFont f = takeLabel->font();
        f.setPointSize(f.pointSize() - 1);
        takeLabel->setFont(f);
        takeLabel->setEnabled(false); // greyed out — purely decorative
        toolbar->addWidget(takeLabel);
    }

    m_takeCombo = new QComboBox;
    m_takeCombo->setObjectName(tr("Take"));
    m_takeCombo->setToolTip(tr("The take that is shown and played: its "
                               "audio, its pitch track and its notes"));
    m_takeCombo->setMinimumContentsLength(8);
    m_takeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_takeCombo->setEnabled(false);
    connect(m_takeCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(takeChosenInCombo(int)));
    connect(this, SIGNAL(canChangeTakes(bool)),
            m_takeCombo, SLOT(setEnabled(bool)));
    toolbar->addWidget(m_takeCombo);

    toolbar = addToolBar(tr("Play Mode Toolbar"));

    QAction *psAction = toolbar->addAction(il.load("playselection"),
                                           tr("Constrain Playback to Selection"));
    psAction->setCheckable(true);
    psAction->setChecked(m_viewManager->getPlaySelectionMode());
    psAction->setShortcut(tr("s"));
    psAction->setStatusTip(tr("Constrain playback to the selected regions"));
    connect(m_viewManager, SIGNAL(playSelectionModeChanged(bool)),
            psAction, SLOT(setChecked(bool)));
    connect(psAction, SIGNAL(triggered()), this, SLOT(playSelectionToggled()));
    connect(this, SIGNAL(canPlaySelection(bool)), psAction, SLOT(setEnabled(bool)));

    QAction *plAction = toolbar->addAction(il.load("playloop"),
                                           tr("Loop Playback"));
    plAction->setCheckable(true);
    plAction->setChecked(m_viewManager->getPlayLoopMode());
    plAction->setShortcut(tr("l"));
    plAction->setStatusTip(tr("Loop playback"));
    connect(m_viewManager, SIGNAL(playLoopModeChanged(bool)),
            plAction, SLOT(setChecked(bool)));
    connect(plAction, SIGNAL(triggered()), this, SLOT(playLoopToggled()));
    connect(this, SIGNAL(canPlay(bool)), plAction, SLOT(setEnabled(bool)));

    QAction *oneLeftAction = new QAction(tr("&One Note Left"), this);
    oneLeftAction->setShortcut(tr("Ctrl+Left"));
    oneLeftAction->setStatusTip(tr("Move cursor to the preceding note (or silence) onset."));
    connect(oneLeftAction, SIGNAL(triggered()), this, SLOT(moveOneNoteLeft()));
    connect(this, SIGNAL(canScroll(bool)), oneLeftAction, SLOT(setEnabled(bool)));
    
    QAction *oneRightAction = new QAction(tr("O&ne Note Right"), this);
    oneRightAction->setShortcut(tr("Ctrl+Right"));
    oneRightAction->setStatusTip(tr("Move cursor to the succeeding note (or silence)."));
    connect(oneRightAction, SIGNAL(triggered()), this, SLOT(moveOneNoteRight()));
    connect(this, SIGNAL(canScroll(bool)), oneRightAction, SLOT(setEnabled(bool)));

    QAction *selectOneLeftAction = new QAction(tr("&Select One Note Left"), this);
    selectOneLeftAction->setShortcut(tr("Ctrl+Shift+Left"));
    selectOneLeftAction->setStatusTip(tr("Select to the preceding note (or silence) onset."));
    connect(selectOneLeftAction, SIGNAL(triggered()), this, SLOT(selectOneNoteLeft()));
    connect(this, SIGNAL(canScroll(bool)), selectOneLeftAction, SLOT(setEnabled(bool)));
    
    QAction *selectOneRightAction = new QAction(tr("S&elect One Note Right"), this);
    selectOneRightAction->setShortcut(tr("Ctrl+Shift+Right"));
    selectOneRightAction->setStatusTip(tr("Select to the succeeding note (or silence)."));
    connect(selectOneRightAction, SIGNAL(triggered()), this, SLOT(selectOneNoteRight()));
    connect(this, SIGNAL(canScroll(bool)), selectOneRightAction, SLOT(setEnabled(bool)));

    m_keyReference->registerShortcut(psAction);
    m_keyReference->registerShortcut(plAction);
    m_keyReference->registerShortcut(playAction);
    m_keyReference->registerShortcut(recordAction);
    m_keyReference->registerShortcut(m_rwdAction);
    m_keyReference->registerShortcut(m_ffwdAction);
    m_keyReference->registerShortcut(rwdStartAction);
    m_keyReference->registerShortcut(ffwdEndAction);
    m_keyReference->registerShortcut(recordAction);
    m_keyReference->registerShortcut(oneLeftAction);
    m_keyReference->registerShortcut(oneRightAction);
    m_keyReference->registerShortcut(selectOneLeftAction);
    m_keyReference->registerShortcut(selectOneRightAction);

    menu->addAction(playAction);
    menu->addAction(psAction);
    menu->addAction(plAction);
    menu->addSeparator();
    menu->addAction(m_rwdAction);
    menu->addAction(m_ffwdAction);
    menu->addSeparator();
    menu->addAction(rwdStartAction);
    menu->addAction(ffwdEndAction);
    menu->addSeparator();
    menu->addAction(oneLeftAction);
    menu->addAction(oneRightAction);
    menu->addAction(selectOneLeftAction);
    menu->addAction(selectOneRightAction);
    menu->addSeparator();
    menu->addAction(recordAction);
    menu->addSeparator();

    m_audioDeviceMenu = menu->addMenu(tr("Audio Output &Device"));
    m_audioDeviceMenu->setStatusTip(tr("Choose which device Tony plays through"));
    m_audioDeviceGroup = new QActionGroup(this);
    m_audioDeviceGroup->setExclusive(true);

    m_audioInputDeviceMenu = menu->addMenu(tr("Audio &Input Device"));
    m_audioInputDeviceMenu->setStatusTip(tr("Choose which device Tony records from"));
    m_audioInputDeviceGroup = new QActionGroup(this);
    m_audioInputDeviceGroup->setExclusive(true);

    for (QMenu *m: { m_audioDeviceMenu, m_audioInputDeviceMenu }) {
        connect(m, SIGNAL(aboutToShow()), this, SLOT(rescanAudioDevices()));
        // Placeholder so that the submenu is not empty: an empty submenu is
        // drawn disabled and never emits aboutToShow, which is where the
        // real device list is built.
        m->addAction(tr("(Scanning...)"))->setEnabled(false);
    }

    for (QActionGroup *g: { m_audioDeviceGroup, m_audioInputDeviceGroup }) {
        connect(g, SIGNAL(triggered(QAction *)),
                this, SLOT(audioDeviceSelected(QAction *)));
    }

    // The audio check, and the latency takes are placed with: a line to
    // read, never chosen, brought up to date whenever the menu opens
    m_calibrateAudioAction = menu->addAction(tr("&Calibrate Audio..."));
    m_calibrateAudioAction->setStatusTip
        (tr("Measure how late recordings arrive through these devices, with "
            "an earcup held against the microphone"));
    connect(m_calibrateAudioAction, &QAction::triggered,
            this, &MainWindow::calibrateAudio);

    m_latencyLineAction = menu->addAction(QString());
    m_latencyLineAction->setEnabled(false);

    m_forgetLatencyAction = menu->addAction(tr("&Forget Measured Latency"));
    m_forgetLatencyAction->setStatusTip
        (tr("Place takes on these devices with the latency the driver "
            "reports again"));
    connect(m_forgetLatencyAction, &QAction::triggered,
            this, [this]() { forgetMeasuredLatency(); });

    connect(menu, &QMenu::aboutToShow,
            this, &MainWindow::updateLatencyMenuLine);
    updateLatencyMenuLine();
    menu->addSeparator();

    m_rightButtonPlaybackMenu->addAction(playAction);
    m_rightButtonPlaybackMenu->addAction(psAction);
    m_rightButtonPlaybackMenu->addAction(plAction);
    m_rightButtonPlaybackMenu->addSeparator();
    m_rightButtonPlaybackMenu->addAction(m_rwdAction);
    m_rightButtonPlaybackMenu->addAction(m_ffwdAction);
    m_rightButtonPlaybackMenu->addSeparator();
    m_rightButtonPlaybackMenu->addAction(rwdStartAction);
    m_rightButtonPlaybackMenu->addAction(ffwdEndAction);
    m_rightButtonPlaybackMenu->addSeparator();
    m_rightButtonPlaybackMenu->addAction(oneLeftAction);
    m_rightButtonPlaybackMenu->addAction(oneRightAction);
    m_rightButtonPlaybackMenu->addAction(selectOneLeftAction);
    m_rightButtonPlaybackMenu->addAction(selectOneRightAction);
    m_rightButtonPlaybackMenu->addSeparator();
    m_rightButtonPlaybackMenu->addAction(recordAction);
    m_rightButtonPlaybackMenu->addSeparator();

    QAction *fastAction = menu->addAction(tr("Speed Up"));
    fastAction->setShortcut(tr("Ctrl+PgUp"));
    fastAction->setStatusTip(tr("Time-stretch playback to speed it up without changing pitch"));
    connect(fastAction, SIGNAL(triggered()), this, SLOT(speedUpPlayback()));
    connect(this, SIGNAL(canSpeedUpPlayback(bool)), fastAction, SLOT(setEnabled(bool)));
    
    QAction *slowAction = menu->addAction(tr("Slow Down"));
    slowAction->setShortcut(tr("Ctrl+PgDown"));
    slowAction->setStatusTip(tr("Time-stretch playback to slow it down without changing pitch"));
    connect(slowAction, SIGNAL(triggered()), this, SLOT(slowDownPlayback()));
    connect(this, SIGNAL(canSlowDownPlayback(bool)), slowAction, SLOT(setEnabled(bool)));

    QAction *normalAction = menu->addAction(tr("Restore Normal Speed"));
    normalAction->setShortcut(tr("Ctrl+Home"));
    normalAction->setStatusTip(tr("Restore non-time-stretched playback"));
    connect(normalAction, SIGNAL(triggered()), this, SLOT(restoreNormalPlayback()));
    connect(this, SIGNAL(canChangePlaybackSpeed(bool)), normalAction, SLOT(setEnabled(bool)));

    m_keyReference->registerShortcut(fastAction);
    m_keyReference->registerShortcut(slowAction);
    m_keyReference->registerShortcut(normalAction);

    m_rightButtonPlaybackMenu->addAction(fastAction);
    m_rightButtonPlaybackMenu->addAction(slowAction);
    m_rightButtonPlaybackMenu->addAction(normalAction);

    toolbar = new QToolBar(tr("Playback Controls"));
    addToolBar(Qt::BottomToolBarArea, toolbar);

    toolbar->addWidget(m_playSpeed);
    toolbar->addWidget(m_fader);

    toolbar = addToolBar(tr("Show and Play"));
    addToolBar(Qt::BottomToolBarArea, toolbar);

    // "Reference:" label before the reference-track button group
    {
        QLabel *refLabel = new QLabel(tr(" Reference:"));
        QFont f = refLabel->font();
        f.setPointSize(f.pointSize() - 1);
        refLabel->setFont(f);
        refLabel->setEnabled(false); // greyed out — purely decorative
        toolbar->addWidget(refLabel);
    }

    m_showAudio = toolbar->addAction(il.load("waveform"), tr("Show Audio"));
    m_showAudio->setCheckable(true);
    connect(m_showAudio, SIGNAL(triggered()), this, SLOT(showAudioToggled()));
    connect(this, SIGNAL(canPlay(bool)), m_showAudio, SLOT(setEnabled(bool)));

    m_playAudio = toolbar->addAction(il.load("speaker"), tr("Play Audio"));
    m_playAudio->setCheckable(true);
    connect(m_playAudio, SIGNAL(triggered()), this, SLOT(playAudioToggled()));
    connect(this, SIGNAL(canPlayWaveform(bool)), m_playAudio, SLOT(setEnabled(bool)));

    int lpwSize, bigLpwSize;
#ifdef Q_OS_MAC
    lpwSize = m_viewManager->scalePixelSize(32); // Mac toolbars are fatter
    bigLpwSize = int(lpwSize * 2.2);
#else
    lpwSize = m_viewManager->scalePixelSize(26);
    bigLpwSize = int(lpwSize * 2.8);
#endif
    
    m_audioLPW->setImageSize(lpwSize);
    m_audioLPW->setBigImageSize(bigLpwSize);
    toolbar->addWidget(m_audioLPW);

    // Pitch (f0)
    QLabel *spacer = new QLabel; // blank
    spacer->setFixedWidth(m_viewManager->scalePixelSize(30));
    toolbar->addWidget(spacer);

    m_showPitch = toolbar->addAction(il.load("values"), tr("Show Pitch Track"));
    m_showPitch->setCheckable(true);
    connect(m_showPitch, SIGNAL(triggered()), this, SLOT(showPitchToggled()));
    connect(this, SIGNAL(canPlay(bool)), m_showPitch, SLOT(setEnabled(bool)));

    if (m_withSonification) {
        m_playPitch = toolbar->addAction(il.load("speaker"), tr("Play Pitch Track"));
        m_playPitch->setCheckable(true);
        connect(m_playPitch, SIGNAL(triggered()), this, SLOT(playPitchToggled()));
        connect(this, SIGNAL(canPlayPitch(bool)), m_playPitch, SLOT(setEnabled(bool)));

        m_pitchLPW->setImageSize(lpwSize);
        m_pitchLPW->setBigImageSize(bigLpwSize);
        toolbar->addWidget(m_pitchLPW);
    } else {
        m_playPitch = 0;
    }

    // Notes
    spacer = new QLabel;
    spacer->setFixedWidth(m_viewManager->scalePixelSize(30));
    toolbar->addWidget(spacer);

    m_showNotes = toolbar->addAction(il.load("notes"), tr("Show Notes"));
    m_showNotes->setCheckable(true);
    connect(m_showNotes, SIGNAL(triggered()), this, SLOT(showNotesToggled()));
    connect(this, SIGNAL(canPlay(bool)), m_showNotes, SLOT(setEnabled(bool)));

    if (m_withSonification) {
        m_playNotes = toolbar->addAction(il.load("speaker"), tr("Play Notes"));
        m_playNotes->setCheckable(true);
        connect(m_playNotes, SIGNAL(triggered()), this, SLOT(playNotesToggled()));
        connect(this, SIGNAL(canPlayNotes(bool)), m_playNotes, SLOT(setEnabled(bool)));

        m_notesLPW->setImageSize(lpwSize);
        m_notesLPW->setBigImageSize(bigLpwSize);
        toolbar->addWidget(m_notesLPW);
    } else {
        m_playNotes = 0;
    }

    // Singing track pitch track — preceded by a "Singing:" section label
    spacer = new QLabel;
    spacer->setFixedWidth(m_viewManager->scalePixelSize(30));
    toolbar->addWidget(spacer);

    {
        QLabel *singLabel = new QLabel(tr("Singing:"));
        QFont f = singLabel->font();
        f.setPointSize(f.pointSize() - 1);
        singLabel->setFont(f);
        singLabel->setEnabled(false); // greyed out — purely decorative
        toolbar->addWidget(singLabel);
    }

    // Use the "record" (microphone) icon to visually distinguish this from
    // the reference pitch button which uses "values".
    m_showSingingPitch = toolbar->addAction(il.load("record"), tr("Show Singing Pitch Track"));
    m_showSingingPitch->setCheckable(true);
    m_showSingingPitch->setToolTip(tr("Show/hide the singing track pitch (orange)"));
    connect(m_showSingingPitch, &QAction::triggered, this, [this](bool checked) {
        if (m_analyser2) {
            m_analyser2->setVisible(Analyser::PitchTrack, checked);
        }
        if (m_realtimePitchLayer) {
            m_realtimePitchLayer->showLayer(m_paneStack->getPane(0), checked);
        }
    });
    connect(this, SIGNAL(canShowRealtimePitch(bool)), m_showSingingPitch, SLOT(setEnabled(bool)));
    m_showSingingPitch->setEnabled(false);

    // Singing track notes
    spacer = new QLabel;
    spacer->setFixedWidth(m_viewManager->scalePixelSize(10));
    toolbar->addWidget(spacer);

    m_showSingingNotes = toolbar->addAction(il.load("notes"), tr("Show Singing Notes"));
    m_showSingingNotes->setCheckable(true);
    m_showSingingNotes->setToolTip(tr("Show/hide the singing track notes (purple)"));
    connect(m_showSingingNotes, &QAction::triggered, this, [this](bool checked) {
        if (m_analyser2) {
            m_analyser2->setVisible(Analyser::Notes, checked);
        }
    });
    connect(this, SIGNAL(canShowRealtimePitch(bool)), m_showSingingNotes, SLOT(setEnabled(bool)));
    m_showSingingNotes->setEnabled(false);

    // Singing track audio playback toggle
    spacer = new QLabel;
    spacer->setFixedWidth(m_viewManager->scalePixelSize(10));
    toolbar->addWidget(spacer);

    m_playSingingAudio = toolbar->addAction(il.load("speaker"), tr("Play Singing Audio"));
    m_playSingingAudio->setCheckable(true);
    m_playSingingAudio->setChecked(true);
    m_playSingingAudio->setToolTip(tr("Enable/disable playback of the recorded singing audio"));
    connect(m_playSingingAudio, SIGNAL(triggered()), this, SLOT(playSingingAudioToggled()));
    connect(this, SIGNAL(canShowRealtimePitch(bool)), m_playSingingAudio, SLOT(setEnabled(bool)));
    m_playSingingAudio->setEnabled(false);

    // Play reference track while recording — lets the singer hear the
    // reference audio through headphones to time their performance.
    spacer = new QLabel;
    spacer->setFixedWidth(m_viewManager->scalePixelSize(30));
    toolbar->addWidget(spacer);

    {
        QLabel *recLabel = new QLabel(tr("While recording:"));
        QFont f = recLabel->font();
        f.setPointSize(f.pointSize() - 1);
        recLabel->setFont(f);
        recLabel->setEnabled(false);
        toolbar->addWidget(recLabel);
    }

    m_playRefWhileRecording = toolbar->addAction(il.load("speaker"),
                                                  tr("Play Reference While Recording"));
    m_playRefWhileRecording->setCheckable(true);
    {
        QSettings settings;
        settings.beginGroup("MainWindow");
        m_playRefWhileRecording->setChecked(
            settings.value("playrefwhilerecording", false).toBool());
        settings.endGroup();
    }
    m_playRefWhileRecording->setToolTip(
        tr("Play the reference track through speakers/headphones during recording "
           "so you can time your singing against it"));
    connect(m_playRefWhileRecording, &QAction::toggled, this, [this](bool on) {
        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.setValue("playrefwhilerecording", on);
        settings.endGroup();
    });
    connect(this, SIGNAL(canPlay(bool)), m_playRefWhileRecording, SLOT(setEnabled(bool)));

    // Pre-roll: a lead-in before the take's position, heard but not
    // recorded over, so the singer comes in in time.  Its length is the
    // QSettings value MainWindow/prerollseconds, with no UI to change it.
    m_preRoll = toolbar->addAction(il.load("rewind"), tr("Pre-roll"));
    m_preRoll->setCheckable(true);
    {
        QSettings settings;
        settings.beginGroup("MainWindow");
        m_preRoll->setChecked(settings.value("preroll", false).toBool());
        settings.endGroup();
    }
    m_preRoll->setToolTip(
        tr("Play the reference from a few seconds before the recording "
           "position, counting down, so that you can come in in time. "
           "Nothing before the position is recorded over."));
    connect(m_preRoll, &QAction::toggled, this, [this](bool on) {
        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.setValue("preroll", on);
        settings.endGroup();
    });
    connect(this, SIGNAL(canPlay(bool)), m_preRoll, SLOT(setEnabled(bool)));

    // Punch in and out: record the selected range and nothing else.
    // The selection says where the take starts and where it stops, so
    // the singer need not reach for Stop.
    m_recordIntoSelection = toolbar->addAction(il.load("playselection"),
                                               tr("Record into Selection"));
    m_recordIntoSelection->setCheckable(true);
    {
        QSettings settings;
        settings.beginGroup("MainWindow");
        m_recordIntoSelection->setChecked(
            settings.value("recordintoselection", false).toBool());
        settings.endGroup();
    }
    m_recordIntoSelection->setToolTip(
        tr("Record into the selected range only: recording starts at the "
           "start of the selection, whatever the playhead says, and stops "
           "by itself at its end"));
    connect(m_recordIntoSelection, &QAction::toggled, this, [this](bool on) {
        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.setValue("recordintoselection", on);
        settings.endGroup();
    });
    connect(this, SIGNAL(canPlay(bool)), m_recordIntoSelection,
            SLOT(setEnabled(bool)));

    // The alternate pitch track: the reference pitch an octave or more
    // up or down, for the singer to follow in place of the reference.
    spacer = new QLabel;
    spacer->setFixedWidth(m_viewManager->scalePixelSize(30));
    toolbar->addWidget(spacer);

    {
        QLabel *followLabel = new QLabel(tr("Follow:"));
        QFont f = followLabel->font();
        f.setPointSize(f.pointSize() - 1);
        followLabel->setFont(f);
        followLabel->setEnabled(false);
        toolbar->addWidget(followLabel);
    }

    m_showAlternatePitch = toolbar->addAction(il.load("values"),
                                              tr("Alternate Pitch Track"));
    m_showAlternatePitch->setCheckable(true);
    connect(m_showAlternatePitch, SIGNAL(triggered()),
            this, SLOT(alternatePitchToggled()));

    // No icons for these; the toolbar shows the text
    m_alternatePitchDownAction = toolbar->addAction(tr("8vb"));
    m_alternatePitchDownAction->setToolTip
        (tr("Move the alternate pitch track down an octave"));
    m_alternatePitchDownAction->setStatusTip
        (tr("Move the alternate pitch track down an octave"));
    connect(m_alternatePitchDownAction, SIGNAL(triggered()),
            this, SLOT(alternatePitchDown()));

    m_alternatePitchUpAction = toolbar->addAction(tr("8va"));
    m_alternatePitchUpAction->setToolTip
        (tr("Move the alternate pitch track up an octave"));
    m_alternatePitchUpAction->setStatusTip
        (tr("Move the alternate pitch track up an octave"));
    connect(m_alternatePitchUpAction, SIGNAL(triggered()),
            this, SLOT(alternatePitchUp()));

    // Background music section: an additional audio track that plays
    // alongside the reference track but is never analysed.
    spacer = new QLabel;
    spacer->setFixedWidth(m_viewManager->scalePixelSize(30));
    toolbar->addWidget(spacer);

    {
        QLabel *bgLabel = new QLabel(tr("Background:"));
        QFont f = bgLabel->font();
        f.setPointSize(f.pointSize() - 1);
        bgLabel->setFont(f);
        bgLabel->setEnabled(false);
        toolbar->addWidget(bgLabel);
    }

    m_playBackgroundMusic = toolbar->addAction(il.load("speaker"), tr("Mix Background Music"));
    m_playBackgroundMusic->setCheckable(true);
    m_playBackgroundMusic->setChecked(true);
    m_playBackgroundMusic->setToolTip(
        tr("Enable/disable mixing the background music track during playback and recording"));
    m_playBackgroundMusic->setEnabled(false);
    connect(m_playBackgroundMusic, SIGNAL(triggered()), this, SLOT(backgroundMusicToggled()));

    m_bgMusicLPW->setImageSize(lpwSize);
    m_bgMusicLPW->setBigImageSize(bigLpwSize);
    m_bgMusicLPW->setEnabled(false);
    toolbar->addWidget(m_bgMusicLPW);

    // Spectrogram
    spacer = new QLabel;
    spacer->setFixedWidth(m_viewManager->scalePixelSize(30));
    toolbar->addWidget(spacer);

    if (!m_withSpectrogram)
    {
        m_showSpect = new QAction(tr("Show Spectrogram"), this);
    } else {
        m_showSpect = toolbar->addAction(il.load("spectrogram"), tr("Show Spectrogram"));
    }
    m_showSpect->setCheckable(true);
    connect(m_showSpect, SIGNAL(triggered()), this, SLOT(showSpectToggled()));
    connect(this, SIGNAL(canPlay(bool)), m_showSpect, SLOT(setEnabled(bool)));

    Pane::registerShortcuts(*m_keyReference);

    updateLayerStatuses();
    
//    QTimer::singleShot(500, this, SLOT(betaReleaseWarning()));
}


void
MainWindow::moveOneNoteRight()
{
    // cerr << "MainWindow::moveOneNoteRight" << endl;
    moveByOneNote(true, false);
}

void
MainWindow::moveOneNoteLeft()
{
    // cerr << "MainWindow::moveOneNoteLeft" << endl;
    moveByOneNote(false, false);
}

void
MainWindow::selectOneNoteRight()
{
    moveByOneNote(true, true);
}

void
MainWindow::selectOneNoteLeft()
{
    moveByOneNote(false, true);
}


void
MainWindow::moveByOneNote(bool right, bool doSelect)
{
    sv_frame_t frame = m_viewManager->getPlaybackFrame();
    cerr << "MainWindow::moveByOneNote startframe: " << frame << endl;
    
    bool isAtSelectionBoundary = false;
    MultiSelection::SelectionList selections = m_viewManager->getSelections();
    if (!selections.empty()) {
        Selection sel = *selections.begin();
        isAtSelectionBoundary = (frame == sel.getStartFrame()) || (frame == sel.getEndFrame());
    }
    if (!doSelect || !isAtSelectionBoundary) {
        m_selectionAnchor = frame;
    }

    Layer *layer = m_analyser->getLayer(Analyser::Notes);
    if (!layer) return;

    auto model = ModelById::getAs<NoteModel>(layer->getModel());
    if (!model) return;

    //!!! This seems like a strange and inefficient way to do this -
    //!!! there is almost certainly a better way making use of
    //!!! EventSeries api
    
    EventVector points = model->getAllEvents();
    if (points.empty()) return;

    EventVector::iterator i = points.begin();
    std::set<sv_frame_t> snapFrames;
    snapFrames.insert(0);
    while (i != points.end()) {
        snapFrames.insert(i->getFrame());
        snapFrames.insert(i->getFrame() + i->getDuration() + 1);
        ++i;
    }
    std::set<sv_frame_t>::iterator i2;
    if (snapFrames.find(frame) == snapFrames.end()) {
        // we're not on an existing snap point, so go to previous
        snapFrames.insert(frame);
    }
    i2 = snapFrames.find(frame);
    if (right) {
        i2++;
        if (i2 == snapFrames.end()) i2--;
    } else {
        if (i2 != snapFrames.begin()) i2--;
    }
    frame = *i2;
    m_viewManager->setPlaybackFrame(frame);
    if (doSelect) {
        Selection sel;
        if (frame > m_selectionAnchor) {
            sel = Selection(m_selectionAnchor, frame);
        } else {
            sel = Selection(frame, m_selectionAnchor);
        }
        m_viewManager->setSelection(sel);
    }
    cerr << "MainWindow::moveByOneNote endframe: " << frame << endl;
}

void
MainWindow::toolNavigateSelected()
{
    m_viewManager->setToolMode(ViewManager::NavigateMode);
    m_intelligentActionOn = true;
}

void
MainWindow::toolEditSelected()
{
    cerr << "MainWindow::toolEditSelected" << endl;
    m_viewManager->setToolMode(ViewManager::NoteEditMode);
    m_intelligentActionOn = true;
    m_analyser->setIntelligentActions(m_intelligentActionOn);
}

void
MainWindow::toolFreeEditSelected()
{
    m_viewManager->setToolMode(ViewManager::NoteEditMode);
    m_intelligentActionOn = false;
    m_analyser->setIntelligentActions(m_intelligentActionOn);
}

void
MainWindow::updateMenuStates()
{
    MainWindowBase::updateMenuStates();

    Pane *currentPane = 0;
    Layer *currentLayer = 0;

    if (m_paneStack) currentPane = m_paneStack->getCurrentPane();
    if (currentPane) currentLayer = currentPane->getSelectedLayer();

    bool haveMainModel =
	(getMainModel() != 0);
    bool havePlayTarget =
	(m_playTarget != 0 || m_audioIO != 0);
    bool haveCurrentPane =
        (currentPane != 0);
    bool haveCurrentLayer =
        (haveCurrentPane &&
         (currentLayer != 0));
    bool haveSelection = 
        (m_viewManager &&
         !m_viewManager->getSelections().empty());
    bool haveCurrentTimeInstantsLayer = 
        (haveCurrentLayer &&
         qobject_cast<TimeInstantLayer *>(currentLayer));
    bool haveCurrentTimeValueLayer = 
        (haveCurrentLayer &&
         qobject_cast<TimeValueLayer *>(currentLayer));
    bool pitchCandidatesVisible = 
        m_analyser->arePitchCandidatesShown();

    emit canChangePlaybackSpeed(true);
    int v = m_playSpeed->value();
    emit canSpeedUpPlayback(v < m_playSpeed->maximum());
    emit canSlowDownPlayback(v > m_playSpeed->minimum());

    bool haveWaveform =
        m_analyser->isVisible(Analyser::Audio) &&
        m_analyser->getLayer(Analyser::Audio);

    bool havePitchTrack = 
        m_analyser->isVisible(Analyser::PitchTrack) &&
        m_analyser->getLayer(Analyser::PitchTrack);

    bool haveNotes = 
        m_analyser->isVisible(Analyser::Notes) &&
        m_analyser->getLayer(Analyser::Notes);

    emit canExportPitchTrack(havePitchTrack);
    emit canExportNotes(haveNotes);
    emit canSnapNotes(haveSelection && haveNotes);

    emit canPlayWaveform(haveWaveform && haveMainModel && havePlayTarget);
    emit canPlayPitch(havePitchTrack && haveMainModel && havePlayTarget);
    emit canPlayNotes(haveNotes && haveMainModel && havePlayTarget);

    // Enable singing-track toolbar buttons whenever a singing track analyser
    // or a realtime (recording) pitch layer is active.
    bool haveSinging = (m_analyser2 != nullptr) || (m_realtimePitchLayer != nullptr);
    emit canShowRealtimePitch(haveSinging);

    // Editing the singing of a take: there has to be a take with
    // something recorded in it, and no take being recorded just now.
    // Erasing needs a selection to erase as well, and waits for the
    // analysis of a recorded range: erasing swaps the take's audio,
    // which would throw that analysis's result away (see
    // eraseSingingInSelection())
    bool inTake = (m_recordTarget && m_recordTarget->isRecording());
    bool haveCoverage = m_takes && m_takes->haveTake() &&
        !m_takes->getCoverage().isEmpty();
    bool analysingRange = (m_analyser2 && m_analyser2->isAnalysingRange());
    emit canSelectRecording(haveCoverage && !inTake);
    emit canEraseSinging(haveCoverage && !inTake && haveSelection &&
                         !analysingRange);

    // Nor can playback be constrained to the selection during a take:
    // see liftPlaySelectionForTake()
    if (inTake) emit canPlaySelection(false);

    // The takes of the session: switching and making one need a session
    // and nothing running, and the rest need a take to act on as well
    bool canChange = takeOperationsAllowed();
    emit canChangeTakes(canChange);
    emit canActOnTake(canChange && m_takes->getActiveIndex() >= 0);

    // The audio check records takes of its own, and keeps what it
    // measures for the devices it started on.  Record is shut after the
    // base class has opened it: a press would stop the check's take, or
    // record one of the user's into the check's session
    bool checking = audioCheckRunning();
    if (checking) emit canRecord(false);
    if (m_calibrateAudioAction) {
        m_calibrateAudioAction->setEnabled(!inTake && !checking);
    }
    for (QMenu *m : { m_audioDeviceMenu, m_audioInputDeviceMenu }) {
        if (m) m->menuAction()->setEnabled(!checking);
    }
    updateLatencyMenuLine();

    if (pitchCandidatesVisible) {
        m_showCandidatesAction->setText(tr("Hide Pitch Candidates"));
        m_showCandidatesAction->setStatusTip(tr("Remove the display of alternate pitch candidates for the selected region"));
    } else {
        m_showCandidatesAction->setText(tr("Show Pitch Candidates"));
        m_showCandidatesAction->setStatusTip(tr("Show alternate pitch candidates for the selected region"));
    }

    if (m_ffwdAction && m_rwdAction) {
        if (haveCurrentTimeInstantsLayer) {
            m_ffwdAction->setText(tr("Fast Forward to Next Instant"));
            m_ffwdAction->setStatusTip(tr("Fast forward to the next time instant in the current layer"));
            m_rwdAction->setText(tr("Rewind to Previous Instant"));
            m_rwdAction->setStatusTip(tr("Rewind to the previous time instant in the current layer"));
        } else if (haveCurrentTimeValueLayer) {
            m_ffwdAction->setText(tr("Fast Forward to Next Point"));
            m_ffwdAction->setStatusTip(tr("Fast forward to the next point in the current layer"));
            m_rwdAction->setText(tr("Rewind to Previous Point"));
            m_rwdAction->setStatusTip(tr("Rewind to the previous point in the current layer"));
        } else {
            m_ffwdAction->setText(tr("Fast Forward"));
            m_ffwdAction->setStatusTip(tr("Fast forward"));
            m_rwdAction->setText(tr("Rewind"));
            m_rwdAction->setStatusTip(tr("Rewind"));
        }
    }
}

void
MainWindow::showAudioToggled()
{
    m_analyser->toggleVisible(Analyser::Audio);

    QSettings settings;
    settings.beginGroup("MainWindow");

    bool playOn = false;
    if (m_analyser->isVisible(Analyser::Audio)) {
        // just switched layer on; check whether playback was also on previously
        playOn = settings.value("playaudiowas", true).toBool();
    } else {
        settings.setValue("playaudiowas", m_playAudio->isChecked());
    }
    m_analyser->setAudible(Analyser::Audio, playOn);

    settings.endGroup();

    updateMenuStates();
    updateLayerStatuses();
}

void
MainWindow::showPitchToggled()
{
    m_analyser->toggleVisible(Analyser::PitchTrack);

    QSettings settings;
    settings.beginGroup("MainWindow");

    bool playOn = false;
    if (m_analyser->isVisible(Analyser::PitchTrack)) {
        // just switched layer on; check whether playback was also on previously
        playOn = settings.value("playpitchwas", true).toBool();
    } else {
        settings.setValue("playpitchwas", m_playPitch->isChecked());
    }
    m_analyser->setAudible(Analyser::PitchTrack, playOn);

    settings.endGroup();

    updateMenuStates();
    updateLayerStatuses();
}

void
MainWindow::showSpectToggled()
{
    m_analyser->toggleVisible(Analyser::Spectrogram);
}

void
MainWindow::showNotesToggled()
{
    m_analyser->toggleVisible(Analyser::Notes);

    QSettings settings;
    settings.beginGroup("MainWindow");

    bool playOn = false;
    if (m_analyser->isVisible(Analyser::Notes)) {
        // just switched layer on; check whether playback was also on previously
        playOn = settings.value("playnoteswas", true).toBool();
    } else {
        settings.setValue("playnoteswas", m_playNotes->isChecked());
    }
    m_analyser->setAudible(Analyser::Notes, playOn);

    settings.endGroup();

    updateMenuStates();
    updateLayerStatuses();
}

void
MainWindow::playAudioToggled()
{
    m_analyser->toggleAudible(Analyser::Audio);
    updateLayerStatuses();
}

void
MainWindow::playPitchToggled()
{
    m_analyser->toggleAudible(Analyser::PitchTrack);
    updateLayerStatuses();
}

void
MainWindow::playNotesToggled()
{
    m_analyser->toggleAudible(Analyser::Notes);
    updateLayerStatuses();
}

void
MainWindow::playSingingAudioToggled()
{
    if (m_singingAudioMutedForTake) {
        // Muted whatever the button says; it takes effect after the take
        m_singingAudioAfterTake = !m_singingAudioAfterTake;
    } else if (m_analyser2) {
        m_analyser2->toggleAudible(Analyser::Audio);
    }
    updateLayerStatuses();
}

void
MainWindow::updateLayerStatuses()
{
    m_showAudio->setChecked(m_analyser->isVisible(Analyser::Audio));
    m_playAudio->setChecked(m_analyser->isAudible(Analyser::Audio));
    m_audioLPW->setEnabled(m_analyser->isAudible(Analyser::Audio));
    m_audioLPW->setLevel(m_analyser->getGain(Analyser::Audio));
    m_audioLPW->setPan(m_analyser->getPan(Analyser::Audio));
    
    m_showPitch->setChecked(m_analyser->isVisible(Analyser::PitchTrack));
    m_playPitch->setChecked(m_analyser->isAudible(Analyser::PitchTrack));
    m_pitchLPW->setEnabled(m_analyser->isAudible(Analyser::PitchTrack));
    m_pitchLPW->setLevel(m_analyser->getGain(Analyser::PitchTrack));
    m_pitchLPW->setPan(m_analyser->getPan(Analyser::PitchTrack));

    m_showNotes->setChecked(m_analyser->isVisible(Analyser::Notes));
    m_playNotes->setChecked(m_analyser->isAudible(Analyser::Notes));
    m_notesLPW->setEnabled(m_analyser->isAudible(Analyser::Notes));
    m_notesLPW->setLevel(m_analyser->getGain(Analyser::Notes));
    m_notesLPW->setPan(m_analyser->getPan(Analyser::Notes));

    m_showSpect->setChecked(m_analyser->isVisible(Analyser::Spectrogram));

    // Singing track controls: enabled when either a second analyser
    // (post-recording full analysis) or the realtime pitch layer is active
    bool haveSingingTrack = (m_analyser2 != nullptr) || (m_realtimePitchLayer != nullptr);

    if (m_showSingingPitch) {
        m_showSingingPitch->setEnabled(haveSingingTrack);
        if (m_analyser2) {
            m_showSingingPitch->setChecked(m_analyser2->isVisible(Analyser::PitchTrack));
        } else if (m_realtimePitchLayer && m_paneStack && m_paneStack->getPaneCount() > 0) {
            m_showSingingPitch->setChecked(
                !m_realtimePitchLayer->isLayerDormant(m_paneStack->getPane(0)));
        } else {
            m_showSingingPitch->setChecked(false);
        }
    }

    if (m_showSingingNotes) {
        m_showSingingNotes->setEnabled(m_analyser2 != nullptr);
        if (m_analyser2) {
            m_showSingingNotes->setChecked(m_analyser2->isVisible(Analyser::Notes));
        } else {
            m_showSingingNotes->setChecked(false);
        }
    }

    if (m_playSingingAudio) {
        m_playSingingAudio->setEnabled(m_analyser2 != nullptr);
        if (m_singingAudioMutedForTake) {
            m_playSingingAudio->setChecked(m_singingAudioAfterTake);
        } else if (m_analyser2) {
            m_playSingingAudio->setChecked(m_analyser2->isAudible(Analyser::Audio));
        } else {
            m_playSingingAudio->setChecked(true); // default on when track arrives
        }
    }

    // Alternate pitch track: there to be had once there is a reference.
    // No moving it during a take, when the singer is following it
    if (m_showAlternatePitch) {
        bool haveReference = (m_document && getMainModel() &&
                              m_paneStack && m_paneStack->getPaneCount() > 0);
        bool shown = m_alternatePitch->isShown();
        bool inTake = (m_recordTarget && m_recordTarget->isRecording());
        m_showAlternatePitch->setEnabled(haveReference && !inTake);
        m_showAlternatePitch->setChecked(shown);
        QString tip = tr("Show a copy of the reference pitch track %1 (brown), and follow that when recording")
            .arg(AlternatePitchTrack::describe(m_alternatePitch->getOctaves()));
        m_showAlternatePitch->setToolTip(tip);
        m_showAlternatePitch->setStatusTip(tip);
        m_alternatePitchUpAction->setEnabled
            (shown && !inTake && m_alternatePitch->canStep(true));
        m_alternatePitchDownAction->setEnabled
            (shown && !inTake && m_alternatePitch->canStep(false));
    }

    // Background music toggle: enabled when a background music track is loaded
    if (m_playBackgroundMusic) {
        bool haveBgMusic = (m_backgroundMusicLayer != nullptr);
        m_playBackgroundMusic->setEnabled(haveBgMusic);
        if (m_bgMusicLPW) m_bgMusicLPW->setEnabled(haveBgMusic);
        if (haveBgMusic) {
            auto params = m_backgroundMusicLayer->getPlayParameters();
            bool audible = params ? params->isPlayAudible() : true;
            m_playBackgroundMusic->setChecked(audible);
            if (m_bgMusicLPW) {
                m_bgMusicLPW->setEnabled(audible);
                m_bgMusicLPW->setLevel(params ? params->getPlayGain() : 1.f);
                m_bgMusicLPW->setPan(params ? params->getPlayPan() : 0.f);
            }
        } else {
            m_playBackgroundMusic->setChecked(true); // default on when track arrives
        }
    }
}

void
MainWindow::editDisplayExtents()
{
    double min, max;
    double vmin = 0;
    double vmax = getMainModel()->getSampleRate() /2;
    
    if (!m_analyser->getDisplayFrequencyExtents(min, max)) {
        //!!!
        return;
    }

    RangeInputDialog dialog(tr("Set frequency range"),
                            tr("Enter new frequency range, from %1 to %2 Hz.\nThese values will be rounded to the nearest spectrogram bin.")
                            .arg(vmin).arg(vmax),
                            "Hz", float(vmin), float(vmax), this);
    dialog.setRange(float(min), float(max));

    if (dialog.exec() == QDialog::Accepted) {
        float fmin, fmax;
        dialog.getRange(fmin, fmax);
        min = fmin;
        max = fmax;
        if (min > max) {
            double tmp = max;
            max = min;
            min = tmp;
        }
        m_analyser->setDisplayFrequencyExtents(min, max);
    }
}

void
MainWindow::updateDescriptionLabel()
{
    // Nothing, we don't have one
}

void
MainWindow::documentModified()
{
    MainWindowBase::documentModified();
}

void
MainWindow::documentRestored()
{
    MainWindowBase::documentRestored();
}

void
MainWindow::newSession()
{
    if (!checkSaveModified()) return;

    closeSession();
    createDocument();
    m_document->setAutoAlignment(true);

    Pane *pane = m_paneStack->addPane();
    pane->setPlaybackFollow(PlaybackScrollPage);

    m_viewManager->setGlobalCentreFrame
        (pane->getFrameForX(width() / 2));
    
    connect(pane, SIGNAL(contextHelpChanged(const QString &)),
            this, SLOT(contextHelpChanged(const QString &)));

//    Layer *waveform = m_document->createMainModelLayer(LayerFactory::Waveform);
//    m_document->addLayerToView(pane, waveform);

    m_overview->registerView(pane);

    CommandHistory::getInstance()->clear();
    CommandHistory::getInstance()->documentSaved();
    documentRestored();
    updateMenuStates();
}

void
MainWindow::documentReplaced()
{
    if (m_document) {
        connect(m_document, SIGNAL(activity(QString)),
                m_activityLog, SLOT(activityHappened(QString)));
    }
}

void
MainWindow::closeSession()
{
    if (!checkSaveModified()) return;

    // A check has nothing left to record into; a take of its own that is
    // running is stopped through the Stop path, as the check's Cancel does.
    // The runner first: one that is replacing the session for the dev
    // checks carries on, and the dev checks see it running and carry on
    // with it, as they do through a reopen of their own
    if (m_audioCheck) m_audioCheck->sessionClosing();
#ifdef TONY_DEV_CHECKS
    if (m_devChecks) m_devChecks->sessionClosing();
#endif

    // Nothing of a take that is still running outlives its session
    stopTakePolling();

    // Tear down singing track and realtime pitch layer before panes/document
    // are destroyed, so they can cleanly remove their layers from the pane.
    teardownRealtimePitchLayer();
    teardownRecordingLayer();
    teardownSingingTrackAnalyser();
    teardownBackgroundMusic();
    m_alternatePitch->hide();
    m_coverageStrip->hide();
    m_referencePitchHiddenForTake = false;
    m_singingPitchHiddenForTake = false;
    m_singingNotesHiddenForTake = false;
    m_pendingSingingModelId = {};
    m_currentRecordingModelId = {};
    m_recordingAsSingingTrack = false;
    m_singingAudioMutedForTake = false;
    restorePlaySelectionAfterTake();
    m_analysedMainModelId = {};

    // Nothing is left waiting for a merge, and the history that holds the
    // commands is cleared at the end of this function
    m_openTakeCommand = nullptr;

    // The takes of the session go with it, and so do the audio files this
    // run wrote for them that nothing refers to any more (spec 5.4).  Not
    // the file the take is in, even in a session that was never saved: it
    // is the only copy of the singing apart from the raw recordings.  Not
    // a file the user brought either -- only what Tony itself wrote
    QStringList gone = m_takes->removeUnusedFiles();
    if (!gone.isEmpty()) {
        cerr << "MainWindow::closeSession: deleted " << gone.size()
             << " superseded take audio file(s)" << endl;
    }

    // A takes folder this run made and that has nothing left in it goes as
    // well (spec 6.4).  One with anything at all in it stays: what is in
    // it was not necessarily put there by us
    for (const QString &folder : m_takeFoldersMade) {
        QDir dir(folder);
        if (!dir.exists()) continue;
        if (!dir.isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System |
                         QDir::NoDotAndDotDot)) continue;
        if (QDir().rmdir(folder)) {
            cerr << "MainWindow::closeSession: removed the empty takes folder "
                 << folder << endl;
        }
    }
    m_takeFoldersMade.clear();

    m_takes->clear();
    m_takePosition = 0;
    m_takePreRoll = 0;
    m_takeEnd = -1;
    m_takeAnalysisRange = Coverage::Range();
    updateTakeCombo();

    m_analyser->fileClosed();

    while (m_paneStack->getPaneCount() > 0) {

        Pane *pane = m_paneStack->getPane(m_paneStack->getPaneCount() - 1);

        while (pane->getLayerCount() > 0) {
            m_document->removeLayerFromView
                (pane, pane->getLayer(pane->getLayerCount() - 1));
        }
        
        m_overview->unregisterView(pane);
        m_paneStack->deletePane(pane);
    }

    while (m_paneStack->getHiddenPaneCount() > 0) {

        Pane *pane = m_paneStack->getHiddenPane
            (m_paneStack->getHiddenPaneCount() - 1);
        
        while (pane->getLayerCount() > 0) {
            m_document->removeLayerFromView
                (pane, pane->getLayer(pane->getLayerCount() - 1));
        }
        
        m_overview->unregisterView(pane);
        m_paneStack->deletePane(pane);
    }

    // m_pendingExtraPanes holds panes that were moved to m_hiddenPanes via
    // hidePane() in record().  The getHiddenPaneCount() loop above already
    // handled them — they are now deleted.  Clear our list so the pointers
    // are not used again.
    m_pendingExtraPanes.clear();

    delete m_document;
    m_document = 0;
    m_viewManager->clearSelections();
    m_timeRulerLayer = 0; // document owned this

    m_sessionFile = "";

    CommandHistory::getInstance()->clear();
    CommandHistory::getInstance()->documentSaved();
    documentRestored();
}

void
MainWindow::openFile()
{
    QString orig = m_audioFile;
    if (orig == "") orig = ".";
    else orig = QFileInfo(orig).absoluteDir().canonicalPath();

    QString path = getOpenFileName(FileFinder::SessionOrAudioFile);

    if (path.isEmpty()) return;

    FileOpenStatus status = openPath(path, ReplaceSession);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>File open failed</b><p>File \"%1\" could not be opened").arg(path));
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    }
}

void
MainWindow::openSingingTrack()
{
    QString path = getOpenFileName(FileFinder::AudioFile);
    if (path.isEmpty()) return;

    loadSingingTrack(path);
}

void
MainWindow::loadSingingTrack(QString path)
{
    // Load a second audio file as the "singing" track.
    // This opens the file as an additional model alongside the main
    // (reference) model, then runs pYIN on it in the secondary colour scheme.

    if (!m_document) {
        QMessageBox::warning(this, tr("No session"),
                             tr("<b>No session open</b><p>Please open a reference audio file first."));
        return;
    }

    emit activity(tr("Load singing track \"%1\"").arg(path));

    // A track the user loads is a take of its own, analysed in full, and
    // the take that was on show is put away as it is (spec 5.3)
    if (m_takes->getActiveIndex() >= 0) {
        closeOpenTakeCommand(true);
        deactivateTake();
        m_takes->addTake();
        putOtherTakeLayersAway();
    }

    ModelId singingModelId;
    std::vector<Pane *> extraPanes;
    FileOpenStatus status =
        openSingingAudioFile(path, singingModelId, extraPanes);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open singing track"),
                              tr("<b>File open failed</b><p>File \"%1\" could not be opened").arg(path));
        return;
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open singing track"),
                              tr("<b>Audio required</b><p>Could not open \"%1\" as audio").arg(path));
        return;
    }

    // Set up the secondary analyser NOW, before pruning the extra pane:
    // the imported WaveformLayer in that pane is the only layer
    // referencing the singing model, so deleting it first would make
    // Document::releaseModel() free the model before it can be analysed.
    // Once m_analyser2's own WaveformLayer references the model the orphan
    // can go.  This also clears m_pendingSingingModelId, so the
    // analyseNewSingingModel() call queued by modelAdded() becomes a no-op.
    analyseNewSingingModel();

    // If the analyser setup above failed, nothing else references the
    // singing model and pruning releases it, which is what we want.
    for (Pane *extra : extraPanes) {
        pruneExtraPane(extra, singingModelId);
    }

    // The history goes, as it does on any change of take (spec 5.4): its
    // take commands are about a take that is not on show any more, and
    // openPath() has just pushed an "Import" command of its own whose
    // pane was pruned away again above
    clearTakeHistory();

    updateTakeCombo();
}

MainWindow::FileOpenStatus
MainWindow::openSingingAudioFile(QString path, sv::ModelId &modelId,
                                 std::vector<Pane *> &extraPanes)
{
    // Record the pane count before opening so we can collect any extra
    // panes that openPath(CreateAdditionalModel) creates via
    // AddPaneCommand.  We want both tracks to share pane 0, not appear in
    // separate panes.
    int paneCountBefore = m_paneStack ? m_paneStack->getPaneCount() : 0;

    FileOpenStatus status = openPath(path, CreateAdditionalModel);

    // The extra pane holds the singing track's own imported waveform
    // layer, which we do not want either.  It is not pruned here: that
    // layer is the only reference to the new model until the caller has
    // one of its own, and pruning releases a model nothing references.
    // Collected from the top down, which is the order they are removed in
    if (m_paneStack) {
        for (int i = m_paneStack->getPaneCount() - 1;
             i >= paneCountBefore; --i) {
            if (Pane *extra = m_paneStack->getPane(i)) {
                extraPanes.push_back(extra);
            }
        }
    }

    // modelAdded() fired synchronously inside openPath() and stored the
    // new model's id in m_pendingSingingModelId.  It is left there for
    // analyseNewSingingModel() to take; a caller that sets its analyser up
    // some other way has to clear it itself
    modelId = m_pendingSingingModelId;

    return status;
}

MainWindow::FileOpenStatus
MainWindow::openTakeAudioFile(QString path, sv::ModelId &modelId)
{
    // The audio file of a take, opened as a model of the document and
    // nothing else: no pane, no layer, no entry in Recent Files and -- the
    // point of it -- no command.
    //
    // openPath() cannot be used for these files.  It makes a pane through
    // an AddPaneCommand, and CommandHistory::addCommand() clears the redo
    // stack and deletes what is on it: a command pushed while an undo or a
    // redo is running destroys the very command that is running.  Undo of
    // a take swaps its audio, so this is not avoidable any other way.
    modelId = {};

    FileSource source(path);
    if (!source.isAvailable()) return FileOpenFailed;
    source.waitForData();

    // The rate the rest of the session is at, as openAudio() works it out
    sv_samplerate_t rate = 0;
    if (Preferences::getInstance()->getFixedSampleRate() != 0) {
        rate = Preferences::getInstance()->getFixedSampleRate();
    } else if (Preferences::getInstance()->getResampleOnLoad() &&
               getMainModel()) {
        rate = getMainModel()->getSampleRate();
    }

    auto model = std::make_shared<ReadOnlyWaveFileModel>(source, rate);
    if (!model->isOK()) return FileOpenFailed;

    ModelId id = ModelById::add(model);
    m_document->addNonDerivedModel(id);

    // modelAdded() has just queued a call to set up a singing track on it,
    // as it does for any audio model that is not the main one.  The caller
    // makes the analyser itself, so that call must find nothing to do
    m_pendingSingingModelId = {};

    modelId = id;
    return FileOpenSucceeded;
}

QString
MainWindow::swapSingingAudio(QString path)
{
    // The audio of a take is written again from scratch whenever a
    // recording is spliced into it or a part of it is erased.  Analysing
    // the whole of it again would take as long as the song, so the pitch
    // and notes layers stay as they are and the new file goes underneath
    // them.
    //
    // What makes that possible: an Analyser claims a pitch or notes layer
    // whose model has the analyser's own audio model as its source model.
    // That is how a restored session's layers find their analyser; here we
    // make it true of the new audio by hand and let the same scan do the
    // rest.

    if (!m_document) return tr("There is no session to swap the audio of");

    Pane *pane = m_paneStack ? m_paneStack->getPane(0) : nullptr;
    if (!pane) return tr("There is no pane to swap the audio in");

    if (!m_analyser2) {
        return tr("There is no singing track to swap the audio of");
    }

    Layer *pitch = m_analyser2->getLayer(Analyser::PitchTrack);
    Layer *notes = m_analyser2->getLayer(Analyser::Notes);
    if (!pitch || !notes) {
        return tr("The singing track has no pitch and notes layers to keep");
    }

    // What the swap must leave as it was.  The analyser of the new audio
    // starts from the settings the two analysers share, which know
    // nothing of what a take has done to these layers
    const Analyser::Component components[] = {
        Analyser::Audio, Analyser::PitchTrack, Analyser::Notes
    };
    const int componentCount = sizeof(components) / sizeof(components[0]);
    bool visible[componentCount], audible[componentCount];
    for (int i = 0; i < componentCount; ++i) {
        visible[i] = m_analyser2->isVisible(components[i]);
        audible[i] = m_analyser2->isAudible(components[i]);
    }
    Layer *selected = pane->getSelectedLayer();

    // 1. The new audio, as a model of the document and nothing more.
    // First, so that a file that cannot be read disturbs nothing
    ModelId newAudio;
    FileOpenStatus status = openTakeAudioFile(path, newAudio);

    if (status != FileOpenSucceeded || newAudio.isNone()) {
        return tr("The file \"%1\" could not be opened as audio").arg(path);
    }

    // 2. The old audio's waveform layer goes, and with it the old audio;
    // the pitch and notes layers stay in the pane with their events.  The
    // analyser has nothing left to lose by being deleted
    m_analyser2->releaseLayers();
    delete m_analyser2;
    m_analyser2 = nullptr;

    // 3. Those layers' models come from the new audio now.  Nothing reads
    // their source model between the release above and here
    for (ModelId id : { pitch->getModel(), notes->getModel() }) {
        if (auto model = ModelById::get(id)) {
            model->setSourceModel(newAudio);
        }
    }

    // 4. An analyser for the new audio, which claims the two layers.  No
    // analysis (deferAnalysis): the layers hold the analysis of all of the
    // take but the range that has just changed, and the take's coverage is
    // not "the whole of this file" either
    bool wasRebuilding = m_rebuildingTakeAudio;
    m_rebuildingTakeAudio = true;
    setupSingingTrackAnalyser(newAudio, true);
    m_rebuildingTakeAudio = wasRebuilding;

    // 5. With no analyser there is no waveform layer holding the new
    // audio, and the layers are left showing a take whose audio has gone.
    // The model stays registered with the document, which releases it when
    // it goes; see loadTakeAudio()
    if (!m_analyser2) {
        return tr("The singing track could not be set up on \"%1\"").arg(path);
    }

    // 6. What the swap was not to change.  On the layers themselves:
    // setVisible() and setAudible() write to the shared settings, and
    // neither the muting of a take nor the pane's own stacking is the
    // user's wish about the reference
    for (int i = 0; i < componentCount; ++i) {
        Layer *layer = m_analyser2->getLayer(components[i]);
        if (!layer) continue;
        layer->setLayerDormant(pane, !visible[i]);
        if (auto params = layer->getPlayParameters()) {
            params->setPlayAudible(audible[i]);
        }
    }
    pane->layerParametersChanged();

    // The selected layer is the top layer of the pane as well, which is
    // where the editing tools look for the layer they act on
    if (selected && selected != pane->getSelectedLayer()) {
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            if (pane->getLayer(i) == selected) {
                m_paneStack->setCurrentLayer(pane, selected);
                break;
            }
        }
    }

    updateLayerStatuses();
    updateMenuStates();

    return "";
}

void
MainWindow::analyseNewSingingModel()
{
    // Called when a new audio model has been added (by loadSingingTrack)
    // and we want to run the secondary (singing-track) analysis on it.
    if (m_pendingSingingModelId.isNone()) return;

    ModelId singingModelId = m_pendingSingingModelId;
    m_pendingSingingModelId = {};

    setupSingingTrackAnalyser(singingModelId);
}

void
MainWindow::analyseRestoredSingingModel()
{
    // The queued call from modelAdded(): an audio model has been added to
    // the document by a route that has not set a singing analyser up for
    // it.  The routes that do -- loadSingingTrack() and the take's own
    // openTakeAudioFile() -- have cleared the pending id by now, and a
    // session being restored is dealt with by restoreTakes(), which drops
    // the audio model the document carried and opens each take's audio
    // itself.  So this is the fallback, and what it finds is a take of its
    // own, analysed in full
    if (m_restoringSession) return;
    if (m_pendingSingingModelId.isNone()) return;

    // Which take this is has to be settled first: its layers are found by
    // its name, so it must have one before they are looked for
    if (m_takes->getActiveIndex() < 0) {
        m_takes->addTake();
    }

    adoptTakeLayers(m_pendingSingingModelId);
    analyseNewSingingModel();
}

void
MainWindow::openBackgroundMusic()
{
    QString path = getOpenFileName(FileFinder::AudioFile);
    if (path.isEmpty()) return;
    loadBackgroundMusic(path);
}

void
MainWindow::loadBackgroundMusic(QString path)
{
    if (!m_document) {
        QMessageBox::warning(this, tr("No session"),
                             tr("<b>No session open</b><p>Please open a reference audio file first."));
        return;
    }

    emit activity(tr("Load background music \"%1\"").arg(path));

    // Tear down any previously loaded background music track.
    teardownBackgroundMusic();

    // Record the pane count before opening so we can remove the extra pane
    // that openPath(CreateAdditionalModel) creates via AddPaneCommand.
    // The background music doesn't need its own pane.
    int paneCountBefore = m_paneStack ? m_paneStack->getPaneCount() : 0;

    // Set the flag so modelAdded() captures the new model ID and skips
    // singing-track analysis for this model.
    m_loadingBackgroundMusic = true;
    FileOpenStatus status = openPath(path, CreateAdditionalModel);
    m_loadingBackgroundMusic = false;

    if (status == FileOpenFailed) {
        m_backgroundMusicModelId = {};
        QMessageBox::critical(this, tr("Failed to open background music"),
                              tr("<b>File open failed</b><p>File \"%1\" could not be opened").arg(path));
        return;
    } else if (status == FileOpenWrongMode) {
        m_backgroundMusicModelId = {};
        QMessageBox::critical(this, tr("Failed to open background music"),
                              tr("<b>Audio required</b><p>Could not open \"%1\" as audio").arg(path));
        return;
    }

    if (m_backgroundMusicModelId.isNone()) {
        cerr << "loadBackgroundMusic: modelAdded did not capture a model ID — aborting" << endl;
        return;
    }

    // Create the WaveformLayer for the background music BEFORE removing the
    // orphan layers from the extra pane.  This ensures the background music
    // model has at least one layer referencing it when the orphan is deleted,
    // so Document::releaseModel() does not free it prematurely.
    //
    // Use createLayer() (not createEmptyLayer()) — the same path that
    // Analyser::addWaveform() uses for the secondary analyser — then
    // immediately rebind it to the background music model with setModel().
    if (m_paneStack && m_paneStack->getPaneCount() > 0) {
        Pane *pane = m_paneStack->getPane(0);
        if (pane && m_document) {
            Layer *rawLayer = m_document->createLayer(LayerFactory::Waveform);
            m_backgroundMusicLayer = qobject_cast<WaveformLayer *>(rawLayer);
            if (m_backgroundMusicLayer) {
                m_document->setModel(m_backgroundMusicLayer, m_backgroundMusicModelId);
                ColourDatabase *cdb = ColourDatabase::getInstance();
                m_backgroundMusicLayer->setBaseColour(
                    cdb->getColourIndex(tr("Green")));
                m_document->attachLayerToView(pane, m_backgroundMusicLayer);

                // The waveform is only needed to register the model with
                // the play source — we don't want it rendered on screen.
                m_backgroundMusicLayer->showLayer(pane, false);

                // Set initial audibility from the toggle state.
                auto params = m_backgroundMusicLayer->getPlayParameters();
                if (params) {
                    bool wantAudible = !m_playBackgroundMusic ||
                                       m_playBackgroundMusic->isChecked();
                    params->setPlayAudible(wantAudible);
                }

                cerr << "loadBackgroundMusic: waveform layer added for model "
                     << m_backgroundMusicModelId << endl;
            } else {
                cerr << "loadBackgroundMusic: failed to create WaveformLayer — aborting" << endl;
                return;
            }
        }
    }

    // Remove the extra pane that openPath(CreateAdditionalModel) created.
    // Now safe to delete the orphan WaveformLayer: our new layer already
    // holds a reference to the model so Document::releaseModel() will not
    // free it when the orphan is deleted.
    if (m_paneStack) {
        while (m_paneStack->getPaneCount() > paneCountBefore) {
            Pane *extra = m_paneStack->getPane(m_paneStack->getPaneCount() - 1);
            if (!extra) break;
            pruneExtraPane(extra, m_backgroundMusicModelId);
        }
    }

    updateLayerStatuses();
    updateMenuStates();
}

void
MainWindow::teardownBackgroundMusic()
{
    if (m_backgroundMusicLayer) {
        // Explicitly remove from play source before deleting the layer, so
        // the model is removed from the mix even if layerInAView(false) is not
        // triggered through the normal path.
        if (m_playSource && !m_backgroundMusicModelId.isNone()) {
            m_playSource->removeModel(m_backgroundMusicModelId);
        }
        if (m_document) {
            m_document->deleteLayer(m_backgroundMusicLayer, true);
        }
        m_backgroundMusicLayer = nullptr;
    }
    m_backgroundMusicModelId = {};
}

void
MainWindow::backgroundMusicToggled()
{
    if (!m_backgroundMusicLayer) return;
    auto params = m_backgroundMusicLayer->getPlayParameters();
    if (!params) return;
    bool wantAudible = m_playBackgroundMusic && m_playBackgroundMusic->isChecked();
    params->setPlayAudible(wantAudible);
    if (m_bgMusicLPW) m_bgMusicLPW->setEnabled(wantAudible);
    cerr << "backgroundMusicToggled: background music "
         << (wantAudible ? "unmuted" : "muted") << endl;
}

void
MainWindow::backgroundMusicGainChanged(float gain)
{
    if (!m_backgroundMusicLayer) return;
    auto params = m_backgroundMusicLayer->getPlayParameters();
    if (!params) return;
    if (gain == 0.f) {
        params->setPlayAudible(false);
        if (m_playBackgroundMusic) m_playBackgroundMusic->setChecked(false);
    } else {
        params->setPlayAudible(true);
        if (m_playBackgroundMusic) m_playBackgroundMusic->setChecked(true);
        params->setPlayGain(gain);
    }
}

void
MainWindow::backgroundMusicPanChanged(float pan)
{
    if (!m_backgroundMusicLayer) return;
    auto params = m_backgroundMusicLayer->getPlayParameters();
    if (params) params->setPlayPan(pan);
}

void
MainWindow::alternatePitchToggled()
{
    if (!m_document || !m_paneStack || m_paneStack->getPaneCount() < 1) {
        updateLayerStatuses();
        return;
    }

    if (m_alternatePitch->isShown()) {
        m_alternatePitch->hide();
        // The layer went without a command, as it must (see
        // teardownRealtimePitchLayer()), but the session has changed
        documentModified();
    } else if (m_alternatePitch->show(m_document, m_paneStack->getPane(0))) {
        // The new layer is on top, and is the one the editing tools
        // would work on: put the tracks that can be edited back there,
        // in the order they were
        m_analyser->stackLayers();
        if (m_analyser2) m_analyser2->stackLayers();
        syncAlternatePitchTrack();
        // As above: the layer arrives without a command, so the change to
        // the session has to be noted here
        documentModified();
    }

    updateLayerStatuses();
}

void
MainWindow::alternatePitchUp()
{
    stepAlternatePitch(true);
}

void
MainWindow::alternatePitchDown()
{
    stepAlternatePitch(false);
}

void
MainWindow::stepAlternatePitch(bool up)
{
    if (!m_alternatePitch->isShown() || !m_alternatePitch->canStep(up)) return;
    m_alternatePitch->step(up);
    documentModified();
    updateLayerStatuses();
    getStatusLabel()->setText
        (tr("Alternate pitch track: %1")
         .arg(AlternatePitchTrack::describe(m_alternatePitch->getOctaves())));
}

void
MainWindow::syncAlternatePitchTrack()
{
    // The reference pitch layer, and its model with it, is replaced
    // whenever the reference is analysed again
    if (!m_alternatePitch->isShown()) return;
    Layer *reference = m_analyser->getLayer(Analyser::PitchTrack);
    m_alternatePitch->setSource(reference ? reference->getModel() : ModelId());
    updateAlternatePitchForTake();
}

void
MainWindow::updateAlternatePitchForTake()
{
    // During a singing take the alternate track is the one to sing to:
    // it is shown in full, and the reference pitch track makes way
    bool following = (m_alternatePitch->isShown() &&
                      m_recordingAsSingingTrack &&
                      m_recordTarget && m_recordTarget->isRecording());

    m_alternatePitch->setFollowed(following);

    Layer *reference = m_analyser->getLayer(Analyser::PitchTrack);
    Pane *pane = m_analyser->getPane();

    if (following) {
        if (!m_referencePitchHiddenForTake && reference && pane &&
            !reference->isLayerDormant(pane)) {
            reference->showLayer(pane, false);
            m_referencePitchHiddenForTake = true;
        }
    } else if (m_referencePitchHiddenForTake) {
        m_referencePitchHiddenForTake = false;
        if (reference && pane) reference->showLayer(pane, true);
    }
}

void
MainWindow::updateSingingTrackForTake()
{
    // The singing that is already there is drawn over the same part of
    // the pane as what is being sung now, and its pitch in the same
    // orange as the live dots: during a take the singer cannot tell the
    // one from the other, and neither helps them follow the track they
    // are singing to.  So the stored pitch and notes make way, and come
    // back when the take stops.  Not with Analyser::setVisible(), which
    // would write the state to the settings the reference shares.
    bool inTake = (m_recordingAsSingingTrack &&
                   m_recordTarget && m_recordTarget->isRecording());

    Pane *pane = m_analyser2 ? m_analyser2->getPane() : nullptr;

    // Only what was on show is hidden, and only what we hid is shown
    // again: the user may have had either of them off already
    auto update = [&](Analyser::Component c, bool &hidden) {
        Layer *layer = m_analyser2 ? m_analyser2->getLayer(c) : nullptr;
        if (inTake) {
            if (!hidden && layer && pane && !layer->isLayerDormant(pane)) {
                layer->showLayer(pane, false);
                hidden = true;
            }
        } else if (hidden) {
            hidden = false;
            if (layer && pane) layer->showLayer(pane, true);
        }
    };

    update(Analyser::PitchTrack, m_singingPitchHiddenForTake);
    update(Analyser::Notes, m_singingNotesHiddenForTake);
}

void
MainWindow::syncCoverageStrip()
{
    // The strip is the store of the take's coverage as well as the
    // picture of it, so it follows every change to the take: a recording
    // spliced in, a file loaded, the session closed
    Pane *pane = m_paneStack ? m_paneStack->getPane(0) : nullptr;
    if (!m_document || !pane) return;

    if (!m_takes->haveTake() || m_takes->getCoverage().isEmpty()) {
        m_coverageStrip->hide();
        return;
    }

    // The strip of the take that is active now.  The strips of the other
    // takes stay in the pane, hidden: each is the stored coverage of its
    // own take, and release() is how this object lets go of one
    QString name = m_takes->getActiveName();
    if (m_coverageStrip->isShown() && m_coverageStrip->getTakeName() != name) {
        m_coverageStrip->release();
    }

    bool wasShown = m_coverageStrip->isShown();
    if (!m_coverageStrip->isShown()) {
        m_coverageStrip->adopt(m_document, pane, name);
    }
    if (!m_coverageStrip->show(m_document, pane, name)) return;

    // The play source takes in the model of every layer that is in a
    // view, whether the model can be played or not, and the models it
    // holds are what say where playback ends.  The strip is a picture,
    // not sound: one left over from a longer singing file would hold
    // playback open past the end of what there is to hear
    if (m_playSource && !m_coverageStrip->getModelId().isNone()) {
        m_playSource->removeModel(m_coverageStrip->getModelId());
    }

    m_coverageStrip->setCoverage(m_takes->getCoverage());

    // The band runs along the bottom of the pane, over the take's
    // waveform, so it has to be above that layer. The swap makes the
    // waveform layer again, on top of everything (as does activating a
    // take), so this is looked at on every call and not only when the
    // strip is first shown
    Layer *strip = m_coverageStrip->getLayer();
    int layers = pane->getLayerCount();
    if (strip && layers > 0 && pane->getLayer(layers - 1) != strip) {
        TakeLayers::raise(pane, strip);
    }

    if (!wasShown) {
        // The new layer is on top, where a tool would look for the layer
        // to act on, so the tracks that can be edited go back there, as
        // setupRecordingLayer() does.  That call does nothing in Tony,
        // though: it goes through PaneStack::setCurrentLayer(), which
        // needs a PropertyStack, and this pane stack has none.  What
        // really keeps the strip out of reach is that no tool Tony sets
        // for pane 0 acts on a layer of this kind -- NoteEditMode takes
        // the pane's top FlexiNoteLayer, and SelectMode, which snaps to
        // the top layer, is set only for the ruler pane
        m_analyser->stackLayers();
        if (m_analyser2) m_analyser2->stackLayers();
    }
}

void
MainWindow::setupSingingTrackAnalyser(sv::ModelId singingModelId, bool deferAnalysis)
{
    if (!m_document) return;
    if (!m_paneStack || m_paneStack->getPaneCount() < 1) return;

    // Reuse the main pane (pane 0) so both pitch tracks overlay each other.
    // NOTE: when called from the recording flow, m_pendingExtraPanes may hold
    // extra panes that were hidden but not yet deleted (see record()).  We must
    // check getPaneCount() AFTER accounting for those hidden panes.  Pane 0
    // is always the main analysis pane created by analyseNewMainModel().
    Pane *pane = m_paneStack->getPane(0);
    if (!pane) return;

    // Tear down any previous secondary analyser
    teardownSingingTrackAnalyser();

    // Create the secondary analyser with the singing-track colour scheme
    m_analyser2 = new Analyser(Analyser::SecondaryColors);

    connect(m_analyser2, SIGNAL(layersChanged()),
            this, SLOT(updateLayerStatuses()));
    connect(m_analyser2, SIGNAL(layersChanged()),
            this, SLOT(updateMenuStates()));

    // Erase Singing is switched off while a recorded range is being
    // analysed, so the menus have to hear when one is done with
    connect(m_analyser2, SIGNAL(initialAnalysisCompleted()),
            this, SLOT(updateMenuStates()));

    // The result of a ranged analysis completes the undo command of the
    // recording that asked for it (spec 5.4)
    connect(m_analyser2, &Analyser::rangedAnalysisMerged,
            this, &MainWindow::takeAnalysisMerged);

    // deferAnalysis=true: only set up waveform/visualisation layers now;
    // pYIN will be run later by analyseNow() once recording is complete.
    QString error = m_analyser2->newFileLoaded(
        m_document, singingModelId, m_paneStack, pane, deferAnalysis);

    if (error != "") {
        QMessageBox::warning(this, tr("Failed to analyse singing track"),
                             tr("<b>Analysis failed</b><p>%1</p>").arg(error));
        delete m_analyser2;
        m_analyser2 = nullptr;
        // Do NOT drain m_pendingExtraPanes here — nothing holds a reference
        // to the recording model at this point, so deleteLayer(orphan, true)
        // would free the live WritableWaveFileModel mid-capture → crash.
        // The hidden extra pane will be cleaned up by closeSession()'s
        // getHiddenPaneCount() loop, or on the next successful recording.
        return;
    }

    // A take's audio is named in the session's <takes> element and opened
    // from there, so neither the waveform layer nor the audio model is to
    // be written to the session as well: a second copy, with an absolute
    // path, that the session reader would ask the user to locate if the
    // file had gone
    if (Layer *audio = m_analyser2->getLayer(Analyser::Audio)) {
        audio->setSavedInSession(false);
    }

    // m_analyser2->newFileLoaded() has now created its own WaveformLayer
    // referencing singingModelId.  This means it is safe to delete the orphan
    // WaveformLayer that MainWindowBase::record() put in the extra pane:
    // Document::releaseModel() will not free singingModelId because
    // m_analyser2's layer still holds a reference to it.
    //
    // We must do this BEFORE calling m_paneStack->deletePane(), because
    // deleteLayer(force=true) iterates Document::m_layerViewMap to remove the
    // layer from any views — and that map still contains the live pane pointer.
    // After deletePane() the pointer would be dangling → crash.
    drainPendingExtraPanes(singingModelId);

    // The take's audio is the file behind this model.  All of a file the
    // user loaded holds recorded singing; a file we have just spliced
    // ourselves has the coverage the splice worked out, which must not be
    // thrown away here.  A session that was saved with a coverage strip
    // says for itself which parts of its take hold singing: that layer is
    // in the pane already, waiting to be taken over.
    if (!m_rebuildingTakeAudio) {
        if (auto wfm = ModelById::getAs<WaveFileModel>(singingModelId)) {
            // The take first, because its name is what the strip of a
            // session that has one is stored under
            m_takes->setWholeFileTake(wfm->getLocation(),
                                      wfm->getFrameCount());
            Coverage stored;
            if (!m_coverageStrip->isShown() &&
                m_coverageStrip->adopt(m_document, pane,
                                       m_takes->getActiveName())) {
                stored = m_coverageStrip->getCoverage();
            }
            if (!stored.isEmpty()) {
                cerr << "MainWindow::setupSingingTrackAnalyser: the session's "
                     << "coverage strip has " << stored.getRanges().size()
                     << " range(s) of recorded singing in it" << endl;
                m_takes->setTake(wfm->getLocation(), stored);
            }
        }
        syncCoverageStrip();
    }

    // These layers are the active take's, and everything in the pane that
    // is some other take's is put away: the takes that are not on show
    // must not be claimed by an analyser, heard, or painted
    nameActiveTakeLayers();
    putOtherTakeLayersAway();

    // Re-stack layers so the primary pitch track stays on top
    m_analyser->getLayer(Analyser::PitchTrack);  // ensure primary is on top
    updateTakeCombo();
    updateLayerStatuses();
    updateMenuStates();

    if (deferAnalysis) {
        emit activity(tr("Singing track set up, with no analysis of its own"));
    } else {
        emit activity(tr("Singing track loaded and analysis started"));
    }
}

void
MainWindow::pruneExtraPane(Pane *extra, sv::ModelId ownedModelId)
{
    // The rules for what may be deleted and what only detached live
    // with the helper: see PaneUtils.cpp.
    ::pruneExtraPane(m_document, m_paneStack, extra, ownedModelId, m_overview);
}

void
MainWindow::drainPendingExtraPanes(sv::ModelId singingModelId)
{
    // Called from setupSingingTrackAnalyser() after m_analyser2 has been
    // initialised with its own WaveformLayer referencing the recording
    // model, which is what makes pruneExtraPane() safe for the panes that
    // record() hid rather than deleted.
    if (m_pendingExtraPanes.empty()) return;

    cerr << "MainWindow::drainPendingExtraPanes: draining "
         << m_pendingExtraPanes.size() << " pending extra pane(s)" << endl;

    for (Pane *extra : m_pendingExtraPanes) {
        pruneExtraPane(extra, singingModelId);
    }

    m_pendingExtraPanes.clear();
}

void
MainWindow::muteSingingAudioForTake()
{
    // The singing that is there is not heard while it is being recorded
    // into: the singer would hear themselves along with the reference,
    // and on speakers that goes back into the microphone.  Not with
    // setAudible(), which would write the state to the settings the
    // reference shares; the button goes on saying what the user asked
    // for, and restoreSingingAudioAfterTake() applies it afterwards.
    m_singingAudioAfterTake =
        (m_analyser2 ? m_analyser2->isAudible(Analyser::Audio) : true);

    if (!m_analyser2) return;
    if (Layer *audio = m_analyser2->getLayer(Analyser::Audio)) {
        if (auto params = audio->getPlayParameters()) {
            params->setPlayAudible(false);
            m_singingAudioMutedForTake = true;
        }
    }
}

void
MainWindow::restoreSingingAudioAfterTake()
{
    if (!m_singingAudioMutedForTake) return;
    m_singingAudioMutedForTake = false;
    if (!m_analyser2) return;
    // By now m_analyser2 is usually the one made for the take's new audio
    // file, not the one that was muted: what the user asked for is what
    // matters, not which layer it is applied to
    if (Layer *audio = m_analyser2->getLayer(Analyser::Audio)) {
        if (auto params = audio->getPlayParameters()) {
            params->setPlayAudible(m_singingAudioAfterTake);
        }
    }
    updateLayerStatuses();
}

void
MainWindow::liftPlaySelectionForTake()
{
    // Playback constrained to the selection starts in the selection, not
    // at the lead-in of a pre-roll or at a playhead outside it, and stops
    // or loops at its end while the recording runs on. What was sung would
    // then not be where the take puts it: the take counts the reference
    // as playing on from playbackStart() without a break. Through the
    // view manager, which writes no settings; the button follows it, and
    // is greyed out meanwhile (updateMenuStates())
    if (!m_viewManager->getPlaySelectionMode()) return;
    m_viewManager->setPlaySelectionMode(false);
    m_playSelectionLiftedForTake = true;
}

void
MainWindow::restorePlaySelectionAfterTake()
{
    if (!m_playSelectionLiftedForTake) return;
    m_playSelectionLiftedForTake = false;
    m_viewManager->setPlaySelectionMode(true);
}

void
MainWindow::teardownSingingTrackAnalyser()
{
    // m_singingAudioMutedForTake is deliberately not cleared here: the
    // singing track is torn down and built again in the middle of
    // finishing a take, and what the user asked Play Singing Audio for
    // has to survive that.  restoreSingingAudioAfterTake() clears it when
    // the take is over, and closeSession() when the session goes.
    if (!m_analyser2) return;

    // removeAllLayers() removes each layer from the pane and deletes it from
    // the document (releasing the model if unreferenced), then calls
    // fileClosed() to clear the analyser's internal state.  This is the
    // correct teardown when the document is still alive (e.g. when replacing
    // a previous singing-track recording with a new one).
    //
    // NOTE: we do NOT call drainPendingExtraPanes() here.  The pending extra
    // panes always belong to the most-recently-started recording (the one
    // about to begin, not the one being torn down).  Draining them here would
    // call deleteLayer(orphan) while m_analyser2 for the NEW recording doesn't
    // exist yet, so nothing would hold the new model reference → crash.
    // drainPendingExtraPanes() is called from setupSingingTrackAnalyser() once
    // m_analyser2 is set up and its WaveformLayer holds the model reference.
    // closeSession() handles any residual hidden panes via its own
    // getHiddenPaneCount() loop using removeLayerFromView + deletePane.
    //
    // The forced deletes do not fire layerInAView(false), which is what
    // usually takes a model out of the play source.  The models go from
    // there when the document releases them (modelAboutToBeReleased).
    if (m_document) {
        m_analyser2->removeAllLayers();
    } else {
        // Document is already gone (e.g. closeSession destroyed it); just
        // clear the in-memory state without touching the document.
        m_analyser2->fileClosed();
    }
    delete m_analyser2;
    m_analyser2 = nullptr;
}

void
MainWindow::setupRealtimePitchLayer()
{
    // Create a SparseTimeValueModel and TimeValueLayer to display a
    // live pitch estimate during microphone recording.
    //
    // At the time this is called (triggered by recordStatusChanged(true)),
    // MainWindowBase::record() has already:
    //   1. Created a WritableWaveFileModel for the recording.
    //   2. Set it as the document main model.
    //   3. Emitted audioFileLoaded() -> analyseNewMainModel() which added panes.
    //
    // So getMainModelId() returns the WritableWaveFileModel being filled.
    // RealtimePitchTracker polls that model via getData() on a QTimer.

    if (!m_document) return;
    if (!m_paneStack || m_paneStack->getPaneCount() < 1) return;

    Pane *pane = m_paneStack->getPane(0);
    if (!pane) return;

    // Remove any stale realtime layer from a previous recording session.
    teardownRealtimePitchLayer();

    // The audio source is the WritableWaveFileModel being recorded into.
    // In RecordReplaceSession mode it is the document's main model.
    // In RecordCreateAdditionalModel mode (recording as singing track) the
    // main model is still the reference track, so we must search for the
    // WritableWaveFileModel among all document models instead.
    ModelId audioSourceId;

    if (m_recordingAsSingingTrack && m_document) {
        // Use the model ID captured in modelAdded() when the recording
        // WritableWaveFileModel was first registered.  Do NOT scan all
        // document models here: a previous recording's WritableWaveFileModel
        // may still be registered (because its orphan waveform layer, which
        // was view-detached but not deleted from m_document->m_layers, holds
        // a reference that prevents releaseModel() from freeing it).  A scan
        // would find that stale model first and point the tracker at the
        // completed old recording, replaying its entire pitch content as dots.
        if (!m_currentRecordingModelId.isNone()) {
            audioSourceId = m_currentRecordingModelId;
            cerr << "setupRealtimePitchLayer: using captured recording model "
                 << audioSourceId << endl;
        } else {
            // Fallback: m_currentRecordingModelId not yet set (modelAdded
            // deferred lambda hasn't fired).  Scan as last resort but prefer
            // the model with the fewest frames (most recently started).
            ModelId mainId = getMainModelId();
            sv_frame_t fewestFrames = -1;
            for (ModelId mid : m_document->getModels()) {
                if (mid == mainId) continue;
                if (ModelById::isa<WritableWaveFileModel>(mid)) {
                    auto wfm = ModelById::getAs<WritableWaveFileModel>(mid);
                    sv_frame_t frames = wfm ? wfm->getFrameCount() : 0;
                    if (audioSourceId.isNone() || frames < fewestFrames) {
                        audioSourceId = mid;
                        fewestFrames = frames;
                    }
                }
            }
            if (audioSourceId.isNone()) {
                audioSourceId = mainId;
                cerr << "setupRealtimePitchLayer: could not find singing-track "
                        "recording model, falling back to main model" << endl;
            } else {
                cerr << "setupRealtimePitchLayer: fallback scan found recording "
                     << "model " << audioSourceId
                     << " (fewest frames=" << fewestFrames << ")" << endl;
            }
        }
    } else {
        audioSourceId = getMainModelId();
    }

    if (audioSourceId.isNone()) {
        cerr << "setupRealtimePitchLayer: no audio source model found, cannot set up realtime tracker" << endl;
        return;
    }

    // Determine sample rate from the audio source model.
    sv_samplerate_t sr = 44100;
    if (auto audioModel = ModelById::getAs<WritableWaveFileModel>(audioSourceId)) {
        sr = audioModel->getSampleRate();
    } else if (auto wfm = getMainModel()) {
        sr = wfm->getSampleRate();
    }

    // Create a SparseTimeValueModel to receive pitch estimates.
    // Its resolution is the YIN hop size: one estimate per hop.
    // Unit "Hz" is required so TimeValueLayer::shouldAutoAlign() defers to
    // the pane's log-frequency coordinate system (same as the pYIN pitch track).
    //
    // notifyOnAdd false: a notice for each of the ~170 dots a second
    // would be a redraw for each. The model then tells nobody of a dot,
    // though, so m_realtimeDotsNotifier tells the pane of what was added,
    // 25 times a second
    auto pitchModel = std::make_shared<SparseTimeValueModel>
        (sr, RealtimePitchTracker::kHopSize, false);
    pitchModel->setObjectName(tr("Realtime Pitch (Live)"));
    pitchModel->setScaleUnits("Hz");
    m_realtimePitchModelId = ModelById::add(pitchModel);
    m_document->addNonDerivedModel(m_realtimePitchModelId);

    // Create a TimeValueLayer to display the pitch estimates.  Not with
    // createEmptyLayer(): that gives the layer an empty model of its own,
    // which goes into the play source only to be released again by the
    // setModel() below.
    Layer *rawLayer = m_document->createLayer(LayerFactory::TimeValues);
    m_realtimePitchLayer = qobject_cast<TimeValueLayer *>(rawLayer);

    if (!m_realtimePitchLayer) {
        cerr << "setupRealtimePitchLayer: failed to create TimeValueLayer" << endl;
        // Release the pitch model we just added; the Document will not hold
        // a reference to it since we never called setModel yet.
        ModelById::release(m_realtimePitchModelId);
        m_realtimePitchModelId = {};
        return;
    }

    // Associate our pre-filled SparseTimeValueModel with the layer.
    // The model was already registered via addNonDerivedModel above.
    m_document->setModel(m_realtimePitchLayer, m_realtimePitchModelId);
    m_realtimeDotsNotifier.setModel(m_realtimePitchModelId);
    m_realtimePitchLayer->setVerticalScale(TimeValueLayer::AutoAlignScale);
    m_realtimePitchLayer->setPlotStyle(TimeValueLayer::PlotPoints);

    // Out of the pane's cache: told of a change to the model of a layer
    // in it, the pane draws every layer in it again -- the reference's
    // pitch track, notes and waveform, 25 times a second
    m_realtimePitchLayer->setCachedInView(false);

    // Singing/recording track uses the "Orange" colour so it is visually
    // distinct from the reference track (black) and notes (blue).
    ColourDatabase *cdb = ColourDatabase::getInstance();
    m_realtimePitchLayer->setBaseColour(cdb->getColourIndex(tr("Orange")));

    m_document->attachLayerToView(pane, m_realtimePitchLayer);

    // Create and start the pitch tracker.  Its thread reads new frames
    // from audioSourceId (the WritableWaveFileModel) and emits
    // pitchDetected(); onRealtimePitchDetected() writes the estimates into
    // m_realtimePitchModelId on this thread.
    m_realtimePitchTracker = new RealtimePitchTracker(
        audioSourceId, this);
    connect(m_realtimePitchTracker, &RealtimePitchTracker::pitchDetected,
            this, &MainWindow::onRealtimePitchDetected);
    m_realtimePitchTracker->start();

    cerr << "setupRealtimePitchLayer: realtime pitch tracking started "
         << "(audio source model " << audioSourceId << ", sr=" << sr << ")" << endl;
}

void
MainWindow::stopRealtimePitchTracker()
{
    if (m_realtimePitchTracker) {
        m_realtimePitchTracker->stop();
        delete m_realtimePitchTracker;
        m_realtimePitchTracker = nullptr;
    }
}

void
MainWindow::teardownRealtimePitchLayer()
{
    stopRealtimePitchTracker();
    m_realtimeDotsNotifier.setModel({});

    if (m_realtimeLayerTeardownConnection) {
        disconnect(m_realtimeLayerTeardownConnection);
        m_realtimeLayerTeardownConnection = {};
    }

    if (m_realtimePitchLayer) {
        // Use deleteLayer(force=true) directly — do NOT call
        // removeLayerFromView first.
        //
        // removeLayerFromView creates a RemoveLayerCommand in the undo
        // history with m_added=false.  If deleteLayer then destroys the
        // layer object, that command holds a dangling pointer.  When
        // CommandHistory is later cleared (e.g. on the next closeSession)
        // the RemoveLayerCommand destructor checks !m_added and calls
        // m_d->deleteLayer(m_layer) on the already-deleted layer —
        // use-after-free / crash, and the old SparseTimeValueModel can
        // stay alive inside the undo entry long enough that its orange
        // dots reappear during the next recording.
        //
        // deleteLayer(force=true) removes the layer from all views
        // internally (without generating any undo command), then
        // releases the model if unreferenced and deletes the layer.
        // This is the correct path for a silent, non-undoable teardown.
        if (m_document) {
            m_document->deleteLayer(m_realtimePitchLayer, true);
        }
        m_realtimePitchLayer = nullptr;
    }

    // Note: we do NOT call ModelById::release(m_realtimePitchModelId) here.
    // The Document owns the SparseTimeValueModel we added via addNonDerivedModel,
    // and deleteLayer() above will have already released the model if no other
    // layer is referencing it.  Calling release() a second time would be a
    // double-free.
    m_realtimePitchModelId = {};
}

void
MainWindow::setupRecordingLayer()
{
    // A layer of our own on the recording, so that the document holds the
    // WritableWaveFileModel while the device writes to it.  The singing
    // analyser cannot do that job any more: it is showing the take's own
    // audio, pitch and notes, which stay as they are for the duration.
    //
    // The layer is never shown.  The recording starts at frame 0 of its
    // own file, which is not where its sound belongs on the reference's
    // timeline, so drawing it would put the waveform in the wrong place;
    // the live dots are what the singer watches.  It is muted for the
    // same reason the live pitch model is (review finding 3).
    //
    // With this layer in place the extra pane that AddPaneCommand made is
    // no longer the only thing holding the model, so record() can prune
    // it at once, as loadSingingTrack() does.
    if (m_recordingLayer) teardownRecordingLayer();

    if (!m_document || m_currentRecordingModelId.isNone()) return;
    if (!m_paneStack || m_paneStack->getPaneCount() < 1) return;
    Pane *pane = m_paneStack->getPane(0);
    if (!pane) return;

    Layer *rawLayer = m_document->createLayer(LayerFactory::Waveform);
    m_recordingLayer = qobject_cast<WaveformLayer *>(rawLayer);
    if (!m_recordingLayer) {
        cerr << "MainWindow::setupRecordingLayer: failed to create the layer "
             << "for the recording" << endl;
        return;
    }

    m_document->setModel(m_recordingLayer, m_currentRecordingModelId);
    m_document->attachLayerToView(pane, m_recordingLayer);
    // Raw material of a take in progress: no part of a session
    m_recordingLayer->setSavedInSession(false);
    m_recordingLayer->showLayer(pane, false);
    if (auto params = m_recordingLayer->getPlayParameters()) {
        params->setPlayAudible(false);
    }

    // The new layer is on top, where the editing tools look for the layer
    // to act on: put the tracks that can be edited back there, as
    // alternatePitchToggled() does
    m_analyser->stackLayers();
    if (m_analyser2) m_analyser2->stackLayers();
}

void
MainWindow::teardownRecordingLayer()
{
    // The recording has been spliced into the take (or the take came to
    // nothing): the model is not needed any more, and releasing it closes
    // the file handles the recording still has open.  The file itself
    // stays on disk; Tony never deletes a recording.
    ModelId recordingModelId = m_currentRecordingModelId;

    if (m_recordingLayer) {
        if (m_document) m_document->deleteLayer(m_recordingLayer, true);
        m_recordingLayer = nullptr;
    }

    // The fallback in record(): with no layer of ours, the pane that
    // AddPaneCommand made was kept, hidden, because its waveform layer
    // was all that held the model
    drainPendingExtraPanes(recordingModelId);

    // deleteLayer(force) does not fire layerInAView(false).  The model
    // leaves the play source when it is released, which is what has just
    // happened; this makes sure of it even if something else holds it
    if (m_playSource && !recordingModelId.isNone()) {
        m_playSource->removeModel(recordingModelId);
    }

    m_currentRecordingModelId = {};
}

void
MainWindow::record()
{
    // If recording is already in progress this click is a STOP request, not a
    // start request.  Delegate straight to the base class (which calls stop())
    // without doing any pre-flight teardown.  The teardown would destroy
    // m_analyser2 and remove the live recording model from m_playSource while
    // audio is still being captured — causing a crash or a null m_analyser2
    // when recordCompleted() fires analyseNow() moments later.
    if (m_recordTarget && m_recordTarget->isRecording()) {
        // Nothing left for the timer to watch, whether this Stop came
        // from the button or from the timer itself
        stopTakePolling();
        MainWindowBase::record();
        return;
    }

    // If a reference track is already loaded, record the microphone input
    // into the singing track rather than replacing the whole session.  We
    // do that by switching to RecordCreateAdditionalModel for the call, so
    // that MainWindowBase::record() adds the WritableWaveFileModel as an
    // additional (non-main) model; modelAdded() takes note of it, and
    // finishSingingTake() splices it into the take's audio at the end.
    //
    // If there is no main model yet (first-time record), fall through with
    // the default RecordReplaceSession behaviour.

    bool haveReference = (getMainModel() != nullptr);

    // A new take starts with no latency compensation, whichever kind of take
    // it is: the live dots are drawn with these, and a standalone take must
    // not inherit the shift of a singing take made before it.  The figures
    // for a singing take are computed in recordingStarted().  This is below
    // the early return above on purpose: a Stop must leave them alone, the
    // splice on Stop needs them.
    m_recordingLatencyFrames = 0;
    m_recordingStartGapEstimate = 0;
    m_awaitingReferenceStart = false;
    m_recordingStartGapMeasured = -1;
    m_takeLatency = TakeLatency();

    if (haveReference) {

        // The recording goes into the take at the playback position.  It
        // has to be read before the base class call, which centres the view
        // on frame 0, and while we are not recording yet: once we are,
        // ViewManager reports the duration of the take instead.
        sv_frame_t position = m_viewManager ? m_viewManager->getPlaybackFrame() : 0;
        if (position < 0) position = 0;

        // Record into Selection: the selection is what is recorded, so it
        // says where the take starts and where it stops, and the playhead
        // only picks which selection that is.  With none selected this is
        // an ordinary recording from the playhead.  The audio check's
        // takes always record into the selection it makes.
        sv_frame_t end = -1;
        bool intoSelection = m_audioCheckTakes ||
            (m_recordIntoSelection && m_recordIntoSelection->isChecked());
        if (intoSelection && m_viewManager) {
            Coverage::Ranges selected;
            for (const Selection &s : m_viewManager->getSelections()) {
                if (!s.isEmpty()) {
                    selected.push_back(Coverage::Range(s.getStartFrame(),
                                                       s.getEndFrame()));
                }
            }
            Coverage::Range chosen;
            if (TakeTiming::chooseRange(selected, position, chosen)) {
                position = chosen.start;
                end = chosen.end;
                cerr << "MainWindow::record: recording into the selection ["
                     << position << "," << end << ")" << endl;
            }
        }

        // Recording from inside singing that is already there replaces it
        // from that point on.  The user is asked first, unless they have
        // said not to be: undo can bring it back.  Not asked when
        // recording into a selection: making the selection was the answer,
        // and the end of the recording is known there (spec 5.1).
        if (end < 0 && m_takes->shouldConfirmRecordingAt(position) &&
            !confirmRecordingOverTake()) {
            cerr << "MainWindow::record: recording over the existing singing "
                 << "was declined" << endl;
            return;
        }

        m_takePosition = position;
        m_takeEnd = end;
        m_takePreRoll = TakeTiming::preRollBefore(position,
                                                  wantedPreRollFrames());

        cerr << "MainWindow::record: recording into the singing track from "
             << "frame " << position << ", with a lead-in of "
             << m_takePreRoll << " frames" << endl;

        // Dots and a tracker of a take whose analysis never finished
        if (m_realtimePitchTracker || m_realtimePitchLayer) {
            cerr << "MainWindow::record: tearing down leftover realtime pitch layer" << endl;
            teardownRealtimePitchLayer();
        }

        // Likewise a recording that was never spliced into the take
        if (m_recordingLayer || !m_currentRecordingModelId.isNone()) {
            cerr << "MainWindow::record: releasing a recording left over from "
                 << "a take that did not finish" << endl;
            teardownRecordingLayer();
        }

        // The singing track itself stays as it is: its pitch and notes
        // are what the singer is adding to.  Its audio is kept out of the
        // mix (spec 5.1), and its pitch and notes out of sight -- see
        // updateSingingTrackForTake(), called below once the device has
        // either started or failed to.
        muteSingingAudioForTake();

        m_pendingSingingModelId = {};
        m_recordingInProgress = false;

        m_recordingAsSingingTrack = true;
        // The recording is only to be added to the document as a model:
        // the pane, the layer and the "Import Recorded Audio" undo entry
        // that RecordCreateAdditionalModel makes for it are all things we
        // would delete at once, leaving a command on the undo stack whose
        // pane has gone.  The pane count is still remembered, so that the
        // pruning code finds nothing to do rather than being taken out
        m_paneCountBeforeRecording = m_paneStack ? m_paneStack->getPaneCount() : 0;
        setAudioRecordMode(RecordCreateUnshownModel);
    } else {
        m_recordingAsSingingTrack = false;
        m_takePosition = 0;
        m_takePreRoll = 0;
        m_takeEnd = -1;
        m_paneCountBeforeRecording = 0;
        setAudioRecordMode(RecordReplaceSession);
    }

    // While recording, the playback cursor is where the take's playback
    // started plus what has been recorded: the views follow the take from
    // where it is being made, not from frame 0.  With a pre-roll that is
    // the start of the lead-in, so the cursor runs through the lead-in in
    // step with the reference.
    sv_frame_t playbackStart = currentTakeTiming().playbackStart();
    if (m_viewManager) m_viewManager->setRecordStartFrame(playbackStart);

    MainWindowBase::record();

    // The base class gives up without a signal when the device cannot be
    // opened or the recording cannot be started.  No take is coming then,
    // so nothing must be left waiting for one: with the flag still set,
    // Analyse Now would be routed to a singing track that is not there,
    // and the reference would not be re-analysed.  (Opening a file is not
    // affected: that closes the session, which clears the flag.)
    if (!m_recordTarget || !m_recordTarget->isRecording()) {
        cerr << "MainWindow::record: recording did not start" << endl;
        m_recordingAsSingingTrack = false;
        m_recordingInProgress = false;
        restoreSingingAudioAfterTake();
    }

    // The base class centres the view on frame 0.  The take is being
    // recorded from where playback was — the start of the lead-in when
    // there is one — and that is where the singer is watching (spec
    // section 4).
    if (m_recordingAsSingingTrack && m_viewManager) {
        m_viewManager->setPlaybackFrame(playbackStart);
        m_viewManager->setGlobalCentreFrame(playbackStart);
    }

    updateAlternatePitchForTake();
    updateSingingTrackForTake();
    updateLayerStatuses();

    // Restore the default mode so that a subsequent "standalone" recording
    // (after the singing track session is closed) behaves correctly.
    setAudioRecordMode(RecordReplaceSession);

    if (m_recordingAsSingingTrack) {

        // Give the recording a layer of our own to hold it in the document,
        // and then remove the extra pane that AddPaneCommand made for it.
        // The order matters: that pane's imported waveform layer is the
        // only thing referencing the live WritableWaveFileModel until our
        // layer is there, and deleting it first would have
        // Document::releaseModel() destroy the model mid-capture.
        setupRecordingLayer();

        if (m_paneStack) {
            while (m_paneStack->getPaneCount() > m_paneCountBeforeRecording) {
                Pane *extra = m_paneStack->getPane(m_paneStack->getPaneCount() - 1);
                if (!extra) break;

                if (m_recordingLayer) {
                    pruneExtraPane(extra, m_currentRecordingModelId);
                    continue;
                }

                // Nothing of ours holds the model, so the pane's own layer
                // has to keep it alive until the take is over.  Hiding the
                // pane takes it out of the visible list while leaving the
                // widget alive with valid Document::m_layerViewMap entries:
                // a pane must not be deleted while a layer of its own is
                // still in that map (see "Extra-pane Pruning" in the dev
                // doc).  teardownRecordingLayer() prunes it at the end.
                if (m_overview) m_overview->unregisterView(extra);
                m_paneStack->hidePane(extra);
                m_pendingExtraPanes.push_back(extra);

                cerr << "MainWindow::record: no layer for the recording; "
                     << "keeping its pane " << extra << " hidden for the take"
                     << endl;
            }
        }

        // A take with an end of its own to reach is watched until it
        // gets there
        startTakePolling();
    }
}

void
MainWindow::recordPressed()
{
    // The check starts and stops its takes through record() itself, and
    // a press would stop its take early, or start one of the user's in
    // the check's session.  The button is shut while it runs; a press
    // that arrives all the same (queued before it was shut, say) leaves
    // the button showing what is really happening
    if (audioCheckRunning()) {
        cerr << "MainWindow::recordPressed: the audio check is running; "
             << "Record is ignored" << endl;
        if (m_recordAction) {
            m_recordAction->setChecked
                (m_recordTarget && m_recordTarget->isRecording());
        }
        return;
    }
    record();
}

bool
MainWindow::audioCheckRunning() const
{
    if (m_audioCheck && m_audioCheck->isRunning()) return true;
#ifdef TONY_DEV_CHECKS
    if (m_devChecks && m_devChecks->isRunning()) return true;
#endif
    return false;
}

void
MainWindow::startTakePolling()
{
    // Only a take that is to stop by itself needs watching: nothing else
    // about a take is decided while it runs
    if (!m_takeTimer) return;
    if (!m_recordTarget || !m_recordTarget->isRecording()) return;
    if (!currentTakeTiming().havePunchOut()) return;

    m_takeTimer->start();
}

void
MainWindow::stopTakePolling()
{
    if (m_takeTimer) m_takeTimer->stop();
}

void
MainWindow::pollTakeProgress()
{
    // The take may have ended, or the session gone, between one poll and
    // the next: there is nothing to watch then
    if (!m_recordTarget || !m_recordTarget->isRecording() ||
        !m_recordingAsSingingTrack) {
        stopTakePolling();
        return;
    }

    // The latency may have been refined since the last poll, which moves
    // the end of the take with it: currentTakeTiming() has the figure as
    // it stands now
    TakeTiming timing = currentTakeTiming();
    sv_frame_t received = m_recordTarget->getFramesReceived();

    // Everything up to the end of the selection has been sung and
    // recorded; what comes after it would not be used anyway
    if (timing.shouldStopAt(received)) {
        cerr << "MainWindow::pollTakeProgress: the singing up to frame "
             << timing.end << " has arrived (" << received
             << " frames recorded): stopping the take" << endl;
        // The same path as the Stop button, which stops the timer
        record();
    }
}

bool
MainWindow::showTakeCountdown() const
{
    // While the lead-in of a pre-roll runs, the status bar counts it down
    // instead of saying where playback is or how much has been recorded:
    // what is coming in does not count yet.
    //
    // Everything that writes the status bar during a take has to come
    // through here, because they all write often — the recorded duration
    // every 10 ms, the playback position every 20 ms, the visible range
    // whenever the view scrolls after the cursor — and anything written
    // between two of those would be gone before it could be read.  (The
    // live dots write it too, and need no help: none is drawn during the
    // lead-in.)
    if (!m_recordingAsSingingTrack || m_takePreRoll <= 0 || !m_recordTarget) {
        return false;
    }

    QString countdown = currentTakeTiming().countdownText
        (m_recordTarget->getFramesReceived());
    if (countdown == "") return false;

    m_myStatusMessage = countdown;
    getStatusLabel()->setText(countdown);
    return true;
}

void
MainWindow::recordDurationChanged(sv_frame_t frame, sv_samplerate_t rate)
{
    if (showTakeCountdown()) return;
    MainWindowBase::recordDurationChanged(frame, rate);
}

void
MainWindow::playbackFrameChanged(sv_frame_t frame)
{
    if (showTakeCountdown()) return;
    MainWindowBase::playbackFrameChanged(frame);
}

void
MainWindow::recordingStarted()
{
    // recordStatusChanged(bool) is emitted both when recording starts
    // (true) and stops (false). We only want to act when it starts.
    if (!m_recordTarget) return;
    if (!m_recordTarget->isRecording()) {
        // Recording stopped - recordingFinishedFull() is called from
        // analyseNow() once pYIN completes.  The reference pitch track
        // comes back now, though: the singer has stopped following, and
        // so do the take's own pitch and notes.
        updateAlternatePitchForTake();
        updateSingingTrackForTake();
        updateLayerStatuses();
        return;
    }

    cerr << "MainWindow::recordingStarted: scheduling realtime pitch layer setup" << endl;
    m_recordingInProgress = true;

    // TIMING: recordStatusChanged(true) is emitted from within
    // AudioCallbackRecordTarget::startRecording(), which is called by
    // MainWindowBase::record() BEFORE setMainModel() and BEFORE
    // emit audioFileLoaded() -> analyseNewMainModel() creates the panes.
    // If we call setupRealtimePitchLayer() directly here, m_paneStack will
    // have zero panes and the setup will silently bail out.
    //
    // Fix: defer via QTimer::singleShot(0) so the slot runs on the next
    // event-loop iteration, by which time record() has finished completely
    // (including emit audioFileLoaded() -> panes created).
    QTimer::singleShot(0, this, [this]() {
        if (!m_recordingInProgress) {
            return;
        }
        cerr << "MainWindow::recordingStarted (deferred): setting up realtime pitch layer" << endl;
        setupRealtimePitchLayer();

        // If the "play reference while recording" toggle is on, start
        // playback from where the take is being recorded, so the singer
        // hears the reference from there.  The audio IO was already
        // resumed by record() so m_playSource can be started directly
        // without calling MainWindowBase::play() (which would stop
        // recording if isRecording() is true).  The audio check's takes
        // always play it: they measure where it arrives.
        bool playReference = m_audioCheckTakes ||
            (m_playRefWhileRecording && m_playRefWhileRecording->isChecked());
        if (m_recordingAsSingingTrack && playReference &&
            m_playSource && !m_playSource->isPlaying()) {
            cerr << "MainWindow::recordingStarted: starting reference playback" << endl;

            // Measure round-trip hardware latency so we can compensate the
            // singing recording's timeline after the take.
            // output latency = time from play() call until audio exits the speaker
            // input latency  = time from sound entering the mic until it arrives here
            // The singer's response to the reference where playback starts
            // arrives in the recording at approximately frame
            // (outputLatency + inputLatency), so that is the frame the splice
            // reads the recording from.  Drivers often report those two
            // wrong; a round trip the audio check measured on this device
            // is used instead, while there is one (roundTripAt()).
            // With a pre-roll, playback starts at the beginning of the
            // lead-in rather than at the take's position, and the splice
            // skips the lead-in as well (TakeTiming::spliceOffset()).
            sv_frame_t playbackStart = currentTakeTiming().playbackStart();

            sv_frame_t outputLatency = m_playSource->getTargetPlayLatency();
            sv_frame_t inputLatency  = m_recordTarget ? m_recordTarget->getSystemRecordLatency() : 0;
            //
            // The take is already running by now: record() started it, and
            // this lambda runs an event-loop turn (and a layer setup) later.
            // Whatever the device delivers before the reference starts sits
            // at the front of the take, ahead of reference frame 0, so it is
            // part of the shift as well.  What it has delivered so far is
            // only an estimate of that, because the play source takes a
            // little while to start; the audio callback reports the real
            // figure, and refineRecordingLatency() picks it up.
            m_recordingStartGapEstimate =
                m_recordTarget ? m_recordTarget->getFramesReceived() : 0;

            // The round trip is taken off the front of the recording, so it
            // is counted in frames of the recording, at the device's rate.
            // The reported latencies are not both counted so: the play
            // source usually has the output latency in frames of the
            // session.  They differ when the device is not at 44.1 kHz, so
            // the round trip goes through seconds
            sv_samplerate_t recordingRate = 0;
            if (auto wfm = ModelById::getAs<WritableWaveFileModel>
                (m_currentRecordingModelId)) {
                recordingRate = wfm->getSampleRate();
            }
            if (recordingRate <= 0) {
                recordingRate = sessionRate();
                cerr << "MainWindow::recordingStarted: the recording's rate "
                     << "is not known; taking the session's, "
                     << recordingRate << " Hz" << endl;
            }
            m_lastRecordingRate = recordingRate;

            LatencyCalibration::InUse inUse = roundTripAt(recordingRate);

            // A check that brings a round trip of its own places its takes
            // with that, for the run only (the dev checks, with the figure
            // the calibration before them measured): nothing is stored,
            // and latencyInUse() goes on describing roundTripAt()'s.  The
            // reported pair stays the device's
            const bool checkOwn =
                m_audioCheckTakes && m_audioCheckRoundTrip >= 0.0;
            if (checkOwn) inUse.roundTrip = m_audioCheckRoundTrip;

            sv_frame_t roundTrip =
                LatencyCalibration::toFrames(inUse.roundTrip, recordingRate);
            m_recordingLatencyFrames = roundTrip + m_recordingStartGapEstimate;

            m_takeLatency.roundTrip = roundTrip;
            m_takeLatency.reportedOutput = inUse.reportedOutput;
            m_takeLatency.reportedInput = inUse.reportedInput;
            m_takeLatency.measured = checkOwn ||
                (inUse.source == LatencyCalibration::Source::Measured);
            m_takeLatency.startGap = m_recordingStartGapEstimate;
            m_takeLatency.startGapMeasured = false;
            cerr << "MainWindow::recordingStarted: round trip " << roundTrip
                 << " frames at " << recordingRate << " Hz ("
                 << inUse.roundTrip * 1000.0 << " ms), ";
            if (checkOwn) {
                cerr << "the audio check's own, for its run only";
            } else {
                cerr << LatencyCalibration::sourceName(inUse.source);
                if (inUse.source == LatencyCalibration::Source::Measured) {
                    cerr << " on "
                         << inUse.date.toString(Qt::ISODate).toStdString();
                } else if (inUse.stale) {
                    cerr << ": the measured one is stale, the device reports "
                         << "other latencies now";
                }
            }
            cerr << "; output latency=" << outputLatency
                 << " input latency=" << inputLatency
                 << " estimated start gap=" << m_recordingStartGapEstimate
                 << " total compensation=" << m_recordingLatencyFrames << " frames" << endl;

            m_recordingStartGapMeasured = -1;
            m_awaitingReferenceStart = true;

            liftPlaySelectionForTake();
            m_viewManager->setPlaybackFrame(playbackStart);
            m_playSource->play(playbackStart);
        }

        updateLayerStatuses();
        updateMenuStates();
    });
}

void
MainWindow::refineRecordingLatency()
{
    sv_frame_t measured = m_recordingStartGapMeasured;
    if (measured < 0) return;

    // Measured, whether or not the estimate was right: the audio check
    // reports for each take which of the two it was placed with.  Kept
    // here, on the GUI thread, and not by the audio callback that
    // measures it
    m_takeLatency.startGap = measured;
    m_takeLatency.startGapMeasured = true;

    if (measured == m_recordingStartGapEstimate) return;
    cerr << "MainWindow::refineRecordingLatency: start gap was " << measured
         << " frames, not the estimated " << m_recordingStartGapEstimate << endl;
    m_recordingLatencyFrames = currentRecordingLatency();
    m_recordingStartGapEstimate = measured;
}

sv_frame_t
MainWindow::currentRecordingLatency() const
{
    // Reading the measurement without taking it: pollTakeProgress() wants
    // the latest figure every time it looks, but making it the stored one
    // is onRealtimePitchDetected()'s business — it throws the dots placed
    // with the estimate away when the figure changes.
    sv_frame_t measured = m_recordingStartGapMeasured;
    if (measured < 0) return m_recordingLatencyFrames;
    return m_recordingLatencyFrames + (measured - m_recordingStartGapEstimate);
}

sv_samplerate_t
MainWindow::sessionRate() const
{
    sv_samplerate_t rate = m_playSource ? m_playSource->getSourceSampleRate() : 0;
    if (rate <= 0) rate = Preferences::getInstance()->getFixedSampleRate();
    return rate;
}

LatencyCalibration::InUse
MainWindow::roundTripAt(sv_samplerate_t recordingRate) const
{
    // The play source counts its output latency in frames at the rate it
    // was told the device runs at, as its own getCurrentPlayingFrame()
    // does.  bqaudioio's ResamplerWrapper tells it the session's rate and
    // converts the device's figure to it, when the session had a rate by
    // the time the device was opened.  If it had none yet (a device chosen
    // before any file was opened), the wrapper passed the figure on as the
    // device counts it and told the play source 0: the recording's frames
    sv_samplerate_t outputRate =
        m_playSource ? m_playSource->getDeviceSampleRate() : 0;
    if (outputRate <= 0) outputRate = recordingRate;

    double output = m_playSource ?
        LatencyCalibration::reportedSeconds
        (m_playSource->getTargetPlayLatency(), outputRate) : 0.0;
    double input = m_recordTarget ?
        LatencyCalibration::reportedSeconds
        (m_recordTarget->getSystemRecordLatency(), recordingRate) : 0.0;

    QSettings settings;
    LatencyCalibration::Figure figure;
    bool stored = LatencyCalibration::load
        (settings, LatencyCalibration::currentKey(settings, recordingRate),
         figure);
    return LatencyCalibration::roundTripInUse
        (stored ? &figure : nullptr, output, input);
}

sv_samplerate_t
MainWindow::expectedRecordingRate() const
{
    return m_lastRecordingRate > 0 ? m_lastRecordingRate : sessionRate();
}

LatencyCalibration::InUse
MainWindow::latencyInUse() const
{
    return roundTripAt(expectedRecordingRate());
}

bool
MainWindow::storeMeasuredLatency(const AudioCheckResult &result)
{
    if (!result.calibrationUsable()) return false;

    LatencyCalibration::Figure figure;
    figure.roundTrip = result.calibratedRoundTrip;
    figure.spread = result.summary.spread;
    figure.date = QDateTime::currentDateTimeUtc();
    figure.reportedOutput = result.reportedOutputLatency;
    figure.reportedInput = result.reportedInputLatency;

    // Under the devices the check ran on, which the Preferences may no
    // longer name: the result can be on show long after the run
    LatencyCalibration::Key key = result.key;
    key.rate = result.recordingRate;

    QSettings settings;
    LatencyCalibration::store(settings, key, figure);
    cerr << "MainWindow::storeMeasuredLatency: round trip "
         << figure.roundTrip * 1000.0 << " ms at " << key.rate
         << " Hz, the device reporting " << figure.reportedOutput * 1000.0
         << " ms out and " << figure.reportedInput * 1000.0 << " ms in"
         << endl;
    updateLatencyMenuLine();
    return true;
}

void
MainWindow::forgetMeasuredLatency()
{
    QSettings settings;
    LatencyCalibration::forget
        (settings, LatencyCalibration::currentKey(settings,
                                                  expectedRecordingRate()));
    cerr << "MainWindow::forgetMeasuredLatency: at "
         << expectedRecordingRate() << " Hz" << endl;
    updateLatencyMenuLine();
}

void
MainWindow::updateLatencyMenuLine()
{
    if (!m_latencyLineAction || !m_forgetLatencyAction) return;
    LatencyCalibration::InUse inUse = latencyInUse();
    m_latencyLineAction->setText
        (tr("Latency: %1").arg(CalibrateAudioDialog::describeLatency(inUse)));

    // A stale figure is kept too (it applies again if the device goes
    // back to its old buffers), and can be forgotten like any other
    m_forgetLatencyAction->setEnabled
        (inUse.source == LatencyCalibration::Source::Measured || inUse.stale);
}

void
MainWindow::calibrateAudio()
{
    if (!m_calibrateAudioDialog) {
        m_calibrateAudioDialog = new CalibrateAudioDialog(this, m_audioCheck);
#ifdef TONY_DEV_CHECKS
        m_calibrateAudioDialog->setDevChecks(m_devChecks);
#endif
    }
    m_calibrateAudioDialog->present();
}

TakeTiming
MainWindow::currentTakeTiming() const
{
    TakeTiming timing;
    auto model = getMainModel();
    timing.rate = model ? model->getSampleRate() : 0;
    timing.position = m_takePosition;
    timing.end = m_takeEnd;
    timing.preRoll = m_takePreRoll;
    timing.latency = currentRecordingLatency();
    return timing;
}

sv_frame_t
MainWindow::wantedPreRollFrames() const
{
    double seconds = 0.0;
    if (m_audioCheckTakes) {
        // The audio check's takes have a lead-in of their own
        seconds = m_audioCheckPreRoll;
    } else {
        if (!m_preRoll || !m_preRoll->isChecked()) return 0;
        QSettings settings;
        settings.beginGroup("MainWindow");
        seconds = settings.value("prerollseconds", 3.0).toDouble();
        settings.endGroup();
    }
    if (seconds <= 0.0) return 0;

    auto model = getMainModel();
    sv_samplerate_t rate = model ? model->getSampleRate() : 0;
    if (rate <= 0) return 0;

    return sv_frame_t(seconds * rate);
}

void
MainWindow::onRealtimePitchDetected(sv::sv_frame_t frame, double hz)
{
    // Called on the GUI thread via Qt::QueuedConnection (RealtimePitchTracker
    // emits from its background thread).  Write the point into the model here
    // so all model mutations stay on the GUI thread.
    //
    // Events still queued when the take ended arrive here as well. The
    // dots may still be on show then, waiting for pYIN, but the take is
    // over: leave them, and the status bar, alone.
    if (!m_recordingInProgress) return;

    // Draw the dot where the finished pitch track will put this sound: the
    // take is spliced into the singing track from m_takePosition on, with
    // the recording latency and the lead-in taken off its front.  The first
    // dots may have been placed using the estimate of the start gap. They
    // all belong to sound from before the reference started, which has no
    // place on the reference's timeline.
    sv_frame_t latencyBefore = m_recordingLatencyFrames;
    refineRecordingLatency();
    auto m = ModelById::getAs<SparseTimeValueModel>(m_realtimePitchModelId);
    if (m && m_recordingLatencyFrames != latencyBefore) {
        for (const Event &e : m->getAllEvents()) m->remove(e);
    }

    // A negative answer is sound sung during the lead-in of a pre-roll, or
    // before the reference started at all: no dot for it
    sv_frame_t intoTake = currentTakeTiming().liveFrameIntoTake(frame);
    if (intoTake < 0) return;
    sv_frame_t dotFrame = m_takePosition + intoTake;

    if (m) {
        m->add(Event(dotFrame, float(hz), tr("")));
        m_realtimeDotsNotifier.changed
            (dotFrame, dotFrame + RealtimePitchTracker::kHopSize);
    }

    // Convert Hz to MIDI note number and cents deviation.
    // MIDI note 69 = A4 = 440 Hz.
    double midiNote = 12.0 * std::log2(hz / 440.0) + 69.0;
    int nearestNote = int(std::round(midiNote));
    int cents = int(std::round((midiNote - nearestNote) * 100.0));

    // Note names (no flats — sharps only for display simplicity).
    static const char *noteNames[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    int noteIndex = ((nearestNote % 12) + 12) % 12;
    int octave    = (nearestNote / 12) - 1;
    QString noteName = QString("%1%2").arg(noteNames[noteIndex]).arg(octave);

    QString centsStr;
    if (cents == 0) {
        centsStr = tr("in tune");
    } else if (cents > 0) {
        centsStr = tr("+%1 cents").arg(cents);
    } else {
        centsStr = tr("%1 cents").arg(cents);
    }

    getStatusLabel()->setText(
        tr("Recording — singing: %1 (%2 Hz, %3)")
        .arg(noteName)
        .arg(hz, 0, 'f', 1)
        .arg(centsStr));
}

void
MainWindow::recordingFinishedFull(Analyser *analysing)
{
    // Called from analyseNow() once the pYIN analysis of the newly
    // recorded audio has been started.  The take is over, so the tracker
    // goes now.  The coarse realtime pitch layer (the "live" orange dots)
    // stays on show until the analyser passed in reports that the full
    // pYIN pitch track is there to replace it.  With no analyser (the
    // analysis could not be started) there is nothing to wait for.
    cerr << "MainWindow::recordingFinishedFull: take finished" << endl;
    stopTakePolling();
    m_recordingInProgress = false;
    m_recordingAsSingingTrack = false;
    m_currentRecordingModelId = {};
    restoreSingingAudioAfterTake();
    updateSingingTrackForTake();

    if (analysing && m_realtimePitchLayer) {
        stopRealtimePitchTracker();
        if (m_realtimeLayerTeardownConnection) {
            disconnect(m_realtimeLayerTeardownConnection);
        }
        m_realtimeLayerTeardownConnection =
            connect(analysing, &Analyser::initialAnalysisCompleted,
                    this, [this]() {
                        cerr << "MainWindow: pYIN done, removing realtime pitch layer" << endl;
                        teardownRealtimePitchLayer();
                    });
    } else {
        teardownRealtimePitchLayer();
    }

    // Stop reference playback that was started for the singer's benefit.
    // Suspend the audio IO so it doesn't keep consuming CPU while idle.
    if (m_playSource && m_playSource->isPlaying()) {
        cerr << "MainWindow::recordingFinishedFull: stopping reference playback" << endl;
        m_playSource->stop();
        if (m_audioIO) m_audioIO->suspend();
        else if (m_playTarget) m_playTarget->suspend();
    }
    restorePlaySelectionAfterTake();

    updateLayerStatuses();
    updateMenuStates();
}

void
MainWindow::finishSingingTake()
{
    // The take has stopped, and what the device recorded is raw material:
    // it goes into the take's audio file at the position the take was
    // started from, with the latency and the lead-in taken off its front,
    // and the singing track is then shown and analysed from the file that
    // comes out -- the new audio under the pitch and notes layers that
    // are there, with only the range that changed analysed, which is what
    // makes this quick on a long song.

    stopTakePolling();
    refineRecordingLatency();

    TakeTiming timing = currentTakeTiming();
    sv_frame_t offset = timing.spliceOffset();
    sv_frame_t length = timing.spliceLength();
    sv_frame_t position = m_takePosition;

    QString recordingPath;
    sv_frame_t recorded = 0;
    if (auto wfm = ModelById::getAs<WritableWaveFileModel>
        (m_currentRecordingModelId)) {
        recordingPath = wfm->getLocation();
        recorded = wfm->getFrameCount();
        m_takeLatency.recordingRate = wfm->getSampleRate();
    }

    // The take is over: the tracker goes first, so that the recording's
    // model is never released under it, and then the model itself.
    // AudioCallbackRecordTarget::stopRecording() has flushed the buffers
    // and called writeComplete(), and releasing the model closes what is
    // left, so the file is whole before the splice reads it.
    stopRealtimePitchTracker();
    m_recordingInProgress = false;
    m_recordingAsSingingTrack = false;
    teardownRecordingLayer();

    // The playhead goes back to where the take started: Play then hears
    // what was just sung, and Record again records the same part.  (While
    // recording, ViewManager keeps the playback frame at the duration of
    // the take, so it is somewhere else by now.)
    if (m_viewManager) m_viewManager->setPlaybackFrame(position);

    // A take stopped the moment it was started, or one no longer than the
    // latency and the lead-in together, has nothing in it to add.  Nothing
    // has gone wrong; there is simply nothing to do
    if (recordingPath != "" && recorded <= offset) {
        cerr << "MainWindow::finishSingingTake: nothing to use: " << recorded
             << " frames recorded, the first " << offset
             << " of which are the latency and the lead-in" << endl;
        recordingFinishedFull(nullptr);
        return;
    }

    QString error;
    Coverage::Range placed;

    // What an undo of this recording has to put back
    QString pathBefore = m_takes->getAudioPath();
    Coverage coverageBefore = m_takes->getCoverage();

    if (recordingPath == "") {
        error = tr("The recording is no longer there to be used");
    } else {
        QString directory = takeAudioDirectory();
        if (directory == "") {
            error = tr("Could not find a directory to write the singing "
                       "track into");
        } else {
            // Everything before the offset is sound from before the singer
            // could have heard the reference at the take's position: the
            // round trip, and the lead-in of a pre-roll before it.  The
            // length is what a punch-out allows, or all there is
            error = m_takes->spliceRecording(recordingPath, offset, position,
                                             length, directory, &placed);
        }
    }

    if (error != "") {
        QMessageBox::warning
            (this,
             tr("Failed to add the recording to the singing track"),
             tr("<b>The recording could not be added to the singing track</b>"
                "<p>%1</p>").arg(error),
             QMessageBox::Ok);
        // The singing track is as it was, and the recording is still in
        // the record directory; there is nothing for the dots to wait for
        recordingFinishedFull(nullptr);
        return;
    }

    cerr << "MainWindow::finishSingingTake: " << recorded << " frames "
         << "recorded, used from frame " << offset << ", placed at ["
         << placed.start << "," << placed.end << ") of "
         << m_takes->getAudioPath() << endl;

    // The command of this recording, made before the audio is shown and
    // added to the history after: a range short enough is analysed and
    // merged before the rebuild returns, and the merge has to find the
    // command it belongs to.  Any command still waiting for an analysis is
    // closed first -- the rebuild below folds that range into its own run,
    // so the merge it was waiting for is never coming
    closeOpenTakeCommand(false);
    SingingTakeCommand *command =
        new SingingTakeCommand(this, tr("Record Singing"),
                               pathBefore, coverageBefore,
                               m_takes->getAudioPath(), m_takes->getCoverage());
    m_openTakeCommand = command;

    bool analysing = rebuildSingingTrackFromTake(placed);

    if (analysing) {
        // The events of this run are not in the command yet, and an undo
        // pressed before they are abandons the run: a redo has to make it
        // again, over the range the run really covers
        command->setPendingAnalysis(m_takeAnalysisRange);
    } else if (m_openTakeCommand == command) {
        // Nothing was analysed, or it failed to start: nothing to wait for
        m_openTakeCommand = nullptr;
    }

    addTakeCommand(command);

    // The coverage has changed whether or not the new audio could be
    // shown, and the strip says what it is now.  After the rebuild, not
    // before: a swap puts the pane's selected layer back as it found it,
    // and the strip is never to be that
    syncCoverageStrip();

    // The dots stay until the analysis that replaces them is done
    recordingFinishedFull(analysing ? m_analyser2 : nullptr);
}

bool
MainWindow::rebuildSingingTrackFromTake(const Coverage::Range &placed)
{
    if (!m_takes->haveTake()) return false;

    // What the analysis that follows has to cover: the range the splice
    // has just written, and any range whose analysis is still running.
    // That one's result is lost -- the swap below releases the models it
    // is being written into, and its analyser with them -- so it is
    // analysed again rather than left half done
    Coverage::Range analyse = placed;
    if (m_analyser2 && m_analyser2->isAnalysingRange() &&
        m_takeAnalysisRange.length() > 0) {
        analyse.start = std::min(analyse.start, m_takeAnalysisRange.start);
        analyse.end = std::max(analyse.end, m_takeAnalysisRange.end);
    }
    m_takeAnalysisRange = Coverage::Range();

    QString path = m_takes->getAudioPath();

    // A take with pitch and notes on show keeps them: the new audio goes
    // underneath, and only the range that changed is analysed.  The
    // first recording of a take has neither, so its audio is opened as a
    // singing track with no analysis of its own and given empty ones
    bool haveLayers = m_analyser2 &&
        m_analyser2->getLayer(Analyser::PitchTrack) &&
        m_analyser2->getLayer(Analyser::Notes);

    QString error = (haveLayers ? swapSingingAudio(path)
                     : loadTakeAudio(path));

    if (error == "" && m_analyser2) {
        error = m_analyser2->addEmptyAnalyses();

        // The first recording of a take has just been given its layers:
        // they are named after it, and they go above the takes that are
        // put away, where the note tool looks for the layer to act on
        nameActiveTakeLayers();
        raiseActiveTakeLayers();
    }

    if (error != "") {
        // The recording is in the take's audio file and the take's
        // coverage says so, but the screen does not: say so rather than
        // leave the two quietly disagreeing
        QMessageBox::warning
            (this,
             tr("Failed to show the recording"),
             tr("<b>The recording was added to the singing track, but it "
                "could not be shown</b><p>%1</p><p>What is on screen is the "
                "singing track as it was. The recording is in the take's "
                "audio file, \"%2\".</p>").arg(error).arg(path),
             QMessageBox::Ok);
        return false;
    }

    return startTakeAnalysis(analyse.start, analyse.end);
}

QString
MainWindow::loadTakeAudio(QString path)
{
    // The take's audio, opened and shown with no analysis of its own:
    // what is analysed is the range a recording has just gone into, and
    // the pitch and notes of the rest of the take are either in the pane
    // already or about to be made empty.  swapSingingAudio() is this for
    // a take that has its layers; the two are the same but for the
    // handing over
    ModelId audio;
    FileOpenStatus status = openTakeAudioFile(path, audio);

    if (status != FileOpenSucceeded || audio.isNone()) {
        return tr("The file \"%1\" could not be opened as audio").arg(path);
    }

    // Layers of the take that no analyser owns are handed to the one
    // about to be made, as after a session restore
    adoptTakeLayers(audio);

    // m_rebuildingTakeAudio: the coverage of this file is the one the
    // splice worked out, not "the whole of it"
    bool wasRebuilding = m_rebuildingTakeAudio;
    m_rebuildingTakeAudio = true;
    setupSingingTrackAnalyser(audio, true);
    m_rebuildingTakeAudio = wasRebuilding;

    if (!m_analyser2) {
        // The audio model is left registered with the document, which
        // releases it when it goes: Document::releaseModel() is the
        // document's own business, and holding a model nothing shows
        // costs only memory
        return tr("The singing track could not be set up on \"%1\"").arg(path);
    }

    return "";
}

bool
MainWindow::adoptTakeLayers(ModelId audio)
{
    // An analyser claims a pitch or notes layer whose model has the
    // analyser's own audio model as its source model
    // (Analyser::claimExistingAnalyses()).  The layers of a take that
    // has had audio swapped under it have no such link: the model they
    // were derived from is long gone, and a session file keeps them as
    // ordinary layers.  So the link is made here, for the layers of the
    // active take, just before the analyser that is to claim them is made.
    Pane *pane = m_paneStack ? m_paneStack->getPane(0) : nullptr;
    if (!pane || audio.isNone()) return false;

    QString takeName = m_takes->getActiveName();

    // By name, which is what says whose a take's layers are (spec 6.4).
    // The layers of the takes that are put away have no live source model
    // either, so nothing but the name can tell them apart.  (Before 7b
    // there was a fallback for a session whose takes had no names of their
    // own; a session that does not name its takes now opens without them.)
    TakeLayers::Found found = TakeLayers::find(pane, takeName);
    TimeValueLayer *pitch = found.pitch;
    FlexiNoteLayer *notes = found.notes;

    // Half a pair is no use: the analyser claims both or neither
    if (!pitch || !notes) return false;

    cerr << "MainWindow::adoptTakeLayers: the take's pitch and notes layers "
         << "come from model " << audio << " now" << endl;

    for (ModelId id : { pitch->getModel(), notes->getModel() }) {
        if (auto model = ModelById::get(id)) {
            model->setSourceModel(audio);
        }
    }

    return true;
}

bool
MainWindow::startTakeAnalysis(sv_frame_t start, sv_frame_t end)
{
    if (!m_analyser2 || end <= start) return false;

    // How far the analysis may reach for the context it needs: as far as
    // the material the take has, and no further.  The margin may run
    // into silence that has been recorded over, but silence that was
    // never recorded tells pYIN nothing and is not ours to analyse
    const Coverage &coverage = m_takes->getCoverage();
    sv_frame_t clipStart = start, clipEnd = end;
    Coverage::Range at;
    if (coverage.getRangeAt(start, at)) clipStart = at.start;
    if (coverage.getRangeAt(end - 1, at)) clipEnd = at.end;

    QString error = m_analyser2->analyseRange(start, end, clipStart, clipEnd);

    if (error != "") {
        QMessageBox::warning
            (this,
             tr("Failed to analyse the recording"),
             tr("<b>The singing could not be analysed</b><p>%1</p>").arg(error),
             QMessageBox::Ok);
        return false;
    }

    // A range short enough to have been analysed and merged before the
    // call returned leaves nothing to wait for
    if (!m_analyser2->isAnalysingRange()) return false;

    m_takeAnalysisRange = Coverage::Range(start, end);

    // Erasing is not to be had while this runs
    updateMenuStates();
    return true;
}

bool
MainWindow::analyseTakeCoverage()
{
    // Analyse Now takes in the singing of the take as well (spec 7): all
    // of its coverage is analysed again and merged into its pitch and
    // notes.  One run from the first range to the last, not one run per
    // range: only one ranged analysis can be running at a time, and pYIN
    // over the silence between two ranges costs less than a queue of
    // runs would.
    if (!m_analyser2 || !m_takes->haveTake()) return false;

    const Coverage::Ranges &ranges = m_takes->getCoverage().getRanges();
    if (ranges.empty()) return false;

    // Analyse Now is not undoable (spec 7), and its merge must not be
    // taken for the one a recording's command is waiting for.  The run
    // that command was waiting for is about to be abandoned by this one,
    // so the events it would have added never existed; the command keeps
    // the range, and analyses it again if it is ever redone
    closeOpenTakeCommand(false);

    return startTakeAnalysis(ranges.front().start, ranges.back().end);
}

void
MainWindow::eraseSingingInSelection()
{
    // The singing in the selected ranges goes out of the take: silence
    // in place of it in a new audio file, the ranges out of the coverage,
    // and the pitch and notes that were analysed there deleted.  Nothing
    // is analysed: there is nothing left there to analyse.
    //
    // The action is disabled unless all of this holds, but a shortcut or
    // a script can still reach it
    if (!m_viewManager || !m_takes->haveTake()) return;
    if (m_recordTarget && m_recordTarget->isRecording()) return;

    // The analysis of a recorded range is merged into the very models
    // the swap below hands over, and the swap would cancel it and lose
    // its result.  It lasts a fraction of the recording it follows, so
    // waiting for it is better than rescuing it
    if (m_analyser2 && m_analyser2->isAnalysingRange()) return;

    Coverage::Ranges selected;
    for (const Selection &s : m_viewManager->getSelections()) {
        selected.push_back(Coverage::Range(s.getStartFrame(),
                                           s.getEndFrame()));
    }
    if (selected.empty()) return;

    QString directory = takeAudioDirectory();
    QString error;

    if (directory == "") {
        error = tr("Could not find a directory to write the singing track "
                   "into");
    }

    // What an undo of this erase has to put back
    QString pathBefore = m_takes->getAudioPath();
    Coverage coverageBefore = m_takes->getCoverage();

    Coverage::Ranges erased;
    if (error == "") {
        error = m_takes->eraseRanges(selected, directory, &erased);
    }

    if (error != "") {
        QMessageBox::warning
            (this,
             tr("Failed to erase the singing"),
             tr("<b>The singing in the selection could not be erased</b>"
                "<p>%1</p>").arg(error),
             QMessageBox::Ok);
        return;
    }

    if (erased.empty()) {
        // Nothing of the selection held recorded singing
        emit activity(tr("No recorded singing in the selection to erase"));
        return;
    }

    QString path = m_takes->getAudioPath();

    cerr << "MainWindow::eraseSingingInSelection: erased "
         << erased.size() << " range(s) into " << path << endl;

    // The new audio goes under the take's pitch and notes layers, which
    // are edited below rather than analysed again
    bool haveLayers = m_analyser2 &&
        m_analyser2->getLayer(Analyser::PitchTrack) &&
        m_analyser2->getLayer(Analyser::Notes);

    QString showError = (haveLayers ? swapSingingAudio(path)
                         : loadTakeAudio(path));

    // The coverage has changed whether or not the new audio could be
    // shown, and the strip says what it is now.  After the swap, which
    // puts the pane's selected layer back as it found it, and the strip
    // is never to be that
    syncCoverageStrip();

    TakeEvents::Change pitchChange, notesChange;
    eraseTakeEvents(erased, &pitchChange, &notesChange);

    // Undoable as a whole: the audio file, the coverage and the events
    // that went with the singing.  Nothing was analysed, so the command is
    // complete the moment it is made
    SingingTakeCommand *command =
        new SingingTakeCommand(this, tr("Erase Singing"),
                               pathBefore, coverageBefore,
                               m_takes->getAudioPath(), m_takes->getCoverage());
    command->setEventChanges(pitchChange, notesChange);
    addTakeCommand(command);

    if (showError != "") {
        // As after a splice that could not be shown: the take's audio
        // and its coverage have changed, and the screen has not
        QMessageBox::warning
            (this,
             tr("Failed to show the erased singing"),
             tr("<b>The singing was erased, but the result could not be "
                "shown</b><p>%1</p><p>What is on screen is the singing track "
                "as it was. The erased audio is in the take's audio file, "
                "\"%2\".</p>").arg(showError).arg(path),
             QMessageBox::Ok);
    }

    updateLayerStatuses();
    updateMenuStates();
    emit activity(tr("Erased the singing in the selection"));
}

void
MainWindow::eraseTakeEvents(const Coverage::Ranges &erased,
                            TakeEvents::Change *pitchChange,
                            TakeEvents::Change *notesChange)
{
    if (!m_analyser2) return;

    Layer *pitchLayer = m_analyser2->getLayer(Analyser::PitchTrack);
    Layer *notesLayer = m_analyser2->getLayer(Analyser::Notes);

    auto pitch = pitchLayer ?
        ModelById::getAs<SparseTimeValueModel>(pitchLayer->getModel()) :
        nullptr;
    auto notes = notesLayer ?
        ModelById::getAs<NoteModel>(notesLayer->getModel()) : nullptr;

    // Straight on the models, the way an analysis result is merged: no
    // command of their own.  What was changed goes back to the caller,
    // which puts the erase as a whole on the undo stack
    if (pitch) {
        TakeEvents::Change change =
            TakeEvents::erasePitch(pitch->getAllEvents(), erased);
        for (const Event &e : change.removed) pitch->remove(e);
        for (const Event &e : change.added) pitch->add(e);
        if (pitchChange) *pitchChange = change;
    }

    if (notes) {
        TakeEvents::Change change =
            TakeEvents::eraseNotes(notes->getAllEvents(), erased);
        for (const Event &e : change.removed) notes->remove(e);
        for (const Event &e : change.added) notes->add(e);
        if (notesChange) *notesChange = change;
    }
}

QString
MainWindow::takeAudioDirectory()
{
    // A take's combined audio belongs to the song, in the session's own
    // folder beside the .ton (spec 6.4).  Before the first save there is
    // no folder to put it in, so it goes to the record directory with the
    // raw recordings, and the save copies it across
    if (m_sessionFile != "") {
        QString folder = ensureTakesFolder(m_sessionFile);
        if (folder != "") return folder;

        // Nowhere to write it beside the session: better in the record
        // directory than nowhere at all, and the next save says so when it
        // cannot copy it either
        cerr << "MainWindow::takeAudioDirectory: could not make the takes "
             << "folder of \"" << m_sessionFile << "\"; writing to the record "
             << "directory instead" << endl;
    }

    return RecordDirectory::getRecordDirectory();
}

QString
MainWindow::ensureTakesFolder(QString sessionPath)
{
    QString folder = TakesFile::takesFolder(sessionPath);
    if (folder == "") return "";

    if (QFileInfo(folder).isDir()) return folder;

    if (!QDir().mkpath(folder)) return "";

    // Made here and empty so far: if nothing is left in it when the
    // session closes, it goes again
    if (!m_takeFoldersMade.contains(folder)) m_takeFoldersMade.push_back(folder);

    return folder;
}

bool
MainWindow::copyTakeAudioForSave(QString sessionPath)
{
    if (!m_takes || m_takes->getTakeCount() == 0) return true;

    // Nothing to do for a session whose takes are all in its folder
    // already, and no folder to make for one that has no audio at all
    bool anyOutside = false;
    QString folder = TakesFile::takesFolder(sessionPath);
    for (const SingingTakes::Take &take : m_takes->getTakes()) {
        if (take.audioPath == "") continue;
        if (!TakesFile::isInFolder(folder, take.audioPath)) anyOutside = true;
    }
    if (!anyOutside) return true;

    QString error;
    QString made = ensureTakesFolder(sessionPath);

    if (made == "") {
        error = tr("The folder \"%1\", where the takes' audio belongs, could "
                   "not be made").arg(folder);
    } else {
        error = TakesFile::copyTakeAudioInto(*m_takes, made);
    }

    if (error != "") {
        QMessageBox::critical
            (this,
             tr("Failed to save the session"),
             tr("<b>The session was not saved</b><p>The audio of its takes "
                "must be in the folder beside it, and could not be put "
                "there.</p><p>%1</p>").arg(error),
             QMessageBox::Ok);
        return false;
    }

    return true;
}

bool
MainWindow::saveSessionFile(QString path)
{
    // Not while the analysis of a recorded range runs: the session would
    // hold neither the state before the merge nor the state after it
    if (!waitForRangedAnalysis()) return false;

    // The takes' audio goes into the folder of the session being saved
    // before the file is written, so that the file names it where it is.
    // A Save As copies into the new session's folder and leaves the old
    // one alone: it belongs to the .ton that is still there
    if (!copyTakeAudioForSave(path)) return false;

    // What toXml() names the takes' audio relative to
    m_savingSessionPath = path;
    bool saved = MainWindowBase::saveSessionFile(path);
    m_savingSessionPath = "";

    // The .ton that has just been written names the audio file of every
    // take of the session.  Recording into a take again writes another file
    // and supersedes that one, but the saved session still needs it, so it
    // is not ours to delete when the session closes
    if (saved && m_takes) {
        for (const SingingTakes::Take &take : m_takes->getTakes()) {
            m_takes->protectPath(take.audioPath);
        }
    }

    return saved;
}

void
MainWindow::toXml(QTextStream &out, bool asTemplate)
{
    // A template holds no session content, so it holds no takes either
    if (asTemplate) {
        MainWindowBase::toXml(out, asTemplate);
        return;
    }

    // The takes of the session are one element of Tony's own inside the
    // <sv> document (spec 6.4), which SVFileReader passes over with a
    // warning on the terminal and nothing else.  The base class writes the
    // document from <?xml?> to </sv> in one call and has no hook in the
    // middle, so it writes into a string here and the element goes in
    // before the closing tag.  A hook in the fork would save the copy.
    QString document;
    {
        QTextStream buffer(&document);
        MainWindowBase::toXml(buffer, asTemplate);
        buffer.flush();
    }

    int closing = document.lastIndexOf("</sv>");

    if (closing < 0) {
        // Not a document we know where to write in: better saved without
        // its takes than not saved at all
        cerr << "MainWindow::toXml: no </sv> to write the takes before" << endl;
        out << document;
        return;
    }

    out << document.left(closing)
        << TakesFile::toXml(*m_takes, m_savingSessionPath)
        << document.mid(closing);
}

MainWindow::FileOpenStatus
MainWindow::openSession(FileSource source)
{
    // m_restoringSession: modelAdded() queues a call that would make a take
    // of the audio model the document carries, and restoreTakes() is what
    // deals with that model instead
    m_restoringSession = true;
    FileOpenStatus status = MainWindowBase::openSession(source);
    m_restoringSession = false;

    if (status != FileOpenSucceeded) return status;

    // The document is in the panes now, with every take's layers in pane 0,
    // and nothing has been analysed
    restoreTakes(source.getLocalFilename());

    return status;
}

void
MainWindow::restoreTakes(QString sessionPath)
{
    if (!m_document) return;

    TakesFile::Takes stored = TakesFile::read(sessionPath);

    // Opening a session is not a change to it, whatever is done below
    bool wasModified = m_documentModified;

    // The audio model of the active take, which the document carried
    // because the waveform layer showing it is in pane 0: of no use here,
    // and not to be mistaken for a singing track of its own
    dropRestoredSingingTrack(!stored.found);

    if (!stored.found) {
        // A session saved before the takes were stored (spec 3, "Old
        // sessions"): it opens without its singing track, and the layers
        // that showed one have gone with it
        cerr << "MainWindow::restoreTakes: the session has no takes element: "
             << "it opens without a singing track" << endl;
        updateTakeCombo();
        updateLayerStatuses();
        updateMenuStates();
        if (!wasModified) documentRestored();
        return;
    }

    Pane *pane = m_paneStack ? m_paneStack->getPane(0) : nullptr;

    // Every name the session used is reserved before any take is made, so
    // that the numbering carries on from it and no new take can be given
    // the name of a take of the session -- or of one whose layers are in
    // the pane although no take claims them any more
    for (const TakesFile::Take &take : stored.takes) {
        m_takes->reserveTakeName(take.name);
    }
    if (pane) {
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            QString name;
            TakeLayers::Kind kind;
            if (TakeLayers::parse(pane->getLayer(i)->objectName(),
                                  name, kind)) {
                m_takes->reserveTakeName(name);
            }
        }
    }

    for (const TakesFile::Take &take : stored.takes) {

        QString name = m_takes->addTake(take.name);

        // The coverage of a take is stored in the regions of its own
        // coverage strip, which the document has put back into the pane
        // (5a); a take with no recording in it yet has neither
        Coverage coverage;
        if (pane) {
            TakeLayers::Found found = TakeLayers::find(pane, name);
            if (found.coverage) {
                if (auto model = ModelById::getAs<RegionModel>
                    (found.coverage->getModel())) {
                    coverage = Coverage::fromEvents(model->getAllEvents());
                }
            }
        }

        // The stored path is relative to the session file, so that a song
        // and its takes folder can be moved together (spec 6.4); a session
        // saved before the folder names its audio absolutely
        QString audioPath = TakesFile::resolveAudioPath(sessionPath,
                                                        take.audioPath);

        // restoreTake() and not setTake(): nothing here supersedes a file
        m_takes->restoreTake(audioPath, coverage);
    }

    int active = m_takes->indexOf(stored.active);
    if (active < 0 && m_takes->getTakeCount() > 0) active = 0;

    cerr << "MainWindow::restoreTakes: " << m_takes->getTakeCount()
         << " take(s) restored, active is \"" << stored.active << "\"" << endl;

    // The .ton moved without its folder: every take is silent, and the
    // user is told once, about the folder, rather than once per take
    // (spec 6.4).  Only the active take opens its audio, so this is also
    // the only place the other takes' files are ever looked for
    QStringList missing;
    for (const SingingTakes::Take &take : m_takes->getTakes()) {
        if (take.audioPath == "" || QFileInfo::exists(take.audioPath)) continue;
        missing.push_back(take.audioPath);
    }

    if (active >= 0) {
        // The same path a switch uses: the take's audio under the take's
        // own layers, an analyser that claims them with no analysis, its
        // coverage strip, and every other take put away
        m_takes->setActiveIndex(active);
        activateTake(missing.isEmpty());
    } else {
        putOtherTakeLayersAway();
    }

    if (!missing.isEmpty()) {
        QMessageBox::warning
            (this,
             tr("Takes without their audio"),
             tr("<b>The takes of this session are shown without their "
                "audio</b><p>%1 audio file(s) were expected in \"%2\", and "
                "are not there. Each take still has its pitch, its notes and "
                "its coverage; only the sound is missing.</p>")
             .arg(missing.size())
             .arg(TakesFile::takesFolder(sessionPath)),
             QMessageBox::Ok);
    }

    updateTakeCombo();
    updateLayerStatuses();
    updateMenuStates();

    // Whatever was done above, the session is as it was saved
    if (!wasModified) documentRestored();
}

void
MainWindow::dropRestoredSingingTrack(bool withTakeLayers)
{
    // Nothing is to make a take of the audio model the session carried:
    // restoreTakes() opens each take's audio from the path in <takes>
    m_pendingSingingModelId = {};

    if (!m_document) return;

    // The reference is the main model; every other audio model in a
    // restored document belongs to a singing track
    ModelId mainId = getMainModelId();
    std::vector<ModelId> audio;
    for (ModelId id : m_document->getModels()) {
        if (id == mainId) continue;
        if (ModelById::isa<WaveFileModel>(id)) audio.push_back(id);
    }

    // (None, in a session saved since the take's waveform layer was kept
    // out of the file -- Layer::setSavedInSession() -- but sessions saved
    // before that carry one, and layers named after a take may be there
    // to drop either way)
    if (audio.empty() && !withTakeLayers) return;

    auto isAudio = [&audio](ModelId id) {
        return std::find(audio.begin(), audio.end(), id) != audio.end();
    };

    // Collected before anything is deleted: releasing a model clears the
    // source of what was derived from it
    std::vector<Layer *> going;

    for (Layer *layer : m_document->getLayers()) {

        ModelId modelId = layer->getModel();

        if (isAudio(modelId)) {
            going.push_back(layer);
            continue;
        }

        if (!withTakeLayers) continue;

        // The pitch and notes of that singing track, whether they are
        // named after a take or (in a session from before this feature)
        // simply derived from its audio
        auto model = ModelById::get(modelId);
        if (model && isAudio(model->getSourceModel())) {
            going.push_back(layer);
            continue;
        }

        QString takeName;
        TakeLayers::Kind kind;
        if (TakeLayers::parse(layer->objectName(), takeName, kind)) {
            going.push_back(layer);
        }
    }

    cerr << "MainWindow::dropRestoredSingingTrack: dropping " << going.size()
         << " restored layer(s) of the singing track the document carried"
         << endl;

    for (Layer *layer : going) dropLayerSilently(layer);
}

void
MainWindow::dropLayerSilently(Layer *layer)
{
    if (!layer || !m_document) return;

    // The play source takes in the model of every layer that is in a view,
    // and a forced delete does not fire layerInAView() to take it out again
    if (m_playSource && !layer->getModel().isNone()) {
        m_playSource->removeModel(layer->getModel());
    }

    // deleteLayer(force) and nothing else: no undo command, out of every
    // view it is in, and the model goes with it if nothing else uses it
    m_document->deleteLayer(layer, true);
}

void
MainWindow::addTakeCommand(SingingTakeCommand *command)
{
    if (!command) return;

    // Undo after a take must undo the take, not take away some layer of
    // Tony's own.  Everything this application adds to a pane is kept off
    // the undo stack: its layers go in by Document::attachLayerToView(),
    // a take's audio is opened by openTakeAudioFile(), and the recording
    // itself is made in RecordCreateUnshownModel mode, which makes no
    // pane and no "Import Recorded Audio" entry.  So the history is left
    // as it is, and the takes and the edits made before this one can
    // still be undone after it.
    //
    // A command still waiting for a merge is not this one's business any
    // more: the callers close it before they get here
    if (m_openTakeCommand != command) m_openTakeCommand = nullptr;

    // Already done: the take's audio has been written and is on screen.
    // CommandHistory marks the document modified, which is right for both
    // of these operations
    CommandHistory::getInstance()->addCommand(command, false);
}

void
MainWindow::closeOpenTakeCommand(bool cancelAnalysis)
{
    if (cancelAnalysis && m_analyser2 && m_analyser2->isAnalysingRange()) {
        cerr << "MainWindow::closeOpenTakeCommand: abandoning the analysis of "
             << "[" << m_takeAnalysisRange.start << ","
             << m_takeAnalysisRange.end << ")" << endl;
        m_analyser2->cancelRangedAnalysis();
        m_takeAnalysisRange = Coverage::Range();

        // The live dots of the take were waiting for that analysis to
        // replace them, and it is not coming
        teardownRealtimePitchLayer();

        // Erasing is to be had again now that nothing is running
        updateMenuStates();
    }

    m_openTakeCommand = nullptr;
}

void
MainWindow::takeAnalysisMerged()
{
    if (!m_openTakeCommand || !m_analyser2) return;

    // The recording is on the undo stack already, with the splice in it
    // and nothing of the analysis: the events the merge has just changed
    // complete it, so that one Undo takes both back
    m_openTakeCommand->setEventChanges(m_analyser2->getRangedPitchChange(),
                                       m_analyser2->getRangedNotesChange());
    m_openTakeCommand = nullptr;
}

void
MainWindow::applyTakeEventChanges(const TakeState &state)
{
    if (!m_analyser2) return;

    Layer *pitchLayer = m_analyser2->getLayer(Analyser::PitchTrack);
    Layer *notesLayer = m_analyser2->getLayer(Analyser::Notes);

    auto pitch = pitchLayer ?
        ModelById::getAs<SparseTimeValueModel>(pitchLayer->getModel()) :
        nullptr;
    auto notes = notesLayer ?
        ModelById::getAs<NoteModel>(notesLayer->getModel()) : nullptr;

    // The removals first, in both directions: where the audio did not
    // change, an analysis can put back the very event it took out, and
    // that event has to be there once at the end and not twice
    if (pitch) {
        for (const Event &e : state.pitchRemove) pitch->remove(e);
        for (const Event &e : state.pitchAdd) pitch->add(e);
    }
    if (notes) {
        for (const Event &e : state.notesRemove) notes->remove(e);
        for (const Event &e : state.notesAdd) notes->add(e);
    }
}

bool
MainWindow::applyTakeState(SingingTakeCommand *command, const TakeState &state)
{
    // An undo or a redo of a recording or an erase.  Nothing of the
    // command is a layer or a model: what the take is shown with now is
    // found here, now, whatever it has been replaced by since
    if (!m_document) return false;

    // A recording can be undone while the analysis of it is still
    // running.  That result belongs to the state being left behind, so the
    // run is abandoned rather than allowed to land on the take we are
    // putting back; the command remembers the range, and analyses it again
    // if it is redone
    closeOpenTakeCommand(true);

    m_takes->restoreTake(state.path, state.coverage);

    QString error;

    if (state.path == "") {
        // Undo of the first recording of a take: no audio to show, and no
        // pitch or notes either, exactly as before that recording.  The
        // next Record makes them again (rebuildSingingTrackFromTake())
        teardownSingingTrackAnalyser();
    } else {
        bool haveLayers = m_analyser2 &&
            m_analyser2->getLayer(Analyser::PitchTrack) &&
            m_analyser2->getLayer(Analyser::Notes);
        error = (haveLayers ? swapSingingAudio(state.path)
                 : loadTakeAudio(state.path));
        if (error == "" && m_analyser2) {
            error = m_analyser2->addEmptyAnalyses();
            // A redo of the first recording of a take makes its layers
            // again, and they are the take's
            nameActiveTakeLayers();
            raiseActiveTakeLayers();
        }
    }

    // After the swap, which puts the pane's selected layer back as it
    // found it, and the strip is never to be that
    syncCoverageStrip();

    if (error == "") applyTakeEventChanges(state);

    if (error != "") {
        QMessageBox::warning
            (this,
             tr("Failed to show the singing track"),
             tr("<b>The singing track could not be shown as it was</b>"
                "<p>%1</p><p>The take's audio is in the file \"%2\".</p>")
             .arg(error).arg(state.path),
             QMessageBox::Ok);
    }

    // A range whose analysis never finished: its result is in no event
    // list, so it is analysed again rather than restored, and the command
    // is open once more until that merge lands
    if (error == "" && state.analyse.length() > 0) {
        m_openTakeCommand = command;
        if (!startTakeAnalysis(state.analyse.start, state.analyse.end) &&
            m_openTakeCommand == command) {
            m_openTakeCommand = nullptr;
        }
    }

    updateLayerStatuses();
    updateMenuStates();

    emit activity(tr("The singing track is the take's file \"%1\" again")
                  .arg(state.path == "" ? tr("(none)") : state.path));

    return error == "";
}

void
MainWindow::selectRecordingAtPlayhead()
{
    // The selection becomes the coverage range the playhead is in, so
    // that erasing a whole recording is two commands.  In a gap there is
    // nothing to select, and the selection is left as it was
    if (!m_viewManager || !m_takes->haveTake()) return;
    if (m_recordTarget && m_recordTarget->isRecording()) return;

    Coverage::Range range;
    if (!m_takes->getCoverage().getRangeAt
        (m_viewManager->getPlaybackFrame(), range)) {
        emit activity(tr("No recorded singing at the playback position"));
        return;
    }

    m_viewManager->setSelection(Selection(range.start, range.end));
}

bool
MainWindow::confirmRecordingOverTake()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Record over the existing singing?"));
    box.setText(tr("<b>Record over the existing singing from here?</b>"));
    box.setInformativeText
        (tr("There is singing recorded from this point on. Recording from "
            "here replaces it."));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::Yes);

    QCheckBox *dontAsk = new QCheckBox(tr("Don't ask again"), &box);
    box.setCheckBox(dontAsk);

    bool yes = (box.exec() == QMessageBox::Yes);

    // "Don't ask again" means "always go ahead", so it is only taken as
    // an answer when this one was yes
    if (yes && dontAsk->isChecked()) {
        SingingTakes::setOverwriteConfirmationWanted(false);
    }

    return yes;
}

bool
MainWindow::confirmDeleteTake(QString name)
{
    return QMessageBox::question
        (this, tr("Delete this take?"),
         tr("<b>Delete the take \"%1\"?</b><p>Its pitch track and its notes "
            "go with it, and this cannot be undone. Its audio file is not "
            "deleted.</p>").arg(name),
         QMessageBox::Yes | QMessageBox::No,
         QMessageBox::No) == QMessageBox::Yes;
}

QString
MainWindow::askForTakeName(QString current)
{
    bool ok = false;
    QString name = QInputDialog::getText
        (this, tr("Rename take"), tr("Name for this take:"),
         QLineEdit::Normal, current, &ok);
    return ok ? name : QString();
}

bool
MainWindow::takeOperationsAllowed() const
{
    // Nothing about the takes of the session changes while one is being
    // recorded, or while the analysis of a recorded range is running: that
    // analysis is merged into the models a switch would hand over, and the
    // switch would lose its result (as an erase would, see
    // eraseSingingInSelection())
    if (!m_document) return false;
    if (m_paneStack && m_paneStack->getPaneCount() < 1) return false;
    if (!getMainModel()) return false;
    if (m_recordTarget && m_recordTarget->isRecording()) return false;
    if (m_analyser2 && m_analyser2->isAnalysingRange()) return false;
    return true;
}

void
MainWindow::clearTakeHistory()
{
    // A take command holds the state of one take, and after a switch, a
    // new take, a duplicate or a delete it is not the take on show any
    // more: undoing it would write one take's singing over another's.
    // Spec 5.4: the whole history goes, with no prompt
    closeOpenTakeCommand(true);
    CommandHistory::getInstance()->clear();
}

void
MainWindow::nameActiveTakeLayers()
{
    // The pitch and notes layers the singing analyser holds are the active
    // take's, and their object names say so: that is the only link between
    // a take and the layers that show it (spec 6.4).  A whole-file
    // analysis and addEmptyAnalyses() both name them after the transform
    // that made them, so this is done every time they change hands
    QString name = m_takes->getActiveName();
    if (name == "" || !m_analyser2) return;

    const struct { Analyser::Component component; TakeLayers::Kind kind; }
    wanted[] = {
        { Analyser::PitchTrack, TakeLayers::Pitch },
        { Analyser::Notes, TakeLayers::Notes }
    };

    for (const auto &w : wanted) {
        Layer *layer = m_analyser2->getLayer(w.component);
        if (!layer) continue;
        QString objectName = TakeLayers::nameFor(name, w.kind);
        if (layer->objectName() != objectName) {
            layer->setObjectName(objectName);
        }
    }
}

void
MainWindow::putOtherTakeLayersAway()
{
    // Everything in pane 0 that belongs to a take other than the active
    // one: hidden, silent, out of the play source and with no source
    // model.  Three things depend on this:
    //
    //  - an Analyser claims a pitch or notes layer whose model's source
    //    model is its own audio, so a take that is put away must have no
    //    source model at all (and adoptTakeLayers() goes by name);
    //  - the play source takes in every model of a layer that is in a
    //    view, and what it holds says where playback ends, so a take
    //    longer than the active one would hold playback open past the end
    //    of what there is to hear (5a's finding, as for the strip);
    //  - a pitch track and a set of notes can be sonified in Tony, and a
    //    take that is not on show is not to be heard.
    Pane *pane = m_paneStack ? m_paneStack->getPane(0) : nullptr;
    if (!pane) return;

    QString active = m_takes->getActiveName();

    for (int i = 0; i < pane->getLayerCount(); ++i) {

        Layer *layer = pane->getLayer(i);

        QString name;
        TakeLayers::Kind kind;
        if (!TakeLayers::parse(layer->objectName(), name, kind)) continue;
        if (name == active) continue;

        layer->setLayerDormant(pane, true);

        if (auto params = layer->getPlayParameters()) {
            params->setPlayAudible(false);
        }

        ModelId modelId = layer->getModel();
        if (auto model = ModelById::get(modelId)) {
            model->setSourceModel(ModelId());
        }
        if (m_playSource && !modelId.isNone()) {
            m_playSource->removeModel(modelId);
        }
    }
}

void
MainWindow::raiseActiveTakeLayers()
{
    // The note tool acts on the pane's topmost note layer, whether that
    // layer is dormant or not, so the active take's notes have to be above
    // the ones of the takes that are put away.  A take's layers are added
    // to the pane when it is first recorded into, so without this the
    // newest take would keep the tools to itself
    Pane *pane = m_paneStack ? m_paneStack->getPane(0) : nullptr;
    if (!pane || !m_analyser2) return;

    for (Analyser::Component c : { Analyser::PitchTrack, Analyser::Notes }) {
        if (Layer *layer = m_analyser2->getLayer(c)) {
            TakeLayers::raise(pane, layer);
        }
    }
}

void
MainWindow::deactivateTake()
{
    // The take being put away keeps its layers, with every event in them:
    // they are where it is stored (spec 6.4).  What it loses is the
    // analyser, the audio model under it, and its place in the mix
    if (m_analyser2) {
        m_analyser2->releaseLayers();
        delete m_analyser2;
        m_analyser2 = nullptr;
    }

    // The strip stays in the pane as well, holding this take's coverage
    m_coverageStrip->release();

    // Which leaves nothing belonging to the active take but its name:
    // putOtherTakeLayersAway() does the rest once another take is active
}

bool
MainWindow::activateTake(bool warnIfNoAudio)
{
    Pane *pane = m_paneStack ? m_paneStack->getPane(0) : nullptr;
    if (!pane) return false;

    QString name = m_takes->getActiveName();
    if (name == "") return false;

    QString path = m_takes->getAudioPath();
    QString error;

    if (path != "") {
        // The take's audio under the take's layers, with no analysis:
        // loadTakeAudio() finds them by name (adoptTakeLayers()) and the
        // new analyser claims them.  m_rebuildingTakeAudio: the coverage
        // of this file is the take's own, not "the whole of it"
        bool wasRebuilding = m_rebuildingTakeAudio;
        m_rebuildingTakeAudio = true;
        error = loadTakeAudio(path);
        m_rebuildingTakeAudio = wasRebuilding;

        // A take that has audio but no pitch and notes -- one whose
        // layers were lost with a session that could not be read back --
        // gets empty ones, so that the next recording has something to
        // merge into
        if (error == "" && m_analyser2) {
            error = m_analyser2->addEmptyAnalyses();
        }
        nameActiveTakeLayers();
    }

    // The models of the take that is on show now are in the play source
    // again: they came out of it when the take was put away.  Whether they
    // are seen and heard is the analyser's business -- it has just loaded
    // the show and play settings onto them, as it does for any file
    if (m_analyser2 && m_playSource) {
        for (Analyser::Component c : { Analyser::PitchTrack, Analyser::Notes }) {
            Layer *layer = m_analyser2->getLayer(c);
            if (!layer || layer->getModel().isNone()) continue;
            m_playSource->addModel(layer->getModel());
        }
    }

    putOtherTakeLayersAway();
    syncCoverageStrip();
    raiseActiveTakeLayers();

    if (error != "" && warnIfNoAudio) {
        // Its pitch and notes are there; only the sound is missing
        // (spec 6.4, a missing audio file)
        QMessageBox::warning
            (this,
             tr("Failed to open the take's audio"),
             tr("<b>The take \"%1\" is shown without its audio</b><p>%2</p>")
             .arg(name).arg(error),
             QMessageBox::Ok);
    }

    return error == "";
}

bool
MainWindow::switchToTake(int index)
{
    if (!takeOperationsAllowed()) return false;
    if (index == m_takes->getActiveIndex()) return true;
    if (!m_takes->getTake(index)) return false;

    cerr << "MainWindow::switchToTake: from take "
         << m_takes->getActiveIndex() << " to " << index << endl;

    clearTakeHistory();

    deactivateTake();
    m_takes->setActiveIndex(index);
    bool ok = activateTake();

    updateTakeCombo();
    updateLayerStatuses();
    updateMenuStates();

    // Which take is the active one is stored in the session (spec 6.4), so
    // a switch is a change to it
    documentModified();

    emit activity(tr("Switched to the take \"%1\"")
                  .arg(m_takes->getActiveName()));

    return ok;
}

void
MainWindow::takeChosenInCombo(int index)
{
    if (m_updatingTakeCombo) return;
    if (index < 0) return;
    if (index == m_takes->getActiveIndex()) return;

    if (!switchToTake(index)) {
        // Whatever went wrong, the combo box must go on saying which take
        // is the active one
        updateTakeCombo();
    }
}

void
MainWindow::updateTakeCombo()
{
    if (!m_takeCombo) return;

    m_updatingTakeCombo = true;

    QStringList names = m_takes->getTakeNames();
    QStringList shown;
    for (int i = 0; i < m_takeCombo->count(); ++i) {
        shown.push_back(m_takeCombo->itemText(i));
    }

    if (shown != names) {
        m_takeCombo->clear();
        m_takeCombo->addItems(names);
    }
    m_takeCombo->setCurrentIndex(m_takes->getActiveIndex());

    m_updatingTakeCombo = false;
}

void
MainWindow::newEmptyTake()
{
    if (!takeOperationsAllowed()) return;

    clearTakeHistory();

    // The take on show is put away as it is; the new one has no audio and
    // no layers until the first recording goes into it
    deactivateTake();
    QString name = m_takes->addTake();
    putOtherTakeLayersAway();
    syncCoverageStrip();

    updateTakeCombo();
    updateLayerStatuses();
    updateMenuStates();

    // The session has a take it did not have before
    documentModified();

    emit activity(tr("Started the empty take \"%1\"").arg(name));
}

void
MainWindow::duplicateTake()
{
    if (!takeOperationsAllowed()) return;
    if (m_takes->getActiveIndex() < 0) return;

    // The events of the take being copied, taken before anything moves:
    // the copy gets models of its own holding the same events, and the
    // same audio file, which neither take writes over (spec 5.4)
    EventVector pitchEvents, notesEvents;
    if (m_analyser2) {
        if (Layer *layer = m_analyser2->getLayer(Analyser::PitchTrack)) {
            if (auto model = ModelById::getAs<SparseTimeValueModel>
                (layer->getModel())) {
                pitchEvents = model->getAllEvents();
            }
        }
        if (Layer *layer = m_analyser2->getLayer(Analyser::Notes)) {
            if (auto model = ModelById::getAs<NoteModel>(layer->getModel())) {
                notesEvents = model->getAllEvents();
            }
        }
    }

    clearTakeHistory();

    QString from = m_takes->getActiveName();
    deactivateTake();
    QString name = m_takes->duplicateActiveTake();

    // The copy has no layers of its own yet: activateTake() opens the
    // audio, and addEmptyAnalyses() makes the pitch and notes that the
    // events below go into
    activateTake();

    if (m_analyser2) {
        if (Layer *layer = m_analyser2->getLayer(Analyser::PitchTrack)) {
            if (auto model = ModelById::getAs<SparseTimeValueModel>
                (layer->getModel())) {
                for (const Event &e : pitchEvents) model->add(e);
            }
        }
        if (Layer *layer = m_analyser2->getLayer(Analyser::Notes)) {
            if (auto model = ModelById::getAs<NoteModel>(layer->getModel())) {
                for (const Event &e : notesEvents) model->add(e);
            }
        }
    }

    updateTakeCombo();
    updateLayerStatuses();
    updateMenuStates();
    documentModified();

    emit activity(tr("Copied the take \"%1\" into \"%2\"")
                  .arg(from).arg(name));
}

void
MainWindow::renameTake()
{
    // The one take operation that leaves the undo history alone: nothing
    // of the singing changes, only what the take is called
    int index = m_takes->getActiveIndex();
    if (index < 0) return;

    QString current = m_takes->getActiveName();
    QString name = askForTakeName(current);
    if (name == "" || name == current) return;

    if (!m_takes->renameTake(index, name)) {
        QMessageBox::warning
            (this, tr("Could not rename the take"),
             tr("<b>The take could not be renamed to \"%1\"</b><p>Another "
                "take of this session has that name.</p>").arg(name),
             QMessageBox::Ok);
        return;
    }

    name = m_takes->getActiveName();

    // The take's layers are named after it, so they are renamed with it
    nameActiveTakeLayers();

    if (Pane *pane = m_paneStack ? m_paneStack->getPane(0) : nullptr) {
        TakeLayers::Found found = TakeLayers::find(pane, current);
        if (found.coverage) {
            found.coverage->setObjectName
                (TakeLayers::nameFor(name, TakeLayers::Coverage));
        }
        // The strip remembers the name it was adopted under, so it takes
        // its layer up again under the new one
        m_coverageStrip->release();
        syncCoverageStrip();
    }

    updateTakeCombo();
    updateMenuStates();
    documentModified();

    emit activity(tr("The take \"%1\" is called \"%2\" now")
                  .arg(current).arg(name));
}

void
MainWindow::deleteTake()
{
    if (!takeOperationsAllowed()) return;

    int index = m_takes->getActiveIndex();
    const SingingTakes::Take *take = m_takes->getTake(index);
    if (!take) return;

    if (!confirmDeleteTake(take->name)) return;

    deleteTakeAt(index);
}

bool
MainWindow::deleteTakeAt(int index)
{
    const SingingTakes::Take *take = m_takes->getTake(index);
    if (!take) return false;
    if (!m_document) return false;

    QString name = take->name;
    bool wasActive = (index == m_takes->getActiveIndex());

    cerr << "MainWindow::deleteTakeAt: deleting take \"" << name
         << "\" (" << (wasActive ? "active" : "inactive") << ")" << endl;

    clearTakeHistory();

    if (wasActive) {
        // Its analyser and its audio go; the layers are deleted below
        deactivateTake();
    }

    Pane *pane = m_paneStack ? m_paneStack->getPane(0) : nullptr;
    if (pane) {
        TakeLayers::Found found = TakeLayers::find(pane, name);
        for (Layer *layer : { static_cast<Layer *>(found.pitch),
                              static_cast<Layer *>(found.notes),
                              static_cast<Layer *>(found.coverage) }) {
            if (!layer) continue;
            // As the analyser and the strip take their own layers away:
            // no command, and the model goes with the layer
            if (m_playSource && !layer->getModel().isNone()) {
                m_playSource->removeModel(layer->getModel());
            }
            m_document->deleteLayer(layer, true);
        }
    }

    m_takes->removeTake(index);

    // A neighbour is the active take now, or there is none at all.  Its
    // audio has to be opened either way: the take that was deleted had it
    if (wasActive && m_takes->getActiveIndex() >= 0) {
        activateTake();
    }

    updateTakeCombo();
    updateLayerStatuses();
    updateMenuStates();
    documentModified();

    emit activity(tr("Deleted the take \"%1\"").arg(name));

    return true;
}

void
MainWindow::openLocation()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    QString lastLocation = settings.value("lastremote", "").toString();

    bool ok = false;
    QString text = QInputDialog::getText
        (this, tr("Open Location"),
         tr("Please enter the URL of the location to open:"),
         QLineEdit::Normal, lastLocation, &ok);

    if (!ok) return;

    settings.setValue("lastremote", text);

    if (text.isEmpty()) return;

    FileOpenStatus status = openPath(text, ReplaceSession);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Open failed</b><p>URL \"%1\" could not be opened").arg(text));
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    }
}

void
MainWindow::openRecentFile()
{
    QObject *obj = sender();
    QAction *action = qobject_cast<QAction *>(obj);
    
    if (!action) {
        cerr << "WARNING: MainWindow::openRecentFile: sender is not an action"
             << endl;
        return;
    }

    QString path = action->objectName();
    if (path == "") return;

    FileOpenStatus status = openPath(path, ReplaceSession);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Open failed</b><p>File or URL \"%1\" could not be opened").arg(path));
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open location"),
                              tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
    }
}

void
MainWindow::paneAdded(Pane *pane)
{
    pane->setPlaybackFollow(PlaybackScrollPage);
    m_paneStack->sizePanesEqually();
    if (m_overview) m_overview->registerView(pane);
}    

void
MainWindow::paneHidden(Pane *pane)
{
    if (m_overview) m_overview->unregisterView(pane); 
}    

void
MainWindow::paneAboutToBeDeleted(Pane *pane)
{
    if (m_overview) m_overview->unregisterView(pane); 
}    

void
MainWindow::paneDropAccepted(Pane *pane, QStringList uriList)
{
    if (pane) m_paneStack->setCurrentPane(pane);

    for (QStringList::iterator i = uriList.begin(); i != uriList.end(); ++i) {

        FileOpenStatus status = openPath(*i, ReplaceSession);

        if (status == FileOpenFailed) {
            QMessageBox::critical(this, tr("Failed to open dropped URL"),
                                  tr("<b>Open failed</b><p>Dropped URL \"%1\" could not be opened").arg(*i));
        } else if (status == FileOpenWrongMode) {
            QMessageBox::critical(this, tr("Failed to open dropped URL"),
                                  tr("<b>Audio required</b><p>Please load at least one audio file before importing annotation data"));
        }
    }
}

void
MainWindow::paneDropAccepted(Pane *pane, QString text)
{
    if (pane) m_paneStack->setCurrentPane(pane);

    QUrl testUrl(text);
    if (testUrl.scheme() == "file" || 
        testUrl.scheme() == "http" || 
        testUrl.scheme() == "ftp") {
        QStringList list;
        list.push_back(text);
        paneDropAccepted(pane, list);
        return;
    }

    //!!! open as text -- but by importing as if a CSV, or just adding
    //to a text layer?
}

void
MainWindow::closeEvent(QCloseEvent *e)
{
//    cerr << "MainWindow::closeEvent" << endl;

    if (m_openingAudioFile) {
//        cerr << "Busy - ignoring close event" << endl;
        e->ignore();
        return;
    }

    if (!checkSaveModified()) {
//        cerr << "Ignoring close event" << endl;
        e->ignore();
        return;
    }

    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("size", size());
    settings.setValue("position", pos());
    settings.endGroup();

    delete m_keyReference;
    m_keyReference = 0;

    closeSession();

    e->accept();
    return;
}

bool
MainWindow::commitData(bool mayAskUser)
{
    if (mayAskUser) {
        bool rv = checkSaveModified();
        return rv;
    } else {
        if (!m_documentModified) return true;

        // If we can't check with the user first, then we can't save
        // to the original session file (even if we have it) -- have
        // to use a temporary file

        QString svDirBase = ".sv1";
        QString svDir = QDir::home().filePath(svDirBase);

        if (!QFileInfo(svDir).exists()) {
            if (!QDir::home().mkdir(svDirBase)) return false;
        } else {
            if (!QFileInfo(svDir).isDir()) return false;
        }
        
        // This name doesn't have to be unguessable. Its extension is the
        // one this application opens as a session -- .ton, not Sonic
        // Visualiser's .sv, which Tony would try to open as audio
        QString extension = InteractiveFileFinder::getInstance()
            ->getApplicationSessionExtension();
#ifndef _WIN32
        QString fname = QString("tmp-%1-%2.%3")
            .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz"))
            .arg(QProcess().processId())
            .arg(extension);
#else
        QString fname = QString("tmp-%1.%2")
            .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz"))
            .arg(extension);
#endif
        QString fpath = QDir(svDir).filePath(fname);
        if (saveSessionFile(fpath)) {
            m_recentFiles.addFile(fpath);
            return true;
        } else {
            return false;
        }
    }
}

bool
MainWindow::checkSaveModified()
{
    // Called before some destructive operation (e.g. new session,
    // exit program).  Return true if we can safely proceed, false to
    // cancel.

    if (!m_documentModified) return true;

    int button = 
        QMessageBox::warning(this,
                             tr("Session modified"),
                             tr("The current session has been modified.\nDo you want to save it?"),
                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                             QMessageBox::Yes);

    if (button == QMessageBox::Yes) {
        saveSession();
        if (m_documentModified) { // save failed -- don't proceed!
            return false;
        } else {
            return true; // saved, so it's safe to continue now
        }
    } else if (button == QMessageBox::No) {
        m_documentModified = false; // so we know to abandon it
        return true;
    }

    // else cancel
    return false;
}

bool
MainWindow::waitForInitialAnalysis()
{
    // Called before saving a session. We can't safely save while the
    // initial analysis is happening, because then we end up with an
    // incomplete session on reload. There are certainly theoretically
    // better ways to handle this...
    
    QSettings settings;
    settings.beginGroup("Analyser");
    bool autoAnalyse = settings.value("auto-analysis", true).toBool();
    settings.endGroup();

    if (!autoAnalyse) {
        return true;
    }

    if (!m_analyser || m_analyser->getInitialAnalysisCompletion() >= 100) {
        return true;
    }

    QMessageBox mb(QMessageBox::Information,
                   tr("Waiting for analysis"),
                   tr("Waiting for initial analysis to finish before loading or saving..."),
                   QMessageBox::Cancel,
                   this);

    connect(m_analyser, SIGNAL(initialAnalysisCompleted()), 
            &mb, SLOT(accept()));

    if (mb.exec() == QDialog::Accepted) {
        return true;
    } else {
        return false;
    }
}

bool
MainWindow::waitForRangedAnalysis()
{
    // A session must not be saved in the middle of the analysis of a
    // recorded range (spec 6.3): the take's pitch and notes still hold the
    // state before the merge, and the two models the run works in are in
    // the document, in no pane, so both would be written.  The run takes a
    // fraction of the recording it follows -- a second or so -- and its
    // merge is driven by the event loop, so it is waited for here rather
    // than with the dialog waitForInitialAnalysis() puts up for the
    // reference's first analysis, which can take as long as the song.
    if (!m_analyser2 || !m_analyser2->isAnalysingRange()) return true;

    cerr << "MainWindow::waitForRangedAnalysis: waiting for the analysis of ["
         << m_takeAnalysisRange.start << "," << m_takeAnalysisRange.end
         << ") to be merged" << endl;

    QEventLoop loop;

    QTimer poll;
    connect(&poll, &QTimer::timeout, &loop, [this, &loop]() {
        if (!m_analyser2 || !m_analyser2->isAnalysingRange()) loop.quit();
    });
    poll.start(10);

    // A run that never finishes must not hold the save for ever
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();

    if (m_analyser2 && m_analyser2->isAnalysingRange()) {
        // Abandoning it loses the analysis of that range, which the user
        // can ask for again with Analyse Now; writing its temporary models
        // into the session would leave the session itself wrong
        cerr << "MainWindow::waitForRangedAnalysis: the analysis has not "
             << "finished; abandoning it so that the session can be saved"
             << endl;
        closeOpenTakeCommand(true);
    }

    return true;
}

void
MainWindow::saveSession()
{
    // We do not want to save mid-analysis regions -- that would cause
    // confusion on reloading
    m_analyser->clearReAnalysis();
    clearSelection();

    if (m_sessionFile != "") {
        if (!saveSessionFile(m_sessionFile)) {
            QMessageBox::critical
                (this, tr("Failed to save file"),
                 tr("Session file \"%1\" could not be saved.").arg(m_sessionFile));
        } else {
            CommandHistory::getInstance()->documentSaved();
            documentRestored();
        }
    } else {
        saveSessionAs();
    }
}

void
MainWindow::saveSessionInAudioPath()
{
    if (m_audioFile == "") return;

    if (!waitForInitialAnalysis()) return;

    // We do not want to save mid-analysis regions -- that would cause
    // confusion on reloading
    m_analyser->clearReAnalysis();
    clearSelection();

    QString filepath = QFileInfo(m_audioFile).absoluteDir().canonicalPath();
    QString basename = QFileInfo(m_audioFile).completeBaseName();

    QString path = QDir(filepath).filePath(basename + ".ton");

    cerr << path << endl;

    // We don't want to overwrite an existing .ton file unless we put
    // it there in the first place
    bool shouldVerify = true;
    if (m_sessionFile == path) {
        shouldVerify = false;
    }

    if (shouldVerify && QFileInfo(path).exists()) {
        if (QMessageBox::question(0, tr("File exists"),
                                  tr("<b>File exists</b><p>The file \"%1\" already exists.\nDo you want to overwrite it?").arg(path),
                                  QMessageBox::Ok,
                                  QMessageBox::Cancel) != QMessageBox::Ok) {
            return;
        }
    }

    if (!waitForInitialAnalysis()) {
        QMessageBox::warning(this, tr("File not saved"),
                             tr("Wait cancelled: the session has not been saved."));
    }

    saveSessionToPath(path);
}

bool
MainWindow::saveSessionToPath(QString path)
{
    if (!saveSessionFile(path)) {
        QMessageBox::critical(this, tr("Failed to save file"),
                              tr("Session file \"%1\" could not be saved.").arg(path));
        return false;
    }

    setWindowTitle(tr("%1: %2")
                   .arg(QApplication::applicationName())
                   .arg(QFileInfo(path).fileName()));

    // From here on the session has a file, so the audio of a take recorded
    // from now on is written into its folder (spec 6.4)
    m_sessionFile = path;

    CommandHistory::getInstance()->documentSaved();
    documentRestored();
    m_recentFiles.addFile(path);
    return true;
}

void
MainWindow::saveSessionAs()
{
    // We do not want to save mid-analysis regions -- that would cause
    // confusion on reloading
    m_analyser->clearReAnalysis();
    clearSelection();

    QString path = getSaveFileName(FileFinder::SessionFile);

    if (path == "") {
        return;
    }

    if (!waitForInitialAnalysis()) {
        QMessageBox::warning(this, tr("File not saved"),
                             tr("Wait cancelled: the session has not been saved."));
        return;
    }

    saveSessionToPath(path);
}

QString
MainWindow::exportToSVL(QString path, Layer *layer)
{
    auto model = ModelById::get(layer->getModel());
    if (!model) return "Internal error: No model in layer";

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return tr("Failed to open file %1 for writing").arg(path);
    } else {
        QTextStream out(&file);
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<!DOCTYPE sonic-visualiser>\n"
            << "<sv>\n"
            << "  <data>\n";
        
        model->toXml(out, "    ");
        
        out << "  </data>\n"
            << "  <display>\n";
        
        layer->toXml(out, "    ");
        
        out << "  </display>\n"
            << "</sv>\n";

        return "";
    }
}

void
MainWindow::importPitchLayer()
{
    QString path = getOpenFileName(FileFinder::LayerFileNoMidiNonSV);
    if (path == "") return;

    FileOpenStatus status = importPitchLayer(path);

    if (status == FileOpenFailed) {
        emit hideSplash();
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>File open failed</b><p>Layer file %1 could not be opened.").arg(path));
        return;
    } else if (status == FileOpenWrongMode) {
        emit hideSplash();
        QMessageBox::critical(this, tr("Failed to open file"),
                              tr("<b>Audio required</b><p>Unable to load layer data from \"%1\" without an audio file.<br>Please load at least one audio file before importing annotations.").arg(path));
    }
}

MainWindow::FileOpenStatus
MainWindow::importPitchLayer(FileSource source)
{
    if (!source.isAvailable()) return FileOpenFailed;
    source.waitForData();

    if (!waitForInitialAnalysis()) return FileOpenCancelled;
    
    QString path = source.getLocalFilename();

    RDFImporter::RDFDocumentType rdfType = 
        RDFImporter::identifyDocumentType(QUrl::fromLocalFile(path).toString());

    if (rdfType != RDFImporter::NotRDF) {

        //!!!
        return FileOpenFailed;

    } else if (source.getExtension().toLower() == "svl" ||
               (source.getExtension().toLower() == "xml" &&
                (SVFileReader::identifyXmlFile(source.getLocalFilename())
                 == SVFileReader::SVLayerFile))) {
        
        //!!!
        return FileOpenFailed;

    } else {
        
        try {

            CSVFormat format(path);
            format.setSampleRate(getMainModel()->getSampleRate());

            if (format.getModelType() != CSVFormat::TwoDimensionalModel) {
                //!!! error report
                return FileOpenFailed;
            }

            Model *model = DataFileReaderFactory::loadCSV
                (path, format, getMainModel()->getSampleRate());

            if (model) {

                SVDEBUG << "MainWindow::importPitchLayer: Have model" << endl;

                ModelId modelId = ModelById::add
                    (std::shared_ptr<Model>(model));
                
                CommandHistory::getInstance()->startCompoundOperation
                    (tr("Import Pitch Track"), true);

                Layer *newLayer = m_document->createImportedLayer(modelId);

                m_analyser->takePitchTrackFrom(newLayer);

                m_document->deleteLayer(newLayer);

                CommandHistory::getInstance()->endCompoundOperation();

                if (!source.isRemote()) {
                    registerLastOpenedFilePath
                        (FileFinder::LayerFile,
                         path); // for file dialog
                }

                return FileOpenSucceeded;
            }
        } catch (DataFileReaderFactory::Exception e) {
            if (e == DataFileReaderFactory::ImportCancelled) {
                return FileOpenCancelled;
            }
        }
    }
    
    return FileOpenFailed;
}

void
MainWindow::exportPitchLayer()
{
    Layer *layer = m_analyser->getLayer(Analyser::PitchTrack);
    if (!layer) return;

    auto model = ModelById::getAs<SparseTimeValueModel>(layer->getModel());
    if (!model) return;

    FileFinder::FileType type = FileFinder::LayerFileNoMidiNonSV;

    QString path = getSaveFileName(type);

    if (path == "") return;

    if (!waitForInitialAnalysis()) return;
    
    if (QFileInfo(path).suffix() == "") path += ".svl";

    QString suffix = QFileInfo(path).suffix().toLower();

    QString error;

    if (suffix == "xml" || suffix == "svl") {

        error = exportToSVL(path, layer);

    } else if (suffix == "ttl" || suffix == "n3") {

        RDFExporter exporter(path, model.get());
        exporter.write();
        if (!exporter.isOK()) {
            error = exporter.getError();
        }

    } else {

        DataExportOptions options = DataExportFillGaps;
        
        CSVFileWriter writer(path, model.get(),
                             ((suffix == "csv") ? "," : "\t"),
                             options);
        writer.write();

        if (!writer.isOK()) {
            error = writer.getError();
        }
    }

    if (error != "") {
        QMessageBox::critical(this, tr("Failed to write file"), error);
    } else {
        emit activity(tr("Export layer to \"%1\"").arg(path));
    }
}

void
MainWindow::exportNoteLayer()
{
    Layer *layer = m_analyser->getLayer(Analyser::Notes);
    if (!layer) return;

    auto model = ModelById::getAs<NoteModel>(layer->getModel());
    if (!model) return;

    FileFinder::FileType type = FileFinder::LayerFileNonSV;

    QString path = getSaveFileName(type);

    if (path == "") return;

    if (QFileInfo(path).suffix() == "") path += ".svl";

    QString suffix = QFileInfo(path).suffix().toLower();

    QString error;

    if (suffix == "xml" || suffix == "svl") {

        error = exportToSVL(path, layer);

    } else if (suffix == "mid" || suffix == "midi") {
     
        MIDIFileWriter writer(path, model.get(), model->getSampleRate());
        writer.write();
        if (!writer.isOK()) {
            error = writer.getError();
        }

    } else if (suffix == "ttl" || suffix == "n3") {

        RDFExporter exporter(path, model.get());
        exporter.write();
        if (!exporter.isOK()) {
            error = exporter.getError();
        }

    } else {

        DataExportOptions options = DataExportOmitLevel;
        
        CSVFileWriter writer(path, model.get(),
                             ((suffix == "csv") ? "," : "\t"),
                             options);
        writer.write();

        if (!writer.isOK()) {
            error = writer.getError();
        }
    }

    if (error != "") {
        QMessageBox::critical(this, tr("Failed to write file"), error);
    } else {
        emit activity(tr("Export layer to \"%1\"").arg(path));
    }
}

void
MainWindow::browseRecordedAudio()
{
    if (!m_recordTarget) return;

    QString path = RecordDirectory::getRecordContainerDirectory();
    if (path == "") path = RecordDirectory::getRecordDirectory();
    if (path == "") return;

    openLocalFolder(path);
}

void
MainWindow::doubleClickSelectInvoked(sv_frame_t frame)
{
    sv_frame_t f0, f1;
    m_analyser->getEnclosingSelectionScope(frame, f0, f1);
    
    cerr << "MainWindow::doubleClickSelectInvoked(" << frame << "): [" << f0 << "," << f1 << "]" << endl;

    Selection sel(f0, f1);
    m_viewManager->setSelection(sel);
}

void
MainWindow::abandonSelection()
{
    // Named abandonSelection rather than clearSelection to indicate
    // that this is an active operation -- it restores the original
    // content of the pitch track in the selected region rather than
    // simply un-selecting.

    cerr << "MainWindow::abandonSelection()" << endl;

    CommandHistory::getInstance()->startCompoundOperation(tr("Abandon Selection"), true);

    MultiSelection::SelectionList selections = m_viewManager->getSelections();
    if (!selections.empty()) {
        Selection sel = *selections.begin();
        m_analyser->abandonReAnalysis(sel);
        auxSnapNotes(sel);
    }

    MainWindowBase::clearSelection();

    CommandHistory::getInstance()->endCompoundOperation();
}

void
MainWindow::selectionChangedByUser()
{
    if (!m_document) {
        // we're exiting, most likely
        return;
    }

    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    cerr << "MainWindow::selectionChangedByUser" << endl;

    m_analyser->showPitchCandidates(m_pendingConstraint.isConstrained());

    if (!selections.empty()) {
        Selection sel = *selections.begin();
        cerr << "MainWindow::selectionChangedByUser: have selection" << endl;
        QString error = m_analyser->reAnalyseSelection
            (sel, m_pendingConstraint);
        if (error != "") {
            QMessageBox::critical
                (this, tr("Failed to analyse selection"),
                 tr("<b>Analysis failed</b><p>%2</p>").arg(error));
        }
    }

    m_pendingConstraint = Analyser::FrequencyRange();
}

void
MainWindow::regionOutlined(QRect r)
{
    cerr << "MainWindow::regionOutlined(" << r.x() << "," << r.y() << "," << r.width() << "," << r.height() << ")" << endl;

    Pane *pane = qobject_cast<Pane *>(sender());
    if (!pane) {
        cerr << "MainWindow::regionOutlined: not sent by pane, ignoring" << endl;
        return;
    }

    if (!m_analyser) {
        cerr << "MainWindow::regionOutlined: no analyser, ignoring" << endl;
        return;
    }

    SpectrogramLayer *spectrogram = qobject_cast<SpectrogramLayer *>
        (m_analyser->getLayer(Analyser::Spectrogram));
    if (!spectrogram) {
        cerr << "MainWindow::regionOutlined: no spectrogram layer, ignoring" << endl;
        return;
    }

    sv_frame_t f0 = pane->getFrameForX(r.x());
    sv_frame_t f1 = pane->getFrameForX(r.x() + r.width());
    
    double v0 = spectrogram->getFrequencyForY(pane, r.y() + r.height());
    double v1 = spectrogram->getFrequencyForY(pane, r.y());

    cerr << "MainWindow::regionOutlined: frame " << f0 << " -> " << f1 
         << ", frequency " << v0 << " -> " << v1 << endl;

    m_pendingConstraint = Analyser::FrequencyRange(v0, v1);

    Selection sel(f0, f1);
    m_viewManager->setSelection(sel);
}

void
MainWindow::clearPitches()
{
    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    CommandHistory::getInstance()->startCompoundOperation(tr("Clear Pitches"), true);

    for (MultiSelection::SelectionList::iterator k = selections.begin();
         k != selections.end(); ++k) {
        m_analyser->deletePitches(*k);
        auxSnapNotes(*k);
    }

    CommandHistory::getInstance()->endCompoundOperation();
}

void
MainWindow::octaveShift(bool up)
{
    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    CommandHistory::getInstance()->startCompoundOperation
        (up ? tr("Choose Higher Octave") : tr("Choose Lower Octave"), true);

    for (MultiSelection::SelectionList::iterator k = selections.begin();
         k != selections.end(); ++k) {

        m_analyser->shiftOctave(*k, up);
        auxSnapNotes(*k);
    }

    CommandHistory::getInstance()->endCompoundOperation();
}

void
MainWindow::togglePitchCandidates()
{
    m_analyser->showPitchCandidates(!m_analyser->arePitchCandidatesShown());

    updateMenuStates();
}

void
MainWindow::switchPitchUp()
{
    if (m_analyser->arePitchCandidatesShown()) {
        if (m_analyser->haveHigherPitchCandidate()) {

            CommandHistory::getInstance()->startCompoundOperation
                (tr("Choose Higher Pitch Candidate"), true);

            MultiSelection::SelectionList selections = m_viewManager->getSelections();

            for (MultiSelection::SelectionList::iterator k = selections.begin();
                 k != selections.end(); ++k) {
                m_analyser->switchPitchCandidate(*k, true);
                auxSnapNotes(*k);
            }

            CommandHistory::getInstance()->endCompoundOperation();
        }
    } else {
        octaveShift(true);
    }
}

void
MainWindow::switchPitchDown()
{
    if (m_analyser->arePitchCandidatesShown()) {
        if (m_analyser->haveLowerPitchCandidate()) {

            CommandHistory::getInstance()->startCompoundOperation
                (tr("Choose Lower Pitch Candidate"), true);

            MultiSelection::SelectionList selections = m_viewManager->getSelections();
            
            for (MultiSelection::SelectionList::iterator k = selections.begin();
                 k != selections.end(); ++k) {
                m_analyser->switchPitchCandidate(*k, false);
                auxSnapNotes(*k);
            }

            CommandHistory::getInstance()->endCompoundOperation();
        }
    } else {
        octaveShift(false);
    }
}

void
MainWindow::snapNotesToPitches()
{
    cerr << "in snapNotesToPitches" << endl;
    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    if (!selections.empty()) {

        CommandHistory::getInstance()->startCompoundOperation
            (tr("Snap Notes to Pitches"), true);
                
        for (MultiSelection::SelectionList::iterator k = selections.begin();
             k != selections.end(); ++k) {
            auxSnapNotes(*k);
        }
        
        CommandHistory::getInstance()->endCompoundOperation();
    }
}

void
MainWindow::auxSnapNotes(Selection s)
{
    cerr << "in auxSnapNotes" << endl;
    FlexiNoteLayer *layer =
        qobject_cast<FlexiNoteLayer *>(m_analyser->getLayer(Analyser::Notes));
    if (!layer) return;

    layer->snapSelectedNotesToPitchTrack(m_analyser->getPane(), s);
}    

void
MainWindow::splitNote()
{
    FlexiNoteLayer *layer =
        qobject_cast<FlexiNoteLayer *>(m_analyser->getLayer(Analyser::Notes));
    if (!layer) return;

    layer->splitNotesAt(m_analyser->getPane(), m_viewManager->getPlaybackFrame());
}

void
MainWindow::mergeNotes()
{
    FlexiNoteLayer *layer =
        qobject_cast<FlexiNoteLayer *>(m_analyser->getLayer(Analyser::Notes));
    if (!layer) return;

    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    if (!selections.empty()) {

        CommandHistory::getInstance()->startCompoundOperation
            (tr("Merge Notes"), true);
                
        for (MultiSelection::SelectionList::iterator k = selections.begin();
             k != selections.end(); ++k) {
            layer->mergeNotes(m_analyser->getPane(), *k, true);
        }
        
        CommandHistory::getInstance()->endCompoundOperation();
    }
}

void
MainWindow::deleteNotes()
{
    FlexiNoteLayer *layer =
        qobject_cast<FlexiNoteLayer *>(m_analyser->getLayer(Analyser::Notes));
    if (!layer) return;

    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    if (!selections.empty()) {

        CommandHistory::getInstance()->startCompoundOperation
            (tr("Delete Notes"), true);
                
        for (MultiSelection::SelectionList::iterator k = selections.begin();
             k != selections.end(); ++k) {
            layer->deleteSelectionInclusive(*k);
        }
        
        CommandHistory::getInstance()->endCompoundOperation();
    }
}


void
MainWindow::formNoteFromSelection()
{
    Pane *pane = m_analyser->getPane();
    Layer *layer0 = m_analyser->getLayer(Analyser::Notes);
    auto model = ModelById::getAs<NoteModel>(layer0->getModel());
    FlexiNoteLayer *layer = qobject_cast<FlexiNoteLayer *>(layer0);
    if (!layer || !model) return;

    MultiSelection::SelectionList selections = m_viewManager->getSelections();

    if (!selections.empty()) {
    
        CommandHistory::getInstance()->startCompoundOperation
            (tr("Form Note from Selection"), true);

        for (MultiSelection::SelectionList::iterator k = selections.begin();
             k != selections.end(); ++k) {

            // Chop existing events at start and end frames; remember
            // the first starting pitch, to use as default for new
            // note; delete existing events; create new note; ask
            // layer to merge, just in order to adapt the note to the
            // existing pitch track if possible. This way we should
            // handle all the possible cases of existing notes that
            // may or may not overlap the start or end times
            
            sv_frame_t start = k->getStartFrame();
            sv_frame_t end = k->getEndFrame();

            EventVector existing =
                model->getEventsStartingWithin(start, end - start);

            int defaultPitch = 100;
            if (!existing.empty()) {
                defaultPitch = int(roundf(existing.begin()->getValue()));
            }
            
            layer->splitNotesAt(pane, start);
            layer->splitNotesAt(pane, end);
            layer->deleteSelection(*k);
            
            layer->addNoteOn(start, defaultPitch, 100);
            layer->addNoteOff(end, defaultPitch);
            
            layer->mergeNotes(pane, *k, false);
        }

        CommandHistory::getInstance()->endCompoundOperation();     
    }
}

void
MainWindow::playSpeedChanged(int position)
{
    PlaySpeedRangeMapper mapper;

    double percent = m_playSpeed->mappedValue();
    double factor = mapper.getFactorForValue(percent);

    int centre = m_playSpeed->defaultValue();

    // Percentage is shown to 0dp if >100, to 1dp if <100; factor is
    // shown to 3sf

    char pcbuf[30];
    char facbuf[30];
    
    if (position == centre) {
        contextHelpChanged(tr("Playback speed: Normal"));
    } else if (position < centre) {
        sprintf(pcbuf, "%.1f", percent);
        sprintf(facbuf, "%.3g", 1.0 / factor);
        contextHelpChanged(tr("Playback speed: %1% (%2x slower)")
                           .arg(pcbuf)
                           .arg(facbuf));
    } else {
        sprintf(pcbuf, "%.0f", percent);
        sprintf(facbuf, "%.3g", factor);
        contextHelpChanged(tr("Playback speed: %1% (%2x faster)")
                           .arg(pcbuf)
                           .arg(facbuf));
    }

    m_playSource->setTimeStretch(1.0 / factor); // factor is a speedup

    updateMenuStates();
}

void
MainWindow::playSharpenToggled()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("playsharpen", m_playSharpen->isChecked());
    settings.endGroup();

    playSpeedChanged(m_playSpeed->value());
    // TODO: pitch gain?
}

void
MainWindow::playMonoToggled()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("playmono", m_playMono->isChecked());
    settings.endGroup();

    playSpeedChanged(m_playSpeed->value());
    // TODO: pitch gain?
}    

void
MainWindow::speedUpPlayback()
{
    int value = m_playSpeed->value();
    value = value + m_playSpeed->pageStep();
    if (value > m_playSpeed->maximum()) value = m_playSpeed->maximum();
    m_playSpeed->setValue(value);
}

void
MainWindow::slowDownPlayback()
{
    int value = m_playSpeed->value();
    value = value - m_playSpeed->pageStep();
    if (value < m_playSpeed->minimum()) value = m_playSpeed->minimum();
    m_playSpeed->setValue(value);
}

void
MainWindow::restoreNormalPlayback()
{
    m_playSpeed->setValue(m_playSpeed->defaultValue());
}

void
MainWindow::audioGainChanged(float gain)
{
    double db = AudioLevel::voltage_to_dB(gain);
    cerr << "gain = " << gain << " (" << db << " dB)" << endl;
    contextHelpChanged(tr("Audio Gain: %1 dB").arg(db));
    if (gain == 0.f) {
        m_analyser->setAudible(Analyser::Audio, false);
    } else {
        m_analyser->setAudible(Analyser::Audio, true);
        m_analyser->setGain(Analyser::Audio, gain);
    }
    updateMenuStates();
} 

void
MainWindow::pitchGainChanged(float gain)
{
    double db = AudioLevel::voltage_to_dB(gain);
    cerr << "gain = " << gain << " (" << db << " dB)" << endl;
    contextHelpChanged(tr("Pitch Gain: %1 dB").arg(db));
    if (gain == 0.f) {
        m_analyser->setAudible(Analyser::PitchTrack, false);
    } else {
        m_analyser->setAudible(Analyser::PitchTrack, true);
        m_analyser->setGain(Analyser::PitchTrack, gain);
    }
    updateMenuStates();
} 

void
MainWindow::notesGainChanged(float gain)
{
    double db = AudioLevel::voltage_to_dB(gain);
    cerr << "gain = " << gain << " (" << db << " dB)" << endl;
    contextHelpChanged(tr("Notes Gain: %1 dB").arg(db));
    if (gain == 0.f) {
        m_analyser->setAudible(Analyser::Notes, false);
    } else {
        m_analyser->setAudible(Analyser::Notes, true);
        m_analyser->setGain(Analyser::Notes, gain);
    }
    updateMenuStates();
} 

void
MainWindow::audioPanChanged(float pan)
{
    contextHelpChanged(tr("Audio Pan: %1").arg(pan));
    m_analyser->setPan(Analyser::Audio, pan);
    updateMenuStates();
} 

void
MainWindow::pitchPanChanged(float pan)
{
    contextHelpChanged(tr("Pitch Pan: %1").arg(pan));
    m_analyser->setPan(Analyser::PitchTrack, pan);
    updateMenuStates();
} 

void
MainWindow::notesPanChanged(float pan)
{
    contextHelpChanged(tr("Notes Pan: %1").arg(pan));
    m_analyser->setPan(Analyser::Notes, pan);
    updateMenuStates();
} 

void
MainWindow::updateVisibleRangeDisplay(Pane *p) const
{
    if (!getMainModel() || !p) {
        return;
    }

    // The countdown of a pre-roll's lead-in has the status bar to itself
    if (showTakeCountdown()) return;

    bool haveSelection = false;
    sv_frame_t startFrame = 0, endFrame = 0;

    if (m_viewManager && m_viewManager->haveInProgressSelection()) {

        bool exclusive = false;
        Selection s = m_viewManager->getInProgressSelection(exclusive);

        if (!s.isEmpty()) {
            haveSelection = true;
            startFrame = s.getStartFrame();
            endFrame = s.getEndFrame();
        }
    }

    if (!haveSelection) {
        startFrame = p->getFirstVisibleFrame();
        endFrame = p->getLastVisibleFrame();
    }

    RealTime start = RealTime::frame2RealTime
        (startFrame, getMainModel()->getSampleRate());

    RealTime end = RealTime::frame2RealTime
        (endFrame, getMainModel()->getSampleRate());

    RealTime duration = end - start;

    QString startStr, endStr, durationStr;
    startStr = start.toText(true).c_str();
    endStr = end.toText(true).c_str();
    durationStr = duration.toText(true).c_str();

    if (haveSelection) {
        m_myStatusMessage = tr("Selection: %1 to %2 (duration %3)")
            .arg(startStr).arg(endStr).arg(durationStr);
    } else {
        m_myStatusMessage = tr("Visible: %1 to %2 (duration %3)")
            .arg(startStr).arg(endStr).arg(durationStr);
    }
    
    getStatusLabel()->setText(m_myStatusMessage);
}

void
MainWindow::updatePositionStatusDisplays() const
{
    if (!statusBar()->isVisible()) return;

}

void
MainWindow::monitoringLevelsChanged(float left, float right)
{
    m_fader->setPeakLeft(left);
    m_fader->setPeakRight(right);
}

void
MainWindow::sampleRateMismatch(sv_samplerate_t ,
                               sv_samplerate_t ,
                               bool )
{
    updateDescriptionLabel();
}

void
MainWindow::audioOverloadPluginDisabled()
{
    QMessageBox::information
        (this, tr("Audio processing overload"),
         tr("<b>Overloaded</b><p>Audio effects plugin auditioning has been disabled due to a processing overload."));
}

void
MainWindow::layerRemoved(Layer *layer)
{
    MainWindowBase::layerRemoved(layer);
}

void
MainWindow::layerInAView(Layer *layer, bool inAView)
{
    MainWindowBase::layerInAView(layer, inAView);
}

void
MainWindow::modelAdded(ModelId model)
{
    MainWindowBase::modelAdded(model);
    auto dtvm = ModelById::getAs<DenseTimeValueModel>(model);
    if (dtvm) {
        cerr << "A dense time-value model (such as an audio file) has been loaded" << endl;

        // If we're loading background music, capture the model ID and return —
        // do NOT treat it as a singing track or queue any secondary analysis.
        if (m_loadingBackgroundMusic) {
            m_backgroundMusicModelId = model;
            return;
        }

        // A recording being made into the singing track is raw material,
        // not the singing track itself: record() gives it a layer of its
        // own and finishSingingTake() splices it in at the end.  The
        // singing analyser is left with the take's own audio, whose pitch
        // and notes stay on show for the duration.
        if (m_recordingAsSingingTrack && model != getMainModelId()) {
            m_currentRecordingModelId = model;
            cerr << "modelAdded: the recording of the take is model "
                 << model << endl;
            return;
        }

        // If there is already a main model and this is a new additional
        // audio model (not the realtime pitch model), treat it as the
        // singing track to be analysed with the secondary colour scheme.
        ModelId mainId = getMainModelId();
        if (!mainId.isNone() && model != mainId &&
            m_realtimePitchModelId != model) {
            // Guard against race: if modelAdded() fires twice quickly (e.g.
            // for an audio model and its alignment model), only set the
            // pending id once.
            if (m_pendingSingingModelId.isNone()) {
                m_pendingSingingModelId = model;

                // An audio file has been loaded as the singing track: the
                // take's own audio, or one the user chose.
                // loadSingingTrack() runs the analysis itself as soon as
                // openPath() returns (it has to happen before the extra
                // pane is pruned); this deferred call is what sets up the
                // singing track of a session being restored, and the
                // fallback for any other route that adds a model.
                QTimer::singleShot
                    (0, this, SLOT(analyseRestoredSingingModel()));
            } else {
                cerr << "modelAdded: m_pendingSingingModelId already set, ignoring model "
                     << model << endl;
            }
        }
    }
}

void
MainWindow::mainModelChanged(ModelId model)
{
    m_panLayer->setModel(model);

    MainWindowBase::mainModelChanged(model);

    if (m_playTarget || m_audioIO) {
        connect(m_fader, SIGNAL(valueChanged(float)),
                this, SLOT(mainModelGainChanged(float)));
    }
}

void
MainWindow::mainModelGainChanged(float gain)
{
    if (m_playTarget) {
        m_playTarget->setOutputGain(gain);
    } else if (m_audioIO) {
        m_audioIO->setOutputGain(gain);
    }
}

void
MainWindow::analyseNow()
{
    cerr << "analyseNow called" << endl;

    // Not during a take.  The analysis of a take is started from here
    // when the take ends (recordCompleted, by which time isRecording()
    // is false).  Run in the middle of one, it would analyse the part
    // recorded so far and end the take's bookkeeping early, so that the
    // end of the take would re-analyse the reference instead, discarding
    // any edits made to its pitch track.
    if (m_recordTarget && m_recordTarget->isRecording()) {
        cerr << "analyseNow: recording in progress, ignoring" << endl;
        return;
    }

    // A take that has just stopped is not analysed where it is: the
    // recording is raw material, to be spliced into the take's audio at
    // the position it was started from.  finishSingingTake() does that,
    // and rebuilds the singing track from the file that comes out.
    if (m_recordingAsSingingTrack) {
        cerr << "analyseNow: the take that has just stopped goes into the "
             << "singing track" << endl;
        finishSingingTake();
        return;
    }

    if (!m_analyser) return;

    CommandHistory::getInstance()->startCompoundOperation
        (tr("Analyse Audio"), true);

    QString error = m_analyser->analyseExistingFile();

    CommandHistory::getInstance()->endCompoundOperation();

    // The singing of a take is analysed again too, over its coverage
    analyseTakeCoverage();

    if (error != "") {
        QMessageBox::warning
            (this,
             tr("Failed to analyse audio"),
             tr("<b>Analysis failed</b><p>%1</p>").arg(error),
             QMessageBox::Ok);
    }

    // If this analyseNow was triggered by recording completion, the
    // realtime pitch layer goes when the full pYIN analysis is available.
    if (m_realtimePitchTracker || m_realtimePitchLayer) {
        recordingFinishedFull(error == "" ? m_analyser : nullptr);
    }
}

void
MainWindow::analyseNewMainModel()
{
    // When recording as a singing track alongside an existing reference track
    // (RecordCreateAdditionalModel mode), MainWindowBase::record() still emits
    // audioFileLoaded() at the end — which triggers this slot.  But in that
    // mode the main model has NOT changed (it is still the reference track),
    // so there is nothing for this slot to do.  All secondary-track setup is
    // handled by modelAdded() → setupSingingTrackAnalyser().  Proceeding here
    // would wrongly call m_analyser->newFileLoaded() on the reference track a
    // second time, tearing down its existing pitch/note layers and re-running
    // pYIN — causing errors, crashes, and a corrupt UI state.
    if (m_recordingAsSingingTrack) {
        cerr << "analyseNewMainModel: recording-as-singing-track mode, skipping (main model unchanged)" << endl;
        return;
    }

    auto model = getMainModel();

    SVDEBUG << "MainWindow::analyseNewMainModel: main model is " << model << endl;

    SVDEBUG << "(document is " << m_document << ", it says main model is " << m_document->getMainModel() << ")" << endl;
    
    if (!model) {
        cerr << "no main model!" << endl;
        return;
    }

    if (!m_paneStack) {
        cerr << "no pane stack!" << endl;
        return;
    }

    // openAudio() emits audioFileLoaded() for CreateAdditionalModel too (a
    // singing track, background music), with the main model unchanged.
    // Going on would hand the reference to m_analyser a second time, which
    // keeps its layers but forgets its pitch candidates, and would connect
    // the pane's regionOutlined() to us once more.
    if (getMainModelId() == m_analysedMainModelId) {
        SVDEBUG << "MainWindow::analyseNewMainModel: main model unchanged, nothing to do" << endl;
        return;
    }
    m_analysedMainModelId = getMainModelId();

    int pc = m_paneStack->getPaneCount();
    Pane *pane = 0;
    Pane *selectionStrip = 0;

    if (pc < 2) {
        SVDEBUG << "MainWindow::analyseNewMainModel: Adding pane and selection strip (ruler)" << endl;
        pane = m_paneStack->addPane();
        selectionStrip = m_paneStack->addPane();
        m_document->attachLayerToView
            (selectionStrip,
             m_document->createMainModelLayer(LayerFactory::TimeRuler));
    } else {
        pane = m_paneStack->getPane(0);
        selectionStrip = m_paneStack->getPane(1);
    }

    pane->setPlaybackFollow(PlaybackScrollPage);

    if (selectionStrip) {
        selectionStrip->setPlaybackFollow(PlaybackScrollPage);
        selectionStrip->setFixedHeight(26);
        m_paneStack->sizePanesEqually();
        m_viewManager->clearToolModeOverrides();
        m_viewManager->setToolModeFor(selectionStrip,
                                      ViewManager::SelectMode);
    }

    if (pane) {

        // The reference is the work of this pane, and its end is the end
        // of the song.  Without saying so, the pane blocks itself off at
        // the end of whichever audio model happens to be topmost: the
        // take's file, which stops where the singing did, or the
        // recording in progress, whose end crawls along behind the
        // playback cursor.  Either greys out the pane from there on.
        pane->setWorkModel(getMainModelId());

        disconnect(pane, SIGNAL(regionOutlined(QRect)),
                   pane, SLOT(zoomToRegion(QRect)));
        connect(pane, SIGNAL(regionOutlined(QRect)),
                this, SLOT(regionOutlined(QRect)));

        QString error = m_analyser->newFileLoaded
            (m_document, getMainModelId(), m_paneStack, pane);
        if (error != "") {
            QMessageBox::warning
                (this,
                 tr("Failed to analyse audio"),
                 tr("<b>Analysis failed</b><p>%1</p>").arg(error),
                 QMessageBox::Ok);
        }
    }

    // A session saved with the alternate pitch track has its layer in
    // the pane already
    if (pane && m_alternatePitch->adopt(m_document, pane)) {
        cerr << "analyseNewMainModel: found the alternate pitch track of the session, "
             << m_alternatePitch->getOctaves() << " octave(s)" << endl;
        syncAlternatePitchTrack();
    }

    if (!m_withSpectrogram) {
        m_analyser->setVisible(Analyser::Spectrogram, false);
    }

    if (!m_withSonification) {
        m_analyser->setAudible(Analyser::PitchTrack, false);
        m_analyser->setAudible(Analyser::Notes, false);
    }

    // A session used to be searched here for a second WaveFileModel, which
    // was then set up as the singing track.  The takes of a session come
    // from its <takes> element now (spec 7), and restoreTakes() opens the
    // audio of the active take itself, so there is nothing to look for.

    updateLayerStatuses();
    documentRestored();
}

void
MainWindow::modelGenerationFailed(QString transformName, QString message)
{
    if (message != "") {

        QMessageBox::warning
            (this,
             tr("Failed to generate layer"),
             tr("<b>Layer generation failed</b><p>Failed to generate derived layer.<p>The layer transform \"%1\" failed:<p>%2")
             .arg(transformName).arg(message),
             QMessageBox::Ok);
    } else {
        QMessageBox::warning
            (this,
             tr("Failed to generate layer"),
             tr("<b>Layer generation failed</b><p>Failed to generate a derived layer.<p>The layer transform \"%1\" failed.<p>No error information is available.")
             .arg(transformName),
             QMessageBox::Ok);
    }
}

void
MainWindow::modelGenerationWarning(QString /* transformName */, QString message)
{
    QMessageBox::warning
        (this, tr("Warning"), message, QMessageBox::Ok);
}

void
MainWindow::modelRegenerationFailed(QString layerName,
                                    QString transformName,
                                    QString message)
{
    if (message != "") {

        QMessageBox::warning
            (this,
             tr("Failed to regenerate layer"),
             tr("<b>Layer generation failed</b><p>Failed to regenerate derived layer \"%1\" using new data model as input.<p>The layer transform \"%2\" failed:<p>%3")
             .arg(layerName).arg(transformName).arg(message),
             QMessageBox::Ok);
    } else {
        QMessageBox::warning
            (this,
             tr("Failed to regenerate layer"),
             tr("<b>Layer generation failed</b><p>Failed to regenerate derived layer \"%1\" using new data model as input.<p>The layer transform \"%2\" failed.<p>No error information is available.")
             .arg(layerName).arg(transformName),
             QMessageBox::Ok);
    }
}

void
MainWindow::modelRegenerationWarning(QString layerName,
                                     QString /* transformName */,
                                     QString message)
{
    QMessageBox::warning
        (this, tr("Warning"), tr("<b>Warning when regenerating layer</b><p>When regenerating the derived layer \"%1\" using new data model as input:<p>%2").arg(layerName).arg(message), QMessageBox::Ok);
}

void
MainWindow::alignmentFailed(ModelId, QString message)
{
    QMessageBox::warning
        (this,
         tr("Failed to calculate alignment"),
         tr("<b>Alignment calculation failed</b><p>Failed to calculate an audio alignment:<p>%1")
         .arg(message),
         QMessageBox::Ok);
}

void
MainWindow::paneRightButtonMenuRequested(Pane *pane, QPoint position)
{
//    cerr << "MainWindow::rightButtonMenuRequested(" << pane << ", " << position.x() << ", " << position.y() << ")" << endl;
    m_paneStack->setCurrentPane(pane);
    m_rightButtonMenu->popup(position);
}

void
MainWindow::panePropertiesRightButtonMenuRequested(Pane *, QPoint)
{
}

void
MainWindow::layerPropertiesRightButtonMenuRequested(Pane *, Layer *, QPoint)
{
}

void
MainWindow::handleOSCMessage(const OSCMessage &)
{
    cerr << "MainWindow::handleOSCMessage: Not implemented" << endl;
}

void
MainWindow::mouseEnteredWidget()
{
    QWidget *w = qobject_cast<QWidget *>(sender());
    if (!w) return;

    if (w == m_fader) {
        contextHelpChanged(tr("Adjust the master playback level"));
    } else if (w == m_playSpeed) {
        contextHelpChanged(tr("Adjust the master playback speed"));
    } else if (w == m_playSharpen && w->isEnabled()) {
        contextHelpChanged(tr("Toggle transient sharpening for playback time scaling"));
    } else if (w == m_playMono && w->isEnabled()) {
        contextHelpChanged(tr("Toggle mono mode for playback time scaling"));
    }
}

void
MainWindow::mouseLeftWidget()
{
    contextHelpChanged("");
}

void
MainWindow::betaReleaseWarning()
{
    QMessageBox::information
        (this, tr("Beta release"),
         tr("<b>This is a beta release of %1</b><p>Please see the \"What's New\" option in the Help menu for a list of changes since the last proper release.</p>").arg(QApplication::applicationName()));
}

void
MainWindow::help()
{
    //!!! todo: help URL!
    openHelpUrl(tr("http://code.soundsoftware.ac.uk/projects/tony/wiki/Reference"));
}

void
MainWindow::whatsNew()
{
    QFile changelog(":CHANGELOG");
    changelog.open(QFile::ReadOnly);
    QByteArray content = changelog.readAll();
    QString text = QString::fromUtf8(content);

    QDialog *d = new QDialog(this);
    d->setWindowTitle(tr("What's New"));
        
    QGridLayout *layout = new QGridLayout;
    d->setLayout(layout);

    int row = 0;
    
    QLabel *iconLabel = new QLabel;
    iconLabel->setPixmap(QApplication::windowIcon().pixmap(64, 64));
    layout->addWidget(iconLabel, row, 0);
    
    layout->addWidget
        (new QLabel(tr("<h3>What's New in %1</h3>")
                    .arg(QApplication::applicationName())),
         row++, 1);
    layout->setColumnStretch(2, 10);

    QTextEdit *textEdit = new QTextEdit;
    layout->addWidget(textEdit, row++, 1, 1, 2);

    if (m_newerVersionIs != "") {
        layout->addWidget(new QLabel(tr("<b>Note:</b> A newer version of %1 is available.<br>(Version %2 is available; you are using version %3)").arg(QApplication::applicationName()).arg(m_newerVersionIs).arg(TONY_VERSION)), row++, 1, 1, 2);
    }
    
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok);
    layout->addWidget(bb, row++, 0, 1, 3);
    connect(bb, SIGNAL(accepted()), d, SLOT(accept()));
    
    // Remove spurious linefeeds from DOS line endings
    text.replace('\r', "");

    // Un-wrap indented paragraphs (assume they are always preceded by
    // an empty line, so don't get merged into prior para)
    text.replace(QRegularExpression("(.)\n +(.)"), "\\1 \\2");

    // Rest of para following a " - " at start becomes bulleted entry
    text.replace(QRegularExpression("\n+ - ([^\n]+)"), "\n<li>\\1</li>");

    // Line-ending ":" introduces the bulleted list
    text.replace(QRegularExpression(": *\n"), ":\n<ul>\n");

    // Blank line (after unwrapping) ends the bulleted list
    text.replace(QRegularExpression("</li>\n\\s*\n"), "</li>\n</ul>\n\n");

    // Text leading up to that line-ending ":" becomes bold heading
    text.replace(QRegularExpression("\n(\\w[^:\n]+:)"), "\n<p><b>\\1</b></p>");
    
    textEdit->setHtml(text);
    textEdit->setReadOnly(true);

    d->setMinimumSize(m_viewManager->scalePixelSize(520),
                      m_viewManager->scalePixelSize(450));
    
    d->exec();

    delete d;
}

QString
MainWindow::getReleaseText() const
{
    bool debug = false;
    QString version = "(unknown version)";

#ifdef BUILD_DEBUG
    debug = true;
#endif // BUILD_DEBUG
#ifdef TONY_VERSION
#ifdef SVNREV
    version = tr("Release %1 : Revision %2").arg(TONY_VERSION).arg(SVNREV);
#else // !SVNREV
    version = tr("Release %1").arg(TONY_VERSION);
#endif // SVNREV
#else // !TONY_VERSION
#ifdef SVNREV
    version = tr("Unreleased : Revision %1").arg(SVNREV);
#endif // SVNREV
#endif // TONY_VERSION

    return tr("%1 : %2 configuration, %3-bit build")
        .arg(version)
        .arg(debug ? tr("Debug") : tr("Release"))
        .arg(sizeof(void *) * 8);
}

void
MainWindow::about()
{
    QString aboutText;

    aboutText += tr("<h3>About Tony</h3>");
    aboutText += tr("<p>Tony is a program for interactive note and pitch analysis and annotation.</p>");
    aboutText += QString("<p><small>%1</small></p>").arg(getReleaseText());
    aboutText += tr("<p>Using Qt framework version %1.</p>")
        .arg(QT_VERSION_STR);

    aboutText += 
        "<p>Copyright &copy; 2005&ndash;2019 Chris Cannam, Queen Mary University of London, and the Tony project authors: Matthias Mauch, George Fazekas, Justin Salamon, and Rachel Bittner.</p>"
        "<p>pYIN analysis plugin written by Matthias Mauch.</p>"
        "<p>This program is free software; you can redistribute it and/or "
        "modify it under the terms of the GNU General Public License as "
        "published by the Free Software Foundation; either version 2 of the "
        "License, or (at your option) any later version.<br>See the file "
        "COPYING included with this distribution for more information.</p>";
    
    // use our own dialog so we can influence the size

    QDialog *d = new QDialog(this);

    d->setWindowTitle(tr("About %1").arg(QApplication::applicationName()));
        
    QGridLayout *layout = new QGridLayout;
    d->setLayout(layout);

    int row = 0;
    
    QLabel *iconLabel = new QLabel;
    iconLabel->setPixmap(QApplication::windowIcon().pixmap(64, 64));
    layout->addWidget(iconLabel, row, 0, Qt::AlignTop);

    QLabel *mainText = new QLabel();
    layout->addWidget(mainText, row, 1, 1, 2);

    layout->setRowStretch(row, 10);
    layout->setColumnStretch(1, 10);

    ++row;

    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok);
    layout->addWidget(bb, row++, 0, 1, 3);
    connect(bb, SIGNAL(accepted()), d, SLOT(accept()));

    mainText->setWordWrap(true);
    mainText->setOpenExternalLinks(true);
    mainText->setText(aboutText);

    d->setMinimumSize(m_viewManager->scalePixelSize(420),
                      m_viewManager->scalePixelSize(200));
    
    d->exec();

    delete d;
}

void
MainWindow::keyReference()
{
    m_keyReference->show();
}

void
MainWindow::newerVersionAvailable(QString version)
{
    m_newerVersionIs = version;
    
    //!!! nicer URL would be nicer
    QSettings settings;
    settings.beginGroup("NewerVersionWarning");
    QString tag = QString("version-%1-available-show").arg(version);
    if (settings.value(tag, true).toBool()) {
        QString title(tr("Newer version available"));
        QString text(tr("<h3>Newer version available</h3><p>You are using version %1 of Tony, but version %2 is now available.</p><p>Please see the <a href=\"http://code.soundsoftware.ac.uk/projects/tony/\">Tony website</a> for more information.</p>").arg(TONY_VERSION).arg(version));
        QMessageBox::information(this, title, text);
        settings.setValue(tag, false);
    }
    settings.endGroup();
}

void
MainWindow::ffwd()
{
    if (!getMainModel()) return;

    sv_frame_t frame = m_viewManager->getPlaybackFrame();
    ++frame;

    sv_samplerate_t sr = getMainModel()->getSampleRate();

    // The step is supposed to scale and be as wide as a step of 
    // m_defaultFfwdRwdStep seconds at zoom level 720 and sr = 44100
    
    ZoomLevel zoom = m_viewManager->getGlobalZoom();
    double framesPerPixel = 1.0;
    if (zoom.zone == ZoomLevel::FramesPerPixel) {
        framesPerPixel = zoom.level;
    } else {
        framesPerPixel = 1.0 / zoom.level;
    }
    double defaultFramesPerPixel = (720 * 44100) / sr;
    double scaler = framesPerPixel / defaultFramesPerPixel;
    RealTime step = m_defaultFfwdRwdStep * scaler;
    
    frame = RealTime::realTime2Frame
        (RealTime::frame2RealTime(frame, sr) + step, sr);

    if (frame > getMainModel()->getEndFrame()) {
        frame = getMainModel()->getEndFrame();
    }
       
    if (frame < 0) frame = 0;

    if (m_viewManager->getPlaySelectionMode()) {
        frame = m_viewManager->constrainFrameToSelection(frame);
    }
    
    m_viewManager->setPlaybackFrame(frame);

    if (frame == getMainModel()->getEndFrame() &&
        m_playSource &&
        m_playSource->isPlaying() &&
        !m_viewManager->getPlayLoopMode()) {
        stop();
    }
}

void
MainWindow::rewind()
{
    if (!getMainModel()) return;

    sv_frame_t frame = m_viewManager->getPlaybackFrame();
    if (frame > 0) --frame;

    sv_samplerate_t sr = getMainModel()->getSampleRate();

    // The step is supposed to scale and be as wide as a step of 
    // m_defaultFfwdRwdStep seconds at zoom level 720 and sr = 44100

    ZoomLevel zoom = m_viewManager->getGlobalZoom();
    double framesPerPixel = 1.0;
    if (zoom.zone == ZoomLevel::FramesPerPixel) {
        framesPerPixel = zoom.level;
    } else {
        framesPerPixel = 1.0 / zoom.level;
    }
    double defaultFramesPerPixel = (720 * 44100) / sr;
    double scaler = framesPerPixel / defaultFramesPerPixel;
    RealTime step = m_defaultFfwdRwdStep * scaler;

    frame = RealTime::realTime2Frame
        (RealTime::frame2RealTime(frame, sr) - step, sr);
    
    if (frame < getMainModel()->getStartFrame()) {
        frame = getMainModel()->getStartFrame();
    }

    if (frame < 0) frame = 0;

    if (m_viewManager->getPlaySelectionMode()) {
        frame = m_viewManager->constrainFrameToSelection(frame);
    }

    m_viewManager->setPlaybackFrame(frame);
}
