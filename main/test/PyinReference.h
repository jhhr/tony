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

#ifndef TEST_PYIN_REFERENCE_H
#define TEST_PYIN_REFERENCE_H

#include <vector>

/**
 * pYIN's own YinUtil::fastDifference, for a frame of in.size() samples;
 * returns in.size()/2 values. Wrapped in its own translation unit
 * because YinUtil.h pulls in the Vamp plugin SDK headers, which must
 * not be mixed with the host SDK headers that svcore uses.
 */
std::vector<double> pyinFastDifference(const std::vector<double> &in);

#endif
