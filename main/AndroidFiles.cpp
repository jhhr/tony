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
#include <QSaveFile>

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
                problems << QString("Cannot link or copy %1 to %2: %3")
                    .arg(library).arg(link).arg(file.errorString());
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
            .arg(path)
            .arg(QString::fromLocal8Bit(error ? error : "no reason given"));
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
            .arg(target).arg(out.errorString());
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
                .arg(target).arg(out.errorString());
            out.cancelWriting();
            return "";
        }
        total += n;
    }

    if (!out.commit()) {
        error = QString("writing %1 failed: %2")
            .arg(target).arg(out.errorString());
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
