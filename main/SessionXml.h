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

#ifndef TONY_SESSION_XML_H
#define TONY_SESSION_XML_H

#include <QString>

#include <functional>
#include <map>

class QXmlStreamReader;

/**
 * The XML of a session file, for the passes of Tony's own that pick its
 * own elements out of it once the session reader has read the document
 * (TakesFile, AnalysisSettings, VoiceTracks).
 */
namespace SessionXml
{
    /**
     * Run pass over the file's XML: bzip2, as a `.ton` is, or plain XML,
     * which the session reader also accepts.  False, and pass not run,
     * if there is no file to open.
     */
    bool read(QString sessionPath,
              const std::function<void(QXmlStreamReader &)> &pass);

    /// What reads an element, from its start on
    typedef std::function<void(QXmlStreamReader &)> ElementReader;

    /**
     * One pass over the file's XML that hands each element named in
     * readers (in lower case) to its reader as the element starts, so
     * that elements in any order are read with the file decompressed
     * once. False if there is no file to open.
     */
    bool readElements(QString sessionPath,
                      const std::map<QString, ElementReader> &readers);
}

#endif
