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

#include "AndroidFiles.h"

#include "base/Debug.h"
#include "system/System.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

#include <vector>

using namespace sv;

QStringList
AndroidFiles::linkVampPlugins(QString libraryDir,
                              QString linkDir,
                              QStringList names,
                              QStringList &problems)
{
    QStringList links;

    if (!QDir().mkpath(linkDir)) {
        problems << QString("Cannot create the plugin folder %1").arg(linkDir);
        SVCERR << "AndroidFiles: " << problems.back() << endl;
        return links;
    }

    for (QString name : names) {

        // Absolute: a relative link would be read from linkDir
        QString library = QFileInfo(QDir(libraryDir).filePath
                                    ("lib" + name + ".so")).absoluteFilePath();
        QString link = QDir(linkDir).filePath(name + ".so");

        // Whatever is there is from an earlier start, and may point into
        // a library directory that an update has since removed
        QFile::remove(link);

        if (!QFileInfo::exists(library)) {
            problems << QString("No %1 plugin: there is no %2")
                .arg(name).arg(library);
            SVCERR << "AndroidFiles: " << problems.back() << endl;
            continue;
        }

        QFile file(library);
        if (file.link(link)) {
            SVCERR << "AndroidFiles: linked " << link << " to " << library
                   << endl;
        } else {
            QString linkError = file.errorString();
            if (file.copy(link)) {
                SVCERR << "AndroidFiles: could not link " << link << " to "
                       << library << " (" << linkError
                       << "), so copied it" << endl;
            } else {
                // One arg() for all: a path may hold a "%1" that a second
                // arg() would fill
                problems << QString("Cannot link or copy %1 to %2: %3")
                    .arg(library, link, file.errorString());
                SVCERR << "AndroidFiles: " << problems.back() << endl;
                continue;
            }
        }

        links << link;
    }

    return links;
}

QString
AndroidFiles::checkVampPlugin(QString path)
{
    QString problem;

    void *handle = DLOPEN(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        const char *error = DLERROR();
        problem = QString("Cannot load %1: %2")
            .arg(path,
                 QString::fromLocal8Bit(error ? error : "no reason given"));
    } else {
        if (!DLSYM(handle, "vampGetPluginDescriptor")) {
            problem = QString("%1 is not a Vamp plugin library").arg(path);
        }
        (void)DLCLOSE(handle);
    }

    if (problem != "") {
        SVCERR << "AndroidFiles: " << problem << endl;
    } else {
        SVCERR << "AndroidFiles: " << path << " loads" << endl;
    }
    return problem;
}

QString
AndroidFiles::copyIn(QString source, QString name, QString dir,
                     QString &error)
{
    if (!QDir().mkpath(dir)) {
        error = QString("cannot create the folder %1").arg(dir);
        return "";
    }

    QString target = QDir(dir).filePath(safeFileName(name));

    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        error = QString("cannot read it: %1").arg(in.errorString());
        return "";
    }

    // QSaveFile writes a file of its own and renames it over the target
    // at the end, so a copy that fails half-way leaves the earlier one
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly)) {
        error = QString("cannot write %1: %2")
            .arg(target, out.errorString());
        return "";
    }

    // Read to the end rather than to the size: a cloud provider may hand
    // over a pipe, which has none
    std::vector<char> buffer(1 << 20);
    qint64 total = 0;
    while (true) {
        qint64 n = in.read(buffer.data(), qint64(buffer.size()));
        if (n < 0) {
            error = QString("reading it failed: %1").arg(in.errorString());
            out.cancelWriting();
            return "";
        }
        if (n == 0) break;
        if (out.write(buffer.data(), n) != n) {
            error = QString("writing %1 failed: %2")
                .arg(target, out.errorString());
            out.cancelWriting();
            return "";
        }
        total += n;
    }

    if (!out.commit()) {
        error = QString("writing %1 failed: %2")
            .arg(target, out.errorString());
        return "";
    }

    SVCERR << "AndroidFiles: copied " << total << " bytes from " << source
           << " to " << target << endl;
    return target;
}

