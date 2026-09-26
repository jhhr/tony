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

#include "OboeAudioIO.h"

#include <bqaudioio/ApplicationPlaybackSource.h>
#include <bqaudioio/ApplicationRecordTarget.h>

#include <bqvec/Allocators.h>
#include <bqvec/VectorOps.h>

#include <oboe/Oboe.h>

#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include <time.h>

using namespace breakfastquay;
using std::cerr;
using std::endl;

namespace {

// Callbacks that must have reached the application since a start
// before the streams count as running as they will go on: by then
// FullDuplexStream has finished draining the input
const int steadyCallbacks = 8;

// How long the constructor runs the streams, at most, for them to
// have timestamps
const int openWaitMillis = 1000;

// Readings taken for one measurement of the latency
const int readingsWanted = 9;

int64_t
monotonicNanos()
{
    // The clock of Oboe's timestamps
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

// How a stream was opened, as the log has it and as a measured round
// trip's fingerprint keeps it: everything its latency depends on
std::string
describeStream(oboe::AudioStream *stream)
{
    // Only an AAudio stream can be asked about MMAP
    bool mmap = (stream->getAudioApi() == oboe::AudioApi::AAudio &&
                 oboe::OboeExtensions::isMMapUsed(stream));
    std::ostringstream os;
    os << oboe::convertToText(stream->getAudioApi())
       << (mmap ? " (MMAP)" : "")
       << ", " << stream->getSampleRate() << " Hz, "
       << stream->getChannelCount() << " channel(s), "
       << oboe::convertToText(stream->getFormat()) << ", "
       << oboe::convertToText(stream->getPerformanceMode()) << ", "
       << oboe::convertToText(stream->getSharingMode())
       << ", burst " << stream->getFramesPerBurst()
       << ", buffer " << stream->getBufferSizeInFrames()
       << " of " << stream->getBufferCapacityInFrames() << " frames";
    if (stream->getDirection() == oboe::Direction::Input) {
        os << ", preset " << oboe::convertToText(stream->getInputPreset());
    }
    return os.str();
}

// The device AAudio opened a stream on, as Android's AudioManager lists
// it. Only the id if it is not listed, or the stream cannot say
// (OpenSL ES: 0). On the GUI thread, as QJniObject clears and logs any
// exception a call throws
AudioRoute::Device
lookUpDevice(int id, bool input)
{
    AudioRoute::Device device;
    device.id = id;
    if (id <= 0) return device;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return device;
    QJniObject manager = context.callObjectMethod
        ("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
         QJniObject::fromString("audio").object<jstring>());
    if (!manager.isValid()) return device;

    // AudioManager.GET_DEVICES_INPUTS and GET_DEVICES_OUTPUTS
    QJniObject devices = manager.callObjectMethod
        ("getDevices", "(I)[Landroid/media/AudioDeviceInfo;",
         jint(input ? 1 : 2));
    if (!devices.isValid()) return device;

    QJniEnvironment env;
    jobjectArray array = devices.object<jobjectArray>();
    const jsize count = env->GetArrayLength(array);
    for (jsize i = 0; i < count; ++i) {
        QJniObject info = QJniObject::fromLocalRef
            (env->GetObjectArrayElement(array, i));
        if (!info.isValid()) continue;
        if (info.callMethod<jint>("getId", "()I") != id) continue;
        device.type = info.callMethod<jint>("getType", "()I");
        QJniObject name = info.callObjectMethod
            ("getProductName", "()Ljava/lang/CharSequence;");
        if (name.isValid()) {
            device.productName = name.callObjectMethod
                ("toString", "()Ljava/lang/String;").toString();
        }
        break;
    }
    return device;
}

std::string
describeDevice(const AudioRoute::Device &device)
{
    std::ostringstream os;
    os << AudioRoute::deviceName(device).toStdString()
       << ", device " << device.id << ", type " << device.type;
    return os.str();
}

std::string
describeMs(int frames, int rate)
{
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(1);
    os << (rate > 0 ? double(frames) * 1000.0 / rate : 0.0) << " ms";
    return os.str();
}

std::string
describe(StreamLatency::Estimate latency, bool withInput, int rate)
{
    auto ms = [rate](int frames) { return double(frames) * 1000.0 / rate; };
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(1);
    os << "output " << latency.output << " frames ("
       << ms(latency.output) << " ms)";
    if (withInput) {
        os << ", input " << latency.input << " frames ("
           << ms(latency.input) << " ms), round trip "
           << latency.roundTrip() << " frames ("
           << ms(latency.roundTrip()) << " ms)";
    }
    return os.str();
}

}

// The output stream's data callback: input and output together once
// FullDuplexStream is done draining, or output alone
class OboeAudioIO::Engine : public oboe::FullDuplexStream
{
public:
    explicit Engine(OboeAudioIO *io) : m_io(io) { }

