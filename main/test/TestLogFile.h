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

#ifndef TEST_LOG_FILE_H
#define TEST_LOG_FILE_H

// Tier 1: the copy of Tony's output kept on Android for Help > Save
// Log... (LogFile): an earlier run's kept once, the whole never much
// more than twice its limit, and read back oldest first.

#include "../LogFile.h"

#include <QObject>
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

class TestLogFile : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_counter = 0;

    QString newPath() {
        return m_dir.filePath(QString("log-%1/tony.log").arg(++m_counter));
    }

    static bool writeFile(QString path, QByteArray content) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        bool ok = (file.write(content) == content.size());
        file.close();
        return ok;
    }

    static qint64 size(QString path) {
        return QFileInfo(path).size();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void a_log_is_begun_in_a_folder_not_there_yet() {
        QString path = newPath();
        LogFile log(path, 1000);
        QVERIFY(!log.isOpen());
        QVERIFY(log.open());
        QVERIFY(log.isOpen());

        log.write("Setting VAMP_PATH to /data/user/0/io.github.jhhr.tony/files/vamp");
        QCOMPARE(LogFile::contents(path),
                 QByteArray("Setting VAMP_PATH to /data/user/0/"
                            "io.github.jhhr.tony/files/vamp\n"));
    }

    void the_run_before_is_kept_and_the_one_before_that_goes() {
        QString path = newPath();
        QVERIFY(writeFile(LogFile::olderPath(path), "two runs ago\n"));
        QVERIFY(writeFile(path, "the last run\n"));

        LogFile log(path, 1000);
        QVERIFY(log.open());
        log.write("this run");

        QCOMPARE(LogFile::contents(path),
                 QByteArray("the last run\nthis run\n"));
    }

    void a_log_past_its_limit_begins_again() {
        QString path = newPath();
        const qint64 limit = 100;
        LogFile log(path, limit);
        QVERIFY(log.open());

        // Each line 10 bytes with its newline
        for (int i = 0; i < 50; ++i) {
            log.write(QString("line %1...").arg(i, 2, 10, QChar('0')).toUtf8()
                      .left(9));
        }

        QVERIFY(size(path) < limit);
        QVERIFY(size(LogFile::olderPath(path)) <= limit);

        QByteArray all = LogFile::contents(path);
        QVERIFY(all.size() <= 2 * limit);
        QVERIFY(all.size() >= limit);
        QVERIFY(all.endsWith("line 49..\n"));
        QVERIFY(!all.contains("line 00"));

        // In order, oldest first, no line cut
        QList<QByteArray> lines = all.split('\n');
        lines.removeLast();
        int first = lines.first().mid(5, 2).toInt();
        for (int i = 0; i < lines.size(); ++i) {
            QCOMPARE(lines[i], QString("line %1..").arg(first + i, 2, 10,
                                                        QChar('0')).toUtf8());
        }
    }

    void there_is_nothing_to_read_before_a_log_is_begun() {
        QCOMPARE(LogFile::contents(newPath()), QByteArray());
    }
};

#endif
