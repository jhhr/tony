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

#ifndef TONY_ANDROID_STORAGE_H
#define TONY_ANDROID_STORAGE_H

#include <QCoreApplication>
#include <QString>

class QWidget;

/**
 * Android's "All files access" (MANAGE_EXTERNAL_STORAGE), with which
 * Tony reads and writes the phone's own storage by path, as on the
 * desktop: a session opens and saves where it is, with its audio and
 * takes folder beside it, in a folder a sync app keeps in step with a
 * computer. Android only; the path of a picked file is
 * AndroidFiles::pathFromContentUri(), tested on the desktop.
 */
class AndroidStorage
{
    Q_DECLARE_TR_FUNCTIONS(AndroidStorage)

public:
    // parent is what the boxes asking for access are shown over
    AndroidStorage(QWidget *parent);

    // Whether Tony may use the phone's shared storage by path. Never
    // before Android 11, which has no such permission
    static bool hasAllFilesAccess();

    // The root of the phone's own shared storage, normally
    // /storage/emulated/0; "" if Android does not say
    static QString primaryRoot();

    // The real path of a content:// URI the file picker gave, if it names
    // a file in the phone's own storage; else ""
    QString pathFor(QString uri) const;

    // Asks for All files access: why, in a box, and then the system's
    // settings page for it; back from there, checks again. True if Tony
    // has it now
    bool ask(QString why);

    // Whether ask() has been called since Tony started
    bool hasAsked() const { return m_asked; }

private:
    QWidget *m_parent;
    bool m_asked;

    // The settings page for All files access, Tony's own or the list of
    // apps; false if it could not be opened
    static bool openSettings(bool thisApp);
};

#endif
