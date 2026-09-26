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
#include <QStandardPaths>
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

// ContentResolver calls are made through JNI directly rather than through
// QJniObject, which clears an exception a call throws (a
// SecurityException for a URI without a grant, say) and tells only the
// system log: here it is caught and said, for the user's message and
// Tony's own log
namespace {

// The exception the last call left, as Java names it, cleared; "" if
// there is none
QString
takeException(QJniEnvironment &env)
{
    if (!env->ExceptionCheck()) return "";
    jthrowable thrown = env->ExceptionOccurred();
    env->ExceptionClear();
    QString text("an unknown exception");
    if (thrown) {
        jclass thrownClass = env->GetObjectClass(thrown);
        jmethodID toString = env->GetMethodID
            (thrownClass, "toString", "()Ljava/lang/String;");
        jobject description =
            (toString ? env->CallObjectMethod(thrown, toString) : nullptr);
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (description) {
            text = QJniObject::fromLocalRef(description).toString();
        }
        env->DeleteLocalRef(thrownClass);
        env->DeleteLocalRef(thrown);
    }
    return text;
}

QJniObject
contentResolver()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return QJniObject();
    return context.callObjectMethod
        ("getContentResolver", "()Landroid/content/ContentResolver;");
}

// Uri.parse(): the same string, to the character, so that a grant for it
// is found
QJniObject
parseUri(QString uri)
{
    return QJniObject::callStaticObjectMethod
        ("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
         QJniObject::fromString(uri).object<jstring>());
}

jobjectArray
stringArray(QJniEnvironment &env, QStringList strings)
{
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray array = env->NewObjectArray(jsize(strings.size()),
                                             stringClass, nullptr);
    for (int i = 0; i < strings.size(); ++i) {
        QJniObject s = QJniObject::fromString(strings[i]);
        env->SetObjectArrayElement(array, jsize(i), s.object());
    }
    env->DeleteLocalRef(stringClass);
    return array;
}

// ContentResolver.query(): each row the values of columns in order, as
// strings ("" for none). Empty, with error saying why, if it fails
QList<QStringList>
query(QString uri, QStringList columns, QString selection,
      QStringList arguments, QString &error)
{
    QList<QStringList> rows;

    QJniObject resolver = contentResolver();
    QJniObject parsed = parseUri(uri);
    if (!resolver.isValid() || !parsed.isValid()) {
        error = "no content resolver";
        return rows;
    }

    QJniEnvironment env;
    jclass resolverClass = env->GetObjectClass(resolver.object());
    jmethodID queryMethod = env->GetMethodID
        (resolverClass, "query",
         "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;"
         "[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;");
    env->DeleteLocalRef(resolverClass);
    if (!queryMethod) {
        error = takeException(env);
        return rows;
    }

    jobjectArray projection = stringArray(env, columns);
    jobjectArray selectionArgs =
        (arguments.empty() ? nullptr : stringArray(env, arguments));
    QJniObject selectionString;
    if (selection != "") selectionString = QJniObject::fromString(selection);

    jobject cursor = env->CallObjectMethod
        (resolver.object(), queryMethod, parsed.object(), projection,
         selectionString.isValid() ? selectionString.object() : nullptr,
         selectionArgs, nullptr);
    QString thrown = takeException(env);

    env->DeleteLocalRef(projection);
    if (selectionArgs) env->DeleteLocalRef(selectionArgs);

    if (thrown != "") {
        error = thrown;
        return rows;
    }
    if (!cursor) {
        error = "the provider answered nothing";
        return rows;
    }

    QJniObject c = QJniObject::fromLocalRef(cursor);
    while (c.callMethod<jboolean>("moveToNext", "()Z")) {
        QStringList row;
        for (int i = 0; i < columns.size(); ++i) {
            row << c.callObjectMethod("getString", "(I)Ljava/lang/String;",
                                      jint(i)).toString();
        }
        rows << row;
    }
    c.callMethod<void>("close", "()V");
    return rows;
}

}

