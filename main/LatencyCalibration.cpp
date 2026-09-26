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

#include "LatencyCalibration.h"

#include <QSettings>

#include <algorithm>
#include <cmath>

using namespace sv;

namespace LatencyCalibration {
namespace {

const char *const settingsGroup = "LatencyCalibration";

// A device name as part of a group name.  QSettings takes "/" and "\"
// as the start of a subgroup, so they are percent-encoded, and so is
// "%" itself, and the "|" that separates the names.  Nothing else is:
// every settings format keeps any other character, non-ASCII ones
// included, and the Windows registry allows a key name 255 characters
// at most, which a name encoded whole could run past
QString encoded(QString name)
{
    name.replace("%", "%25");
    name.replace("/", "%2F");
    name.replace("\\", "%5C");
    name.replace("|", "%7C");
    return name;
}

// The other way
QString decoded(QString name)
{
    name.replace("%7C", "|");
    name.replace("%5C", "\\");
    name.replace("%2F", "/");
    name.replace("%25", "%");
    return name;
}

QString devicesGroup(const Key &key)
{
    return encoded(key.implementation) + "|" +
        encoded(key.playbackDevice) + "|" +
        encoded(key.recordDevice);
}

QString rateGroup(const Key &key)
{
    return QString::number(qint64(std::llround(key.rate)));
}

// As text, so that every settings format keeps all of the figure
QString number(double value)
{
    return QString::number(value, 'g', 17);
}

double numberFrom(const QVariant &value, bool *ok = nullptr)
{
    return value.toString().toDouble(ok);
}

}

Key
currentKey(QSettings &settings, sv_samplerate_t recordingRate)
{
    Key key;
    settings.beginGroup("Preferences");
    key.implementation = settings.value("audio-target", "").toString();
    QString suffix;
    if (key.implementation != "") suffix = "-" + key.implementation;
    key.playbackDevice =
        settings.value("audio-playback-device" + suffix, "").toString();
    key.recordDevice =
        settings.value("audio-record-device" + suffix, "").toString();
    settings.endGroup();
    key.rate = recordingRate;
    return key;
}

Key
routeKey(const AudioRoute::Route &route, sv_samplerate_t rate)
{
    Key key;
    key.implementation = route.driver;
    key.playbackDevice = AudioRoute::deviceName(route.output);
    if (route.hasInput) {
        key.recordDevice = AudioRoute::deviceName(route.input);
    }
    key.rate = rate;
    return key;
}

bool
onlyRecordDevice(QSettings &settings, const Key &key, QString &recordDevice)
{
    // The groups of the driver and playback device, whatever the record
    // device: their names as devicesGroup() makes them, up to the record
    // device's, which is encoded and so holds no "|"
    const QString prefix = encoded(key.implementation) + "|" +
        encoded(key.playbackDevice) + "|";
    QStringList found;
    settings.beginGroup(settingsGroup);
    for (const QString &group : settings.childGroups()) {
        if (!group.startsWith(prefix)) continue;
        settings.beginGroup(group);
        const bool atRate = settings.childGroups().contains(rateGroup(key));
        settings.endGroup();
        if (atRate) found.push_back(decoded(group.mid(prefix.size())));
    }
    settings.endGroup();
    if (found.size() != 1) return false;
    recordDevice = found.front();
    return true;
}

void
store(QSettings &settings, const Key &key, const Figure &figure)
{
    settings.beginGroup(settingsGroup);
    settings.beginGroup(devicesGroup(key));
    settings.beginGroup(rateGroup(key));
    settings.setValue("roundTrip", number(figure.roundTrip));
    settings.setValue("spread", number(figure.spread));
    settings.setValue("date",
                      figure.date.toUTC().toString(Qt::ISODateWithMs));
    settings.setValue("reportedOutput", number(figure.reportedOutput));
    settings.setValue("reportedInput", number(figure.reportedInput));
    // Only for a device that describes its streams, so that the desktop's
    // figures are kept as they always were
    if (figure.outputStreams != "" || figure.inputStreams != "") {
        settings.setValue("outputStreams", figure.outputStreams);
        settings.setValue("inputStreams", figure.inputStreams);
    } else {
        settings.remove("outputStreams");
        settings.remove("inputStreams");
    }
    settings.endGroup();
    settings.endGroup();
    settings.endGroup();
}

bool
load(QSettings &settings, const Key &key, Figure &figure)
{
    Figure f;
    bool ok = false;
    settings.beginGroup(settingsGroup);
    settings.beginGroup(devicesGroup(key));
    settings.beginGroup(rateGroup(key));
    f.roundTrip = numberFrom(settings.value("roundTrip"), &ok);
    f.spread = numberFrom(settings.value("spread"));
    f.date = QDateTime::fromString(settings.value("date").toString(),
                                   Qt::ISODateWithMs);
    f.reportedOutput = numberFrom(settings.value("reportedOutput"));
    f.reportedInput = numberFrom(settings.value("reportedInput"));
    f.outputStreams = settings.value("outputStreams").toString();
    f.inputStreams = settings.value("inputStreams").toString();
    settings.endGroup();
    settings.endGroup();
    settings.endGroup();

    if (!ok || !std::isfinite(f.roundTrip) || f.roundTrip < 0.0) {
        return false;
    }
    figure = f;
    return true;
}

void
forget(QSettings &settings, const Key &key)
{
    settings.beginGroup(settingsGroup);
    settings.beginGroup(devicesGroup(key));
    settings.remove(rateGroup(key));
    bool empty = settings.childGroups().isEmpty() &&
        settings.childKeys().isEmpty();
    settings.endGroup();
    // Figures for the devices at no other rate: nothing left of them
    if (empty) settings.remove(devicesGroup(key));
    settings.endGroup();
}

bool
isStale(const Figure &figure, double reportedOutput, double reportedInput,
        const QString &outputStreams, const QString &inputStreams)
{
    // Oboe's latencies come from timestamps and move by several ms from
    // one start of the same streams to the next (5.2 then 8.4 then 4.4 ms
    // out on the phone first tried), which the 1 ms below would take for
    // new buffers at every take.  What the round trip depends on there is
    // how the streams were opened: MMAP or not, exclusive or shared,
    // their burst and buffer
    if (outputStreams != "" || inputStreams != "") {
        return (outputStreams != "" && outputStreams != figure.outputStreams) ||
            (inputStreams != "" && inputStreams != figure.inputStreams);
    }
    return std::fabs(figure.reportedOutput - reportedOutput) >
        kStaleToleranceSeconds ||
        std::fabs(figure.reportedInput - reportedInput) >
        kStaleToleranceSeconds;
}

const char *
sourceName(Source source)
{
    switch (source) {
    case Source::Reported: return "reported";
    case Source::Measured: return "measured";
    }
    return "";
}

InUse
roundTripInUse(const Figure *stored,
               double reportedOutput, double reportedInput,
               const QString &outputStreams, const QString &inputStreams)
{
    InUse inUse;
    inUse.reportedOutput = reportedOutput;
    inUse.reportedInput = reportedInput;
    if (stored && !isStale(*stored, reportedOutput, reportedInput,
                           outputStreams, inputStreams)) {
        inUse.source = Source::Measured;
        inUse.roundTrip = stored->roundTrip;
        inUse.date = stored->date;
        return inUse;
    }
    inUse.source = Source::Reported;
    inUse.roundTrip = std::max(reportedOutput, 0.0) +
        std::max(reportedInput, 0.0);
    inUse.stale = (stored != nullptr);
    return inUse;
}

double
reportedSeconds(sv_frame_t frames, sv_samplerate_t rate)
{
    if (frames <= 0 || !(rate > 0)) return 0.0;
    return double(frames) / rate;
}

sv_frame_t
toFrames(double seconds, sv_samplerate_t rate)
{
    if (!(seconds > 0.0) || !(rate > 0)) return 0;
    return sv_frame_t(std::llround(seconds * rate));
}

}
