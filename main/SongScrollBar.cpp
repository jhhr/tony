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

#include "SongScrollBar.h"

#include "view/Pane.h"
#include "view/PaneStack.h"
#include "view/ViewManager.h"
#include "data/model/SparseTimeValueModel.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace sv;

SongScrollBar::SongScrollBar(ViewManager *viewManager, PaneStack *paneStack,
                             QWidget *parent) :
    QWidget(parent),
    m_viewManager(viewManager),
    m_paneStack(paneStack)
{
    setObjectName("Song Scroll Bar");
    setFixedHeight(stripHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Long enough that pYIN's steps make a few contours a second, not
    // one for each block it writes
    m_contourTimer.setSingleShot(true);
    m_contourTimer.setInterval(250);
    connect(&m_contourTimer, &QTimer::timeout,
            this, &SongScrollBar::contourTimedOut);

    m_checkTimer.setSingleShot(true);
    m_checkTimer.setInterval(0);
    connect(&m_checkTimer, &QTimer::timeout,
            this, &SongScrollBar::checkPositions);

    if (viewManager) {
        connect(viewManager, &ViewManager::globalCentreFrameChanged,
                this, [this]() { scheduleCheck(); });
        connect(viewManager, qOverload<View *, sv_frame_t>
                (&ViewManager::viewCentreFrameChanged),
                this, [this]() { scheduleCheck(); });
        connect(viewManager, qOverload<View *, ZoomLevel, bool>
                (&ViewManager::viewZoomLevelChanged),
                this, [this]() { scheduleCheck(); });
        // Every 20 ms while playing or recording, which is also how a
        // page turned by playback is seen
        connect(viewManager, &ViewManager::playbackFrameChanged,
                this, [this]() { scheduleCheck(); });
    }
}

SongScrollBar::~SongScrollBar()
{
}

QSize
SongScrollBar::sizeHint() const
{
    return QSize(200, stripHeight);
}

void
SongScrollBar::setSongModel(ModelId model)
{
    if (model == m_songModel) return;
    m_songModel = model;
    m_contourStale = true;
    update();
}

void
SongScrollBar::setPitchModel(ModelId model)
{
    if (!ModelById::isa<SparseTimeValueModel>(model)) model = {};
    if (model == m_pitchModel) return;

    if (auto previous = ModelById::get(m_pitchModel)) {
        disconnect(previous.get(), nullptr, this, nullptr);
    }

    m_pitchModel = model;

    // pYIN writes to the model from its own thread: these are queued.
    // A model released without anyone saying so leaves no contour behind
    if (auto pitch = ModelById::get(m_pitchModel)) {
        connect(pitch.get(), &Model::modelChanged,
                this, &SongScrollBar::pitchModelChanged);
        connect(pitch.get(), &Model::modelChangedWithin,
                this, &SongScrollBar::pitchModelChanged);
        connect(pitch.get(), &QObject::destroyed,
                this, &SongScrollBar::pitchModelChanged);
    }

    m_contourTimer.stop();
    m_contourStale = true;
    update();
}

sv_frame_t
SongScrollBar::getSongFrames() const
{
    auto model = ModelById::get(m_songModel);
    if (!model) return 0;
    return std::max(model->getEndFrame(), sv_frame_t(0));
}

Pane *
SongScrollBar::pane() const
{
    if (!m_paneStack || m_paneStack->getPaneCount() == 0) return nullptr;
    return m_paneStack->getPane(0);
}

SongScroll::Thumb
SongScrollBar::currentThumb() const
{
    Pane *p = pane();
    sv_frame_t song = getSongFrames();
    if (!p || song <= 0) return {};
    return SongScroll::thumb(p->getStartFrame(), p->getEndFrame(),
                             song, width(), minThumbWidth);
}

int
SongScrollBar::playheadX() const
{
    sv_frame_t song = getSongFrames();
    if (!m_viewManager || song <= 0 || width() <= 0) return -1;
    double x = SongScroll::xForFrame(m_viewManager->getPlaybackFrame(),
                                     song, width());
    return std::clamp(int(std::lround(x)), 0, width() - 1);
}

void
SongScrollBar::scheduleCheck()
{
    // Hidden, as on the desktop, it has nothing to follow
    if (isVisible() && !m_checkTimer.isActive()) m_checkTimer.start();
}

void
SongScrollBar::checkPositions()
{
    SongScroll::Thumb thumb = currentThumb();
    bool withThumb = (thumb.width() > 0.0);
    if (withThumb != m_paintedWithThumb ||
        std::lround(thumb.x0) != std::lround(m_paintedThumb.x0) ||
        std::lround(thumb.x1) != std::lround(m_paintedThumb.x1) ||
        playheadX() != m_paintedPlayheadX) {
        update();
    }
}

void
SongScrollBar::pitchModelChanged()
{
    if (!m_contourTimer.isActive()) m_contourTimer.start();
}

void
SongScrollBar::contourTimedOut()
{
    m_contourStale = true;
    update();
}

void
SongScrollBar::changeEvent(QEvent *e)
{
    // The contour is drawn in the palette's colours: dark mode
    if (e->type() == QEvent::PaletteChange) {
        m_contourStale = true;
        update();
    }
    QWidget::changeEvent(e);
}

void
SongScrollBar::rebuildContour(sv_frame_t songFrames)
{
    m_contourStale = false;
    m_contourSongFrames = songFrames;
    m_columns.clear();

    qreal ratio = devicePixelRatioF();
    int w = int(std::lround(width() * ratio));
    int h = int(std::lround(height() * ratio));
    if (w <= 0 || h <= 0) {
        m_contour = QImage();
        return;
    }

    m_contour = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    m_contour.fill(Qt::transparent);

    auto pitch = ModelById::getAs<SparseTimeValueModel>(m_pitchModel);
    if (pitch && songFrames > 0) {
        std::vector<std::pair<sv_frame_t, double>> events;
        EventVector all = pitch->getAllEvents();
        events.reserve(all.size());
        for (const Event &e : all) {
            events.push_back({ e.getFrame(), double(e.getValue()) });
        }
        m_columns = SongScroll::pitchColumns
            (events, pitch->getResolution(), songFrames, w);
    }

    double low = 0.0, high = 0.0;
    if (SongScroll::pitchRange(m_columns, low, high)) {

        // Faint, and clear of the thumb's outline at the top and bottom
        QColor colour = palette().color(QPalette::WindowText);
        colour.setAlpha(130);
        double margin = 4.0 * ratio;
        double band = h - 2.0 * margin;
        int thinnest = std::max(1, int(std::lround(ratio)));

        QPainter paint(&m_contour);
        for (int c = 0; c < int(m_columns.size()); ++c) {
            const SongScroll::Column &column = m_columns[c];
            if (column.isEmpty()) continue;
            int top = int(std::floor
                          (margin + SongScroll::yForPitch
                           (column.high, low, high, band)));
            int bottom = int(std::ceil
                             (margin + SongScroll::yForPitch
                              (column.low, low, high, band)));
            bottom = std::max(bottom, top + thinnest);
            paint.fillRect(c, top, 1, bottom - top, colour);
        }
    }

    m_contour.setDevicePixelRatio(ratio);
}

void
SongScrollBar::paintEvent(QPaintEvent *)
{
    QPainter paint(this);
    paint.fillRect(rect(), palette().color(QPalette::Base));

    sv_frame_t song = getSongFrames();
    qreal ratio = devicePixelRatioF();
    QSize size(int(std::lround(width() * ratio)),
               int(std::lround(height() * ratio)));
    if (m_contourStale || song != m_contourSongFrames ||
        m_contour.size() != size || m_contour.devicePixelRatio() != ratio) {
        rebuildContour(song);
    }
    if (!m_contour.isNull()) {
        paint.drawImage(QPointF(0.0, 0.0), m_contour);
    }

    // What the panes do not show dimmed, as the Overview has it
    SongScroll::Thumb thumb = currentThumb();
    bool withThumb = (thumb.width() > 0.0);
    if (withThumb) {
        QColor dim = palette().color(QPalette::Window);
        dim.setAlpha(160);
        paint.fillRect(QRectF(0.0, 0.0, thumb.x0, height()), dim);
        paint.fillRect(QRectF(thumb.x1, 0.0, width() - thumb.x1, height()),
                       dim);

        paint.setRenderHint(QPainter::Antialiasing, true);
        paint.setPen(QPen(palette().color(QPalette::Highlight), 2.0));
        paint.setBrush(Qt::NoBrush);
        paint.drawRoundedRect(QRectF(thumb.x0, 0.0, thumb.width(), height())
                              .adjusted(1.0, 1.0, -1.0, -1.0), 3.0, 3.0);
        paint.setRenderHint(QPainter::Antialiasing, false);
    }

    int playhead = playheadX();
    if (playhead >= 0) {
        paint.setPen(QPen(palette().color(QPalette::WindowText), 1.0));
        paint.drawLine(playhead, 0, playhead, height());
    }

    m_paintedThumb = thumb;
    m_paintedWithThumb = withThumb;
    m_paintedPlayheadX = playhead;
}

void
SongScrollBar::moveCentreTo(sv_frame_t centre)
{
    // As the Overview does: every pane that follows the global centre
    // (all of Tony's) goes with it
    if (m_viewManager) m_viewManager->setGlobalCentreFrame(centre);
}

void
SongScrollBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;

    Pane *p = pane();
    sv_frame_t song = getSongFrames();
    if (!p || song <= 0) return;

    m_dragging = true;
    m_grabX = e->position().x();

    // On the thumb, a drag moves the panes from where they are. Anywhere
    // else they go there first, and a drag moves them on from there
    if (SongScroll::hitsThumb(currentThumb(), m_grabX)) {
        m_grabbedCentre = p->getCentreFrame();
    } else {
        m_grabbedCentre = SongScroll::jumpCentre(m_grabX, song, width());
        moveCentreTo(m_grabbedCentre);
    }
}

void
SongScrollBar::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging) return;

    // The session may have gone from under the finger
    sv_frame_t song = getSongFrames();
    if (!pane() || song <= 0) {
        m_dragging = false;
        return;
    }

    moveCentreTo(SongScroll::dragCentre(m_grabbedCentre, m_grabX,
                                        e->position().x(), song, width()));
}

void
SongScrollBar::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    if (m_dragging) mouseMoveEvent(e);
    m_dragging = false;
}
