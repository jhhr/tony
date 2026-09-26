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

#include "AudioCheckRunner.h"

#include "MainWindow.h"
#include "Analyser.h"
#include "SingingTakes.h"

#include "audio/AudioCallbackRecordTarget.h"
#include "base/PlayParameterRepository.h"
#include "base/PlayParameters.h"
#include "base/Preferences.h"
#include "base/Selection.h"
#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"
#include "data/fileio/WavFileWriter.h"
#include "data/model/ReadOnlyWaveFileModel.h"
#include "data/model/WaveFileModel.h"
#include "layer/Layer.h"
#include "transform/ModelTransformerFactory.h"
#include "view/ViewManager.h"
#include "widgets/LevelPanToolButton.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iostream>

using std::cerr;
using std::endl;
using std::vector;

using namespace sv;

bool
AudioCheckResult::calibrationUsable() const
{
    // A take recorded at another rate is misplaced by an amount that
    // grows with its position, so no one round trip places it right
    if (failure != "" || rateMismatch) return false;
    return summary.verdict == LatencyCheck::Verdict::Ok ||
        summary.verdict == LatencyCheck::Verdict::Unsteady;
}

AudioCheckRunner::AudioCheckRunner(MainWindow *window) :
    QObject(window),
    m_window(window),
    m_timer(new QTimer(this)),
    m_step(Step::Idle),
    m_punchIn(0),
    m_openingReference(false),
    m_inPoll(false),
    m_stepLimitMs(0)
{
    m_timer->setInterval(kPollMs);
    connect(m_timer, &QTimer::timeout, this, &AudioCheckRunner::poll);
}

AudioCheckRunner::~AudioCheckRunner()
{
    // Deleted by the window in its destructor, before anything it reads
    // goes, so the window is whole here.  A run ends without a word:
    // whoever was waiting for it goes with the window.  A take in
    // progress is left to the window, as any take is when it is deleted:
    // stopping it here would splice it and start its analysis in the
    // middle of the window's teardown
    if (m_step != Step::Idle) {
        cerr << "AudioCheckRunner: the window is going; the run ends" << endl;
    }
    abandon();
}

void
AudioCheckRunner::abandon()
{
    m_timer->stop();
    if (m_step == Step::Idle) return;
    cerr << "AudioCheckRunner: the run is abandoned" << endl;
    clearOverride();
    m_step = Step::Idle;
}

AudioCheckRunner::Plan
AudioCheckRunner::calibrationPlan()
{
    Plan plan;
    plan.layout = LatencyCheck::calibrationLayout();
    plan.punchIns = 4;
    plan.eventsEach = 3;
    return plan;
}

vector<LatencyCheck::PunchIn>
AudioCheckRunner::punchInsOf(const Plan &plan)
{
    if (plan.ranges.empty()) {
        return LatencyCheck::punchInsFor
            (plan.layout, plan.punchIns, plan.eventsEach);
    }
    if (plan.layout.rate <= 0) return {};

    // One after another along the timeline, as judgeTake() is told they
    // were recorded.  Two may meet, as takes into adjacent selections do.
    // Written so that a range of NaNs fails too
    const double length = double(plan.layout.length) / plan.layout.rate;
    double free = 0.0;
    for (const LatencyCheck::PunchIn &p : plan.ranges) {
        if (!(p.start >= free) || !(p.end > p.start) || !(p.end <= length)) {
            return {};
        }
        free = p.end;
    }
    return plan.ranges;
}

QString
AudioCheckRunner::referenceDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString
AudioCheckRunner::nextReferencePath(QString directory, QString inUse)
{
    const QString stem = "calibrate-audio-reference";
    const QDir dir(directory);
    const QFileInfo held(inUse);

    // One that cannot be removed (another program has it open, say)
    // stays, and its number is passed over below
    const QStringList names =
        dir.entryList(QStringList() << stem + "*.wav", QDir::Files);
    for (const QString &name : names) {
        const QString path = dir.absoluteFilePath(name);
        if (inUse != "" && QFileInfo(path) == held) continue;
        if (!QFile::remove(path)) {
            cerr << "AudioCheckRunner: an earlier reference could not be "
                 << "removed: " << path << endl;
        }
    }

    for (int n = 1; ; ++n) {
        const QString path =
            dir.absoluteFilePath(QString("%1-%2.wav").arg(stem).arg(n));
        if (!QFileInfo::exists(path)) return path;
    }
}