    // Whether the input is read. Changed only while the streams stop
    std::atomic<bool> duplex { false };

    // Odd while a callback runs, so that a latency reading can tell
    // whether one ran while it was taken
    std::atomic<uint32_t> callbackSeq { 0 };

    // Callbacks that have reached the application since the last start
    std::atomic<int> processed { 0 };

    // The input could not be read, and FullDuplexStream has stopped
    // both streams. Oboe reports no error for a stream that has no
    // callback of its own, as the input has not
    std::atomic<bool> inputFailed { false };

    // The most input frames one callback has read since the last start,
    // once FullDuplexStream was done draining: more than a callback
    // leaves between reads is input that piled up while the callbacks
    // were held up, and was read back up at once
    std::atomic<int> largestRead { 0 };

    // Where the input is read to: room for all the input stream holds.
    // Allocated before the streams start, never in the callback
    void setInputRoom(int frames, int channels) {
        m_inputRoom = std::max(0, frames);
        m_inputChannels = std::max(1, channels);
        m_inputBuffer.assign(size_t(m_inputRoom) * m_inputChannels, 0.f);
    }

    // FullDuplexStream reads only as many input frames as the output
    // asks for, so input that piles up while the callbacks are held up
    // (the phone busy, as after a take) stays piled up for as long as
    // the streams run, the input that much later all the while, until
    // the input buffer is full. Everything there is is read instead,
    // into a buffer of this class's, since FullDuplexStream's has room
    // for one output buffer only
    oboe::ResultWithValue<int32_t> readInput(int32_t numFrames) override {
        oboe::AudioStream *input = getInputStream();
        oboe::ResultWithValue<int32_t> available = input->getAvailableFrames();
        int32_t wanted = StreamLatency::inputFramesToRead
            (numFrames, available ? available.value() : 0, m_inputRoom);
        oboe::ResultWithValue<int32_t> read =
            input->read(m_inputBuffer.data(), wanted, 0 /* no wait */);
        m_inputRead = read ? read.value() : 0;
        return read;
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream,
                                          void *audioData,
                                          int32_t numFrames) override {
        callbackSeq.fetch_add(1, std::memory_order_acq_rel);
        oboe::DataCallbackResult result =
            oboe::DataCallbackResult::Continue;
        if (duplex.load(std::memory_order_relaxed)) {
            result = FullDuplexStream::onAudioReady
                (stream, audioData, numFrames);
            if (result != oboe::DataCallbackResult::Continue) {
                inputFailed.store(true);
            }
        } else {
            m_io->process(nullptr, 0,
                          static_cast<float *>(audioData), numFrames);
            processed.fetch_add(1, std::memory_order_relaxed);
        }
        callbackSeq.fetch_add(1, std::memory_order_acq_rel);
        return result;
    }

