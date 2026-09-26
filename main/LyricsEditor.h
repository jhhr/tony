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

#ifndef TONY_LYRICS_EDITOR_H
#define TONY_LYRICS_EDITOR_H

#include "LyricsEdit.h"

#include "base/BaseTypes.h"
#include "base/Event.h"
#include "data/model/Model.h"

#include <QCursor>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QMouseEvent;
class LyricsTrack;

namespace sv {
class Pane;
class RegionLayer;
class RegionModel;
class ChangeEventsCommand;
}

/**
 * Edit mode for the lyrics (Edit > Edit Lyrics): the mouse in the box
 * row of the lyrics, along the bottom of their pane, moves a word's
 * start or end, changes its text on a double-click, and on a right
 * click offers a small menu to change or delete the word there, or to
 * add one in the space between words.
 *
 * The lyrics layer is never the pane's top layer, so the pane's tools
 * never reach it: this watches the pane's mouse events through an
 * event filter, which is on only while edit mode is.  Only what it acts
 * on is kept from the pane: a left press on an edge and the drag it
 * starts, a double-click on a word, a right press anywhere in the box
 * row, and moves with no button held in the box row, where the cursor
 * and the context help are this one's.  Everything else, a click
 * anywhere to move the playback cursor included, goes to the pane as it
 * would without edit mode.
 *
 * What an edit may do is LyricsEdit's to say; this does as it says.
 * A drag edits the model as it goes, so the words move under the
 * pointer, and is one command on the undo history when the button is
 * let go, or nothing at all if the word is where it was.  A change of
 * text, an added word and a deleted one are a command each.
 *
 * The layer and the model are found through LyricsTrack at every
 * event, and nothing of them is kept between drags: an import, a
 * remove or another session can replace them at any time.  For the
 * same reason a menu entry holds the word as a value, and the word is
 * looked for again when the entry is chosen and again once its text
 * has been asked for.
 *
 * Like the lyrics track itself, MainWindow owns it and only wires it.
 */
class LyricsEditor : public QObject
{
    Q_OBJECT

public:
    LyricsEditor(LyricsTrack *lyrics, QObject *parent = nullptr);
    virtual ~LyricsEditor();

    /**
     * Switch edit mode on, in the pane the lyrics are in, or off.  On
     * does nothing if there are no lyrics.  Off finishes a drag that is
     * in progress first, as letting go of the button would, then puts
     * the pane's cursor back and takes the event filter off.
     */
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    /// True from the press on an edge to the release
    bool isDragging() const { return m_dragging; }

    /// How near an edge, in logical pixels on either side, grabs it
    static constexpr int grabPixels = 6;

    /**
     * How a word's text is asked for: given the text the word has ("" for
     * a new word), true with the text the user gave in its place, false
     * if they cancelled.  MainWindow asks with a dialog, whose event loop
     * runs while the question is open.  With none set, no text is changed
     * and no word added.
     */
    typedef std::function<bool(QString &text, bool isNew)> TextQuestion;
    void setTextQuestion(TextQuestion question) { m_askText = question; }

    /**
     * One entry of the menu a right press in the box row opens.  Values
     * only: the word as it was when the menu was made, and the model it
     * was in, which choose() looks for again.
     */
    struct MenuEntry {
        enum class Operation { EditText, DeleteWord, AddWord };

        QString text;
        bool enabled = false;
        Operation operation = Operation::EditText;
        sv::ModelId model;

        /// EditText, DeleteWord: the word the pointer was on
        sv::Event word;

        /// AddWord: the frame the new word goes at
        sv::sv_frame_t frame = 0;
    };

    /**
     * The entries of the menu for a right press at this point of the
     * pane, in the order shown: on a word "Edit Word Text..." and "Delete
     * Word", elsewhere in the box row "Add Word...", disabled where
     * there is no room for a word.  None if the point is not in the box
     * row, or edit mode is off: the press is then the pane's.
     */
    std::vector<MenuEntry> menuEntriesAt(QPoint pos) const;

    /**
     * Do what an entry says, as choosing it in the menu does: nothing
     * if it is disabled, if edit mode is off, or if the lyrics have
     * changed so that its word is not there as it was.  The text is
     * asked for first, where the entry needs one.
     */
    void choose(const MenuEntry &entry);

signals:
    /// What the mouse does where the pointer is, "" when that is over
    void contextHelpChanged(const QString &);

protected:
    bool eventFilter(QObject *, QEvent *) override;

private:
    LyricsTrack *m_lyrics;
    bool m_enabled;

    // The pane whose events are filtered, while edit mode is on
    QPointer<sv::Pane> m_pane;

    // The drag.  The words are those of the model at the press, and the
    // same ones go to LyricsEdit at every move: the limits of the edge
    // come from where the word was then, so a drag back puts it back
    bool m_dragging;
    sv::ModelId m_dragModel;
    sv::EventVector m_dragWords;
    int m_dragWord;
    LyricsEdit::Part m_dragPart;
    sv::sv_frame_t m_dragEdgeFrame;
    sv::sv_frame_t m_dragPressFrame;
    sv::Event m_dragOriginal;
    sv::Event m_dragCurrent;
    sv::ChangeEventsCommand *m_dragCommand;

    // The pane's own cursor, while ours is shown over an edge
    bool m_cursorSet;
    QCursor m_savedCursor;

    // Whether the context help is ours
    bool m_helpShown;

    // The lyrics layer, if it is in the pane being edited, shown or
    // hidden; and its model, if that is the one the drag began in
    sv::RegionLayer *currentLayer() const;
    std::shared_ptr<sv::RegionModel> dragModel() const;

    TextQuestion m_askText;

    // The words and their boxes, if the point is in the box row: false
    // if it is not, or there are no lyrics on show.  The words are the
    // model's now
    bool wordsAt(QPoint pos, sv::EventVector &words,
                 LyricsEdit::Boxes &boxes) const;

    // What the pointer is on, if it is in the box row, as wordsAt()
    bool hitAt(QPoint pos, LyricsEdit::Hit &hit,
               sv::EventVector &words) const;

    // The lyrics' model, if it is this one and is still being edited
    std::shared_ptr<sv::RegionModel> editedModel(sv::ModelId) const;

    // The three edits.  Each looks for its word in the model again, asks
    // for a text where it needs one, and looks again after the question,
    // whose event loop may have let anything happen
    void editText(sv::ModelId model, sv::Event word);
    void deleteWord(sv::ModelId model, sv::Event word);
    void addWord(sv::ModelId model, sv::sv_frame_t frame);

    // The menu of menuEntriesAt(), shown at this point.  False if there
    // is none there
    bool showMenu(QPoint pos);

    bool mousePressed(QMouseEvent *);
    bool mouseMoved(QMouseEvent *);
    bool mouseReleased(QMouseEvent *);

    // Cursor and context help for a pointer here with no button held.
    // True if it is in the box row
    bool hover(QPoint pos);
    void leaveRow();

    void dragTo(int x);

    // Push the drag's command, if the word has moved
    void finishDrag();

    // Throw the drag's command away, without touching the model: the
    // model has changed under the drag
    void abandonDrag();

    void setEdgeCursor();
    void restoreCursor();
    void showHelp(const QString &);
    void clearHelp();
};

#endif
