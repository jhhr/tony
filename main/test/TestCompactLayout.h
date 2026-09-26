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

#include "version.h"

#include "view/Overview.h"
#include "view/ViewManager.h"
#include "data/fileio/WavFileWriter.h"
#include "transform/ModelTransformerFactory.h"

#include <QObject>
#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

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
    SingingTakes *takes() { return m_takes; }
    sv::ViewManager *viewManager() { return m_viewManager; }
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
};

#endif
