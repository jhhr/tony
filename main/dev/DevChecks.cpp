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

#ifdef TONY_DEV_CHECKS

#include "DevChecks.h"

#include "../MainWindow.h"
#include "../RealtimePitchTracker.h"
#include "../SingingTakes.h"

#include "audio/AudioCallbackPlaySource.h"
#include "audio/AudioCallbackRecordTarget.h"
#include "data/fileio/FileSource.h"
#include "data/fileio/WavFileReader.h"
#include "data/model/NoteModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "layer/Layer.h"

#include <bqaudioio/AudioFactory.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iostream>

using std::cerr;
using std::endl;
using std::vector;

using namespace sv;

namespace {

// Signed, to a tenth of a millisecond: the tolerance is 2 ms
QString
signedMs(double seconds)
{
    const double ms = std::round(seconds * 10000.0) / 10.0;
    return QString("%1%2 ms").arg(ms > 0.0 ? "+" : "")
        .arg(ms == 0.0 ? 0.0 : ms, 0, 'f', 1);
}

QString
unsignedMs(double seconds)
{
    return QString("%1 ms").arg(seconds * 1000.0, 0, 'f', 1);
}

QString
secondsText(double seconds)
{
    return QString("%1").arg(seconds, 0, 'f', 2);
}

// A value as the session file holds it: Event::toXml() writes it with
// QString::arg(), which keeps six significant figures
float
asSaved(float value)
{
    return QString("%1").arg(value).toFloat();
}

// The events as a session saved and read back has them, in order, so
// that they can be compared with those of the session read back
EventVector
asSaved(const EventVector &events)
{
    EventVector saved;
    saved.reserve(events.size());
    for (const Event &e : events) {
        Event s = e;
        if (s.hasValue()) s = s.withValue(asSaved(s.getValue()));
        if (s.hasLevel()) s = s.withLevel(asSaved(s.getLevel()));
        saved.push_back(s);
    }
    std::sort(saved.begin(), saved.end());
    return saved;
}

// Whether the events after the reopen are those before the save, and
// in words how many there are, or the first that differs
bool
sameEvents(const EventVector &before, const EventVector &after,
           sv_samplerate_t rate, QString what, QString &description)
{
    const EventVector b = asSaved(before);
    const EventVector a = asSaved(after);
    if (a == b) {
        description = QString("%1 %2, the same").arg(a.size()).arg(what);
        return true;
    }
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    sv_frame_t frame = (i < b.size() ? b[i].getFrame() :
                        i < a.size() ? a[i].getFrame() : 0);
    description = QString("%1 %2, %3 before the save; the first difference "
                          "at %4 s")
        .arg(a.size()).arg(what).arg(b.size())
        .arg(rate > 0 ? secondsText(double(frame) / rate) : QString("?"));
    return false;
}

double
median(vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return n % 2 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

// A level, full scale 1, in dBFS; exact silence in words
QString
levelText(double level)
{
    if (level <= 0.0) return "silence";
    return QString("%1 dBFS").arg(20.0 * std::log10(level), 0, 'f', 1);
}

// The peak of each channel of an audio file, full scale 1: the file as
// it is, not normalised as the session's models are read
vector<float>
channelPeaks(QString path, QString &error)
{
    error = "";
    if (path == "") {
        error = DevChecks::tr("its raw recording was not seen");
        return {};
    }
    FileSource source(path);
    WavFileReader reader(source);
    const int channels = reader.getChannelCount();
    if (!reader.isOK() || channels < 1) {
        error = DevChecks::tr("its raw recording \"%1\" could not be read: %2")
            .arg(path).arg(reader.getError());
        return {};
    }
    const floatvec_t data = reader.getInterleavedFrames
        (0, reader.getFrameCount());
    vector<float> peaks(channels, 0.f);
    for (size_t i = 0; i < data.size(); ++i) {
        float &p = peaks[i % size_t(channels)];
        p = std::max(p, std::fabs(data[i]));
    }
    return peaks;
}

// The inputs, counting from 1, in words: "input 2", "inputs 1 and 2"
QString
inputsText(const vector<int> &inputs)
{
    if (inputs.empty()) return DevChecks::tr("no input");
    QStringList names;
    for (int i : inputs) names << QString::number(i + 1);
    if (names.size() == 1) return DevChecks::tr("input %1").arg(names[0]);
    const QString last = names.takeLast();
    return DevChecks::tr("inputs %1 and %2").arg(names.join(", ")).arg(last);
}

} // namespace

QString
CheckResult::verdictName(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Pass: return "Pass";
    case Verdict::Fail: return "Fail";
    case Verdict::Measured: return "Measured";
    case Verdict::Skipped: return "Skipped";
    }
    return "";
}

int
DevReport::count(CheckResult::Verdict verdict) const
{
    int n = 0;
    for (const CheckResult &c : checks) {
        if (c.verdict == verdict) ++n;
    }
    return n;
}

DevChecks::DevChecks(MainWindow *window, AudioCheckRunner *runner) :
    QObject(window),
    m_window(window),
    m_runner(runner),
    m_timer(new QTimer(this)),
    m_stage(-1),
    m_begun(false),
    m_inPoll(false),
    m_runnerRunning(false),
    m_reopening(false),
    m_saved(false),
    m_observer(new TakeObserver(window, this)),
    m_observedPunchIn(0),
    m_haveFresh(false),
    m_reopened(false)
{
    m_timer->setInterval(kPollMs);
    connect(m_timer, &QTimer::timeout, this, &DevChecks::poll);

    // Direct: the result has no metatype, and the stage waiting for it
    // is looked at on the next poll
    connect(runner, &AudioCheckRunner::finished,
            this, &DevChecks::runnerFinished);

    // Direct as well: a take is watched from the poll of the runner's
    // that started it
    connect(runner, &AudioCheckRunner::progress,
            this, &DevChecks::runnerProgress);
}

