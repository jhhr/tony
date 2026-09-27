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

#ifndef TEST_COMPACT_LAYOUT_H
#define TEST_COMPACT_LAYOUT_H

// Tier 5: the compact layout (CompactLayout) of the real MainWindow,
// switched on and off as the View menu does, on a window on show at
// about a phone's size in landscape.  Whether a widget is visible is
// only known on a window on show.

#include "TestSignals.h"

#include "../MainWindow.h"
#include "../Analyser.h"
#include "../CompactLayout.h"
#include "../LyricsTrack.h"
#include "../SongScroll.h"
#include "../SongScrollBar.h"

#include "version.h"

#include "layer/Layer.h"
#include "view/Overview.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/ViewManager.h"
#include "data/fileio/WavFileWriter.h"
#include "transform/ModelTransformerFactory.h"

#include <QObject>
#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QGuiApplication>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointingDevice>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include <cmath>
#include <cstdlib>
#include <vector>

/**
 * MainWindow without an audio device, with what the tests look at
 */
class CompactTestWindow : public MainWindow
{
public:
    CompactTestWindow() : MainWindow(AUDIO_PLAYBACK_AND_RECORD, true, false) { }

    CompactLayout *compact() { return m_compactLayout; }
    QComboBox *takeBox() { return m_takeCombo; }
    QWidget *overview() { return m_overview; }
    SongScrollBar *songScroll() { return m_songScroll; }
    sv::PaneStack *paneStack() { return m_paneStack; }
    QAction *zoomInAction() { return m_zoomInAction; }
    SingingTakes *takes() { return m_takes; }
    sv::ViewManager *viewManager() { return m_viewManager; }
    LyricsTrack *lyrics() { return m_lyrics; }
    Analyser *analyser() { return m_analyser; }

    // What the compact toolbar should hold, in order, but for the take
    // box, the menu button and the panel button, and Undo and Redo
    QList<QAction *> ownActions() {
        return { m_playAction, m_recordAction, m_recordIntoSelection,
                 m_eraseSingingAction, m_zoomInAction, m_zoomOutAction };
    }

    // What it hides: the note-editing tools and the audio device menus
    QList<QAction *> hiddenActions() {
        return { m_navigateToolAction, m_noteEditToolAction,
                 m_audioDeviceMenu->menuAction(),
                 m_audioInputDeviceMenu->menuAction() };
    }

    QAction *navigateTool() { return m_navigateToolAction; }
    QAction *noteEditTool() { return m_noteEditToolAction; }
    QAction *eraseAction() { return m_eraseSingingAction; }

    void discardModifications() { m_documentModified = false; }
    void doCloseSession() { discardModifications(); closeSession(); }
    void doNewEmptyTake() { newEmptyTake(); }

protected:
    void createAudioIO() override { }
};

class TestCompactLayout : public QObject
{
    Q_OBJECT

    static constexpr double rate = 44100.0;

    QTemporaryDir m_dir;
    CompactTestWindow *m_window = nullptr;
    QTimer m_watchdog;
    QStringList m_dialogs;

    // Everything switching off must put back as it was
    struct Layout {
        QByteArray state; // QMainWindow::saveState()
        QSize iconSize;
        bool menuBarShown = false;
        bool overviewShown = false;
        bool songScrollShown = false;
        // Of the toolbars in the window's layout, by name
        QMap<QString, bool> toolBarsShown;
        QMap<QString, QSize> toolBarIconSizes;
        QMap<QString, QList<QAction *>> toolBarActions;
        // Of the actions compact mode hides
        QList<bool> actionsVisible;
        // Of the menus of the menu bar and their submenus, in order
        QList<bool> tearOffs;
        QWidget *takeBoxParent = nullptr;
        bool takeBoxShown = false;
    };

    static void addTearOffs(QMenu *menu, QList<bool> &tearOffs) {
        tearOffs.push_back(menu->isTearOffEnabled());
        for (QAction *action: menu->actions()) {
            if (QMenu *submenu = action->menu()) addTearOffs(submenu, tearOffs);
        }
    }

    QList<QToolBar *> layoutToolBars() {
        QList<QToolBar *> toolBars;
        for (QToolBar *toolBar: m_window->findChildren<QToolBar *>
                 (QString(), Qt::FindDirectChildrenOnly)) {
            if (m_window->toolBarArea(toolBar) != Qt::NoToolBarArea) {
                toolBars.push_back(toolBar);
            }
        }
        return toolBars;
    }

    QToolBar *toolBar(QString name) {
        return m_window->findChild<QToolBar *>
            (name, Qt::FindDirectChildrenOnly);
    }

