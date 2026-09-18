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

#include "RunSuite.h"

#include "system/Init.h"

#include <QApplication>
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

    {
        TestSingingDocument t;
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