DevChecks::~DevChecks()
{
    // Deleted by the window in its destructor, before the runner.  The
    // run ends without a word, as the runner's does, and so does the
    // runner's run it started: without the Stop path, which would splice
    // a take and start its analysis in the middle of the window's
    // teardown.  The take in progress is left to the window
    m_timer->stop();
    m_observer->stop();
    if (m_stage >= 0) {
        cerr << "DevChecks: the window is going; the run ends" << endl;
        if (m_runnerRunning && m_runner) m_runner->abandon();
        m_runnerRunning = false;
        m_stage = -1;
    }
}

vector<LatencyCheck::PunchIn>
DevChecks::freshPunchIns()
{
    return { LatencyCheck::PunchIn(6.3, 10.2),
             LatencyCheck::PunchIn(16.8, 21.2) };
}

QString
DevChecks::nextScratchFolder(QString directory, QString inUse)
{
    const QDir dir(directory);
    const QRegularExpression name("^dev-checks-[0-9]+$");

    // Only folders by our own name: the directory may hold anything else
    const QStringList folders =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &folder : folders) {
        if (!name.match(folder).hasMatch()) continue;
        const QString path = dir.absoluteFilePath(folder);
        if (inUse != "" && QFileInfo(inUse).absoluteDir() == QDir(path)) {
            continue;
        }
        if (!QDir(path).removeRecursively()) {
            cerr << "DevChecks: an earlier scratch folder could not be "
                 << "removed: " << path << endl;
        }
    }

    for (int n = 1; ; ++n) {
        const QString path =
            dir.absoluteFilePath(QString("dev-checks-%1").arg(n));
        if (QFileInfo::exists(path)) continue;
        if (!QDir().mkpath(path)) return "";
        return path;
    }
}

bool
DevChecks::start(const Options &options)
{
    if (isRunning()) return false;
    if (!m_runner || m_runner->isRunning()) {
        cerr << "DevChecks::start: the audio check is running" << endl;
        return false;
    }
    if (m_window->m_recordTarget && m_window->m_recordTarget->isRecording()) {
        cerr << "DevChecks::start: a take is being recorded" << endl;
        return false;
    }

    // The folder the session open now lives in stays, as it does
    const QString scratch = nextScratchFolder
        (scratchDirectory(options.scratchDirectory), m_window->m_sessionFile);
    if (scratch == "") {
        cerr << "DevChecks::start: no scratch folder could be made in "
             << scratchDirectory(options.scratchDirectory) << endl;
        return false;
    }

    m_options = options;
    m_scratchFolder = scratch;
    m_sessionPath = QDir(scratch).filePath(kSessionFileName);
    m_saved = false;
    m_startedAt = QDateTime::currentDateTime();
    {
        QSettings settings;
        m_devices = LatencyCalibration::currentKey(settings, 0);
    }

    m_layout = LatencyCheck::devLayout();
    m_observedPunchIn = 0;
    m_watched.clear();
    m_haveFresh = false;
    m_fresh = AudioCheckResult();
    m_coverageAfterFresh = Coverage();
    m_freshWatched.clear();
    m_pitchBefore.clear();
    m_notesBefore.clear();
    m_pitchAfter.clear();
    m_notesAfter.clear();
    m_reopened = false;
    m_rejudged = LatencyCheck::TakeSummary();
    m_runnerRunning = false;
    m_runnerResult = AudioCheckResult();

    m_stages.clear();
    m_stages.push_back({ tr("Fresh punch-ins"),
                         [this]() { beginFreshPunchIns(); },
                         [this]() { return freshPunchInsDone(); },
                         kCheckStageTimeoutMs });
    m_stages.push_back({ tr("Save and reopen"),
                         [this]() { beginReopen(); },
                         [this]() { return reopenDone(); },
                         kReopenTimeoutMs });

    cerr << "DevChecks::start: " << m_stages.size() << " stages, scratch "
         << "folder " << m_scratchFolder << ", round trip ";
    if (m_options.roundTrip >= 0.0) {
        cerr << m_options.roundTrip * 1000.0 << " ms" << endl;
    } else {
        cerr << "the window's own" << endl;
    }

    // Nothing is done before the first poll, so that however the run
    // ends, the caller hears of it through finished() and never from
    // inside this call
    m_stage = 0;
    m_begun = false;
    m_timer->start();
    return true;
}

void
DevChecks::cancel()
{
    if (!isRunning()) return;
    end(tr("The dev checks were cancelled."));
}

bool
DevChecks::isRunning() const
{
    return m_stage >= 0;
}

void
DevChecks::sessionClosing()
{
    // Its own reopen replaces the session, and so does its runner as it
    // opens the dev reference.  The window tells the runner first: a run
    // of the runner's still going after that is one replacing the session
    if (!isRunning() || m_reopening) return;
    if (m_runnerRunning && m_runner && m_runner->isRunning()) return;
    end(tr("The session was closed during the dev checks."));
}

