/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Tony
    An intonation analysis and annotation tool
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006-2012 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "Analyser.h"

#include "transform/TransformFactory.h"
#include "transform/ModelTransformer.h"
#include "transform/ModelTransformerFactory.h"
#include "transform/FeatureExtractionModelTransformer.h"
#include "framework/Document.h"
#include "data/model/WaveFileModel.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "layer/Layer.h"
#include "layer/TimeValueLayer.h"
#include "layer/NoteLayer.h"
#include "layer/FlexiNoteLayer.h"
#include "layer/WaveformLayer.h"
#include "layer/ColourDatabase.h"
#include "layer/ColourMapper.h"
#include "layer/LayerFactory.h"
#include "layer/SpectrogramLayer.h"
#include "layer/Colour3DPlotLayer.h"
#include "layer/ShowLayerCommand.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"

#include <QSettings>
#include <QMutexLocker>

#include <algorithm>

using std::vector;
using std::cerr;
using std::endl;

using namespace sv;

// The two pYIN outputs of a full analysis, and the step size they are
// run at: the grid every result of ours sits on
static const QString pyinPlugin = "pYIN";
static const QString pyinBase = "vamp:pyin:pyin:";
static const QString pyinPitchOutput = "smoothedpitchtrack";
static const QString pyinNotesOutput = "notes";
static const int analysisStepSize = 256;

Analyser::Analyser(ColorScheme colorScheme) :
    m_colorScheme(colorScheme),
    m_document(0),
    m_paneStack(0),
    m_pane(0),
    m_currentCandidate(-1),
    m_candidatesVisible(false),
    m_currentAsyncHandle(0),
    m_rangedStart(0),
    m_rangedEnd(0),
    m_rangedMergeStart(0),
    m_rangedMergeEnd(0),
    m_rangedClippedEnd(false)
{
    QSettings settings;
    settings.beginGroup("LayerDefaults");
    settings.setValue
        ("timevalues",
         QString("<layer verticalScale=\"%1\" plotStyle=\"%2\" "
                 "scaleMinimum=\"%3\" scaleMaximum=\"%4\"/>")
         .arg(int(TimeValueLayer::AutoAlignScale))
         .arg(int(TimeValueLayer::PlotPoints))
         .arg(27.5f).arg(880.f)); // temporary values: better get the real extents of the data from the model
    settings.setValue
        ("flexinotes",
         QString("<layer verticalScale=\"%1\"/>")
         .arg(int(FlexiNoteLayer::AutoAlignScale)));
    settings.endGroup();
}

Analyser::~Analyser()
{
    // A ranged analysis still running would go on writing into models
    // the document is about to release, and its temporary layers would
    // be left in the document with nobody left who knows what they are
    discardRangedAnalysis();
}

std::map<QString, QVariant>
Analyser::getAnalysisSettings()
{
    return { { "precision-analysis", false },
             { "lowamp-analysis", true },
             { "onset-analysis", true },
             { "prune-analysis", true }
    };
}

QString
Analyser::newFileLoaded(Document *doc, ModelId model,
			PaneStack *paneStack, Pane *pane,
			bool deferAnalysis)
{
    m_document = doc;
    m_fileModel = model;
    m_paneStack = paneStack;
    m_pane = pane;

    if (!ModelById::isa<WaveFileModel>(m_fileModel)) {
        return "Internal error: Analyser::newFileLoaded() called with no model, or a non-WaveFileModel";
    }
    
    // By member pointer: this class is outside namespace sv, so moc
    // records the slot as taking sv::Layer *, which a SLOT() string
    // saying Layer * never matches.  Unique, because this is called
    // again with the same document for every take
    connect(doc, &Document::layerAboutToBeDeleted,
            this, &Analyser::layerAboutToBeDeleted,
            Qt::UniqueConnection);

    QSettings settings;
    settings.beginGroup("Analyser");
    bool autoAnalyse = settings.value("auto-analysis", true).toBool();
    settings.endGroup();

    return doAllAnalyses(autoAnalyse && !deferAnalysis);
}

QString
Analyser::analyseExistingFile()
{
    if (!m_document) return "Internal error: Analyser::analyseExistingFile() called with no document present";

    if (!m_pane) return "Internal error: Analyser::analyseExistingFile() called with no pane present";

    if (m_fileModel.isNone()) return "Internal error: Analyser::analyseExistingFile() called with no model present";

    // The layers removed below are kept alive by the undo history, so
    // an analysis still running on them would carry on unseen
    cancelAnalyses();

    if (m_layers[PitchTrack]) {
        m_document->removeLayerFromView(m_pane, m_layers[PitchTrack]);
        m_layers[PitchTrack] = 0;
    }
    if (m_layers[Notes]) {
        m_document->removeLayerFromView(m_pane, m_layers[Notes]);
        m_layers[Notes] = 0;
    }

    return doAllAnalyses(true);
}

QString
Analyser::doAllAnalyses(bool withPitchTrack)
{
    m_reAnalysingSelection = Selection();
    m_reAnalysisCandidates.clear();
    m_currentCandidate = -1;
    m_candidatesVisible = false;

    // Note that we need at least one main-model layer (time ruler,
    // waveform or what have you). It could be hidden if we don't want
    // to see it but it must exist.

    QString warning, error;

    cerr << "Analyser::newFileLoaded: about to check visualisations etc" << endl;

    // This isn't fatal -- we can proceed without
    // visualisations. Other failures are fatal though.
    warning = addVisualisations();

    error = addWaveform();
    if (error != "") return error;

    if (withPitchTrack) {
        error = addAnalyses();
        if (error != "") return error;
    } else {
        // No analysis to run, but pitch and notes layers may be there for
        // us all the same: a session restore with auto-analysis switched
        // off, or another audio file swapped under the layers of a take
        (void)claimExistingAnalyses(false);
    }

    loadState(Audio);
    loadState(PitchTrack);
    loadState(Notes);
    loadState(Spectrogram);

    silenceSecondaryAnalysisLayers();

    stackLayers();

    emit layersChanged();

    return warning;
}

void
Analyser::cancelAnalyses()
{
    std::vector<Layer *> derived(m_reAnalysisCandidates.begin(),
                                 m_reAnalysisCandidates.end());
    for (Component c : { PitchTrack, Notes }) {
        auto it = m_layers.find(c);
        if (it != m_layers.end() && it->second) derived.push_back(it->second);
    }

    auto mtf = ModelTransformerFactory::getInstance();
    for (Layer *layer : derived) {
        ModelId modelId = layer->getModel();
        // cancel() returns once the transform thread has exited; it
        // does nothing if the transform has already finished
        if (!modelId.isNone()) mtf->cancel(modelId);
    }

    // A ranged analysis is cancelled the same way, and there is then
    // nothing worth merging from it, so its temporary layers go too
    discardRangedAnalysis();
}

void
Analyser::fileClosed()
{
    cerr << "Analyser::fileClosed" << endl;
    cancelAnalyses();
    m_layers.clear();
    m_reAnalysisCandidates.clear();
    m_currentCandidate = -1;
    m_reAnalysingSelection = Selection();
}

