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

#include "TestAndroidFiles.h"
#include "TestRealtimeYin.h"
#include "TestRealtimePitchTracker.h"
#include "TestLatencyShift.h"
#include "TestCoverage.h"
#include "TestPinchZoom.h"
#include "TestStreamLatency.h"
#include "TestTakeAudio.h"
#include "TestTakeEvents.h"
#include "TestSingingTakes.h"
#include "TestTakesFile.h"
#include "TestTakeTiming.h"

#include "RunSuite.h"

#include "system/Init.h"

#include <QtTest>

#include <iostream>

using namespace std;
using namespace sv;

int main(int argc, char *argv[])
{
    int good = 0, bad = 0;

    svSystemSpecificInitialisation();

    // Names distinct from the application's, so that nothing here reads
    // or writes the user's real Tony settings
    QCoreApplication app(argc, argv);
    app.setOrganizationName("tony-tests");
    app.setApplicationName("test-tony-core");

    {
        TestAndroidFiles t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestRealtimeYin t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestRealtimePitchTracker t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestLatencyShift t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestCoverage t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestPinchZoom t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestStreamLatency t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestTakeAudio t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestTakeEvents t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestSingingTakes t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestTakesFile t;
        if (runSuite(&t, argc, argv)) ++good;
        else ++bad;
    }

    {
        TestTakeTiming t;
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
