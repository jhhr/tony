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

#include "PyinReference.h"

#include "../../pyin/YinUtil.h"

std::vector<double>
pyinFastDifference(const std::vector<double> &in)
{
    int halfSize = int(in.size() / 2);
    std::vector<double> out(halfSize, 0.0);
    YinUtil util(halfSize);
    util.fastDifference(in.data(), out.data());
    return out;
}
