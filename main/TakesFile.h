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

#ifndef TONY_TAKES_FILE_H
#define TONY_TAKES_FILE_H

#include <QString>

#include <vector>

class SingingTakes;
class QXmlStreamReader;

/**
 * The takes of a session in the session file: the one element of Tony's
 * own that a `.ton` carries (spec 6.4).
 *
 *     <takes active="Take 2">
 *       <take name="Take 1" audio="C:/songs/take-1.wav"/>
 *       <take name="Take 2" audio=""/>
 *     </takes>
 *
 * Everything else about a take -- its pitch, its notes and the coverage
 * strip that holds its coverage -- is in the document as ordinary layers,
 * named after the take (TakeLayers), and comes back with it.  This element
 * says which takes there are, which of them was on show, and where each
 * one's audio is; a take with no recording in it yet has an empty audio
 * path.
 *
 * `SVFileReader` does not know the element and only warns about it, so it
 * is written inside the `<sv>` document and read by a pass of Tony's own
 * over the same file afterwards.  The element being absent is what tells a
 * session saved before this feature from one saved with no takes in it.
 */
class TakesFile
{
public:
    struct Take {
        QString name;
        QString audioPath; // "" for a take with no recording in it yet
    };

    struct Takes {
        std::vector<Take> takes;

        /// The name of the take that was on show
        QString active;

        /// The file had a `<takes>` element at all
        bool found = false;
    };

    /**
     * The element for the takes of this session, indented with indent and
     * ending in a newline.  Names and paths are escaped.
     */
    static QString toXml(const SingingTakes &takes, QString indent = "  ");

    /**
     * Read the element from a session file: bzip2, as a `.ton` is, or
     * plain XML, which the session reader also accepts.  A file that
     * cannot be read, or that has no element, gives found == false.
     */
    static Takes read(QString sessionPath);

    /// The pass itself, over a document that is open already
    static Takes readFrom(QXmlStreamReader &reader);
};

#endif