bool
AudioCheckRunner::start(const Plan &plan)
{
    if (m_step != Step::Idle) return false;

    if (m_window->m_recordTarget && m_window->m_recordTarget->isRecording()) {
        cerr << "AudioCheckRunner::start: a take is being recorded" << endl;
        return false;
    }

    vector<LatencyCheck::PunchIn> punchIns = punchInsOf(plan);
    if (punchIns.empty()) {
        if (plan.ranges.empty()) {
            cerr << "AudioCheckRunner::start: the layout has no room for "
                 << plan.punchIns << " punch-ins of " << plan.eventsEach
                 << " events" << endl;
        } else {
            cerr << "AudioCheckRunner::start: the punch-ins given overlap, "
                 << "are out of order, are empty or reach outside the layout"
                 << endl;
        }
        return false;
    }

    // Written so that a NaN fails too
    if (!(plan.preRoll >= 0.0)) {
        cerr << "AudioCheckRunner::start: a pre-roll of " << plan.preRoll
             << " s" << endl;
        return false;
    }

    if (plan.keepSession && !m_window->getMainModel()) {
        cerr << "AudioCheckRunner::start: there is no session to record into"
             << endl;
        return false;
    }

    m_plan = plan;
    m_result = AudioCheckResult();
    m_reported = Progress();

    // The devices the takes will be recorded on.  The window's device
    // menus are shut while the run lasts; the rate comes with the takes
    {
        QSettings settings;
        m_result.key = LatencyCalibration::currentKey(settings, 0);
    }
    m_punchIns = punchIns;
    m_starts.clear();
    m_ends.clear();
    m_punchIn = 0;

    cerr << "AudioCheckRunner::start: " << m_punchIns.size() << " punch-ins";
    if (m_plan.ranges.empty()) {
        cerr << " of " << m_plan.eventsEach << " events";
    }
    if (m_plan.keepSession) cerr << ", into the session open now";
    if (m_plan.roundTrip >= 0.0) {
        cerr << ", placed with a round trip of its own, "
             << m_plan.roundTrip * 1000.0 << " ms";
    }
    if (m_plan.preRoll != kPreRollSeconds) {
        cerr << ", with a pre-roll of " << m_plan.preRoll << " s";
    }
    cerr << endl;

    // Nothing is done before the first poll, so that however the run
    // ends, the caller hears of it through finished() and never from
    // inside this call
    setStep(Step::OpeningReference, 0);
    m_timer->start();
    return true;
}

void
AudioCheckRunner::cancel()
{
    if (m_step == Step::Idle) return;
    stopTake();
    end(tr("The check was cancelled."));
}

bool
AudioCheckRunner::isRunning() const
{
    return m_step != Step::Idle;
}

void
AudioCheckRunner::sessionClosing()
{
    // Opening the reference closes the session it replaces
    if (m_step == Step::Idle || m_openingReference) return;
    stopTake();
    end(tr("The session was closed during the check."));
}

void
AudioCheckRunner::poll()
{
    if (m_inPoll) return;
    m_inPoll = true;

    // Before the step, so that the first poll says the reference is
    // being opened before it is, and after it, so that a step begun here
    // is reported as it begins and not a poll later
    reportProgress();

    switch (m_step) {

    case Step::Idle:
        m_timer->stop();
        break;

    case Step::OpeningReference:
        openReference();
        break;

    case Step::AnalysingReference:
        if (!analysing(m_window->m_analyser)) {
            startPunchIn();
        } else if (stepTimedOut()) {
            end(tr("The test reference was not analysed in time."));
        }
        break;

    case Step::Recording:
        if (!m_window->m_recordTarget ||
            !m_window->m_recordTarget->isRecording()) {
            takeStopped();
        } else if (stepTimedOut()) {
            stopTake();
            end(tr("A take did not stop at the end of its range."));
        }
        break;

    case Step::AnalysingTake:
        // Not needed for the judgement, which reads the take's audio: it
        // keeps pYIN's load out of the timing of the next take
        if (!analysing(m_window->m_analyser2)) {
            if (++m_punchIn < int(m_starts.size())) {
                startPunchIn();
            } else {
                judge();
            }
        } else if (stepTimedOut()) {
            end(tr("A take was not analysed in time."));
        }
        break;
    }

    reportProgress();

    m_inPoll = false;
}

