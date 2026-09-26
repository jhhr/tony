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

#ifndef TONY_ANDROID_FILES_H
#define TONY_ANDROID_FILES_H

#include <QString>
#include <QStringList>

/**
 * File work that only the Android build calls: Android hands Tony
 * neither its Vamp plugins under their own names nor the files the user
 * picks as paths.  Plain file operations, so they are built and tested
 * on the desktop as well.
 */
class AndroidFiles
{
public:
    /**
     * Android installs only libraries named lib*.so with an application,
     * so pYIN and CHP go into the APK as libpyin.so and libchp.so, beside
     * the application's own library in libraryDir.  But svcore names a
     * plugin after its library's file name (vamp:pyin:pyin:...), and its
     * scan of VAMP_PATH opens every .so in a directory, of which the
     * application's has dozens.  So this makes linkDir hold only the
     * plugins, under their own names: a link <name>.so to
     * <libraryDir>/lib<name>.so for each of names, or a copy where a link
     * cannot be made.  A link left from an earlier start is replaced
     * first, as the library directory moves with every install.
     *
     * Returns the paths of the links made.  Each plugin that could not be
     * set up adds a line to problems, saying why.
     */
    static QStringList linkVampPlugins(QString libraryDir,
                                       QString linkDir,
                                       QStringList names,
                                       QStringList &problems);

    /**
     * Opens the plugin library at path as svcore's scan does and looks
     * for the Vamp entry point.  Returns "" if it is there, else why not:
     * a library that fails here is one the scan will pass over, telling
     * only its own log file.
     */
    static QString checkVampPlugin(QString path);

    /**
     * Copies the file at source into dir as name, the name the user knows
     * it by, and returns the copy's path; on Android source is the
     * content:// URI the system file picker gives, which Tony's readers
     * cannot open, and Qt's QFile can.  An earlier copy of the same name
     * is replaced, and kept if the copy fails.  Returns "" on failure,
     * with error saying why.
     */
    static QString copyIn(QString source, QString name, QString dir,
                          QString &error);

    /**
     * name as a file name that can be used in any directory: path
     * separators and the characters Windows refuses become '_', and a
     * name that is empty or only dots becomes "imported".
     */
    static QString safeFileName(QString name);
};

#endif
