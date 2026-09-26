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

#include "TestSingingDocument.h"
#include "TestViewCache.h"
#include "TestSingingAnalysis.h"
#include "TestRecordWorkflow.h"
#include "TestUiChecks.h"
#include "TestAudioCheck.h"

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
    int good = 0, bad = 0;

    svSystemSpecificInitialisation();

    // Widgets are created but never shown. meson runs this with
    // QT_QPA_PLATFORM=offscreen; default to that when run by hand too.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    // Names distinct from the application's, so that nothing here reads
    // or writes the user's real Tony settings
    QApplication app(argc, argv);
    app.setOrganizationName("tony-tests");
    app.setApplicationName("test-tony-app");

    // Text in shades of grey, whatever the machine's fontconfig asks for.
    // Ubuntu's asks for sub-pixel anti-aliasing, and Qt 6.4 follows it:
    // the labels of a scale then have orange and blue fringes, and
    // TestUiChecks, which finds the take's dots by their orange, takes
    // the fringes for dots
    QFont font = QApplication::font();
    font.setStyleStrategy(QFont::NoSubpixelAntialias);
    QApplication::setFont(font);

    // Tier 4 runs the real pYIN plugin, which the build leaves next to
    // this executable. Replace rather than extend VAMP_PATH, so that a
    // pYIN installed elsewhere on this machine is never the one tested.
    qputenv("VAMP_PATH",
            QDir::toNativeSeparators(app.applicationDirPath()).toLocal8Bit());

    {
        TestSingingDocument t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestViewCache t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestSingingAnalysis t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestRecordWorkflow t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestUiChecks t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestAudioCheck t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    (void)good;

    if (bad > 0) {
        SVCERR << "\n********* " << bad << " test suite(s) failed!\n" << endl;
        return 1;
    } else {
        SVCERR << "All tests passed" << endl;
        return 0;
    }
}