void
DevChecks::poll()
{
    if (m_inPoll) return;
    if (!isRunning()) {
        m_timer->stop();
        return;
    }
    m_inPoll = true;

    // A copy: whatever a stage calls may end the run
    const int index = m_stage;
    const Stage stage = m_stages[index];

    if (!m_begun) {
        m_begun = true;
        m_stageClock.start();
        cerr << "DevChecks: stage " << (index + 1) << " of "
             << m_stages.size() << ": " << stage.name << endl;
        emit progress(stage.name, index + 1, int(m_stages.size()));
        if (m_stage == index) stage.begin();
    } else if (stage.done()) {
        if (m_stage == index) {
            m_begun = false;
            if (++m_stage >= int(m_stages.size())) {
                m_stage = index;
                end("");
            }
        }
    } else if (m_stage == index &&
               m_stageClock.elapsed() > qint64(stage.limitMs)) {
        end(tr("Stage %1 of %2, \"%3\", did not finish within %4 s.")
            .arg(index + 1).arg(m_stages.size()).arg(stage.name)
            .arg(stage.limitMs / 1000));
    }

    m_inPoll = false;
}

void
DevChecks::runnerFinished(const AudioCheckResult &result)
{
    // A run the dialog started, or anyone else, is not ours
    if (!m_runnerRunning) return;
    if (m_observer->isObserving()) finishObservation();
    m_runnerRunning = false;
    m_runnerResult = result;
}

void
DevChecks::runnerProgress(const AudioCheckRunner::Progress &state)
{
    if (!m_runnerRunning) return;

    // Reported once the take has started, from the same poll of the
    // runner's.  A punch-in's analysis is done when the next one starts
    // recording, or when the runner finishes
    using Step = AudioCheckRunner::Step;
    const bool sameTake =
        (state.step == Step::Recording || state.step == Step::AnalysingTake) &&
        state.punchIn == m_observedPunchIn;
    if (m_observer->isObserving() && !sameTake) finishObservation();

    if (state.step == Step::Recording && !m_observer->isObserving()) {
        m_observedPunchIn = state.punchIn;
        m_observer->start();
    }
}

void
DevChecks::finishObservation()
{
    m_observer->stop();
    Watched w;
    w.punchIn = m_observedPunchIn - 1;
    w.seen = m_observer->observation();
    m_watched.push_back(w);
    m_observedPunchIn = 0;
}

const DevChecks::Watched *
DevChecks::freshWatched(int i) const
{
    for (const Watched &w : m_freshWatched) {
        if (w.punchIn == i) return &w;
    }
    return nullptr;
}

void
DevChecks::beginFreshPunchIns()
{
    AudioCheckRunner::Plan plan;
    plan.layout = m_layout;
    plan.ranges = freshPunchIns();
    plan.roundTrip = m_options.roundTrip;

    m_watched.clear();
    m_observedPunchIn = 0;
    m_runnerResult = AudioCheckResult();
    m_runnerRunning = m_runner && m_runner->start(plan);
    if (!m_runnerRunning) {
        end(tr("The audio check could not start."));
    }
}

bool
DevChecks::freshPunchInsDone()
{
    if (m_runnerRunning) return false;

    if (m_runnerResult.failure != "") {
        end(tr("The punch-ins could not be recorded: %1")
            .arg(m_runnerResult.failure));
        return false;
    }

    m_fresh = m_runnerResult;
    m_haveFresh = true;
    m_coverageAfterFresh = m_window->m_takes->getCoverage();

    // Each channel of what the device delivered, before anything was
    // mixed or spliced, to see which input the mic is on
    m_freshWatched = m_watched;
    for (Watched &w : m_freshWatched) {
        w.channelPeaks = channelPeaks(w.seen.recordingPath, w.channelError);
    }
    return true;
}

void
DevChecks::beginReopen()
{
    // Read from the models before the save, and again from the models
    // there are after the reopen
    m_pitchBefore = takeEvents(Analyser::PitchTrack);
    m_notesBefore = takeEvents(Analyser::Notes);

    // The way Save As saves once it has a name, with no dialog but for a
    // failure: the take's audio is copied into the session's folder
    if (!m_window->saveSessionToPath(m_sessionPath)) {
        if (isRunning()) {
            end(tr("The session could not be saved to \"%1\".")
                .arg(m_sessionPath));
        }
        return;
    }
    m_saved = true;
    if (!isRunning()) return;

    m_reopening = true;
    const MainWindowBase::FileOpenStatus status =
        m_window->openPath(m_sessionPath, MainWindowBase::ReplaceSession);
    m_reopening = false;
    if (!isRunning()) return;

    if (status != MainWindowBase::FileOpenSucceeded) {
        end(tr("The session saved in \"%1\" could not be opened again.")
            .arg(m_sessionPath));
    }
}

bool
DevChecks::reopenDone()
{
    // The session restores its analyses rather than running them, but
    // waits as the runner does, in case something runs all the same
    if (AudioCheckRunner::analysing(m_window->m_analyser)) return false;
    if (!m_window->m_takes->haveTake()) {
        end(tr("The session opened again has no take."));
        return false;
    }
    if (!m_window->m_analyser2 ||
        AudioCheckRunner::analysing(m_window->m_analyser2)) {
        return false;
    }

    m_pitchAfter = takeEvents(Analyser::PitchTrack);
    m_notesAfter = takeEvents(Analyser::Notes);

    // The take's file as the session names it now: the copy in its
    // folder.  Judged over the same ranges as before the save
    vector<float> mono;
    sv_samplerate_t rate = 0;
    const QString error = AudioCheckRunner::readTakeFile
        (m_window->m_takes->getAudioPath(), mono, rate);
    if (error != "") {
        end(error);
        return false;
    }
    vector<LatencyCheck::PunchIn> ranges;
    for (const LatencyCheck::PunchInResult &p : m_fresh.summary.punchIns) {
        ranges.push_back(p.range);
    }
    m_rejudged = LatencyCheck::judgeTake
        (m_layout, mono.data(), sv_frame_t(mono.size()), rate, ranges);
    m_reopened = true;
    return true;
}

