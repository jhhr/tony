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

#ifndef TEST_LYRICS_LAYER_H
#define TEST_LYRICS_LAYER_H

// Tier 3: the lyrics plot style of RegionLayer (svgui fork), which
// draws the words of the lyrics along the top of pane 0. Where each
// label goes is worked out by a pure function, tested first; then the
// layer is painted into images, with no MainWindow.

#include "framework/Document.h"
#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/ViewManager.h"
#include "layer/RegionLayer.h"
#include "layer/LayerFactory.h"
#include "data/model/RegionModel.h"
#include "data/model/WritableWaveFileModel.h"

#include <QObject>
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <memory>
#include <vector>

class TestLyricsLayer : public QObject
{
    Q_OBJECT

    typedef std::vector<std::pair<double, double>> Spans;

    QTemporaryDir m_dir;

    sv::ViewManager *m_viewManager = nullptr;
    sv::PaneStack *m_paneStack = nullptr;
    sv::Document *m_document = nullptr;
    sv::Pane *m_pane = nullptr;
    sv::RegionLayer *m_layer = nullptr;

    static constexpr double kRate = 44100.0;
    static constexpr int kWidth = 800;
    static constexpr int kHeight = 200;

    sv::ModelId makeAudioModel() {
        auto model = std::make_shared<sv::WritableWaveFileModel>
            (m_dir.filePath("audio.wav"), kRate, 1,
             sv::WritableWaveFileModel::Normalisation::None);
        std::vector<float> data(size_t(kRate) * 10, 0.25f);
        const float *ptr = data.data();
        model->addSamples(&ptr, sv::sv_frame_t(data.size()));
        model->writeComplete();
        return sv::ModelById::add(model);
    }

    // Words a quarter of a second apart, each longer than that at the
    // zoom the tests use (100 pixels a second), so that they do not
    // all fit in one row; a new line every four words
    void addWords(int count) {
        auto model = sv::ModelById::getAs<sv::RegionModel>(m_layer->getModel());
        QVERIFY(model);
        for (int i = 0; i < count; ++i) {
            sv::sv_frame_t frame = sv::sv_frame_t(kRate * 0.25 * i);
            model->add(sv::Event(frame, float(i / 4), sv::sv_frame_t(kRate * 0.2),
                                 QString("sanaseppo%1").arg(i)));
        }
    }

    // The layer painted into a white image, clip rect by clip rect, as
    // the view paints what has scrolled into sight
    QImage render(const std::vector<QRect> &rects) {
        QImage image(kWidth, kHeight, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        for (const QRect &rect : rects) {
            painter.save();
            painter.setClipRect(rect);
            m_layer->paint(m_pane, painter, rect);
            painter.restore();
        }
        painter.end();
        return image;
    }

    static bool rowIsWhite(const QImage &image, int y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != qRgb(255, 255, 255)) return false;
        }
        return true;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void init() {
        m_viewManager = new sv::ViewManager;
        m_paneStack = new sv::PaneStack(nullptr, m_viewManager);
        m_document = new sv::Document;
        m_document->setMainModel(makeAudioModel());
        m_pane = m_paneStack->addPane();
        m_pane->resize(kWidth, kHeight);
        m_pane->setZoomLevel(sv::ZoomLevel(sv::ZoomLevel::FramesPerPixel,
                                           int(kRate / 100)));
        m_pane->setStartFrame(0);

        auto model = std::make_shared<sv::RegionModel>(kRate, 1);
        sv::ModelId id = sv::ModelById::add(model);
        m_document->addNonDerivedModel(id);
        m_layer = qobject_cast<sv::RegionLayer *>
            (m_document->createLayer(sv::LayerFactory::Regions));
        QVERIFY(m_layer);
        m_document->setModel(m_layer, id);
        m_layer->setPlotStyle(sv::RegionLayer::PlotLyrics);
        m_document->attachLayerToView(m_pane, m_layer);
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
        m_pane = nullptr;
        m_layer = nullptr;
    }

    // --- Where labels go -------------------------------------------------

