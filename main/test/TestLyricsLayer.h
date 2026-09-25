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
// draws the words of the lyrics along the bottom of pane 0. Where each
// box goes and how big its font is are worked out by pure functions,
// tested first; then the layer is painted into images, with no
// MainWindow.

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
#include <QSignalSpy>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <cmath>
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
    static constexpr int kHeight = 300;

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

    // Words two seconds apart and a second and a half long, with short
    // labels: at the tests' zoom each box is 150 pixels of region with
    // its label well inside it, and nothing is left out
    void addSpacedWords(int count) {
        auto model = sv::ModelById::getAs<sv::RegionModel>(m_layer->getModel());
        QVERIFY(model);
        for (int i = 0; i < count; ++i) {
            model->add(sv::Event(sv::sv_frame_t(kRate * 2.0 * i), 0.f,
                                 sv::sv_frame_t(kRate * 1.5),
                                 QString("w%1").arg(i)));
        }
    }

    sv::Event spacedWord(int i) {
        return sv::Event(sv::sv_frame_t(kRate * 2.0 * i), 0.f,
                         sv::sv_frame_t(kRate * 1.5), QString("w%1").arg(i));
    }

    // The columns in which two images differ, as [first, last], or
    // (-1, -1) if they are the same
    static std::pair<int, int> differingColumns(const QImage &a, const QImage &b) {
        int first = -1, last = -1;
        for (int x = 0; x < a.width(); ++x) {
            for (int y = 0; y < a.height(); ++y) {
                if (a.pixel(x, y) != b.pixel(x, y)) {
                    if (first < 0) first = x;
                    last = x;
                    break;
                }
            }
        }
        return { first, last };
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

    void a_label_reaching_back_over_the_one_before_goes_to_the_next_row() {
        // A long label centred on a short region can start left of the
        // label of the region before it
        Spans spans { { 100, 10 }, { 60, 100 }, { 115, 10 } };
        QCOMPARE(sv::RegionLayer::assignLabelRows(spans, 2, 4),
                 std::vector<int>({ 0, 1, 0 }));
    }

    void a_box_is_its_region_or_its_label_centred_on_the_region() {
        typedef std::pair<double, double> Span;
        QCOMPARE(sv::RegionLayer::getLyricsBoxSpan(100, 200, 50), Span(100, 100));
        QCOMPARE(sv::RegionLayer::getLyricsBoxSpan(100, 200, 100), Span(100, 100));
        QCOMPARE(sv::RegionLayer::getLyricsBoxSpan(100, 120, 60), Span(80, 60));
    }

    void the_font_grows_with_the_zoom_from_twice_to_four_times() {
        // Base 13 pixels in a tall view: 26 to 52, a pixel for every 10
        // pixels per second in between
        QCOMPARE(sv::RegionLayer::getLyricsFontPixelSize(50, 13, 800), 26);
        QCOMPARE(sv::RegionLayer::getLyricsFontPixelSize(300, 13, 800), 30);
        QCOMPARE(sv::RegionLayer::getLyricsFontPixelSize(400, 13, 800), 40);
        QCOMPARE(sv::RegionLayer::getLyricsFontPixelSize(5000, 13, 800), 52);
        // Never more than an eighth of the view, nor less than its font
        QCOMPARE(sv::RegionLayer::getLyricsFontPixelSize(5000, 13, 200), 25);
        QCOMPARE(sv::RegionLayer::getLyricsFontPixelSize(50, 13, 200), 25);
        QCOMPARE(sv::RegionLayer::getLyricsFontPixelSize(50, 13, 40), 13);

        int last = 0;
        for (double pps = 10; pps < 2000; pps *= 1.3) {
            int size = sv::RegionLayer::getLyricsFontPixelSize(pps, 13, 600);
            QVERIFY2(size >= last, "the font got smaller as the view zoomed in");
            last = size;
        }
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

    void lyrics_are_drawn_only_along_the_bottom() {
        addWords(20);
        QImage image = render({ QRect(0, 0, kWidth, kHeight) });

        bool drawn = false;
        for (int y = kHeight * 3 / 4; y < kHeight; ++y) {
            if (!rowIsWhite(image, y)) drawn = true;
        }
        QVERIFY2(drawn, "nothing was drawn along the bottom of the pane");

        for (int y = 0; y < kHeight / 2; ++y) {
            QVERIFY2(rowIsWhite(image, y),
                     qPrintable(QString("something was drawn at y = %1, "
                                        "above the band of the lyrics").arg(y)));
        }

        // The coverage strip's band, the bottom six pixels, is left to it
        for (int y = kHeight - m_pane->scalePixelSize(6); y < kHeight; ++y) {
            QVERIFY2(rowIsWhite(image, y),
                     qPrintable(QString("something was drawn at y = %1, "
                                        "where the coverage strip goes").arg(y)));
        }
    }

    void a_label_is_centred_in_its_box() {
        addSpacedWords(3);
        QImage image = render({ QRect(0, 0, kWidth, kHeight) });

        // The dark pixels of the text of the second word, which spans
        // x = 200 to 350, above the bars
        int x0 = m_pane->getXForFrame(spacedWord(1).getFrame());
        int x1 = m_pane->getXForFrame(spacedWord(1).getFrame() +
                                      spacedWord(1).getDuration());
        int left = -1, right = -1;
        for (int x = x0; x <= x1; ++x) {
            for (int y = kHeight / 2; y < kHeight - 12; ++y) {
                if (qGray(image.pixel(x, y)) < 100) {
                    if (left < 0) left = x;
                    right = x;
                    break;
                }
            }
        }
        QVERIFY2(left >= 0, "no text was found in the box");
        double textMiddle = (left + right) / 2.0;
        double boxMiddle = (x0 + x1) / 2.0;
        QVERIFY2(std::fabs(textMiddle - boxMiddle) <= 3.0,
                 qPrintable(QString("the text is centred on x = %1, the box "
                                    "on x = %2").arg(textMiddle).arg(boxMiddle)));
    }

    void the_highlight_follows_the_frame_and_repaints_only_for_a_new_word() {
        addSpacedWords(4);
        QSignalSpy repaints(m_layer, &sv::Layer::layerParametersChanged);
        sv::Event e(0);

        m_layer->setHighlightFrame(sv::sv_frame_t(kRate * 2.1));
        QCOMPARE(int(repaints.count()), 1);
        QVERIFY(m_layer->getHighlightedEvent(e));
        QVERIFY(e == spacedWord(1));

        // Within the same word: nothing to paint again
        m_layer->setHighlightFrame(sv::sv_frame_t(kRate * 3.0));
        QCOMPARE(int(repaints.count()), 1);

        // Between words: none
        m_layer->setHighlightFrame(sv::sv_frame_t(kRate * 3.7));
        QCOMPARE(int(repaints.count()), 2);
        QVERIFY(!m_layer->getHighlightedEvent(e));
        m_layer->setHighlightFrame(-1);
        QCOMPARE(int(repaints.count()), 2);

        m_layer->setHighlightFrame(sv::sv_frame_t(kRate * 4.0));
        QCOMPARE(int(repaints.count()), 3);
        QVERIFY(m_layer->getHighlightedEvent(e));
        QVERIFY(e == spacedWord(2));
    }

    void the_highlighted_word_is_drawn_differently_and_nothing_else_is() {
        addSpacedWords(4);
        QImage plain = render({ QRect(0, 0, kWidth, kHeight) });

        m_layer->setHighlightFrame(sv::sv_frame_t(kRate * 2.1));
        QImage highlighted = render({ QRect(0, 0, kWidth, kHeight) });

        auto columns = differingColumns(plain, highlighted);
        QVERIFY2(columns.first >= 0, "the highlighted word looks the same");
        int x0 = m_pane->getXForFrame(spacedWord(1).getFrame());
        int x1 = m_pane->getXForFrame(spacedWord(1).getFrame() +
                                      spacedWord(1).getDuration());
        QVERIFY2(columns.first >= x0 && columns.second <= x1,
                 qPrintable(QString("pixels changed from x = %1 to %2, outside "
                                    "the word's box at %3 to %4")
                            .arg(columns.first).arg(columns.second)
                            .arg(x0).arg(x1)));
    }

    // The highlight's box colour, and nothing else drawn is like it
    static int highlightPixels(const QImage &image) {
        int n = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                QRgb p = image.pixel(x, y);
                if (qRed(p) > 240 && qGreen(p) > 195 && qGreen(p) < 225 &&
                    qBlue(p) > 100 && qBlue(p) < 150) ++n;
            }
        }
        return n;
    }

    void the_highlighted_word_is_shown_even_with_no_room_of_its_own() {
        // Words this close fill both rows, and the third has no room
        addWords(3);
        QImage plain = render({ QRect(0, 0, kWidth, kHeight) });
        QCOMPARE(highlightPixels(plain), 0);

        m_layer->setHighlightFrame(sv::sv_frame_t(kRate * 0.55));
        sv::Event e(0);
        QVERIFY(m_layer->getHighlightedEvent(e));
        QCOMPARE(e.getLabel(), QString("sanaseppo2"));
        QImage highlighted = render({ QRect(0, 0, kWidth, kHeight) });
        QVERIFY2(highlightPixels(highlighted) > 100,
                 "the word being sung was not drawn: it had no row");
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
