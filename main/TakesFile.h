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
     *
     * sessionPath is the file being written: a take's audio inside that
     * session's own directory is named relative to it (spec 6.4), which
     * is what lets a song and its takes folder be moved together.  With
     * no session path the paths are written as they are.
     */
    static QString toXml(const SingingTakes &takes,
                         QString sessionPath = "",
                         QString indent = "  ");

    // --- Where a session's take audio lives (spec 6.4) ---

    /**
     * The folder the combined audio files of a session's takes belong
     * in: the session file's name without its extension, plus
     * ".takes", beside the session file.  "C:/songs/My Song.ton" gives
     * "C:/songs/My Song.takes".  Forward slashes, no trailing one; ""
     * for an empty path.
     */
    static QString takesFolder(QString sessionPath);

    /**
     * The path to store in the session file for this audio file: the
     * path relative to the session file's own directory when the audio
     * is inside it, and the absolute path otherwise -- an audio file
     * somewhere else is named where it is.  Forward slashes.
     */
    static QString relativeAudioPath(QString sessionPath, QString audioPath);

    /**
     * The audio file a stored path means, for a session read from
     * sessionPath: a relative path is taken against the session file's
     * own directory, so that the song and its folder may be moved
     * together; an absolute one -- as a session saved before the folder
     * has -- is where it says it is.
     */
    static QString resolveAudioPath(QString sessionPath, QString storedPath);

    /// path is inside folder, at any depth
    static bool isInFolder(QString folder, QString path);

    /**
     * A path in folder for a copy of a file of this name, under a name
     * no file there has: the name itself if it is free, and otherwise
     * the name with "-2", "-3" and so on before the extension.  ""
     * if folder or fileName is empty, or every name tried was taken.
     */
    static QString freeCopyPath(QString folder, QString fileName);

    /**
     * Copy the audio of every take that is not in folder already into
     * it, and point the take at the copy: what a session save does
     * before it writes the file, so that the paths it writes are inside
     * the session's own folder (spec 6.4).
     *
     * Copied and not moved: the active take's audio model has its file
     * open, which Windows will not let us move, and it can go on
     * reading the old, identical copy until the next swap.  A file two
     * takes share -- a duplicated take -- is copied once and both take
     * the copy.  No file in folder is ever written over.
     *
     * "" on success.  Otherwise the return is a message for the user,
     * every take still points where it did, and the copies this call
     * made have been removed again: a session must not be saved naming
     * files that are not there.
     */
    static QString copyTakeAudioInto(SingingTakes &takes, QString folder);

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