    Layout layout() {
        Layout layout;
        layout.state = m_window->saveState();
        layout.iconSize = m_window->iconSize();
        layout.menuBarShown = m_window->menuBar()->isVisible();
        layout.overviewShown = m_window->overview()->isVisible();
        layout.songScrollShown = m_window->songScroll()->isVisible();
        for (QToolBar *toolBar: layoutToolBars()) {
            QString name = toolBar->objectName();
            layout.toolBarsShown[name] = toolBar->isVisible();
            layout.toolBarIconSizes[name] = toolBar->iconSize();
            layout.toolBarActions[name] = toolBar->actions();
        }
        for (QAction *action: m_window->hiddenActions()) {
            layout.actionsVisible.push_back(action->isVisible());
        }
        for (QAction *action: m_window->menuBar()->actions()) {
            if (QMenu *menu = action->menu()) addTearOffs(menu, layout.tearOffs);
        }
        layout.takeBoxParent = m_window->takeBox()->parentWidget();
        layout.takeBoxShown = m_window->takeBox()->isVisible();
        return layout;
    }

    void verifySameLayout(const Layout &now, const Layout &before) {
        QCOMPARE(now.menuBarShown, before.menuBarShown);
        QCOMPARE(now.overviewShown, before.overviewShown);
        QCOMPARE(now.songScrollShown, before.songScrollShown);
        QCOMPARE(now.toolBarsShown, before.toolBarsShown);
        QCOMPARE(now.toolBarIconSizes, before.toolBarIconSizes);
        QCOMPARE(now.iconSize, before.iconSize);
        QVERIFY2(now.toolBarActions == before.toolBarActions,
                 "the toolbars do not hold the actions they held");
        QCOMPARE(now.actionsVisible, before.actionsVisible);
        QCOMPARE(now.tearOffs, before.tearOffs);
        QCOMPARE(now.takeBoxParent, before.takeBoxParent);
        QCOMPARE(now.takeBoxShown, before.takeBoxShown);
        QCOMPARE(now.state, before.state);
    }

    // Let the window lay itself out again
    void settle() {
        QCoreApplication::sendPostedEvents();
        QTest::qWait(20);
    }

    void switchCompact() {
        m_window->compact()->getAction()->trigger();
        settle();
    }

    // The compact toolbar's action that holds the take box, if it has it
    QWidgetAction *takeBoxAction(QToolBar *toolBar) {
        for (QAction *action: toolBar->actions()) {
            auto widgetAction = qobject_cast<QWidgetAction *>(action);
            if (widgetAction &&
                widgetAction->defaultWidget() == m_window->takeBox()) {
                return widgetAction;
            }
        }
        return nullptr;
    }

    static QString names(const QList<QAction *> &actions) {
        QStringList names;
        for (QAction *action: actions) {
            if (qobject_cast<QWidgetAction *>(action)) names << "(widget)";
            else names << action->iconText();
        }
        return names.join(", ");
    }

    bool analysed() {
        Analyser *a = m_window->analyser();
        return a && a->getLayer(Analyser::PitchTrack) &&
            a->getLayer(Analyser::Notes) &&
            a->getInitialAnalysisCompletion() >= 100 &&
            !sv::ModelTransformerFactory::getInstance()
            ->haveRunningTransformers();
    }

    // At about a phone's size in landscape, once Qt has scaled for the
    // screen's density
    void openWindow() {
        m_window = new CompactTestWindow;
        m_window->resize(900, 400);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        settle();
    }

    // Two seconds of reference, analysed: takes need a main model
    void openReference() {
        QString path = m_dir.filePath("reference.wav");
        if (!QFile::exists(path)) {
            std::vector<float> data = TestSignals::sine
                (220.5, rate, int(2 * rate), 0.5);
            sv::WavFileWriter writer(path, rate, 1,
                                     sv::WavFileWriter::WriteToTarget);
            const float *ptr = data.data();
            QVERIFY(writer.isOK());
            QVERIFY(writer.writeSamples(&ptr, sv::sv_frame_t(data.size())));
            QVERIFY(writer.close());
        }
        m_window->discardModifications();
        QCOMPARE(m_window->openPath(path, MainWindow::ReplaceSession),
                 MainWindow::FileOpenSucceeded);
        QTRY_VERIFY_WITH_TIMEOUT(analysed(), 30000);
    }

    SongScrollBar *songScroll() { return m_window->songScroll(); }
    sv::Pane *pane() { return m_window->paneStack()->getPane(0); }

    // The panes at level frames per pixel about centre
    void setView(int level, sv::sv_frame_t centre) {
        pane()->setZoomLevel
            (sv::ZoomLevel(sv::ZoomLevel::FramesPerPixel, level));
        pane()->setCentreFrame(centre);
        settle();
    }

    // Whether the strip has painted the thumb for what the panes show
    bool paintedAsNow() {
        SongScroll::Thumb painted = songScroll()->getPaintedThumb();
        SongScroll::Thumb now = songScroll()->currentThumb();
        return now.width() > 0.0 &&
            std::fabs(painted.x0 - now.x0) < 1.0 &&
            std::fabs(painted.x1 - now.x1) < 1.0;
    }

    static QString text(const SongScroll::Thumb &t) {
        return QString("[%1, %2]").arg(t.x0).arg(t.x1);
    }

    // The frames a pixel of the strip stands for
    double stripFramesPerPixel() {
        return double(songScroll()->getSongFrames()) / songScroll()->width();
    }

    int voicedColumns() {
        int n = 0;
        for (const auto &c : songScroll()->getContour()) {
            if (!c.isEmpty()) ++n;
        }
        return n;
    }

