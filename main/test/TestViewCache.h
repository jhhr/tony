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

#ifndef TEST_VIEW_CACHE_H
#define TEST_VIEW_CACHE_H

// Tier 3: what a pane draws again from its layers, and what it takes
// from its cache of what they drew (the svgui fork). No MainWindow.
// That the live dots of a take are kept out of the cache is
// TestRecordWorkflow's business (live_dots_appear).

#include "view/Pane.h"
#include "view/ViewManager.h"
#include "layer/TimeValueLayer.h"
#include "data/model/SparseTimeValueModel.h"

#include <QObject>
#include <QPainter>
#include <QtTest>

#include <memory>
#include <vector>

class TestViewCache : public QObject
{
    Q_OBJECT

    // A layer that counts the times the pane has it draw
    class CountedLayer : public sv::TimeValueLayer
    {
    public:
        mutable int drawn = 0;
        void paint(sv::LayerGeometryProvider *v, QPainter &paint,
                   QRect rect) const override {
            ++drawn;
            sv::TimeValueLayer::paint(v, paint, rect);
        }
    };

    sv::ViewManager *m_viewManager = nullptr;
    sv::Pane *m_pane = nullptr;

    // Back to front
    std::vector<CountedLayer *> m_layers;
    std::vector<sv::ModelId> m_models;

    void changeModelOf(int layer) {
        auto model = sv::ModelById::get(m_models[layer]);
        emit model->modelChangedWithin(m_models[layer], 0, 44100);
    }

    // Painted until the pane has nothing left to settle, then counted
    // from nothing
    void settle() {
        for (int i = 0; i < 3; ++i) {
            QCoreApplication::processEvents();
            m_pane->repaint();
        }
        for (auto layer : m_layers) layer->drawn = 0;
    }

    std::vector<int> drawn() {
        std::vector<int> counts;
        for (auto layer : m_layers) counts.push_back(layer->drawn);
        return counts;
    }

private slots:
    void init() {
        m_viewManager = new sv::ViewManager();
        m_pane = new sv::Pane();
        m_pane->setViewManager(m_viewManager);
        m_pane->resize(400, 200);

        for (int i = 0; i < 3; ++i) {
            auto model = std::make_shared<sv::SparseTimeValueModel>
                (44100, 256, false);
            for (int j = 0; j < 20; ++j) {
                model->add(sv::Event(j * 2048, 220.f + float(i * 20 + j), ""));
            }
            m_models.push_back(sv::ModelById::add(model));
            auto layer = new CountedLayer();
            layer->setModel(m_models.back());
            m_layers.push_back(layer);
            m_pane->addLayer(layer);
        }

        m_pane->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_pane));
    }

    void cleanup() {
        delete m_pane;
        m_pane = nullptr;
        for (auto layer : m_layers) delete layer;
        m_layers.clear();
        for (auto id : m_models) sv::ModelById::release(id);
        m_models.clear();
        delete m_viewManager;
        m_viewManager = nullptr;
    }

    // Painted again with nothing changed, the pane takes what it shows
    // from its cache and has none of its layers draw
    void nothing_changed_draws_nothing() {
        settle();
        m_pane->repaint();
        m_pane->repaint(QRect(100, 0, 20, m_pane->height()));
        QCOMPARE(drawn(), std::vector<int>({ 0, 0, 0 }));
    }

    // A change to the model of any layer in the cache has every layer
    // in it drawn again
    void a_change_draws_the_cache_again() {
        settle();
        changeModelOf(1);
        m_pane->repaint();
        QCOMPARE(drawn(), std::vector<int>({ 1, 1, 1 }));
    }

    // A new plot size (View > Plot Size) has every layer drawn again,
    // at once, at the new size
    void a_plot_scale_change_draws_the_cache_again() {
        settle();
        m_viewManager->setPlotScale(1.5);
        m_pane->repaint();
        QCOMPARE(drawn(), std::vector<int>({ 1, 1, 1 }));
    }

    // Kept out of the cache, a layer is drawn every time the pane is
    // painted, and so is every layer in front of it; a change to its
    // model leaves the layers behind it in the cache
    void a_layer_kept_out_is_drawn_by_itself() {
        m_layers[1]->setCachedInView(false);
        settle();

        changeModelOf(1);
        m_pane->repaint();
        QCOMPARE(drawn(), std::vector<int>({ 0, 1, 1 }));

        m_pane->repaint();
        QCOMPARE(drawn(), std::vector<int>({ 0, 2, 2 }));

        // What is still in the cache is drawn again as ever
        changeModelOf(0);
        m_pane->repaint();
        QCOMPARE(drawn(), std::vector<int>({ 1, 3, 3 }));
    }
};

#endif
