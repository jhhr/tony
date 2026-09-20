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

#ifndef TONY_TAKE_LAYERS_H
#define TONY_TAKE_LAYERS_H

#include <QString>

namespace sv {
class Layer;
class Pane;
class TimeValueLayer;
class FlexiNoteLayer;
class RegionLayer;
}

/**
 * The layers of a take, found by name.
 *
 * Every take of a session has three layers in pane 0 -- pitch, notes and
 * coverage -- and their object names carry the identity of the take they
 * belong to: "Take 2 Pitch", "Take 2 Notes", "Take 2 Coverage" (spec
 * 6.4).  That is the only link between a take and what shows it: no
 * pointer to a layer is kept anywhere but in the analyser of the take
 * that is active, so a take whose layers were put back by a session load
 * is found the same way as one that has been switched away from.
 *
 * The names are not translated: they are stored in the session file.
 */
class TakeLayers
{
public:
    enum Kind { Pitch, Notes, Coverage };

    /// The object name of that layer of the take of this name
    static QString nameFor(QString takeName, Kind kind);

    /**
     * Take a layer's object name apart again.  False if it is not the
     * name of a take's layer at all.
     */
    static bool parse(QString layerName, QString &takeName, Kind &kind);

    /// The layers of the take of this name that are in the pane
    struct Found {
        sv::TimeValueLayer *pitch = nullptr;
        sv::FlexiNoteLayer *notes = nullptr;
        sv::RegionLayer *coverage = nullptr;
    };

    static Found find(sv::Pane *pane, QString takeName);

    /**
     * Move a layer to the top of the pane's layer stack, which is where
     * the editing tools look for the layer to act on: the note tool takes
     * the pane's topmost note layer, dormant or not, so the active take's
     * notes have to be above the takes that are put away.
     *
     * Done on the view alone -- the layer is not added to or removed from
     * the document, so nothing is created, deleted or made undoable, and
     * the layer goes on being saved with the session.  See the note in
     * the implementation about the fork change that would retire this.
     */
    static void raise(sv::Pane *pane, sv::Layer *layer);
};

#endif