EventVector
DevChecks::takeEvents(Analyser::Component component) const
{
    Analyser *analyser = m_window->m_analyser2;
    Layer *layer = analyser ? analyser->getLayer(component) : nullptr;
    if (!layer) return {};
    if (auto model = ModelById::getAs<SparseTimeValueModel>
        (layer->getModel())) {
        return model->getAllEvents();
    }
    if (auto model = ModelById::getAs<NoteModel>(layer->getModel())) {
        return model->getAllEvents();
    }
    return {};
}

void
DevChecks::end(QString failure)
{
    if (!isRunning()) return;

    m_timer->stop();
    m_observer->stop();
    m_observedPunchIn = 0;
    m_stage = -1;
    m_begun = false;

    // A take of its own is stopped through the Stop path, as the check's
    // Cancel does; what the runner says then is not waited for
    if (m_runnerRunning) {
        m_runnerRunning = false;
        if (m_runner) m_runner->cancel();
    }

    DevReport report;
    report.failure = failure;
    report.checks = evaluate(failure);
    if (m_saved) report.sessionPath = m_sessionPath;
    report.reportPath = writeReport(report);

    cerr << "DevChecks: the run ended"
         << (failure != "" ? ": " + failure.toStdString() : std::string())
         << "; " << report.count(CheckResult::Verdict::Pass) << " passed, "
         << report.count(CheckResult::Verdict::Fail) << " failed, "
         << report.count(CheckResult::Verdict::Measured) << " measured, "
         << report.count(CheckResult::Verdict::Skipped) << " skipped; "
         << "report in " << report.reportPath << endl;

    emit finished(report);
}

vector<CheckResult>
DevChecks::evaluate(QString reason) const
{
    return { latencyCheck(reason), phrasesCheck(reason),
             liveDotsCheck(reason), speakersCheck(reason),
             micChannelCheck(reason) };
}

CheckResult
DevChecks::latencyCheck(QString reason) const
{
    CheckResult c;
    c.item = 1;
    c.name = "latency_on_this_machine";

    if (!m_haveFresh) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // Every judged sweep of every punch-in, found and within the
    // tolerance
    const LatencyCheck::TakeSummary &s = m_fresh.summary;
    QStringList problems;
    double largest = 0.0;
    for (int i = 0; i < int(s.punchIns.size()); ++i) {
        const LatencyCheck::PunchInResult &p = s.punchIns[i];
        QStringList offsets;
        for (const LatencyCheck::EventResult &e : s.events) {
            if (e.punchIn != i) continue;
            if (!e.arrival.found) {
                offsets << tr("not found");
                problems << tr("the sweep at %1 s was not found")
                    .arg(secondsText(e.expectedSeconds));
                continue;
            }
            const double offset = e.arrival.errorSeconds;
            offsets << signedMs(offset);
            if (std::fabs(offset) > std::fabs(largest)) largest = offset;
            if (std::fabs(offset) > kPlacementSeconds) {
                problems << tr("the sweep at %1 s landed %2 off")
                    .arg(secondsText(e.expectedSeconds))
                    .arg(signedMs(offset));
            }
        }
        if (p.judged == 0) {
            problems << tr("punch-in %1 judged no sweep").arg(i + 1);
        }
        c.numbers.push_back
            ({ tr("offsets, punch-in %1 (%2 to %3 s)").arg(i + 1)
               .arg(secondsText(p.range.start))
               .arg(secondsText(p.range.end)),
               offsets.isEmpty() ? tr("none judged") : offsets.join(", ") });
    }
    if (s.punchIns.empty()) problems << tr("no punch-in was judged");
    c.numbers.push_back({ tr("largest offset"), signedMs(largest) });
    c.numbers.push_back({ tr("round trip used"),
                          unsignedMs(m_fresh.usedRoundTrip) });

    if (!m_reopened) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        if (!problems.isEmpty()) {
            c.message += " " + tr("Before that: %1.").arg(problems.join("; "));
        }
        return c;
    }

    // The same file, copied into the session's folder by the save: every
    // sweep where it was, to the frame
    const LatencyCheck::TakeSummary &r = m_rejudged;
    QString offsetsAfter = tr("the same");
    bool same = (r.events.size() == s.events.size());
    for (size_t i = 0; same && i < s.events.size(); ++i) {
        const LatencyCheck::EventResult &a = s.events[i];
        const LatencyCheck::EventResult &b = r.events[i];
        if (a.event != b.event || a.arrival.found != b.arrival.found ||
            a.arrival.errorFrames != b.arrival.errorFrames) {
            same = false;
            offsetsAfter = tr("the sweep at %1 s: %2, %3 before the save")
                .arg(secondsText(a.expectedSeconds))
                .arg(b.arrival.found ? signedMs(b.arrival.errorSeconds)
                     : tr("not found"))
                .arg(a.arrival.found ? signedMs(a.arrival.errorSeconds)
                     : tr("not found"));
        }
    }
    if (r.events.size() != s.events.size()) {
        offsetsAfter = tr("%1 sweeps judged, %2 before the save")
            .arg(r.events.size()).arg(s.events.size());
    }
    if (!same) problems << tr("the take judged again after reopening differs");
    c.numbers.push_back({ tr("offsets after reopening"), offsetsAfter });

    const sv_samplerate_t rate = m_fresh.referenceRate;
    QString pitch, notes;
    if (!sameEvents(m_pitchBefore, m_pitchAfter, rate, tr("pitch events"),
                    pitch)) {
        problems << tr("the take's pitch changed after reopening");
    }
    if (!sameEvents(m_notesBefore, m_notesAfter, rate, tr("notes"), notes)) {
        problems << tr("the take's notes changed after reopening");
    }
    if (m_pitchBefore.empty()) {
        problems << tr("the take had no pitch track to compare");
    }
    c.numbers.push_back({ tr("pitch after reopening"), pitch });
    c.numbers.push_back({ tr("notes after reopening"), notes });

    if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Every sweep landed within %1 of where the reference "
                       "has it, and the session saved and opened again "
                       "gives the same offsets, pitch and notes.")
            .arg(unsignedMs(kPlacementSeconds));
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
    }
    return c;
}

