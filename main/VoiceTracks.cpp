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

#include "VoiceTracks.h"

#include <QRegularExpression>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>

using namespace sv;

QString
VoiceTracks::name(Voice voice)
{
    switch (voice) {
    case Strongest: return "strongest";
    case Lower: return "lower";
    case Higher: return "higher";
    }
    return "";
}

void
VoiceTracks::set(Voice voice, const Track &points)
{
    Track &track = m_tracks[voice];
    track.clear();
    for (const Point &p : points) {
        if (!(p.hz > 0)) continue;
        if (!track.empty() && p.frame <= track.back().frame) continue;
        track.push_back(p);
    }
    m_set[voice] = true;
}

bool
VoiceTracks::isComplete() const
{
    for (int v = 0; v < voiceCount; ++v) {
        if (!m_set[v]) return false;
    }
    return true;
}

void
VoiceTracks::clear()
{
    for (int v = 0; v < voiceCount; ++v) {
        m_tracks[v].clear();
        m_set[v] = false;
    }
}

VoiceTracks::Track
VoiceTracks::within(Voice voice, sv_frame_t start, sv_frame_t end) const
{
    const Track &track = m_tracks[voice];
    auto byFrame = [](const Point &p, sv_frame_t f) { return p.frame < f; };
    auto from = std::lower_bound(track.begin(), track.end(), start, byFrame);
    auto to = std::lower_bound(from, track.end(), end, byFrame);
    return Track(from, to);
}

bool
VoiceTracks::operator==(const VoiceTracks &other) const
{
    for (int v = 0; v < voiceCount; ++v) {
        if (m_set[v] != other.m_set[v]) return false;
        if (!(m_tracks[v] == other.m_tracks[v])) return false;
    }
    return true;
}

QString
VoiceTracks::toXml(QString indent) const
{
    if (!isComplete()) return "";

    QString xml = indent + "<voicetracks>\n";
    for (int v = 0; v < voiceCount; ++v) {
        xml += indent + "  <voice name=\"" + name(Voice(v)) + "\">";
        QStringList pairs;
        pairs.reserve(int(m_tracks[v].size()));
        for (const Point &p : m_tracks[v]) {
            // Seven significant figures: a hundredth of a cent
            pairs.push_back(QString::number(p.frame) + " " +
                            QString::number(p.hz, 'g', 7));
        }
        xml += pairs.join(" ");
        xml += "</voice>\n";
    }
    xml += indent + "</voicetracks>\n";
    return xml;
}

VoiceTracks
VoiceTracks::fromElement(QXmlStreamReader &reader)
{
    VoiceTracks tracks;
    while (!reader.atEnd()) {
        QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement &&
            reader.name().toString().toLower() == "voicetracks") {
            break;
        }
        if (token != QXmlStreamReader::StartElement ||
            reader.name().toString().toLower() != "voice") {
            continue;
        }
        QString which = reader.attributes().value("name").toString();
        QString text = reader.readElementText();
        int voice = -1;
        for (int v = 0; v < voiceCount; ++v) {
            if (which == name(Voice(v))) voice = v;
        }
        if (voice < 0) continue;

        // Frames and pitches in pairs; anything else is no track at all,
        // which leaves the tracks incomplete
        const QStringList words = text.split(QRegularExpression("\\s+"),
                                             Qt::SkipEmptyParts);
        if (words.size() % 2 != 0) continue;
        Track track;
        track.reserve(words.size() / 2);
        bool ok = true;
        for (int i = 0; ok && i + 1 < words.size(); i += 2) {
            bool frameOk = false, hzOk = false;
            sv_frame_t frame = words[i].toLongLong(&frameOk);
            double hz = words[i + 1].toDouble(&hzOk);
            ok = frameOk && hzOk;
            track.push_back({ frame, hz });
        }
        if (ok) tracks.set(Voice(voice), track);
    }
    return tracks;
}
