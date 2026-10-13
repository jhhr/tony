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

#ifndef TONY_ANALYSIS_SETTINGS_H
#define TONY_ANALYSIS_SETTINGS_H

#include <QString>
#include <QVariant>

#include <map>

class QSettings;
class QXmlStreamReader;

/**
 * The options of a song's analysis: those of the Analysis menu, by the
 * keys the settings keep them under (Analyser::getAnalysisSettings()
 * names them and gives their defaults).  A song's session keeps the ones
 * its reference was analysed with, in an element of Tony's own:
 *
 *     <analysis lowamp-analysis="true" onset-analysis="true"
 *               precision-analysis="false" prune-analysis="true"
 *               voice-to-follow="1"/>
 *
 * Written, as the `<takes>` element is (TakesFile), inside the `<sv>`
 * document, which SVFileReader passes over, and read by a pass of our
 * own over the file.  The element being absent is what tells a session
 * whose analysis is not known -- saved before the options were kept, or
 * never analysed -- from one that has them.
 */
class AnalysisSettings
{
public:
    /// An option's value by its key ("precision-analysis", ...)
    typedef std::map<QString, QVariant> Values;

    /**
     * The user's own: each option of defaults from the group "Analyser"
     * of the settings, as the type of its default, or the default where
     * the settings have none of that type.
     */
    static Values fromSettings(QSettings &settings, const Values &defaults);

    /**
     * The element for these values, indented with indent and ending in
     * a newline: an attribute for each option, named by its key.
     */
    static QString toXml(const Values &values, QString indent = "  ");

    struct Stored {
        /// Each option of the defaults: the element's attribute of its
        /// name, as the type of its default, where the element has it;
        /// otherwise, and where there is no element, the default
        Values values;

        /// The file had an `<analysis>` element at all
        bool found = false;
    };

    /**
     * Read the element from a session file: bzip2, as a `.ton` is, or
     * plain XML.  A file that cannot be read, or that has no element,
     * gives found == false.
     */
    static Stored read(QString sessionPath, const Values &defaults);

    /// The pass itself, over a document that is open already
    static Stored readFrom(QXmlStreamReader &reader, const Values &defaults);
};

#endif
