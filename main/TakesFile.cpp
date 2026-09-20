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

#include "TakesFile.h"

#include "SingingTakes.h"

#include "base/XmlExportable.h"
#include "data/fileio/BZipFileDevice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

#include <map>

using namespace sv;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("TakesFile", text);
}

// Windows tells no two file names apart by their case, and a path
// written with back slashes names the same file as one with forward
// slashes.  The session file stores forward slashes throughout
#ifdef Q_OS_WIN
const Qt::CaseSensitivity nameCase = Qt::CaseInsensitive;
#else
const Qt::CaseSensitivity nameCase = Qt::CaseSensitive;
#endif

QString cleaned(QString path)
{
    if (path == "") return path;
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

// The directory part of a cleaned path, without its trailing slash
// ("C:/songs/a.ton" gives "C:", "/songs/a.ton" gives "/songs")
QString directoryOf(QString cleanedPath)
{
    int slash = cleanedPath.lastIndexOf('/');
    if (slash < 0) return "";
    if (slash == 0) return "/";
    return cleanedPath.left(slash);
}

// One file, under one name: for telling whether two takes are sharing
// their audio
QString fileKey(QString path)
{
    QString key = cleaned(path);
    return nameCase == Qt::CaseInsensitive ? key.toLower() : key;
}

}

QString
TakesFile::toXml(const SingingTakes &takes, QString sessionPath, QString indent)
{
    QString xml;

    xml += QString("%1<takes active=\"%2\">\n")
        .arg(indent)
        .arg(XmlExportable::encodeEntities(takes.getActiveName()));

    for (const SingingTakes::Take &take : takes.getTakes()) {
        xml += QString("%1  <take name=\"%2\" audio=\"%3\"/>\n")
            .arg(indent)
            .arg(XmlExportable::encodeEntities(take.name))
            .arg(XmlExportable::encodeEntities
                 (relativeAudioPath(sessionPath, take.audioPath)));
    }

    xml += QString("%1</takes>\n").arg(indent);

    return xml;
}

TakesFile::Takes
TakesFile::read(QString sessionPath)
{
    Takes takes;
    if (sessionPath == "") return takes;

    // A session file is bzip2, but the session reader takes a plain XML
    // one as well, so the first bytes say which this is rather than the
    // extension
    bool compressed = false;
    {
        QFile probe(sessionPath);
        if (!probe.open(QIODevice::ReadOnly)) return takes;
        compressed = (probe.read(3) == QByteArray("BZh"));
    }

    if (compressed) {
        BZipFileDevice file(sessionPath);
        if (!file.open(QIODevice::ReadOnly)) return takes;
        QXmlStreamReader reader(&file);
        takes = readFrom(reader);
        file.close();
    } else {
        QFile file(sessionPath);
        if (!file.open(QIODevice::ReadOnly)) return takes;
        QXmlStreamReader reader(&file);
        takes = readFrom(reader);
    }

    return takes;
}

TakesFile::Takes
TakesFile::readFrom(QXmlStreamReader &reader)
{
    Takes takes;
    bool inTakes = false;

    // Everything of the document but our own element is passed over: it
    // has been read once already, by the session reader
    while (!reader.atEnd()) {

        switch (reader.readNext()) {

        case QXmlStreamReader::StartElement:
        {
            QString name = reader.name().toString().toLower();

            if (name == "takes") {
                inTakes = true;
                takes.found = true;
                takes.active = reader.attributes().value("active").toString();
            } else if (inTakes && name == "take") {
                Take take;
                take.name = reader.attributes().value("name").toString();
                take.audioPath = reader.attributes().value("audio").toString();
                // A take is its name: one without a name is no take
                if (take.name != "") takes.takes.push_back(take);
            }
            break;
        }

        case QXmlStreamReader::EndElement:
            if (reader.name().toString().toLower() == "takes") inTakes = false;
            break;

        default:
            break;
        }
    }

    return takes;
}

QString
TakesFile::takesFolder(QString sessionPath)
{
    if (sessionPath == "") return "";

    QString path = cleaned(sessionPath);
    QString directory = directoryOf(path);
    QString name = path.mid(path.lastIndexOf('/') + 1);

    // The base name of the session file: everything but the extension,
    // so that "My Song.ton" gives "My Song.takes" and a name with dots
    // of its own keeps them
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.left(dot);
    if (name == "") return "";

    name += ".takes";

    if (directory == "") return name;
    if (directory == "/") return "/" + name;
    return directory + "/" + name;
}