    // The input FullDuplexStream hands over is in its own buffer, which
    // readInput() above does not fill: what it read, and how much, is
    // in this class's
    oboe::DataCallbackResult onBothStreamsReady(const void *,
                                                int numInputFrames,
                                                void *outputData,
                                                int numOutputFrames)
        override {
        int frames = std::min(numInputFrames, m_inputRead);
        if (frames > largestRead.load(std::memory_order_relaxed)) {
            largestRead.store(frames, std::memory_order_relaxed);
        }
        m_io->process(m_inputBuffer.data(), frames,
                      static_cast<float *>(outputData), numOutputFrames);
        processed.fetch_add(1, std::memory_order_relaxed);
        return oboe::DataCallbackResult::Continue;
    }

private:
    OboeAudioIO *m_io;
    std::vector<float> m_inputBuffer;
    int m_inputRoom = 0;
    int m_inputChannels = 1;
    int m_inputRead = 0;
};

// The output stream's error callback. Oboe calls it on a thread of its
// own, which holds the stream, and the stream holds this: so it may
// run after the OboeAudioIO has gone, and touches nothing of it
class OboeAudioIO::ErrorFlag : public oboe::AudioStreamErrorCallback
{
public:
    std::atomic<bool> failed { false };

    void onErrorAfterClose(oboe::AudioStream *, oboe::Result) override {
        failed.store(true);
    }
};

OboeAudioIO::OboeAudioIO(ApplicationRecordTarget *target,
                         ApplicationPlaybackSource *source) :
    SystemAudioIO(target, source),
    m_epoch(std::chrono::steady_clock::now()),
    m_rate(0),
    m_sourceChannels(2),
    m_outputChannels(0),
    m_inputChannels(0),
    m_maxFrames(0),
    m_outBufferChannels(0),
    m_outBuffers(nullptr),
    m_inBuffers(nullptr),
    m_gain(1.f),
    m_balance(0.f),
    m_suspended(true),
    m_inputRunning(false),
    m_recordSuppressed(false),
    m_startFailed(false),
    m_reopening(false),
    m_outputXRuns(0),
    m_inputXRunsAtStart(-1)
{
    if (m_source && m_source->getApplicationChannelCount() > 0) {
        m_sourceChannels = m_source->getApplicationChannelCount();
    }

    if (!openStreams()) return;

    if (!measureOnceOpen()) {
        // Without the input, the caller can still have playback
        m_startupError = "Failed to start the audio streams";
        cerr << "OboeAudioIO: " << m_startupError << endl;
        if (m_input) {
            m_input->close();
            m_input.reset();
        } else {
            m_output->close();
            m_output.reset();
        }
    }
}

OboeAudioIO::~OboeAudioIO()
{
    closeStreams();
    m_engine.reset();
    freeBuffers();
}

bool
OboeAudioIO::openStreams()
{
    // A fresh engine and error flag for each pair of streams: an old
    // stream's error callback may yet come, and must not mark these
    m_engine.reset(new Engine(this));
    m_errors = std::make_shared<ErrorFlag>();
    m_outputXRuns = 0;

    // The output first, at the device's own rate; the input is opened
    // at the same rate, the lowest latency path for both. Exclusive
    // where the device allows it: AAudio opens a shared stream if not
    oboe::AudioStreamBuilder outBuilder;
    outBuilder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::Float)
        ->setFormatConversionAllowed(true)
        ->setChannelCount(2)
        ->setChannelConversionAllowed(true)
        ->setUsage(oboe::Usage::Media)
        ->setContentType(oboe::ContentType::Music)
        ->setDataCallback(m_engine.get())
        ->setErrorCallback(m_errors);

    oboe::Result result = outBuilder.openStream(m_output);
    if (result == oboe::Result::OK &&
        m_output->getFormat() != oboe::AudioFormat::Float) {
        result = oboe::Result::ErrorInvalidFormat;
        m_output->close();
    }
    if (result != oboe::Result::OK) {
        m_startupError = std::string("Failed to open the audio output: ") +
            oboe::convertToText(result);
        cerr << "OboeAudioIO: " << m_startupError << endl;
        m_output.reset();
        return false;
    }

