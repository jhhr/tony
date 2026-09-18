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

#ifndef TEST_SINGING_DOCUMENT_H
#define TEST_SINGING_DOCUMENT_H

// Tier 3: the Document ownership rules that the singing-track flows
// depend on, and the pane pruning helper built on them. No MainWindow.
//
// Each test starts from the state the application is in after a
// session load: a main model, pane 0, and a time ruler in pane 0 that
// the document's other panes share.

#include "../PaneUtils.h"

#include "framework/Document.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/ViewManager.h"
#include "layer/Layer.h"
#include "layer/LayerFactory.h"
#include "widgets/CommandHistory.h"
#include "data/model/WritableWaveFileModel.h"

#include <QObject>
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>
#include <vector>

class TestSingingDocument : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_fileCounter = 0;

    sv::ViewManager *m_viewManager = nullptr;
    sv::PaneStack *m_paneStack = nullptr;
    sv::Document *m_document = nullptr;
    sv::Pane *m_mainPane = nullptr;
    sv::Layer *m_ruler = nullptr;

    sv::ModelId makeAudioModel() {
        QString path = m_dir.filePath
            (QString("audio-%1.wav").arg(++m_fileCounter));
        auto model = std::make_shared<sv::WritableWaveFileModel>
            (path, 44100.0, 1,
             sv::WritableWaveFileModel::Normalisation::None);
        std::vector<float> data(4410, 0.25f);
        const float *ptr = data.data();
        model->addSamples(&ptr, sv::sv_frame_t(data.size()));
        model->writeComplete();
        return sv::ModelById::add(model);
    }

    // What openAudio(CreateAdditionalModel) and record() leave behind:
    // a new pane holding an imported waveform layer that is the only
    // reference to the new model and, optionally, the shared ruler.
    struct Extra {
        sv::ModelId model;
        sv::Layer *waveform = nullptr;
        sv::Pane *pane = nullptr;
    };

    Extra addExtraPane(bool withRuler) {
        Extra extra;
        extra.model = makeAudioModel();
        extra.waveform = m_document->createImportedLayer(extra.model);
        extra.pane = m_paneStack->addPane();
        if (withRuler) {
            m_document->addLayerToView(extra.pane, m_ruler);
        }
        if (extra.waveform) {
            m_document->addLayerToView(extra.pane, extra.waveform);
        }
        return extra;
    }

    // As Analyser::addWaveform() and loadBackgroundMusic() do
    sv::Layer *addSecondReference(sv::ModelId model) {
        sv::Layer *layer = m_document->createLayer(sv::LayerFactory::Waveform);
        if (!layer) return nullptr;
        m_document->setModel(layer, model);
        m_document->addLayerToView(m_mainPane, layer);
        return layer;
    }

    static bool paneHasLayer(sv::Pane *pane, sv::Layer *layer) {
        for (int i = 0; i < pane->getLayerCount(); ++i) {
            if (pane->getLayer(i) == layer) return true;
        }
        return false;
    }

    bool documentHasLayer(sv::Layer *layer) const {
        return m_document->getLayers().count(layer) > 0;
    }

    static bool modelAlive(sv::ModelId id) {
        return bool(sv::ModelById::get(id));
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        qRegisterMetaType<sv::Layer *>("Layer*");
    }

    void init() {
        m_viewManager = new sv::ViewManager;
        m_paneStack = new sv::PaneStack(nullptr, m_viewManager);
        m_document = new sv::Document;
        m_document->setMainModel(makeAudioModel());
        m_mainPane = m_paneStack->addPane();
        m_ruler = m_document->createMainModelLayer(sv::LayerFactory::TimeRuler);
        QVERIFY(m_ruler);
        m_document->addLayerToView(m_mainPane, m_ruler);
    }

    void cleanup() {
        // The document force-deletes its layers from their views, so
        // it has to go while the panes still exist
        delete m_document;
        delete m_paneStack;
        delete m_viewManager;
        m_document = nullptr;
        m_paneStack = nullptr;
        m_viewManager = nullptr;
        m_mainPane = nullptr;
        m_ruler = nullptr;
    }

    // --- The rules -------------------------------------------------------

    void force_delete_releases_sole_model() {
        // The mechanism behind review finding 1
        Extra extra = addExtraPane(false);
        QVERIFY(extra.waveform);
        QVERIFY(modelAlive(extra.model));

        m_document->deleteLayer(extra.waveform, true);

        QVERIFY(!documentHasLayer(extra.waveform));
        QVERIFY(!modelAlive(extra.model));
    }

    void second_layer_keeps_model_alive() {
        // The background-music workaround
        Extra extra = addExtraPane(false);
        QVERIFY(extra.waveform);
        QVERIFY(addSecondReference(extra.model));

        m_document->deleteLayer(extra.waveform, true);

        QVERIFY(modelAlive(extra.model));
    }

    void detach_keeps_layer_and_model() {
        // Document::detachLayerFromView is an addition in the svapp fork
        Extra extra = addExtraPane(false);
        QVERIFY(extra.waveform);
        QSignalSpy commands(sv::CommandHistory::getInstance(),
                            SIGNAL(commandExecuted()));

        m_document->detachLayerFromView(extra.pane, extra.waveform);

        QVERIFY(!paneHasLayer(extra.pane, extra.waveform));
        QVERIFY(documentHasLayer(extra.waveform));
        QVERIFY(modelAlive(extra.model));
        QCOMPARE(int(commands.count()), 0);
    }

    void force_delete_skips_play_source() {
        // layerInAView(layer, false) is what usually takes a model out
        // of the play source. A forced delete never emits it (review
        // finding 6); what the main window goes by then is
        // modelAboutToBeReleased.
        Extra viaCommand = addExtraPane(false);
        Extra viaForce = addExtraPane(false);
        QVERIFY(viaCommand.waveform && viaForce.waveform);
        QSignalSpy inAView(m_document,
                           SIGNAL(layerInAView(Layer *, bool)));
        QVERIFY(inAView.isValid());
        QSignalSpy released(m_document,
                            SIGNAL(modelAboutToBeReleased(ModelId)));
        QVERIFY(released.isValid());

        m_document->removeLayerFromView(viaCommand.pane, viaCommand.waveform);
        QCOMPARE(int(inAView.count()), 1);
        QCOMPARE(inAView.at(0).at(1).toBool(), false);
        QCOMPARE(int(released.count()), 0);

        m_document->deleteLayer(viaForce.waveform, true);
        QCOMPARE(int(inAView.count()), 1);
        QCOMPARE(int(released.count()), 1);
        QVERIFY(!modelAlive(viaForce.model));
    }

    // --- The pruning helper ----------------------------------------------

    void shared_ruler_survives_prune() {
        // Review finding 2
        Extra extra = addExtraPane(true);
        QVERIFY(extra.waveform);
        QVERIFY(addSecondReference(extra.model));
        QVERIFY(paneHasLayer(extra.pane, m_ruler));

        pruneExtraPane(m_document, m_paneStack, extra.pane, extra.model);

        QVERIFY2(documentHasLayer(m_ruler),
                 "the shared time ruler was deleted from the document");
        QVERIFY(paneHasLayer(m_mainPane, m_ruler));
    }

    void prune_deletes_orphan_and_pane() {
        Extra extra = addExtraPane(true);
        QVERIFY(extra.waveform);
        sv::Layer *kept = addSecondReference(extra.model);
        QVERIFY(kept);
        QCOMPARE(m_paneStack->getPaneCount(), 2);
        QSignalSpy commands(sv::CommandHistory::getInstance(),
                            SIGNAL(commandExecuted()));

        pruneExtraPane(m_document, m_paneStack, extra.pane, extra.model);

        QCOMPARE(m_paneStack->getPaneCount(), 1);
        QCOMPARE(m_paneStack->getHiddenPaneCount(), 0);
        QVERIFY(!documentHasLayer(extra.waveform));
        QVERIFY(documentHasLayer(kept));
        QVERIFY(modelAlive(extra.model));
        // Nothing undoable may be left pointing at the dead pane
        QCOMPARE(int(commands.count()), 0);
    }

    void prune_without_second_ref_releases_model() {
        // What loadSingingTrack() relies on when the analyser could not
        // be set up: pruning is then what disposes of the model
        Extra extra = addExtraPane(true);
        QVERIFY(extra.waveform);

        pruneExtraPane(m_document, m_paneStack, extra.pane, extra.model);

        QVERIFY(!modelAlive(extra.model));
        QCOMPARE(m_paneStack->getPaneCount(), 1);
    }

    void prune_none_id_deletes_nothing() {
        // The ruler's own model id is "none". An empty owned id must
        // not be taken to match it.
        Extra extra = addExtraPane(true);
        QVERIFY(extra.waveform);

        pruneExtraPane(m_document, m_paneStack, extra.pane, sv::ModelId());

        QVERIFY2(documentHasLayer(m_ruler),
                 "the shared time ruler was deleted from the document");
        QVERIFY(paneHasLayer(m_mainPane, m_ruler));
        QVERIFY(documentHasLayer(extra.waveform));
        QVERIFY(modelAlive(extra.model));
        QCOMPARE(m_paneStack->getPaneCount(), 1);
    }

    void prune_hidden_pane() {
        // The record flow hides the pane first and prunes it later
        Extra extra = addExtraPane(true);
        QVERIFY(extra.waveform);
        QVERIFY(addSecondReference(extra.model));
        m_paneStack->hidePane(extra.pane);
        QCOMPARE(m_paneStack->getPaneCount(), 1);
        QCOMPARE(m_paneStack->getHiddenPaneCount(), 1);

        pruneExtraPane(m_document, m_paneStack, extra.pane, extra.model);

        QCOMPARE(m_paneStack->getHiddenPaneCount(), 0);
        QVERIFY2(documentHasLayer(m_ruler),
                 "the shared time ruler was deleted from the document");
        QVERIFY(paneHasLayer(m_mainPane, m_ruler));
        QVERIFY(!documentHasLayer(extra.waveform));
        QVERIFY(modelAlive(extra.model));
    }

    void layer_view_map_clean_after_prune() {
        // If pruning left the dead pane in the document's layer-to-view
        // map, force-deleting the ruler would call into the freed
        // widget. Only a memory checker makes that failure certain;
        // without one this at least exercises the path.
        Extra extra = addExtraPane(true);
        QVERIFY(extra.waveform);
        QVERIFY(addSecondReference(extra.model));

        pruneExtraPane(m_document, m_paneStack, extra.pane, extra.model);

        QVERIFY2(documentHasLayer(m_ruler),
                 "the shared time ruler was deleted from the document");
        m_document->deleteLayer(m_ruler, true);
        QVERIFY(!paneHasLayer(m_mainPane, m_ruler));
        m_ruler = nullptr;
    }
};

#endif