void
Analyser::removeAllLayers()
{
    cerr << "Analyser::removeAllLayers" << endl;

    // Before any model is released: see cancelAnalyses()
    cancelAnalyses();

    // First discard any re-analysis candidate layers (these are not in
    // m_layers, but they are registered with the document).
    discardPitchCandidates();

    // Remove and delete each layer this analyser owns, in reverse stacking
    // order (Notes on top, then PitchTrack, Spectrogram, Audio at bottom).
    // We iterate over a fixed order rather than the map itself because
    // deleteLayer() can trigger layerAboutToBeDeleted() which modifies m_layers.
    static const Component order[] = { Notes, Spectrogram, PitchTrack, Audio };

    for (Component c : order) {
        auto it = m_layers.find(c);
        if (it == m_layers.end() || !it->second) continue;

        Layer *layer = it->second;
        it->second = nullptr; // clear before deleteLayer fires the slot

        if (m_document) {
            // Use deleteLayer(force=true) directly — do NOT call
            // removeLayerFromView first.
            //
            // removeLayerFromView creates a RemoveLayerCommand in the undo
            // history with m_added=false.  If deleteLayer then destroys the
            // layer object, that command holds a dangling pointer.  When
            // CommandHistory is later cleared (e.g. on closeSession) the
            // RemoveLayerCommand destructor checks !m_added and calls
            // m_d->deleteLayer(m_layer) on the already-deleted layer —
            // use-after-free / crash, and the old model stays alive in the
            // undo entry, causing its orange dots to reappear.
            //
            // deleteLayer(force=true) removes the layer from all views
            // internally (without generating any undo command), then
            // releases the model if unreferenced and deletes the layer.
            // This is the correct path for a silent, non-undoable replace.
            m_document->deleteLayer(layer, true);
        }
    }

    // fileClosed() clears the rest of the state (candidates, selection, etc.)
    fileClosed();
}

void
Analyser::releaseLayers()
{
    cerr << "Analyser::releaseLayers" << endl;

    // Before any model is released: see cancelAnalyses().  This also has
    // to happen before the pitch and notes models are given another
    // source model, which is what the caller does next
    cancelAnalyses();

    // The candidates are of the audio that is going
    discardPitchCandidates();

    // Only the waveform goes.  Deleting it releases the audio model
    // behind it, as nothing else holds a layer on it; the file stays on
    // disk.  deleteLayer(force=true) and not removeLayerFromView, for the
    // reasons set out in removeAllLayers()
    if (Layer *audio = m_layers[Audio]) {
        m_layers[Audio] = nullptr; // before deleteLayer fires the slot
        if (m_document) m_document->deleteLayer(audio, true);
    }

    // The pitch and notes layers are simply forgotten: they stay in the
    // pane with their events, and fileClosed() clears the rest of the
    // state.  From here this analyser owns nothing, so deleting it -- or
    // deleting a layer it used to own -- disturbs nobody
    fileClosed();
}

bool
Analyser::getDisplayFrequencyExtents(double &min, double &max)
{
    if (!m_layers[Spectrogram]) return false;
    return m_layers[Spectrogram]->getDisplayExtents(min, max);
}

bool
Analyser::setDisplayFrequencyExtents(double min, double max)
{
    if (!m_layers[Spectrogram]) return false;
    m_layers[Spectrogram]->setDisplayExtents(min, max);
    return true;
}

int
Analyser::getInitialAnalysisCompletion()
{
    int completion = 0;

    if (m_layers[PitchTrack]) {
        completion = m_layers[PitchTrack]->getCompletion(m_pane);
    }

    if (m_layers[Notes]) {
        int c = m_layers[Notes]->getCompletion(m_pane);
        if (c < completion) completion = c;
    }
    
    return completion;
}

void
Analyser::layerCompletionChanged(ModelId)
{
    if (getInitialAnalysisCompletion() < 100) {
        return;
    }

    emit initialAnalysisCompleted();

    if (!m_layers[Audio]) {
        return;
    }

    // Extend pitch-track and note layers so as to nominally end at
    // the same time as the audio. This affects any time-filling done
    // on export etc.

    auto audioModel = ModelById::get(m_layers[Audio]->getModel());
    sv_frame_t endFrame = audioModel->getEndFrame();
        
    if (m_layers[PitchTrack]) {
        auto model = ModelById::getAs<SparseTimeValueModel>
            (m_layers[PitchTrack]->getModel());
        if (model) {
            model->extendEndFrame(endFrame);
        }
    }

    if (m_layers[Notes]) {
        auto model = ModelById::getAs<NoteModel>
            (m_layers[Notes]->getModel());
        if (model) {
            model->extendEndFrame(endFrame);
        }
    }
}

QString
Analyser::addVisualisations()
{
    if (m_fileModel.isNone()) return "Internal error: Analyser::addVisualisations() called with no model present";

    // The secondary (singing/recording) analyser shares the primary pane.
    // A spectrogram for the singing track would be visually confusing and
    // would incorrectly steal the primary analyser's spectrogram layer.
    // Simply skip the spectrogram for the secondary colour scheme.
    if (m_colorScheme == SecondaryColors) {
        m_layers[Spectrogram] = nullptr;
        return "";
    }

    // A spectrogram, off by default. Must go at the back because it's
    // opaque

/* This is roughly what we'd do for a constant-Q spectrogram, but it
   currently has issues with y-axis alignment
  
    TransformFactory *tf = TransformFactory::getInstance();

    QString name = "Constant-Q";
    QString base = "vamp:cqvamp:cqvamp:";
    QString out = "constantq";

    QString notFound = tr("Transform \"%1\" not found, spectrogram will not be enabled.<br><br>Is the %2 Vamp plugin correctly installed?");
    if (!tf->haveTransform(base + out)) {
	return notFound.arg(base + out).arg(name);
    }

    Transform transform = tf->getDefaultTransformFor
        (base + out, m_fileModel->getSampleRate());
    transform.setParameter("bpo", 36);

    Colour3DPlotLayer *spectrogram = qobject_cast<Colour3DPlotLayer *>
        (m_document->createDerivedLayer(transform, m_fileModel));

    if (!spectrogram) return tr("Transform \"%1\" did not run correctly (no layer or wrong layer type returned)").arg(base + out);
*/    

    // As with all the visualisation layers, if we already have one in
    // the pane we do not create another, just record its
    // existence. (We create a new one when loading a new audio file,
    // but just note the existing one when loading a complete session.)

    for (int i = 0; i < m_pane->getLayerCount(); ++i) {
        SpectrogramLayer *existing = qobject_cast<SpectrogramLayer *>
            (m_pane->getLayer(i));
        if (existing) {
            // Only claim this spectrogram if it belongs to the main model
            // (i.e. it is a main-model layer derived from our file model).
            // In dual-analyser mode the pane is shared, so we must not
            // steal a spectrogram that was created by the other analyser.
            ModelId existingModel = existing->getModel();
            if (existingModel == m_fileModel ||
                existingModel == m_document->getMainModel()) {
                cerr << "recording existing spectrogram layer (matching main model)" << endl;
                m_layers[Spectrogram] = existing;
                return "";
            }
        }
    }

    SpectrogramLayer *spectrogram = qobject_cast<SpectrogramLayer *>
        (m_document->createMainModelLayer(LayerFactory::MelodicRangeSpectrogram));

    spectrogram->setColourMap((int)ColourMapper::BlackOnWhite);
    spectrogram->setNormalization(ColumnNormalization::Hybrid);
    // This magical scale factor happens to get us a similar display
    // to Tony v1.0
    spectrogram->setGain(0.25f);
    // attachLayerToView() and not addLayerToView() throughout this class:
    // an analyser's layers are Tony's own furniture, made and taken away
    // by the analyser itself (removeAllLayers() and releaseLayers() use
    // deleteLayer(force), which leaves an AddLayerCommand holding a
    // deleted layer).  The undo history is for what the user did -- and
    // after a take its top entry must be the take
    m_document->attachLayerToView(m_pane, spectrogram);
    spectrogram->setLayerDormant(m_pane, true);

    m_layers[Spectrogram] = spectrogram;

    return "";
}