    m_rate = m_output->getSampleRate();
    m_outputChannels = m_output->getChannelCount();

    if (m_target) {
        oboe::AudioStreamBuilder inBuilder;
        inBuilder.setDirection(oboe::Direction::Input)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setFormatConversionAllowed(true)
            ->setChannelCount(1)
            ->setChannelConversionAllowed(true)
            ->setSampleRate(m_rate)
            // Low latency and no automatic gain or noise suppression.
            // Android 10 and later; before that Oboe asks for
            // VoiceRecognition, the nearest
            ->setInputPreset(oboe::InputPreset::VoicePerformance)
            // FullDuplexStream's advice
            ->setBufferCapacityInFrames
            (m_output->getBufferCapacityInFrames() * 2);

        result = inBuilder.openStream(m_input);
        // At the output's rate, in float, and with no more channels than
        // the output has, which FullDuplexStream's own input buffer had
        // room for: Engine::readInput() reads into one of its own now,
        // but no more has been tried
        if (result == oboe::Result::OK &&
            (m_input->getSampleRate() != m_rate ||
             m_input->getFormat() != oboe::AudioFormat::Float ||
             m_input->getChannelCount() > m_outputChannels)) {
            result = oboe::Result::ErrorInvalidFormat;
            m_input->close();
        }
        if (result != oboe::Result::OK) {
            m_startupError = std::string("Failed to open the audio input: ") +
                oboe::convertToText(result);
            cerr << "OboeAudioIO: " << m_startupError << endl;
            m_input.reset();
            return false;
        }
    }

    freeBuffers();
    m_inputChannels = (m_input ? m_input->getChannelCount() : 0);
    int burst = m_output->getFramesPerBurst();
    m_maxFrames = std::max({ m_output->getBufferCapacityInFrames(),
                             burst, 1024 });
    m_outBufferChannels = std::max(m_sourceChannels, m_outputChannels);
    m_outBuffers = allocate_and_zero_channels<float>
        (m_outBufferChannels, m_maxFrames);
    if (m_input) {
        m_inBuffers = allocate_and_zero_channels<float>
            (m_inputChannels, m_maxFrames);
        m_engine->setInputRoom(m_input->getBufferCapacityInFrames(),
                               m_inputChannels);
        m_engine->setSharedInputStream(m_input);
    }
    m_engine->setSharedOutputStream(m_output);

    // All of this before the first callback, as PortAudioIO does. The
    // latency is a guess until it is measured
    m_latency = StreamLatency::guess
        (m_output->getBufferSizeInFrames(),
         m_input ? m_input->getFramesPerBurst() : 0);

    if (m_source) {
        m_source->setSystemPlaybackBlockSize(burst);
        m_source->setSystemPlaybackSampleRate(m_rate);
        m_source->setSystemPlaybackLatency(m_latency.output);
        // Not the stream's channel count: the wrappers svapp puts round
        // its play source take this for the number of channels they
        // will be asked for, and refuse any other. The mix to the
        // stream's channels is done here
        m_source->setSystemPlaybackChannelCount(m_sourceChannels);
    }
    if (m_target) {
        m_target->setSystemRecordBlockSize(m_input->getFramesPerBurst());
        m_target->setSystemRecordSampleRate(m_rate);
        m_target->setSystemRecordLatency(m_latency.input);
        m_target->setSystemRecordChannelCount(m_inputChannels);
    }

    logStream("output", m_output.get());
    if (m_input) logStream("input", m_input.get());
    findRoute();
    return true;
}

void
OboeAudioIO::closeStreams()
{
    if (!m_suspended) stopStreams();
    m_suspended = true;

    // Closing waits for a callback that is running; none comes after
    if (m_output) m_output->close();
    if (m_input) m_input->close();
    m_output.reset();
    m_input.reset();
}

void
OboeAudioIO::freeBuffers()
{
    if (m_outBuffers) deallocate_channels(m_outBuffers, m_outBufferChannels);
    if (m_inBuffers) deallocate_channels(m_inBuffers, m_inputChannels);
    m_outBuffers = nullptr;
    m_inBuffers = nullptr;
}

