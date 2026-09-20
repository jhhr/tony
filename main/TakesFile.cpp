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

#include "TakesFile.h"

#include "SingingTakes.h"

#include "base/XmlExportable.h"
#include "data/fileio/BZipFileDevice.h"

#include <QFile>
#include <QXmlStreamReader>

using namespace sv;

QString
TakesFile::toXml(const SingingTakes &takes, QString indent)
{
    QString xml;

    xml += QString("%1<takes active=\"%2\">\n")
        .arg(indent)
        .arg(XmlExportable::encodeEntities(takes.getActiveName()));

    for (const SingingTakes::Take &take : takes.getTakes()) {
        xml += QString("%1  <take name=\"%2\" audio=\"%3\"/>\n")
            .arg(indent)
            .arg(XmlExportable::encodeEntities(take.name))
            .arg(XmlExportable::encodeEntities(take.audioPath));
    }

    xml += QString("%1</takes>\n").arg(indent);

    return xml;
}

TakesFile::Takes
TakesFile::read(QString sessionPath)
{
    Takes takes;
    if (sessionPath == "") return takes;

    // A session file is bzip2, but the session reader takes a plain XML
    // one as well, so the first bytes say which this is rather than the
    // extension
    bool compressed = false;
    {
        QFile probe(sessionPath);
        if (!probe.open(QIODevice::ReadOnly)) return takes;
        compressed = (probe.read(3) == QByteArray("BZh"));
    }

    if (compressed) {
        BZipFileDevice file(sessionPath);
        if (!file.open(QIODevice::ReadOnly)) return takes;
        QXmlStreamReader reader(&file);
        takes = readFrom(reader);
        file.close();
    } else {
        QFile file(sessionPath);
        if (!file.open(QIODevice::ReadOnly)) return takes;
        QXmlStreamReader reader(&file);
        takes = readFrom(reader);
    }

    return takes;
}

TakesFile::Takes
TakesFile::readFrom(QXmlStreamReader &reader)
{
    Takes takes;
    bool inTakes = false;

    // Everything of the document but our own element is passed over: it
    // has been read once already, by the session reader
    while (!reader.atEnd()) {

        switch (reader.readNext()) {

        case QXmlStreamReader::StartElement:
        {
            QString name = reader.name().toString().toLower();

            if (name == "takes") {
                inTakes = true;
                takes.found = true;
                takes.active = reader.attributes().value("active").toString();
            } else if (inTakes && name == "take") {
                Take take;
                take.name = reader.attributes().value("name").toString();
                take.audioPath = reader.attributes().value("audio").toString();
                // A take is its name: one without a name is no take
                if (take.name != "") takes.takes.push_back(take);
            }
            break;
        }

        case QXmlStreamReader::EndElement:
            if (reader.name().toString().toLower() == "takes") inTakes = false;
            break;

        default:
            break;
        }
    }

    return takes;
}