QString
Analyser::addWaveform()
{
    // Our waveform layer is just a shadow, light grey and taking up
    // little space at the bottom.

    // As with the spectrogram above, if one exists already we just
    // use it -- but only if it's associated with our file model.
    // In dual-track mode, the pane may already have a waveform layer
    // belonging to the primary analyser; we must not steal it.
    for (int i = 0; i < m_pane->getLayerCount(); ++i) {
        WaveformLayer *existing = qobject_cast<WaveformLayer *>
            (m_pane->getLayer(i));
        if (existing && existing->getModel() == m_fileModel) {
            cerr << "recording existing waveform layer (matching our file model)" << endl;
            m_layers[Audio] = existing;
            return "";
        }
    }

    WaveformLayer *waveform = nullptr;

    if (m_colorScheme == SecondaryColors) {
        // The secondary analyser's file model is NOT the document main model,
        // so createMainModelLayer would show the wrong (reference) audio.
        // Instead create a layer directly and associate it with our model.
        // The model must already have been registered with the document via
        // addNonDerivedModel (done in MainWindow::setupSingingTrackAnalyser).
        Layer *raw = m_document->createLayer(LayerFactory::Waveform);
        waveform = qobject_cast<WaveformLayer *>(raw);
        if (waveform) {
            m_document->setModel(waveform, m_fileModel);
        }
    } else {
        waveform = qobject_cast<WaveformLayer *>
            (m_document->createMainModelLayer(LayerFactory::Waveform));
    }

    if (!waveform) {
        return tr("Internal error: could not create waveform layer");
    }

    waveform->setMiddleLineHeight(0.9);
    waveform->setShowMeans(false); // too small & pale for this
    waveform->setBaseColour
        (ColourDatabase::getInstance()->getColourIndex(tr("Grey")));
    auto params = waveform->getPlayParameters();
    if (params) {
        params->setPlayPan(-1);
        params->setPlayGain(1);
    }
    
    m_document->attachLayerToView(m_pane, waveform);

    m_layers[Audio] = waveform;
    return "";
}

QString
Analyser::buildAnalysisTransforms(Transforms &transforms)
{
    auto waveFileModel = ModelById::getAs<WaveFileModel>(m_fileModel);
    if (!waveFileModel) {
        return "Internal error: Analyser::buildAnalysisTransforms() called with no model present";
    }

    TransformFactory *tf = TransformFactory::getInstance();

    QString plugname = pyinPlugin;
    QString base = pyinBase;
    QString f0out = pyinPitchOutput;
    QString noteout = pyinNotesOutput;

    QString notFound = tr("Transform \"%1\" not found. Unable to analyse audio file.<br><br>Is the %2 Vamp plugin correctly installed?");
    if (!tf->haveTransform(base + f0out)) {
	return notFound.arg(base + f0out).arg(plugname);
    }
    if (!tf->haveTransform(base + noteout)) {
	return notFound.arg(base + noteout).arg(plugname);
    }

    QSettings settings;
    settings.beginGroup("Analyser");

    bool precise = false, lowamp = true, onset = true, prune = true;

    std::map<QString, bool &> flags {
        { "precision-analysis", precise },
        { "lowamp-analysis", lowamp },
        { "onset-analysis", onset },
        { "prune-analysis", prune }
    };

    auto keyMap = getAnalysisSettings();

    for (auto p: flags) {
        auto ki = keyMap.find(p.first);
        if (ki != keyMap.end()) {
            p.second = settings.value(ki->first, ki->second).toBool();
        } else {
            throw std::logic_error("Internal error: One or more analysis settings keys not found in map: check buildAnalysisTransforms and getAnalysisSettings");
        }
    }

    settings.endGroup();

    Transform t = tf->getDefaultTransformFor
        (base + f0out, waveFileModel->getSampleRate());
    t.setStepSize(analysisStepSize);
    t.setBlockSize(2048);

    if (precise) {
        cerr << "setting parameters for precise mode" << endl;
        t.setParameter("precisetime", 1);
    } else {
        cerr << "setting parameters for vague mode" << endl;
        t.setParameter("precisetime", 0);
    }

    if (lowamp) {
        cerr << "setting parameters for lowamp suppression" << endl;
        t.setParameter("lowampsuppression", 0.2f);
    } else {
        cerr << "setting parameters for no lowamp suppression" << endl;
        t.setParameter("lowampsuppression", 0.0f);
    }

    if (onset) {
        cerr << "setting parameters for increased onset sensitivity" << endl;
        t.setParameter("onsetsensitivity", 0.7f);
    } else {
        cerr << "setting parameters for non-increased onset sensitivity" << endl;
        t.setParameter("onsetsensitivity", 0.0f);
    }

    if (prune) {
        cerr << "setting parameters for duration pruning" << endl;
        t.setParameter("prunethresh", 0.1f);
    } else {
        cerr << "setting parameters for no duration pruning" << endl;
        t.setParameter("prunethresh", 0.0f);
    }

    transforms.push_back(t);

    t.setOutput(noteout);

    transforms.push_back(t);

    return "";
}

QString
Analyser::addAnalyses()
{
    auto waveFileModel = ModelById::getAs<WaveFileModel>(m_fileModel);
    if (!waveFileModel) {
        return "Internal error: Analyser::addAnalyses() called with no model present";
    }
    
    // As with the spectrogram above, if these layers exist we use them
    // rather than making another pair
    if (claimExistingAnalyses(true)) return "";

    Transforms transforms;
    QString error = buildAnalysisTransforms(transforms);
    if (error != "") return error;

/*!!! we could have more than one pitch track...
    QString cx = "vamp:cepstral-pitchtracker:cepstral-pitchtracker:f0";
    if (tf->haveTransform(cx)) {
        Transform tx = tf->getDefaultTransformFor(cx);
        TimeValueLayer *lx = qobject_cast<TimeValueLayer *>
            (m_document->createDerivedLayer(tx, m_fileModel));
        lx->setVerticalScale(TimeValueLayer::AutoAlignScale);
        lx->setBaseColour(ColourDatabase::getInstance()->getColourIndex(tr("Bright Red")));
        m_document->addLayerToView(m_pane, lx);
    }
*/

    std::vector<Layer *> layers =
        m_document->createDerivedLayers(transforms, m_fileModel);

    for (int i = 0; i < (int)layers.size(); ++i) {

        FlexiNoteLayer *f = qobject_cast<FlexiNoteLayer *>(layers[i]);
        TimeValueLayer *t = qobject_cast<TimeValueLayer *>(layers[i]);
        
        if (f) m_layers[Notes] = f;
        if (t) m_layers[PitchTrack] = t;
        
        m_document->attachLayerToView(m_pane, layers[i]);
    }

    configureAnalysisLayers();
    connectAnalysisLayers();

    return "";
}

