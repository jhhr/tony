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

#include "TakeLayers.h"

#include "view/Pane.h"
#include "layer/TimeValueLayer.h"
#include "layer/FlexiNoteLayer.h"
#include "layer/RegionLayer.h"

using namespace sv;

// The word each kind of layer is named after its take
static QString
suffixFor(TakeLayers::Kind kind)
{
    switch (kind) {
    case TakeLayers::Pitch: return "Pitch";
    case TakeLayers::Notes: return "Notes";
    case TakeLayers::Coverage: default: return "Coverage";
    }
}

QString
TakeLayers::nameFor(QString takeName, Kind kind)
{
    if (takeName == "") return "";
    return takeName + " " + suffixFor(kind);
}

bool
TakeLayers::parse(QString layerName, QString &takeName, Kind &kind)
{
    for (Kind k : { Pitch, Notes, Coverage }) {
        QString suffix = " " + suffixFor(k);
        if (!layerName.endsWith(suffix)) continue;
        QString name = layerName.left(layerName.length() - suffix.length());
        if (name == "") continue;
        takeName = name;
        kind = k;
        return true;
    }
    return false;
}

TakeLayers::Found
TakeLayers::find(Pane *pane, QString takeName)
{
    Found found;
    if (!pane || takeName == "") return found;

    for (int i = 0; i < pane->getLayerCount(); ++i) {

        Layer *layer = pane->getLayer(i);
        QString name = layer->objectName();

        if (!found.pitch && name == nameFor(takeName, Pitch)) {
            found.pitch = qobject_cast<TimeValueLayer *>(layer);
        }
        if (!found.notes && name == nameFor(takeName, Notes)) {
            found.notes = qobject_cast<FlexiNoteLayer *>(layer);
        }
        if (!found.coverage && name == nameFor(takeName, Coverage)) {
            found.coverage = qobject_cast<RegionLayer *>(layer);
        }
    }

    return found;
}

void
TakeLayers::raise(Pane *pane, Layer *layer)
{
    if (!pane || !layer) return;

    // The stacking order of a pane is the order its layers were added in:
    // Analyser::stackLayers() does nothing in Tony, because it goes
    // through PaneStack::setCurrentLayer(), which needs a PropertyStack
    // that this pane stack has none of.  So the only way to put a layer on
    // top is to add it to the view again.
    //
    // View::removeLayer() and addLayer() are the view's own bookkeeping:
    // Document::m_layerViewMap is untouched, so the layer stays a layer of
    // this view as far as the document and the session file are concerned,
    // and no command is made.  If Pane::getTopFlexiNoteLayer() ever skips
    // dormant layers (a fork change), the active take's notes would be on
    // top without this.
    //
    // removeLayer() does not disconnect layerMeasurementRectsChanged,
    // which addLayer() connects, so that one is undone here: without it
    // the connection is made again on every switch.
    QObject::disconnect(layer, SIGNAL(layerMeasurementRectsChanged()),
                        pane, nullptr);

    pane->removeLayer(layer);
    pane->addLayer(layer);
}
