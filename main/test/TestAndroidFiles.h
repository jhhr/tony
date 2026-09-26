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

#ifndef TEST_ANDROID_FILES_H
#define TEST_ANDROID_FILES_H

// Tier 1: the file work the Android build does at start (the Vamp plugin
// links) and when a file is picked (the copy into app storage, the path
// of a picked content:// URI or where to look it up, the URI string
// Android granted, the files Tony opens, the names Save Session As
// suggests and accepts, the recent files that are still there), done
// here on plain files in a temporary directory and on URIs written as
// Android writes them. What only a phone has -- a provider behind the
// URI, MediaStore, the installed library directory -- is not here.

#include "../AndroidFiles.h"

#include "plugin/PluginIdentifier.h"

#include <QObject>
#include <QtTest>
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>

class TestAndroidFiles : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_counter = 0;

    // A path of its own for each use, created or not
    QString newPath(QString name) {
        return m_dir.filePath(QString("%1-%2").arg(name).arg(++m_counter));
    }

    QString newDir(QString name) {
        QString path = newPath(name);
        QDir().mkpath(path);
        return path;
    }

    static bool writeFile(QString path, QByteArray content) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        bool ok = (file.write(content) == content.size());
        file.close();
        return ok;
    }

    static QByteArray readFile(QString path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray("(unreadable)");
        return file.readAll();
    }

    // What svcore's scan of a VAMP_PATH directory finds there
    // (NativeVampPluginFactory's getCandidateLibraries())
    static QStringList scanned(QString dir) {
        return QDir(dir, "*.so", QDir::Name | QDir::IgnoreCase,
                    QDir::Files | QDir::Readable).entryList();
    }

    // The folder as Android installs an application's libraries: the
    // plugins among them, renamed lib*.so
    QString installedLibraries(QByteArray pyin, QByteArray chp) {
        QString dir = newDir("lib-arm64");
        writeFile(dir + "/libTony_arm64-v8a.so", "Tony");
        writeFile(dir + "/libQt6Core_arm64-v8a.so", "Qt");
        writeFile(dir + "/libplugins_platforms_qtforandroid_arm64-v8a.so", "Qt");
        writeFile(dir + "/libc++_shared.so", "C++");
        if (pyin != "") writeFile(dir + "/libpyin.so", pyin);
        if (chp != "") writeFile(dir + "/libchp.so", chp);
        return dir;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    // --- The copy of a picked file ---

    void a_picked_file_is_copied_under_its_own_name() {
        // On Android the source is a content:// URI, named like nothing
        // the user knows
        QString source = newDir("provider") + "/document-1234";
        QVERIFY(writeFile(source, "RIFF and the rest"));

        QString into = newPath("imported"); // not there yet
        QString error;
        QString copy = AndroidFiles::copyIn(source, "My Song.wav", into, error);

        QCOMPARE(error, QString());
        QCOMPARE(copy, QDir(into).filePath("My Song.wav"));
        QCOMPARE(readFile(copy), QByteArray("RIFF and the rest"));
        QCOMPARE(readFile(source), QByteArray("RIFF and the rest"));
    }

    void picking_a_file_of_the_same_name_again_replaces_the_copy() {
        QString from = newDir("provider");
        QVERIFY(writeFile(from + "/a", "first"));
        QVERIFY(writeFile(from + "/b", "second, and longer"));

        QString into = newPath("imported");
        QString error;
        QString first = AndroidFiles::copyIn(from + "/a", "take.wav", into, error);
        QString second = AndroidFiles::copyIn(from + "/b", "take.wav", into, error);

        QCOMPARE(error, QString());
        QCOMPARE(second, first);
        QCOMPARE(readFile(second), QByteArray("second, and longer"));
        QCOMPARE(QDir(into).entryList(QDir::Files), QStringList({ "take.wav" }));
    }

    void a_failed_copy_says_why_and_keeps_the_earlier_one() {
        QString from = newDir("provider");
        QVERIFY(writeFile(from + "/a", "the good one"));

        QString into = newPath("imported");
        QString error;
        QString copy = AndroidFiles::copyIn(from + "/a", "take.wav", into, error);
        QCOMPARE(error, QString());

        QString failed = AndroidFiles::copyIn(from + "/gone", "take.wav",
                                              into, error);
        QCOMPARE(failed, QString());
        QVERIFY(error != "");
        QCOMPARE(readFile(copy), QByteArray("the good one"));
        QCOMPARE(QDir(into).entryList(QDir::Files), QStringList({ "take.wav" }));
    }

    void a_file_is_copied_from_what_its_provider_hands_over() {
        // On Android: a file descriptor from the provider, through the URI
        // as Android wrote it, which may be a pipe with no size
        QByteArray content("ID3 and the rest of an MP3");
        QBuffer pipe(&content);
        QVERIFY(pipe.open(QIODevice::ReadOnly));

        QString into = newPath("imported");
        QString error;
        QString copy = AndroidFiles::copyIn
            (pipe, "content://com.google.android.apps.docs.storage/document/"
             "acc%3D1%3Bdoc%3Dencoded%3Dabc",
             "(vocals) Avi Kaplan - Peace Somehow.mp3", into, error);

        QCOMPARE(error, QString());
        QCOMPARE(copy, QDir(into).filePath
                 ("(vocals) Avi Kaplan - Peace Somehow.mp3"));
        QCOMPARE(readFile(copy), content);
    }

    void a_name_cannot_put_the_copy_elsewhere() {
        QString source = newDir("provider") + "/document";
        QVERIFY(writeFile(source, "x"));

        QString into = newPath("imported");
        QString error;
        QString copy = AndroidFiles::copyIn(source, "../../escape.wav",
                                            into, error);

        QCOMPARE(error, QString());
        QCOMPARE(QFileInfo(copy).absolutePath(), QFileInfo(into).absoluteFilePath());
        QCOMPARE(QFileInfo(copy).fileName(), QString(".._.._escape.wav"));
    }

    void names_are_made_safe_to_use() {
        QCOMPARE(AndroidFiles::safeFileName("Song 1.wav"), QString("Song 1.wav"));
        QCOMPARE(AndroidFiles::safeFileName("Ääni – live.mp3"),
                 QString("Ääni – live.mp3"));
        QCOMPARE(AndroidFiles::safeFileName("a/b\\c.wav"), QString("a_b_c.wav"));
        QCOMPARE(AndroidFiles::safeFileName("What? <Live>: \"x\"|*.ogg"),
                 QString("What_ _Live__ _x___.ogg"));
        QCOMPARE(AndroidFiles::safeFileName("line\nbreak.wav"),
                 QString("line_break.wav"));
        QCOMPARE(AndroidFiles::safeFileName("  padded.wav "), QString("padded.wav"));
        QCOMPARE(AndroidFiles::safeFileName(""), QString("imported"));
        QCOMPARE(AndroidFiles::safeFileName("."), QString("imported"));
        QCOMPARE(AndroidFiles::safeFileName(".."), QString("imported"));
    }

    // --- A picked document's path ---

    // As Android's Uri.toString() gives it, which QFileDialog then passes
    // through QUrl: the string Tony gets from the picker. Tested in both
    // forms, since QUrl decodes some of the id and not the rest
    static QString pickedAsQtGivesIt(QString uri) {
        return QUrl(uri).toString(QUrl::PreferLocalFile);
    }

    void a_document_in_the_phones_storage_has_its_path() {
        QString root = "/storage/emulated/0";
        QString uri = "content://com.android.externalstorage.documents/"
            "document/primary%3AMusic%2Ftest.ton";

        QCOMPARE(AndroidFiles::pathFromContentUri(uri, root),
                 QString("/storage/emulated/0/Music/test.ton"));
        QCOMPARE(AndroidFiles::pathFromContentUri(pickedAsQtGivesIt(uri), root),
                 QString("/storage/emulated/0/Music/test.ton"));

        // The root is the phone's, whatever it is
        QCOMPARE(AndroidFiles::pathFromContentUri(uri, "/storage/emulated/10"),
                 QString("/storage/emulated/10/Music/test.ton"));
    }

    void names_with_spaces_and_finnish_letters_are_decoded() {
        QString root = "/storage/emulated/0";
        // "Music/Laulut/Sävel äänessä 100%.ton", as Android encodes it
        QString uri = "content://com.android.externalstorage.documents/"
            "document/primary%3AMusic%2FLaulut%2FS%C3%A4vel%20%C3%A4%C3%A4ness"
            "%C3%A4%20100%25.ton";
        QString expected("/storage/emulated/0/Music/Laulut/"
                         "Sävel äänessä 100%.ton");

        QCOMPARE(AndroidFiles::pathFromContentUri(uri, root), expected);
        QString given = pickedAsQtGivesIt(uri);
        QCOMPARE(AndroidFiles::pathFromContentUri(given, root), expected);
    }

    void a_document_under_a_folder_grant_has_its_path() {
        QString uri = "content://com.android.externalstorage.documents/"
            "tree/primary%3ASync%2FSongs/document/"
            "primary%3ASync%2FSongs%2FMy%20Song.ton";
        QCOMPARE(AndroidFiles::pathFromContentUri(uri, "/storage/emulated/0"),
                 QString("/storage/emulated/0/Sync/Songs/My Song.ton"));
    }

    void the_top_of_the_phones_storage_is_its_root() {
        QString uri = "content://com.android.externalstorage.documents/"
            "document/primary%3A";
        QCOMPARE(AndroidFiles::pathFromContentUri(uri, "/storage/emulated/0/"),
                 QString("/storage/emulated/0"));
    }

    void a_document_on_a_card_is_under_its_volume() {
        QString uri = "content://com.android.externalstorage.documents/"
            "document/1A2B-3C4D%3AMusic%2FSong.mp3";
        QCOMPARE(AndroidFiles::pathFromContentUri(uri, "/storage/emulated/0"),
                 QString("/storage/1A2B-3C4D/Music/Song.mp3"));
    }

    void a_download_with_a_raw_id_has_that_path() {
        QString uri = "content://com.android.providers.downloads.documents/"
            "document/raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2FSong.mp3";
        QCOMPARE(AndroidFiles::pathFromContentUri(uri, "/storage/emulated/0"),
                 QString("/storage/emulated/0/Download/Song.mp3"));
        QCOMPARE(AndroidFiles::pathFromContentUri(pickedAsQtGivesIt(uri),
                                                  "/storage/emulated/0"),
                 QString("/storage/emulated/0/Download/Song.mp3"));
    }

    void documents_elsewhere_have_no_path() {
        QString root = "/storage/emulated/0";
        QStringList none = {
            // Google Drive, Dropbox: a cloud provider's own ids
            "content://com.google.android.apps.docs.storage/document/"
                "acc%3D1%3Bdoc%3Dencoded%3DabcDEF",
            "content://com.dropbox.android.document/document/"
                "%2FSongs%2Ftest.ton",
            // The media provider and a download, by number: looked up in
            // MediaStore instead (pathLookupFor())
            "content://com.android.providers.media.documents/document/audio%3A42",
            "content://com.android.providers.downloads.documents/document/msf%3A1234",
            "content://com.android.providers.downloads.documents/document/1234",
            // A raw id that is not a path
            "content://com.android.providers.downloads.documents/document/raw%3ASong.mp3",
            // The external storage provider, but no volume Tony knows
            "content://com.android.externalstorage.documents/document/home%3ADocuments",
            "content://com.android.externalstorage.documents/document/Music%2Ftest.ton",
            "content://com.android.externalstorage.documents/document/%3AMusic",
            // Not a document
            "content://com.android.externalstorage.documents/tree/primary%3AMusic",
            "content://com.android.externalstorage.documents/root/primary",
            // Not content:// at all
            "/storage/emulated/0/Music/test.ton",
            "file:///storage/emulated/0/Music/test.ton",
            "",
        };
        for (QString uri : none) {
            QCOMPARE(AndroidFiles::pathFromContentUri(uri, root), QString());
        }

        // Without the root there is nowhere to put primary's documents
        QCOMPARE(AndroidFiles::pathFromContentUri
                 ("content://com.android.externalstorage.documents/"
                  "document/primary%3AMusic%2Ftest.ton", ""),
                 QString());
    }

    void an_id_cannot_lead_out_of_its_volume() {
        QString root = "/storage/emulated/0";
        QCOMPARE(AndroidFiles::pathFromContentUri
                 ("content://com.android.externalstorage.documents/"
                  "document/primary%3A..%2F..%2F..%2Fdata%2Fx", root),
                 QString());
        QCOMPARE(AndroidFiles::pathFromContentUri
                 ("content://com.android.externalstorage.documents/"
                  "document/primary%3AMusic%2F..%2F..%2Fx", root),
                 QString());
        QCOMPARE(AndroidFiles::pathFromContentUri
                 ("content://com.android.providers.downloads.documents/"
                  "document/raw%3A%2Fstorage%2F..%2Fdata%2Fx", root),
                 QString());
    }

    // The user's session, whose name has spaces and parentheses, in each
    // form its URI can come in: as Android writes it; as
    // QFileDialog::selectedFiles() gives it (what the "File does not
    // exist" box showed); and as Qt's content file engine rebuilds it,
    // with the parentheses encoded as well
    void the_users_session_has_its_path_in_every_form_of_its_uri() {
        QString root = "/storage/emulated/0";
        QString provider =
            "content://com.android.externalstorage.documents/document/";
        QString name = "(vocals) Avi Kaplan - Peace Somehow.ton";
        QString inMusic = "/storage/emulated/0/Music/" + name;
        QString inDownload = "/storage/emulated/0/Download/" + name;

        QString android = provider + "primary%3AMusic%2F"
            "(vocals)%20Avi%20Kaplan%20-%20Peace%20Somehow.ton";
        QString shown = provider + "primary%3AMusic%2F"
            "(vocals) Avi Kaplan - Peace Somehow.ton";
        QString rebuilt = provider + "primary%3AMusic%2F"
            "%28vocals%29%20Avi%20Kaplan%20-%20Peace%20Somehow.ton";

        QCOMPARE(pickedAsQtGivesIt(android), shown);
        QCOMPARE(AndroidFiles::pathFromContentUri(android, root), inMusic);
        QCOMPARE(AndroidFiles::pathFromContentUri(shown, root), inMusic);
        QCOMPARE(AndroidFiles::pathFromContentUri(rebuilt, root), inMusic);
        QCOMPARE(AndroidFiles::pathLookupFor(shown),
                 AndroidFiles::PathLookup::InUri);

        // In Download, by the phone's storage or by the Downloads root
        QString download = provider + "primary%3ADownload%2F"
            "(vocals)%20Avi%20Kaplan%20-%20Peace%20Somehow.ton";
        QCOMPARE(AndroidFiles::pathFromContentUri(download, root), inDownload);
        QCOMPARE(AndroidFiles::pathFromContentUri
                 (pickedAsQtGivesIt(download), root), inDownload);
        QString raw = "content://com.android.providers.downloads.documents/"
            "document/raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2F"
            "(vocals)%20Avi%20Kaplan%20-%20Peace%20Somehow.ton";
        QCOMPARE(AndroidFiles::pathFromContentUri(raw, root), inDownload);
        QCOMPARE(AndroidFiles::pathFromContentUri
                 (pickedAsQtGivesIt(raw), root), inDownload);

        // Under a grant of the folder
        QString tree = "content://com.android.externalstorage.documents/"
            "tree/primary%3AMusic/document/primary%3AMusic%2F"
            "(vocals)%20Avi%20Kaplan%20-%20Peace%20Somehow.ton";
        QCOMPARE(AndroidFiles::pathFromContentUri(tree, root), inMusic);
        QCOMPARE(AndroidFiles::pathFromContentUri
                 (pickedAsQtGivesIt(tree), root), inMusic);
    }

    // What Tony opens a picked document by, when it has no path: the URI
    // exactly as Android wrote it, which is what Android granted
    void the_uri_is_opened_as_android_wrote_it() {
        QStringList uris = {
            "content://com.android.externalstorage.documents/document/"
                "primary%3AMusic%2F(vocals)%20Avi%20Kaplan%20-%20Peace%20"
                "Somehow.ton",
            // Uri.encode() leaves "_-!.~'()*" as they are, and encodes
            // the rest as UTF-8, in capitals
            "content://com.android.externalstorage.documents/document/"
                "primary%3ADownload%2FIt's%20a%20*star*!%20%C3%84%C3%A4ni"
                "%20%2B%26%3D%3B%2C%24%23%3F%25%5B%5D.wav",
            "content://com.android.externalstorage.documents/document/"
                "primary%3ADownload%2F%7B%7D%7C%5C%5E%60%22%3C%3E%40.wav",
            "content://com.android.externalstorage.documents/tree/"
                "primary%3AMusic/document/primary%3AMusic%2F(a)%20b.ton",
            "content://com.google.android.apps.docs.storage/document/"
                "acc%3D1%3Bdoc%3Dencoded%3DAbC-_x%2Fy%2Bz%3D%3D",
            "content://com.android.providers.downloads.documents/document/"
                "msf%3A1000001234",
            "content://com.android.providers.downloads.documents/document/"
                "raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2Fx%20(1).ton",
            "content://com.android.providers.downloads.documents/document/1234",
            "content://com.android.providers.media.documents/document/"
                "audio%3A42",
        };
        for (QString uri : uris) {
            QCOMPARE(AndroidFiles::grantedUri(QUrl(uri)), uri);
        }

        // Not the string the picker's selectedFiles() gives: that has the
        // spaces decoded, and is another URI to Android
        QVERIFY(pickedAsQtGivesIt(uris[0]) != uris[0]);
    }

    void the_provider_is_named() {
        QCOMPARE(AndroidFiles::providerOf
                 ("content://com.google.android.apps.docs.storage/document/"
                  "acc%3D1%3Bdoc%3Dencoded%3Dabc"),
                 QString("com.google.android.apps.docs.storage"));
        QCOMPARE(AndroidFiles::providerOf
                 ("content://com.android.externalstorage.documents/root/primary"),
                 QString("com.android.externalstorage.documents"));
        QCOMPARE(AndroidFiles::providerOf("/storage/emulated/0/x.ton"),
                 QString());
        QCOMPARE(AndroidFiles::providerOf(""), QString());
    }

    void downloads_and_media_are_looked_up_in_mediastore() {
        QString downloads =
            "content://com.android.providers.downloads.documents/document/";
        QString media =
            "content://com.android.providers.media.documents/document/";
        using L = AndroidFiles::PathLookup;

        QCOMPARE(AndroidFiles::pathLookupFor(downloads + "msf%3A1000001234"),
                 L::MediaStore);
        QCOMPARE(AndroidFiles::mediaStoreUriFor(downloads + "msf%3A1000001234"),
                 QString("content://media/external/downloads/1000001234"));
        // As the picker's selectedFiles() gives it
        QCOMPARE(AndroidFiles::mediaStoreUriFor(downloads + "msf:77"),
                 QString("content://media/external/downloads/77"));

        QCOMPARE(AndroidFiles::pathLookupFor(media + "audio%3A42"),
                 L::MediaStore);
        QCOMPARE(AndroidFiles::mediaStoreUriFor(media + "audio%3A42"),
                 QString("content://media/external/audio/media/42"));
        QCOMPARE(AndroidFiles::mediaStoreUriFor(media + "image%3A7"),
                 QString("content://media/external/images/media/7"));
        QCOMPARE(AndroidFiles::mediaStoreUriFor(media + "video%3A8"),
                 QString("content://media/external/video/media/8"));
        QCOMPARE(AndroidFiles::mediaStoreUriFor(media + "document%3A9"),
                 QString("content://media/external/file/9"));

        // A download known by its number in the download manager
        QCOMPARE(AndroidFiles::pathLookupFor(downloads + "1234"),
                 L::ByNameAndSize);
        QCOMPARE(AndroidFiles::mediaStoreUriFor(downloads + "1234"), QString());

        // The path is in the URI
        QCOMPARE(AndroidFiles::pathLookupFor
                 (downloads + "raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2Fa.ton"),
                 L::InUri);
        QCOMPARE(AndroidFiles::pathLookupFor
                 ("content://com.android.externalstorage.documents/document/"
                  "primary%3AMusic%2Fa.ton"),
                 L::InUri);

        // Nothing to look up: folders, the providers' roots and groups, a
        // cloud provider's documents, ids that are not numbers
        QStringList none = {
            downloads + "msd%3A55",
            downloads + "downloads",
            downloads + "msf%3A12a",
            downloads + "raw%3ASong.mp3",
            media + "audio_root",
            media + "album%3A3",
            media + "artist%3A4",
            media + "images_bucket%3A5",
            media + "audio%3A",
            "content://com.android.providers.media.documents/tree/audio_root",
            "content://com.google.android.apps.docs.storage/document/"
                "acc%3D1%3Bdoc%3Dencoded%3Dabc",
            "content://com.android.externalstorage.documents/document/Music",
            "content://media/external/audio/media/42",
            "/storage/emulated/0/Download/a.ton",
            "",
        };
        for (QString uri : none) {
            QCOMPARE(AndroidFiles::pathLookupFor(uri), L::None);
            QCOMPARE(AndroidFiles::mediaStoreUriFor(uri), QString());
        }
    }

    void a_numbered_download_is_found_by_name_and_size() {
        QString root = "/storage/emulated/0";
        QString download = "/storage/emulated/0/Download/Song (1).mp3";
        QString copy = "/storage/emulated/0/Music/Song (1).mp3";
        QString deeper = "/storage/emulated/0/Download/Old/Song (1).mp3";

        QCOMPARE(AndroidFiles::chooseDownload({ download }, root), download);
        QCOMPARE(AndroidFiles::chooseDownload({ copy }, root), copy);

        // Several of that name and size: the one in Download
        QCOMPARE(AndroidFiles::chooseDownload({ copy, download, deeper }, root),
                 download);
        QCOMPARE(AndroidFiles::chooseDownload({ download, copy }, root + "/"),
                 download);
        QCOMPARE(AndroidFiles::chooseDownload({ download, download }, root),
                 download);

        // Not settled
        QCOMPARE(AndroidFiles::chooseDownload({ copy, deeper }, root), QString());
        QCOMPARE(AndroidFiles::chooseDownload({ download, copy }, ""), QString());
        QCOMPARE(AndroidFiles::chooseDownload({}, root), QString());
        QCOMPARE(AndroidFiles::chooseDownload({ "" }, root), QString());
    }

    void only_tonys_own_files_are_opened() {
        QString session = "*.ton";
        QString audio = "*.aiff *.flac *.mp3 *.ogg *.opus *.wav";

        QVERIFY(AndroidFiles::hasExtensionIn
                ("(vocals) Avi Kaplan - Peace Somehow.ton", session));
        QVERIFY(AndroidFiles::hasExtensionIn("Song.MP3", audio));
        QVERIFY(AndroidFiles::hasExtensionIn("Song v1.2.wav", audio));
        QVERIFY(AndroidFiles::hasExtensionIn("a.WaV", "*.WAV"));

        QVERIFY(!AndroidFiles::hasExtensionIn("Song.ton", audio));
        QVERIFY(!AndroidFiles::hasExtensionIn("Notes.pdf", session + " " + audio));
        QVERIFY(!AndroidFiles::hasExtensionIn("Song", audio));
        QVERIFY(!AndroidFiles::hasExtensionIn("Song.mp3.part", audio));
        QVERIFY(!AndroidFiles::hasExtensionIn("mp3", audio));
        QVERIFY(!AndroidFiles::hasExtensionIn("", audio));
        QVERIFY(!AndroidFiles::hasExtensionIn("Song.mp3", ""));
    }

    void recent_files_that_are_gone_are_left_out() {
        QString dir = newDir("recent");
        QString here = dir + "/Here.ton";
        QString moved = dir + "/Moved.ton";
        QVERIFY(writeFile(here, "BZh91AY&SY"));

        QStringList recent = {
            moved,
            here,
            "content://com.android.externalstorage.documents/document/"
                "primary%3AMusic%2FSong.mp3",
            newDir("a-folder"),
            "",
        };
        QCOMPARE(AndroidFiles::usableRecentFiles(recent), QStringList({ here }));
    }

    // --- The name Save Session As suggests and accepts ---

    void the_suggested_name_is_the_sessions_or_the_references() {
        QCOMPARE(AndroidFiles::suggestedSessionName
                 ("/storage/emulated/0/Music/My Song.ton",
                  "/storage/emulated/0/Music/Other.mp3"),
                 QString("My Song.ton"));
        QCOMPARE(AndroidFiles::suggestedSessionName
                 ("", "/data/user/0/io.github.jhhr.tony/files/imported/"
                  "Ääni v1.2.mp3"),
                 QString("Ääni v1.2.ton"));
        QCOMPARE(AndroidFiles::suggestedSessionName("", ""), QString());
    }

    void a_picked_name_without_an_extension_gets_one() {
        QCOMPARE(AndroidFiles::sessionFileName("test.ton"), QString("test.ton"));
        QCOMPARE(AndroidFiles::sessionFileName("test"), QString("test.ton"));
        QCOMPARE(AndroidFiles::sessionFileName("My Song v1.2"),
                 QString("My Song v1.2"));
        QCOMPARE(AndroidFiles::sessionFileName("test (1).ton"),
                 QString("test (1).ton"));
    }

    void a_picked_name_that_names_nothing_is_refused() {
        QCOMPARE(AndroidFiles::sessionFileName(""), QString());
        QCOMPARE(AndroidFiles::sessionFileName("   "), QString());
        QCOMPARE(AndroidFiles::sessionFileName(".ton"), QString());
        QCOMPARE(AndroidFiles::sessionFileName(" .ton"), QString());
        QCOMPARE(AndroidFiles::sessionFileName("."), QString());
        QCOMPARE(AndroidFiles::sessionFileName("..ton"), QString());
        QCOMPARE(AndroidFiles::sessionFileName("(invalid)"), QString());
    }

    void only_an_empty_document_is_removed() {
        QString dir = newDir("picked");

        // What the picker leaves for a save that is not made there
        QString empty = dir + "/test.ton";
        QVERIFY(writeFile(empty, ""));
        QVERIFY(AndroidFiles::removeIfEmpty(empty));
        QVERIFY(!QFileInfo::exists(empty));

        // A session that was there before is not to be touched
        QString session = dir + "/Song.ton";
        QVERIFY(writeFile(session, "BZh91AY&SY"));
        QVERIFY(!AndroidFiles::removeIfEmpty(session));
        QCOMPARE(readFile(session), QByteArray("BZh91AY&SY"));

        QVERIFY(!AndroidFiles::removeIfEmpty(dir + "/not-there.ton"));
        QVERIFY(!AndroidFiles::removeIfEmpty(newDir("folder")));
        QVERIFY(!AndroidFiles::removeIfEmpty(""));
    }

    // --- The Vamp plugin links ---

    void the_plugins_are_linked_under_their_own_names() {
#ifdef Q_OS_WIN
        QSKIP("Symbolic links: the Android build's, not Windows'");
#endif
        QString libraries = installedLibraries("pyin", "chp");
        QString vamp = newPath("vamp"); // not there yet

        QStringList problems;
        QStringList links = AndroidFiles::linkVampPlugins
            (libraries, vamp, { "pyin", "chp" }, problems);

        QCOMPARE(problems, QStringList());
        QCOMPARE(links, QStringList({ vamp + "/pyin.so", vamp + "/chp.so" }));

        // The scan opens every .so it finds: the plugins, and nothing else
        QCOMPARE(scanned(vamp), QStringList({ "chp.so", "pyin.so" }));
        QVERIFY(QFileInfo(links[0]).isSymLink());
        QCOMPARE(readFile(links[0]), QByteArray("pyin"));
        QCOMPARE(readFile(links[1]), QByteArray("chp"));

        // svcore names a plugin after its library's file name, and Tony
        // asks for pYIN as vamp:pyin:pyin:... (Analyser)
        QCOMPARE(sv::PluginIdentifier::createIdentifier("vamp", links[0], "pyin"),
                 QString("vamp:pyin:pyin"));
    }

    void links_left_by_an_earlier_install_are_replaced() {
#ifdef Q_OS_WIN
        QSKIP("Symbolic links: the Android build's, not Windows'");
#endif
        QString vamp = newPath("vamp");
        QStringList problems;

        QString before = installedLibraries("old pyin", "old chp");
        AndroidFiles::linkVampPlugins(before, vamp, { "pyin", "chp" }, problems);
        QCOMPARE(problems, QStringList());

        // An update installs the libraries in a new folder and removes
        // the old one, leaving the links pointing nowhere
        QVERIFY(QDir(before).removeRecursively());
        QString after = installedLibraries("new pyin", "new chp");

        QStringList links = AndroidFiles::linkVampPlugins
            (after, vamp, { "pyin", "chp" }, problems);

        QCOMPARE(problems, QStringList());
        QCOMPARE(links.size(), 2);
        QCOMPARE(readFile(links[0]), QByteArray("new pyin"));
        QCOMPARE(readFile(links[1]), QByteArray("new chp"));
        QCOMPARE(QFileInfo(links[0]).symLinkTarget(),
                 QFileInfo(after + "/libpyin.so").absoluteFilePath());
        QCOMPARE(scanned(vamp), QStringList({ "chp.so", "pyin.so" }));
    }

    void a_missing_plugin_is_reported_and_the_rest_linked() {
#ifdef Q_OS_WIN
        QSKIP("Symbolic links: the Android build's, not Windows'");
#endif
        QString vamp = newPath("vamp");
        QStringList problems;
        AndroidFiles::linkVampPlugins(installedLibraries("pyin", "chp"), vamp,
                                      { "pyin", "chp" }, problems);
        QCOMPARE(problems, QStringList());

        QStringList links = AndroidFiles::linkVampPlugins
            (installedLibraries("pyin", ""), vamp, { "pyin", "chp" }, problems);

        QCOMPARE(links, QStringList({ vamp + "/pyin.so" }));
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems[0].contains("libchp.so"));
        // Not the link from before, to a library that is not there now
        QCOMPARE(scanned(vamp), QStringList({ "pyin.so" }));
    }

    void a_linked_plugin_is_checked_as_the_scan_would_load_it() {
#ifdef Q_OS_WIN
        QSKIP("Symbolic links: the Android build's, not Windows'");
#endif
        // The real pYIN, which the build leaves beside this executable,
        // installed as Android installs it; and a library that is not one
        QString built = QCoreApplication::applicationDirPath() + "/pyin.so";
        if (!QFileInfo::exists(built)) {
            QSKIP("No pyin.so beside the test executable: build it first");
        }
        QString libraries = installedLibraries("", "not a library");
        QVERIFY(QFile::copy(built, libraries + "/libpyin.so"));

        QString vamp = newPath("vamp");
        QStringList problems;
        QStringList links = AndroidFiles::linkVampPlugins
            (libraries, vamp, { "pyin", "chp" }, problems);
        QCOMPARE(problems, QStringList());
        QCOMPARE(links.size(), 2);

        QCOMPARE(AndroidFiles::checkVampPlugin(links[0]), QString());

        QString problem = AndroidFiles::checkVampPlugin(links[1]);
        QVERIFY(problem.startsWith("Cannot load " + links[1] + ": "));
        QVERIFY(problem.size() > QString("Cannot load " + links[1] + ": ").size());
    }
};

#endif
