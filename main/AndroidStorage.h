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
#include <QJniObject>
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

    // The real path of the file a content:// URI the file picker gave
    // names, if it is in the phone's own storage: from the URI, or from
    // MediaStore, which shows Tony other apps' files only with All files
    // access (AndroidFiles::pathLookupFor()). Else "", with why saying
    // why not. Whether the file is there is not checked. The URI as
    // Android wrote it (AndroidFiles::grantedUri()): the picker's grant
    // is for that string only
    static QString pathFor(QString uri, QString &why);

    // The name the file's provider gives the document at uri; "" if it
    // gives none
    static QString displayName(QString uri);

    /**
     * A document opened through its provider, from the URI as Android
     * wrote it: not through QFile, whose content file engine rebuilds
     * the URI, differently for names with parentheses, and then has no
     * grant for it. fd() is read or written as it is (QFile's
     * DontCloseHandle) and stays the provider's ParcelFileDescriptor's:
     * close() closes it through that, which is what tells a provider
     * that watches for the close (MediaStore behind Downloads, a cloud
     * app) that Tony has finished with the document. A descriptor taken
     * from it instead (detachFd()) tells the provider so at once, before
     * anything is written, and a provider may then keep nothing.
     */
    class Document
    {
    public:
        Document();
        // Closes the document if it is still open
        ~Document();

        // Opens the document at uri to read ("r") or write ("wt", which
        // leaves only what is written now); false on failure, with error
        // saying why
        bool open(QString uri, QString mode, QString &error);

        bool isOpen() const { return m_fd >= 0; }
        int fd() const { return m_fd; }

        // The size of the file behind the descriptor, as fstat() gives
        // it; -1 for a pipe, which a provider may give instead of a file
        qint64 fileSize() const;

        // Closes the document through its provider. False if that failed,
        // or if the provider has said through a pipe that it went wrong
        // at its end, with error saying why
        bool close(QString &error);

    private:
        QJniObject m_descriptor;
        int m_fd;

        Document(const Document &) = delete;
        Document &operator=(const Document &) = delete;
    };

    // The size of the document at uri as its provider gives it (its
    // _size column): "" if it gives none, or could not be asked, with
    // error saying why (see AndroidFiles::savedSize())
    static QString sizeOf(QString uri, QString &error);

    // Removes the document at uri if it is empty: the one the picker
    // makes for a save, when the save is not made there. True if it did
    static bool removeIfEmpty(QString uri);

    // The file Tony's output is kept in as well as the system log
    // (main.cpp), which Help > Save Log... saves a copy of (LogFile)
    static QString logPath();

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
