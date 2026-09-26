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

#ifndef TEST_RUN_SUITE_H
#define TEST_RUN_SUITE_H

#include <QtTest>
#include <QDir>
#include <QMetaMethod>

/**
 * The test functions of a suite that shard \a shard of \a count runs:
 * every count-th one in declaration order, starting at the shard's
 * number. The test functions are what QTest takes them to be, the
 * private slots without arguments, less initTestCase, cleanupTestCase,
 * init, cleanup and the _data functions.
 */
inline QStringList
shardFunctions(const QObject *suite, int shard, int count)
{
    QStringList names;
    const QMetaObject *mo = suite->metaObject();
    int index = 0;
    for (int i = 0; i < mo->methodCount(); ++i) {
        QMetaMethod m = mo->method(i);
        if (m.methodType() != QMetaMethod::Slot ||
            m.access() != QMetaMethod::Private ||
            m.parameterCount() != 0) {
            continue;
        }
        QString name = QString::fromLatin1(m.name());
        if (name == "initTestCase" || name == "cleanupTestCase" ||
            name == "init" || name == "cleanup" || name.endsWith("_data")) {
            continue;
        }
        if (index++ % count == shard) {
            names << name;
        }
    }
    return names;
}

/**
 * Run one suite with the command-line arguments given. If the
 * environment variable TONY_TEST_LOG_DIR is set, the suite's results
 * are also written to <dir>/<SuiteClassName>.txt. A single "-o file"
 * argument cannot do that, as each suite would overwrite the last.
 *
 * If TONY_TEST_SHARD is set to "i/n", only the suite's shard i of n
 * runs (shardFunctions()), so that n processes started at once run the
 * whole suite between them in about 1/n of the time: most tests wait
 * on a fake device playing in real time. A suite with nothing in the
 * shard is not run. Not for use with test names on the command line.
 */
inline bool
runSuite(QObject *suite, int argc, char *argv[])
{
    QStringList args;
    for (int i = 0; i < argc; ++i) {
        args << QString::fromLocal8Bit(argv[i]);
    }
    QString shard = qEnvironmentVariable("TONY_TEST_SHARD");
    if (shard != "") {
        QStringList parts = shard.split('/');
        int n = (parts.size() == 2 ? parts[1].toInt() : 0);
        int i = (parts.size() == 2 ? parts[0].toInt() : -1);
        if (n < 1 || i < 0 || i >= n) {
            qWarning("TONY_TEST_SHARD must be i/n with 0 <= i < n, not \"%s\"",
                     qPrintable(shard));
            return false;
        }
        QStringList names = shardFunctions(suite, i, n);
        if (names.isEmpty()) {
            return true;
        }
        args << names;
    }
    QString logDir = qEnvironmentVariable("TONY_TEST_LOG_DIR");
    if (logDir != "") {
        QString file = QDir(logDir).filePath
            (QString("%1.txt").arg(suite->metaObject()->className()));
        args << "-o" << (file + ",txt") << "-o" << "-,txt";
    }
    return QTest::qExec(suite, args) == 0;
}

#endif