void
AudioCheckRunner::openReference()
{
    // The session open now is the reference, as the caller says, and
    // stays open with its take
    if (m_plan.keepSession) {
        if (!m_window->getMainModel()) {
            end(tr("There is no session to record into."));
            return;
        }
        referenceOpen();
        return;
    }

    // Each call below may show a dialog, and while one is up the run may
    // end: the session closed, or the check cancelled.  A check's own
    // session holds nothing of the user's, and its takes have marked it
    // modified: it is let go as answering "No" does
    if (sessionIsACheck()) {
        cerr << "AudioCheckRunner: replacing the session of an earlier "
             << "check without asking" << endl;
        m_window->m_documentModified = false;
    } else if (!m_window->checkSaveModified()) {
        if (m_step == Step::OpeningReference) {
            end(tr("Your session was kept open, so the check did not "
                   "replace it."));
        }
        return;
    }
    if (m_step != Step::OpeningReference) return;

    // Made from the plan and written afresh every time, to a file of its
    // own: the session open now may be the check before, and on Windows
    // the file it plays cannot be written over.  A plan that names a
    // file has that one written over
    QString path = m_plan.referencePath;
    if (path == "") {
        path = nextReferencePath(referenceDirectory(), mainModelFile());
        m_plan.referencePath = path;
    }
    cerr << "AudioCheckRunner: writing the reference to " << path << endl;
    QDir().mkpath(QFileInfo(path).absolutePath());
    vector<float> samples = LatencyCheck::generate(m_plan.layout);
    WavFileWriter writer(path, m_plan.layout.rate, 1,
                         WavFileWriter::WriteToTarget);
    const float *data = samples.data();
    if (!writer.isOK() ||
        !writer.writeSamples(&data, sv_frame_t(samples.size())) ||
        !writer.close()) {
        end(tr("The test reference could not be written to \"%1\".")
            .arg(path));
        return;
    }

    m_openingReference = true;
    MainWindowBase::FileOpenStatus status =
        m_window->openPath(path, MainWindowBase::ReplaceSession);
    m_openingReference = false;
    if (m_step != Step::OpeningReference) return;

    if (status != MainWindowBase::FileOpenSucceeded ||
        !m_window->getMainModel()) {
        end(tr("The test reference \"%1\" could not be opened.").arg(path));
        return;
    }

    referenceOpen();
}

bool
AudioCheckRunner::sessionIsACheck() const
{
    // Saved, it is the user's to keep, whatever it holds
    if (m_window->m_sessionFile != "") return false;

    const QString file = mainModelFile();
    if (file == "") return false;

    // Canonical paths, where both exist
    return QFileInfo(file).absoluteDir() == QDir(referenceDirectory());
}

QString
AudioCheckRunner::mainModelFile() const
{
    // Not ReadOnlyWaveFileModel::getLocalFilename(): with the "normalise
    // audio" preference on, as Tony has it, that is the file the reader
    // decoded the audio into, in the temporary directory.  The location
    // is what the model was opened from, resolved here as it was then
    auto model = std::dynamic_pointer_cast<ReadOnlyWaveFileModel>
        (m_window->getMainModel());
    if (!model) return "";
    FileSource source(model->getLocation());
    if (source.isRemote()) return "";
    return source.getLocalFilename();
}

void
AudioCheckRunner::referenceOpen()
{
    // The punch-ins from here on are as the takes record them: in whole
    // frames of the session, which is what the selections are made of
    const sv_samplerate_t rate = m_window->getMainModel()->getSampleRate();
    m_result.referenceRate = rate;
    for (LatencyCheck::PunchIn &p : m_punchIns) {
        m_starts.push_back(sv_frame_t(std::llround(p.start * rate)));
        m_ends.push_back(sv_frame_t(std::llround(p.end * rate)));
        p = LatencyCheck::PunchIn(double(m_starts.back()) / rate,
                                  double(m_ends.back()) / rate);
    }

    // Its layers were made as it opened, or are there already
    setPlayback();

    setStep(Step::AnalysingReference, kReferenceTimeoutMs);
}

void
AudioCheckRunner::startPunchIn()
{
    // A take of the user's own is never stopped by this record(), which
    // would be a Stop
    if (m_window->m_recordTarget && m_window->m_recordTarget->isRecording()) {
        end(tr("Another take was being recorded."));
        return;
    }

    const sv_frame_t from = m_starts[m_punchIn];
    const sv_frame_t to = m_ends[m_punchIn];
    const Step step = m_step;

    cerr << "AudioCheckRunner: punch-in " << (m_punchIn + 1) << " of "
         << m_starts.size() << ", [" << from << "," << to << ")" << endl;

    // The selection is what Record into Selection records.  Made quietly:
    // a selection the user makes has the reference's pitch analysed
    // again inside it, which would load the machine during the take
    ViewManager *viewManager = m_window->m_viewManager;
    viewManager->clearSelections();
    viewManager->addSelectionQuietly(Selection(from, to));

    // Again for every take: whatever has happened since the last, the
    // takes are all recorded with the same playback
    setPlayback();

    m_window->m_audioCheckTakes = true;
    m_window->m_audioCheckRoundTrip = m_plan.roundTrip;
    m_window->m_audioCheckPreRoll = m_plan.preRoll;
    m_window->record();
    if (m_step != step) return;

    if (!m_window->m_recordTarget ||
        !m_window->m_recordTarget->isRecording()) {
        end(tr("The take did not start: the audio device could not be "
               "opened, or it refused to record."));
        return;
    }

    // The lead-in and the range, then time for the latency and for the
    // take to see that it has reached the end
    const double seconds =
        double(m_window->m_takePreRoll + (to - from)) / m_result.referenceRate;
    setStep(Step::Recording, qint64(seconds * 1000.0) + kTakeStopTimeoutMs);
}