QString
Analyser::addEmptyAnalyses()
{
    // The first recording of a take has no analysis to keep and none to
    // run: what it sings is analysed over its own range and merged into
    // the pitch track and notes of the take (spec 6.2, last paragraph),
    // which therefore have to exist, empty, first.  They are made here
    // rather than in MainWindow so that they are the same layers on the
    // same kind of model as a whole-file analysis leaves behind -- type,
    // resolution, units, colours, names, play parameters and the source
    // model that lets an analyser claim them again after a swap or a
    // session restore.
    auto waveFileModel = ModelById::getAs<WaveFileModel>(m_fileModel);
    if (!waveFileModel) {
        return "Internal error: Analyser::addEmptyAnalyses() called with no model present";
    }
    if (!m_document || !m_pane) {
        return "Internal error: Analyser::addEmptyAnalyses() called with no document or pane present";
    }

    // Nothing to do for a take that has them: the usual case
    if (m_layers[PitchTrack] && m_layers[Notes]) return "";

    // Half a pair is partial state from a failed analysis, and no use
    // to the merge either
    for (Component c : { PitchTrack, Notes }) {
        if (m_layers[c]) {
            m_document->removeLayerFromView(m_pane, m_layers[c]);
            m_layers[c] = nullptr;
        }
    }

    sv_samplerate_t rate = waveFileModel->getSampleRate();

    // The names a transform's output models are given
    // (ModelTransformerFactory::transformMultiple): they are what the
    // layer's presentation name and the session file show
    TransformFactory *tf = TransformFactory::getInstance();
    QString sourceName = waveFileModel->objectName();

    struct Wanted {
        Component component;
        LayerFactory::LayerType layerType;
        QString output;
    };

    const Wanted wanted[] = {
        { PitchTrack, LayerFactory::TimeValues, pyinPitchOutput },
        { Notes, LayerFactory::FlexiNotes, pyinNotesOutput }
    };

    for (const Wanted &w : wanted) {

        // The layer first: a model registered with the document is not
        // ours to release again (see the ownership rules in the dev doc),
        // so nothing is registered until there is a layer to hold it.
        // Not createEmptyLayer(): that makes a model of its own, on the
        // main model's sample rate and with a resolution of 1
        Layer *layer = m_document->createLayer(w.layerType);
        if (!layer) {
            return "Internal error: Analyser::addEmptyAnalyses() could not create a layer";
        }

        // One value per hop of the pitch track, notes with a duration
        // at the same resolution, both in Hz, as pYIN's outputs are
        // described.  Notified on add, which is the state a transform's
        // model is switched to when it completes; these are complete
        // from the start
        std::shared_ptr<Model> model;
        if (w.component == PitchTrack) {
            auto pitch = std::make_shared<SparseTimeValueModel>
                (rate, analysisStepSize, true);
            pitch->setScaleUnits("Hz");
            model = pitch;
        } else {
            auto notes = std::make_shared<NoteModel>
                (rate, analysisStepSize, true, NoteModel::FLEXI_NOTE);
            notes->setScaleUnits("Hz");
            model = notes;
        }

        QString transformName =
            tf->getTransformFriendlyName(pyinBase + w.output);
        if (sourceName != "" && transformName != "") {
            model->setObjectName(tr("%1: %2").arg(sourceName,
                                                  transformName));
        } else if (transformName != "") {
            model->setObjectName(transformName);
        }

        // The link a whole-file analysis makes by deriving the model
        // from the audio: it is what claimExistingAnalyses() looks for
        model->setSourceModel(m_fileModel);

        ModelId modelId = ModelById::add(model);
        m_document->addNonDerivedModel(modelId);

        m_document->setModel(layer, modelId);
        m_document->attachLayerToView(m_pane, layer);
        m_layers[w.component] = layer;
    }

    configureAnalysisLayers();
    connectAnalysisLayers();

    // As doAllAnalyses() does for the layers it has just made
    loadState(PitchTrack);
    loadState(Notes);
    silenceSecondaryAnalysisLayers();
    stackLayers();

    emit layersChanged();

    return "";
}

void
Analyser::configureAnalysisLayers()
{
    ColourDatabase *cdb = ColourDatabase::getInstance();

    // Choose colors based on color scheme:
    // Primary (reference/target track): Black pitch, Bright Blue notes
    // Secondary (singer/recording track): Orange pitch, Bright Purple notes
    QString pitchColour = (m_colorScheme == SecondaryColors)
        ? tr("Orange") : tr("Black");
    QString notesColour = (m_colorScheme == SecondaryColors)
        ? tr("Bright Purple") : tr("Bright Blue");

    TimeValueLayer *pitchLayer =
        qobject_cast<TimeValueLayer *>(m_layers[PitchTrack]);
    if (pitchLayer) {
        pitchLayer->setBaseColour(cdb->getColourIndex(pitchColour));
        auto params = pitchLayer->getPlayParameters();
        if (params) {
            params->setPlayPan(1);
            params->setPlayGain(0.5);
        }
    }

    FlexiNoteLayer *flexiNoteLayer =
        qobject_cast<FlexiNoteLayer *>(m_layers[Notes]);
    if (flexiNoteLayer) {
        flexiNoteLayer->setBaseColour(cdb->getColourIndex(notesColour));
        auto params = flexiNoteLayer->getPlayParameters();
        if (params) {
            params->setPlayPan(1);
            params->setPlayGain(0.5);
        }
    }
}

void
Analyser::silenceSecondaryAnalysisLayers()
{
    // The secondary analyser's pitch and note tracks are visual-only (there is
    // no UI toggle to control their audibility, and sonifying two pitch/note
    // tracks at once is confusing).  Mute them directly — do NOT call
    // setAudible(), which would also call saveState() and corrupt the primary
    // analyser's shared settings key.
    if (m_colorScheme != SecondaryColors) return;

    for (Component c : { PitchTrack, Notes }) {
        if (m_layers[c]) {
            auto params = m_layers[c]->getPlayParameters();
            if (params) params->setPlayAudible(false);
        }
    }
}

bool
Analyser::claimExistingAnalyses(bool removeMismatched)
{
    // Accept a layer only if its source model is our file model (or the
    // layer is on our file model itself).  When two analysers share a
    // pane (dual-track mode), each must claim only its own layers, not
    // those of the other analyser.
    TimeValueLayer *existingPitch = 0;
    FlexiNoteLayer *existingNotes = 0;
    for (int i = 0; i < m_pane->getLayerCount(); ++i) {
        if (!existingPitch) {
            TimeValueLayer *tvl =
                qobject_cast<TimeValueLayer *>(m_pane->getLayer(i));
            if (tvl) {
                auto model = ModelById::get(tvl->getModel());
                if (model && (tvl->getModel() == m_fileModel ||
                              model->getSourceModel() == m_fileModel)) {
                    existingPitch = tvl;
                }
            }
        }
        if (!existingNotes) {
            FlexiNoteLayer *fnl =
                qobject_cast<FlexiNoteLayer *>(m_pane->getLayer(i));
            if (fnl) {
                auto model = ModelById::get(fnl->getModel());
                if (model && (fnl->getModel() == m_fileModel ||
                              model->getSourceModel() == m_fileModel)) {
                    existingNotes = fnl;
                }
            }
        }
    }

    if (existingPitch && existingNotes) {
        cerr << "recording existing pitch and notes layers (matching our file model)" << endl;
        m_layers[PitchTrack] = existingPitch;
        m_layers[Notes] = existingNotes;
        connectAnalysisLayers();
        return true;
    }

    if (removeMismatched) {
        // Half a pair is partial state from a previous failed analysis
        // run, and the caller is about to make the pair itself
        if (existingPitch) {
            m_document->removeLayerFromView(m_pane, existingPitch);
            m_layers[PitchTrack] = 0;
        }
        if (existingNotes) {
            m_document->removeLayerFromView(m_pane, existingNotes);
            m_layers[Notes] = 0;
        }
    }

    return false;
}

void
Analyser::connectAnalysisLayers()
{
    // Claimed layers need these as much as ones we made ourselves: the
    // analyser they belonged to before is gone, and with it its
    // connections.  Unique, because an analyser handed the same layers
    // twice would otherwise hear each signal twice
    if (auto pitchLayer = qobject_cast<TimeValueLayer *>(m_layers[PitchTrack])) {
        connect(pitchLayer, SIGNAL(modelCompletionChanged(ModelId)),
                this, SLOT(layerCompletionChanged(ModelId)),
                Qt::UniqueConnection);
    }

    if (auto noteLayer = qobject_cast<FlexiNoteLayer *>(m_layers[Notes])) {
        connect(noteLayer, SIGNAL(modelCompletionChanged(ModelId)),
                this, SLOT(layerCompletionChanged(ModelId)),
                Qt::UniqueConnection);
        connect(noteLayer, SIGNAL(reAnalyseRegion(sv_frame_t, sv_frame_t, float, float)),
                this, SLOT(reAnalyseRegion(sv_frame_t, sv_frame_t, float, float)),
                Qt::UniqueConnection);
        connect(noteLayer, SIGNAL(materialiseReAnalysis()),
                this, SLOT(materialiseReAnalysis()),
                Qt::UniqueConnection);
    }
}

void
Analyser::reAnalyseRegion(sv_frame_t frame0, sv_frame_t frame1, float freq0, float freq1)
{
    cerr << "Analyser::reAnalyseRegion(" << frame0 << ", " << frame1
         << ", " << freq0 << ", " << freq1 << ")" << endl;
    showPitchCandidates(true);
    (void)reAnalyseSelection(Selection(frame0, frame1),
                             FrequencyRange(freq0, freq1));
}

void
Analyser::materialiseReAnalysis()
{
    if (m_reAnalysingSelection.isEmpty()) return;
    switchPitchCandidate(m_reAnalysingSelection, true); // or false, doesn't matter
}

