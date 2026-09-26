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

class QIODevice;
class QUrl;

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
     * The same, from in, open for reading: on Android a file descriptor
     * the file's provider gave (AndroidStorage::Document), which may be
     * a pipe. sourceName says where it came from, for the log.
     */
    static QString copyIn(QIODevice &in, QString sourceName, QString name,
                          QString dir, QString &error);

    /**
     * name as a file name that can be used in any directory: path
     * separators and the characters Windows refuses become '_', and a
     * name that is empty or only dots becomes "imported".
     */
    static QString safeFileName(QString name);

    /**
     * The URI of a file picked in Android's picker, as the string Android
     * wrote: Android lets Tony read that document through that exact
     * string only (its grants are matched by string), and Qt's
     * QFileDialog::selectedFiles() gives it partly decoded (spaces and
     * letters such as 'ä' unencoded, "%3A" and "%2F" kept). Qt's own
     * content file engine turns that back into a URI with '(' and ')'
     * (and "!'*") encoded, which Android leaves as they are, so for a
     * name holding any of those it finds no grant and reports the file
     * missing. picked is the QUrl the picker gave (selectedUrls()).
     */
    static QString grantedUri(const QUrl &picked);

    /**
     * The authority of a content:// URI: which app's provider the file
     * comes from, such as com.google.android.apps.docs.storage (Google
     * Drive); "" if uri is not content://.
     */
    static QString providerOf(QString uri);

    /**
     * The real path of the file a content:// URI from Android's file
     * picker names, when the URI itself says it; "" for any other URI (a
     * cloud provider's, and the documents whose path is looked up in
     * MediaStore: see pathLookupFor()).
     *
     * The external storage provider's documents, alone or under a folder
     * grant (.../document/<id> and .../tree/<id>/document/<id>), have ids
     * "primary:<path>", under the phone's own shared storage, whose root
     * primaryRoot is (Environment.getExternalStorageDirectory(): normally
     * /storage/emulated/0), and "<volume UUID>:<path>" on a card or USB
     * drive, under /storage/<volume UUID>. The downloads provider names
     * some files "raw:<absolute path>". The id is percent-encoded in the
     * URI, and may be partly decoded in the string Qt hands over.
     */
    static QString pathFromContentUri(QString uri, QString primaryRoot);

    /**
     * How the real path of a picked document can be found, if it is in
     * the phone's own storage. Only InUri needs nothing more than the
     * URI; the rest need All files access, without which MediaStore
     * shows Tony none of the files other apps put there.
     */
    enum class PathLookup {
        None,          // A cloud provider's, or not a file: no path
        InUri,         // pathFromContentUri()
        MediaStore,    // The _data column of mediaStoreUriFor()'s row
        ByNameAndSize  // A numbered download: see chooseDownload()
    };
    static PathLookup pathLookupFor(QString uri);

    /**
     * The MediaStore row (content://media/external/...) whose _data
     * column holds the path of the file behind uri, for the downloads
     * provider's "msf:<n>" (a download MediaStore knows) and the media
     * provider's "audio:<n>", "image:<n>", "video:<n>" and
     * "document:<n>" (the picker's Audio, Images, Videos and Documents,
     * and much of its Recent); "" for any other uri.
     */
    static QString mediaStoreUriFor(QString uri);

    /**
     * The downloads provider names a file the download manager fetched
     * by its number there ("<n>"), and the download manager shows other
     * apps none of its records. Such a file is found in MediaStore, where
     * the download manager puts every download, by the name and size the
     * provider gives: candidates are the paths of the files of that name
     * and size. Returns the only one, or else the only one in the
     * Download folder of primaryRoot; "" if that does not settle it.
     */
    static QString chooseDownload(QStringList candidates, QString primaryRoot);

    /**
     * Whether name has one of the extensions in patterns, a list such as
     * svcore's getKnownExtensions() give ("*.wav *.mp3"), in any case.
     * The picker offers every file, and Android's file types cannot say
     * which are Tony's (see MainWindow::getOpenFileName()).
     */
    static bool hasExtensionIn(QString name, QString patterns);

    /**
     * The entries of the recent files list that can be opened: paths of
     * files that are there. A file moved or deleted since, and anything
     * that is not a path (a content:// URI, which the picker's grant no
     * longer covers), is left out.
     */
    static QStringList usableRecentFiles(QStringList identifiers);

    /**
     * The name Save Session As suggests on Android, where the system's
     * picker suggests none of its own: the session's name if it has a
     * file, else the reference audio's with the session extension, else
     * "".
     */
    static QString suggestedSessionName(QString sessionPath,
                                        QString audioPath);

    /**
     * The name to save a session under when the picker returned picked:
     * with the session extension added if it has none, as the desktop's
     * file dialog adds it; or "" for a name that names nothing (empty,
     * only an extension, or the "(invalid)" Android's storage gives a
     * document created with an empty name).
     */
    static QString sessionFileName(QString picked);

    /**
     * Removes the file at path if it is there and empty: the document the
     * system's picker makes for a save, when the save is not going to be
     * written there. A file with anything in it is left alone. path may
     * be a content:// URI, which Qt's QFile removes through the file's
     * provider. True if it removed one.
     */
    static bool removeIfEmpty(QString path);

    /**
     * What a document holds after a save through its provider, against
     * what was written to it: Save Log says both in the log, and tells
     * the user when they differ. held is the document's _size column as
     * ContentResolver.query() gives it, "" for a null: a provider that
     * gives no size says nothing either way.
     */
    struct SavedSize {
        qint64 written = 0;
        qint64 held = -1;       // -1: the provider gives no size
        bool known() const { return held >= 0; }
        bool differs() const { return known() && held != written; }
    };
    static SavedSize savedSize(qint64 written, QString held);
};

#endif