void
AudioCheckRunner::takeStopped()
{
    clearOverride();
    m_result.takes.push_back(m_window->m_takeLatency);

    // The splice went wrong (the window has said so), or the recording
    // was too short to hold anything
    const SingingTakes *takes = m_window->m_takes;
    if (!takes->haveTake() ||
        !takes->getCoverage().contains(m_starts[m_punchIn])) {
        end(tr("A take was not added to the singing track."));
        return;
    }

    setStep(Step::AnalysingTake, kTakeAnalysisTimeoutMs);
}

void
AudioCheckRunner::judge()
{
    // The file holds what was recorded, at the rate it was recorded at
    vector<float> mono;
    sv_samplerate_t rate = 0;
    const QString error =
        readTakeFile(m_window->m_takes->getAudioPath(), mono, rate);
    if (error != "") {
        end(error);
        return;
    }

    m_result.summary = LatencyCheck::judgeTake
        (m_plan.layout, mono.data(), sv_frame_t(mono.size()), rate,
         m_punchIns);

    end("");
}

QString
AudioCheckRunner::readTakeFile(QString path, vector<float> &mono,
                               sv_samplerate_t &rate)
{
    mono.clear();
    rate = 0;

    FileSource source(path);
    WavFileReader reader(source);
    if (!reader.isOK() || reader.getChannelCount() < 1) {
        return tr("The take's audio file \"%1\" could not be read: %2")
            .arg(path).arg(reader.getError());
    }

    const int channels = reader.getChannelCount();
    const floatvec_t data = reader.getInterleavedFrames
        (0, reader.getFrameCount());
    const sv_frame_t count = sv_frame_t(data.size()) / channels;
    mono.assign(count, 0.f);
    for (sv_frame_t i = 0; i < count; ++i) {
        float sum = 0.f;
        for (int c = 0; c < channels; ++c) sum += data[i * channels + c];
        mono[i] = sum / float(channels);
    }
    rate = reader.getSampleRate();
    return "";
}

void
AudioCheckRunner::setPlayback()
{
    const float gain = float(referenceGain());

    // The reference's model: its waveform layer, and any other layer on
    // it, has these parameters.  Audible even if the user has muted the
    // reference in their own sessions: the check has to hear it
    if (auto params = PlayParameterRepository::getInstance()
        ->getPlayParameters(m_window->getMainModelId().untyped)) {
        params->setPlayAudible(true);
        params->setPlayPan(0.f);
        params->setPlayGain(gain);
    }

    Analyser *reference = m_window->m_analyser;
    for (Analyser::Component c : { Analyser::PitchTrack, Analyser::Notes }) {
        Layer *layer = reference ? reference->getLayer(c) : nullptr;
        if (!layer) continue;
        if (auto params = layer->getPlayParameters()) {
            params->setPlayAudible(false);
        }
    }

    // The toolbar's level control shows the reference's gain.  Given one
    // between its notches, it moves to the nearest and says so, and the
    // window sets that gain through Analyser::setGain() and setAudible(),
    // which write the shared settings.  Moved here first without a word,
    // it has nothing to say when the window shows the gain
    if (LevelPanToolButton *control = m_window->m_audioLPW) {
        QSignalBlocker quiet(control);
        control->setLevel(gain);
        control->setPan(0.f);
    }
    m_window->updateLayerStatuses();
}

double
AudioCheckRunner::referenceGain()
{
    // Made with its peak at kPeakDbfs, and read normalised to full scale,
    // as MainWindow has every audio file read
    if (!Preferences::getInstance()->getNormaliseAudio()) return 1.0;
    return std::pow(10.0, LatencyCheck::kPeakDbfs / 20.0);
}

