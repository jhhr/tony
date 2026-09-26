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

#include "LyricsEditor.h"

#include "LyricsTrack.h"

#include "view/Pane.h"
#include "layer/RegionLayer.h"
#include "data/model/RegionModel.h"
#include "data/model/EventCommands.h"
#include "widgets/CommandHistory.h"

#include <QEvent>
#include <QMouseEvent>

using namespace sv;

LyricsEditor::LyricsEditor(LyricsTrack *lyrics, QObject *parent) :
    QObject(parent),
    m_lyrics(lyrics),
    m_enabled(false),
    m_dragging(false),
    m_dragWord(-1),
    m_dragPart(LyricsEdit::Part::Nothing),
    m_dragEdgeFrame(0),
    m_dragPressFrame(0),
    m_dragCommand(nullptr),
    m_cursorSet(false),
    m_helpShown(false)
{
}

LyricsEditor::~LyricsEditor()
{
    // Nothing is pushed from here: the history may be going as well
    abandonDrag();
    m_dragging = false;
    restoreCursor();
    if (m_pane) m_pane->removeEventFilter(this);
}

void
LyricsEditor::setEnabled(bool enabled)
{
    if (enabled == m_enabled) return;

    if (enabled) {
        Pane *pane = m_lyrics ? m_lyrics->getPane() : nullptr;
        if (!pane || !m_lyrics->getLayer()) return;
        m_pane = pane;
        m_enabled = true;
        pane->installEventFilter(this);
        return;
    }

    // Off before anything else, so that whatever the push of the drag's
    // command sets off finds edit mode off already, and does not come
    // back here half way through
    m_enabled = false;
    finishDrag();
    restoreCursor();
    clearHelp();
    if (m_pane) m_pane->removeEventFilter(this);
    m_pane = nullptr;
}

RegionLayer *
LyricsEditor::currentLayer() const
{
    if (!m_pane || !m_lyrics || m_lyrics->getPane() != m_pane) return nullptr;
    return m_lyrics->getLayer();
}

std::shared_ptr<RegionModel>
LyricsEditor::dragModel() const
{
    RegionLayer *layer = currentLayer();
    if (!layer || m_dragModel.isNone() || layer->getModel() != m_dragModel) {
        return {};
    }
    return ModelById::getAs<RegionModel>(m_dragModel);
}

bool
LyricsEditor::hitAt(QPoint pos, LyricsEdit::Hit &hit,
                    EventVector &words) const
{
    // Hidden lyrics keep the box row they were last painted with, which
    // is not there to be clicked
    RegionLayer *layer = currentLayer();
    if (!layer || !m_lyrics->isVisible()) return false;

    // As last painted, in the pane's own coordinates, which are the
    // mouse's.  Empty until the layer has been painted in the pane
    QRect row = layer->getLyricsBoxRow(m_pane);
    if (!row.contains(pos)) return false;

    auto model = ModelById::getAs<RegionModel>(layer->getModel());
    if (!model) return false;

    words = model->getAllEvents();
    Pane *pane = m_pane;
    LyricsEdit::Boxes boxes = LyricsEdit::boxesFor
        (words, [pane](sv_frame_t frame) { return pane->getXForFrame(frame); });
    hit = LyricsEdit::hitTest(boxes, pos.x(), grabPixels);
    return true;
}

bool
LyricsEditor::eventFilter(QObject *object, QEvent *event)
{
    if (!m_enabled || !m_pane || object != m_pane) return false;

    switch (event->type()) {

    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        // A double-click comes in place of the second press: on an edge,
        // that is where a drag begins as well
        return mousePressed(static_cast<QMouseEvent *>(event));

    case QEvent::MouseMove:
        return mouseMoved(static_cast<QMouseEvent *>(event));

    case QEvent::MouseButtonRelease:
        return mouseReleased(static_cast<QMouseEvent *>(event));

    case QEvent::Leave:
        if (!m_dragging) leaveRow();
        return false;

    default:
        return false;
    }
}

bool
LyricsEditor::mousePressed(QMouseEvent *e)
{
    // The pane never saw the press that began the drag, and must not see
    // another button in the middle of it: a right press would open its
    // menu with our button still held
    if (m_dragging) return true;

    if (e->button() != Qt::LeftButton) return false;

    QPoint pos = e->position().toPoint();
    LyricsEdit::Hit hit;
    EventVector words;
    if (!hitAt(pos, hit, words) || !hit.isEdge()) return false;

    RegionLayer *layer = currentLayer();
    if (!layer || layer->getModel().isNone()) return false;

    m_dragging = true;
    m_dragModel = layer->getModel();
    m_dragWords = words;
    m_dragWord = hit.word;
    m_dragPart = hit.part;
    m_dragOriginal = words[hit.word];
    m_dragCurrent = m_dragOriginal;
    m_dragEdgeFrame = m_dragOriginal.getFrame();
    if (m_dragPart == LyricsEdit::Part::End) {
        m_dragEdgeFrame += m_dragOriginal.getDuration();
    }
    m_dragPressFrame = m_pane->getFrameForX(pos.x());

    // Nothing is made until the word moves: a click on an edge is no
    // edit at all
    m_dragCommand = nullptr;

    setEdgeCursor();
    return true;
}

bool
LyricsEditor::mouseMoved(QMouseEvent *e)
{
    QPoint pos = e->position().toPoint();

    if (m_dragging) {
        if (e->buttons() & Qt::LeftButton) {
            dragTo(pos.x());
            return true;
        }
        // The release went somewhere else, a dialog that opened in the
        // middle of the drag perhaps: the drag ends where it got to
        finishDrag();
    }

    // Someone else's drag, which began outside the row: the pane's
    if (e->buttons() != Qt::NoButton) return false;

    return hover(pos);
}

