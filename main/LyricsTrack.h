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

#ifndef TONY_LYRICS_TRACK_H
#define TONY_LYRICS_TRACK_H

#include "base/ById.h"
#include "base/Event.h"
#include "data/model/Model.h"

#include <QObject>
#include <QString>

namespace sv {
class Document;
class Pane;
class Layer;
class RegionLayer;
}

/**
 * The timed lyrics of the session: a RegionLayer in pane 0 with one
 * region per word (lyricsToEvents()), drawn as words in boxes along the
 * bottom of the pane by the lyrics plot style of the svgui fork, with
 * the word at the playback position highlighted.
 *
 * The lyrics belong to the song, not to a take: one set per session,
 * which only an import replaces.  The layer and its model are ordinary
 * document contents, so a session keeps them, and the layer's object
 * name is how it is known again.
 *
 * The layer is display only.  It is never the pane's top layer, because
 * the pane takes the hover readout and the vertical scale from that one
 * and this style has neither; it cannot be played (a RegionModel has no
 * play parameters), and it takes no edits itself: LyricsEditor edits
 * its words.
 *
 * It looks like the coverage strip's class and is used the same way:
 * MainWindow only wires it.
 */
class LyricsTrack : public QObject
{
    Q_OBJECT

public:
    LyricsTrack(QObject *parent = nullptr);
    virtual ~LyricsTrack();

    /**
     * Make a layer holding these events in the given pane, under the
     * layer that is on top of the pane now.  Lyrics already shown are
     * replaced: their layer and model go first.  The presentation name
     * is what the user sees the layer called.  Returns false if the
     * layer could not be created.
     */
    bool show(sv::Document *document, sv::Pane *pane,
              const sv::EventVector &events, QString presentationName);

    /**
     * Take over a layer that show() made in an earlier run, and that a
     * session load has put back into the pane, with the visibility and
     * the presentation name it was saved with.  Returns false if there
     * is none.
     */
    bool adopt(sv::Document *document, sv::Pane *pane);

    /// Delete the layer and its model from the document
    void hide();

    bool isShown() const { return m_layer != nullptr; }

    /**
     * The user's Show Lyrics toggle: the layer stays, hidden.  Not kept
     * in the settings: the pane writes each layer's visibility into the
     * session, which is where it belongs.
     */
    void setVisible(bool visible);
    bool isVisible() const;

    /**
     * The playback position, or the recording position during a take:
     * the word there is the one highlighted.  The layer repaints only
     * when that is another word, so this can be called for every frame
     * the view manager reports.  Nothing is saved or marked modified.
     */
    void setPlaybackFrame(sv::sv_frame_t frame);

    sv::RegionLayer *getLayer() const { return m_layer; }

    /// The pane the layer is in, null when there are no lyrics
    sv::Pane *getPane() const { return m_layer ? m_pane : nullptr; }

    /// The model the words are in
    sv::ModelId getModelId() const;

    /**
     * The layer's object name, which is how it is known after a session
     * load.  Not translated: it is stored in the session file.
     */
    static QString layerName();

    /**
     * The share of svgui's size the words are drawn at: 65% on a phone,
     * where the desktop's size leaves room for only a few words in the
     * pane and a verse wants to be on show at once; all of it elsewhere.
     */
    static double textScale();

private slots:
    void layerAboutToBeDeleted(sv::Layer *);

private:
    sv::Document *m_document;
    sv::Pane *m_pane;
    sv::RegionLayer *m_layer;

    void takeLayer(sv::Document *, sv::Pane *, sv::RegionLayer *);
    void configureLayer();

    // If the lyrics are the pane's top layer, put the one under them on
    // top: see show()
    void keepUnderTop();
};

#endif