QString
Analyser::reAnalyseSelection(Selection sel, FrequencyRange range)
{
    QMutexLocker locker(&m_asyncMutex);

    auto waveFileModel = ModelById::getAs<WaveFileModel>(m_fileModel);
    if (!waveFileModel) {
        return "Internal error: Analyser::reAnalyseSelection() called with no model present";
    }
    
    if (!m_reAnalysingSelection.isEmpty()) {
        if (sel == m_reAnalysingSelection && range == m_reAnalysingRange) {
            cerr << "selection & range are same as current analysis, ignoring" << endl;
            return "";
        }
    }

    if (sel.isEmpty()) return "";

    if (m_currentAsyncHandle) {
        m_document->cancelAsyncLayerCreation(m_currentAsyncHandle);
    }

    if (!m_reAnalysisCandidates.empty()) {
        CommandHistory::getInstance()->startCompoundOperation
            (tr("Discard Previous Candidates"), true);
        discardPitchCandidates();
        CommandHistory::getInstance()->endCompoundOperation();
    }

    m_reAnalysingSelection = sel;
    m_reAnalysingRange = range;

    m_preAnalysis = Clipboard();
    Layer *myLayer = m_layers[PitchTrack];
    if (myLayer) {
        myLayer->copy(m_pane, sel, m_preAnalysis);
    }

    TransformFactory *tf = TransformFactory::getInstance();
    
    QString plugname1 = "pYIN";
    QString plugname2 = "CHP";

    QString base = "vamp:pyin:localcandidatepyin:";
    QString out = "pitchtrackcandidates";

    if (range.isConstrained()) {
        base = "vamp:chp:constrainedharmonicpeak:";
        out = "peak";
    }

    Transforms transforms;

    QString notFound = tr("Transform \"%1\" not found. Unable to perform interactive analysis.<br><br>Are the %2 and %3 Vamp plugins correctly installed?");
    if (!tf->haveTransform(base + out)) {
	return notFound.arg(base + out).arg(plugname1).arg(plugname2);
    }

    Transform t = tf->getDefaultTransformFor
        (base + out, waveFileModel->getSampleRate());
    t.setStepSize(256);
    t.setBlockSize(2048);

    if (range.isConstrained()) {
        t.setParameter("minfreq", float(range.min));
        t.setParameter("maxfreq", float(range.max));
        t.setBlockSize(4096);
    }

    // get time stamps that align with the 256-sample grid of the original extraction
    const sv_frame_t grid = 256;
    sv_frame_t startSample = (sel.getStartFrame() / grid) * grid;
    if (startSample < sel.getStartFrame()) startSample += grid;
    sv_frame_t endSample = (sel.getEndFrame() / grid) * grid;
    if (endSample < sel.getEndFrame()) endSample += grid;
    if (!range.isConstrained()) {
        startSample -= 4*grid; // 4*256 is for 4 frames offset due to timestamp shift
        endSample   -= 4*grid;
    } else {
        endSample   -= 9*grid; // MM says: not sure what the CHP plugin does there
    }
    RealTime start = RealTime::frame2RealTime(startSample, waveFileModel->getSampleRate()); 
    RealTime end = RealTime::frame2RealTime(endSample, waveFileModel->getSampleRate());

    RealTime duration;

    if (sel.getEndFrame() > sel.getStartFrame()) {
        duration = end - start;
    }

    cerr << "Analyser::reAnalyseSelection: start " << start << " end " << end << " original selection start " << sel.getStartFrame() << " end " << sel.getEndFrame() << " duration " << duration << endl;

    if (duration <= RealTime::zeroTime) {
        cerr << "Analyser::reAnalyseSelection: duration <= 0, not analysing" << endl;
        return "";
    }
    
    t.setStartTime(start);
    t.setDuration(duration);

    transforms.push_back(t);
    
    m_currentAsyncHandle =
        m_document->createDerivedLayersAsync(transforms, m_fileModel, this);

    return "";
}

QString
Analyser::analyseRange(sv_frame_t start, sv_frame_t end,
                       sv_frame_t clipStart, sv_frame_t clipEnd)
{
    auto waveFileModel = ModelById::getAs<WaveFileModel>(m_fileModel);
    if (!waveFileModel) {
        return "Internal error: Analyser::analyseRange() called with no model present";
    }
    if (!m_document || !m_pane) {
        return "Internal error: Analyser::analyseRange() called with no document or pane present";
    }
    if (!m_layers[PitchTrack] || !m_layers[Notes]) {
        return "Internal error: Analyser::analyseRange() called with no pitch track and notes to merge into";
    }

    // One at a time.  A second call means the audio under us has been
    // replaced again, so the first run's result is of no use to anyone
    discardRangedAnalysis();

    // ... and neither is what the merge before it changed
    m_rangedPitchChange = TakeEvents::Change();
    m_rangedNotesChange = TakeEvents::Change();

    sv_samplerate_t rate = waveFileModel->getSampleRate();

    if (clipStart < 0) clipStart = 0;

    if (waveFileModel->isReady()) {
        sv_frame_t fileEnd = waveFileModel->getEndFrame();
        if (clipEnd < 0 || clipEnd > fileEnd) clipEnd = fileEnd;
    } else if (clipEnd < 0) {
        // A file that is still being decoded reports a frame count that
        // is still growing, so it cannot say where its end is; the
        // caller has not said either.  Whatever is asked for, the run
        // stops at the end of the file, and the transform waits for the
        // model before it reads any of it
        clipEnd = end;
    }

    if (start < clipStart) start = clipStart;
    if (end > clipEnd) end = clipEnd;
    if (end <= start) return "";

    // Half a second of context on each side, so that a note at an edge
    // is found whole -- but never outside the material the caller says
    // is there to analyse (the take's coverage)
    sv_frame_t margin = sv_frame_t(rate / 2);
    bool clippedStart = (start - margin < clipStart);
    bool clippedEnd = (end + margin > clipEnd);
    sv_frame_t from = std::max(clipStart, start - margin);
    sv_frame_t to = std::min(clipEnd, end + margin);

    // Aligned to the 256-frame grid, as reAnalyseSelection() does. The
    // step size is the grid a whole-file run's results sit on, and the
    // notes of a ranged run are placed relative to its first block (see
    // mergeRangedAnalysis()), so only a start on the grid puts them
    // where a whole-file run would have put them
    const sv_frame_t grid = analysisStepSize;
    from = (from / grid) * grid;
    to = ((to + grid - 1) / grid) * grid;
    if (to <= from) return "";

    Transforms transforms;
    QString error = buildAnalysisTransforms(transforms);
    if (error != "") return error;

    RealTime startTime = RealTime::frame2RealTime(from, rate);
    RealTime duration = RealTime::frame2RealTime(to - from, rate);

    for (Transform &t : transforms) {
        t.setStartTime(startTime);
        t.setDuration(duration);
    }

    cerr << "Analyser::analyseRange: " << start << " to " << end
         << ", widened and aligned to " << from << " to " << to
         << " (clip " << clipStart << " to " << clipEnd << ")" << endl;

    // The temporary layers are registered with the document, so that
    // deleting them releases their models, but they go into no view
    std::vector<Layer *> layers =
        m_document->createDerivedLayers(transforms, m_fileModel);

    for (Layer *layer : layers) {
        m_rangedLayers.push_back(layer);
        ModelId id = layer->getModel();
        // By model and not by layer type: all we want is the events
        if (ModelById::getAs<NoteModel>(id)) {
            m_rangedNotesModel = id;
        } else if (ModelById::getAs<SparseTimeValueModel>(id)) {
            m_rangedPitchModel = id;
        }
    }

    if (m_rangedPitchModel.isNone() || m_rangedNotesModel.isNone()) {
        discardRangedAnalysis();
        return tr("Transform \"pYIN\" did not run correctly (no pitch track and notes for the range)");
    }

    m_rangedStart = from;
    m_rangedEnd = to;

    // Only the middle of the run is merged (see mergeRangedAnalysis()):
    // half the margin on each side of the range asked for, so that the
    // run keeps a quarter of a second of its own context on each side
    // that nothing is taken from. Where the run stopped at the edge of
    // the caller's coverage there is no context to keep -- nothing but
    // silence beyond -- and the window reaches that edge
    m_rangedMergeStart = clippedStart ? from : std::max(from, start - margin/2);
    m_rangedMergeEnd = clippedEnd ? to : std::min(to, end + margin/2);
    m_rangedClippedEnd = clippedEnd;

    for (ModelId id : { m_rangedPitchModel, m_rangedNotesModel }) {
        auto model = ModelById::get(id);
        if (!model) continue;
        // Emitted on the transform's own thread, so delivered here as a
        // queued call: the merge happens on this thread like any other
        connect(model.get(), SIGNAL(completionChanged(ModelId)),
                this, SLOT(rangedAnalysisCompletionChanged(ModelId)));
    }

    // createDerivedLayers() returns only once the transform has set both
    // outputs' completion to 0, so no signal can have been missed above.
    // A very short range could have finished by now all the same
    rangedAnalysisCompletionChanged({});

    return "";
}