AudioCheckRunner::Progress
AudioCheckRunner::currentProgress() const
{
    Progress p;
    p.step = m_step;
    p.punchIns = int(m_punchIns.size());
    if (m_step == Step::Recording || m_step == Step::AnalysingTake) {
        p.punchIn = m_punchIn + 1;
    }

    // The lead-in of a take to come is the whole of the plan's, unless
    // its range starts sooner than that
    for (int i = m_punchIn; i < int(m_punchIns.size()); ++i) {
        const LatencyCheck::PunchIn &range = m_punchIns[i];
        double seconds = std::min(m_plan.preRoll, range.start) +
            (range.end - range.start);
        if (i == m_punchIn) {
            if (m_step == Step::AnalysingTake) continue;
            if (m_step == Step::Recording) {
                seconds = std::max(0.0, seconds -
                                   double(m_stepClock.elapsed()) / 1000.0);
            }
        }
        p.secondsLeft += seconds;
    }
    return p;
}

void
AudioCheckRunner::reportProgress()
{
    if (m_step == Step::Idle) return;
    const Progress p = currentProgress();
    if (p.step == m_reported.step && p.punchIn == m_reported.punchIn &&
        std::ceil(p.secondsLeft) == std::ceil(m_reported.secondsLeft)) {
        return;
    }
    m_reported = p;
    emit progress(p);
}

void
AudioCheckRunner::setStep(Step step, qint64 limitMs)
{
    m_step = step;
    m_stepLimitMs = limitMs;
    m_stepClock.start();
}

bool
AudioCheckRunner::stepTimedOut() const
{
    return m_stepLimitMs > 0 && m_stepClock.elapsed() > m_stepLimitMs;
}

void
AudioCheckRunner::stopTake()
{
    if (m_step != Step::Recording) return;
    if (m_window->m_recordTarget && m_window->m_recordTarget->isRecording()) {
        // The Stop button's path, which is also how a take ends itself
        m_window->record();
    }
}

void
AudioCheckRunner::end(QString failure)
{
    if (m_step == Step::Idle) return;

    m_timer->stop();
    m_step = Step::Idle;
    clearOverride();

    m_result.failure = failure;
    if (!m_result.takes.empty()) {
        // The reported pair as the take path worked it out, and compares
        // a stored figure's fingerprint with
        const TakeLatency &first = m_result.takes.front();
        m_result.usedRoundTrip = first.recordingSeconds(first.roundTrip);
        m_result.reportedOutputLatency = first.reportedOutput;
        m_result.reportedInputLatency = first.reportedInput;
        m_result.recordingRate = first.recordingRate;
        m_result.key.rate = first.recordingRate;
        m_result.rateMismatch = m_result.recordingRate > 0 &&
            m_result.referenceRate > 0 &&
            m_result.recordingRate != m_result.referenceRate;
    }
    if (failure == "") {
        m_result.calibratedRoundTrip = LatencyCheck::calibratedRoundTrip
            (m_result.usedRoundTrip, m_result.summary.medianOffset);
    }

    if (failure != "") {
        cerr << "AudioCheckRunner: the check ended: " << failure << endl;
    } else {
        const LatencyCheck::TakeSummary &s = m_result.summary;
        cerr << "AudioCheckRunner: " << LatencyCheck::verdictName(s.verdict)
             << ", " << s.found << " of " << s.judged << " sweeps found, "
             << "median offset " << s.medianOffset * 1000.0 << " ms, spread "
             << s.spread * 1000.0 << " ms, input peak " << s.inputPeak
             << "; round trip used " << m_result.usedRoundTrip * 1000.0
             << " ms, measured " << m_result.calibratedRoundTrip * 1000.0
             << " ms; recorded at " << m_result.recordingRate
             << " Hz, reference at " << m_result.referenceRate << " Hz"
             << endl;
    }

    // A copy: whoever hears of it may start another run
    AudioCheckResult result = m_result;
    emit finished(result);
}

void
AudioCheckRunner::clearOverride()
{
    m_window->m_audioCheckTakes = false;
    m_window->m_audioCheckRoundTrip = -1.0;
    m_window->m_audioCheckPreRoll = kPreRollSeconds;
}

bool
AudioCheckRunner::analysing(Analyser *a)
{
    // What the app suite waits for: the first analysis complete, that of
    // a recorded range merged, and the transform threads gone as well.
    // Except that an analyser with no pitch track has nothing to wait
    // for: with automatic analysis switched off, the reference is never
    // analysed, and the check needs no analysis of its own
    if (ModelTransformerFactory::getInstance()->haveRunningTransformers()) {
        return true;
    }
    if (!a) return false;
    if (a->isAnalysingRange()) return true;
    return a->getLayer(Analyser::PitchTrack) &&
        a->getInitialAnalysisCompletion() < 100;
}
