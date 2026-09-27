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

#include "AndroidMediaReadStream.h"

#include "DecodedPcm.h"

#include <bqaudiostream/Exceptions.h>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaError.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace breakfastquay;

namespace {

// MediaFormat's KEY_ENCODER_DELAY and KEY_ENCODER_PADDING: their NDK
// names are of API level 29, and Tony's is 28
const char *const keyEncoderDelay = "encoder-delay";
const char *const keyEncoderPadding = "encoder-padding";

// How long one wait for decoded output lasts, and how long the decoder
// may take nothing and give nothing before it is taken to have stopped
const int64_t waitMicroseconds = 5000;
const auto stalledAfter = std::chrono::seconds(10);

struct FormatDeleter {
    void operator()(AMediaFormat *f) const { AMediaFormat_delete(f); }
};
typedef std::unique_ptr<AMediaFormat, FormatDeleter> Format;

// The format a MIME type names, for the log: "AAC (audio/mp4a-latm)"
std::string
describeMime(const std::string &mime)
{
    static const char *const names[][2] = {
        { "audio/mp4a-latm", "AAC" },
        { "audio/flac", "FLAC" },
        { "audio/vorbis", "Vorbis" },
        { "audio/opus", "Opus" },
        { "audio/mpeg", "MP3" },
        { "audio/3gpp", "AMR-NB" },
        { "audio/amr-wb", "AMR-WB" },
        { "audio/alac", "Apple Lossless" },
        { "audio/ac3", "AC-3" },
        { "audio/eac3", "E-AC-3" },
        { "audio/raw", "PCM" },
    };
    for (const auto &n : names) {
        if (mime == n[0]) return std::string(n[1]) + " (" + mime + ")";
    }
    return mime;
}

std::string
seconds(double s)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << s << " s";
    return os.str();
}

// One line to the log at once, so that another thread's cannot break it
void
logLine(const std::string &line)
{
    std::cerr << ("AndroidMediaReadStream: " + line + "\n") << std::flush;
}

}

// Tags: the extensions of files Android's extractors read (Android's
// "Supported media formats") and the Android build cannot read
// otherwise. Not wav, mp3 or opus: libsndfile, libmad and opusfile read
// those, and where two readers claim one extension the first registered
// wins, in an order that depends on the linker
static AudioReadStreamBuilder<AndroidMediaReadStream>
androidMediaBuilder("https://github.com/jhhr/tony/AndroidMediaReadStream",
                    AndroidMediaReadStream::getExtensions());

std::vector<std::string>
AndroidMediaReadStream::getExtensions()
{
    return {
        "m4a", "mp4", "aac", "3gp", // AAC, and AMR in 3GPP
        "amr",
        "flac",
        "ogg", "oga",               // Vorbis or Opus
        "webm", "mka", "mkv"        // Vorbis or Opus, and others
    };
}

class AndroidMediaReadStream::D
{
public:
    D(std::string p) : path(p) { }
    ~D();

    std::string path;
    std::string error;

    int fd = -1;
    AMediaExtractor *extractor = nullptr;
    AMediaCodec *codec = nullptr;
    bool started = false;

    std::string mime;
    std::string codecName;
    int64_t durationUs = 0;
    int32_t delay = 0;
    int32_t padding = 0;
    int trackRate = 0;
    int trackChannels = 0;

    // The decoder's output format as it stands
    int rate = 0;
    int channels = 0;
    int encoding = DecodedPcm::Int16;

    // Made at the first decoded audio, whose channels and rate are the
    // stream's from then on
    std::unique_ptr<DecodedPcm> pcm;
    int streamRate = 0;
    bool rateChangeLogged = false;

    bool inputEnded = false;
    bool outputEnded = false;
    int64_t framesQueued = 0;

    void open();
    void decodeUntil(bool (D::*done)() const);
    bool hasDecoded() const { return pcm != nullptr || outputEnded; }
    bool hasAvailable() const {
        return (pcm && pcm->getAvailable() > 0) || outputEnded;
    }

private:
    bool step();
    void takeOutputFormat();
    void takeOutput(ssize_t index, const AMediaCodecBufferInfo &info);
    [[noreturn]] void fail(std::string why);
};

