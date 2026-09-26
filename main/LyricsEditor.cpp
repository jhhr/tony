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

#include <QAction>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>

#include <algorithm>

using namespace sv;

namespace {

// An edit that is done already, as the command was filled, goes on the
// history as it is.  CommandHistory marks the session modified
void
pushDone(ChangeEventsCommand *command)
{
    command = command->finish();
    if (command) {
        CommandHistory::getInstance()->addCommand(command, false);
    }
}

}

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

std::shared_ptr<RegionModel>
LyricsEditor::editedModel(ModelId id) const
{
    // Edit mode gone off, or other lyrics than the ones the edit began
    // in: an import or a new session in the meantime
    RegionLayer *layer = currentLayer();
    if (!m_enabled || !layer || id.isNone() || layer->getModel() != id) {
        return {};
    }
    return ModelById::getAs<RegionModel>(id);
}

bool
LyricsEditor::wordsAt(QPoint pos, EventVector &words,
                      LyricsEdit::Boxes &boxes) const
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
    boxes = LyricsEdit::boxesFor
        (words, [pane](sv_frame_t frame) { return pane->getXForFrame(frame); });
    return true;
}

bool
LyricsEditor::hitAt(QPoint pos, LyricsEdit::Hit &hit,
                    EventVector &words) const
{
    LyricsEdit::Boxes boxes;
    if (!wordsAt(pos, words, boxes)) return false;
    hit = LyricsEdit::hitTest(boxes, pos.x(), grabPixels);
    return true;
}

std::vector<LyricsEditor::MenuEntry>
LyricsEditor::menuEntriesAt(QPoint pos) const
{
    std::vector<MenuEntry> entries;
    if (!m_enabled || m_dragging) return entries;

    EventVector words;
    LyricsEdit::Boxes boxes;
    if (!wordsAt(pos, words, boxes)) return entries;

    ModelId modelId = currentLayer()->getModel();
    auto model = ModelById::getAs<RegionModel>(modelId);
    if (!model) return entries;

    // On a word, anywhere in its box, near its edges too: the menu is for
    // the word the pointer is on
    int word = LyricsEdit::wordAt(boxes, pos.x());
    if (word >= 0) {
        MenuEntry edit;
        edit.text = tr("Edit Word Text...");
        edit.enabled = true;
        edit.operation = MenuEntry::Operation::EditText;
        edit.model = modelId;
        edit.word = words[word];
        entries.push_back(edit);

        MenuEntry remove = edit;
        remove.text = tr("Delete Word");
        remove.operation = MenuEntry::Operation::DeleteWord;
        entries.push_back(remove);
        return entries;
    }

    // Between words, at the last frame the pointer's column shows.  Its
    // first can be inside the word before: that word's end is somewhere
    // in the column just after its box
    sv_frame_t frame = std::max(m_pane->getFrameForX(pos.x()),
                                m_pane->getFrameForX(pos.x() + 1) - 1);
    LyricsEdit::Span span;

    MenuEntry add;
    add.text = tr("Add Word...");
    add.enabled = LyricsEdit::newWordSpan(words, frame,
                                          model->getSampleRate(), span);
    add.operation = MenuEntry::Operation::AddWord;
    add.model = modelId;
    add.frame = frame;
    entries.push_back(add);
    return entries;
}

void
LyricsEditor::choose(const MenuEntry &entry)
{
    if (!entry.enabled || m_dragging) return;

    switch (entry.operation) {
    case MenuEntry::Operation::EditText:
        editText(entry.model, entry.word);
        break;
    case MenuEntry::Operation::DeleteWord:
        deleteWord(entry.model, entry.word);
        break;
    case MenuEntry::Operation::AddWord:
        addWord(entry.model, entry.frame);
        break;
    }
}

void
LyricsEditor::editText(ModelId modelId, Event word)
{
    if (!m_askText) return;
    {
        auto model = editedModel(modelId);
        if (!model || !model->containsEvent(word)) return;
    }

    QString text = word.getLabel();
    if (!m_askText(text, false)) return;

    // The question had an event loop of its own, and the lyrics may have
    // gone, or the word been changed, meanwhile: then this edit is of
    // something that is not there
    auto model = editedModel(modelId);
    if (!model || !model->containsEvent(word)) return;

    // Nothing left of the text is refused, and the word stays as it was:
    // Delete Word is for deleting it
    QString cleaned = LyricsEdit::cleanText(text);
    if (cleaned == "" || cleaned == word.getLabel()) return;

    auto command = new ChangeEventsCommand
        (modelId.untyped, tr("Change Word Text"));
    command->remove(word);
    command->add(word.withLabel(cleaned));
    pushDone(command);
}

