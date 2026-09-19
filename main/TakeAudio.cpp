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

#include "TakeAudio.h"

#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"
#include "data/fileio/WavFileWriter.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <memory>
#include <vector>

using namespace sv;

namespace {

const sv_frame_t blockFrames = 16384;

QString tr(const char *text)
{
    return QCoreApplication::translate("TakeAudio", text);
}

// A stretch [start, end) of the take to be filled from a file, or
// with silence if there is no source
struct Patch {
    sv_frame_t start;
    sv_frame_t end;
    WavFileReader *source;
    sv_frame_t sourceOffset; // source frame that lands on "start"
};

std::unique_ptr<WavFileReader> openWav(QString path, QString &error)
{
    if (!QFileInfo(path).isFile()) {
        error = tr("Audio file \"%1\" does not exist").arg(path);
        return {};
    }
    std::unique_ptr<WavFileReader> reader(new WavFileReader(FileSource(path)));
    if (!reader->isOK()) {
        error = tr("Failed to read audio file \"%1\": %2")
            .arg(path).arg(reader->getError());
        return {};
    }
    return reader;
}

// count frames from "start" on, interleaved in "channels" channels,
// silent where the file has nothing (start may be past its end)
floatvec_t readFrames(WavFileReader *reader, sv_frame_t start,
                      sv_frame_t count, int channels)
{
    floatvec_t result(count * channels, 0.f);
    if (!reader || start >= reader->getFrameCount()) return result;

    int have = reader->getChannelCount();
    floatvec_t data = reader->getInterleavedFrames(start, count);
    sv_frame_t got = sv_frame_t(data.size()) / have;

    for (sv_frame_t i = 0; i < got; ++i) {
        for (int c = 0; c < channels; ++c) {
            if (have == channels) {
                result[i * channels + c] = data[i * have + c];
            } else if (channels == 1) {
                float sum = 0.f;
                for (int k = 0; k < have; ++k) sum += data[i * have + k];
                result[i] = sum / float(have);
            } else {
                result[i * channels + c] = data[i * have + (c % have)];
            }
        }
    }

    return result;
}

// How much of the patch, rather than of what it replaces, is heard at
// a frame: 1 in the middle, less within "fade" frames of either end
float weightAt(sv_frame_t frame, const Patch &patch, sv_frame_t fade)
{
    sv_frame_t fromEdge = std::min(frame - patch.start + 1, patch.end - frame);
    if (fromEdge > fade) return 1.f;
    return float(fromEdge) / float(fade + 1);
}

// Patches must be in order and must not overlap
QString write(WavFileReader *old, sv_samplerate_t rate, int channels,
              sv_frame_t total, const std::vector<Patch> &patches,
              sv_frame_t fade, QString outPath)
{
    if (fade < 0) fade = TakeAudio::defaultFadeFrames(rate);

    QString error;

    {
        // To a temporary file that is moved into place on close
        WavFileWriter writer(outPath, rate, channels,
                             WavFileWriter::WriteToTemporary);
        if (!writer.isOK()) {
            error = writer.getError();
        }

        for (sv_frame_t b = 0; b < total && error == ""; b += blockFrames) {

            sv_frame_t count = std::min(blockFrames, total - b);
            floatvec_t block = readFrames(old, b, count, channels);

            for (const Patch &patch : patches) {

                sv_frame_t from = std::max(b, patch.start);
                sv_frame_t to = std::min(b + count, patch.end);
                if (from >= to) continue;

                // Never more than half the patch, or the ends would meet
                sv_frame_t patchFade = std::min(fade, (patch.end - patch.start) / 2);

                floatvec_t replacement = readFrames
                    (patch.source, patch.sourceOffset + (from - patch.start),
                     to - from, channels);

                for (sv_frame_t i = from; i < to; ++i) {
                    float w = weightAt(i, patch, patchFade);
                    for (int c = 0; c < channels; ++c) {
                        float &sample = block[(i - b) * channels + c];
                        sample = sample * (1.f - w) +
                            replacement[(i - from) * channels + c] * w;
                    }
                }
            }

            if (!writer.putInterleavedFrames(block)) {
                error = writer.getError();
                if (error == "") error = tr("Failed to write audio data");
            }
        }

        if (error == "" && !writer.close()) {
            error = writer.getError();
            if (error == "") error = tr("Failed to finish writing audio file");
        }
    }

    // The writer puts its file in place even when it is abandoned
    if (error != "") {
        QFile::remove(outPath);
        return tr("Failed to write \"%1\": %2").arg(outPath).arg(error);
    }

    return "";
}

QString checkOutPath(QString outPath, QString oldPath)
{
    if (outPath == "") return tr("No file name given to write to");
    if (QFileInfo(outPath) == QFileInfo(oldPath) ||
        QFileInfo::exists(outPath)) {
        return tr("File \"%1\" exists already, and is not to be overwritten")
            .arg(outPath);
    }
    return "";
}

} // namespace

sv_frame_t
TakeAudio::defaultFadeFrames(sv_samplerate_t rate)
{
    // 5 ms: too short to hear as a fade, long enough not to click
    return sv_frame_t(rate * 0.005);
}

QString
TakeAudio::splice(QString oldPath, QString recordingPath,
                  sv_frame_t recordingOffset, sv_frame_t position,
                  sv_frame_t length, QString outPath,
                  Coverage::Range *placed, sv_frame_t fadeFrames)
{
    QString error = checkOutPath(outPath, oldPath);
    if (error != "") return error;

    auto recording = openWav(recordingPath, error);
    if (!recording) return error;

    std::unique_ptr<WavFileReader> old;
    if (oldPath != "") {
        old = openWav(oldPath, error);
        if (!old) return error;
        if (old->getSampleRate() != recording->getSampleRate()) {
            return tr("The recording's sample rate (%1) is not that of the take so far (%2)")
                .arg(recording->getSampleRate()).arg(old->getSampleRate());
        }
    }

    if (recordingOffset < 0) recordingOffset = 0;

    sv_frame_t available = recording->getFrameCount() - recordingOffset;
    if (length < 0 || length > available) length = available;

    if (position < 0) {
        recordingOffset -= position;
        length += position;
        position = 0;
    }

    if (length <= 0) {
        return tr("The recording is too short to use");
    }

    Patch patch { position, position + length,
                  recording.get(), recordingOffset };

    sv_frame_t total = patch.end;
    if (old) total = std::max(total, old->getFrameCount());

    int channels = old ? old->getChannelCount() : recording->getChannelCount();

    error = write(old.get(), recording->getSampleRate(), channels, total,
                  { patch }, fadeFrames, outPath);

    if (error == "" && placed) {
        *placed = Coverage::Range(patch.start, patch.end);
    }

    return error;
}

QString
TakeAudio::erase(QString oldPath, const Coverage::Ranges &ranges,
                 QString outPath, sv_frame_t fadeFrames)
{
    QString error = checkOutPath(outPath, oldPath);
    if (error != "") return error;

    auto old = openWav(oldPath, error);
    if (!old) return error;

    sv_frame_t total = old->getFrameCount();

    // In order, apart, and within the file
    Coverage tidy;
    for (const Coverage::Range &r : ranges) {
        tidy.add(std::max(r.start, sv_frame_t(0)), std::min(r.end, total));
    }

    std::vector<Patch> patches;
    for (const Coverage::Range &r : tidy.getRanges()) {
        patches.push_back({ r.start, r.end, nullptr, 0 });
    }

    return write(old.get(), old->getSampleRate(), old->getChannelCount(),
                 total, patches, fadeFrames, outPath);
}