AndroidMediaReadStream::D::~D()
{
    if (codec) {
        if (started) AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
    }
    if (extractor) AMediaExtractor_delete(extractor);
    // After the extractor, which may still be reading it
    if (fd >= 0) ::close(fd);
}

void
AndroidMediaReadStream::D::fail(std::string why)
{
    error = why;
    logLine("\"" + path + "\": " + why);
    throw InvalidFileFormat(path, why);
}

void
AndroidMediaReadStream::D::open()
{
    fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        int e = errno;
        error = std::string("cannot open the file: ") + std::strerror(e);
        logLine("\"" + path + "\": " + error);
        if (e == ENOENT) throw FileNotFound(path);
        throw FileReadFailed(path);
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        fail(std::string("cannot read the file: ") + std::strerror(errno));
    }

    extractor = AMediaExtractor_new();
    if (!extractor) fail("Android gave no media extractor");

    media_status_t status =
        AMediaExtractor_setDataSourceFd(extractor, fd, 0, st.st_size);
    if (status != AMEDIA_OK) {
        fail("Android does not know the file's format (media error " +
             std::to_string(int(status)) + ")");
    }

    // The first audio track
    size_t tracks = AMediaExtractor_getTrackCount(extractor);
    Format format;
    size_t track = 0;
    std::string others;
    for (size_t i = 0; i < tracks; ++i) {
        Format f(AMediaExtractor_getTrackFormat(extractor, i));
        const char *m = nullptr;
        if (!f || !AMediaFormat_getString(f.get(), AMEDIAFORMAT_KEY_MIME, &m) ||
            !m) {
            continue;
        }
        std::string trackMime(m);
        if (!format && trackMime.compare(0, 6, "audio/") == 0) {
            format = std::move(f);
            track = i;
            mime = trackMime;
        } else {
            others += (others == "" ? "" : ", ") + trackMime;
        }
    }
    if (!format) {
        fail("the file has no audio track" +
             (others == "" ? std::string() : " (it holds " + others + ")"));
    }

    status = AMediaExtractor_selectTrack(extractor, track);
    if (status != AMEDIA_OK) {
        fail("Android cannot read the audio track (media error " +
             std::to_string(int(status)) + ")");
    }

    int32_t v = 0;
    if (AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE, &v)) {
        trackRate = rate = v;
    }
    if (AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT, &v)) {
        trackChannels = channels = v;
    }
    int64_t d = 0;
    if (AMediaFormat_getInt64(format.get(), AMEDIAFORMAT_KEY_DURATION, &d)) {
        durationUs = d;
    }

    // The decoder would trim the delay and padding its format declares,
    // which Media Foundation seems not to (see the header): so it is told
    // of none
    if (AMediaFormat_getInt32(format.get(), keyEncoderDelay, &delay) &&
        delay != 0) {
        AMediaFormat_setInt32(format.get(), keyEncoderDelay, 0);
    }
    if (AMediaFormat_getInt32(format.get(), keyEncoderPadding, &padding) &&
        padding != 0) {
        AMediaFormat_setInt32(format.get(), keyEncoderPadding, 0);
    }

    codec = AMediaCodec_createDecoderByType(mime.c_str());
    if (!codec) {
        fail("this phone has no decoder for " + describeMime(mime) +
             ", the format of the file's audio");
    }

    char *name = nullptr;
    if (AMediaCodec_getName(codec, &name) == AMEDIA_OK && name) {
        codecName = name;
        AMediaCodec_releaseName(codec, name);
    }

    status = AMediaCodec_configure(codec, format.get(), nullptr, nullptr, 0);
    if (status != AMEDIA_OK) {
        fail("the decoder " + codecName + " does not take this " +
             describeMime(mime) + " track (media error " +
             std::to_string(int(status)) + ")");
    }

    status = AMediaCodec_start(codec);
    if (status != AMEDIA_OK) {
        fail("the decoder " + codecName + " did not start (media error " +
             std::to_string(int(status)) + ")");
    }
    started = true;

    // Until the first audio, whose format is the stream's
    decodeUntil(&D::hasDecoded);
    if (!pcm) {
        fail("the decoder " + codecName + " gave no audio from this " +
             describeMime(mime) + " track");
    }
}

