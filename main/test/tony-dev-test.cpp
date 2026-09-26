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

// The development checks' own suite, built only where they are
// (TONY_DEV_CHECKS). Kept out of test-tony-app because each of its
// tests records several takes in real time: run it when a change
// touches what the checks drive (see AGENTS.md)

#include "TestDevChecks.h"

#include "RunSuite.h"

#include "system/Init.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QtTest>

#include <iostream>

using namespace std;
using namespace sv;

int main(int argc, char *argv[])
{
    svSystemSpecificInitialisation();

    // Offscreen, as test-tony-app
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    // Names distinct from the application's, so that nothing here reads
    // or writes the user's real Tony settings, and a shard's distinct
    // from the other shards', so that shards running at once keep apart
    QApplication app(argc, argv);
    app.setOrganizationName("tony-tests");
    app.setApplicationName(shardApplicationName
                           ("test-tony-dev",
                            qEnvironmentVariable("TONY_TEST_SHARD")));

    // Text in shades of grey, as in test-tony-app
    QFont font = QApplication::font();
    font.setStyleStrategy(QFont::NoSubpixelAntialias);
    QApplication::setFont(font);

    // The real pYIN plugin, from next to this executable only
    qputenv("VAMP_PATH",
            QDir::toNativeSeparators(app.applicationDirPath()).toLocal8Bit());

    TestDevChecks t;
    bool ok = runSuite(&t, argc, argv);

    if (!ok) {
        SVCERR << "\n********* the dev checks' suite failed!\n" << endl;
        return 1;
    } else {
        SVCERR << "All tests passed" << endl;
        return 0;
    }
}