void
Analyser::rangedAnalysisCompletionChanged(ModelId)
{
    if (m_rangedLayers.empty()) return;

    auto newPitch = ModelById::getAs<SparseTimeValueModel>(m_rangedPitchModel);
    auto newNotes = ModelById::getAs<NoteModel>(m_rangedNotesModel);

    if (!newPitch || !newNotes) {
        cerr << "Analyser::rangedAnalysisCompletionChanged: a temporary model "
             << "has gone, merging nothing" << endl;
        discardRangedAnalysis();
        return;
    }

    // A transform sets its outputs' completion to 100 whether it ran to
    // the end or was abandoned, but an abandoned one is cancelled and
    // discarded from here (see discardRangedAnalysis()), so completion
    // at 100 in both means a result
    if (!newPitch->isReady() || !newNotes->isReady()) return;

    mergeRangedAnalysis();
}

void
Analyser::mergeRangedAnalysis()
{
    auto newPitch = ModelById::getAs<SparseTimeValueModel>(m_rangedPitchModel);
    auto newNotes = ModelById::getAs<NoteModel>(m_rangedNotesModel);

    auto pitch = m_layers[PitchTrack] ?
        ModelById::getAs<SparseTimeValueModel>(m_layers[PitchTrack]->getModel()) :
        nullptr;
    auto notes = m_layers[Notes] ?
        ModelById::getAs<NoteModel>(m_layers[Notes]->getModel()) : nullptr;

    if (!newPitch || !newNotes || !pitch || !notes) {
        cerr << "Analyser::mergeRangedAnalysis: a model has gone, "
             << "merging nothing" << endl;
        discardRangedAnalysis();
        return;
    }

    EventVector newPitchEvents = newPitch->getAllEvents();
    EventVector newNoteEvents;

    // The time-stamp question of spec section 11, settled by
    // TestSingingAnalysis::ranged_matches_whole_file: the smoothed pitch
    // track needs no correction, because it is a fixed-sample-rate
    // output and the host rounds each feature to the nearest multiple of
    // the step size of the whole file.  The notes output is
    // variable-sample-rate, and pYIN times a note by its frame number
    // *within this run* (PYinVamp::addNoteFeatures()), so for a run that
    // did not start at the beginning of the file the notes come back
    // shifted to near zero
    for (const Event &e : newNotes->getAllEvents()) {
        newNoteEvents.push_back(e.withFrame(e.getFrame() + m_rangedStart));
    }

    // What the merge replaces is not the whole run but the window W in
    // the middle of it (analyseRange() works it out).  The run's own
    // ends are where pYIN has least context, and it cannot stamp its
    // first two hops at all, so what it says there is worse than what
    // the models hold already -- which, for audio that has not changed,
    // is the answer of a whole-file run
    sv_frame_t wFrom = m_rangedMergeStart;
    sv_frame_t pitchTo = m_rangedMergeEnd, noteTo = m_rangedMergeEnd;

    // The exception is an end of the run that stopped at the edge of the
    // caller's coverage: nothing is kept beyond it, so the window takes
    // in whatever the run stamped past it.  pYIN stamps a block a
    // quarter of a block in (two hops here), so the last events of a run
    // lie a little past its end
    if (m_rangedClippedEnd) {
        if (!newPitchEvents.empty()) {
            pitchTo = std::max(pitchTo, newPitchEvents.back().getFrame() + 1);
        }
        for (const Event &e : newNoteEvents) {
            noteTo = std::max(noteTo, e.getFrame() + 1);
        }
    }

    cerr << "Analyser::mergeRangedAnalysis: " << newPitchEvents.size()
         << " pitch event(s) and " << newNoteEvents.size() << " note(s) from "
         << m_rangedStart << " to " << m_rangedEnd << "; merging pitch in "
         << wFrom << " to " << pitchTo << ", notes in " << wFrom << " to "
         << noteTo << endl;

    // Every remove and add is noted as it is made: the two changes
    // together are what an undo of the recording this analysis belongs to
    // has to reverse (getRangedPitchChange())
    m_rangedPitchChange = TakeEvents::Change();
    m_rangedNotesChange = TakeEvents::Change();

    for (const Event &e :
             pitch->getEventsStartingWithin(wFrom, pitchTo - wFrom)) {
        pitch->remove(e);
        m_rangedPitchChange.removed.push_back(e);
    }
    // pYIN in fixed-lag mode (the default, and what we run) stamps one
    // frame of every run twice: the last frame that process() emits is
    // emitted again as the first of getRemainingFeatures(), 100 hops
    // before the end of the run.  A whole-file run has the same double
    // frame near the end of the file, where it does no harm, but a ranged
    // run puts it in the middle of the merge window, so drop it here
    sv_frame_t lastAdded = -1;
    for (const Event &e : newPitchEvents) {
        if (e.getFrame() >= wFrom && e.getFrame() < pitchTo &&
            e.getFrame() != lastAdded) {
            pitch->add(e);
            m_rangedPitchChange.added.push_back(e);
            lastAdded = e.getFrame();
        }
    }

    // Notes go by their onset: the new notes that begin in the window
    // replace the old ones that begin in it
    EventVector adding;
    for (const Event &e : newNoteEvents) {
        if (e.getFrame() >= wFrom && e.getFrame() < noteTo) {
            adding.push_back(e);
        }
    }

    EventVector oldNotes = notes->getAllEvents();

    // The first of the new notes, and the first old note that begins at
    // or after the window: the two notes the window's edges can run into
    sv_frame_t firstAdded = adding.empty() ? -1 : adding.front().getFrame();
    sv_frame_t nextOldOnset = -1;
    for (const Event &e : oldNotes) {
        if (e.getFrame() >= noteTo) {
            nextOldOnset = e.getFrame();
            break;
        }
    }

    // The end of an old note that carries on past the end of the run.
    // The run had to stop singing that note where it stopped listening,
    // and the audio out there has not changed, so the old note is the
    // one that knows where it really ends.  Not where the run's end is
    // the edge of the coverage: there is nothing beyond that but
    // silence, and no old note to believe
    sv_frame_t endBeyondRun = -1;
    if (!m_rangedClippedEnd) {
        for (const Event &e : oldNotes) {
            if (e.getFrame() < m_rangedEnd &&
                e.getFrame() + e.getDuration() > m_rangedEnd) {
                endBeyondRun = e.getFrame() + e.getDuration();
                break;
            }
        }
    }

    for (const Event &e : oldNotes) {
        sv_frame_t f = e.getFrame();
        if (f >= wFrom && f < noteTo) {
            notes->remove(e);
            m_rangedNotesChange.removed.push_back(e);
        } else if (f < wFrom && firstAdded >= 0 &&
                   f + e.getDuration() > firstAdded) {
            // A note that runs into the window from before it is left as
            // it is unless one of the new notes starts inside it, when it
            // is cut back to that onset.  Where the audio has not
            // changed the run finds that note going on from before the
            // window, has nothing to add inside it, and the one note
            // stays one note
            notes->remove(e);
            notes->add(e.withDuration(firstAdded - f));
            m_rangedNotesChange.removed.push_back(e);
            m_rangedNotesChange.added.push_back(e.withDuration(firstAdded - f));
        }
    }
    for (Event e : adding) {
        // A note that the end of the run cut off goes on to where the
        // old note it belongs to ended.  A few hops of slack: the run's
        // last note ends within a block or so of where it stopped
        if (endBeyondRun > 0) {
            sv_frame_t end = e.getFrame() + e.getDuration();
            sv_frame_t slack = 4 * analysisStepSize;
            if (end > m_rangedEnd - slack && end < m_rangedEnd + slack &&
                endBeyondRun > end) {
                e = e.withDuration(endBeyondRun - e.getFrame());
            }
        }
        // The far edge the same way round: an old note that begins at or
        // after the window keeps its onset, and a new note that would run
        // over it is cut back
        if (nextOldOnset > e.getFrame() &&
            e.getFrame() + e.getDuration() > nextOldOnset) {
            e = e.withDuration(nextOldOnset - e.getFrame());
        }
        notes->add(e);
        m_rangedNotesChange.added.push_back(e);
    }

    // The events went straight into the models, as a transform's do, with
    // no command of their own.  What the merge changed is remembered
    // instead, for the command of the recording that asked for it: that
    // one command undoes the splice and the analysis of it together
    discardRangedAnalysis();

    emit rangedAnalysisMerged();
    emit initialAnalysisCompleted();
}