QString
AndroidStorage::pathFor(QString uri, QString &why)
{
    QString app = QApplication::applicationName();
    QString error;

    switch (AndroidFiles::pathLookupFor(uri)) {

    case AndroidFiles::PathLookup::None:
        why = tr("its provider gives no path for it");
        return "";

    case AndroidFiles::PathLookup::InUri: {
        QString path = AndroidFiles::pathFromContentUri(uri, primaryRoot());
        if (path == "") why = tr("its path could not be read from its URI");
        return path;
    }

    case AndroidFiles::PathLookup::MediaStore: {
        if (!hasAllFilesAccess()) {
            why = tr("%1 has no All files access, without which it cannot "
                     "look up where the file is").arg(app);
            return "";
        }
        QString row = AndroidFiles::mediaStoreUriFor(uri);
        QList<QStringList> rows = query(row, { "_data" }, "", {}, error);
        if (rows.size() == 1 && rows[0].size() == 1 && rows[0][0] != "") {
            return rows[0][0];
        }
        why = (error != "" ?
               tr("MediaStore could not be asked for %1: %2").arg(row, error) :
               tr("MediaStore has no path for %1").arg(row));
        return "";
    }

    case AndroidFiles::PathLookup::ByNameAndSize: {
        if (!hasAllFilesAccess()) {
            why = tr("%1 has no All files access, without which it cannot "
                     "look up where the file is").arg(app);
            return "";
        }
        // The name and size the downloads provider gives, through the
        // picker's grant; then the files of that name and size
        QList<QStringList> document =
            query(uri, { "_display_name", "_size" }, "", {}, error);
        if (document.size() != 1 || document[0].size() != 2 ||
            document[0][0] == "") {
            why = (error != "" ?
                   tr("its provider could not be asked for its name: %1")
                   .arg(error) :
                   tr("its provider gives no name for it"));
            return "";
        }
        QString name = document[0][0];
        QString size = document[0][1];
        QList<QStringList> files =
            query("content://media/external/file", { "_data" },
                  "_display_name = ? AND _size = ?", { name, size }, error);
        QStringList candidates;
        for (const QStringList &file : files) {
            if (!file.empty()) candidates << file[0];
        }
        QString path = AndroidFiles::chooseDownload(candidates, primaryRoot());
        if (path == "") {
            why = (error != "" ?
                   tr("MediaStore could not be asked for it: %1").arg(error) :
                   tr("MediaStore has %1 files named \"%2\" of %3 bytes, "
                      "not one").arg(QString::number(candidates.size()),
                                     name, size));
        }
        return path;
    }
    }

    return "";
}

QString
AndroidStorage::displayName(QString uri)
{
    QString error;
    QList<QStringList> rows = query(uri, { "_display_name" }, "", {}, error);
    if (rows.size() == 1 && rows[0].size() == 1) return rows[0][0];
    if (error != "") {
        cerr << "AndroidStorage: no name for " << uri.toStdString() << ": "
             << error.toStdString() << endl;
    }
    return "";
}

AndroidStorage::Document::Document() :
    m_fd(-1)
{
}

AndroidStorage::Document::~Document()
{
    if (!m_descriptor.isValid()) return;
    QString error;
    if (!close(error)) {
        cerr << "AndroidStorage: closing a document: " << error.toStdString()
             << endl;
    }
}

bool
AndroidStorage::Document::open(QString uri, QString mode, QString &error)
{
    if (m_descriptor.isValid()) {
        error = "a document is open already";
        return false;
    }

    QJniObject resolver = contentResolver();
    QJniObject parsed = parseUri(uri);
    if (!resolver.isValid() || !parsed.isValid()) {
        error = "no content resolver";
        return false;
    }

    QJniEnvironment env;
    jclass resolverClass = env->GetObjectClass(resolver.object());
    jmethodID openMethod = env->GetMethodID
        (resolverClass, "openFileDescriptor",
         "(Landroid/net/Uri;Ljava/lang/String;)"
         "Landroid/os/ParcelFileDescriptor;");
    env->DeleteLocalRef(resolverClass);
    if (!openMethod) {
        error = takeException(env);
        return false;
    }

    QJniObject modeString = QJniObject::fromString(mode);
    jobject descriptor = env->CallObjectMethod
        (resolver.object(), openMethod, parsed.object(), modeString.object());
    QString thrown = takeException(env);
    if (thrown != "") {
        error = thrown;
        return false;
    }
    if (!descriptor) {
        error = "the provider gave nothing to open";
        return false;
    }

    // The descriptor stays the ParcelFileDescriptor's, and is closed
    // through it
    m_descriptor = QJniObject::fromLocalRef(descriptor);
    m_fd = m_descriptor.callMethod<jint>("getFd", "()I");
    if (m_fd < 0) {
        QString ignored;
        close(ignored);
        error = "the provider gave no file descriptor";
        return false;
    }
    return true;
}

