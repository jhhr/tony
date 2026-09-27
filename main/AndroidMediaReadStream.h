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

#ifndef TONY_ANDROID_MEDIA_READ_STREAM_H
#define TONY_ANDROID_MEDIA_READ_STREAM_H

#include <bqaudiostream/AudioReadStream.h>

#include <memory>
#include <string>
#include <vector>

/**
 * Audio files read through Android's own extractors and decoders (the
 * NDK's AMediaExtractor and AMediaCodec, in libmediandk): what the
 * Android build cannot read otherwise. Its libsndfile has neither FLAC
 * nor Ogg, and there is no Media Foundation, which reads M4A and AAC on
 * Windows. Built for Android only.
 *
 * A bqaudiostream reader, registered with bqaudiostream's factory for
 * its extensions (getExtensions()) as bqaudiostream's own readers are,
 * so that svcore's BQAFileReader uses it. WAV, MP3 and Opus are not
 * among them: libsndfile, libmad and opusfile read those as before.
 * The registration is a static object in this file that nothing else
 * refers to, kept in the application because it links tony_core whole.
 *
 * The first audio track is decoded from start to end: there is no
 * seeking, as in bqaudiostream's Opus and Media Foundation readers.
 * The channel count and rate are those of the decoder's output, which
 * may differ from the container's (HE-AAC doubles the rate), and the
 * samples in whatever PCM encoding the decoder gives (DecodedPcm).
 *
 * The delay and padding an encoder left, which an M4A declares (its
 * iTunSMPB or edit list), stay in: the decoder is told of none, so that
 * it trims nothing. Media Foundation seems to keep them on Windows (a
 * reference it read for a user's session is a whole number of AAC
 * frames long), and a session's reference has to be the same audio on
 * both, or the pitch the session holds is out of line with it.
 *
 * Each file's format, decoder and length go to the log (stderr, which
 * Help > Save Log... keeps), and so does why a file cannot be read.
 */
class AndroidMediaReadStream : public breakfastquay::AudioReadStream
{
public:
    AndroidMediaReadStream(std::string path);
    ~AndroidMediaReadStream() override;

    std::string getTrackName() const override { return ""; }
    std::string getArtistName() const override { return ""; }
    std::string getError() const override;

    /// The file extensions it is registered for, in lower case
    static std::vector<std::string> getExtensions();

protected:
    size_t getFrames(size_t count, float *frames) override;

private:
    class D;
    std::unique_ptr<D> m_d;
};

#endif
