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

#ifndef TONY_ALTERNATE_PITCH_TRACK_H
#define TONY_ALTERNATE_PITCH_TRACK_H

#include <QObject>
#include <QString>
#include <QTimer>

#include "base/ById.h"
#include "data/model/Model.h"

namespace sv {
class Document;
class Pane;
class Layer;
class TimeValueLayer;
}

/**
 * The alternate pitch track: a copy of the reference pitch track moved
 * up or down by a whole number of octaves, for a singer whose range is
 * not that of the reference.  It is drawn in dark brown, faded unless a
 * take is being recorded ("followed"), when it stands in for the
 * reference pitch track, which MainWindow hides for the duration.
 *
 * The copy is a model of its own, rebuilt whenever the reference pitch
 * model changes.  It is never edited, never played, and has no source
 * model, so no Analyser claims its layer.
 *
 * The layer and its model belong to the document and are saved in the
 * session with it.  The number of octaves is kept in the layer's object
 * name, which is also what adopt() knows the layer by after a session
 * has been loaded.
 */
class AlternatePitchTrack : public QObject
{
    Q_OBJECT

public:
    static constexpr int minOctaves = -3;
    static constexpr int maxOctaves = 3;

    AlternatePitchTrack(QObject *parent = nullptr);
    virtual ~AlternatePitchTrack();

    /**
     * Create the layer in the given pane, on top of whatever is there.
     * Does nothing if the layer exists already.  Returns false if it
     * could not be created.
     */
    bool show(sv::Document *document, sv::Pane *pane);

    /**
     * Take over a layer that show() made in an earlier run, and that a
     * session load has put back into the pane, along with the number
     * of octaves it was saved with.  Returns false if there is none.
     */
    bool adopt(sv::Document *document, sv::Pane *pane);

    /**
     * Delete the layer and its model from the document.  The number
     * of octaves is remembered.
     */
    void hide();

    bool isShown() const { return m_layer != nullptr; }

    /**
     * The model to copy: that of the reference pitch track, or none.
     */
    void setSource(sv::ModelId referencePitchModel);
    sv::ModelId getSource() const { return m_source; }

    /**
     * Distance from the reference in octaves, within minOctaves to
     * maxOctaves.  Never zero: that is the reference itself, and
     * stepping across it goes from -1 to 1.
     */
    int getOctaves() const { return m_octaves; }
    void setOctaves(int octaves);
    bool canStep(bool up) const;
    void step(bool up);

    /**
     * Drawn in full rather than faded.  For the duration of a take.
     */
    void setFollowed(bool followed);
    bool isFollowed() const { return m_followed; }

    sv::TimeValueLayer *getLayer() const { return m_layer; }

    /**
     * Copy the source again now, rather than when the event loop next
     * comes round.
     */
    void rebuildNow();

    static double shifted(double hz, int octaves);
    static QString layerNameFor(int octaves);
    static bool octavesFromLayerName(QString name, int &octaves);
    static QString describe(int octaves);

private slots:
    void sourceChanged();
    void layerAboutToBeDeleted(sv::Layer *);

private:
    sv::Document *m_document;
    sv::Pane *m_pane;
    sv::TimeValueLayer *m_layer;
    sv::ModelId m_source;
    int m_octaves;
    bool m_followed;
    QTimer m_rebuildTimer;

    void takeLayer(sv::Document *, sv::Pane *, sv::TimeValueLayer *);
    void configureLayer();
    void applyColour();
};

#endif