void
Analyser::discardRangedAnalysis()
{
    if (m_rangedLayers.empty()) {
        m_rangedPitchModel = {};
        m_rangedNotesModel = {};
        return;
    }

    // Cleared before anything is cancelled or deleted: a completion
    // signal that arrives from a transform we are abandoning then finds
    // nothing to merge, and deleteLayer() below reaches
    // layerAboutToBeDeleted() with the layers already forgotten
    std::vector<Layer *> doomed;
    doomed.swap(m_rangedLayers);
    ModelId pitchId = m_rangedPitchModel, notesId = m_rangedNotesModel;
    m_rangedPitchModel = {};
    m_rangedNotesModel = {};

    // Before the models are released: see cancelAnalyses()
    auto mtf = ModelTransformerFactory::getInstance();
    for (ModelId id : { pitchId, notesId }) {
        if (!id.isNone()) mtf->cancel(id);
    }

    for (Layer *layer : doomed) {
        // As in removeAllLayers(): force, and no removeLayerFromView,
        // so that nothing of this is left on the undo stack
        if (m_document) m_document->deleteLayer(layer, true);
    }
}

bool
Analyser::arePitchCandidatesShown() const
{
    return m_candidatesVisible;
}

void
Analyser::showPitchCandidates(bool shown) 
{
    if (m_candidatesVisible == shown) return;

    foreach (Layer *layer, m_reAnalysisCandidates) {
        if (shown) {
            CommandHistory::getInstance()->addCommand
                (new ShowLayerCommand(m_pane, layer, true,
                                      tr("Show Pitch Candidates")));
        } else {
            CommandHistory::getInstance()->addCommand
                (new ShowLayerCommand(m_pane, layer, false,
                                      tr("Hide Pitch Candidates")));
        }
    }

    m_candidatesVisible = shown;
}

void
Analyser::layersCreated(Document::LayerCreationAsyncHandle handle,
                        vector<Layer *> primary,
                        vector<Layer *> additional)
{
    {
        QMutexLocker locker(&m_asyncMutex);

        if (handle != m_currentAsyncHandle || 
            m_reAnalysingSelection == Selection()) {
            // We don't want these!
            for (int i = 0; i < (int)primary.size(); ++i) {
                m_document->deleteLayer(primary[i]);
            }
            for (int i = 0; i < (int)additional.size(); ++i) {
                m_document->deleteLayer(additional[i]);
            }
            return;
        }
        m_currentAsyncHandle = 0;

        CommandHistory::getInstance()->startCompoundOperation
            (tr("Re-Analyse Selection"), true);

        m_reAnalysisCandidates.clear();

        vector<Layer *> all;
        for (int i = 0; i < (int)primary.size(); ++i) {
            all.push_back(primary[i]);
        }
        for (int i = 0; i < (int)additional.size(); ++i) {
            all.push_back(additional[i]);
        }

        for (int i = 0; i < (int)all.size(); ++i) {
            TimeValueLayer *t = qobject_cast<TimeValueLayer *>(all[i]);
            if (t) {
                auto params = t->getPlayParameters();
                if (params) {
                    params->setPlayAudible(false);
                }
                t->setBaseColour
                    (ColourDatabase::getInstance()->getColourIndex(tr("Bright Orange")));
                t->setPresentationName("candidate");
                m_document->addLayerToView(m_pane, t);
                m_reAnalysisCandidates.push_back(t);
                /*
                cerr << "New re-analysis candidate model has "
                     << ((SparseTimeValueModel *)t->getModel())->getAllEvents().size() << " point(s)" << endl;
                */
            }
        }

        if (!all.empty()) {
            bool show = m_candidatesVisible;
            m_candidatesVisible = !show; // to ensure the following takes effect
            showPitchCandidates(show);
        }

        CommandHistory::getInstance()->endCompoundOperation();
    }

    emit layersChanged();
}

bool
Analyser::haveHigherPitchCandidate() const
{
    if (m_reAnalysisCandidates.empty()) return false;
    return (m_currentCandidate < 0 ||
            (m_currentCandidate + 1 < (int)m_reAnalysisCandidates.size()));
}    

bool
Analyser::haveLowerPitchCandidate() const
{
    if (m_reAnalysisCandidates.empty()) return false;
    return (m_currentCandidate < 0 || m_currentCandidate >= 1);
}    

void
Analyser::switchPitchCandidate(Selection sel, bool up)
{
    if (m_reAnalysisCandidates.empty()) return;

    if (up) {
        m_currentCandidate = m_currentCandidate + 1;
        if (m_currentCandidate >= (int)m_reAnalysisCandidates.size()) {
            m_currentCandidate = 0;
        }
    } else {
        m_currentCandidate = m_currentCandidate - 1;
        if (m_currentCandidate < 0) {
            m_currentCandidate = (int)m_reAnalysisCandidates.size() - 1;
        }
    }

    Layer *pitchTrack = m_layers[PitchTrack];
    if (!pitchTrack) return;

    Clipboard clip;
    pitchTrack->deleteSelection(sel);
    m_reAnalysisCandidates[m_currentCandidate]->copy(m_pane, sel, clip);
    pitchTrack->paste(m_pane, clip, 0, false);

    stackLayers();
}

void
Analyser::stackLayers()
{
    // raise the pitch track, then notes on top (if present)
    if (m_layers[PitchTrack]) {
        m_paneStack->setCurrentLayer(m_pane, m_layers[PitchTrack]);
    }
    if (m_layers[Notes] && !m_layers[Notes]->isLayerDormant(m_pane)) {
        m_paneStack->setCurrentLayer(m_pane, m_layers[Notes]);
    }
}

void
Analyser::shiftOctave(Selection sel, bool up)
{
    float factor = (up ? 2.f : 0.5f);
    
    vector<Layer *> actOn;

    Layer *pitchTrack = m_layers[PitchTrack];
    if (pitchTrack) actOn.push_back(pitchTrack);

    foreach (Layer *layer, actOn) {
        
        Clipboard clip;
        layer->copy(m_pane, sel, clip);
        layer->deleteSelection(sel);

        Clipboard shifted;
        foreach (Event e, clip.getPoints()) {
            if (e.hasValue()) {
                Event se = e.withValue(e.getValue() * factor);
                shifted.addPoint(se);
            } else {
                shifted.addPoint(e);
            }
        }
        
        layer->paste(m_pane, shifted, 0, false);
    }
}