bool
LyricsEditor::mouseReleased(QMouseEvent *e)
{
    if (!m_dragging) return false;

    if (e->button() == Qt::LeftButton) {
        QPoint pos = e->position().toPoint();
        dragTo(pos.x());
        finishDrag();
        hover(pos);
    }

    // Any release in the middle of the drag is ours, as its press was
    return true;
}

bool
LyricsEditor::hover(QPoint pos)
{
    LyricsEdit::Hit hit;
    EventVector words;
    if (!hitAt(pos, hit, words)) {
        leaveRow();
        return false;
    }

    if (hit.isEdge()) {
        setEdgeCursor();
        QString word = words[hit.word].getLabel();
        if (hit.part == LyricsEdit::Part::Start) {
            showHelp(tr("Drag to move the start of \"%1\"").arg(word));
        } else {
            showHelp(tr("Drag to move the end of \"%1\"").arg(word));
        }
    } else {
        restoreCursor();
        showHelp(tr("Drag a word's start or end to move it"));
    }

    // The row is ours while edit mode is on: the pane would only put its
    // own help and cursor over ours
    return true;
}

void
LyricsEditor::leaveRow()
{
    // Cleared here: the pane's own help need not reach the status bar
    // (Tony shows none for the reference's pane), and if it does, the
    // pane gets the same event next and says what it has to say
    restoreCursor();
    clearHelp();
}

void
LyricsEditor::dragTo(int x)
{
    if (!m_dragging || !m_pane || m_dragModel.isNone()) return;

    // The model can change under a drag: an undo from the keyboard, or
    // the lyrics removed or replaced.  The word being dragged is then not
    // what the drag last made it, and nothing it would do now is right
    auto model = dragModel();
    if (!model || !model->containsEvent(m_dragCurrent)) {
        abandonDrag();
        return;
    }

    // The edge moves as far as the pointer has, in time: the pointer
    // need not have been exactly on it, and the view may scroll
    sv_frame_t wanted = m_dragEdgeFrame +
        (m_pane->getFrameForX(x) - m_dragPressFrame);

    sv_samplerate_t rate = model->getSampleRate();
    Event moved = (m_dragPart == LyricsEdit::Part::Start ?
                   LyricsEdit::startDraggedTo(m_dragWords, m_dragWord,
                                              wanted, rate) :
                   LyricsEdit::endDraggedTo(m_dragWords, m_dragWord,
                                            wanted, rate));
    if (moved == m_dragCurrent) return;

    // Each move takes the word out and puts the moved one in, and the
    // command folds the two steps of each move before into this one: at
    // the end it holds the original out and the last one in.  The layer
    // lays the words out again and repaints as the model changes
    if (!m_dragCommand) {
        m_dragCommand = new ChangeEventsCommand
            (m_dragModel.untyped,
             m_dragPart == LyricsEdit::Part::Start ?
             tr("Move Word Start") : tr("Move Word End"));
    }
    m_dragCommand->remove(m_dragCurrent);
    m_dragCurrent = moved;
    m_dragCommand->add(m_dragCurrent);
}

void
LyricsEditor::finishDrag()
{
    if (!m_dragging) return;
    m_dragging = false;

    ChangeEventsCommand *command = m_dragCommand;
    m_dragCommand = nullptr;
    m_dragWords.clear();
    if (!command) return;

    // As in dragTo(): if the model has changed under the drag, there is
    // nothing right to put on the history
    auto model = dragModel();
    if (!model || !model->containsEvent(m_dragCurrent)) {
        delete command;
        return;
    }

    // Dragged away and back: the model holds the word as it was, and
    // there is nothing to undo
    if (m_dragCurrent == m_dragOriginal) {
        delete command;
        return;
    }

    // Done already, as the drag went.  CommandHistory marks the session
    // modified
    command = command->finish();
    if (command) {
        CommandHistory::getInstance()->addCommand(command, false);
    }
}

void
LyricsEditor::abandonDrag()
{
    // Deleting a command does not touch the model: whatever the drag had
    // done to it stays, as whatever changed it since left it
    delete m_dragCommand;
    m_dragCommand = nullptr;
    m_dragWords.clear();

    // The rest of the drag, up to the release, moves nothing, and is
    // still not the pane's: it never saw the press
    m_dragModel = ModelId();
}

void
LyricsEditor::setEdgeCursor()
{
    if (!m_pane) return;
    if (!m_cursorSet) {
        m_savedCursor = m_pane->cursor();
        m_cursorSet = true;
    }
    m_pane->setCursor(Qt::SizeHorCursor);
}

void
LyricsEditor::restoreCursor()
{
    if (!m_cursorSet) return;
    m_cursorSet = false;

    // Unless the pane has put another of its own in the meantime, for a
    // change of tool say: that one stays
    if (m_pane && m_pane->cursor().shape() == Qt::SizeHorCursor) {
        m_pane->setCursor(m_savedCursor);
    }
}

void
LyricsEditor::showHelp(const QString &help)
{
    // Every time, as the pane does: something else may have written the
    // status bar since
    m_helpShown = true;
    emit contextHelpChanged(help);
}

void
LyricsEditor::clearHelp()
{
    if (!m_helpShown) return;
    m_helpShown = false;
    emit contextHelpChanged("");
}
