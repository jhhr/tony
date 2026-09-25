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

#include "LyricsTrack.h"

#include "TakeLayers.h"

#include "framework/Document.h"
#include "view/Pane.h"
#include "layer/RegionLayer.h"
#include "layer/LayerFactory.h"
#include "layer/ColourDatabase.h"
#include "data/model/RegionModel.h"
#include "base/PlayParameters.h"

#include <QPointer>
#include <QTimer>

#include <iostream>

using namespace sv;

using std::cerr;
using std::endl;

// The words are in frames of the reference's timeline, as the parser
// worked them out: a coarser resolution would move them
static const int lyricsResolution = 1;

// Used only if the session has no main model to take the rate from,
// which cannot happen while there is a reference to time the words by
static const sv_samplerate_t defaultSampleRate = 44100;

LyricsTrack::LyricsTrack(QObject *parent) :
    QObject(parent),
    m_document(nullptr),
    m_pane(nullptr),
    m_layer(nullptr)
{
}

LyricsTrack::~LyricsTrack()
{
}

QString
LyricsTrack::layerName()
{
    return "Lyrics";
}

bool
LyricsTrack::show(Document *document, Pane *pane, const EventVector &events,
                  QString presentationName)
{
    if (!document || !pane) return false;

    // One set of lyrics per session: a new import takes the place of the
    // last, whose layer and model go with it
    if (m_layer) hide();

    sv_samplerate_t rate = defaultSampleRate;
    if (auto main = ModelById::get(document->getMainModel())) {
        rate = main->getSampleRate();
    }

    auto model = std::make_shared<RegionModel>(rate, lyricsResolution);
    model->setObjectName(tr("Lyrics"));
    for (const Event &e : events) model->add(e);
    ModelId modelId = ModelById::add(model);

    // Not createEmptyLayer(): see MainWindow::setupRealtimePitchLayer().
    // Before the model goes to the document, so that a failure here can
    // release it: the document must never hold a released model
    auto layer = qobject_cast<RegionLayer *>
        (document->createLayer(LayerFactory::Regions));
    if (!layer) {
        cerr << "LyricsTrack::show: failed to create layer" << endl;
        ModelById::release(modelId);
        return false;
    }

    document->addNonDerivedModel(modelId);
    document->setModel(layer, modelId);
    takeLayer(document, pane, layer);
    m_layer->setPresentationName(presentationName);

    // The pane takes its hover readout and its vertical scale from its
    // top layer, and this one has neither to give: whatever is on top
    // now stays there
    Layer *previousTop = pane->getTopLayer();

    // Not addLayerToView(): an import is not undoable, and hide() takes
    // the layer away with deleteLayer(force), which would leave an
    // AddLayerCommand holding a deleted layer
    document->attachLayerToView(pane, layer);

    if (previousTop) TakeLayers::raise(pane, previousTop);

    return true;
}

bool
LyricsTrack::adopt(Document *document, Pane *pane)
{
    if (m_layer) return true;
    if (!document || !pane) return false;

    for (int i = 0; i < pane->getLayerCount(); ++i) {
        auto layer = qobject_cast<RegionLayer *>(pane->getLayer(i));
        if (!layer || layer->objectName() != layerName()) continue;
        if (!ModelById::isa<RegionModel>(layer->getModel())) continue;
        takeLayer(document, pane, layer);

        // The session puts the layers back in the order they were saved
        // in, and the lyrics end up on top if the layer that was above
        // them went before the save.  Not to stay there, for the reason
        // show() leaves the top layer where it is
        keepUnderTop();

        return true;
    }

    return false;
}

void
LyricsTrack::takeLayer(Document *document, Pane *pane, RegionLayer *layer)
{
    m_document = document;
    m_pane = pane;
    m_layer = layer;

    connect(m_document, &Document::layerAboutToBeDeleted,
            this, &LyricsTrack::layerAboutToBeDeleted,
            Qt::UniqueConnection);

    configureLayer();
}

void
LyricsTrack::configureLayer()
{
    if (!m_layer) return;

    m_layer->setObjectName(layerName());

    // Words along the top of the pane: a plot style the svgui fork has
    // for this.  It has no vertical scale and takes no edits.
    // EqualSpaced as well, so that nothing in the pane can align its
    // scale to this layer
    m_layer->setVerticalScale(RegionLayer::EqualSpaced);
    m_layer->setPlotStyle(RegionLayer::PlotLyrics);

    // The bar under each word; the words themselves are in the view's
    // own colours.  The layer's default would be black
    m_layer->setBaseColour
        (ColourDatabase::getInstance()->getColourIndex(tr("Grey")));

    // A RegionModel cannot play, so there are none; if that ever
    // changes, the words are still not something to hear
    if (auto params = m_layer->getPlayParameters()) {
        params->setPlayAudible(false);
    }
}

void
LyricsTrack::hide()
{
    RegionLayer *layer = m_layer;
    m_layer = nullptr;

    // deleteLayer(force) and nothing else: see the notes on tearing a
    // layer down silently in MainWindow::teardownRealtimePitchLayer().
    // The model goes with the layer, which is its only user
    if (layer && m_document) {
        m_document->deleteLayer(layer, true);
    }

    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = nullptr;
    m_pane = nullptr;
}

void
LyricsTrack::setVisible(bool visible)
{
    if (!m_layer || !m_pane) return;
    m_layer->showLayer(m_pane, visible);
}

bool
LyricsTrack::isVisible() const
{
    return m_layer && m_pane && !m_layer->isLayerDormant(m_pane);
}

void
LyricsTrack::keepUnderTop()
{
    if (!m_layer || !m_pane) return;
    int count = m_pane->getLayerCount();
    if (count > 1 && m_pane->getTopLayer() == m_layer) {
        TakeLayers::raise(m_pane, m_pane->getLayer(count - 2));
    }
}

void
LyricsTrack::layerAboutToBeDeleted(Layer *layer)
{
    // Someone else's doing, the document being closed for instance
    if (layer && layer == m_layer) {
        m_layer = nullptr;
        hide();
        return;
    }

    // Another layer going can leave the lyrics on top of the pane: the
    // alternate pitch track turned off, a take deleted.  It is still in
    // the pane now, so this looks again once it has gone.  A layer and a
    // pane that have gone by then are not ours to look at
    QPointer<Pane> pane(m_pane);
    QTimer::singleShot(0, this, [this, pane]() {
        if (pane && pane == m_pane) keepUnderTop();
    });
}

ModelId
LyricsTrack::getModelId() const
{
    return m_layer ? m_layer->getModel() : ModelId();
}
