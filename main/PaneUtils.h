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

#ifndef TONY_PANE_UTILS_H
#define TONY_PANE_UTILS_H

#include "base/ById.h"
#include "data/model/Model.h"

namespace sv {
class Document;
class PaneStack;
class Pane;
class Overview;
}

/**
 * Remove an unwanted extra pane that openAudio()/record() created via
 * AddPaneCommand in CreateAdditionalModel mode. Shared by the record,
 * load-singing-track and load-background-music flows, all of which
 * want the new audio overlaid on pane 0 rather than in its own pane.
 *
 * Layers in the pane whose model is ownedModelId (the imported
 * waveform) are deleted from the document; any other layer (the
 * shared time ruler) is only detached from this pane. The pane is
 * then unregistered from the overview, if one is given, and deleted.
 * The pane may be visible or hidden.
 *
 * Some other layer must already reference ownedModelId, otherwise
 * Document::releaseModel() frees the model along with the orphan.
 */
void pruneExtraPane(sv::Document *document,
                    sv::PaneStack *paneStack,
                    sv::Pane *extra,
                    sv::ModelId ownedModelId,
                    sv::Overview *overview = nullptr);

#endif