CheckResult
DevChecks::phrasesCheck(QString reason) const
{
    CheckResult c;
    c.item = 2;
    c.name = "several_phrases_in_one_take";

    if (!m_haveFresh) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // Each punch-in in the take where it was recorded, placed right, and
    // with the start gap of its own stream measured
    const LatencyCheck::TakeSummary &s = m_fresh.summary;
    const sv_samplerate_t rate = m_fresh.referenceRate;
    QStringList problems;
    for (int i = 0; i < int(s.punchIns.size()); ++i) {
        const LatencyCheck::PunchInResult &p = s.punchIns[i];

        // As the runner selected it, in whole frames of the session
        const sv_frame_t start = sv_frame_t(std::llround(p.range.start * rate));
        const sv_frame_t end = sv_frame_t(std::llround(p.range.end * rate));
        Coverage::Range held;
        if (!m_coverageAfterFresh.getRangeAt(start, held) ||
            held.start > start || held.end < end) {
            problems << tr("punch-in %1 is not all in the take").arg(i + 1);
        }

        if (p.found == 0) {
            problems << tr("punch-in %1: no sweep found").arg(i + 1);
        } else if (std::fabs(p.medianOffset) > kPlacementSeconds) {
            problems << tr("punch-in %1 landed %2 off").arg(i + 1)
                .arg(signedMs(p.medianOffset));
        }

        const TakeLatency t = (i < int(m_fresh.takes.size()) ?
                               m_fresh.takes[i] : TakeLatency());
        if (!t.startGapMeasured) {
            problems << tr("punch-in %1's start gap was not measured")
                .arg(i + 1);
        }

        c.numbers.push_back
            ({ tr("punch-in %1, median offset").arg(i + 1),
               p.found > 0 ? signedMs(p.medianOffset) : tr("nothing found") });
        c.numbers.push_back
            ({ tr("punch-in %1, start gap").arg(i + 1),
               tr("%1 (%2 frames), %3")
               .arg(unsignedMs(t.recordingSeconds(t.startGap)))
               .arg(t.startGap)
               .arg(t.startGapMeasured ? tr("measured") :
                    tr("estimated only")) });
    }
    if (s.punchIns.size() < 2) {
        problems << tr("%1 punch-ins, not several").arg(s.punchIns.size());
    }

    if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Every punch-in is in the take, placed within %1, "
                       "with a start gap measured for its own stream.")
            .arg(unsignedMs(kPlacementSeconds));
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
    }
    return c;
}

CheckResult
DevChecks::liveDotsCheck(QString reason) const
{
    CheckResult c;
    c.item = 3;
    c.name = "live_dots";

    if (!m_haveFresh) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // Every dot where the take's pitch track will have the sound it
    // came from: on one of the reference's tones, and at its pitch.  The
    // loopback records the reference, so that is where the singing is.
    //
    // A dot is drawn at the middle of the tracker's window, but YIN
    // hears mostly the window's first half, so a dot comes up to half a
    // window after the sound that made it, and not before it: on the
    // loopback fake a tone's dots begin about 540 frames into it and
    // end up to 440 past it.  So a sound's dots lie from its start to
    // half a window past its end, give or take kDotHops.  The end of
    // each sweep, near 8 kHz, makes a dot or two at a subharmonic just
    // under the tracker's 1 kHz ceiling: dots of the sweeps are counted
    // apart, and their pitch means nothing
    const sv_samplerate_t rate = m_fresh.referenceRate;
    const LatencyCheck::Layout &layout = m_layout;
    const double before =
        double(kDotHops * RealtimePitchTracker::kHopSize) / rate;
    const double after = before +
        double(RealtimePitchTracker::kWindowSize / 2) / rate;
    const double sweepSeconds =
        double(LatencyCheck::sweep(layout.rate).size()) / layout.rate;
    const LatencyCheck::TakeSummary &s = m_fresh.summary;

    QStringList problems;
    vector<double> behind;
    for (int i = 0; i < int(s.punchIns.size()); ++i) {
        const LatencyCheck::PunchInResult &p = s.punchIns[i];
        const Watched *w = freshWatched(i);
        if (!w) {
            problems << tr("punch-in %1 was not watched").arg(i + 1);
            continue;
        }

        const vector<TakeObserver::Dot> &dots = w->seen.dots;
        int onSweeps = 0;
        int off = 0;
        QString firstOff;
        for (const TakeObserver::Dot &d : dots) {
            // Behind the cursor: the cursor had got this far past the
            // dot's place when the dot was first there
            behind.push_back(double(d.playbackFrame - d.frame) / rate);

            const double t = double(d.frame) / rate;
            bool on = false;
            bool sweep = false;
            QString why = tr("on no tone");
            for (const LatencyCheck::Event &e : layout.events) {
                const double sweepAt = double(e.sweepStart) / layout.rate;
                if (t >= sweepAt - before &&
                    t <= sweepAt + sweepSeconds + after) {
                    sweep = true;
                    break;
                }
                const double from = double(e.toneStart) / layout.rate;
                const double to =
                    double(e.toneStart + e.toneLength) / layout.rate;
                if (t < from - before || t > to + after) continue;
                const double cents =
                    1200.0 * std::log2(double(d.hz) / e.toneHz);
                on = (std::fabs(cents) <= kDotCents);
                why = tr("%1 cents from the tone of %2 Hz")
                    .arg(cents, 0, 'f', 0).arg(e.toneHz);
                break;
            }
            if (sweep) ++onSweeps;
            if (on || sweep) continue;
            if (off++ == 0) {
                firstOff = tr("the first at %1 s, %2 Hz, %3")
                    .arg(t, 0, 'f', 3).arg(d.hz, 0, 'f', 1).arg(why);
            }
        }

        if (int(dots.size()) <= kMinDots) {
            problems << tr("punch-in %1 drew %2 live dots").arg(i + 1)
                .arg(dots.size());
        }
        if (off > 0) {
            problems << tr("punch-in %1: %2 of its %3 dots are off the "
                           "reference's sounds, %4")
                .arg(i + 1).arg(off).arg(dots.size()).arg(firstOff);
        }
        c.numbers.push_back
            ({ tr("dots, punch-in %1 (%2 to %3 s)").arg(i + 1)
               .arg(secondsText(p.range.start))
               .arg(secondsText(p.range.end)),
               tr("%1: %2 on the tones, %3 on the sweeps, %4 elsewhere")
               .arg(dots.size()).arg(int(dots.size()) - onSweeps - off)
               .arg(onSweeps).arg(off) });
    }
    if (s.punchIns.empty()) problems << tr("no punch-in was judged");

    // Checklist item 8, and the "cursor versus dots" risk: the cursor
    // runs with what has been recorded, the dots with the round trip
    if (!behind.empty()) {
        const auto range = std::minmax_element(behind.begin(), behind.end());
        c.numbers.push_back({ tr("dots behind the cursor, median"),
                              signedMs(median(behind)) });
        c.numbers.push_back({ tr("dots behind the cursor, spread"),
                              unsignedMs(*range.second - *range.first) });
    }

    if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Every punch-in drew more than %1 live dots, each on "
                       "one of the reference's sounds (from %2 before it "
                       "to %3 after it), and those on its tones within %4 "
                       "cents of their pitch.")
            .arg(kMinDots).arg(unsignedMs(before)).arg(unsignedMs(after))
            .arg(kDotCents);
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
    }
    return c;
}

