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

#include "CompactLayout.h"

#include "widgets/CommandHistory.h"

#include <QAction>
#include <QComboBox>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

CompactLayout::CompactLayout(QMainWindow *window) :
    QObject(window),
    m_window(window)
{
    m_action = new QAction(tr("&Compact Layout"), this);
    m_action->setCheckable(true);
    m_action->setStatusTip(tr("Show one toolbar of large buttons in place "
                              "of the menus and toolbars, as on a phone"));
    connect(m_action, &QAction::toggled, this, &CompactLayout::setOn);
}

CompactLayout::~CompactLayout()
{
    // The toolbar and its menu are the window's children, and go with it
}

void
CompactLayout::setParts(const Parts &parts)
{
    m_parts = parts;
}

bool
CompactLayout::isWantedAtStart(const QStringList &arguments)
{
#ifdef Q_OS_ANDROID
    (void)arguments;
    return true;
#else
    return arguments.contains("--compact");
#endif
}

void
CompactLayout::setOn(bool on)
{
    if (on != m_on) {
        if (on) switchOn();
        else switchOff();
    }
    m_action->setChecked(m_on);
}

void
CompactLayout::makeToolBar()
{
    // Made the first time it is wanted, so that a window that is never
    // compact has nothing of it
    m_toolBar = new QToolBar(tr("Compact Toolbar"), m_window);
    m_toolBar->setObjectName("Compact Toolbar");
    m_toolBar->setIconSize(QSize(iconSize, iconSize));

    // Not to be dragged away by a finger, nor hidden from the window's
    // menu of toolbars: the menus would go with it
    m_toolBar->setMovable(false);
    m_toolBar->toggleViewAction()->setVisible(false);

    // No icon to hand for the menus: the button shows the menu's title.
    // It opens its popup on a tap, not on a press held down
    m_menu = new QMenu(tr("Menu"), m_toolBar);
    m_toolBar->addAction(m_menu->menuAction());
    m_menuButton = qobject_cast<QToolButton *>
        (m_toolBar->widgetForAction(m_menu->menuAction()));
    if (m_menuButton) {
        m_menuButton->setPopupMode(QToolButton::InstantPopup);
    }
    m_toolBar->addSeparator();

    for (QAction *action: { m_parts.play, m_parts.record,
                            m_parts.recordIntoSelection }) {
        if (action) m_toolBar->addAction(action);
    }

    // The take box is not the toolbar's for good: see moveTakeBoxIn()
    m_takeBoxPlace = m_toolBar->addSeparator();

    // The same Undo and Redo as the window's own toolbar, with their
    // menus of what there is to undo and redo
    sv::CommandHistory::getInstance()->registerToolbar(m_toolBar);

    if (m_parts.erase) m_toolBar->addAction(m_parts.erase);
    m_toolBar->addSeparator();

    for (QAction *action: { m_parts.zoomIn, m_parts.zoomOut }) {
        if (action) m_toolBar->addAction(action);
    }
    m_toolBar->addSeparator();

    // Again no icon: the name of the bar it brings up
    m_panelAction = m_toolBar->addAction(tr("Show and Play"));
    m_panelAction->setCheckable(true);
    m_panelAction->setToolTip(tr("Show or hide what is shown and played, "
                                 "the gains and the playback speed"));
    connect(m_panelAction, &QAction::triggered,
            this, &CompactLayout::showPanel);

    // A button that shows text is only as tall as the text: these are the
    // toolbar's own buttons, and may grow to the height of the others
    for (QAction *action: m_toolBar->actions()) {
        if (!action->icon().isNull()) continue;
        if (auto button = qobject_cast<QToolButton *>
            (m_toolBar->widgetForAction(action))) {
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        }
    }
}