bool
OboeAudioIO::measureOnceOpen()
{
    // Run the streams until they can say what their latency is, so that
    // the first take is compensated by a measurement too, and leave
    // them suspended: the application suspends a new device at once
    // anyway (MainWindowBase::createAudioIO() does)
    auto started = std::chrono::steady_clock::now();
    oboe::Result result = startStreams();
    if (result != oboe::Result::OK) {
        cerr << "OboeAudioIO: failed to start: "
             << oboe::convertToText(result) << endl;
        m_startFailed = true;
        return false;
    }
    m_suspended = false;

    bool measurable = waitUntilMeasurable(openWaitMillis);
    int waited = int(std::chrono::duration_cast<std::chrono::milliseconds>
                     (std::chrono::steady_clock::now() - started).count());
    StreamLatency::Estimate latency;
    int backlog = 0;
    bool withInput = m_inputRunning;
    bool measured = measurable && measureLatency(latency, backlog);
    stopStreams();
    m_suspended = true;

    if (measured) {
        report(latency, withInput);
        cerr << "OboeAudioIO: latency measured " << waited
             << " ms after starting: "
             << describe(m_latency, m_input != nullptr, m_rate) << endl;
    } else {
        cerr << "OboeAudioIO: no timestamps " << waited
             << " ms after starting; latency guessed: "
             << describe(m_latency, m_input != nullptr, m_rate) << endl;
    }
    return true;
}

bool
OboeAudioIO::reopen()
{
    // Whatever route Android has now: the one that went may have gone
    // for good (headphones out), or come back with another id
    m_reopening = true;
    closeStreams();
    bool ok = openStreams() && measureOnceOpen();
    m_reopening = false;
    if (!ok) {
        cerr << "OboeAudioIO: the streams could not be opened again" << endl;
    }
    return ok;
}

bool
OboeAudioIO::isSourceOK() const
{
    // Without a record target the input is not wanted
    return !m_target || m_input != nullptr;
}

bool
OboeAudioIO::isTargetOK() const
{
    return m_output != nullptr;
}

double
OboeAudioIO::getCurrentTime() const
{
    // The play source asks in the callback too: this reads a clock and
    // nothing else
    return std::chrono::duration<double>
        (std::chrono::steady_clock::now() - m_epoch).count();
}

bool
OboeAudioIO::hasFailed() const
{
    return m_errors->failed.load() || m_engine->inputFailed.load() ||
        m_startFailed;
}

void
OboeAudioIO::resume()
{
    if (!m_suspended || !m_output || hasFailed()) return;

    oboe::Result result = startStreams();

    // Streams that Android disconnected while they were stopped (after
    // an idle spell, or a route that went away) are opened afresh and
    // started at once, so that what was asked for goes ahead rather than
    // recording nothing until the window reopens the device
    if (result == oboe::Result::ErrorDisconnected && !m_reopening) {
        cerr << "OboeAudioIO: failed to start: "
             << oboe::convertToText(result)
             << "; the streams were disconnected while stopped, so they "
             << "are opened again" << endl;
        if (!reopen()) {
            m_startFailed = true;
            return;
        }
        result = startStreams();
    }

    if (result != oboe::Result::OK) {
        cerr << "OboeAudioIO: failed to start: "
             << oboe::convertToText(result) << endl;
        m_startFailed = true;
        return;
    }

    m_suspended = false;
}

oboe::Result
OboeAudioIO::startStreams()
{
    bool duplex = (m_input && !m_recordSuppressed);
    m_engine->duplex.store(duplex);
    m_engine->processed.store(0);
    m_engine->largestRead.store(0);

    // FullDuplexStream starts the input, then the output, and starts
    // draining the input again
    oboe::Result result =
        (duplex ? m_engine->start() : m_output->requestStart());
    m_inputRunning = duplex;

    m_inputXRunsAtStart = -1;
    if (duplex && result == oboe::Result::OK) {
        oboe::ResultWithValue<int32_t> xruns = m_input->getXRunCount();
        if (xruns) m_inputXRunsAtStart = xruns.value();
    }

    if (result != oboe::Result::OK) stopStreams();
    return result;
}