CheckResult
DevChecks::speakersCheck(QString reason) const
{
    CheckResult c;
    c.item = 4;
    c.name = "nothing_of_the_take_in_the_speakers";

    if (!m_haveFresh) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    QStringList problems;

    // The input played back out, by Tony or by the system, reaches the
    // mic again a little later: every sweep arrives twice
    const LatencyCheck::Echo &echo = m_fresh.summary.echo;
    if (echo.heard) {
        problems << tr("every sweep arrived a second time, %1 later at "
                       "%2 dB: the input is being played back out, by "
                       "Tony or by the system (\"Listen to this device\")")
            .arg(unsignedMs(echo.delaySeconds))
            .arg(echo.levelDb, 0, 'f', 1);
    }
    c.numbers.push_back
        ({ tr("second arrival"),
           echo.heard ? tr("%1 after the sweep, %2 dB, in %3 sweeps")
           .arg(unsignedMs(echo.delaySeconds)).arg(echo.levelDb, 0, 'f', 1)
           .arg(echo.events) : tr("none heard") });

    // What Tony played: exactly nothing where the reference is silent,
    // so no take and no synth.  The reference's sounds, in seconds
    const LatencyCheck::Layout &layout = m_layout;
    const double sweepSeconds =
        double(LatencyCheck::sweep(layout.rate).size()) / layout.rate;
    vector<std::pair<double, double>> sounds;
    for (const LatencyCheck::Event &e : layout.events) {
        const double sweepAt = double(e.sweepStart) / layout.rate;
        sounds.push_back({ sweepAt, sweepAt + sweepSeconds });
        sounds.push_back({ double(e.toneStart) / layout.rate,
                           double(e.toneStart + e.toneLength) / layout.rate });
    }
    auto silent = [&sounds](double from, double to) {
        for (const auto &sound : sounds) {
            if (from < sound.second && to > sound.first) return false;
        }
        return true;
    };

    const sv_samplerate_t rate = m_fresh.referenceRate;
    const LatencyCheck::TakeSummary &s = m_fresh.summary;
    double loudest = 0.0;
    double loudestInGaps = 0.0;
    double margin = 0.0;
    int gapPolls = 0;
    QString firstHeard;
    for (int i = 0; i < int(s.punchIns.size()); ++i) {
        const Watched *w = freshWatched(i);
        if (!w) {
            problems << tr("punch-in %1 was not watched").arg(i + 1);
            continue;
        }
        const TakeObserver::Observation &o = w->seen;

        // Play Singing Audio: what it said before the take, it says
        // after, and the take is heard or not as it says
        if (!o.sawAfter) {
            problems << tr("punch-in %1 was not seen after it stopped")
                .arg(i + 1);
        } else {
            auto onOff = [](bool on) { return on ? tr("on") : tr("off"); };
            if (o.singingAudioAfter != o.singingAudioBefore) {
                problems << tr("Play Singing Audio was %1 before punch-in "
                               "%2 and %3 after it")
                    .arg(onOff(o.singingAudioBefore)).arg(i + 1)
                    .arg(onOff(o.singingAudioAfter));
            }
            if (o.takeAudibleAfter != o.singingAudioAfter) {
                problems << tr("after punch-in %1 Play Singing Audio is %2, "
                               "but the take's audio is %3")
                    .arg(i + 1).arg(onOff(o.singingAudioAfter))
                    .arg(o.takeAudibleAfter ? tr("heard") : tr("silent"));
            }
            c.numbers.push_back
                ({ tr("Play Singing Audio, punch-in %1").arg(i + 1),
                   tr("%1 before, %2 after, the take %3")
                   .arg(onOff(o.singingAudioBefore))
                   .arg(onOff(o.singingAudioAfter))
                   .arg(o.takeAudibleAfter ? tr("heard") : tr("silent")) });
        }

        // An audio callback takes in a block of input, then hands out a
        // block of output.  The reference's first block went out from
        // where playback started, after the start gap's input, so the
        // frames received say which frames of the reference went out
        const TakeLatency t = (i < int(m_fresh.takes.size()) ?
                               m_fresh.takes[i] : TakeLatency());
        if (!t.startGapMeasured || t.recordingRate <= 0) {
            problems << tr("punch-in %1's start gap was not measured, so "
                           "what it played could not be placed").arg(i + 1);
            continue;
        }
        const double start = double(o.playbackStart) / rate;
        auto played = [&](sv_frame_t received) {
            return start + double(received - t.startGap) / t.recordingRate;
        };

        // The levels read at a poll are of the blocks handed out since
        // the read before.  Those were taken in after the frames counted
        // just before that read, but for the one block being handled
        // then, which went out after it: one block early.  And one late,
        // for a resampler between the play source and a device at
        // another rate.  The output latency plays no part: the levels
        // are of what was handed to the device, not of what was heard.
        // How long a block is, nothing in the window says (the play
        // source is never told the device's), and PortAudio may vary
        // it; but each block's input is counted at once, so no block is
        // longer than the most frames that came in between two looks.
        // Not counting looks held up: the first ones of a take wait for
        // the window to set it up, and would count several blocks
        sv_frame_t blockFrames = 0;
        sv_frame_t heldUp = 0;
        const TakeObserver::Sample *previous = nullptr;
        for (const TakeObserver::Sample &sample : o.samples) {
            if (!sample.recording) continue;
            blockFrames = std::max(blockFrames, sample.framesAfter -
                                   sample.framesBefore);
            if (previous) {
                const sv_frame_t since =
                    sample.framesBefore - previous->framesAfter;
                if (sample.ms - previous->ms <= 2 * TakeObserver::kPollMs) {
                    blockFrames = std::max(blockFrames, since);
                } else {
                    heldUp = std::max(heldUp, since);
                }
            }
            previous = &sample;
        }
        if (blockFrames <= 0) blockFrames = heldUp;
        const double block = double(blockFrames) / t.recordingRate;
        margin = std::max(margin, block);
        for (size_t k = 1; k < o.samples.size(); ++k) {
            const TakeObserver::Sample &a = o.samples[k - 1];
            const TakeObserver::Sample &b = o.samples[k];
            if (!a.outputRead || !b.outputRead) continue;
            const double level = std::max(b.outputLeft, b.outputRight);
            loudest = std::max(loudest, level);
            const double from = played(a.framesBefore) - block;
            const double to = played(b.framesAfter) + block;
            if (from < start || !silent(from, to)) continue;
            ++gapPolls;
            if (level > loudestInGaps) loudestInGaps = level;
            if (level > 0.0 && firstHeard == "") {
                firstHeard = tr("%1 from %2 to %3 s, in punch-in %4")
                    .arg(levelText(level)).arg(from, 0, 'f', 3)
                    .arg(to, 0, 'f', 3).arg(i + 1);
            }
        }
    }
    if (s.punchIns.empty()) problems << tr("no punch-in was judged");

    if (loudest <= 0.0) {
        problems << tr("no output level was reported while the reference "
                       "played, so the silent gaps say nothing");
    } else if (gapPolls == 0) {
        problems << tr("no look at the output fell wholly in one of the "
                       "reference's silent gaps");
    }
    if (firstHeard != "") {
        problems << tr("Tony played something where the reference is "
                       "silent: %1").arg(firstHeard);
    }
    c.numbers.push_back({ tr("largest output level in the silent gaps"),
                          tr("%1, over %2 looks").arg(levelText(loudestInGaps))
                          .arg(gapPolls) });
    c.numbers.push_back({ tr("largest output level"), levelText(loudest) });
    c.numbers.push_back({ tr("margin either side of a look"),
                          unsignedMs(margin) });

    if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("No sweep arrived twice, Tony played nothing where the "
                       "reference is silent, and Play Singing Audio was as "
                       "it had been after each take.");
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
    }
    return c;
}

