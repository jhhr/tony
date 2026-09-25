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

#include "SingingTakes.h"

#include "TakeAudio.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>

using namespace sv;

SingingTakes::SingingTakes(QObject *parent) :
    QObject(parent),
    m_active(-1),
    m_named(0)
{
}

SingingTakes::~SingingTakes()
{
}

void
SingingTakes::clear()
{
    m_takes.clear();
    m_active = -1;
    m_named = 0;
    m_reserved.clear();
    m_superseded.clear();
    m_written.clear();
    m_protected.clear();
}

const SingingTakes::Take *
SingingTakes::activeTake() const
{
    if (m_active < 0 || m_active >= int(m_takes.size())) return nullptr;
    return &m_takes[m_active];
}

SingingTakes::Take &
SingingTakes::takeForRecording()
{
    if (m_active < 0 || m_active >= int(m_takes.size())) {
        // The first recording of a session records into a take of its own
        // (spec 5.3), and so does one made after every take was deleted
        addTake();
    }
    return m_takes[m_active];
}

bool
SingingTakes::haveTake() const
{
    const Take *take = activeTake();
    return take && take->audioPath != "";
}

QString
SingingTakes::getAudioPath() const
{
    const Take *take = activeTake();
    return take ? take->audioPath : QString();
}

const Coverage &
SingingTakes::getCoverage() const
{
    static const Coverage empty;
    const Take *take = activeTake();
    return take ? take->coverage : empty;
}

QString
SingingTakes::getActiveName() const
{
    const Take *take = activeTake();
    return take ? take->name : QString();
}

QStringList
SingingTakes::getTakeNames() const
{
    QStringList names;
    for (const Take &take : m_takes) names.push_back(take.name);
    return names;
}

int
SingingTakes::indexOf(QString name) const
{
    for (int i = 0; i < int(m_takes.size()); ++i) {
        if (m_takes[i].name == name) return i;
    }
    return -1;
}

const SingingTakes::Take *
SingingTakes::getTake(int index) const
{
    if (index < 0 || index >= int(m_takes.size())) return nullptr;
    return &m_takes[index];
}

bool
SingingTakes::setActiveIndex(int index)
{
    if (index < 0 || index >= int(m_takes.size())) return false;
    m_active = index;
    return true;
}

QString
SingingTakes::addTake(QString name)
{
    Take take;

    if (name != "" && indexOf(name) < 0) {
        take.name = name;
    } else {
        // "Take N" with an N this session has not used, however many takes
        // have been deleted since: the name is the take's identity, and
        // the layers of a deleted take may still be in the document.  Not
        // translated: the names of the take's layers are built from it and
        // stored in the session file
        do {
            take.name = QString("Take %1").arg(++m_named);
        } while (indexOf(take.name) >= 0 || m_reserved.contains(take.name));
    }

    m_takes.push_back(take);
    m_active = int(m_takes.size()) - 1;
    return take.name;
}

QString
SingingTakes::duplicateActiveTake(QString name)
{
    const Take *from = activeTake();
    if (!from) return "";

    // Copied before addTake(), which may make the vector move
    QString path = from->audioPath;
    Coverage coverage = from->coverage;

    QString made = addTake(name);
    m_takes[m_active].audioPath = path;
    m_takes[m_active].coverage = coverage;

    // The audio file is not copied and not superseded: the two takes read
    // the same one until one of them is recorded into or erased from,
    // which writes a new file anyway (spec 5.4)
    return made;
}

bool
SingingTakes::renameTake(int index, QString name)
{
    if (index < 0 || index >= int(m_takes.size())) return false;

    name = name.trimmed();
    if (name == "") return false;

    int existing = indexOf(name);
    if (existing >= 0 && existing != index) return false;

    m_takes[index].name = name;
    return true;
}

void
SingingTakes::reserveTakeName(QString name)
{
    if (name == "") return;

    if (!m_reserved.contains(name)) m_reserved.push_back(name);

    // A session that had a "Take 7" in it goes on at "Take 8", however
    // many of the takes before it have been deleted since: the default
    // names carry on where the session left off rather than starting again
    // at the first number no take happens to be using
    static const QRegularExpression pattern("^Take (\\d+)$");
    QRegularExpressionMatch match = pattern.match(name);
    if (match.hasMatch()) {
        int number = match.captured(1).toInt();
        if (number > m_named) m_named = number;
    }
}

bool
SingingTakes::removeTake(int index)
{
    if (index < 0 || index >= int(m_takes.size())) return false;

    m_takes.erase(m_takes.begin() + index);

    if (m_takes.empty()) {
        m_active = -1;
    } else if (index < m_active) {
        --m_active;
    } else if (index == m_active) {
        // A neighbour takes over: the one before, or the first if this
        // was it
        m_active = (index > 0 ? index - 1 : 0);
    }

    return true;
}

void
SingingTakes::setTake(QString path, const Coverage &coverage)
{
    Take &take = takeForRecording();
    if (take.audioPath != "" && take.audioPath != path) {
        m_superseded.push_back(take.audioPath);
    }
    take.audioPath = path;
    take.coverage = coverage;
}

void
SingingTakes::restoreTake(QString path, const Coverage &coverage)
{
    Take &take = takeForRecording();
    take.audioPath = path;
    take.coverage = coverage;
}

void
SingingTakes::relocateTake(int index, QString path)
{
    if (index < 0 || index >= int(m_takes.size())) return;
    if (path == "" || m_takes[index].audioPath == path) return;

    m_takes[index].audioPath = path;
    if (!m_written.contains(path)) m_written.push_back(path);
}