void
OboeAudioIO::suspend()
{
    if (m_suspended || !m_output) return;

    // Measured while the streams still run, reported once they have
    // stopped, so that nothing the audio thread reads changes under it.
    // The application reads the figures when a take starts, so the next
    // take is compensated by what the device did now
    StreamLatency::Estimate latency;
    int backlog = 0;
    bool withInput = m_inputRunning;
    bool steady = (m_engine->processed.load() >= steadyCallbacks);

    // Input lost to an overrun: the device's position ran on while the
    // frames read did not, and the timestamps would give the loss as
    // latency (the phone's logs had 244 and 484 ms, one and two buffers)
    int overruns = 0;
    if (withInput && m_inputXRunsAtStart >= 0) {
        oboe::ResultWithValue<int32_t> xruns = m_input->getXRunCount();
        if (xruns) overruns = xruns.value() - m_inputXRunsAtStart;
    }

    bool measured = steady && overruns == 0 &&
        measureLatency(latency, backlog);
    int largestRead = m_engine->largestRead.load();

    stopStreams();
    m_suspended = true;

    if (measured) {
        StreamLatency::Estimate before = m_latency;
        report(latency, withInput);
        if (m_latency.output != before.output ||
            m_latency.input != before.input) {
            cerr << "OboeAudioIO: latency now "
                 << describe(m_latency, m_input != nullptr, m_rate) << endl;
        }
    } else if (overruns > 0) {
        cerr << "OboeAudioIO: the input overran " << overruns
             << " time(s) while running, and what was lost would read as "
             << "latency, so the latency was not measured: kept at "
             << describe(m_latency, m_input != nullptr, m_rate) << endl;
    } else if (steady && backlog > 0) {
        cerr << "OboeAudioIO: the input was " << backlog << " frames ("
             << describeMs(backlog, m_rate) << ") behind as the device "
             << "stopped, so the latency was not measured: kept at "
             << describe(m_latency, m_input != nullptr, m_rate) << endl;
    }

    // Input that piled up while the callbacks were held up, or that
    // comes in bursts, and was read at once: FullDuplexStream would have
    // left it waiting, the input that much later for the rest of the run
    if (withInput && !keptUp(largestRead)) {
        cerr << "OboeAudioIO: one callback read " << largestRead
             << " frames (" << describeMs(largestRead, m_rate)
             << ") of input at once while running, more than a callback "
             << "leaves: the callbacks were held up, or come in bursts"
             << endl;
    }

    oboe::ResultWithValue<int32_t> xruns = m_output->getXRunCount();
    if (xruns && xruns.value() != m_outputXRuns) {
        m_outputXRuns = xruns.value();
        cerr << "OboeAudioIO: " << m_outputXRuns
             << " output underrun(s) since the device was opened" << endl;
    }
}

void
OboeAudioIO::suppressRecordSide(bool suppress)
{
    if (suppress == m_recordSuppressed) return;
    bool wasRunning = !m_suspended;
    if (wasRunning) suspend();
    m_recordSuppressed = suppress;
    if (wasRunning) resume();
}

void
OboeAudioIO::setOutputGain(float gain)
{
    SystemAudioIO::setOutputGain(gain);
    m_gain.store(gain);
}

void
OboeAudioIO::setOutputBalance(float balance)
{
    SystemAudioIO::setOutputBalance(balance);
    m_balance.store(balance);
}

void
OboeAudioIO::stopStreams()
{
    // The output first, as its callback reads the input. stop() waits
    // until the stream has stopped, so no callback runs after this.
    // A stream Oboe has closed after an error just says so
    if (m_output) m_output->stop();
    if (m_input && m_inputRunning) m_input->stop();
    m_inputRunning = false;
}