CheckResult
DevChecks::micChannelCheck(QString reason) const
{
    CheckResult c;
    c.item = 5;
    c.name = "mic_on_input_2";

    if (!m_haveFresh) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // Each channel the device delivered, as it delivered it
    const LatencyCheck::TakeSummary &s = m_fresh.summary;
    QStringList problems;
    vector<float> loudest;
    for (int i = 0; i < int(s.punchIns.size()); ++i) {
        const Watched *w = freshWatched(i);
        if (!w) {
            problems << tr("punch-in %1 was not watched").arg(i + 1);
            continue;
        }
        if (w->channelError != "") {
            problems << tr("punch-in %1: %2").arg(i + 1).arg(w->channelError);
            continue;
        }
        QStringList peaks;
        for (size_t ch = 0; ch < w->channelPeaks.size(); ++ch) {
            if (loudest.size() <= ch) loudest.resize(ch + 1, 0.f);
            loudest[ch] = std::max(loudest[ch], w->channelPeaks[ch]);
            peaks << tr("input %1 %2").arg(ch + 1)
                .arg(levelText(w->channelPeaks[ch]));
        }
        c.numbers.push_back({ tr("input peaks, punch-in %1").arg(i + 1),
                              peaks.join(", ") });
    }

    float top = 0.f;
    for (float peak : loudest) top = std::max(top, peak);
    vector<int> carrying;
    for (size_t ch = 0; ch < loudest.size(); ++ch) {
        if (top > 0.f && loudest[ch] > 0.f &&
            20.0 * std::log10(loudest[ch] / top) >= -kMicChannelDb) {
            carrying.push_back(int(ch));
        }
    }
    c.numbers.push_back({ tr("the mic is on"),
                          tr("%1, peak %2").arg(inputsText(carrying))
                          .arg(levelText(top)) });

    if (problems.isEmpty() && carrying.empty()) {
        problems << tr("nothing was recorded on any input");
    }
    if (!problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
        return c;
    }

    if (carrying != vector<int>{ 1 }) {
        c.verdict = CheckResult::Verdict::Measured;
        c.message = tr("Not applicable here: the mic is on %1, not on "
                       "input 2 alone.").arg(inputsText(carrying));
        return c;
    }

    // On input 2 alone: the live tracker hears the mixdown, so the dots
    // are drawn all the same
    for (int i = 0; i < int(s.punchIns.size()); ++i) {
        const Watched *w = freshWatched(i);
        const int dots = w ? int(w->seen.dots.size()) : 0;
        if (dots <= kMinDots) {
            problems << tr("punch-in %1 drew %2 live dots").arg(i + 1)
                .arg(dots);
        }
    }
    if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("The mic is on input 2, and every punch-in drew more "
                       "than %1 live dots.").arg(kMinDots);
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = tr("The mic is on input 2, and %1.")
            .arg(problems.join("; "));
    }
    return c;
}

