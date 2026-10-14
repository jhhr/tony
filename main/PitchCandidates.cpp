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

#include "PitchCandidates.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace PitchCandidates
{

namespace {

// Half the hop of Tony's analyses: the candidates and the pitch track
// sit on the same grid, but a frame is matched to the nearest within
// this all the same
constexpr sv::sv_frame_t kFrameTolerance = 128;

double cents(double a, double b)
{
    return 1200.0 * std::log2(a / b);
}

double medianHz(const Track &t)
{
    if (t.empty()) return 0.0;
    std::vector<double> hz;
    for (const auto &p : t) hz.push_back(p.second);
    auto mid = hz.begin() + hz.size() / 2;
    std::nth_element(hz.begin(), mid, hz.end());
    return *mid;
}

}

int
compare(const Track &a, const Track &b)
{
    if (a.empty() || b.empty()) return 0;

    int common = 0, above = 0, below = 0;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        sv::sv_frame_t d = a[i].first - b[j].first;
        if (d < -kFrameTolerance) {
            ++i;
        } else if (d > kFrameTolerance) {
            ++j;
        } else {
            ++common;
            double c = cents(a[i].second, b[j].second);
            if (c > kSameCents) ++above;
            else if (c < -kSameCents) ++below;
            ++i;
            ++j;
        }
    }

    if (common == 0) {
        double c = cents(medianHz(a), medianHz(b));
        if (c > kSameCents) return 1;
        if (c < -kSameCents) return -1;
        return 0;
    }

    if (above > below && above >= kMinShare * common) return 1;
    if (below > above && below >= kMinShare * common) return -1;
    return 0;
}

bool
same(const Track &a, const Track &b)
{
    if (a.empty() || b.empty()) return a.empty() && b.empty();

    // Frames voiced in either, and of them those where both are voiced
    // and agree
    int either = 0, agree = 0;
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        if (j == b.size() ||
            (i < a.size() && a[i].first < b[j].first - kFrameTolerance)) {
            ++either;
            ++i;
        } else if (i == a.size() || b[j].first < a[i].first - kFrameTolerance) {
            ++either;
            ++j;
        } else {
            ++either;
            if (std::abs(cents(a[i].second, b[j].second)) <= kSameCents) ++agree;
            ++i;
            ++j;
        }
    }
    return agree >= kSameShare * either;
}

std::vector<int>
distinct(const std::vector<Track> &candidates)
{
    std::vector<int> out;
    for (int i = 0; i < int(candidates.size()); ++i) {
        if (candidates[i].empty()) continue;
        bool known = false;
        for (int k : out) {
            if (same(candidates[k], candidates[i])) {
                known = true;
                break;
            }
        }
        if (!known) out.push_back(i);
    }
    return out;
}

std::vector<int>
order(const std::vector<Track> &candidates)
{
    int n = int(candidates.size());

    // Each candidate scores a point for every other it lies above and
    // loses one for every other above it; ties, which tracks with
    // little in common can make, go by median pitch. Candidates that
    // are never voiced come last
    std::vector<int> score(n, 0);
    std::vector<double> median(n, 0.0);
    for (int i = 0; i < n; ++i) {
        median[i] = medianHz(candidates[i]);
        for (int j = i + 1; j < n; ++j) {
            int r = compare(candidates[i], candidates[j]);
            score[i] += r;
            score[j] -= r;
        }
    }

    std::vector<int> out(n);
    std::iota(out.begin(), out.end(), 0);
    std::stable_sort(out.begin(), out.end(), [&](int x, int y) {
        bool ex = candidates[x].empty(), ey = candidates[y].empty();
        if (ex != ey) return ey;
        if (score[x] != score[y]) return score[x] < score[y];
        return median[x] < median[y];
    });
    return out;
}

int
step(const std::vector<Track> &candidates, const Track &current, bool up)
{
    std::vector<int> o = order(candidates);
    if (!up) std::reverse(o.begin(), o.end());

    for (int k : o) {
        if (candidates[k].empty()) continue;
        if (current.empty()) return k;
        int r = compare(candidates[k], current);
        if ((up && r > 0) || (!up && r < 0)) return k;
    }
    return -1;
}

}
