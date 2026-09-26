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

// The checks that need a real audio device and a microphone that can
// hear the speakers. Run by hand, never by meson test: see
// docs/manual-checklist.md

#include "TestRealDevice.h"

#include "RunSuite.h"

#include "system/Init.h"

#include <QApplication>
#include <QDir>
#include <QtTest>

#include <iostream>

using namespace std;
using namespace sv;

int main(int argc, char *argv[])
{
    svSystemSpecificInitialisation();

    // Not offscreen by default, unlike the other suites: the window is
    // there to be watched while it records

    // Names distinct from the application's: Tony's own settings are only
    // read, for the choice of device
    QApplication app(argc, argv);
    app.setOrganizationName("tony-tests");
    app.setApplicationName("test-tony-device");

    qputenv("VAMP_PATH",
            QDir::toNativeSeparators(app.applicationDirPath()).toLocal8Bit());

    TestRealDevice t;
    bool ok = runSuite(&t, argc, argv);

    if (!ok) {
        SVCERR << "\n********* the device check failed\n" << endl;
        return 1;
    } else {
        SVCERR << "The device check passed" << endl;
        return 0;
    }
}
