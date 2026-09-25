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

#ifndef TONY_TAKE_AUDIO_H
#define TONY_TAKE_AUDIO_H

#include "Coverage.h"

#include "base/BaseTypes.h"

#include <QString>

/**
 * The audio of a singing take is one WAV file that starts at frame 0
 * of the reference's timeline, silent wherever nothing was recorded.
 * These functions make the next such file from the last: nothing is
 * ever changed in place, so the file before is there to go back to.
 *
 * All of them work through the files a block at a time, and all
 * return an empty string on success and a message for the user
 * otherwise. If they fail, there is no file at outPath.
 */
namespace TakeAudio
{
    /**
     * The length of the fades that join new material to old, for
     * audio at the given rate.
     */
    sv::sv_frame_t defaultFadeFrames(sv::sv_samplerate_t rate);

    /**
     * Write to outPath the take in oldPath with a recording put into
     * it at the frame "position".
     *
     * oldPath may be empty: there is no take yet, and the result is
     * silence up to the position. The recording is used from its
     * frame recordingOffset on (the latency: what was recorded before
     * the singer could have heard frame "position" of the reference),
     * for "length" frames, or to its end if length is negative. Frames
     * that would land before frame 0 of the take are left out.
     *
     * The result is as long as it needs to be to hold both. It has
     * the channels of the old take if there was one (a recording with
     * a different number is mixed down, or copied across, to suit) and
     * of the recording otherwise. The ends of the new material are
     * crossfaded with what was there over fadeFrames frames; negative
     * means defaultFadeFrames().
     *
     * If placed is not null, it receives the range of the take that
     * the recording now fills.
     */
    QString splice(QString oldPath,
                   QString recordingPath,
                   sv::sv_frame_t recordingOffset,
                   sv::sv_frame_t position,
                   sv::sv_frame_t length,
                   QString outPath,
                   Coverage::Range *placed = nullptr,
                   sv::sv_frame_t fadeFrames = -1);

    /**
     * Write to outPath the take in oldPath with the given ranges made
     * silent, fading out into each and in again after it. The result
     * is as long as the original.
     */
    QString erase(QString oldPath,
                  const Coverage::Ranges &ranges,
                  QString outPath,
                  sv::sv_frame_t fadeFrames = -1);

    /**
     * Write to outPath the audio in inPath at another sample rate: as
     * long as it was in seconds, with the same channels, and what was
     * at a time in the one at the same time in the other. For a
     * recording from a device that does not run at the reference's
     * rate, whose frames have to be the reference's before it can go
     * into a take.
     */
    QString resample(QString inPath,
                     sv::sv_samplerate_t rate,
                     QString outPath);

    /// The sample rate of the audio file at path, or 0 if it cannot be read
    sv::sv_samplerate_t sampleRate(QString path);
}

#endif