    // The reference open and analysed, and the window compact
    void openCompactWithReference() {
        openWindow();
        if (QTest::currentTestFailed()) return;
        openReference();
        if (QTest::currentTestFailed()) return;
        switchCompact();
        QVERIFY(songScroll()->isVisible());
        QVERIFY(pane());
        QVERIFY(songScroll()->getSongFrames() > 0);

        // The last of the analysis drawn: nothing else is to repaint the
        // strip while a test looks at what does
        QTRY_VERIFY(!songScroll()->isContourPending());
    }

    void dismissDialog() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) return;
        QString description = modal->windowTitle();
        if (auto box = qobject_cast<QMessageBox *>(modal)) {
            description += ": " + box->text();
            m_dialogs.push_back(description);
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

        // Otherwise the MainWindow constructor asks, in a dialog
        QSettings settings;
        settings.beginGroup("Preferences");
        settings.setValue(QString("network-permission-%1").arg(TONY_VERSION),
                          false);
        settings.endGroup();

        connect(&m_watchdog, &QTimer::timeout,
                this, [this]() { dismissDialog(); });
        m_watchdog.start(50);
    }

    void init() {
        m_dialogs.clear();
    }

    void cleanup() {
        if (m_window) {
            // A test that failed with Qt's button pressed must not leave
            // it so for the next
            if (QGuiApplication::mouseButtons() != Qt::NoButton) {
                QTest::mouseRelease(m_window->windowHandle(), Qt::LeftButton);
            }
            QTRY_VERIFY_WITH_TIMEOUT
                (!sv::ModelTransformerFactory::getInstance()
                 ->haveRunningTransformers(), 30000);
            m_window->doCloseSession();
            delete m_window;
            m_window = nullptr;
        }
        QSettings().remove("MainWindow/plotsize");
        QVERIFY2(m_dialogs.isEmpty(),
                 qPrintable("unexpected dialog: " + m_dialogs.join(" | ")));
    }

    void cleanupTestCase() {
        m_watchdog.stop();
    }

    // From the View menu: the menu bar, every toolbar and the overview
    // go, and one toolbar of large buttons comes, holding the window's
    // own actions and its take box
    void switching_on_leaves_one_toolbar() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        CompactLayout *compact = m_window->compact();
        QVERIFY(!compact->isOn());
        QVERIFY2(!compact->getToolBar(), "made before it was wanted");

        QMenu *viewMenu = nullptr;
        for (QAction *action: m_window->menuBar()->actions()) {
            if (action->menu() &&
                action->menu()->actions().contains(compact->getAction())) {
                viewMenu = action->menu();
            }
        }
        QVERIFY(viewMenu);
        QCOMPARE(viewMenu->title(), QString("&View"));

        // The note-editing tool, which compact mode hides, selected
        m_window->noteEditTool()->trigger();
        QCOMPARE(m_window->viewManager()->getToolMode(),
                 sv::ViewManager::NoteEditMode);

        switchCompact();
        QVERIFY(compact->isOn());
        QVERIFY(compact->getAction()->isChecked());

        QToolBar *bar = compact->getToolBar();
        QVERIFY(bar);
        QVERIFY(bar->isVisible());
        QCOMPARE(m_window->toolBarArea(bar), Qt::TopToolBarArea);

        QVERIFY(!m_window->menuBar()->isVisible());
        QVERIFY(!m_window->overview()->isVisible());
        QVERIFY(m_window->songScroll()->isVisible());
        QCOMPARE(int(layoutToolBars().size()), 7); // the window's six and this
        for (QToolBar *toolBar: layoutToolBars()) {
            if (toolBar == bar) continue;
            QVERIFY2(!toolBar->isVisible(), qPrintable(toolBar->objectName()));
        }

        // Menu, Play, Record, Record into Selection, the take box, Undo,
        // Redo, Erase, Zoom In, Zoom Out, Show and Play.  Undo and Redo are
        // the ones of the window's own toolbar, with their menus
        QToolBar *tools = toolBar("Tools Toolbar");
        QVERIFY(tools);
        QVERIFY(compact->getMenuButton());
        QList<QAction *> own = m_window->ownActions();
        QList<QAction *> expected {
            compact->getMenuButton()->defaultAction(),
            own[0], own[1], own[2],
            takeBoxAction(bar),
            tools->actions()[0], tools->actions()[1],
            own[3], own[4], own[5],
            compact->getPanelAction()
        };
        QVERIFY(!expected.contains(nullptr));
        QList<QAction *> actions;
        for (QAction *action: bar->actions()) {
            if (!action->isSeparator()) actions.push_back(action);
        }
        QVERIFY2(actions == expected,
                 qPrintable(QString("the toolbar holds %1, not %2")
                            .arg(names(actions)).arg(names(expected))));
        QVERIFY(tools->actions()[0]->menu());
        QCOMPARE(m_window->eraseAction()->iconText(), QString("Erase"));

        // Every button on show, large; the take box the window's own, on
        // show and working
        QCOMPARE(bar->iconSize(), QSize(CompactLayout::iconSize,
                                        CompactLayout::iconSize));
        for (QAction *action: actions) {
            QWidget *widget = bar->widgetForAction(action);
            QVERIFY2(widget && widget->isVisible(),
                     qPrintable(action->iconText()));
            // and as tall as the icons, those that show text too
            if (auto button = qobject_cast<QToolButton *>(widget)) {
                QCOMPARE(button->iconSize(), bar->iconSize());
                QVERIFY2(button->height() >= CompactLayout::iconSize,
                         qPrintable(QString("%1 is %2 high")
                                    .arg(action->iconText())
                                    .arg(button->height())));
            }
        }
        QCOMPARE(bar->widgetForAction(takeBoxAction(bar)),
                 static_cast<QWidget *>(m_window->takeBox()));
        QCOMPARE(m_window->takeBox()->parentWidget(),
                 static_cast<QWidget *>(bar));

        // The note-editing tools and the audio device menus hidden, and
        // the tool the one that edits nothing
        for (QAction *action: m_window->hiddenActions()) {
            QVERIFY2(!action->isVisible(), qPrintable(action->text()));
        }
        QVERIFY(m_window->navigateTool()->isChecked());
        QCOMPARE(m_window->viewManager()->getToolMode(),
                 sv::ViewManager::NavigateMode);
    }

    // Every menu of the menu bar is in the menu button's popup, which a
    // tap opens; the switch back is in there, and nothing can be torn off
    void menu_button_holds_every_menu() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        CompactLayout *compact = m_window->compact();
        switchCompact();

        QToolButton *button = compact->getMenuButton();
        QVERIFY(button);
        QVERIFY(button->isVisible());
        QCOMPARE(button->popupMode(), QToolButton::InstantPopup);
        QMenu *popup = button->defaultAction()->menu();
        QVERIFY(popup);

        QList<QAction *> menus = m_window->menuBar()->actions();
        QCOMPARE(int(menus.size()), 7); // File, Edit, View, Analysis,
                                        // Takes, Playback, Help
        QVERIFY2(popup->actions() == menus,
                 qPrintable(QString("the popup holds %1, the menu bar %2")
                            .arg(names(popup->actions())).arg(names(menus))));
        bool haveSwitch = false;
        for (QAction *action: menus) {
            QVERIFY(action->menu());
            QVERIFY(action->isVisible());
            QVERIFY2(!action->menu()->isTearOffEnabled(),
                     qPrintable(action->text()));
            if (action->menu()->actions().contains(compact->getAction())) {
                haveSwitch = true;
            }
        }
        QVERIFY(haveSwitch);

        // Playback > Calibrate Audio among them, on show: the device menus
        // are hidden, not the check.  Nor Voice Threshold, the music being
        // as likely on a phone's speaker as on any
        QAction *calibrate = nullptr;
        QAction *voiceThreshold = nullptr;
        for (QAction *action: menus) {
            for (QAction *item: action->menu()->actions()) {
                if (item->text() == "&Calibrate Audio...") calibrate = item;
                if (item->text() == "Voice &Threshold") voiceThreshold = item;
            }
        }
        QVERIFY(calibrate);
        QVERIFY(calibrate->isVisible());
        QVERIFY(voiceThreshold);
        QVERIFY(voiceThreshold->menu());
        QVERIFY(voiceThreshold->isVisible());

        // A tap opens it (and a timer closes it again)
        bool checked = false, shown = false;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, this, [&]() {
            shown = popup->isVisible();
            popup->close();
            checked = true;
        });
        timer.start(200);
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(checked, 5000);
        QVERIFY(shown);
    }

    // The shortcuts of the menus work with the menu bar hidden: Select All
    // is in the Edit menu and in no toolbar
    void menu_shortcuts_work_while_compact() {
        openWindow();
        if (QTest::currentTestFailed()) return;
        openReference();
        if (QTest::currentTestFailed()) return;

        switchCompact();
        QVERIFY(!m_window->menuBar()->isVisible());

        m_window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_window));

        QVERIFY(m_window->viewManager()->getSelections().empty());
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(int(m_window->viewManager()->getSelections().size()), 1);
    }

    // The Show and Play button brings the bottom toolbars, the toggles
    // and gains and the playback speed, and takes them away again
    void show_and_play_button_shows_and_hides_the_panel() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        CompactLayout *compact = m_window->compact();
        switchCompact();

        QAction *panel = compact->getPanelAction();
        QVERIFY(panel);
        QVERIFY(panel->isCheckable());
        QVERIFY(!panel->isChecked());
        auto button = qobject_cast<QToolButton *>
            (compact->getToolBar()->widgetForAction(panel));
        QVERIFY(button);
        QVERIFY(button->isVisible());

        QStringList panelNames { "Playback Controls", "Show and Play" };
        QStringList otherNames { "File Toolbar", "Tools Toolbar",
                                 "Playback Toolbar", "Play Mode Toolbar" };
        for (QString name: panelNames + otherNames) {
            QVERIFY2(toolBar(name), qPrintable(name));
            QVERIFY2(!toolBar(name)->isVisible(), qPrintable(name));
        }

        QTest::mouseClick(button, Qt::LeftButton);
        settle();
        QVERIFY(panel->isChecked());
        for (QString name: panelNames) {
            QVERIFY2(toolBar(name)->isVisible(), qPrintable(name));
        }
        // and nothing else with it
        for (QString name: otherNames) {
            QVERIFY2(!toolBar(name)->isVisible(), qPrintable(name));
        }
        QVERIFY(!m_window->menuBar()->isVisible());
        QVERIFY(compact->getToolBar()->isVisible());

        QTest::mouseClick(button, Qt::LeftButton);
        settle();
        QVERIFY(!panel->isChecked());
        for (QString name: panelNames) {
            QVERIFY2(!toolBar(name)->isVisible(), qPrintable(name));
        }
    }

    // Switching off puts back what was there: here a layout of the user's
    // own, with one bottom toolbar hidden, which the panel showed while
    // compact
    void switching_off_restores_the_layout() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        toolBar("Playback Controls")->hide();
        settle();
        Layout before = layout();
        QVERIFY(before.menuBarShown);
        QVERIFY(before.overviewShown);
        QVERIFY(!before.songScrollShown);
        QVERIFY(!before.toolBarsShown["Playback Controls"]);
        QVERIFY(before.toolBarsShown["Show and Play"]);
        QCOMPARE(int(before.toolBarsShown.size()), 6);

        CompactLayout *compact = m_window->compact();
        switchCompact();
        compact->getPanelAction()->trigger();
        settle();
        QVERIFY(toolBar("Playback Controls")->isVisible());

        switchCompact();
        QVERIFY(!compact->isOn());
        QVERIFY(!compact->getAction()->isChecked());
        QCOMPARE(m_window->toolBarArea(compact->getToolBar()),
                 Qt::NoToolBarArea);
        verifySameLayout(layout(), before);
    }

    // On, off, on and off: the same toolbar and popup the second time,
    // nothing added twice, and the layout of the start at the end
    void switching_twice_leaves_nothing_duplicated() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        Layout before = layout();
        int toolBars = int(m_window->findChildren<QToolBar *>().size());
        int menus = int(m_window->findChildren<QMenu *>().size());
        int boxes = int(m_window->findChildren<QComboBox *>().size());

        CompactLayout *compact = m_window->compact();
        switchCompact();
        QToolBar *bar = compact->getToolBar();
        QList<QAction *> actions = bar->actions();
        QList<QAction *> popup = compact->getMenuButton()->defaultAction()
            ->menu()->actions();
        int buttons = int(bar->findChildren<QToolButton *>().size());

        switchCompact();
        switchCompact();
        QCOMPARE(compact->getToolBar(), bar);
        QVERIFY2(bar->actions() == actions,
                 qPrintable(QString("the toolbar holds %1, not %2")
                            .arg(names(bar->actions())).arg(names(actions))));
        QVERIFY(compact->getMenuButton()->defaultAction()->menu()->actions()
                == popup);
        QCOMPARE(int(bar->findChildren<QToolButton *>().size()), buttons);

        switchCompact();
        verifySameLayout(layout(), before);
        if (QTest::currentTestFailed()) return;

        // The compact toolbar and its popup are kept for the next time,
        // and that is all there is more of
        QCOMPARE(int(m_window->findChildren<QToolBar *>().size()),
                 toolBars + 1);
        QCOMPARE(int(m_window->findChildren<QMenu *>().size()), menus + 1);
        QCOMPARE(int(m_window->findChildren<QComboBox *>().size()), boxes);
    }

    // The take box in the compact toolbar is the window's own: choosing a
    // take there switches to it, as it does in the desktop layout
    void take_box_chooses_the_take_while_compact() {
        openWindow();
        if (QTest::currentTestFailed()) return;
        openReference();
        if (QTest::currentTestFailed()) return;

        m_window->doNewEmptyTake();
        m_window->doNewEmptyTake();
        QCOMPARE(m_window->takes()->getTakeNames(),
                 QStringList({ "Take 1", "Take 2" }));
        QCOMPARE(m_window->takes()->getActiveIndex(), 1);
        auto home = qobject_cast<QToolBar *>
            (m_window->takeBox()->parentWidget());
        QVERIFY(home);

        switchCompact();
        QToolBar *bar = m_window->compact()->getToolBar();
        QVERIFY(takeBoxAction(bar));
        QComboBox *box = qobject_cast<QComboBox *>
            (bar->widgetForAction(takeBoxAction(bar)));
        QVERIFY(box);
        QVERIFY(box->isVisible());
        QVERIFY(box->isEnabled());
        QCOMPARE(box->count(), 2);
        QCOMPARE(box->currentText(), QString("Take 2"));

        // As from a keyboard: the take above.  Page Up, as Up is Zoom In's
        QTest::keyClick(box, Qt::Key_PageUp);
        QCOMPARE(m_window->takes()->getActiveIndex(), 0);
        QCOMPARE(box->currentText(), QString("Take 1"));

        // and back in its own toolbar the box says so
        switchCompact();
        QCOMPARE(box->parentWidget(), static_cast<QWidget *>(home));
        QVERIFY(box->isVisible());
        QCOMPARE(box->currentText(), QString("Take 1"));
    }

    // main() switches on at start with --compact, and always on Android:
    // before the window is first shown
    void compact_at_start() {
#ifdef Q_OS_ANDROID
        QVERIFY(CompactLayout::isWantedAtStart({ "Tony" }));
#else
        QVERIFY(CompactLayout::isWantedAtStart({ "Tony", "--compact" }));
        QVERIFY(CompactLayout::isWantedAtStart
                ({ "Tony", "--no-audio", "--compact", "song.wav" }));
        QVERIFY(!CompactLayout::isWantedAtStart({ "Tony" }));
        QVERIFY(!CompactLayout::isWantedAtStart
                ({ "Tony", "--no-audio", "song.wav" }));
#endif

        m_window = new CompactTestWindow;
        m_window->resize(900, 400);
        m_window->setCompactLayout(true);
        QVERIFY(m_window->compact()->isOn());
        QVERIFY(m_window->compact()->getAction()->isChecked());

        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        settle();

        QToolBar *bar = m_window->compact()->getToolBar();
        QVERIFY(bar->isVisible());
        QVERIFY(m_window->takeBox()->isVisible());
        QVERIFY(!m_window->menuBar()->isVisible());
        QVERIFY(!m_window->overview()->isVisible());
        QVERIFY(m_window->songScroll()->isVisible());
        for (QToolBar *toolBar: layoutToolBars()) {
            if (toolBar == bar) continue;
            QVERIFY2(!toolBar->isVisible(), qPrintable(toolBar->objectName()));
        }

        // Off: as a window that was never compact has it
        m_window->setCompactLayout(false);
        settle();
        QVERIFY(!m_window->compact()->getAction()->isChecked());
        QVERIFY(m_window->menuBar()->isVisible());
        QVERIFY(m_window->overview()->isVisible());
        QVERIFY(!m_window->songScroll()->isVisible());
        QCOMPARE(int(layoutToolBars().size()), 6);
        for (QToolBar *toolBar: layoutToolBars()) {
            QVERIFY2(toolBar->isVisible(), qPrintable(toolBar->objectName()));
        }
        QVERIFY(m_window->takeBox()->isVisible());
    }

    // View > Plot Size (PlotSize): the steps with the default checked, a
    // step applied to the panes at once and taken up at the next start.
    // On a phone it is in the menu button's popup with the rest of the
    // View menu
    void plot_size_in_the_view_menu() {
        QSettings().remove("MainWindow/plotsize");
        openWindow();
        if (QTest::currentTestFailed()) return;

        QMenu *plotSize = nullptr;
        for (QAction *menu: m_window->menuBar()->actions()) {
            if (!menu->menu() || !menu->menu()->actions()
                .contains(m_window->compact()->getAction())) continue;
            for (QAction *action: menu->menu()->actions()) {
                if (action->menu() && action->text() == "Plot &Size") {
                    plotSize = action->menu();
                }
            }
        }
        QVERIFY(plotSize);

        QStringList texts, checked;
        for (QAction *action: plotSize->actions()) {
            texts << action->text();
            if (action->isChecked()) checked << action->text();
        }
        QCOMPARE(texts, QStringList({ "100%", "150%", "200%" }));
        QCOMPARE(checked, QStringList({ "100%" }));
        QCOMPARE(m_window->viewManager()->getPlotScale(), 1.0);

        plotSize->actions().at(2)->trigger();
        QCOMPARE(m_window->viewManager()->getPlotScale(), 2.0);

        m_window->doCloseSession();
        delete m_window;
        m_window = nullptr;
        openWindow();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->viewManager()->getPlotScale(), 2.0);
    }

    // View > Lyrics Size (LyricsSize): the same, for the scale the
    // lyrics track gives its layer
    void lyrics_size_in_the_view_menu() {
        QSettings().remove("MainWindow/lyricssize");
        openWindow();
        if (QTest::currentTestFailed()) return;

        QMenu *lyricsSize = nullptr;
        for (QAction *menu: m_window->menuBar()->actions()) {
            if (!menu->menu() || !menu->menu()->actions()
                .contains(m_window->compact()->getAction())) continue;
            for (QAction *action: menu->menu()->actions()) {
                if (action->menu() && action->text() == "Lyrics Si&ze") {
                    lyricsSize = action->menu();
                }
            }
        }
        QVERIFY(lyricsSize);

        QStringList texts, checked;
        for (QAction *action: lyricsSize->actions()) {
            texts << action->text();
            if (action->isChecked()) checked << action->text();
        }
        QCOMPARE(texts, QStringList({ "35%", "50%", "65%", "80%", "100%" }));
        QCOMPARE(checked, QStringList({ "100%" })); // 50% on Android
        QCOMPARE(m_window->lyrics()->getTextScale(), 1.0);

        lyricsSize->actions().at(1)->trigger();
        QCOMPARE(m_window->lyrics()->getTextScale(), 0.5);

        m_window->doCloseSession();
        delete m_window;
        m_window = nullptr;
        openWindow();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(m_window->lyrics()->getTextScale(), 0.5);
        QSettings().remove("MainWindow/lyricssize");
    }

    // The song scroll bar (SongScrollBar) takes the overview's place
    // while compact, a thin strip above the panes, and goes with it
    void song_scroll_bar_in_place_of_the_overview() {
        openWindow();
        if (QTest::currentTestFailed()) return;

        SongScrollBar *strip = songScroll();
        QVERIFY(strip);
        QVERIFY(!strip->isVisible());
        QVERIFY(m_window->overview()->isVisible());

        switchCompact();
        QVERIFY(strip->isVisible());
        QVERIFY(!m_window->overview()->isVisible());
        QCOMPARE(strip->height(), int(SongScrollBar::stripHeight));
        QWidget *stack = m_window->paneStack();
        QVERIFY(strip->mapTo(m_window, QPoint(0, strip->height())).y() <=
                stack->mapTo(m_window, QPoint(0, 0)).y());
        QVERIFY2(strip->width() > m_window->width() * 9 / 10,
                 qPrintable(QString("%1 wide").arg(strip->width())));

        switchCompact();
        QVERIFY(!strip->isVisible());
        QVERIFY(m_window->overview()->isVisible());
    }

    // Dragging the thumb moves every pane by as much as the drag, and a
    // finger does it as the mouse does. The playhead stays
    void song_scroll_bar_drag_moves_the_panes() {
        openCompactWithReference();
        if (QTest::currentTestFailed()) return;

        SongScrollBar *strip = songScroll();
        sv::Pane *p = pane();
        setView(16, 30000);
        QTRY_VERIFY(paintedAsNow());

        SongScroll::Thumb thumb = strip->currentThumb();
        QVERIFY2(thumb.width() > 50.0 && thumb.x0 > 100.0 &&
                 thumb.x1 < strip->width() - 150.0, qPrintable(text(thumb)));
        int y = strip->height() / 2;
        // Grabbed off its middle: the panes move from where they were,
        // not to where the thumb was grabbed
        QPoint from(int(thumb.x0 + thumb.width() / 4), y);
        QPoint to = from + QPoint(100, 0);
        sv::sv_frame_t start = p->getCentreFrame();
        sv::sv_frame_t playhead =
            m_window->viewManager()->getPlaybackFrame();

        QTest::mousePress(strip, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= 10; ++i) {
            QTest::mouseMove(strip, from + (to - from) * i / 10);
        }
        QTest::mouseRelease(strip, Qt::LeftButton, Qt::NoModifier, to);
        sv::sv_frame_t byMouse = p->getCentreFrame() - start;

        // to within a pixel of the pane's, to which it rounds its centre
        double expected = 100 * stripFramesPerPixel();
        QVERIFY2(std::fabs(double(byMouse) - expected) <= 17.0,
                 qPrintable(QString("the mouse moved it by %1, not %2")
                            .arg(byMouse).arg(expected)));
        QCOMPARE(m_window->paneStack()->getPane(1)->getCentreFrame(),
                 p->getCentreFrame());
        QTRY_VERIFY(paintedAsNow());
        QVERIFY2(strip->getPaintedThumb().x0 > thumb.x0 + 90.0,
                 qPrintable(text(strip->getPaintedThumb())));
        QCOMPARE(m_window->viewManager()->getPlaybackFrame(), playhead);

        p->setCentreFrame(start);
        settle();
        QCOMPARE(p->getCentreFrame(), start);

        // Not a double click with the mouse's press
        QTest::qWait(QApplication::doubleClickInterval() + 100);

        QPointingDevice *device = QTest::createTouchDevice();
        QTest::QTouchEventWidgetSequence t =
            QTest::touchEvent(m_window, device, false);
        t.press(0, from, strip).commit();
        for (int i = 1; i <= 10; ++i) {
            t.move(0, from + (to - from) * i / 10, strip).commit();
        }
        t.release(0, to, strip).commit();
        sv::sv_frame_t byTouch = p->getCentreFrame() - start;

        QCOMPARE(byTouch, byMouse);
        QCOMPARE(QGuiApplication::mouseButtons(), Qt::NoButton);
        QCOMPARE(m_window->viewManager()->getPlaybackFrame(), playhead);
    }

    // A press beside the thumb centres the panes there, and a drag goes
    // on from there
    void song_scroll_bar_press_beside_the_thumb_centres_there() {
        openCompactWithReference();
        if (QTest::currentTestFailed()) return;

        SongScrollBar *strip = songScroll();
        sv::Pane *p = pane();
        setView(16, 20000);
        QTRY_VERIFY(paintedAsNow());

        QPoint at(strip->width() * 3 / 4, strip->height() / 2);
        QVERIFY2(!SongScroll::hitsThumb(strip->currentThumb(), at.x()),
                 qPrintable(text(strip->currentThumb())));
        sv::sv_frame_t target = SongScroll::jumpCentre
            (at.x(), strip->getSongFrames(), strip->width());
        sv::sv_frame_t playhead =
            m_window->viewManager()->getPlaybackFrame();

        QTest::mousePress(strip, Qt::LeftButton, Qt::NoModifier, at);
        QVERIFY2(std::llabs(p->getCentreFrame() - target) <= 16,
                 qPrintable(QString("centred on %1, not %2")
                            .arg(p->getCentreFrame()).arg(target)));

        QPoint to = at - QPoint(40, 0);
        for (int i = 1; i <= 4; ++i) {
            QTest::mouseMove(strip, at + (to - at) * i / 4);
        }
        QTest::mouseRelease(strip, Qt::LeftButton, Qt::NoModifier, to);
        double expected = double(target) - 40 * stripFramesPerPixel();
        QVERIFY2(std::fabs(double(p->getCentreFrame()) - expected) <= 17.0,
                 qPrintable(QString("dragged to %1, not %2")
                            .arg(p->getCentreFrame()).arg(expected)));

        QTRY_VERIFY(paintedAsNow());
        QVERIFY(SongScroll::hitsThumb(strip->getPaintedThumb(), to.x()));
        QCOMPARE(m_window->viewManager()->getPlaybackFrame(), playhead);
    }

    // The thumb follows a zoom, and playback's turning of the page,
    // which moves the panes without a signal of their own
    void song_scroll_bar_follows_zoom_and_paging() {
        openCompactWithReference();
        if (QTest::currentTestFailed()) return;

        SongScrollBar *strip = songScroll();
        sv::Pane *p = pane();
        setView(32, 20000);
        QTRY_VERIFY(paintedAsNow());
        double wide = strip->getPaintedThumb().width();

        m_window->zoomInAction()->trigger();
        QVERIFY(p->getZoomLevel().level < 32);
        QTRY_VERIFY(paintedAsNow());
        QVERIFY2(strip->getPaintedThumb().width() < wide - 10.0,
                 qPrintable(QString("%1 wide, was %2")
                            .arg(strip->getPaintedThumb().width()).arg(wide)));

        setView(16, 10000);
        QTRY_VERIFY(paintedAsNow());
        SongScroll::Thumb before = strip->getPaintedThumb();

        sv::sv_frame_t ahead = 60000;
        QVERIFY(ahead > p->getEndFrame());
        m_window->viewManager()->setPlaybackFrame(ahead);
        QVERIFY2(p->getStartFrame() <= ahead && ahead <= p->getEndFrame(),
                 "the pane did not turn the page");
        QTRY_VERIFY(paintedAsNow());
        QVERIFY2(strip->getPaintedThumb().x0 > before.x0 + 100.0,
                 qPrintable(text(strip->getPaintedThumb())));
        int x = int(std::lround(SongScroll::xForFrame
                                (ahead, strip->getSongFrames(),
                                 strip->width())));
        QCOMPARE(strip->getPaintedPlayheadX(), x);
    }

    // The contour is the reference's pitch; closing the session leaves
    // none of it, nor a thumb, and the next reference has its own
    void song_scroll_bar_contour_and_closing_the_session() {
        openCompactWithReference();
        if (QTest::currentTestFailed()) return;

        SongScrollBar *strip = songScroll();
        Analyser *a = m_window->analyser();
        QCOMPARE(strip->getSongModel(), a->getMainModelId());
        QCOMPARE(strip->getPitchModel(),
                 a->getLayer(Analyser::PitchTrack)->getModel());

        // The sine is voiced throughout, at the one pitch
        int columns = int(std::lround(strip->width() *
                                      strip->devicePixelRatioF()));
        QTRY_VERIFY2(voicedColumns() > columns * 8 / 10,
                     qPrintable(QString("%1 of %2 columns voiced")
                                .arg(voicedColumns()).arg(columns)));
        const auto &contour = strip->getContour();
        for (int c = columns / 4; c < columns * 3 / 4; ++c) {
            QVERIFY2(!contour[c].isEmpty() &&
                     std::fabs(contour[c].low - 220.5) < 3.0 &&
                     std::fabs(contour[c].high - 220.5) < 3.0,
                     qPrintable(QString("column %1: %2 to %3").arg(c)
                                .arg(contour[c].low).arg(contour[c].high)));
        }

        m_window->doCloseSession();
        settle();
        QVERIFY(strip->getSongModel().isNone());
        QVERIFY(strip->getPitchModel().isNone());
        strip->repaint();
        QCOMPARE(voicedColumns(), 0);
        QCOMPARE(strip->getPaintedThumb().width(), 0.0);
        QCOMPARE(strip->getPaintedPlayheadX(), -1);

        // Nothing to move, and nothing breaks
        QTest::mouseClick(strip, Qt::LeftButton, Qt::NoModifier,
                          QPoint(strip->width() / 2, strip->height() / 2));

        openReference();
        if (QTest::currentTestFailed()) return;
        QCOMPARE(strip->getSongModel(), a->getMainModelId());
        QTRY_VERIFY2(voicedColumns() > columns * 8 / 10,
                     qPrintable(QString("%1 of %2 columns voiced")
                                .arg(voicedColumns()).arg(columns)));
    }
};

#endif