void
Analyser::deletePitches(Selection sel)
{
    Layer *pitchTrack = m_layers[PitchTrack];
    if (!pitchTrack) return;

    pitchTrack->deleteSelection(sel);
}

void
Analyser::abandonReAnalysis(Selection sel)
{
    // A compound command is already in progress

    discardPitchCandidates();

    Layer *myLayer = m_layers[PitchTrack];
    if (!myLayer) return;
    myLayer->deleteSelection(sel);
    myLayer->paste(m_pane, m_preAnalysis, 0, false);
}    

void
Analyser::clearReAnalysis()
{
    discardPitchCandidates();
}

void
Analyser::discardPitchCandidates()
{
    if (!m_reAnalysisCandidates.empty()) {
        // We don't use a compound command here, because we may be
        // already in one. Caller bears responsibility for doing that
        foreach (Layer *layer, m_reAnalysisCandidates) {
            // This will cause the layer to be deleted later (ownership is
            // transferred to the remove command)
            m_document->removeLayerFromView(m_pane, layer);
        }
        m_reAnalysisCandidates.clear();
    }

    m_currentCandidate = -1;
    m_reAnalysingSelection = Selection();
    m_candidatesVisible = false;
}

void
Analyser::layerAboutToBeDeleted(Layer *doomed)
{
    // Called for every layer the document deletes, ours or not

    vector<Layer *> notDoomed;

    foreach (Layer *layer, m_reAnalysisCandidates) {
        if (layer != doomed) {
            notDoomed.push_back(layer);
        }
    }

    if (notDoomed.size() != m_reAnalysisCandidates.size()) {
        m_reAnalysisCandidates = notDoomed;
        // The index no longer means the candidate it did
        m_currentCandidate = -1;
    }

    // A temporary layer of a ranged analysis, deleted by someone else --
    // we clear m_rangedLayers before deleting them ourselves
    auto r = std::find(m_rangedLayers.begin(), m_rangedLayers.end(), doomed);
    if (r != m_rangedLayers.end()) {
        m_rangedLayers.erase(r);
        if (m_rangedLayers.empty()) {
            m_rangedPitchModel = {};
            m_rangedNotesModel = {};
        }
    }

    // A layer of ours deleted by someone else, e.g. by a command
    // dropped from the undo history
    for (auto &entry : m_layers) {
        if (entry.second == doomed) entry.second = nullptr;
    }
}

void
Analyser::takePitchTrackFrom(Layer *otherLayer)
{
    Layer *myLayer = m_layers[PitchTrack];
    if (!myLayer || !otherLayer) return;

    auto myModel = ModelById::get(myLayer->getModel());
    auto otherModel = ModelById::get(otherLayer->getModel());
    if (!myModel || !otherModel) return;

    Clipboard clip;
    
    Selection sel = Selection(myModel->getStartFrame(),
                              myModel->getEndFrame());
    myLayer->deleteSelection(sel);

    sel = Selection(otherModel->getStartFrame(),
                    otherModel->getEndFrame());
    otherLayer->copy(m_pane, sel, clip);

    // Remove all pitches <= 0Hz -- we now save absent pitches as 0Hz
    // values when exporting a pitch track, so we need to exclude them
    // here when importing again
    EventVector after;
    int excl = 0;
    for (const auto &p: clip.getPoints()) {
        if (p.hasValue() && p.getValue() > 0.f) {
            after.push_back(p);
        } else {
            ++excl;
        }
    }
    clip.setPoints(after);

    myLayer->paste(m_pane, clip, 0, false);
}

void
Analyser::getEnclosingSelectionScope(sv_frame_t f, sv_frame_t &f0, sv_frame_t &f1)
{
    FlexiNoteLayer *flexiNoteLayer = 
        qobject_cast<FlexiNoteLayer *>(m_layers[Notes]);

    sv_frame_t f0i = f, f1i = f;
    int res = 1;

    if (!flexiNoteLayer) {
        f0 = f1 = f;
        return;
    }
    
    flexiNoteLayer->snapToFeatureFrame(m_pane, f0i, res, Layer::SnapLeft, -1);
    flexiNoteLayer->snapToFeatureFrame(m_pane, f1i, res, Layer::SnapRight, -1);

    f0 = (f0i < 0 ? 0 : f0i);
    f1 = (f1i < 0 ? 0 : f1i);
}

void
Analyser::saveState(Component c) const
{
    bool v = isVisible(c);
    bool a = isAudible(c);
    QSettings settings;
    settings.beginGroup("Analyser");
    settings.setValue(QString("visible-%1").arg(int(c)), v);
    settings.setValue(QString("audible-%1").arg(int(c)), a);
    settings.endGroup();
}

void
Analyser::loadState(Component c)
{
    QSettings settings;
    settings.beginGroup("Analyser");
    bool deflt = (c == Spectrogram ? false : true);
    bool v = settings.value(QString("visible-%1").arg(int(c)), deflt).toBool();
    bool a = settings.value(QString("audible-%1").arg(int(c)), true).toBool();
    settings.endGroup();
    setVisible(c, v);
    setAudible(c, a);
}

void
Analyser::setIntelligentActions(bool on) 
{
    std::cerr << "toggle setIntelligentActions " << on << std::endl;

    FlexiNoteLayer *flexiNoteLayer = 
        qobject_cast<FlexiNoteLayer *>(m_layers[Notes]);
    if (flexiNoteLayer) {
        flexiNoteLayer->setIntelligentActions(on);
    }
}

bool
Analyser::isVisible(Component c) const
{
    if (m_layers[c]) {
        return !m_layers[c]->isLayerDormant(m_pane);
    } else {
        return false;
    }
}

void
Analyser::setVisible(Component c, bool v)
{
    if (m_layers[c]) {
        m_layers[c]->setLayerDormant(m_pane, !v);

        if (v) {
            if (c == Notes) {
                m_paneStack->setCurrentLayer(m_pane, m_layers[c]);
            } else if (c == PitchTrack) {
                // raise the pitch track, then notes on top (if present)
                m_paneStack->setCurrentLayer(m_pane, m_layers[c]);
                if (m_layers[Notes] &&
                    !m_layers[Notes]->isLayerDormant(m_pane)) {
                    m_paneStack->setCurrentLayer(m_pane, m_layers[Notes]);
                }
            }
        }

        m_pane->layerParametersChanged();
        saveState(c);
    }
}

bool
Analyser::isAudible(Component c) const
{
    if (m_layers[c]) {
        auto params = m_layers[c]->getPlayParameters();
        if (!params) return false;
        return params->isPlayAudible();
    } else {
        return false;
    }
}

void
Analyser::setAudible(Component c, bool a)
{
    if (m_layers[c]) {
        auto params = m_layers[c]->getPlayParameters();
        if (!params) return;
        params->setPlayAudible(a);
        saveState(c);
    }
}

float
Analyser::getGain(Component c) const
{
    if (m_layers[c]) {
        auto params = m_layers[c]->getPlayParameters();
        if (!params) return 1.f;
        return params->getPlayGain();
    } else {
        return 1.f;
    }
}
    
void
Analyser::setGain(Component c, float gain)
{
    if (m_layers[c]) {
        auto params = m_layers[c]->getPlayParameters();
        if (!params) return;
        params->setPlayGain(gain);
        saveState(c);
    }
}

float
Analyser::getPan(Component c) const
{
    if (m_layers[c]) {
        auto params = m_layers[c]->getPlayParameters();
        if (!params) return 1.f;
        return params->getPlayPan();
    } else {
        return 1.f;
    }
}
    
void
Analyser::setPan(Component c, float pan)
{
    if (m_layers[c]) {
        auto params = m_layers[c]->getPlayParameters();
        if (!params) return;
        params->setPlayPan(pan);
        saveState(c);
    }
}


    