bool
OboeAudioIO::waitUntilMeasurable(int maxMillis) const
{
    auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(maxMillis);
    while (!m_suspended && !hasFailed()) {
        if (m_engine->processed.load() >= steadyCallbacks) {
            bool stamped = bool(m_output->getTimestamp(CLOCK_MONOTONIC));
            if (stamped && m_inputRunning) {
                stamped = bool(m_input->getTimestamp(CLOCK_MONOTONIC));
            }
            if (stamped) return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool
OboeAudioIO::measureLatency(StreamLatency::Estimate &latency,
                            int &backlog) const
{
    // Readings are taken here, not in the callback: Oboe advises
    // against timestamps there before Android 11. A reading that a
    // callback ran through is thrown away, as it would pair one
    // callback's output count with another's input count; so is one
    // taken just after a callback returned and before Oboe counted what
    // it wrote, by the median. So is one taken while the input was not
    // being read as it came in, the callbacks held up: its input latency
    // is how far behind they were, not the device's (backlog says the
    // most that was waiting then)
    bool withInput = m_inputRunning;
    backlog = 0;
    std::vector<StreamLatency::Estimate> readings;
    for (int attempt = 0;
         attempt < readingsWanted * 5 && int(readings.size()) < readingsWanted;
         ++attempt) {

        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(700));
        }

        uint32_t before = m_engine->callbackSeq.load(std::memory_order_acquire);
        if (before & 1) continue;

        StreamLatency::Position output, input;
        output.appFrames = m_output->getFramesWritten();
        int waiting = 0;
        if (withInput) {
            input.appFrames = m_input->getFramesRead();
            auto available = m_input->getAvailableFrames();
            if (!available) continue;
            waiting = available.value();
        }

        auto outputStamp = m_output->getTimestamp(CLOCK_MONOTONIC);
        if (!outputStamp) continue;
        output.hardwareFrame = outputStamp.value().position;
        output.hardwareNanos = outputStamp.value().timestamp;

        if (withInput) {
            auto inputStamp = m_input->getTimestamp(CLOCK_MONOTONIC);
            if (!inputStamp) continue;
            input.hardwareFrame = inputStamp.value().position;
            input.hardwareNanos = inputStamp.value().timestamp;
        }

        int64_t now = monotonicNanos();
        if (m_engine->callbackSeq.load(std::memory_order_acquire) != before) {
            continue;
        }

        if (withInput && !keptUp(waiting)) {
            backlog = std::max(backlog, waiting);
            continue;
        }

        readings.push_back(StreamLatency::fromReading
                           (StreamLatency::outputLatency(output, now, m_rate),
                            withInput ?
                            StreamLatency::inputLatency(input, now, m_rate) :
                            0.0));
    }

    return StreamLatency::median(readings, m_rate, latency);
}

bool
OboeAudioIO::keptUp(int waiting) const
{
    return StreamLatency::inputKeptUp
        (waiting, m_output ? m_output->getBufferSizeInFrames() : 0,
         m_input ? m_input->getFramesPerBurst() : 0);
}

void
OboeAudioIO::report(StreamLatency::Estimate latency, bool withInput)
{
    // Only while the streams are stopped: the play source's wrappers
    // are not safe to change under the callback
    m_latency.output = latency.output;
    if (m_source) m_source->setSystemPlaybackLatency(m_latency.output);
    if (withInput) {
        m_latency.input = latency.input;
        if (m_target) m_target->setSystemRecordLatency(m_latency.input);
    }
}

void
OboeAudioIO::logStream(std::string name, oboe::AudioStream *stream) const
{
    cerr << "OboeAudioIO: " << name << ": " << describeStream(stream) << endl;
}

void
OboeAudioIO::findRoute()
{
    // The devices AAudio chose: for an unspecified device, those of the
    // route Android has now, which is what a measured round trip belongs
    // to. Their ids change when a device is plugged in again, so the
    // route is named by their types and product names
    m_route = AudioRoute::Route();
    m_route.driver = "oboe";
    m_route.rate = m_rate;
    m_route.output = lookUpDevice(m_output->getDeviceId(), false);
    m_route.outputStreams = QString::fromStdString
        (describeStream(m_output.get()));
    if (m_input) {
        m_route.hasInput = true;
        m_route.input = lookUpDevice(m_input->getDeviceId(), true);
        m_route.inputStreams = QString::fromStdString
            (describeStream(m_input.get()));
    }
    cerr << "OboeAudioIO: route: output " << describeDevice(m_route.output);
    if (m_input) {
        cerr << "; input " << describeDevice(m_route.input);
    } else {
        cerr << "; no input";
    }
    cerr << endl;
}

void
OboeAudioIO::process(const float *input, int inputFrames,
                     float *output, int outputFrames)
{
    // On the audio thread: no allocation, no lock, no logging.
    //
    // The input first. The application counts on having a block's
    // input before it is asked for the block's output (the start gap,
    // recording.md "Latency")
    if (m_target && input && inputFrames > 0) {
        float peakLeft = 0.f, peakRight = 0.f;
        for (int done = 0; done < inputFrames; ) {
            int n = std::min(inputFrames - done, m_maxFrames);
            v_deinterleave(m_inBuffers,
                           input + size_t(done) * m_inputChannels,
                           m_inputChannels, n);
            for (int c = 0; c < m_inputChannels && c < 2; ++c) {
                float peak = 0.f;
                for (int i = 0; i < n; ++i) {
                    peak = std::max(peak, std::fabs(m_inBuffers[c][i]));
                }
                if (c == 0) peakLeft = std::max(peakLeft, peak);
                if (c == 1 || m_inputChannels == 1) {
                    peakRight = std::max(peakRight, peak);
                }
            }
            m_target->putSamples(m_inBuffers, m_inputChannels, n);
            done += n;
        }
        m_target->setInputLevels(peakLeft, peakRight);
    }

    if (!output || outputFrames <= 0) return;

    if (!m_source) {
        v_zero(output, outputFrames * m_outputChannels);
        return;
    }

    // As bqaudioio's Gains: the balance turns down the other side
    float gain = m_gain.load(std::memory_order_relaxed);
    float balance = m_balance.load(std::memory_order_relaxed);
    float leftGain = (balance > 0.f ? gain * (1.f - balance) : gain);
    float rightGain = (balance < 0.f ? gain * (1.f + balance) : gain);

    float peakLeft = 0.f, peakRight = 0.f;
    for (int done = 0; done < outputFrames; ) {
        int n = std::min(outputFrames - done, m_maxFrames);
        int got = m_source->getSourceSamples
            (m_outBuffers, m_sourceChannels, n);
        got = std::max(0, std::min(got, n));
        if (got < n) {
            for (int c = 0; c < m_sourceChannels; ++c) {
                v_zero(m_outBuffers[c] + got, n - got);
            }
        }
        v_reconfigure_channels_inplace
            (m_outBuffers, m_outputChannels, m_sourceChannels, n);
        for (int c = 0; c < m_outputChannels; ++c) {
            float g = (c == 0 ? leftGain : c == 1 ? rightGain : gain);
            v_scale(m_outBuffers[c], g, n);
            if (c < 2) {
                float peak = 0.f;
                for (int i = 0; i < n; ++i) {
                    peak = std::max(peak, std::fabs(m_outBuffers[c][i]));
                }
                if (c == 0) peakLeft = std::max(peakLeft, peak);
                if (c == 1 || m_outputChannels == 1) {
                    peakRight = std::max(peakRight, peak);
                }
            }
        }
        v_interleave(output + size_t(done) * m_outputChannels,
                     m_outBuffers, m_outputChannels, n);
        done += n;
    }
    m_source->setOutputLevels(peakLeft, peakRight);
}