QString
AndroidFiles::safeFileName(QString name)
{
    const QString refused("/\\:*?\"<>|");

    QString safe;
    for (QChar c : name) {
        if (c.unicode() < 32 || refused.contains(c)) {
            safe += QChar('_');
        } else {
            safe += c;
        }
    }

    safe = safe.trimmed();
    if (safe.count(QChar('.')) == safe.size()) {
        safe = "imported";
    }
    return safe;
}

// The path of relative under root, or "" if relative would lead out of
// it: a provider's ids do not, but a URI is only a string
static QString
pathUnder(QString root, QString relative)
{
    if (!root.startsWith('/')) return "";
    for (QString part : relative.split('/')) {
        if (part == "..") return "";
    }
    return QDir::cleanPath(root + "/" + relative);
}

QString
AndroidFiles::pathFromContentUri(QString uri, QString primaryRoot)
{
    const QString scheme("content://");
    if (!uri.startsWith(scheme, Qt::CaseInsensitive)) return "";

    // Nothing the picker gives has a query or a fragment
    QString rest = uri.mid(scheme.size());
    static const QRegularExpression queryOrFragment("[?#]");
    int end = rest.indexOf(queryOrFragment);
    if (end >= 0) rest = rest.left(end);

    // Split before decoding: a '/' inside the id is %2F in every form of
    // the URI, Android's and QUrl's, while spaces and letters such as 'ä'
    // may come either way
    QStringList parts = rest.split('/');
    QString authority = parts.takeFirst();
    for (QString &part : parts) {
        part = QUrl::fromPercentEncoding(part.toUtf8());
    }

    QString id;
    if (parts.size() == 2 && parts[0] == "document") {
        id = parts[1];
    } else if (parts.size() == 4 && parts[0] == "tree" &&
               parts[2] == "document") {
        id = parts[3];
    } else {
        return "";
    }

    if (authority == "com.android.externalstorage.documents") {

        int colon = id.indexOf(':');
        if (colon <= 0) return "";
        QString volume = id.left(colon);
        QString relative = id.mid(colon + 1);

        if (volume == "primary") {
            if (primaryRoot == "") return "";
            return pathUnder(primaryRoot, relative);
        }

        // A card or USB drive is named by its file system's UUID, which is
        // also its folder in /storage
        static const QRegularExpression uuid("^[0-9A-Fa-f]+(-[0-9A-Fa-f]+)*$");
        if (uuid.match(volume).hasMatch()) {
            return pathUnder("/storage/" + volume, relative);
        }
        return "";
    }

    if (authority == "com.android.providers.downloads.documents") {
        // Only these carry a path; the rest are numbers in a database
        const QString raw("raw:/");
        if (!id.startsWith(raw)) return "";
        return pathUnder("/", id.mid(raw.size()));
    }

    return "";
}

QString
AndroidFiles::suggestedSessionName(QString sessionPath, QString audioPath)
{
    QString from = (sessionPath != "" ? sessionPath : audioPath);
    QString base = QFileInfo(from).completeBaseName();
    if (base == "") return "";
    return base + ".ton";
}

QString
AndroidFiles::sessionFileName(QString picked)
{
    QString name = picked.trimmed();

    // Android's storage names a document created with no name "(invalid)"
    // (FileUtils.buildValidFatFilename())
    if (name == "(invalid)") return "";

    int dot = name.lastIndexOf('.');
    if (dot < 0) return (name == "" ? QString() : name + ".ton");
    if (name.left(dot).trimmed().count(QChar('.')) ==
        name.left(dot).trimmed().size()) {
        return ""; // ".ton", ".", "..ton" and the like
    }
    return name;
}

bool
AndroidFiles::removeIfEmpty(QString path)
{
    QFileInfo info(path);
    if (path == "" || !info.exists() || info.isDir() || info.size() != 0) {
        return false;
    }
    if (!QFile::remove(path)) {
        SVCERR << "AndroidFiles: could not remove the empty " << path << endl;
        return false;
    }
    SVCERR << "AndroidFiles: removed the empty " << path << endl;
    return true;
}