void
CompactLayout::switchOn()
{
    if (!m_toolBar) makeToolBar();

    // Before its action is hidden, which also disables it
    if (m_parts.navigateTool && !m_parts.navigateTool->isChecked()) {
        m_parts.navigateTool->trigger();
    }

    m_saved = Saved();

    // isHidden(), not isVisible(): on Android this happens before the
    // window is first shown, when nothing in it is visible yet
    QMenuBar *menuBar = m_window->menuBar();
    m_saved.menuBarHidden = menuBar->isHidden();
    menuBar->hide();

    for (QToolBar *toolBar: m_window->findChildren<QToolBar *>
             (QString(), Qt::FindDirectChildrenOnly)) {
        if (toolBar == m_toolBar) continue;
        m_saved.toolBars.push_back({ toolBar, toolBar->isHidden() });
        toolBar->hide();
    }

    for (QWidget *widget: m_parts.hiddenWidgets) {
        if (!widget) continue;
        m_saved.widgets.push_back({ widget, widget->isHidden() });
        widget->hide();
    }

    for (QAction *action: m_parts.hiddenActions) {
        if (!action) continue;
        m_saved.actions.push_back({ action, action->isVisible() });
        action->setVisible(false);
    }

    // The popup holds the menu bar's own menus, the same objects
    for (QAction *action: menuBar->actions()) {
        if (QMenu *menu = action->menu()) disableTearOff(menu);
        m_menu->addAction(action);
    }

    moveTakeBoxIn();

    m_panelAction->setChecked(false);

    m_window->addToolBar(Qt::TopToolBarArea, m_toolBar);

    // Explicitly: it was hidden when last taken away, and a toolbar
    // added to a window on show is otherwise shown only later
    m_toolBar->show();

    m_on = true;
}

void
CompactLayout::switchOff()
{
    moveTakeBoxBack();

    // The menus are the menu bar's alone again. clear() deletes only
    // actions of the popup's own, and these are the menus'
    m_menu->clear();

    m_window->removeToolBar(m_toolBar);

    for (const auto &saved: m_saved.tearOffs) {
        if (saved.first) saved.first->setTearOffEnabled(saved.second);
    }
    for (const auto &saved: m_saved.actions) {
        if (saved.first) saved.first->setVisible(saved.second);
    }
    for (const auto &saved: m_saved.widgets) {
        if (saved.first) saved.first->setHidden(saved.second);
    }
    for (const auto &saved: m_saved.toolBars) {
        if (saved.first) saved.first->setHidden(saved.second);
    }
    m_window->menuBar()->setHidden(m_saved.menuBarHidden);

    m_saved = Saved();
    m_on = false;
}

void
CompactLayout::disableTearOff(QMenu *menu)
{
    m_saved.tearOffs.push_back({ menu, menu->isTearOffEnabled() });
    menu->setTearOffEnabled(false);
    for (QAction *action: menu->actions()) {
        if (QMenu *submenu = action->menu()) disableTearOff(submenu);
    }
}

void
CompactLayout::moveTakeBoxIn()
{
    // A widget added to a toolbar is held by a QWidgetAction of the
    // toolbar's making, and it can be in one toolbar at a time: the
    // action is taken out of the one it is in and put in this one, and
    // the widget goes with it
    QComboBox *box = m_parts.takeBox;
    if (!box) return;
    QToolBar *home = qobject_cast<QToolBar *>(box->parentWidget());
    if (!home) return;

    QList<QAction *> actions = home->actions();
    for (int i = 0; i < actions.size(); ++i) {
        QWidgetAction *action = qobject_cast<QWidgetAction *>(actions[i]);
        if (!action || action->defaultWidget() != box) continue;
        m_saved.takeBoxAction = action;
        m_saved.takeBoxHome = home;
        m_saved.takeBoxNext = (i + 1 < actions.size() ? actions[i+1] : nullptr);
        home->removeAction(action);
        m_toolBar->insertAction(m_takeBoxPlace, action);
        return;
    }
}

void
CompactLayout::moveTakeBoxBack()
{
    QAction *action = m_saved.takeBoxAction;
    QToolBar *home = m_saved.takeBoxHome;
    if (!action || !home) return;

    m_toolBar->removeAction(action);

    // Where it was: before the action that followed it, if that is still
    // there, else at the end
    QAction *next = m_saved.takeBoxNext;
    if (next && !home->actions().contains(next)) next = nullptr;
    home->insertAction(next, action);
}

void
CompactLayout::showPanel(bool shown)
{
    for (QToolBar *toolBar: m_parts.panel) {
        if (toolBar) toolBar->setVisible(shown);
    }
}