void
LyricsEditor::deleteWord(ModelId modelId, Event word)
{
    auto model = editedModel(modelId);
    if (!model || !model->containsEvent(word)) return;

    auto command = new ChangeEventsCommand
        (modelId.untyped, tr("Delete Word"));
    command->remove(word);
    pushDone(command);
}

void
LyricsEditor::addWord(ModelId modelId, sv_frame_t frame)
{
    if (!m_askText) return;
    {
        auto model = editedModel(modelId);
        LyricsEdit::Span span;
        if (!model ||
            !LyricsEdit::newWordSpan(model->getAllEvents(), frame,
                                     model->getSampleRate(), span)) {
            return;
        }
    }

    QString text;
    if (!m_askText(text, true)) return;
    QString cleaned = LyricsEdit::cleanText(text);
    if (cleaned == "") return;

    // Where the word goes, and the line it joins, from the words as they
    // are after the question, which may not be as they were before it
    auto model = editedModel(modelId);
    if (!model) return;
    EventVector words = model->getAllEvents();
    LyricsEdit::Span span;
    if (!LyricsEdit::newWordSpan(words, frame, model->getSampleRate(), span)) {
        return;
    }
    float line = LyricsEdit::newWordLine(words, span);

    // The layer draws the word bold if it is the first of its line now
    auto command = new ChangeEventsCommand
        (modelId.untyped, tr("Add Word"));
    command->add(Event(span.start, line, span.duration(), cleaned));
    pushDone(command);
}

bool
LyricsEditor::showMenu(QPoint pos)
{
    std::vector<MenuEntry> entries = menuEntriesAt(pos);
    if (entries.empty()) return false;

    // As the pane does for its own menu
    clearHelp();

    // Shown and left to run by itself, as Tony's own menu is: the entry
    // chosen is done from the menu's event handling, and its question
    // is not asked from inside the pane's.  Deleted once it is hidden,
    // after the entry chosen is done
    QMenu *menu = new QMenu(m_pane);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    for (const MenuEntry &entry : entries) {
        QAction *action = menu->addAction(entry.text);
        action->setEnabled(entry.enabled);
        connect(action, &QAction::triggered, this,
                [this, entry]() { choose(entry); });
    }
    menu->popup(m_pane->mapToGlobal(pos));
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

    QPoint pos = e->position().toPoint();

    // A right press in the row is for the words' menu, and never reaches
    // the pane, which would open its own as well.  Outside the row it is
    // the pane's, and so is its menu
    if (e->button() == Qt::RightButton) return showMenu(pos);

    if (e->button() != Qt::LeftButton) return false;

    LyricsEdit::Hit hit;
    EventVector words;
    if (!hitAt(pos, hit, words)) return false;

    RegionLayer *layer = currentLayer();
    if (!layer || layer->getModel().isNone()) return false;

    if (!hit.isEdge()) {

        // A double-click in a word, away from its edges, is for its
        // text.  The pane has had the first click of it, and moves the
        // playback cursor there as for any click: it never sees the
        // second, which would have called that off
        if (e->type() == QEvent::MouseButtonDblClick &&
            hit.part == LyricsEdit::Part::Inside) {
            editText(layer->getModel(), words[hit.word]);
            return true;
        }
        return false;
    }

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
    } else if (hit.part == LyricsEdit::Part::Inside) {
        restoreCursor();
        showHelp(tr("Double-click to change the text of \"%1\", "
                    "right-click to delete it")
                 .arg(words[hit.word].getLabel()));
    } else {
        restoreCursor();
        showHelp(tr("Right-click to add a word, "
                    "drag a word's start or end to move it"));
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

    // The model can change under a drag: a keyboard undo that takes the
    // word away, or the lyrics removed or replaced.  The word being
    // dragged is then not what the drag last made it, and nothing it
    // would do now is right.  An undo that leaves the word as the drag
    // made it goes unnoticed: see docs/open-points.md
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
