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
#include "LatencyUtils.h"
#include "PaneUtils.h"

#include "framework/Document.h"
#include "framework/VersionTester.h"

#include "view/Pane.h"
#include "view/PaneStack.h"
#include "data/model/WaveFileModel.h"
#include "data/model/WritableWaveFileModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"
#include "layer/FlexiNoteLayer.h"
#include "view/ViewManager.h"
#include "base/Preferences.h"
#include "base/RecordDirectory.h"
#include "base/AudioLevel.h"
#include "layer/WaveformLayer.h"
#include "layer/TimeInstantLayer.h"
#include "layer/TimeValueLayer.h"
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

// For version information
#include "vamp/vamp.h"
#include "vamp-sdk/PluginBase.h"
#include "plugin/api/ladspa.h"
#include "plugin/api/dssi.h"

#include <bqaudioio/SystemPlaybackTarget.h>
#include <bqaudioio/SystemAudioIO.h>

#include <QApplication>
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
    m_overview(0),
    m_showSingingPitch(nullptr),
    m_showSingingNotes(nullptr),
    m_playSingingAudio(nullptr),
    m_playRefWhileRecording(nullptr),
    m_loadSingingTrackAction(nullptr),
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
    m_paneCountBeforeRecording(0),
    m_currentRecordingModelId(),
    m_recordingLatencyFrames(0),
    m_recordingStartGapEstimate(0),
    m_recordingStartGapMeasured(-1),
    m_awaitingReferenceStart(false)
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
    connect(m_paneStack, SIGNAL(doubleClickSelectInvoked(sv_frame_t)),
            this, SLOT(doubleClickSelectInvoked(sv_frame_t)));
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
    connect(recordAction, SIGNAL(triggered()), this, SLOT(record()));
    connect(m_recordTarget, SIGNAL(recordStatusChanged(bool)),
	    recordAction, SLOT(setChecked(bool)));
    connect(m_recordTarget, SIGNAL(recordCompleted()),
	    this, SLOT(analyseNow()));
    connect(this, SIGNAL(canRecord(bool)),
            recordAction, SLOT(setEnabled(bool)));

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

    // Tear down singing track and realtime pitch layer before panes/document
    // are destroyed, so they can cleanly remove their layers from the pane.
    teardownRealtimePitchLayer();
    teardownSingingTrackAnalyser();
    teardownBackgroundMusic();
    m_pendingSingingModelId = {};
    m_currentRecordingModelId = {};
    m_recordingAsSingingTrack = false;
    m_analysedMainModelId = {};

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

    // Record the pane count before opening so we can remove any extra panes
    // that openPath(CreateAdditionalModel) creates via AddPaneCommand.
    // We want both tracks to share pane 0, not appear in separate panes.
    int paneCountBefore = m_paneStack ? m_paneStack->getPaneCount() : 0;

    FileOpenStatus status = openPath(path, CreateAdditionalModel);

    if (status == FileOpenFailed) {
        QMessageBox::critical(this, tr("Failed to open singing track"),
                              tr("<b>File open failed</b><p>File \"%1\" could not be opened").arg(path));
        return;
    } else if (status == FileOpenWrongMode) {
        QMessageBox::critical(this, tr("Failed to open singing track"),
                              tr("<b>Audio required</b><p>Could not open \"%1\" as audio").arg(path));
        return;
    }

    // modelAdded() fired synchronously inside openPath() and stored the new
    // model's id in m_pendingSingingModelId.  Set up the secondary analyser
    // NOW, before pruning the extra pane: the imported WaveformLayer in that
    // pane is the only layer referencing the singing model, so deleting it
    // first would make Document::releaseModel() free the model before it can
    // be analysed.  Once m_analyser2's own WaveformLayer references the model
    // the orphan can go.  This also clears m_pendingSingingModelId, so the
    // analyseNewSingingModel() call queued by modelAdded() becomes a no-op.
    ModelId singingModelId = m_pendingSingingModelId;
    analyseNewSingingModel();

    // openPath(CreateAdditionalModel) will have called AddPaneCommand which
    // added a new pane for the singing track's waveform layer.  We do NOT
    // want that extra pane — both tracks must overlay pane 0.  Remove any
    // panes above the original count (except the time-ruler pane at index 1
    // which was already there).  We delete from the top down so that index
    // arithmetic stays valid.  If the analyser setup above failed, nothing
    // else references the singing model and pruning releases it, which is
    // what we want.
    if (m_paneStack) {
        while (m_paneStack->getPaneCount() > paneCountBefore) {
            Pane *extra = m_paneStack->getPane(m_paneStack->getPaneCount() - 1);
            if (!extra) break;
            pruneExtraPane(extra, singingModelId);
        }
    }
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
                m_document->addLayerToView(pane, m_backgroundMusicLayer);

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

    // A take being recorded stays out of the mix until it is over.  The
    // play source happens to read ahead of what has been recorded, so the
    // take is silent anyway with the buffer sizes of today; this does not
    // depend on that.  Not with setAudible(), which would write the state
    // to the settings the reference shares.
    if (deferAnalysis) {
        m_singingAudioAfterTake = m_analyser2->isAudible(Analyser::Audio);
        if (Layer *audio = m_analyser2->getLayer(Analyser::Audio)) {
            if (auto params = audio->getPlayParameters()) {
                params->setPlayAudible(false);
                m_singingAudioMutedForTake = true;
            }
        }
    }

    // Re-stack layers so the primary pitch track stays on top
    m_analyser->getLayer(Analyser::PitchTrack);  // ensure primary is on top
    updateLayerStatuses();
    updateMenuStates();

    if (deferAnalysis) {
        emit activity(tr("Recording singing track — analysis will run when recording stops"));
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
MainWindow::restoreSingingAudioAfterTake()
{
    if (!m_singingAudioMutedForTake) return;
    m_singingAudioMutedForTake = false;
    if (!m_analyser2) return;
    if (Layer *audio = m_analyser2->getLayer(Analyser::Audio)) {
        if (auto params = audio->getPlayParameters()) {
            params->setPlayAudible(m_singingAudioAfterTake);
        }
    }
    updateLayerStatuses();
}

void
MainWindow::teardownSingingTrackAnalyser()
{
    m_singingAudioMutedForTake = false;

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
    m_realtimePitchLayer->setVerticalScale(TimeValueLayer::AutoAlignScale);
    m_realtimePitchLayer->setPlotStyle(TimeValueLayer::PlotPoints);

    // Singing/recording track uses the "Orange" colour so it is visually
    // distinct from the reference track (black) and notes (blue).
    ColourDatabase *cdb = ColourDatabase::getInstance();
    m_realtimePitchLayer->setBaseColour(cdb->getColourIndex(tr("Orange")));

    m_document->addLayerToView(pane, m_realtimePitchLayer);

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
MainWindow::record()
{
    // If recording is already in progress this click is a STOP request, not a
    // start request.  Delegate straight to the base class (which calls stop())
    // without doing any pre-flight teardown.  The teardown would destroy
    // m_analyser2 and remove the live recording model from m_playSource while
    // audio is still being captured — causing a crash or a null m_analyser2
    // when recordCompleted() fires analyseNow() moments later.
    if (m_recordTarget && m_recordTarget->isRecording()) {
        MainWindowBase::record();
        return;
    }

    // If a reference track is already loaded, record the microphone input as
    // the singing track rather than replacing the whole session.
    // We do this by temporarily switching to RecordCreateAdditionalModel so
    // that MainWindowBase::record() adds the WritableWaveFileModel as an
    // additional (non-main) model.  Our modelAdded() hook will then pick it
    // up and route it through setupSingingTrackAnalyser() with deferred pYIN.
    //
    // If there is no main model yet (first-time record), fall through with the
    // default RecordReplaceSession behaviour.

    bool haveReference = (getMainModel() != nullptr);

    if (haveReference) {
        cerr << "MainWindow::record: reference track loaded — recording as singing track" << endl;

        // If a previous singing-track recording (or loaded singing file) is
        // still active, discard it now before we start capturing a new one.
        // teardownRealtimePitchLayer() stops any live tracker still running
        // (edge case: user re-records before pYIN finished on the last one).
        // teardownSingingTrackAnalyser() removes the old recording's layers
        // from the pane and releases its model so the document is clean.
        // m_pendingSingingModelId is cleared so the modelAdded() race guard
        // doesn't block the new recording's model from being registered.
        if (m_realtimePitchTracker || m_realtimePitchLayer) {
            cerr << "MainWindow::record: tearing down leftover realtime pitch layer" << endl;
            teardownRealtimePitchLayer();
        }

        // Pre-flight orphan cleanup: delete the WaveformLayer that
        // MainWindowBase::record() created via createImportedLayer() for the
        // previous singing recording, and remove that model from m_playSource.
        //
        // This MUST be done before teardownSingingTrackAnalyser() (which calls
        // removeAllLayers() and would otherwise release the singing model while
        // the orphan layer still holds a reference) AND before deletePane()
        // (which would destroy the extra pane and leave a dangling pointer in
        // m_document->m_layerViewMap for the orphan layer — causing a crash in
        // deleteLayer(true) when it tries to call removeLayer on the dead pane).
        //
        // At this point the extra pane is still alive (deletePane hasn't run),
        // so m_layerViewMap contains a valid pane pointer, and deleteLayer(true)
        // is safe.
        //
        // Two cases arise depending on when the user presses Record again:
        //
        // (A) User re-records while pYIN is still running (or before
        //     recordingFinishedFull() has fired): m_currentRecordingModelId
        //     is still set to the previous recording's WritableWaveFileModel.
        //
        // (B) User re-records after pYIN has completed: recordingFinishedFull()
        //     already cleared m_currentRecordingModelId to {}.  However,
        //     m_analyser2 is still alive and its getMainModelId() still returns
        //     the previous singing model's ID (fileClosed() clears m_layers but
        //     NOT m_fileModel).  We use that ID for the orphan scan instead.
        //
        // In both cases we identify orphan layers by scanning all document
        // layers for any layer whose model matches the previous singing model ID,
        // excluding layers that m_analyser2 owns (those are cleaned up by
        // removeAllLayers() inside teardownSingingTrackAnalyser() below).
        {
            ModelId prevSingingModelId = m_currentRecordingModelId;
            if (prevSingingModelId.isNone() && m_analyser2) {
                prevSingingModelId = m_analyser2->getMainModelId();
                if (!prevSingingModelId.isNone()) {
                    cerr << "MainWindow::record: m_currentRecordingModelId cleared "
                         << "(post-pYIN re-record); using m_analyser2 model id "
                         << prevSingingModelId << " for orphan cleanup" << endl;
                }
            }

            if (m_document && !prevSingingModelId.isNone()) {
                std::vector<Layer *> orphans;
                for (Layer *layer : m_document->getLayers()) {
                    if (layer->getModel() != prevSingingModelId) continue;
                    // Skip layers owned by m_analyser2 — removeAllLayers() handles those.
                    bool ownedByAnalyser2 = false;
                    if (m_analyser2) {
                        for (int c = Analyser::Audio; c <= Analyser::Spectrogram; ++c) {
                            if (m_analyser2->getLayer(static_cast<Analyser::Component>(c)) == layer) {
                                ownedByAnalyser2 = true;
                                break;
                            }
                        }
                    }
                    if (!ownedByAnalyser2) {
                        orphans.push_back(layer);
                    }
                }
                for (Layer *orphan : orphans) {
                    cerr << "MainWindow::record: deleting orphan layer " << orphan
                         << " referencing previous singing model "
                         << prevSingingModelId << endl;
                    m_document->deleteLayer(orphan, true);
                }
                // deleteLayer(true) does not fire layerInAView(false).  The
                // model leaves the play source when it is released, which
                // is normally in the teardown below; this makes sure of it
                // even if something else still holds the model.
                if (m_playSource) {
                    m_playSource->removeModel(prevSingingModelId);
                }
                m_currentRecordingModelId = {};
            }
        }

        if (m_analyser2) {
            cerr << "MainWindow::record: tearing down previous singing-track analyser" << endl;
            teardownSingingTrackAnalyser();
        }
        m_pendingSingingModelId = {};
        m_recordingInProgress = false;

        m_recordingAsSingingTrack = true;
        m_recordingLatencyFrames = 0; // reset; will be computed in recordingStarted()
        m_recordingStartGapEstimate = 0;
        m_awaitingReferenceStart = false;
        m_recordingStartGapMeasured = -1;
        // Remember pane count so we can prune the extra pane that
        // MainWindowBase::record() creates via AddPaneCommand for the
        // recording's waveform layer.  We want both tracks in pane 0.
        m_paneCountBeforeRecording = m_paneStack ? m_paneStack->getPaneCount() : 0;
        setAudioRecordMode(RecordCreateAdditionalModel);
    } else {
        m_recordingAsSingingTrack = false;
        m_paneCountBeforeRecording = 0;
        setAudioRecordMode(RecordReplaceSession);
    }

    MainWindowBase::record();

    // The base class gives up without a signal when the device cannot be
    // opened or the recording cannot be started.  No take is coming then,
    // so nothing must be left waiting for one: with the flag still set,
    // the next file opened would not be analysed, and Analyse Now would
    // be routed to a singing track that is not there.
    if (!m_recordTarget || !m_recordTarget->isRecording()) {
        cerr << "MainWindow::record: recording did not start" << endl;
        m_recordingAsSingingTrack = false;
        m_recordingInProgress = false;
    }

    // Restore the default mode so that a subsequent "standalone" recording
    // (after the singing track session is closed) behaves correctly.
    setAudioRecordMode(RecordReplaceSession);

    // Remove any extra panes that AddPaneCommand created for the recording's
    // waveform layer.  setupSingingTrackAnalyser() will add a proper waveform
    // layer for the recording into pane 0, so the auto-created extra pane is
    // redundant and visually confusing.
    //
    // WHY WE CANNOT DELETE THE ORPHAN LAYER HERE:
    // At this point m_analyser2 has NOT yet been created — its setup is deferred
    // via QTimer::singleShot(0) queued inside modelAdded().  The orphan
    // WaveformLayer in the extra pane is currently the ONLY layer referencing
    // the WritableWaveFileModel being recorded into.  Calling
    // deleteLayer(orphan, true) would invoke Document::releaseModel(), which
    // would destroy the live recording model mid-capture — crash.
    //
    // WHY WE CANNOT CALL Pane::removeLayer() + deletePane() HERE:
    // Pane::removeLayer() removes the layer from the View's internal display
    // list but does NOT update Document::m_layerViewMap.  After deletePane()
    // destroys the widget, m_layerViewMap still contains the now-dangling pane
    // pointer.  On the next recording attempt, the pre-flight orphan cleanup
    // calls deleteLayer(orphan, true), which iterates m_layerViewMap and calls
    // (*j)->removeLayer(layer) on the stale pointer — use-after-free crash.
    //
    // SOLUTION: use PaneStack::hidePane() to move the extra pane out of the
    // visible list (so getPaneCount() drops back and the UI doesn't show it)
    // while keeping the widget alive with valid m_layerViewMap entries.
    // Store the pane in m_pendingExtraPanes.  setupSingingTrackAnalyser() will
    // drain that list once m_analyser2 is set up and its WaveformLayer holds a
    // reference to the recording model, at which point deleteLayer(orphan, true)
    // is safe (the model won't be freed because m_analyser2's layer still refs it)
    // and deletePane() can safely destroy the now-clean pane widget.
    if (m_recordingAsSingingTrack && m_paneStack) {
        while (m_paneStack->getPaneCount() > m_paneCountBeforeRecording) {
            Pane *extra = m_paneStack->getPane(m_paneStack->getPaneCount() - 1);
            if (!extra) break;

            // Unregister from the overview before hiding so it stops rendering.
            if (m_overview) m_overview->unregisterView(extra);

            // hidePane() moves the pane from the visible list to m_hiddenPanes,
            // calls pw->hide() on the widget, and updates getPaneCount() — so
            // this while loop will terminate correctly.
            m_paneStack->hidePane(extra);

            // Store for deferred cleanup in setupSingingTrackAnalyser().
            m_pendingExtraPanes.push_back(extra);

            cerr << "MainWindow::record: hiding extra pane " << extra
                 << " — deferred deletion queued for setupSingingTrackAnalyser" << endl;
        }
    }
}

void
MainWindow::recordingStarted()
{
    // recordStatusChanged(bool) is emitted both when recording starts
    // (true) and stops (false). We only want to act when it starts.
    if (!m_recordTarget) return;
    if (!m_recordTarget->isRecording()) {
        // Recording stopped - nothing to do here; recordingFinishedFull()
        // is called from analyseNow() once pYIN completes.
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
        // playback from frame 0 so the singer hears the reference track.
        // The audio IO was already resumed by record() so m_playSource
        // can be started directly without calling MainWindowBase::play()
        // (which would stop recording if isRecording() is true).
        if (m_recordingAsSingingTrack &&
            m_playRefWhileRecording && m_playRefWhileRecording->isChecked() &&
            m_playSource && !m_playSource->isPlaying()) {
            cerr << "MainWindow::recordingStarted: starting reference playback" << endl;

            // Measure round-trip hardware latency so we can compensate the
            // singing recording's timeline after the take.
            // output latency = time from play() call until audio exits the speaker
            // input latency  = time from sound entering the mic until it arrives here
            // The singer's response to reference frame 0 arrives in the recording
            // at approximately frame (outputLatency + inputLatency), so we will
            // shift the model's start frame by -(outputLatency + inputLatency).
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
            m_recordingLatencyFrames =
                computeRecordingLatency(outputLatency, inputLatency) +
                m_recordingStartGapEstimate;
            cerr << "MainWindow::recordingStarted: output latency=" << outputLatency
                 << " input latency=" << inputLatency
                 << " estimated start gap=" << m_recordingStartGapEstimate
                 << " total compensation=" << m_recordingLatencyFrames << " frames" << endl;

            m_recordingStartGapMeasured = -1;
            m_awaitingReferenceStart = true;

            m_viewManager->setPlaybackFrame(0);
            m_playSource->play(0);
        }

        updateLayerStatuses();
        updateMenuStates();
    });
}

void
MainWindow::refineRecordingLatency()
{
    sv_frame_t measured = m_recordingStartGapMeasured;
    if (measured < 0 || measured == m_recordingStartGapEstimate) return;
    cerr << "MainWindow::refineRecordingLatency: start gap was " << measured
         << " frames, not the estimated " << m_recordingStartGapEstimate << endl;
    m_recordingLatencyFrames += measured - m_recordingStartGapEstimate;
    m_recordingStartGapEstimate = measured;
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

    // Draw the dot where the finished pitch track will put this sound:
    // the take is going to be shifted earlier by the recording latency.
    // The first dots may have been placed using the estimate of the
    // start gap. They all belong to sound from before the reference
    // started, which has no place on the reference's timeline.
    sv_frame_t latencyBefore = m_recordingLatencyFrames;
    refineRecordingLatency();
    auto m = ModelById::getAs<SparseTimeValueModel>(m_realtimePitchModelId);
    if (m && m_recordingLatencyFrames != latencyBefore) {
        for (const Event &e : m->getAllEvents()) m->remove(e);
    }

    sv_frame_t dotFrame = compensatedLiveFrame(frame, m_recordingLatencyFrames);
    if (dotFrame < 0) return;

    if (m) {
        m->add(Event(dotFrame, float(hz), tr("")));
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
    m_recordingInProgress = false;
    m_recordingAsSingingTrack = false;
    m_currentRecordingModelId = {};
    restoreSingingAudioAfterTake();

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

    updateLayerStatuses();
    updateMenuStates();
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
        
        // This name doesn't have to be unguessable
#ifndef _WIN32
        QString fname = QString("tmp-%1-%2.sv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz"))
            .arg(QProcess().processId());
#else
        QString fname = QString("tmp-%1.sv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz"));
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

    if (!saveSessionFile(path)) {
        QMessageBox::critical(this, tr("Failed to save file"),
                              tr("Session file \"%1\" could not be saved.").arg(path));
    } else {
        setWindowTitle(tr("%1: %2")
                       .arg(QApplication::applicationName())
                       .arg(QFileInfo(path).fileName()));
        m_sessionFile = path;
        CommandHistory::getInstance()->documentSaved();
        documentRestored();
        m_recentFiles.addFile(path);
    }
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

    if (!saveSessionFile(path)) {
        QMessageBox::critical(this, tr("Failed to save file"),
                              tr("Session file \"%1\" could not be saved.").arg(path));
    } else {
        setWindowTitle(tr("%1: %2")
                       .arg(QApplication::applicationName())
                       .arg(QFileInfo(path).fileName()));
        m_sessionFile = path;
        CommandHistory::getInstance()->documentSaved();
        documentRestored();
        m_recentFiles.addFile(path);
    }
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
    CommandHistory::getInstance()->startCompoundOperation(tr("Toggle Pitch Candidates"), true);

    m_analyser->showPitchCandidates(!m_analyser->arePitchCandidatesShown());

    CommandHistory::getInstance()->endCompoundOperation();

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

                if (m_recordingAsSingingTrack) {
                    // The model is a WritableWaveFileModel still being
                    // recorded into.  Set up m_analyser2 with waveform/
                    // visualisation layers but defer pYIN until recording
                    // finishes (analyseNow() will call analyseExistingFile()).
                    // Also store the model ID so setupRealtimePitchLayer()
                    // can target this exact model rather than scanning all
                    // document models (which would wrongly pick up a previous
                    // recording's WritableWaveFileModel that is still
                    // registered because its orphan waveform layer prevents
                    // releaseModel() from freeing it).
                    m_currentRecordingModelId = model;
                    QTimer::singleShot(0, this, [this, model]() {
                        m_pendingSingingModelId = {};
                        setupSingingTrackAnalyser(model, /*deferAnalysis=*/true);
                    });
                } else {
                    // Normal case: a finished audio file was loaded as a
                    // singing track.  loadSingingTrack() runs the analysis
                    // itself as soon as openPath() returns (it must happen
                    // before the extra pane is pruned); this deferred call
                    // is the fallback for any other route that adds a model.
                    QTimer::singleShot(0, this, SLOT(analyseNewSingingModel()));
                }
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

    // When the user recorded a singing track alongside an existing reference
    // track (RecordCreateAdditionalModel mode), the recording becomes an
    // additional model, not the main model.  In that case we must route
    // analysis through m_analyser2 (which was set up by setupSingingTrackAnalyser
    // via modelAdded() when the WritableWaveFileModel was registered).
    // We must NOT re-analyse the primary reference track here.
    if (m_recordingAsSingingTrack) {
        cerr << "analyseNow: recording was singing track — routing to m_analyser2" << endl;

        // Apply round-trip latency compensation: shift the singing model's
        // global start frame backward by the round-trip hardware latency so
        // the singer's audio (which arrives late due to output + input latency)
        // aligns with the reference during playback.  This must happen before
        // pYIN analysis so that all derived layers (pitch, notes) inherit the
        // same timeline offset.  Only applied when reference playback was
        // active during the recording (m_recordingLatencyFrames > 0).
        refineRecordingLatency();
        if (m_recordingLatencyFrames > 0 && !m_currentRecordingModelId.isNone()) {
            auto wfm = ModelById::getAs<WritableWaveFileModel>(m_currentRecordingModelId);
            if (wfm) {
                cerr << "analyseNow: applying latency compensation: setStartFrame("
                     << -m_recordingLatencyFrames << ")" << endl;
                wfm->setStartFrame(-m_recordingLatencyFrames);
            }
        }

        // The realtime pitch layer stays until the full pYIN analysis of
        // the singing recording (via m_analyser2) is there to replace it,
        // or goes at once if that analysis could not be started.
        bool wasLive = (m_realtimePitchTracker || m_realtimePitchLayer);

        auto analyseSingingTrack = [this]() -> bool {
            CommandHistory::getInstance()->startCompoundOperation
                (tr("Analyse Singing Track"), true);

            QString error = m_analyser2->analyseExistingFile();

            CommandHistory::getInstance()->endCompoundOperation();

            if (error != "") {
                QMessageBox::warning
                    (this,
                     tr("Failed to analyse singing track"),
                     tr("<b>Analysis failed</b><p>%1</p>").arg(error),
                     QMessageBox::Ok);
                return false;
            }
            return true;
        };

        if (m_analyser2) {
            bool ok = analyseSingingTrack();
            if (wasLive) recordingFinishedFull(ok ? m_analyser2 : nullptr);
        } else {
            // m_analyser2 may still be pending setup (modelAdded fires async).
            // Defer the analysis until the secondary analyser is ready.
            cerr << "analyseNow: m_analyser2 not ready yet, deferring singing-track analysis" << endl;
            if (wasLive) {
                // The take is over either way; the dots wait for the
                // deferred analysis
                stopRealtimePitchTracker();
                m_recordingInProgress = false;
            }
            QTimer::singleShot(200, this, [this, wasLive, analyseSingingTrack]() {
                bool ok = false;
                if (m_analyser2) {
                    ok = analyseSingingTrack();
                } else {
                    cerr << "analyseNow (deferred): m_analyser2 still null, singing-track analysis skipped" << endl;
                }
                if (wasLive) recordingFinishedFull(ok ? m_analyser2 : nullptr);
            });
        }
        return;
    }

    if (!m_analyser) return;

    CommandHistory::getInstance()->startCompoundOperation
        (tr("Analyse Audio"), true);

    QString error = m_analyser->analyseExistingFile();

    CommandHistory::getInstance()->endCompoundOperation();

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
        m_document->addLayerToView
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

    if (!m_withSpectrogram) {
        m_analyser->setVisible(Analyser::Spectrogram, false);
    }

    if (!m_withSonification) {
        m_analyser->setAudible(Analyser::PitchTrack, false);
        m_analyser->setAudible(Analyser::Notes, false);
    }

    // Session restore: if the loaded session contained a second audio model
    // (i.e. a previously loaded singing track), set up the secondary analyser
    // for it now.  We scan all document models for a WaveFileModel that is
    // not the main model and not already being tracked as a singing model.
    // We only do this if we don't already have a secondary analyser (it may
    // have been set up already e.g. via modelAdded() during session load).
    // Skip the scan while a singing model is pending: openAudio() emits
    // audioFileLoaded() for CreateAdditionalModel too, and loadSingingTrack()
    // is about to set up that model itself.  A second, queued setup would
    // tear down m_analyser2's layers — the only references to the singing
    // model — releasing it before the re-setup.
    if (!m_analyser2 && m_document && m_pendingSingingModelId.isNone()) {
        ModelId mainId = getMainModelId();
        ModelId foundSinging;
        for (ModelId mid : m_document->getModels()) {
            if (mid == mainId) continue;
            if (mid == m_realtimePitchModelId) continue;
            if (mid == m_backgroundMusicModelId) continue;
            if (ModelById::isa<WaveFileModel>(mid)) {
                foundSinging = mid;
                break;
            }
        }
        if (!foundSinging.isNone()) {
            cerr << "analyseNewMainModel: found existing singing track model "
                 << foundSinging << " in session, setting up secondary analyser" << endl;
            // Defer so that the primary analyser's layers are fully in place
            // before the secondary analyser tries to share the same pane.
            QTimer::singleShot(0, this, [this, foundSinging]() {
                setupSingingTrackAnalyser(foundSinging);
            });
        }
    }

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