qint64
AndroidStorage::Document::fileSize() const
{
    if (!m_descriptor.isValid()) return -1;
    return qint64(m_descriptor.callMethod<jlong>("getStatSize", "()J"));
}

bool
AndroidStorage::Document::close(QString &error)
{
    if (!m_descriptor.isValid()) return true;

    QJniEnvironment env;
    QString problem;
    jclass descriptorClass = env->GetObjectClass(m_descriptor.object());
    // Each looked up with no exception pending, as JNI requires
    auto method = [&](const char *name, const char *signature) {
        jmethodID id = env->GetMethodID(descriptorClass, name, signature);
        QString thrown = takeException(env);
        if (problem == "") problem = thrown;
        return id;
    };
    jmethodID canDetect = method("canDetectErrors", "()Z");
    jmethodID checkError = method("checkError", "()V");
    jmethodID closeMethod = method("close", "()V");
    env->DeleteLocalRef(descriptorClass);

    // A provider that hands over a pipe says through it whether it went
    // wrong at its end, which an end of file alone does not: asked
    // before the close, which would not say
    if (problem == "" && canDetect && checkError &&
        env->CallBooleanMethod(m_descriptor.object(), canDetect)) {
        env->CallVoidMethod(m_descriptor.object(), checkError);
        problem = takeException(env);
    }

    if (closeMethod) {
        env->CallVoidMethod(m_descriptor.object(), closeMethod);
        QString thrown = takeException(env);
        if (problem == "") problem = thrown;
    }

    m_descriptor = QJniObject();
    m_fd = -1;

    if (problem != "") {
        error = problem;
        return false;
    }
    return true;
}

QString
AndroidStorage::sizeOf(QString uri, QString &error)
{
    QList<QStringList> rows = query(uri, { "_size" }, "", {}, error);
    if (rows.size() == 1 && rows[0].size() == 1) return rows[0][0];
    if (error == "") {
        error = QString("the provider answered %1 rows").arg(rows.size());
    }
    return "";
}

bool
AndroidStorage::removeIfEmpty(QString uri)
{
    QString error;
    QList<QStringList> rows = query(uri, { "_size" }, "", {}, error);
    if (rows.size() != 1 || rows[0].size() != 1 || rows[0][0] != "0") {
        return false;
    }

    QJniObject resolver = contentResolver();
    QJniObject parsed = parseUri(uri);
    if (!resolver.isValid() || !parsed.isValid()) return false;

    QJniEnvironment env;
    jclass contract = env->FindClass("android/provider/DocumentsContract");
    jmethodID remove = (contract ? env->GetStaticMethodID
                        (contract, "deleteDocument",
                         "(Landroid/content/ContentResolver;"
                         "Landroid/net/Uri;)Z") : nullptr);
    bool removed = false;
    if (remove) {
        removed = env->CallStaticBooleanMethod
            (contract, remove, resolver.object(), parsed.object());
    }
    QString thrown = takeException(env);
    if (contract) env->DeleteLocalRef(contract);

    if (thrown != "") {
        cerr << "AndroidStorage: could not remove the empty "
             << uri.toStdString() << ": " << thrown.toStdString() << endl;
        return false;
    }
    if (removed) {
        cerr << "AndroidStorage: removed the empty " << uri.toStdString()
             << endl;
    }
    return removed;
}

QString
AndroidStorage::logPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/log/tony.log";
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