void
AndroidMediaReadStream::D::decodeUntil(bool (D::*done)() const)
{
    auto lastProgress = std::chrono::steady_clock::now();
    while (!(this->*done)()) {
        if (step()) {
            lastProgress = std::chrono::steady_clock::now();
        } else if (std::chrono::steady_clock::now() - lastProgress >
                   stalledAfter) {
            fail("the decoder " + codecName + " stopped giving audio after " +
                 std::to_string(pcm ? pcm->getFramesAdded() : 0) + " frames");
        }
    }
}

// The next compressed frame to the decoder, if it has room for one, and
// what it has decoded from the decoder, if it has anything. False if
// neither happened, having waited a little for output
bool
AndroidMediaReadStream::D::step()
{
    bool progress = false;

    if (!inputEnded) {
        ssize_t index = AMediaCodec_dequeueInputBuffer(codec, 0);
        if (index >= 0) {
            size_t capacity = 0;
            uint8_t *buffer = AMediaCodec_getInputBuffer
                (codec, size_t(index), &capacity);
            if (!buffer) fail("the decoder gave no input buffer");

            ssize_t size = -1;
            if (AMediaExtractor_getSampleTrackIndex(extractor) >= 0) {
                size = AMediaExtractor_readSampleData
                    (extractor, buffer, capacity);
                if (size < 0) {
                    fail("Android could not read compressed frame " +
                         std::to_string(framesQueued + 1) + ", of " +
                         std::to_string(AMediaExtractor_getSampleSize(extractor)) +
                         " bytes, into the decoder's buffer of " +
                         std::to_string(capacity));
                }
            }

            media_status_t status;
            if (size < 0) {
                // No frames left: the decoder is told, and gives what it
                // still holds, the last with the end-of-stream flag
                status = AMediaCodec_queueInputBuffer
                    (codec, size_t(index), 0, 0, 0,
                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                inputEnded = true;
            } else {
                int64_t time = AMediaExtractor_getSampleTime(extractor);
                status = AMediaCodec_queueInputBuffer
                    (codec, size_t(index), 0, size_t(size),
                     uint64_t(time < 0 ? 0 : time), 0);
                ++framesQueued;
                AMediaExtractor_advance(extractor);
            }
            if (status != AMEDIA_OK) {
                fail("the decoder did not take a compressed frame (media "
                     "error " + std::to_string(int(status)) + ")");
            }
            progress = true;
        } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            fail("the decoder gave no input buffer (media error " +
                 std::to_string(index) + ")");
        }
    }

    // Wait for output only if there was no input to give: then there is
    // nothing to do but wait
    AMediaCodecBufferInfo info;
    ssize_t index = AMediaCodec_dequeueOutputBuffer
        (codec, &info, progress ? 0 : waitMicroseconds);

    if (index >= 0) {
        takeOutput(index, info);
        progress = true;
    } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        takeOutputFormat();
        progress = true;
    } else if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        // Nothing to do: buffers are asked for by index each time
        progress = true;
    } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        fail("the decoder gave no output (media error " +
             std::to_string(index) + ")");
    }

    return progress;
}

void
AndroidMediaReadStream::D::takeOutputFormat()
{
    Format format(AMediaCodec_getOutputFormat(codec));
    if (!format) return;

    int32_t v = 0;
    if (AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE, &v) &&
        v > 0) {
        rate = v;
    }
    if (AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT, &v) &&
        v > 0) {
        channels = v;
    }

    // Without it the samples are 16-bit integers (MediaFormat's
    // KEY_PCM_ENCODING)
    int32_t e = DecodedPcm::Int16;
    AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_PCM_ENCODING, &e);
    encoding = e;

    if (!DecodedPcm::isKnownEncoding(encoding)) {
        fail("the decoder " + codecName + " gives samples in " +
             DecodedPcm::describe(encoding) + ", which Tony cannot read");
    }
}

