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
// links) and when a file is picked (the copy into app storage), done here
// on plain files in a temporary directory. What only a phone has -- the
// content:// URI, the installed library directory -- is not here.

#include "../AndroidFiles.h"

#include "plugin/PluginIdentifier.h"

#include <QObject>
#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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
