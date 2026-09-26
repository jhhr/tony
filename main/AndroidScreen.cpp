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

#include "AndroidScreen.h"

#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

#include <iostream>

using std::cerr;
using std::endl;

namespace {

// WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
const jint keepScreenOnFlag = 0x00000080;

}

void
AndroidScreen::keepOn(bool on, const char *why)
{
    if (!QNativeInterface::QAndroidApplication::isActivityContext()) {
        cerr << "AndroidScreen: no activity to keep the screen on for ("
             << why << ")" << endl;
        return;
    }

    // Changed on the GUI thread, a window's flags throw: the view is
    // Android's main thread's.  Not waited for
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        QJniObject window = activity.callObjectMethod
            ("getWindow", "()Landroid/view/Window;");
        if (window.isValid()) {
            const jint flag = keepScreenOnFlag;
            window.callMethod<void>(on ? "addFlags" : "clearFlags", "(I)V",
                                    flag);
        }
        QJniEnvironment env;
        if (!window.isValid() || env.checkAndClearExceptions()) {
            cerr << "AndroidScreen: the window's flag could not be "
                 << (on ? "set" : "cleared") << endl;
        }
    });

    cerr << "AndroidScreen: " << (on ? "keeping the screen on: "
                                  : "no longer keeping the screen on: ")
         << why << endl;
}