void
AndroidMediaReadStream::D::takeOutput(ssize_t index,
                                      const AMediaCodecBufferInfo &info)
{
    size_t size = 0;
    uint8_t *buffer = AMediaCodec_getOutputBuffer(codec, size_t(index), &size);

    bool usable = buffer && info.size > 0 && info.offset >= 0 &&
        size_t(info.offset) + size_t(info.size) <= size &&
        !(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG);

    if (usable) {
        if (!pcm) {
            if (channels < 1 || rate < 1) {
                AMediaCodec_releaseOutputBuffer(codec, size_t(index), false);
                fail("the decoder " + codecName + " gives audio of " +
                     std::to_string(channels) + " channels at " +
                     std::to_string(rate) + " Hz");
            }
            pcm.reset(new DecodedPcm(channels));
            streamRate = rate;
        } else if (rate != streamRate && !rateChangeLogged) {
            // It cannot be resampled here: the rest plays at the wrong
            // speed. Not known to happen
            logLine("\"" + path + "\": the decoder's rate changed from " +
                std::to_string(streamRate) + " to " + std::to_string(rate) +
                " Hz part way through; the rest is taken as " +
                std::to_string(streamRate) + " Hz");
            rateChangeLogged = true;
        }
        pcm->add(buffer + info.offset, size_t(info.size), encoding, channels);
    }

    AMediaCodec_releaseOutputBuffer(codec, size_t(index), false);

    if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) && !outputEnded) {
        outputEnded = true;
        int64_t frames = pcm ? pcm->getFramesAdded() : 0;
        std::ostringstream os;
        os << "\"" << path << "\": decoded " << frames << " frames";
        if (streamRate > 0) {
            os << " (" << seconds(double(frames) / streamRate) << ")";
        }
        os << " from " << framesQueued << " compressed frames";
        if (pcm && pcm->getBytesDropped() > 0) {
            os << "; " << pcm->getBytesDropped()
               << " bytes that were not whole frames left out";
        }
        logLine(os.str());
    }
}

AndroidMediaReadStream::AndroidMediaReadStream(std::string path) :
    m_d(new D(path))
{
    m_channelCount = 0;
    m_sampleRate = 0;
    m_seekable = false;

    m_d->open();

    m_channelCount = size_t(m_d->pcm->getChannelCount());
    m_sampleRate = size_t(m_d->streamRate);
    if (m_d->durationUs > 0) {
        m_estimatedFrameCount = size_t(std::llround
            (double(m_d->durationUs) * double(m_sampleRate) / 1.0e6));
    }

    // Which decoder, and what it gives: the first thing to look for in a
    // log when a file sounds or lines up wrong
    std::ostringstream os;
    os << "\"" << path << "\": " << describeMime(m_d->mime)
       << " by " << (m_d->codecName == "" ? "a decoder" : m_d->codecName)
       << ": " << m_sampleRate << " Hz, " << m_channelCount << " channel(s), "
       << DecodedPcm::describe(m_d->encoding);
    if (m_d->trackRate != int(m_sampleRate) ||
        m_d->trackChannels != int(m_channelCount)) {
        os << " (the file says " << m_d->trackRate << " Hz, "
           << m_d->trackChannels << " channel(s))";
    }
    if (m_d->durationUs > 0) {
        os << "; " << seconds(double(m_d->durationUs) / 1.0e6) << ", about "
           << m_estimatedFrameCount << " frames";
    }
    if (m_d->delay != 0 || m_d->padding != 0) {
        os << "; encoder delay " << m_d->delay << " and padding "
           << m_d->padding << " frames kept, not trimmed";
    }
    logLine(os.str());
}

AndroidMediaReadStream::~AndroidMediaReadStream()
{
}

std::string
AndroidMediaReadStream::getError() const
{
    return m_d ? m_d->error : std::string();
}

size_t
AndroidMediaReadStream::getFrames(size_t count, float *frames)
{
    size_t got = 0;
    while (got < count) {
        got += m_d->pcm->read(frames + got * m_channelCount, count - got);
        if (got == count || m_d->outputEnded) break;
        m_d->decodeUntil(&D::hasAvailable);
    }
    return got;
}