void
SingingTakes::protectPath(QString path)
{
    if (path != "" && !m_protected.contains(path)) m_protected.push_back(path);
}

QStringList
SingingTakes::unusedWrittenFiles() const
{
    QStringList inUse;
    for (const Take &take : m_takes) {
        if (take.audioPath != "") inUse.push_back(take.audioPath);
    }

    QStringList unused;
    for (const QString &path : m_written) {
        if (inUse.contains(path)) continue;
        if (m_protected.contains(path)) continue;
        if (unused.contains(path)) continue;
        unused.push_back(path);
    }
    return unused;
}

QStringList
SingingTakes::removeUnusedFiles()
{
    QStringList gone;
    for (const QString &path : unusedWrittenFiles()) {
        if (QFile::remove(path)) {
            gone.push_back(path);
            m_written.removeAll(path);
        } else if (!QFile::exists(path)) {
            // Something else has taken it away; it is not ours any more
            m_written.removeAll(path);
        }
    }
    return gone;
}

void
SingingTakes::setWholeFileTake(QString path, sv_frame_t frames)
{
    Coverage whole;
    whole.add(0, frames);
    setTake(path, whole);
}

QString
SingingTakes::spliceRecording(QString recordingPath,
                              sv_frame_t recordingOffset,
                              sv_frame_t position,
                              sv_frame_t length,
                              QString directory,
                              Coverage::Range *placed,
                              sv_samplerate_t rate)
{
    QString outPath = nextAudioPath(directory);
    if (outPath == "") {
        return tr("Could not find a name to write the singing track under, "
                  "in \"%1\"").arg(directory);
    }

    // A recording at another rate than the take's is converted first,
    // into a folder of its own beside the take's files that goes again,
    // with what is in it, as soon as the splice has read it
    QString source = recordingPath;
    std::unique_ptr<QTemporaryDir> scratch;
    if (rate > 0 && TakeAudio::sampleRate(recordingPath) != rate) {
        scratch.reset(new QTemporaryDir
                      (QDir(directory).filePath("resampling-XXXXXX")));
        if (!scratch->isValid()) {
            return tr("Could not make a folder to convert the recording in, "
                      "in \"%1\"").arg(directory);
        }
        source = scratch->filePath("recording.wav");
        QString error = TakeAudio::resample(recordingPath, rate, source);
        if (error != "") return error;
    }

    Take &take = takeForRecording();

    Coverage::Range range;
    QString error = TakeAudio::splice(take.audioPath, source,
                                      recordingOffset, position, length,
                                      outPath, &range);
    if (error != "") return error;

    if (take.audioPath != "") m_superseded.push_back(take.audioPath);
    take.audioPath = outPath;
    m_written.push_back(outPath);
    take.coverage.add(range.start, range.end);

    if (placed) *placed = range;
    return "";
}

QString
SingingTakes::eraseRanges(const Coverage::Ranges &ranges, QString directory,
                          Coverage::Ranges *erased)
{
    if (erased) erased->clear();

    // Only what holds singing can be erased.  Silence that was never
    // recorded is not ours to rewrite, and a selection that runs past
    // the end of the singing must not make the file any longer
    Coverage wanted;
    for (const Coverage::Range &r : ranges) {
        for (const Coverage::Range &covered : getCoverage().getRanges()) {
            wanted.add(std::max(r.start, covered.start),
                       std::min(r.end, covered.end));
        }
    }
    // Nothing in the selection holds recorded singing, and there may be no
    // take at all
    if (wanted.isEmpty()) return "";

    QString outPath = nextAudioPath(directory);
    if (outPath == "") {
        return tr("Could not find a name to write the singing track under, "
                  "in \"%1\"").arg(directory);
    }

    Take &take = takeForRecording();

    QString error = TakeAudio::erase(take.audioPath, wanted.getRanges(),
                                     outPath);
    if (error != "") return error;

    m_superseded.push_back(take.audioPath);
    take.audioPath = outPath;
    m_written.push_back(outPath);
    for (const Coverage::Range &r : wanted.getRanges()) {
        take.coverage.remove(r.start, r.end);
    }

    if (erased) *erased = wanted.getRanges();
    return "";
}

bool
SingingTakes::coversPosition(sv_frame_t position) const
{
    return getCoverage().contains(position);
}

bool
SingingTakes::shouldConfirmRecordingAt(sv_frame_t position) const
{
    return coversPosition(position) && isOverwriteConfirmationWanted();
}

bool
SingingTakes::isOverwriteConfirmationWanted()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    bool wanted = settings.value("confirmrecordover", true).toBool();
    settings.endGroup();
    return wanted;
}

void
SingingTakes::setOverwriteConfirmationWanted(bool wanted)
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("confirmrecordover", wanted);
    settings.endGroup();
}

QString
SingingTakes::nextAudioPath(QString directory)
{
    QDir dir(directory);

    // The time of day is enough to tell one from the next within a
    // session; the counter is there because it need only be enough.
    // ":" is not allowed in a file name on Windows
    QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");

    for (int i = 0; i < 1000; ++i) {
        QString name = (i == 0 ?
                        QString("take-%1.wav").arg(stamp) :
                        QString("take-%1-%2.wav").arg(stamp).arg(i));
        if (!dir.exists(name)) return dir.filePath(name);
    }

    return "";
}
