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

#include "AndroidStorage.h"
#include "AndroidFiles.h"

#include <QAbstractButton>
#include <QApplication>
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMessageBox>
#include <QPushButton>
#include <QtCore/qcoreapplication_platform.h>

#include <iostream>

using std::cerr;
using std::endl;

AndroidStorage::AndroidStorage(QWidget *parent) :
    m_parent(parent),
    m_asked(false)
{
}

bool
AndroidStorage::hasAllFilesAccess()
{
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 30) {
        return false;
    }
    return QJniObject::callStaticMethod<jboolean>
        ("android/os/Environment", "isExternalStorageManager", "()Z");
}

QString
AndroidStorage::primaryRoot()
{
    QJniObject directory = QJniObject::callStaticObjectMethod
        ("android/os/Environment", "getExternalStorageDirectory",
         "()Ljava/io/File;");
    if (!directory.isValid()) return "";
    return directory.callObjectMethod
        ("getAbsolutePath", "()Ljava/lang/String;").toString();
}

QString
AndroidStorage::pathFor(QString uri) const
{
    return AndroidFiles::pathFromContentUri(uri, primaryRoot());
}

bool
AndroidStorage::openSettings(bool thisApp)
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return false;

    QJniObject action = QJniObject::getStaticObjectField
        ("android/provider/Settings",
         thisApp ? "ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION"
                 : "ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION",
         "Ljava/lang/String;");
    if (!action.isValid()) return false;

    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      action.object());
    if (!intent.isValid()) return false;

    if (thisApp) {
        QString package = context.callObjectMethod
            ("getPackageName", "()Ljava/lang/String;").toString();
        QJniObject uri = QJniObject::callStaticObjectMethod
            ("android/net/Uri", "parse",
             "(Ljava/lang/String;)Landroid/net/Uri;",
             QJniObject::fromString("package:" + package).object());
        intent.callObjectMethod("setData",
                                "(Landroid/net/Uri;)Landroid/content/Intent;",
                                uri.object());
    }

    if (!QNativeInterface::QAndroidApplication::isActivityContext()) {
        const jint newTask = 0x10000000; // Intent.FLAG_ACTIVITY_NEW_TASK
        intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;",
                                newTask);
    }

    // Called through JNI directly: QJniObject clears the exception a phone
    // without the page throws (ActivityNotFoundException) and tells no one
    QJniEnvironment env;
    jclass contextClass = env->GetObjectClass(context.object());
    jmethodID start = env->GetMethodID(contextClass, "startActivity",
                                       "(Landroid/content/Intent;)V");
    env->DeleteLocalRef(contextClass);
    if (!start) {
        env.checkAndClearExceptions();
        return false;
    }
    env->CallVoidMethod(context.object(), start, intent.object());
    if (env.checkAndClearExceptions()) {
        cerr << "AndroidStorage: could not open the settings page "
             << (thisApp ? "for this app" : "listing the apps") << endl;
        return false;
    }
    return true;
}

bool
AndroidStorage::ask(QString why)
{
    m_asked = true;
    if (hasAllFilesAccess()) return true;

    QString app = QApplication::applicationName();

    if (QNativeInterface::QAndroidApplication::sdkVersion() < 30) {
        QMessageBox::information
            (m_parent, tr("Files in the phone's storage"),
             tr("<b>%1 cannot use files where they are on this phone</b>"
                "<p>%2</p><p>That needs All files access, which Android "
                "has from version 11 on.</p>").arg(app, why));
        return false;
    }

    QMessageBox box(QMessageBox::Question, tr("All files access"),
                    tr("<b>Allow %1 to use files where they are?</b>"
                       "<p>%2</p><p>This needs <i>All files access</i>. "
                       "Open Settings, switch on <i>Allow access to manage "
                       "all files</i> for %1, and come back.</p>")
                    .arg(app, why),
                    QMessageBox::NoButton, m_parent);
    QAbstractButton *open =
        box.addButton(tr("Open Settings"), QMessageBox::AcceptRole);
    box.addButton(tr("Not Now"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != open) return false;

    // Up while the settings page is, and there when the user comes back:
    // it goes by itself if access was given there, and otherwise waits
    // to be told, which also covers a settings page shown beside Tony
    // (split screen), when Tony never goes away. Qt's own box, not
    // Android's: Qt cannot close Android's from here
    bool away = false;
    QMessageBox wait(QMessageBox::Information, tr("All files access"),
                     tr("<b>Waiting for All files access</b><p>Switch on "
                        "<i>Allow access to manage all files</i> for %1 on "
                        "the settings page, then come back here.</p>")
                     .arg(app),
                     QMessageBox::NoButton, m_parent);
    wait.setOption(QMessageBox::Option::DontUseNativeDialog);
    wait.addButton(tr("Continue"), QMessageBox::AcceptRole);
    wait.addButton(tr("Cancel"), QMessageBox::RejectRole);
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, &wait,
                     [&](Qt::ApplicationState state) {
                         if (state != Qt::ApplicationActive) {
                             away = true;
                         } else if (away && hasAllFilesAccess()) {
                             wait.accept();
                         }
                     });

    if (!openSettings(true) && !openSettings(false)) {
        QMessageBox::warning
            (m_parent, tr("All files access"),
             tr("<b>The settings page could not be opened</b><p>Open the "
                "phone's Settings, then Apps, %1, and allow All files "
                "access there (on some phones it is under Special app "
                "access).</p>").arg(app));
        return hasAllFilesAccess();
    }

    wait.exec();

    bool allowed = hasAllFilesAccess();
    cerr << "AndroidStorage: All files access "
         << (allowed ? "given" : "not given") << endl;
    return allowed;
}
