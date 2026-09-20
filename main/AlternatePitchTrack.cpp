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

#include "AlternatePitchTrack.h"

#include "framework/Document.h"
#include "view/Pane.h"
#include "layer/TimeValueLayer.h"
#include "layer/LayerFactory.h"
#include "layer/ColourDatabase.h"
#include "data/model/SparseTimeValueModel.h"
#include "base/PlayParameters.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace sv;

using std::cerr;
using std::endl;

// The layer's object name is this followed by the number of octaves
static const char *layerNamePrefix = "Alternate Pitch Track ";

// Used until the reference has a pitch model to take them from: the
// step size Analyser::addAnalyses() gives pYIN, and the usual rate
static const int defaultResolution = 256;
static const sv_samplerate_t defaultSampleRate = 44100;

AlternatePitchTrack::AlternatePitchTrack(QObject *parent) :
    QObject(parent),
    m_document(nullptr),
    m_pane(nullptr),
    m_layer(nullptr),
    m_octaves(-1),
    m_followed(false)
{
    // An edit to the reference, or pYIN filling it in, is a burst of
    // changes; copy once when it is over
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(50);
    connect(&m_rebuildTimer, &QTimer::timeout,
            this, &AlternatePitchTrack::rebuildNow);
}

AlternatePitchTrack::~AlternatePitchTrack()
{
}

double
AlternatePitchTrack::shifted(double hz, int octaves)
{
    return std::ldexp(hz, octaves);
}

QString
AlternatePitchTrack::layerNameFor(int octaves)
{
    return QString("%1%2").arg(layerNamePrefix).arg(octaves);
}

bool
AlternatePitchTrack::octavesFromLayerName(QString name, int &octaves)
{
    if (!name.startsWith(layerNamePrefix)) return false;
    bool ok = false;
    int n = name.mid(int(qstrlen(layerNamePrefix))).toInt(&ok);
    if (!ok || n == 0 || n < minOctaves || n > maxOctaves) return false;
    octaves = n;
    return true;
}

QString
AlternatePitchTrack::describe(int octaves)
{
    // Not tr("%n octave(s)"): that needs a translation to be loaded
    // even for English
    int n = std::abs(octaves);
    QString distance = (n == 1 ? tr("1 octave") : tr("%1 octaves").arg(n));
    return (octaves > 0 ? tr("%1 above the reference") :
            tr("%1 below the reference")).arg(distance);
}

bool
AlternatePitchTrack::show(Document *document, Pane *pane)
{
    if (m_layer) return true;
    if (!document || !pane) return false;

    auto model = std::make_shared<SparseTimeValueModel>
        (defaultSampleRate, defaultResolution, false);
    model->setObjectName(tr("Alternate Pitch Track"));
    model->setScaleUnits("Hz");
    ModelId modelId = ModelById::add(model);
    document->addNonDerivedModel(modelId);

    // Not createEmptyLayer(): see MainWindow::setupRealtimePitchLayer()
    auto layer = qobject_cast<TimeValueLayer *>
        (document->createLayer(LayerFactory::TimeValues));
    if (!layer) {
        cerr << "AlternatePitchTrack::show: failed to create layer" << endl;
        ModelById::release(modelId);
        return false;
    }

    document->setModel(layer, modelId);
    takeLayer(document, pane, layer);

    // Not addLayerToView(): this layer is Tony's own furniture, and hide()
    // takes it away with deleteLayer(force), which leaves an AddLayerCommand
    // holding a deleted layer.  Undo is for what the user did
    document->attachLayerToView(pane, layer);

    return true;
}

bool
AlternatePitchTrack::adopt(Document *document, Pane *pane)
{
    if (m_layer) return true;
    if (!document || !pane) return false;

    for (int i = 0; i < pane->getLayerCount(); ++i) {
        auto layer = qobject_cast<TimeValueLayer *>(pane->getLayer(i));
        int octaves = 0;
        if (!layer || !octavesFromLayerName(layer->objectName(), octaves)) {
            continue;
        }
        if (!ModelById::isa<SparseTimeValueModel>(layer->getModel())) {
            continue;
        }
        m_octaves = octaves;
        takeLayer(document, pane, layer);
        return true;
    }

    return false;
}

void
AlternatePitchTrack::takeLayer(Document *document, Pane *pane,
                               TimeValueLayer *layer)
{
    m_document = document;
    m_pane = pane;
    m_layer = layer;

    connect(m_document, &Document::layerAboutToBeDeleted,
            this, &AlternatePitchTrack::layerAboutToBeDeleted,
            Qt::UniqueConnection);

    configureLayer();
}

