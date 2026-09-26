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

#ifndef TONY_COMPACT_LAYOUT_H
#define TONY_COMPACT_LAYOUT_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <utility>

class QAction;
class QComboBox;
class QMainWindow;
class QMenu;
class QToolBar;
class QToolButton;
class QWidget;

/**
 * The window laid out for a phone held in landscape: one toolbar of
 * touch-sized buttons in place of the menu bar and every other toolbar.
 * It is on from the start on Android; on the desktop the View menu
 * switches it (getAction()), and so does --compact at start.
 *
 * The toolbar holds the window's own actions, handed over by MainWindow
 * in Parts: a menu button whose popup holds every menu of the menu bar,
 * so that nothing becomes unreachable (and the menus' shortcuts keep
 * working, which they would not with only the hidden menu bar holding
 * them); Play, Record and Record into Selection; the take box; Undo and
 * Redo; Erase; Zoom In and Zoom Out; and a button that shows and hides
 * the panel, the Show and Play and speed toolbars at the bottom.
 *
 * The take box is moved into the toolbar and back rather than copied:
 * it is the same widget, so it cannot drift out of step with the takes.
 *
 * Hidden while compact: the note-editing tools and the audio device
 * menus (Parts::hiddenActions), the overview (Parts::hiddenWidgets) and
 * the tear-off handles of the menus, which a finger would catch.
 *
 * Switching off puts back exactly what was there when it was switched
 * on: the menu bar, which toolbars were on show, the hidden actions and
 * widgets, the tear-off handles and the take box in its place.  The
 * compact toolbar leaves the window's layout, and so its state.  The
 * tool mode is the one exception: switching on selects Navigate, as the
 * note-editing tool that could be selected is hidden, and switching off
 * leaves it so.
 */
class CompactLayout : public QObject
{
    Q_OBJECT

public:
    /// The window's own actions and widgets that compact mode uses
    struct Parts {
        QAction *play = nullptr;
        QAction *record = nullptr;
        QAction *recordIntoSelection = nullptr;
        QComboBox *takeBox = nullptr;
        QAction *erase = nullptr;
        QAction *zoomIn = nullptr;
        QAction *zoomOut = nullptr;

        /// Selected on switching on: every other tool is hidden
        QAction *navigateTool = nullptr;

        /// The toolbars the Show and Play button shows and hides
        QList<QToolBar *> panel;

        /// Hidden while compact (a submenu is hidden by its menuAction())
        QList<QAction *> hiddenActions;
        QList<QWidget *> hiddenWidgets;
    };

    explicit CompactLayout(QMainWindow *window);
    virtual ~CompactLayout();

    /// Before it is first switched on
    void setParts(const Parts &parts);

    /// The checkable switch, for the View menu
    QAction *getAction() const { return m_action; }

    bool isOn() const { return m_on; }

    /// The compact toolbar and two of its buttons: null until first on
    QToolBar *getToolBar() const { return m_toolBar; }
    QToolButton *getMenuButton() const { return m_menuButton; }
    QAction *getPanelAction() const { return m_panelAction; }

    /// The size of the compact toolbar's icons, in logical pixels: on a
    /// phone Qt makes those Android's dp, where the style's 24 is small
    /// for a finger
    static const int iconSize = 40;

    /// Whether the window starts compact: always on Android, elsewhere
    /// when the command line says --compact
    static bool isWantedAtStart(const QStringList &arguments);

public slots:
    void setOn(bool on);

private slots:
    void showPanel(bool shown);

private:
    void makeToolBar();
    void switchOn();
    void switchOff();
    void moveTakeBoxIn();
    void moveTakeBoxBack();
    void disableTearOff(QMenu *menu);

    QMainWindow *m_window;
    Parts m_parts;
    QAction *m_action;
    bool m_on = false;

    QToolBar *m_toolBar = nullptr;
    QMenu *m_menu = nullptr;
    QToolButton *m_menuButton = nullptr;
    QAction *m_panelAction = nullptr;
    QAction *m_takeBoxPlace = nullptr; // the take box goes before this

    // What switching on changed, to put back.  The flags say what it
    // was: for a widget whether it was hidden, for an action whether it
    // was visible, for a menu whether it could be torn off
    struct Saved {
        bool menuBarHidden = false;
        QList<std::pair<QPointer<QToolBar>, bool>> toolBars;
        QList<std::pair<QPointer<QWidget>, bool>> widgets;
        QList<std::pair<QPointer<QAction>, bool>> actions;
        QList<std::pair<QPointer<QMenu>, bool>> tearOffs;

        // The take box's action, its toolbar and the action after it
        // there (null if it was the last)
        QPointer<QAction> takeBoxAction;
        QPointer<QToolBar> takeBoxHome;
        QPointer<QAction> takeBoxNext;
    };
    Saved m_saved;
};

#endif
