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

#ifndef TONY_COVERAGE_STRIP_H
#define TONY_COVERAGE_STRIP_H

#include "Coverage.h"

#include "base/ById.h"
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
 * The coverage strip of a singing take: a RegionLayer in pane 0 with one
 * region per range of the take's coverage.  It shows the user where the
 * take holds recorded material, and it is also where that coverage is
 * stored: the layer and its model are ordinary document contents, so a
 * session keeps them, and a session that has them is where the coverage
 * of its take comes from when it is loaded again.
 *
 * The strip is display only.  It is never the pane's selected layer (the
 * caller re-stacks the editable tracks after it is created), its model
 * cannot be played (a RegionModel has no play parameters), and its
 * vertical scale is the "equal spaced" one, which neither draws a scale
 * of its own nor lets anything align to it, so the pane's own scale is
 * untouched.
 *
 * It looks like the alternate pitch track's class and is used the same
 * way: MainWindow only wires it.
 */
class CoverageStrip : public QObject
{
    Q_OBJECT

public:
    CoverageStrip(QObject *parent = nullptr);
    virtual ~CoverageStrip();

    /**
     * Create the layer in the given pane, on top of whatever is there.
     * Does nothing if the layer exists already.  Returns false if it
     * could not be created.
     */
    bool show(sv::Document *document, sv::Pane *pane);

    /**
     * Take over a layer that show() made in an earlier run, and that a
     * session load has put back into the pane.  Returns false if there
     * is none; the coverage it holds is then read with getCoverage().
     */
    bool adopt(sv::Document *document, sv::Pane *pane);

    /// Delete the layer and its model from the document
    void hide();

    bool isShown() const { return m_layer != nullptr; }

    /// The ranges the strip shows, which are the take's coverage
    void setCoverage(const Coverage &coverage);
    Coverage getCoverage() const;

    sv::RegionLayer *getLayer() const { return m_layer; }

    /// The model the regions are in, which is the stored coverage
    sv::ModelId getModelId() const;

    /**
     * The layer's object name, which is how adopt() knows it again.
     * One function because phase 7a of the takes work names the layer
     * after its take.
     */
    static QString layerName();

private slots:
    void layerAboutToBeDeleted(sv::Layer *);

private:
    sv::Document *m_document;
    sv::Pane *m_pane;
    sv::RegionLayer *m_layer;

    void takeLayer(sv::Document *, sv::Pane *, sv::RegionLayer *);
    void configureLayer();
};

#endif
