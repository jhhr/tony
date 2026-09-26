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

#ifndef TEST_PLOT_SIZE_H
#define TEST_PLOT_SIZE_H

// Tier 3: how large the panes draw the pitch tracks and the notes (the
// svgui fork's plot scale, which a pane applies with its pixel ratio),
// and View > Plot Size (PlotSize). The layers are painted into images
// as a pane paints them into its buffer, through a ViewProxy whose
// pixels are the buffer's, at ratio 1 and at a phone's ratio 3. No
// MainWindow; the menu in the window is TestCompactLayout's.

#include "../PlotSize.h"

#include "view/Pane.h"
#include "view/ViewManager.h"
#include "view/ViewProxy.h"
#include "layer/TimeValueLayer.h"
#include "layer/FlexiNoteLayer.h"
#include "layer/ColourDatabase.h"
#include "layer/CoordinateScale.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/NoteModel.h"

#include <QObject>
#include <QtTest>
#include <QAction>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>

#include <cmath>
#include <memory>
#include <utility>

class TestPlotSize : public QObject
{
    Q_OBJECT

    static constexpr double kRate = 44100.0;
    static constexpr int kHop = 256;
    static constexpr int kWidth = 400;
    static constexpr int kHeight = 200;

    sv::ViewManager *m_viewManager = nullptr;
    sv::Pane *m_pane = nullptr;
    sv::TimeValueLayer *m_pitch = nullptr;
    sv::FlexiNoteLayer *m_notes = nullptr;
    sv::ModelId m_pitchModel;
    sv::ModelId m_noteModel;

    // Columns of the pane, in logical pixels: one through the first
    // note's pitch, one through the middle of that note, well away from
    // its ends
    static constexpr int kPitchX = 50;
    static constexpr int kNoteX = 100;

    static sv::sv_frame_t frames(double seconds) {
        return sv::sv_frame_t(seconds * kRate);
    }

    static QColor colourOf(sv::SingleColourLayer *layer) {
        return sv::ColourDatabase::getInstance()->getColour
            (layer->getBaseColour());
    }