    void labels_that_fit_share_the_first_row() {
        Spans spans { { 0, 10 }, { 20, 10 }, { 40, 10 } };
        QCOMPARE(sv::RegionLayer::assignLabelRows(spans, 2, 4),
                 std::vector<int>({ 0, 0, 0 }));
    }

    void a_label_that_overlaps_goes_to_the_next_row() {
        // The second runs into the first; the third clears the first
        Spans spans { { 0, 30 }, { 10, 30 }, { 45, 10 } };
        QCOMPARE(sv::RegionLayer::assignLabelRows(spans, 2, 4),
                 std::vector<int>({ 0, 1, 0 }));
    }

    void a_label_with_no_room_in_any_row_is_left_out() {
        Spans spans { { 0, 50 }, { 10, 50 }, { 20, 50 }, { 70, 10 } };
        QCOMPARE(sv::RegionLayer::assignLabelRows(spans, 2, 4),
                 std::vector<int>({ 0, 1, -1, 0 }));
    }

    void the_gap_is_the_least_space_between_labels() {
        QCOMPARE(sv::RegionLayer::assignLabelRows({ { 0, 10 }, { 14, 10 } }, 2, 4),
                 std::vector<int>({ 0, 0 }));
        QCOMPARE(sv::RegionLayer::assignLabelRows({ { 0, 10 }, { 13.5, 10 } }, 2, 4),
                 std::vector<int>({ 0, 1 }));
    }

    void no_labels_and_no_rows() {
        QCOMPARE(sv::RegionLayer::assignLabelRows({}, 2, 4), std::vector<int>());
        QCOMPARE(sv::RegionLayer::assignLabelRows({ { 0, 10 } }, 0, 4),
                 std::vector<int>({ -1 }));
    }

    // --- The layer --------------------------------------------------------

    void lyrics_take_no_vertical_scale_and_no_edits() {
        // Like the coverage strip: nothing in the pane may align its
        // scale to the lyrics, and no tool may edit them
        QVERIFY(!m_layer->isLayerEditable());
        QVERIFY(m_layer->getVerticalExtents().first ==
                sv::Layer::NO_VERTICAL_EXTENTS.first);
        addWords(4);
        QPoint pos(10, 10);
        QCOMPARE(m_layer->getFeatureDescription(m_pane, pos), QString());
    }

    void lyrics_are_drawn_only_along_the_top() {
        addWords(20);
        QImage image = render({ QRect(0, 0, kWidth, kHeight) });

        bool drawn = false;
        for (int y = 0; y < kHeight / 4; ++y) {
            if (!rowIsWhite(image, y)) drawn = true;
        }
        QVERIFY2(drawn, "nothing was drawn along the top of the pane");

        for (int y = kHeight / 2; y < kHeight; ++y) {
            QVERIFY2(rowIsWhite(image, y),
                     qPrintable(QString("something was drawn at y = %1, "
                                        "below the band of the lyrics").arg(y)));
        }
    }

    void painting_in_strips_matches_painting_whole() {
        // A view that scrolls repaints only the strip that comes into
        // sight; the labels in it must be where they were when the
        // whole view was painted, or the text breaks at the seams.
        // With words this close some go to the second row and some are
        // left out, which is what depends on the neighbours
        addWords(24);
        QImage whole = render({ QRect(0, 0, kWidth, kHeight) });
        QImage strips = render({ QRect(0, 0, 230, kHeight),
                                 QRect(230, 0, 310, kHeight),
                                 QRect(540, 0, kWidth - 540, kHeight) });
        QVERIFY2(whole == strips,
                 "the layer painted in strips differs from the layer painted whole");

        // And again after the view has scrolled on by part of a label
        m_pane->setStartFrame(sv::sv_frame_t(kRate * 0.37));
        whole = render({ QRect(0, 0, kWidth, kHeight) });
        strips = render({ QRect(0, 0, 400, kHeight),
                          QRect(400, 0, kWidth - 400, kHeight) });
        QVERIFY2(whole == strips,
                 "after scrolling, the layer painted in strips differs from "
                 "the layer painted whole");
    }
};

#endif