QString
TakesFile::relativeAudioPath(QString sessionPath, QString audioPath)
{
    if (audioPath == "") return "";

    QString audio = cleaned(audioPath);
    if (sessionPath == "") return audio;

    QString directory = directoryOf(cleaned(sessionPath));
    if (directory == "") return audio;

    // Anything outside the session's own directory is named where it is:
    // a relative path there would be a trail of "../.." that says nothing
    if (!isInFolder(directory, audio)) return audio;

    int from = (directory == "/" ? 1 : directory.length() + 1);
    return audio.mid(from);
}

QString
TakesFile::resolveAudioPath(QString sessionPath, QString storedPath)
{
    if (storedPath == "") return "";

    QString stored = cleaned(storedPath);

    // A session saved before the takes folder names its audio absolutely
    // (phase 7b), and that is where the file is
    if (QFileInfo(stored).isAbsolute()) return stored;

    QString directory = directoryOf(cleaned(sessionPath));
    if (directory == "") return stored;
    if (directory == "/") return cleaned("/" + stored);
    return cleaned(directory + "/" + stored);
}

bool
TakesFile::isInFolder(QString folder, QString path)
{
    if (folder == "" || path == "") return false;

    QString within = cleaned(folder);
    if (!within.endsWith('/')) within += '/';

    QString file = cleaned(path);
    return file.length() > within.length() &&
        file.startsWith(within, nameCase);
}

QString
TakesFile::freeCopyPath(QString folder, QString fileName)
{
    if (folder == "" || fileName == "") return "";

    QDir dir(folder);
    if (!dir.exists(fileName)) return cleaned(dir.filePath(fileName));

    // A file of that name is there already.  Whatever it is, it is not
    // ours to write over: the copy gets a name of its own
    QString base = fileName, suffix;
    int dot = fileName.lastIndexOf('.');
    if (dot > 0) {
        base = fileName.left(dot);
        suffix = fileName.mid(dot);
    }

    for (int i = 2; i < 1000; ++i) {
        QString name = QString("%1-%2%3").arg(base).arg(i).arg(suffix);
        if (!dir.exists(name)) return cleaned(dir.filePath(name));
    }

    return "";
}

QString
TakesFile::copyTakeAudioInto(SingingTakes &takes, QString folder)
{
    if (folder == "") return tr("No folder to copy the takes' audio into");

    QString error;

    // The copies made here, to be undone if one of them fails, and which
    // source file each of them came from: two takes sharing a file share
    // the copy as well
    QStringList made;
    std::map<QString, QString> copyOf;
    std::vector<std::pair<int, QString>> relocations;

    for (int i = 0; i < takes.getTakeCount(); ++i) {

        const SingingTakes::Take *take = takes.getTake(i);
        if (!take || take->audioPath == "") continue;
        if (isInFolder(folder, take->audioPath)) continue;

        QString key = fileKey(take->audioPath);
        auto known = copyOf.find(key);
        if (known != copyOf.end()) {
            relocations.push_back({ i, known->second });
            continue;
        }

        QString target =
            freeCopyPath(folder, QFileInfo(take->audioPath).fileName());
        if (target == "") {
            error = tr("Could not find a name to copy the audio of the take "
                       "\"%1\" under, in \"%2\"").arg(take->name).arg(folder);
            break;
        }

        if (!QFile::copy(take->audioPath, target)) {
            error = tr("Could not copy the audio of the take \"%1\" to "
                       "\"%2\"").arg(take->name).arg(target);
            break;
        }

        made.push_back(target);
        copyOf[key] = target;
        relocations.push_back({ i, target });
    }

    if (error != "") {
        // Nothing has been changed and nothing is left behind: a session
        // that cannot have all of its audio beside it is not saved at all
        for (const QString &path : made) QFile::remove(path);
        return error;
    }

    for (const auto &relocation : relocations) {
        takes.relocateTake(relocation.first, relocation.second);
    }

    return "";
}
