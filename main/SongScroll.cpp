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

#include "SongScroll.h"

#include <algorithm>
#include <cmath>

using namespace sv;

namespace SongScroll
{

double
xForFrame(sv_frame_t frame, sv_frame_t songFrames, double width)
{
    if (songFrames <= 0 || width <= 0.0) return 0.0;
    return double(frame) * width / double(songFrames);
}

sv_frame_t
frameForX(double x, sv_frame_t songFrames, double width)
{
    if (songFrames <= 0 || width <= 0.0) return 0;
    return sv_frame_t(std::llround(x * double(songFrames) / width));
}

sv_frame_t
clampToSong(sv_frame_t frame, sv_frame_t songFrames)
{
    if (songFrames <= 0 || frame < 0) return 0;
    if (frame >= songFrames) return songFrames - 1;
    return frame;
}

Thumb
thumb(sv_frame_t start, sv_frame_t end, sv_frame_t songFrames,
      double width, double minWidth)
{
    Thumb t;
    if (songFrames <= 0 || width <= 0.0) return t;

    // What of the strip the panes cover: nothing of what they show
    // before the song or after it
    t.x0 = std::clamp(xForFrame(start, songFrames, width), 0.0, width);
    t.x1 = std::clamp(xForFrame(end, songFrames, width), 0.0, width);
    if (t.x1 < t.x0) t.x1 = t.x0;

    // Wide enough to see and to catch, about the same middle, and
    // within the strip
    minWidth = std::min(minWidth, width);
    if (t.width() < minWidth) {
        double middle = (t.x0 + t.x1) / 2.0;
        t.x0 = middle - minWidth / 2.0;
        t.x1 = middle + minWidth / 2.0;
        if (t.x0 < 0.0) {
            t.x1 -= t.x0;
            t.x0 = 0.0;
        }
        if (t.x1 > width) {
            t.x0 -= t.x1 - width;
            t.x1 = width;
        }
    }
    return t;
}

bool
hitsThumb(const Thumb &thumb, double x)
{
    return x >= thumb.x0 && x <= thumb.x1;
}

sv_frame_t
dragCentre(sv_frame_t grabbedCentre, double grabX, double x,
           sv_frame_t songFrames, double width)
{
    if (songFrames <= 0 || width <= 0.0) return 0;
    double moved = (x - grabX) * double(songFrames) / width;
    return clampToSong(grabbedCentre + sv_frame_t(std::llround(moved)),
                       songFrames);
}

sv_frame_t
jumpCentre(double x, sv_frame_t songFrames, double width)
{
    return clampToSong(frameForX(x, songFrames, width), songFrames);
}

std::vector<Column>
pitchColumns(const std::vector<std::pair<sv_frame_t, double>> &events,
             sv_frame_t resolution, sv_frame_t songFrames, int columns)
{
    std::vector<Column> result(std::max(columns, 0));
    if (columns <= 0 || songFrames <= 0) return result;
    if (resolution < 1) resolution = 1;

    // The column that shows frame, which is within the song
    auto columnOf = [&](sv_frame_t frame) {
        return int(std::min(frame * sv_frame_t(columns) / songFrames,
                            sv_frame_t(columns - 1)));
    };

    for (const auto &event : events) {
        double value = event.second;
        if (!(value > 0.0)) continue;

        sv_frame_t first = event.first;
        sv_frame_t last = first + resolution - 1;
        if (last < 0 || first >= songFrames) continue;
        first = std::max(first, sv_frame_t(0));
        last = std::min(last, songFrames - 1);

        for (int c = columnOf(first); c <= columnOf(last); ++c) {
            Column &column = result[c];
            if (column.isEmpty()) {
                column.low = column.high = value;
            } else {
                column.low = std::min(column.low, value);
                column.high = std::max(column.high, value);
            }
        }
    }
    return result;
}

bool
pitchRange(const std::vector<Column> &columns, double &low, double &high)
{
    bool found = false;
    for (const Column &column : columns) {
        if (column.isEmpty()) continue;
        if (!found) {
            low = column.low;
            high = column.high;
            found = true;
        } else {
            low = std::min(low, column.low);
            high = std::max(high, column.high);
        }
    }
    return found;
}

double
yForPitch(double hz, double low, double high, double height)
{
    if (!(low > 0.0) || !(high > low) || !(hz > 0.0)) return height / 2.0;
    double along = std::log(hz / low) / std::log(high / low);
    return (1.0 - std::clamp(along, 0.0, 1.0)) * height;
}

}
