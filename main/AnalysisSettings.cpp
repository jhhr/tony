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

#include "AnalysisSettings.h"

#include "SessionXml.h"

#include "base/XmlExportable.h"

#include <QSettings>
#include <QXmlStreamReader>

using namespace sv;

namespace {

// value as the type of its default, or the default where it is not one.
// A truth value is only "true", "false", 1 or 0: QVariant would take any
// other word for true, and a session file is text anybody may edit
QVariant asTypeOf(QVariant value, const QVariant &fallback)
{
    if (!value.isValid()) return fallback;
    if (fallback.typeId() == QMetaType::Bool) {
        QString text = value.toString().trimmed().toLower();
        if (text == "true" || text == "1") return QVariant(true);
        if (text == "false" || text == "0") return QVariant(false);
        return fallback;
    }
    if (!value.convert(fallback.metaType())) return fallback;
    return value;
}

}

AnalysisSettings::Values
AnalysisSettings::fromSettings(QSettings &settings, const Values &defaults)
{
    Values values;
    settings.beginGroup("Analyser");
    for (const auto &d : defaults) {
        values[d.first] = asTypeOf(settings.value(d.first), d.second);
    }
    settings.endGroup();
    return values;
}

QString
AnalysisSettings::toXml(const Values &values, QString indent)
{
    QString xml = indent + "<analysis";
    for (const auto &v : values) {
        xml += QString(" %1=\"%2\"")
            .arg(XmlExportable::encodeEntities(v.first),
                 XmlExportable::encodeEntities(v.second.toString()));
    }
    xml += "/>\n";
    return xml;
}

AnalysisSettings::Stored
AnalysisSettings::read(QString sessionPath, const Values &defaults)
{
    Stored stored;
    stored.values = defaults;
    SessionXml::read(sessionPath, [&](QXmlStreamReader &reader) {
        stored = readFrom(reader, defaults);
    });
    return stored;
}

AnalysisSettings::Stored
AnalysisSettings::readFrom(QXmlStreamReader &reader, const Values &defaults)
{
    Stored stored;
    stored.values = defaults;

    // Everything of the document but our own element is passed over: it
    // has been read once already, by the session reader
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) continue;
        if (reader.name().toString().toLower() != "analysis") continue;

        stored.found = true;
        const QXmlStreamAttributes attributes = reader.attributes();
        for (const auto &d : defaults) {
            if (attributes.hasAttribute(d.first)) {
                stored.values[d.first] =
                    asTypeOf(attributes.value(d.first).toString(), d.second);
            }
        }
        break;
    }

    return stored;
}
