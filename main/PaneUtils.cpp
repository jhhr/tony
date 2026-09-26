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

#include "PaneUtils.h"

#include "framework/Document.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/Overview.h"
#include "layer/Layer.h"

#include <iostream>

using namespace sv;
using std::cerr;
using std::endl;

void
pruneExtraPane(Document *document, PaneStack *paneStack, Pane *extra,
               ModelId ownedModelId, Overview *overview)
{
    // PRECONDITIONS: some other layer (the secondary analyser's
    // WaveformLayer, or the background music layer) already references
    // ownedModelId, otherwise deleting the orphan below makes
    // Document::releaseModel() free the model.  The pane widget must
    // still be alive, so that the Document::m_layerViewMap iteration in
    // deleteLayer(force=true) is valid.
    //
    // The extra pane contains two kinds of layer:
    //
    //   1. The orphan WaveformLayer from createImportedLayer() — unique to
    //      this pane, with ownedModelId as its model.  deleteLayer(force=true)
    //      removes it from the view without creating an undo command and
    //      deletes it from the document.  We must NOT use removeLayerFromView
    //      for it, as that would push a RemoveLayerCommand onto the undo
    //      stack holding a pointer to a layer we are about to delete.
    //
    //   2. Shared layers, i.e. the time ruler, which openAudio()/record()
    //      add to the new pane when MainWindowBase::m_timeRulerLayer is set
    //      (e.g. after a session load) and which also lives in pane 1.  We
    //      must NOT call deleteLayer() on it: that would remove the ruler
    //      from every view, free it, and leave m_timeRulerLayer dangling.
    //      removeLayerFromView is no good either: it pushes a
    //      RemoveLayerCommand with a raw pointer to the extra pane, which
    //      is about to be destroyed → crash on undo.  Use
    //      detachLayerFromView (no undo command): it removes the layer
    //      from the pane's display list AND updates m_layerViewMap so
    //      deletePane() leaves no dangling pointer.
    //
    // We distinguish them by whether the layer's model is ownedModelId.
    // The ruler's model is the main model (createMainModelLayer() gives
    // it that id), so it never matches the id of an additional model.
    // A layer that has no model at all has the "none" id, though, so an
    // empty ownedModelId must match nothing: without the isNone() guard
    // such a layer would be deleted rather than detached.
    if (!extra) return;

    if (document) {
        int layerCount = extra->getLayerCount();
        for (int i = layerCount - 1; i >= 0; --i) {
            Layer *lay = extra->getLayer(i);
            if (!lay) continue;

            if (!ownedModelId.isNone() && lay->getModel() == ownedModelId) {
                cerr << "pruneExtraPane: deleteLayer orphan "
                     << lay << " [" << lay->objectName().toStdString()
                     << "]" << endl;
                document->deleteLayer(lay, true);
            } else {
                cerr << "pruneExtraPane: detachLayerFromView "
                     << lay << " [" << lay->objectName().toStdString()
                     << "] (shared layer, keeping in other views)" << endl;
                document->detachLayerFromView(extra, lay);
            }
        }
    }

    // Now destroy the pane widget (works for hidden panes too).  By this
    // point m_layerViewMap no longer references this pane.
    if (overview) overview->unregisterView(extra);
    if (paneStack) paneStack->deletePane(extra);
}

void
updateViewFrames(View *view, sv_frame_t from, sv_frame_t to)
{
    if (!view || to <= from) return;

    // A pixel either side: a layer draws at the view's x for a frame,
    // scaled to the pixel ratio, and at least one physical pixel wide
    int x0 = view->getXForFrame(from) - 1;
    int x1 = view->getXForFrame(to) + 1;
    if (x1 < 0 || x0 >= view->width()) return;
    view->update(x0, 0, x1 - x0 + 1, view->height());
}
