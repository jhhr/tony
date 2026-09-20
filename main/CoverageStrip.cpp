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

#include "CoverageStrip.h"

#include "TakeLayers.h"

#include "framework/Document.h"
#include "view/Pane.h"
#include "layer/RegionLayer.h"
#include "layer/LayerFactory.h"
#include "layer/ColourDatabase.h"
#include "data/model/RegionModel.h"
#include "base/PlayParameters.h"

#include <iostream>

using namespace sv;

using std::cerr;
using std::endl;

// Coverage is in frames of the reference's timeline, so the regions are
// exact: a resolution of more than one frame would round the end of the
// last range up
static const int coverageResolution = 1;

// Used only if the session has no main model to take the rate from,
// which cannot happen while there is a take to show coverage of
static const sv_samplerate_t defaultSampleRate = 44100;

CoverageStrip::CoverageStrip(QObject *parent) :
    QObject(parent),
    m_document(nullptr),
    m_pane(nullptr),
    m_layer(nullptr)
{
}

CoverageStrip::~CoverageStrip()
{
}

QString
CoverageStrip::layerName(QString takeName)
{
    // Not translated: it is stored in the session file and adopt() looks
    // it up
    return TakeLayers::nameFor(takeName, TakeLayers::Coverage);
}

bool
CoverageStrip::show(Document *document, Pane *pane, QString takeName)
{
    if (m_layer) return true;
    if (!document || !pane || takeName == "") return false;

    sv_samplerate_t rate = defaultSampleRate;
    if (auto main = ModelById::get(document->getMainModel())) {
        rate = main->getSampleRate();
    }

    auto model = std::make_shared<RegionModel>(rate, coverageResolution);
    model->setObjectName(tr("Singing Coverage"));
    ModelId modelId = ModelById::add(model);
    document->addNonDerivedModel(modelId);

    // Not createEmptyLayer(): see MainWindow::setupRealtimePitchLayer()
    auto layer = qobject_cast<RegionLayer *>
        (document->createLayer(LayerFactory::Regions));
    if (!layer) {
        cerr << "CoverageStrip::show: failed to create layer" << endl;
        ModelById::release(modelId);
        return false;
    }

    document->setModel(layer, modelId);
    takeLayer(document, pane, layer, takeName);

    // Not addLayerToView(): the strip is part of the take, not something
    // the user added, so undo must not take it away and making it does
    // not count as a change to the session
    document->attachLayerToView(pane, layer);

    return true;
}

bool
CoverageStrip::adopt(Document *document, Pane *pane, QString takeName)
{
    if (m_layer) return true;
    if (!document || !pane || takeName == "") return false;

    for (int i = 0; i < pane->getLayerCount(); ++i) {
        auto layer = qobject_cast<RegionLayer *>(pane->getLayer(i));
        if (!layer || layer->objectName() != layerName(takeName)) continue;
        if (!ModelById::isa<RegionModel>(layer->getModel())) continue;
        takeLayer(document, pane, layer, takeName);
        return true;
    }

    return false;
}

void
CoverageStrip::takeLayer(Document *document, Pane *pane, RegionLayer *layer,
                         QString takeName)
{
    m_document = document;
    m_pane = pane;
    m_layer = layer;
    m_takeName = takeName;

    connect(m_document, &Document::layerAboutToBeDeleted,
            this, &CoverageStrip::layerAboutToBeDeleted,
            Qt::UniqueConnection);

    configureLayer();
}

void
CoverageStrip::configureLayer()
{
    if (!m_layer) return;

    m_layer->setObjectName(layerName(m_takeName));
    m_layer->setPresentationName(tr("%1 Coverage").arg(m_takeName));

    // The strip of a take that is not the active one is hidden, and this
    // one is the active take's
    m_layer->setLayerDormant(m_pane, false);

    // A band along the bottom of the pane, filled where there is
    // singing: a plot style the svgui fork has for this.  It has no
    // vertical scale, no labels and takes no edits.  EqualSpaced as well,
    // so that nothing in the pane can align its scale to this layer
    m_layer->setVerticalScale(RegionLayer::EqualSpaced);
    m_layer->setPlotStyle(RegionLayer::PlotStrip);

    m_layer->setBaseColour
        (ColourDatabase::getInstance()->getColourIndex(tr("Orange")));

    // A RegionModel cannot play, so there are none; if that ever
    // changes, the strip is still not something to hear
    if (auto params = m_layer->getPlayParameters()) {
        params->setPlayAudible(false);
    }
}

void
CoverageStrip::hide()
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
    m_takeName = "";
}

void
CoverageStrip::release()
{
    // The layer stays where it is, hidden: it is the stored coverage of a
    // take that is still in the session, only not the active one
    if (m_layer && m_pane) {
        m_layer->setLayerDormant(m_pane, true);
    }

    m_layer = nullptr;

    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = nullptr;
    m_pane = nullptr;
    m_takeName = "";
}

void
CoverageStrip::layerAboutToBeDeleted(Layer *layer)
{
    // Someone else's doing, the document being closed for instance
    if (layer && layer == m_layer) {
        m_layer = nullptr;
        hide();
    }
}

void
CoverageStrip::setCoverage(const Coverage &coverage)
{
    if (!m_layer) return;

    auto model = ModelById::getAs<RegionModel>(m_layer->getModel());
    if (!model) return;

    EventVector existing = model->getAllEvents();
    if (Coverage::fromEvents(existing) == coverage) return;

    for (const Event &e : existing) model->remove(e);
    for (const Event &e : coverage.toEvents()) model->add(e);
}

ModelId
CoverageStrip::getModelId() const
{
    return m_layer ? m_layer->getModel() : ModelId();
}

Coverage
CoverageStrip::getCoverage() const
{
    if (!m_layer) return {};
    auto model = ModelById::getAs<RegionModel>(m_layer->getModel());
    if (!model) return {};
    return Coverage::fromEvents(model->getAllEvents());
}
