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

#ifndef TONY_SONG_SCROLL_BAR_H
#define TONY_SONG_SCROLL_BAR_H

#include "SongScroll.h"

#include "base/BaseTypes.h"
#include "data/model/Model.h"

#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <vector>

namespace sv {
class Pane;
class PaneStack;
class ViewManager;
}

/**
 * A thin strip that stands in for svgui's Overview in the compact
 * layout, where the Overview takes too much of a phone's height: the
 * whole song across its width, a faint contour of the reference's pitch
 * along it (so that the parts that are sung can be told from the rests),
 * a thumb for what the panes show, the rest dimmed, and the playhead.
 *
 * Pressing on the thumb and dragging moves the panes by as much as the
 * drag; pressing elsewhere centres them there, and a drag carries on
 * from that point. The panes are moved through the ViewManager, as the
 * Overview does, so that every pane follows. The playhead is left where
 * it is. A finger arrives as Qt's mouse events.
 *
 * The contour is drawn into an image at the screen's pixel ratio, made
 * again only when the pitch model has changed (pYIN fills it in steps:
 * the changes are gathered for a while first), when the strip's size or
 * the ratio changes, or the song's length. A paint is the image, the
 * thumb and the playhead.
 *
 * The thumb follows the panes' scrolling and zoom, and playback's
 * paging, which moves the panes without telling anyone: the first pane's
 * start and end frames are read afresh each time the playhead moves, and
 * the strip repainted if the thumb or the playhead moved by a pixel.
 *
 * MainWindow creates it hidden, hands it the song's audio model and the
 * reference's pitch model whenever they change, and CompactLayout shows
 * it (CompactLayout::Parts::shownWidgets).
 */
class SongScrollBar : public QWidget
{
    Q_OBJECT

public:
    SongScrollBar(sv::ViewManager *viewManager, sv::PaneStack *paneStack,
                  QWidget *parent = nullptr);
    virtual ~SongScrollBar();

    /// The strip's height, in logical pixels, which are Android's dp
    static const int stripHeight = 24;

    /// The narrowest the thumb is drawn, in logical pixels
    static const int minThumbWidth = 16;

    /// The song's audio: what its width stands for. None for no song
    void setSongModel(sv::ModelId model);

    /// The reference's pitch track, a SparseTimeValueModel, or none
    void setPitchModel(sv::ModelId model);

    sv::ModelId getSongModel() const { return m_songModel; }
    sv::ModelId getPitchModel() const { return m_pitchModel; }

    /// The song's length in frames, 0 if there is none
    sv::sv_frame_t getSongFrames() const;

    /// The thumb for what the first pane shows now, if there is one
    SongScroll::Thumb currentThumb() const;

    /// The thumb and the playhead's x as last painted (-1 for none)
    SongScroll::Thumb getPaintedThumb() const { return m_paintedThumb; }
    int getPaintedPlayheadX() const { return m_paintedPlayheadX; }

    /// Whether the pitch has changed since the contour was last drawn
    bool isContourPending() const {
        return m_contourStale || m_contourTimer.isActive();
    }

    /// The contour's columns, in device pixels, as last drawn
    const std::vector<SongScroll::Column> &getContour() const {
        return m_columns;
    }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void changeEvent(QEvent *) override;

private slots:
    void pitchModelChanged();
    void contourTimedOut();
    void checkPositions();

private:
    sv::Pane *pane() const;
    int playheadX() const;
    void scheduleCheck();
    void moveCentreTo(sv::sv_frame_t centre);
    void rebuildContour(sv::sv_frame_t songFrames);

    QPointer<sv::ViewManager> m_viewManager;
    QPointer<sv::PaneStack> m_paneStack;
    sv::ModelId m_songModel;
    sv::ModelId m_pitchModel;

    // The pitch model's changes, gathered before the contour is made
    QTimer m_contourTimer;
    bool m_contourStale = true;

    // The geometry is checked once the panes have taken a change in
    // themselves: the strip may hear of it before they do
    QTimer m_checkTimer;

    QImage m_contour;
    std::vector<SongScroll::Column> m_columns;
    sv::sv_frame_t m_contourSongFrames = 0;

    SongScroll::Thumb m_paintedThumb;
    bool m_paintedWithThumb = false;
    int m_paintedPlayheadX = -1;

    bool m_dragging = false;
    double m_grabX = 0.0;
    sv::sv_frame_t m_grabbedCentre = 0;
};

#endif