    // The layer painted into a white image as the pane paints it into
    // its buffer at this pixel ratio
    QImage render(sv::Layer *layer, int ratio) {
        QImage image(kWidth * ratio, kHeight * ratio,
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        sv::ViewProxy proxy(m_pane, ratio);
        layer->paint(&proxy, painter, image.rect());
        painter.end();
        return image;
    }

    // The first and last rows drawn in a column, or (-1, -1)
    static std::pair<int, int> drawnRows(const QImage &image, int x) {
        int first = -1, last = -1;
        for (int y = 0; y < image.height(); ++y) {
            if (image.pixel(x, y) != qRgb(255, 255, 255)) {
                if (first < 0) first = y;
                last = y;
            }
        }
        return { first, last };
    }

    static int drawnHeight(const QImage &image, int x) {
        auto rows = drawnRows(image, x);
        return rows.first < 0 ? 0 : rows.second - rows.first + 1;
    }

    // The middle of a logical column, in the pixels of an image at a
    // ratio
    static int column(int x, int ratio) {
        return x * ratio + ratio / 2;
    }

    // Whether the note tool, with the pointer at this point of the pane,
    // would edit the note there: the pointer changes to the cursor of an
    // edit, and to the plain arrow off the note
    bool hitsNote(int x, int y) {
        m_pane->setCursor(Qt::WaitCursor);
        QMouseEvent e(QEvent::MouseMove, QPointF(x, y), QPointF(x, y),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        m_notes->mouseMoveEvent(m_pane, &e);
        Qt::CursorShape shape = m_pane->cursor().shape();
        return shape != Qt::ArrowCursor && shape != Qt::WaitCursor;
    }

    static void forgetSetting() {
        QSettings settings;
        settings.beginGroup("MainWindow");
        settings.remove("plotsize");
        settings.endGroup();
    }

private slots:
    void init() {
        forgetSetting();

        m_viewManager = new sv::ViewManager();
        m_pane = new sv::Pane();
        m_pane->setViewManager(m_viewManager);
        m_pane->resize(kWidth, kHeight);
        m_pane->setZoomLevel(sv::ZoomLevel(sv::ZoomLevel::FramesPerPixel,
                                           kHop));
        m_pane->setStartFrame(0);

        // A second at 220 Hz and a second at 330 Hz, a pitch at every
        // hop and a note for each; the scales run from 100 to 500 Hz,
        // which keeps them clear of the edges
        auto pitch = std::make_shared<sv::SparseTimeValueModel>
            (kRate, kHop, 100.f, 500.f, false);
        pitch->setScaleUnits("Hz");
        for (sv::sv_frame_t f = 0; f < frames(2.0); f += kHop) {
            pitch->add(sv::Event(f, f < frames(1.0) ? 220.f : 330.f, ""));
        }
        auto notes = std::make_shared<sv::NoteModel>
            (kRate, kHop, 100.f, 500.f, false, sv::NoteModel::FLEXI_NOTE);
        notes->setScaleUnits("Hz");
        notes->add(sv::Event(0, 220.f, frames(1.0), 1.f, ""));
        notes->add(sv::Event(frames(1.0), 330.f, frames(1.0), 1.f, ""));
        m_pitchModel = sv::ModelById::add(pitch);
        m_noteModel = sv::ModelById::add(notes);

        m_pitch = new sv::TimeValueLayer();
        m_pitch->setModel(m_pitchModel);
        m_pitch->setPlotStyle(sv::TimeValueLayer::PlotPoints);
        m_pitch->setVerticalScale(sv::TimeValueLayer::LinearScale);
        m_notes = new sv::FlexiNoteLayer();
        m_notes->setModel(m_noteModel);
        // As in Tony, the notes take the scale of the pitch, which
        // has one of its own
        m_pane->addLayer(m_pitch);
        m_pane->addLayer(m_notes);
    }

    void cleanup() {
        delete m_pane;
        m_pane = nullptr;
        delete m_pitch;
        delete m_notes;
        m_pitch = nullptr;
        m_notes = nullptr;
        sv::ModelById::release(m_pitchModel);
        sv::ModelById::release(m_noteModel);
        delete m_viewManager;
        m_viewManager = nullptr;
        forgetSetting();
    }

    // At ratio 1 and the normal size the layers draw what they drew
    // before there was a plot scale, pixel for pixel: a point is a
    // rectangle two pixels high in the view's pen, a note one sixteen
    // high in a pen of a pixel. (A whole pane of Tony's, with its
    // waveform, pitch and notes, was compared pixel for pixel before
    // and after the change to the fork, once.)
    void desktop_draws_as_before() {
        sv::CoordinateScale pitchScale =
            m_pane->getEffectiveVerticalExtentsForLayer(m_pitch);
        sv::CoordinateScale noteScale =
            m_pane->getEffectiveVerticalExtentsForLayer(m_notes);

        QImage pitch(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
        pitch.fill(Qt::white);
        {
            QPainter paint(&pitch);
            QColor colour = colourOf(m_pitch);
            QColor fill = colour;
            fill.setAlpha(80);
            paint.setPen(QPen(colour, m_pane->scalePenWidth(1.0)));
            paint.setBrush(fill);
            auto model = sv::ModelById::getAs<sv::SparseTimeValueModel>
                (m_pitchModel);
            int w = m_pane->getXForFrame(kHop) - m_pane->getXForFrame(0);
            if (w < 1) w = 1;
            for (const auto &e : model->getAllEvents()) {
                int x = m_pane->getXForFrame(e.getFrame());
                int y = pitchScale.getCoordForValueRounded(m_pane, e.getValue());
                paint.drawRect(x, y - 1, w, 2);
            }
        }

        QImage notes(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
        notes.fill(Qt::white);
        {
            QPainter paint(&notes);
            QColor colour = colourOf(m_notes);
            QColor fill = colour;
            fill.setAlpha(80);
            paint.setPen(colour);
            paint.setBrush(fill);
            auto model = sv::ModelById::getAs<sv::NoteModel>(m_noteModel);
            for (const auto &e : model->getAllEvents()) {
                int x = m_pane->getXForFrame(e.getFrame());
                int w = m_pane->getXForFrame(e.getFrame() + e.getDuration()) - x;
                int y = noteScale.getCoordForValueRounded(m_pane, e.getValue());
                paint.drawRect(x, y - 8, w, 16);
            }
        }

        QCOMPARE(drawnHeight(pitch, kPitchX), 3);
        QCOMPARE(drawnHeight(notes, kNoteX), 17);
        QVERIFY(render(m_pitch, 1) == pitch);
        QVERIFY(render(m_notes, 1) == notes);
    }

    // What is drawn keeps its size in logical pixels: at a phone's ratio
    // 3, three times the pixels of ratio 1; and the plot size makes it
    // larger again, at either ratio
    void sizes_follow_the_ratio_and_the_plot_size() {
        for (double scale : { 1.0, 1.5, 2.0 }) {
            m_viewManager->setPlotScale(scale);
            for (int ratio : { 1, 3 }) {
                double size = ratio * scale;
                int point = drawnHeight(render(m_pitch, ratio),
                                        column(kPitchX, ratio));
                int note = drawnHeight(render(m_notes, ratio),
                                       column(kNoteX, ratio));
                QString where = QString("at ratio %1 and plot scale %2: a "
                                        "point %3 pixels high, a note %4")
                    .arg(ratio).arg(scale).arg(point).arg(note);
                // Two pixels and the pen's one, a note's sixteen and
                // the pen's one, all times the size: to a pixel, which
                // a thick pen may round either way
                QVERIFY2(std::abs(point - 3 * size) <= 1, qPrintable(where));
                QVERIFY2(std::abs(note - 17 * size) <= 1, qPrintable(where));
            }
        }
    }

    // The note tool picks a note where it is drawn, in the pane's own
    // (logical) coordinates, at either ratio and every plot size
    void note_hit_area_is_what_is_drawn() {
        for (double scale : { 1.0, 1.5, 2.0 }) {
            m_viewManager->setPlotScale(scale);

            int hitTop = -1, hitBottom = -1;
            for (int y = 0; y < kHeight; ++y) {
                if (hitsNote(kNoteX, y)) {
                    if (hitTop < 0) hitTop = y;
                    hitBottom = y;
                }
            }
            QVERIFY(hitTop >= 0);

            for (int ratio : { 1, 3 }) {
                auto rows = drawnRows(render(m_notes, ratio),
                                      column(kNoteX, ratio));
                // Edges in logical pixels; the drawing's are the pen's
                // outer edges, half a pen outside the note
                double drawnTop = double(rows.first) / ratio;
                double drawnBottom = double(rows.second + 1) / ratio;
                QString where = QString("at ratio %1 and plot scale %2: "
                                        "drawn from %3 to %4, picked from "
                                        "%5 to %6")
                    .arg(ratio).arg(scale).arg(drawnTop).arg(drawnBottom)
                    .arg(hitTop).arg(hitBottom + 1);
                QVERIFY2(std::abs(hitTop - drawnTop) <= scale,
                         qPrintable(where));
                QVERIFY2(std::abs(hitBottom + 1 - drawnBottom) <= scale,
                         qPrintable(where));
            }
        }
    }

    // --- View > Plot Size -------------------------------------------------

    void plot_size_starts_at_the_default() {
        PlotSize plotSize(m_viewManager, nullptr);
        QCOMPARE(PlotSize::getDefaultPercent(), 100); // 150 on Android
        QCOMPARE(plotSize.getPercent(), 100);
        QCOMPARE(m_viewManager->getPlotScale(), 1.0);

        QStringList texts, checked;
        for (QAction *action : plotSize.getActions()) {
            texts << action->text();
            if (action->isChecked()) checked << action->text();
        }
        QCOMPARE(texts, QStringList({ "100%", "150%", "200%" }));
        QCOMPARE(checked, QStringList({ "100%" }));

        // Nothing is stored until a step is chosen
        QVERIFY(!QSettings().contains("MainWindow/plotsize"));
    }

    void a_step_applies_at_once_and_is_remembered() {
        PlotSize plotSize(m_viewManager, nullptr);
        QSignalSpy changed(m_viewManager, &sv::ViewManager::plotScaleChanged);

        plotSize.getActions().at(1)->trigger();
        QCOMPARE(m_viewManager->getPlotScale(), 1.5);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(plotSize.getPercent(), 150);
        QVERIFY(plotSize.getActions().at(1)->isChecked());
        QVERIFY(!plotSize.getActions().at(0)->isChecked());
        QCOMPARE(QSettings().value("MainWindow/plotsize").toInt(), 150);

        // The next start takes it up
        sv::ViewManager other;
        PlotSize again(&other, nullptr);
        QCOMPARE(again.getPercent(), 150);
        QCOMPARE(other.getPlotScale(), 1.5);
        QVERIFY(again.getActions().at(1)->isChecked());
    }

    void only_the_steps_are_taken() {
        PlotSize plotSize(m_viewManager, nullptr);
        plotSize.setPercent(175);
        QCOMPARE(plotSize.getPercent(), 100);
        QCOMPARE(m_viewManager->getPlotScale(), 1.0);
        QVERIFY(!QSettings().contains("MainWindow/plotsize"));

        for (QVariant stored : { QVariant(175), QVariant("large") }) {
            QSettings().setValue("MainWindow/plotsize", stored);
            sv::ViewManager other;
            PlotSize again(&other, nullptr);
            QCOMPARE(again.getPercent(), 100);
            QCOMPARE(other.getPlotScale(), 1.0);
        }
    }
};

#endif