void
AlternatePitchTrack::configureLayer()
{
    if (!m_layer) return;

    m_layer->setObjectName(layerNameFor(m_octaves));
    m_layer->setPresentationName(tr("Alternate Pitch Track"));
    m_layer->setVerticalScale(TimeValueLayer::AutoAlignScale);
    m_layer->setPlotStyle(TimeValueLayer::PlotPoints);

    // There are two pitch tracks to be heard already
    if (auto params = m_layer->getPlayParameters()) {
        params->setPlayAudible(false);
    }

    applyColour();
}

void
AlternatePitchTrack::applyColour()
{
    if (!m_layer) return;

    // Close to the black of the reference, but not to be taken for
    // it; and the same washed out, for when nobody is following it
    ColourDatabase *cdb = ColourDatabase::getInstance();
    int index = m_followed ?
        cdb->addColour(QColor(74, 37, 17), tr("Dark Brown")) :
        cdb->addColour(QColor(164, 146, 136), tr("Faded Brown"));
    m_layer->setBaseColour(index);
}

void
AlternatePitchTrack::hide()
{
    m_rebuildTimer.stop();

    if (!m_source.isNone()) {
        if (auto source = ModelById::get(m_source)) {
            disconnect(source.get(), nullptr, this, nullptr);
        }
        m_source = {};
    }

    TimeValueLayer *layer = m_layer;
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
AlternatePitchTrack::layerAboutToBeDeleted(Layer *layer)
{
    // Someone else's doing, the document being closed for instance
    if (layer && layer == m_layer) {
        m_layer = nullptr;
        hide();
    }
}

void
AlternatePitchTrack::setSource(ModelId referencePitchModel)
{
    if (!ModelById::isa<SparseTimeValueModel>(referencePitchModel)) {
        referencePitchModel = {};
    }
    if (referencePitchModel == m_source) return;

    if (auto previous = ModelById::get(m_source)) {
        disconnect(previous.get(), nullptr, this, nullptr);
    }

    m_source = referencePitchModel;

    // pYIN writes to the model from its own thread: these are queued
    if (auto source = ModelById::get(m_source)) {
        connect(source.get(), &Model::modelChanged,
                this, &AlternatePitchTrack::sourceChanged);
        connect(source.get(), &Model::modelChangedWithin,
                this, &AlternatePitchTrack::sourceChanged);
    }

    rebuildNow();
}

void
AlternatePitchTrack::sourceChanged()
{
    if (!m_rebuildTimer.isActive()) m_rebuildTimer.start();
}

void
AlternatePitchTrack::setOctaves(int octaves)
{
    if (octaves == 0 || octaves < minOctaves || octaves > maxOctaves) return;
    if (octaves == m_octaves) return;
    m_octaves = octaves;
    if (m_layer) {
        m_layer->setObjectName(layerNameFor(m_octaves));
        rebuildNow();
    }
}

bool
AlternatePitchTrack::canStep(bool up) const
{
    return up ? (m_octaves < maxOctaves) : (m_octaves > minOctaves);
}

void
AlternatePitchTrack::step(bool up)
{
    if (!canStep(up)) return;
    int octaves = m_octaves + (up ? 1 : -1);
    if (octaves == 0) octaves += (up ? 1 : -1);
    setOctaves(octaves);
}

void
AlternatePitchTrack::setFollowed(bool followed)
{
    if (m_followed == followed) return;
    m_followed = followed;
    applyColour();
}

void
AlternatePitchTrack::rebuildNow()
{
    m_rebuildTimer.stop();

    if (!m_layer || !m_document) return;

    auto source = ModelById::getAs<SparseTimeValueModel>(m_source);
    auto model = ModelById::getAs<SparseTimeValueModel>(m_layer->getModel());

    // Points are drawn as wide as the resolution says, so ours must be
    // that of the source.  It was a guess if the layer was made before
    // the reference had a pitch track
    if (source && (!model ||
                   model->getSampleRate() != source->getSampleRate() ||
                   model->getResolution() != source->getResolution())) {
        model = std::make_shared<SparseTimeValueModel>
            (source->getSampleRate(), source->getResolution(), false);
        model->setObjectName(tr("Alternate Pitch Track"));
        model->setScaleUnits("Hz");
        ModelId modelId = ModelById::add(model);
        m_document->addNonDerivedModel(modelId);
        // Releases the model it replaces, and copies the play
        // parameters (that is, the muting) across
        m_document->setModel(m_layer, modelId);
    }

    if (!model) return;

    for (const Event &e : model->getAllEvents()) model->remove(e);

    if (!source) return;

    for (const Event &e : source->getAllEvents()) {
        model->add(e.withValue(float(shifted(e.getValue(), m_octaves))));
    }
}
