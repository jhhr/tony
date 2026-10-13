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

#include "SessionXml.h"

#include "data/fileio/BZipFileDevice.h"

#include <QFile>
#include <QXmlStreamReader>

using namespace sv;

bool
SessionXml::read(QString sessionPath,
                 const std::function<void(QXmlStreamReader &)> &pass)
{
    if (sessionPath == "") return false;

    // A session file is bzip2, but the session reader takes a plain XML
    // one as well, so the first bytes say which this is rather than the
    // extension
    bool compressed = false;
    {
        QFile probe(sessionPath);
        if (!probe.open(QIODevice::ReadOnly)) return false;
        compressed = (probe.read(3) == QByteArray("BZh"));
    }

    if (compressed) {
        BZipFileDevice file(sessionPath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        QXmlStreamReader reader(&file);
        pass(reader);
        file.close();
    } else {
        QFile file(sessionPath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        QXmlStreamReader reader(&file);
        pass(reader);
    }

    return true;
}

bool
SessionXml::readElements(QString sessionPath,
                         const std::map<QString, ElementReader> &readers)
{
    return read(sessionPath, [&](QXmlStreamReader &reader) {
        while (!reader.atEnd()) {
            if (reader.readNext() != QXmlStreamReader::StartElement) continue;
            auto it = readers.find(reader.name().toString().toLower());
            if (it != readers.end()) it->second(reader);
        }
    });
}
