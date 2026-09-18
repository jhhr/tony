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

/**
 * Run one suite with the command-line arguments given. If the
 * environment variable TONY_TEST_LOG_DIR is set, the suite's results
 * are also written to <dir>/<SuiteClassName>.txt. A single "-o file"
 * argument cannot do that, as each suite would overwrite the last.
 */
inline bool
runSuite(QObject *suite, int argc, char *argv[])
{
    QStringList args;
    for (int i = 0; i < argc; ++i) {
        args << QString::fromLocal8Bit(argv[i]);
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