QString
DevChecks::writeReport(const DevReport &report) const
{
    const QString directory = reportDirectory(m_options.reportDirectory);
    if (!QDir().mkpath(directory)) {
        cerr << "DevChecks: the report directory " << directory
             << " could not be made" << endl;
        return "";
    }
    const QString path = QDir(directory).filePath(kReportFileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate |
                   QIODevice::Text)) {
        cerr << "DevChecks: the report " << path << " could not be written"
             << endl;
        return "";
    }

    auto device = [](QString name) {
        return name == "" ? QString("(System Default)") : name;
    };

    QTextStream out(&file);
    out << "Tony development checks\n";
    out << "Run: " << m_startedAt.toString(Qt::ISODate) << "\n";
    out << "Output device: " << device(m_devices.playbackDevice) << "\n";
    out << "Input device: " << device(m_devices.recordDevice) << "\n";

    // What the device says of itself, as the window has it now.  The
    // output latency counts frames at the rate the play source was told
    // the device runs at, else the recording's (MainWindow::roundTripAt())
    QStringList drivers;
    for (const std::string &name :
             breakfastquay::AudioFactory::getImplementationNames()) {
        drivers << QString::fromStdString(name);
    }
    out << "Audio drivers built in: " << drivers.join(", ") << "\n";
    const sv_samplerate_t recordingRate = m_fresh.recordingRate;
    sv_samplerate_t outputRate = m_window->m_playSource ?
        m_window->m_playSource->getDeviceSampleRate() : 0;
    if (outputRate <= 0) outputRate = recordingRate;
    auto latency = [](qint64 frames, sv_samplerate_t rate) {
        if (rate <= 0) return QString("%1 frames").arg(frames);
        return QString("%1 frames (%2)").arg(frames)
            .arg(unsignedMs(double(frames) / rate));
    };
    out << "Playback latency reported: "
        << (m_window->m_playSource ?
            latency(m_window->m_playSource->getTargetPlayLatency(),
                    outputRate) : QString("no device")) << "\n";
    out << "Record latency reported: "
        << (m_window->m_recordTarget ?
            latency(m_window->m_recordTarget->getSystemRecordLatency(),
                    recordingRate > 0 ? recordingRate : outputRate)
            : QString("no device")) << "\n";
    out << "Round trip for the run: "
        << (m_options.roundTrip >= 0.0 ? unsignedMs(m_options.roundTrip)
            : QString("the one takes are placed with")) << "\n";
    out << "Scratch folder: " << m_scratchFolder << "\n";
    out << "Session: " << (report.sessionPath != "" ? report.sessionPath
                           : QString("not saved")) << "\n";
    if (report.failure != "") {
        out << "The run ended early: " << report.failure << "\n";
    }

    // Grouped by checklist item, in the order of the checklist
    vector<CheckResult> checks = report.checks;
    std::stable_sort(checks.begin(), checks.end(),
                     [](const CheckResult &a, const CheckResult &b) {
                         return a.item < b.item;
                     });
    int item = -1;
    for (const CheckResult &c : checks) {
        if (c.item != item) {
            item = c.item;
            out << "\nItem " << item << "\n";
        }
        out << "  " << c.name << ": "
            << CheckResult::verdictName(c.verdict).toUpper() << "\n";
        if (c.message != "") out << "    " << c.message << "\n";
        for (const auto &n : c.numbers) {
            out << "    " << n.first << ": " << n.second << "\n";
        }
    }

    out << "\nTotals: " << report.count(CheckResult::Verdict::Pass)
        << " passed, " << report.count(CheckResult::Verdict::Fail)
        << " failed, " << report.count(CheckResult::Verdict::Measured)
        << " measured, " << report.count(CheckResult::Verdict::Skipped)
        << " skipped\n";
    out.flush();
    file.close();

    return QFileInfo(path).absoluteFilePath();
}

QString
DevChecks::reportDirectory(QString given)
{
    if (given != "") return given;
    const QString logs = qEnvironmentVariable("TONY_TEST_LOG_DIR");
    if (logs != "") return logs;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString
DevChecks::scratchDirectory(QString given)
{
    if (given != "") return given;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

#endif
