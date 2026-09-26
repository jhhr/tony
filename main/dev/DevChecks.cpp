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

#include "../AudioDriverMenus.h"
#include "../MainWindow.h"
#include "../RealtimePitchTracker.h"
#include "../SingingTakes.h"
#include "../TakeDiff.h"
#include "../TakeTiming.h"

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
#include <limits>
#include <iostream>

using std::cerr;
using std::endl;
using std::vector;

using namespace sv;

namespace {

// Signed, to a tenth of a millisecond: the tolerance is 6 ms
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

// How many frames an audio file holds, or -1 and why not
sv_frame_t
frameCount(QString path, QString &error)
{
    error = "";
    if (path == "") {
        error = DevChecks::tr("its raw recording was not seen");
        return -1;
    }
    FileSource source(path);
    WavFileReader reader(source);
    if (!reader.isOK()) {
        error = DevChecks::tr("its raw recording \"%1\" could not be read: %2")
            .arg(path).arg(reader.getError());
        return -1;
    }
    return reader.getFrameCount();
}

// A time on the reference's timeline in whole frames, as the runner
// selected its punch-ins
sv_frame_t
frameAt(double seconds, sv_samplerate_t rate)
{
    return sv_frame_t(std::llround(seconds * rate));
}

QString
rangeText(double from, double to)
{
    return DevChecks::tr("%1 to %2 s").arg(secondsText(from))
        .arg(secondsText(to));
}

QString
coverageText(const Coverage &coverage, sv_samplerate_t rate)
{
    QStringList ranges;
    for (const Coverage::Range &r : coverage.getRanges()) {
        ranges << rangeText(double(r.start) / rate, double(r.end) / rate);
    }
    return ranges.isEmpty() ? DevChecks::tr("nothing") : ranges.join(", ");
}

// Where a run's judged sweeps landed, in words, and a problem for each
// one not found or further off than items 1 and 2 allow
QString
offsetsOf(const LatencyCheck::TakeSummary &s, QStringList &problems)
{
    QStringList offsets;
    for (const LatencyCheck::EventResult &e : s.events) {
        if (!e.arrival.found) {
            offsets << DevChecks::tr("not found");
            problems << DevChecks::tr("the sweep at %1 s was not found")
                .arg(secondsText(e.expectedSeconds));
            continue;
        }
        offsets << signedMs(e.arrival.errorSeconds);
        if (std::fabs(e.arrival.errorSeconds) > DevChecks::kPlacementSeconds) {
            problems << DevChecks::tr("the sweep at %1 s landed %2 off")
                .arg(secondsText(e.expectedSeconds))
                .arg(signedMs(e.arrival.errorSeconds));
        }
    }
    if (s.events.empty()) problems << DevChecks::tr("no sweep was judged");
    return offsets.isEmpty() ? DevChecks::tr("none judged") : offsets.join(", ");
}

// What TakeDiff::audioOutside() found, in words
QString
audioText(const TakeDiff::AudioDiff &d, sv_samplerate_t rate)
{
    if (d.pass) return DevChecks::tr("the same, bit for bit");
    return DevChecks::tr("%1 frames differ, the first at %2 s, by up to %3")
        .arg(d.differences)
        .arg(double(d.firstDifference) / rate, 0, 'f', 3)
        .arg(levelText(d.largestDifference));
}

// How many of the events lie wholly outside the window, as
// TakeDiff::eventsOutside() counts them: what it compared
int
countOutside(const EventVector &events, const Coverage::Range &window)
{
    int n = 0;
    for (const Event &e : events) {
        const sv_frame_t end = e.getFrame() +
            std::max(e.getDuration(), sv_frame_t(1));
        if (end <= window.start || e.getFrame() >= window.end) ++n;
    }
    return n;
}

// What TakeDiff::eventsOutside() found, in words: where it looked is
// "where", and the events before the change that lie there
QString
eventsText(const TakeDiff::EventDiff &d, const EventVector &before,
           QString what, QString where, sv_samplerate_t rate)
{
    const int compared = countOutside(before, d.window);
    if (d.pass) {
        return DevChecks::tr("%1 %2 %3, unchanged").arg(compared).arg(what)
            .arg(where);
    }
    return DevChecks::tr("%1 %2 %3: %4 added, %5 removed, %6 changed, the "
                         "first at %7 s")
        .arg(compared).arg(what).arg(where).arg(d.added.size())
        .arg(d.removed.size()).arg(d.changed.size())
        .arg(double(d.firstDifference) / rate, 0, 'f', 3);
}

// The number of seconds a status text counts down, if it is the lead-in's
// countdown as TakeTiming words it; 0 if it is anything else
int
countdownOf(QString status)
{
    const QRegularExpressionMatch m =
        QRegularExpression("[0-9]+").match(status);
    if (!m.hasMatch()) return 0;
    const int n = m.captured(0).toInt();
    TakeTiming timing;
    timing.rate = 1;
    timing.position = n;
    timing.preRoll = n;
    return timing.countdownText(0) == status ? n : 0;
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
DevChecks::longPunchIns(const LatencyCheck::Layout &layout)
{
    // All that judgeTake() reads for a sweep, and the little more that
    // LatencyCheck::punchInsFor() leaves
    const double before = LatencyCheck::kSearchSeconds +
        LatencyCheck::kJudgeMarginSeconds + LatencyCheck::kPunchInSlackSeconds;
    const double after = LatencyCheck::kSearchSeconds +
        LatencyCheck::kSweepSeconds + LatencyCheck::kJudgeMarginSeconds +
        LatencyCheck::kPunchInSlackSeconds;
    if (layout.rate <= 0) return {};
    const double length = double(layout.length) / layout.rate;

    vector<LatencyCheck::PunchIn> punchIns;
    for (double share : { 0.25, 0.625 }) {
        for (const LatencyCheck::Event &e : layout.events) {
            const double at = double(e.sweepStart) / layout.rate;
            if (at < share * length) continue;
            punchIns.push_back(LatencyCheck::PunchIn(at - before, at + after));
            break;
        }
    }
    if (punchIns.size() != 2 || punchIns[0].end > punchIns[1].start ||
        punchIns[1].end > length) {
        return {};
    }
    return punchIns;
}

vector<LatencyCheck::PunchIn>
DevChecks::freshPunchIns()
{
    return { LatencyCheck::PunchIn(6.3, 10.2),
             LatencyCheck::PunchIn(16.8, 21.2) };
}

vector<LatencyCheck::PunchIn>
DevChecks::joinPunchIns()
{
    return { LatencyCheck::PunchIn(26.0, 28.7),
             LatencyCheck::PunchIn(28.7, 32.0) };
}

LatencyCheck::PunchIn
DevChecks::reRecording()
{
    return LatencyCheck::PunchIn(19.2, 21.2);
}

LatencyCheck::PunchIn
DevChecks::nearTheStart()
{
    return LatencyCheck::PunchIn(1.0, 4.2);
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
    // As the Preferences name them, or the route a phone has open
    m_devices = m_window->latencyKey(0);

    m_layout = LatencyCheck::devLayout();
    m_observedPunchIn = 0;
    m_watched.clear();
    m_long = LongSong();
    m_long.layout = LatencyCheck::longLayout(m_layout.rate,
                                             m_options.longSeconds);
    m_haveFresh = false;
    m_fresh = AudioCheckResult();
    m_coverageAfterFresh = Coverage();
    m_freshWatched.clear();
    m_reRecord = PunchInStage();
    m_nearStart = PunchInStage();
    m_joins = PunchInStage();
    m_pitchBefore.clear();
    m_notesBefore.clear();
    m_pitchAfter.clear();
    m_notesAfter.clear();
    m_reopened = false;
    m_beforeSave = LatencyCheck::TakeSummary();
    m_rejudged = LatencyCheck::TakeSummary();
    m_runnerRunning = false;
    m_runnerResult = AudioCheckResult();

    m_stages.clear();
    if (m_options.longSeconds > 0.0) {
        m_stages.push_back({ tr("Long song"),
                             [this]() { beginLongSong(); },
                             [this]() { return longSongDone(); },
                             kCheckStageTimeoutMs });
    }
    m_stages.push_back({ tr("Fresh punch-ins"),
                         [this]() { beginFreshPunchIns(); },
                         [this]() { return freshPunchInsDone(); },
                         kCheckStageTimeoutMs });
    m_stages.push_back({ tr("Re-record"),
                         [this]() {
                             beginPunchInStage
                                 (m_reRecord, { reRecording() },
                                  kReRecordPreRollSeconds);
                         },
                         [this]() { return punchInStageDone(m_reRecord); },
                         kCheckStageTimeoutMs });
    m_stages.push_back({ tr("Pre-roll near the start"),
                         [this]() {
                             beginPunchInStage
                                 (m_nearStart, { nearTheStart() },
                                  kNearStartPreRollSeconds);
                         },
                         [this]() { return punchInStageDone(m_nearStart); },
                         kCheckStageTimeoutMs });
    m_stages.push_back({ tr("Joins"),
                         [this]() {
                             beginPunchInStage
                                 (m_joins, joinPunchIns(),
                                  AudioCheckRunner::kPreRollSeconds);
                         },
                         [this]() { return punchInStageDone(m_joins); },
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
    if (m_long.timing) longSongStep(AudioCheckRunner::Step::Idle, 0);
    if (m_observer->isObserving()) finishObservation();
    m_runnerRunning = false;
    m_runnerResult = result;
}

void
DevChecks::runnerProgress(const AudioCheckRunner::Progress &state)
{
    if (!m_runnerRunning) return;
    if (m_long.timing) longSongStep(state.step, state.punchIn);

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

void
DevChecks::longSongStep(AudioCheckRunner::Step step, int punchIn)
{
    // Reported again as the seconds left go down
    LongSong &s = m_long;
    if (step == s.step && punchIn == s.punchIn) return;

    // Each step ends as the next begins: the whole song's analysis as
    // the first punch-in starts to record, and a punch-in's analysis as
    // the next starts or the runner ends.  The whole song's is timed from
    // its session open, as a song opened by the user is analysed, and a
    // punch-in's from the take stopped: the steps the audio check waits
    using Step = AudioCheckRunner::Step;
    const qint64 now = s.clock.elapsed();
    const double seconds = double(now - s.stepFrom) / 1000.0;
    if (s.step == Step::AnalysingReference) s.wholeSongSeconds = seconds;
    if (s.step == Step::AnalysingTake) s.stopSeconds.push_back(seconds);

    // The take's pitch before each punch-in, which is the pitch after the
    // one before it.  Starting a take hides the pitch but leaves its model
    if (step == Step::Recording) {
        s.pitch.push_back(takeEvents(Analyser::PitchTrack));
    }

    if (s.step == Step::OpeningReference) {
        cerr << "DevChecks: the long song was written and opened in "
             << seconds << " s" << endl;
    } else if (s.step == Step::AnalysingReference) {
        cerr << "DevChecks: the long song was analysed in " << seconds
             << " s" << endl;
    } else if (s.step == Step::AnalysingTake) {
        cerr << "DevChecks: punch-in " << s.punchIn << " into the long song "
             << "was analysed in " << seconds << " s" << endl;
    }

    s.step = step;
    s.punchIn = punchIn;
    s.stepFrom = now;
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
DevChecks::beginLongSong()
{
    AudioCheckRunner::Plan plan;
    plan.layout = m_long.layout;
    plan.ranges = longPunchIns(m_long.layout);
    plan.roundTrip = m_options.roundTrip;

    m_watched.clear();
    m_observedPunchIn = 0;
    m_runnerResult = AudioCheckResult();
    m_long.timing = true;
    m_long.clock.start();
    m_runnerRunning = m_runner && m_runner->start(plan);
    if (!m_runnerRunning) {
        m_long.timing = false;
        end(tr("The audio check could not start on a long song of %1 s.")
            .arg(m_options.longSeconds));
    }
}

bool
DevChecks::longSongDone()
{
    if (m_runnerRunning) return false;
    m_long.timing = false;

    if (m_runnerResult.failure != "") {
        end(tr("The punch-ins into the long song could not be recorded: %1")
            .arg(m_runnerResult.failure));
        return false;
    }

    // The runner has waited for the last punch-in's analysis
    m_long.result = m_runnerResult;
    m_long.coverage = m_window->m_takes->getCoverage();
    m_long.watched = m_watched;
    m_long.pitch.push_back(takeEvents(Analyser::PitchTrack));
    m_long.done = true;
    return true;
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
DevChecks::beginPunchInStage(PunchInStage &stage,
                             vector<LatencyCheck::PunchIn> ranges,
                             double preRoll)
{
    // The take as the stages before left it, to compare with what these
    // punch-ins leave
    stage.before = snapshot();

    AudioCheckRunner::Plan plan;
    plan.layout = m_layout;
    plan.ranges = ranges;
    plan.keepSession = true;
    plan.roundTrip = m_options.roundTrip;
    plan.preRoll = preRoll;

    m_watched.clear();
    m_observedPunchIn = 0;
    m_runnerResult = AudioCheckResult();
    m_runnerRunning = m_runner && m_runner->start(plan);
    if (!m_runnerRunning) {
        end(tr("The audio check could not start."));
    }
}

bool
DevChecks::punchInStageDone(PunchInStage &stage)
{
    if (m_runnerRunning) return false;

    if (m_runnerResult.failure != "") {
        end(tr("The punch-in could not be recorded: %1")
            .arg(m_runnerResult.failure));
        return false;
    }

    // The runner has waited for the take's analysis: its pitch and notes
    // are those this punch-in leaves
    stage.result = m_runnerResult;
    stage.after = snapshot();
    stage.watched = m_watched;

    // The lead-in the window gave the take, which is the last so far,
    // and all that the device delivered until the take stopped
    stage.preRoll = m_window->m_takePreRoll;
    stage.recorded = frameCount
        (stage.watched.empty() ? QString() :
         stage.watched.front().seen.recordingPath, stage.recordedError);

    stage.done = true;
    return true;
}

DevChecks::Snapshot
DevChecks::snapshot() const
{
    Snapshot s;
    sv_samplerate_t rate = 0;
    s.audioError = AudioCheckRunner::readTakeFile
        (m_window->m_takes->getAudioPath(), s.audio, rate);
    s.pitch = takeEvents(Analyser::PitchTrack);
    s.notes = takeEvents(Analyser::Notes);
    s.coverage = m_window->m_takes->getCoverage();
    return s;
}

vector<DevChecks::Run>
DevChecks::runs() const
{
    vector<Run> all;
    if (m_long.done) {
        all.push_back({ &m_long.layout, &m_long.result, &m_long.coverage,
                        &m_long.watched });
    }
    if (m_haveFresh) {
        all.push_back({ &m_layout, &m_fresh, &m_coverageAfterFresh,
                        &m_freshWatched });
    }
    for (const PunchInStage *stage : { &m_reRecord, &m_nearStart, &m_joins }) {
        if (stage->done) {
            all.push_back({ &m_layout, &stage->result, &stage->after.coverage,
                            &stage->watched });
        }
    }
    return all;
}

const DevChecks::Watched *
DevChecks::watchedOf(const Run &run, int i)
{
    for (const Watched &w : *run.watched) {
        if (w.punchIn == i) return &w;
    }
    return nullptr;
}

vector<LatencyCheck::PunchIn>
DevChecks::punchInsSoFar() const
{
    // Not the long song's: that was another session, and another take
    vector<LatencyCheck::PunchIn> ranges;
    for (const Run &run : runs()) {
        if (run.layout != &m_layout) continue;
        for (const LatencyCheck::PunchInResult &p :
                 run.result->summary.punchIns) {
            ranges.push_back(p.range);
        }
    }
    return ranges;
}

void
DevChecks::beginReopen()
{
    // Read from the models before the save, and again from the models
    // there are after the reopen
    m_pitchBefore = takeEvents(Analyser::PitchTrack);
    m_notesBefore = takeEvents(Analyser::Notes);

    // And the take's file judged over every punch-in so far, as it will
    // be again after the reopen.  Not the runs' own judgements: a later
    // punch-in over an earlier one's sweep has replaced it
    {
        vector<float> mono;
        sv_samplerate_t rate = 0;
        const QString error = AudioCheckRunner::readTakeFile
            (m_window->m_takes->getAudioPath(), mono, rate);
        if (error != "") {
            end(error);
            return;
        }
        m_beforeSave = LatencyCheck::judgeTake
            (m_layout, mono.data(), sv_frame_t(mono.size()), rate,
             punchInsSoFar());
    }

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
    m_rejudged = LatencyCheck::judgeTake
        (m_layout, mono.data(), sv_frame_t(mono.size()), rate,
         punchInsSoFar());
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
    m_long.timing = false;
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
             micChannelCheck(reason), positionCheck(reason),
             longSongCheck(reason), joinsCheck(reason),
             leadInCheck(reason), nearStartCheck(reason),
             stopsItselfCheck(reason) };
}

CheckResult
DevChecks::latencyCheck(QString reason) const
{
    CheckResult c;
    c.item = 1;
    c.name = "latency_on_this_machine";

    const vector<Run> all = runs();
    if (all.empty()) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // Every judged sweep of every punch-in of every run, found and within
    // the tolerance; the punch-ins numbered along the run
    QStringList problems;
    double largest = 0.0;
    int n = 0;
    for (const Run &run : all) {
        const LatencyCheck::TakeSummary &s = run.result->summary;
        for (int i = 0; i < int(s.punchIns.size()); ++i) {
            const LatencyCheck::PunchInResult &p = s.punchIns[i];
            ++n;
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
                problems << tr("punch-in %1 judged no sweep").arg(n);
            }
            c.numbers.push_back
                ({ tr("offsets, punch-in %1 (%2)").arg(n)
                   .arg(rangeText(p.range.start, p.range.end)),
                   offsets.isEmpty() ? tr("none judged") :
                   offsets.join(", ") });
        }
    }
    if (n == 0) problems << tr("no punch-in was judged");
    c.numbers.push_back({ tr("largest offset"), signedMs(largest) });
    c.numbers.push_back({ tr("round trip used"),
                          unsignedMs(all.front().result->usedRoundTrip) });

    if (!m_reopened) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        if (!problems.isEmpty()) {
            c.message += " " + tr("Before that: %1.").arg(problems.join("; "));
        }
        return c;
    }

    // The same file, copied into the session's folder by the save: every
    // sweep where it was just before the save, to the frame
    const LatencyCheck::TakeSummary &s = m_beforeSave;
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
    // With no pitch in the take, as when a phone's speaker plays none of
    // the tones, there is nothing to compare, and the part is not judged
    // rather than failed
    QString notJudged;
    if (m_pitchBefore.empty() && m_pitchAfter.empty()) {
        notJudged = tr("Its pitch and notes after reopening were not judged: "
                       "the take had no pitch to compare.");
    }
    c.numbers.push_back({ tr("pitch after reopening"), pitch });
    c.numbers.push_back({ tr("notes after reopening"), notes });

    if (problems.isEmpty() && notJudged == "") {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Every sweep landed within %1 of where the reference "
                       "has it, and the session saved and opened again "
                       "gives the same offsets, pitch and notes.")
            .arg(unsignedMs(kPlacementSeconds));
    } else if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Every sweep landed within %1 of where the reference "
                       "has it, and the session saved and opened again "
                       "gives the same offsets. %2")
            .arg(unsignedMs(kPlacementSeconds)).arg(notJudged);
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
        if (notJudged != "") c.message += " " + notJudged;
    }
    return c;
}

CheckResult
DevChecks::phrasesCheck(QString reason) const
{
    CheckResult c;
    c.item = 2;
    c.name = "several_phrases_in_one_take";

    if (runs().empty()) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // Each punch-in of every run in the take where it was recorded, as
    // the take was straight after its run, placed right, and with the
    // start gap of its own stream measured
    QStringList problems;
    int n = 0;
    for (const Run &run : runs()) {
        const LatencyCheck::TakeSummary &s = run.result->summary;
        const sv_samplerate_t rate = run.result->referenceRate;
        for (int i = 0; i < int(s.punchIns.size()); ++i) {
            const LatencyCheck::PunchInResult &p = s.punchIns[i];
            ++n;

            // As the runner selected it, in whole frames of the session
            const sv_frame_t start = frameAt(p.range.start, rate);
            const sv_frame_t end = frameAt(p.range.end, rate);
            Coverage::Range held;
            if (!run.coverage->getRangeAt(start, held) ||
                held.start > start || held.end < end) {
                problems << tr("punch-in %1 is not all in the take").arg(n);
            }

            if (p.found == 0) {
                problems << tr("punch-in %1: no sweep found").arg(n);
            } else if (std::fabs(p.medianOffset) > kPlacementSeconds) {
                problems << tr("punch-in %1 landed %2 off").arg(n)
                    .arg(signedMs(p.medianOffset));
            }

            const TakeLatency t = (i < int(run.result->takes.size()) ?
                                   run.result->takes[i] : TakeLatency());
            if (!t.startGapMeasured) {
                problems << tr("punch-in %1's start gap was not measured")
                    .arg(n);
            }

            c.numbers.push_back
                ({ tr("punch-in %1, median offset").arg(n),
                   p.found > 0 ? signedMs(p.medianOffset) :
                   tr("nothing found") });
            c.numbers.push_back
                ({ tr("punch-in %1, start gap").arg(n),
                   tr("%1 (%2 frames), %3")
                   .arg(unsignedMs(t.recordingSeconds(t.startGap)))
                   .arg(t.startGap)
                   .arg(t.startGapMeasured ? tr("measured") :
                        tr("estimated only")) });
        }
    }
    if (n < 2) problems << tr("%1 punch-ins, not several").arg(n);

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
    // Where a sound's dots may lie, and which of them are counted apart
    // and not judged (on the sweeps, and at the start of a tone or of
    // the punch-in or at a tone's end), TakeDiff::placeLiveDot() says
    const sv_samplerate_t rate = m_fresh.referenceRate;
    const LatencyCheck::Layout &layout = m_layout;
    const TakeDiff::DotReach reach = TakeDiff::dotReach(layout.rate);
    const LatencyCheck::TakeSummary &s = m_fresh.summary;

    QStringList problems;
    vector<double> behind;
    int atEdges = 0;
    for (int i = 0; i < int(s.punchIns.size()); ++i) {
        const LatencyCheck::PunchInResult &p = s.punchIns[i];
        const Watched *w = freshWatched(i);
        if (!w) {
            problems << tr("punch-in %1 was not watched").arg(i + 1);
            continue;
        }

        const vector<TakeObserver::Dot> &dots = w->seen.dots;
        int onSweeps = 0;
        int edges = 0;
        int off = 0;
        QString firstOff;
        for (const TakeObserver::Dot &d : dots) {
            // Behind the cursor: the cursor had got this far past the
            // dot's place when the dot was first there
            behind.push_back(double(d.playbackFrame - d.frame) / rate);

            const double t = double(d.frame) / rate;
            const TakeDiff::LiveDot dot =
                TakeDiff::placeLiveDot(layout, p.range.start, t, d.hz);
            if (dot.place == TakeDiff::DotPlace::OnPitch) continue;
            if (dot.place == TakeDiff::DotPlace::OnSweep) {
                ++onSweeps;
                continue;
            }
            if (dot.place == TakeDiff::DotPlace::AtEdge) {
                ++edges;
                continue;
            }
            if (off++ == 0) {
                const QString why =
                    dot.place == TakeDiff::DotPlace::OffPitch ?
                    tr("%1 cents from the tone of %2 Hz")
                    .arg(dot.cents, 0, 'f', 0).arg(dot.toneHz) :
                    tr("on no tone");
                firstOff = tr("the first at %1 s, %2 Hz, %3")
                    .arg(t, 0, 'f', 3).arg(d.hz, 0, 'f', 1).arg(why);
            }
        }
        atEdges += edges;

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
               tr("%1: %2 on the tones, %3 at edges, %4 on the sweeps, %5 "
                  "elsewhere")
               .arg(dots.size())
               .arg(int(dots.size()) - onSweeps - edges - off)
               .arg(edges).arg(onSweeps).arg(off) });
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
            .arg(kMinDots).arg(unsignedMs(reach.before))
            .arg(unsignedMs(reach.after)).arg(TakeDiff::kDotCents);
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
    }
    // A window that straddles a start or a tone's end wanders off pitch
    // through a real speaker and mic, so what the dots there did is only
    // told
    if (atEdges > 0) {
        c.message += tr(" %1 dots within %2 after the start of a tone or "
                        "of a punch-in, or within %3 either side of a "
                        "tone's end, were not judged.")
            .arg(atEdges).arg(unsignedMs(reach.onset))
            .arg(unsignedMs(reach.end));
    }
    return c;
}

DevChecks::GapLooks
DevChecks::gapLooks(const LatencyCheck::Layout &layout,
                    const TakeObserver::Observation &o,
                    const TakeLatency &t, sv_samplerate_t rate,
                    double until) const
{
    GapLooks g;

    // An audio callback takes in a block of input, then hands out a
    // block of output.  The reference's first block went out from
    // where playback started, after the start gap's input, so the
    // frames received say which frames of the reference went out
    if (!t.startGapMeasured || t.recordingRate <= 0 || rate <= 0) return g;
    g.placed = true;

    // The reference's sounds, in seconds
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
    g.margin = block;

    for (size_t k = 1; k < o.samples.size(); ++k) {
        const TakeObserver::Sample &a = o.samples[k - 1];
        const TakeObserver::Sample &b = o.samples[k];
        if (!a.outputRead || !b.outputRead) continue;
        const double level = std::max(b.outputLeft, b.outputRight);
        g.loudest = std::max(g.loudest, level);
        const double from = played(a.framesBefore) - block;
        const double to = played(b.framesAfter) + block;
        if (from < until) {
            g.longestWait = std::max(g.longestWait, (b.ms - a.ms) / 1000.0);
        }
        if (from < start || to > until || !silent(from, to)) continue;
        ++g.looks;
        g.loudestInGaps = std::max(g.loudestInGaps, level);
        if (level > 0.0 && g.heard <= 0.0) {
            g.heard = level;
            g.heardFrom = from;
            g.heardTo = to;
        }
    }
    return g;
}

CheckResult
DevChecks::speakersCheck(QString reason) const
{
    CheckResult c;
    c.item = 4;
    c.name = "nothing_of_the_take_in_the_speakers";

    const vector<Run> all = runs();
    if (all.empty()) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    QStringList problems;

    // The input played back out, by Tony or by the system, reaches the
    // mic again a little later: every sweep arrives twice.  In any run
    const LatencyCheck::Echo *echo = &all.front().result->summary.echo;
    for (const Run &run : all) {
        if (run.result->summary.echo.heard) {
            echo = &run.result->summary.echo;
            break;
        }
    }
    if (echo->heard) {
        problems << tr("every sweep arrived a second time, %1 later at "
                       "%2 dB: the input is being played back out, by "
                       "Tony or by the system (\"Listen to this device\")")
            .arg(unsignedMs(echo->delaySeconds))
            .arg(echo->levelDb, 0, 'f', 1);
    }
    c.numbers.push_back
        ({ tr("second arrival"),
           echo->heard ? tr("%1 after the sweep, %2 dB, in %3 sweeps")
           .arg(unsignedMs(echo->delaySeconds)).arg(echo->levelDb, 0, 'f', 1)
           .arg(echo->events) : tr("none heard") });

    // What Tony played, at every punch-in of every run: exactly nothing
    // where the reference is silent, so no take and no synth
    double loudest = 0.0;
    double loudestInGaps = 0.0;
    double margin = 0.0;
    double longestWait = 0.0;
    int gapPolls = 0;
    QString firstHeard;
    int n = 0;
    for (const Run &run : all) {
        const LatencyCheck::TakeSummary &s = run.result->summary;
        for (int i = 0; i < int(s.punchIns.size()); ++i) {
            ++n;
            const Watched *w = watchedOf(run, i);
            if (!w) {
                problems << tr("punch-in %1 was not watched").arg(n);
                continue;
            }
            const TakeObserver::Observation &o = w->seen;

            // Play Singing Audio: what it said before the take, it says
            // after, and the take is heard or not as it says
            if (!o.sawAfter) {
                problems << tr("punch-in %1 was not seen after it stopped")
                    .arg(n);
            } else {
                auto onOff = [](bool on) { return on ? tr("on") : tr("off"); };
                if (o.singingAudioAfter != o.singingAudioBefore) {
                    problems << tr("Play Singing Audio was %1 before "
                                   "punch-in %2 and %3 after it")
                        .arg(onOff(o.singingAudioBefore)).arg(n)
                        .arg(onOff(o.singingAudioAfter));
                }
                if (o.takeAudibleAfter != o.singingAudioAfter) {
                    problems << tr("after punch-in %1 Play Singing Audio is "
                                   "%2, but the take's audio is %3")
                        .arg(n).arg(onOff(o.singingAudioAfter))
                        .arg(o.takeAudibleAfter ? tr("heard") : tr("silent"));
                }
                c.numbers.push_back
                    ({ tr("Play Singing Audio, punch-in %1").arg(n),
                       tr("%1 before, %2 after, the take %3")
                       .arg(onOff(o.singingAudioBefore))
                       .arg(onOff(o.singingAudioAfter))
                       .arg(o.takeAudibleAfter ? tr("heard") :
                            tr("silent")) });
            }

            const TakeLatency t = (i < int(run.result->takes.size()) ?
                                   run.result->takes[i] : TakeLatency());
            const GapLooks g = gapLooks(*run.layout, o, t,
                                        run.result->referenceRate,
                                        std::numeric_limits<double>::max());
            if (!g.placed) {
                problems << tr("punch-in %1's start gap was not measured, "
                               "so what it played could not be placed")
                    .arg(n);
                continue;
            }
            loudest = std::max(loudest, g.loudest);
            loudestInGaps = std::max(loudestInGaps, g.loudestInGaps);
            margin = std::max(margin, g.margin);
            longestWait = std::max(longestWait, g.longestWait);
            gapPolls += g.looks;
            if (g.heard > 0.0 && firstHeard == "") {
                firstHeard = tr("%1 from %2 to %3 s, in punch-in %4")
                    .arg(levelText(g.heard)).arg(g.heardFrom, 0, 'f', 3)
                    .arg(g.heardTo, 0, 'f', 3).arg(n);
            }
        }
    }
    if (n == 0) problems << tr("no punch-in was judged");

    // With no look in any gap, the take played out would not have shown:
    // that part is not judged, rather than failed
    QString notJudged;
    if (loudest <= 0.0) {
        problems << tr("no output level was reported while the reference "
                       "played, so the silent gaps say nothing");
    } else if (gapPolls == 0) {
        notJudged = tr("What Tony played was not judged: no look at the "
                       "output lay wholly in one of the reference's silent "
                       "gaps, where the take played out would show (the "
                       "longest wait between two looks was %1, and a look "
                       "reaches %2 either side).")
            .arg(unsignedMs(longestWait)).arg(unsignedMs(margin));
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

    if (problems.isEmpty() && notJudged == "") {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("No sweep arrived twice, Tony played nothing where the "
                       "reference is silent, and Play Singing Audio was as "
                       "it had been after each take.");
    } else if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("No sweep arrived twice, and Play Singing Audio was as "
                       "it had been after each take. %1").arg(notJudged);
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
        if (notJudged != "") c.message += " " + notJudged;
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

    // A phone's microphone, which OboeAudioIO opens as the one channel
    // it records: there is no other input it could be on
    if (loudest.size() == 1) {
        c.verdict = CheckResult::Verdict::Measured;
        c.message = tr("Not applicable here: the device records one input "
                       "channel.");
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

CheckResult
DevChecks::positionCheck(QString reason) const
{
    CheckResult c;
    c.item = 7;
    c.name = "record_from_a_position";

    const PunchInStage &stage = m_reRecord;
    const LatencyCheck::TakeSummary &s = stage.result.summary;
    if (!stage.done || s.punchIns.empty()) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // Recorded over the end of an earlier punch-in: placed as items 1 and
    // 2 ask, and outside the range selected the take's audio is what it
    // was, to the bit, and its pitch and notes are, beyond the margin a
    // ranged analysis may change.  The range is the selection: what the
    // splice placed, if the take ran to its end (item 14), and nothing
    // the splice wrote past it is excused
    const sv_samplerate_t rate = stage.result.referenceRate;
    const LatencyCheck::PunchIn &p = s.punchIns[0].range;
    const Coverage::Range range(frameAt(p.start, rate), frameAt(p.end, rate));
    QStringList problems;
    c.numbers.push_back({ tr("range"), rangeText(p.start, p.end) });
    c.numbers.push_back({ tr("offsets"), offsetsOf(s, problems) });

    const Snapshot &before = stage.before;
    const Snapshot &after = stage.after;
    if (before.audioError != "" || after.audioError != "") {
        problems << (before.audioError != "" ? before.audioError :
                     after.audioError);
    } else {
        const TakeDiff::AudioDiff audio = TakeDiff::audioOutside
            (before.audio.data(), sv_frame_t(before.audio.size()),
             after.audio.data(), sv_frame_t(after.audio.size()), 1, range);
        if (!audio.pass) {
            problems << tr("the take's audio outside the range changed");
        }
        c.numbers.push_back({ tr("audio outside the range"),
                              audioText(audio, rate) });
    }

    const TakeDiff::EventDiff pitch =
        TakeDiff::eventsOutside(before.pitch, after.pitch, range, rate);
    const TakeDiff::EventDiff notes =
        TakeDiff::eventsOutside(before.notes, after.notes, range, rate);
    const QString where = tr("outside %1")
        .arg(rangeText(double(pitch.window.start) / rate,
                       double(pitch.window.end) / rate));
    if (!pitch.pass) problems << tr("the take's pitch outside the range "
                                    "changed");
    if (!notes.pass) problems << tr("the take's notes outside the range "
                                    "changed");
    // With no pitch there, as when a phone's speaker plays none of the
    // tones, there is nothing to compare: not judged, rather than failed
    QString notJudged;
    if (countOutside(before.pitch, pitch.window) == 0 && pitch.pass) {
        notJudged = tr("Its pitch and notes outside the range were not "
                       "judged: the take had no pitch there to compare.");
    }
    c.numbers.push_back({ tr("pitch"), eventsText(pitch, before.pitch,
                                                  tr("pitch events"), where,
                                                  rate) });
    c.numbers.push_back({ tr("notes"), eventsText(notes, before.notes,
                                                  tr("notes"), where, rate) });

    if (problems.isEmpty() && notJudged == "") {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Recorded over part of an earlier punch-in, the take "
                       "was placed within %1, and outside the range its "
                       "audio is the same bit for bit, and its pitch and "
                       "notes beyond %2 s of it.")
            .arg(unsignedMs(kPlacementSeconds))
            .arg(TakeDiff::kEventMarginSeconds);
    } else if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Recorded over part of an earlier punch-in, the take "
                       "was placed within %1, and outside the range its "
                       "audio is the same bit for bit. %2")
            .arg(unsignedMs(kPlacementSeconds)).arg(notJudged);
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
        if (notJudged != "") c.message += " " + notJudged;
    }
    return c;
}

CheckResult
DevChecks::longSongCheck(QString reason) const
{
    CheckResult c;
    c.item = 9;
    c.name = "stop_on_a_long_song";

    const LongSong &s = m_long;
    const LatencyCheck::TakeSummary &summary = s.result.summary;
    if (!s.done || summary.punchIns.empty()) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = (m_options.longSeconds > 0.0 ? reason :
                     tr("The long song was left out of this run."));
        return c;
    }

    // Stop analyses only the range recorded, and merges it in: far
    // quicker than the whole song's analysis, and nothing of the take's
    // pitch outside the range changes, beyond the margin the merge may
    // touch.  The second punch-in has the first's pitch to leave alone,
    // which a whole-take analysis would make again
    const sv_samplerate_t rate = s.result.referenceRate;
    const double whole = s.wholeSongSeconds;
    QStringList problems;
    c.numbers.push_back
        ({ tr("whole-song analysis"),
           whole < 0.0 ? tr("not timed") :
           tr("%1 s, of a song of %2 s, from its session open")
           .arg(secondsText(whole))
           .arg(secondsText(double(s.layout.length) / s.layout.rate)) });
    if (whole < 0.0) problems << tr("the whole song's analysis was not timed");

    int compared = 0;
    bool allSeenAlike = true;
    for (int i = 0; i < int(summary.punchIns.size()); ++i) {
        const LatencyCheck::PunchIn &p = summary.punchIns[i].range;
        const QString range = rangeText(p.start, p.end);
        if (i >= int(s.stopSeconds.size())) {
            problems << tr("punch-in %1's analysis was not timed").arg(i + 1);
        } else {
            const double stop = s.stopSeconds[i];
            c.numbers.push_back
                ({ tr("Stop to pitch merged, punch-in %1 (%2)").arg(i + 1)
                   .arg(range),
                   whole > 0.0 ?
                   tr("%1 s, %2 per cent of the whole song's")
                   .arg(secondsText(stop)).arg(100.0 * stop / whole, 0, 'f', 0)
                   : tr("%1 s").arg(secondsText(stop)) });
            if (whole >= 0.0 && !(stop < kStopShare * whole)) {
                problems << tr("punch-in %1 took %2 s from Stop to its pitch "
                               "merged, not under %3 of the whole song's "
                               "analysis, %4 s")
                    .arg(i + 1).arg(secondsText(stop)).arg(kStopShare)
                    .arg(secondsText(whole));
            }
        }

        if (i + 1 >= int(s.pitch.size())) {
            problems << tr("the take's pitch around punch-in %1 was not seen")
                .arg(i + 1);
            allSeenAlike = false;
            continue;
        }
        const EventVector &before = s.pitch[i];
        const EventVector &after = s.pitch[i + 1];
        const Coverage::Range r(frameAt(p.start, rate), frameAt(p.end, rate));
        const TakeDiff::EventDiff pitch =
            TakeDiff::eventsOutside(before, after, r, rate);
        compared += countOutside(before, pitch.window);
        if (!pitch.pass) {
            problems << tr("punch-in %1 changed the take's pitch outside its "
                           "range").arg(i + 1);
            allSeenAlike = false;
        }
        c.numbers.push_back
            ({ tr("pitch, punch-in %1").arg(i + 1),
               eventsText(pitch, before, tr("pitch events"),
                          tr("outside %1")
                          .arg(rangeText(double(pitch.window.start) / rate,
                                         double(pitch.window.end) / rate)),
                          rate) });
    }
    // With no pitch there, as when a phone's speaker plays none of the
    // tones, there is nothing to compare: not judged, rather than failed.
    // A comparison that failed, or could not be made, is a problem above
    QString notJudged;
    if (compared == 0 && allSeenAlike) {
        notJudged = tr("The take's pitch outside the punch-ins was not "
                       "judged: it had none there to compare.");
    }

    if (problems.isEmpty() && notJudged == "") {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("On a song of %1 s, each punch-in had its pitch merged "
                       "in under %2 of the time the whole song's analysis "
                       "took, and left the take's pitch beyond %3 s of its "
                       "range as it was: only the range was analysed.")
            .arg(secondsText(double(s.layout.length) / s.layout.rate))
            .arg(kStopShare).arg(TakeDiff::kEventMarginSeconds);
    } else if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("On a song of %1 s, each punch-in had its analysis "
                       "merged in under %2 of the time the whole song's "
                       "analysis took. %3")
            .arg(secondsText(double(s.layout.length) / s.layout.rate))
            .arg(kStopShare).arg(notJudged);
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
        if (notJudged != "") c.message += " " + notJudged;
    }
    return c;
}

CheckResult
DevChecks::joinsCheck(QString reason) const
{
    CheckResult c;
    c.item = 10;
    c.name = "the_joins";

    const PunchInStage &stage = m_joins;
    const LatencyCheck::TakeSummary &s = stage.result.summary;
    if (!stage.done || s.punchIns.size() < 2) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // Two punch-ins that meet in the middle of a held tone, as a singer
    // punching in twice within one note: at the join the samples run on
    // without a step, the pitch track without a hole or a frame twice,
    // one note runs through it, and outside the two ranges the take's
    // pitch and notes are as they were.  Each part is named when it
    // fails.  Two punch-ins placed differently, as on a device whose
    // offset moves when its stream restarts, do not show in the step:
    // each splice fades against the silence the file held there, so the
    // join is a dip of a few ms that hides a jump of phase.  Hence the
    // number "second punch-in against the first", which does
    const sv_samplerate_t rate = stage.result.referenceRate;
    const LatencyCheck::PunchIn &first = s.punchIns[0].range;
    const LatencyCheck::PunchIn &second = s.punchIns[1].range;
    const sv_frame_t join = frameAt(first.end, rate);
    auto at = [rate](sv_frame_t frame) {
        return QString("%1 s").arg(double(frame) / rate, 0, 'f', 3);
    };
    QStringList problems;
    c.numbers.push_back({ tr("join"),
                          tr("%1 s, between %2 and %3")
                          .arg(secondsText(first.end))
                          .arg(rangeText(first.start, first.end))
                          .arg(rangeText(second.start, second.end)) });

    // Where each landed, for reading the rest: items 1 and 2 judge it
    QStringList placing;
    c.numbers.push_back({ tr("offsets"), offsetsOf(s, placing) });
    if (s.punchIns[0].found > 0 && s.punchIns[1].found > 0) {
        c.numbers.push_back({ tr("second punch-in against the first"),
                              signedMs(s.punchIns[1].medianOffset -
                                       s.punchIns[0].medianOffset) });
    }

    const Snapshot &before = stage.before;
    const Snapshot &after = stage.after;
    if (after.audioError != "") {
        problems << tr("step: %1").arg(after.audioError);
    } else {
        const TakeDiff::SampleStep step = TakeDiff::stepAt
            (after.audio.data(), sv_frame_t(after.audio.size()), 1, rate,
             join);
        c.numbers.push_back
            ({ tr("step at the join"),
               tr("%1 dB, at most %2: the largest first difference %3 at %4, "
                  "the typical one %5")
               .arg(step.stepDb, 0, 'f', 1).arg(TakeDiff::kMaxStepDb)
               .arg(levelText(step.largest)).arg(at(step.largestAt))
               .arg(levelText(step.typical)) });
        if (!step.pass) {
            problems << tr("step: the samples jump at the join, %1 dB over "
                           "their typical step, more than %2 dB")
                .arg(step.stepDb, 0, 'f', 1).arg(TakeDiff::kMaxStepDb);
        }
    }

    const TakeDiff::PitchJoin pitch =
        TakeDiff::pitchAcross(after.pitch, join, rate);
    c.numbers.push_back
        ({ tr("pitch across the join"),
           tr("%1 events within %2 s of it; the largest gap %3 (%4 hops) "
              "from %5; %6 frames twice, %7 out of order")
           .arg(pitch.events).arg(TakeDiff::kPitchWindowSeconds)
           .arg(unsignedMs(double(pitch.largestGap) / rate))
           .arg(double(pitch.largestGap) / double(TakeDiff::kHopFrames), 0,
                'f', 1)
           .arg(at(pitch.largestGapFrom)).arg(pitch.doubled)
           .arg(pitch.outOfOrder) });
    // With no pitch near the join, as when a phone's speaker plays none
    // of the tones, the pitch and note parts have nothing to judge: not
    // judged, rather than failed.  A track with a gap is judged
    QStringList notJudged;
    const bool pitchNear = (pitch.events > 0);
    if (!pitchNear) {
        notJudged << tr("pitch: not judged, there was no pitch within %1 s "
                        "of the join")
            .arg(TakeDiff::kPitchWindowSeconds);
    } else if (!pitch.pass) {
        QStringList why;
        if (pitch.largestGap > TakeDiff::kMaxGapHops * TakeDiff::kHopFrames) {
            why << tr("a gap of %1 from %2")
                .arg(unsignedMs(double(pitch.largestGap) / rate))
                .arg(at(pitch.largestGapFrom));
        }
        if (pitch.doubled > 0) {
            why << tr("%1 frames twice, the first at %2").arg(pitch.doubled)
                .arg(at(pitch.firstDoubled));
        }
        if (pitch.outOfOrder > 0) {
            why << tr("%1 events out of order, the first at %2")
                .arg(pitch.outOfOrder).arg(at(pitch.firstOutOfOrder));
        }
        problems << tr("pitch: the track does not run through the join: %1")
            .arg(why.join(", "));
    }

    // And every note within a second of it, to show how a note that does
    // not run through came apart
    const TakeDiff::NoteJoin notes =
        TakeDiff::notesAcross(after.notes, join, rate);
    auto noteText = [rate](const Event &e) {
        return rangeText(double(e.getFrame()) / rate,
                         double(e.getFrame() + e.getDuration()) / rate);
    };
    QStringList spanning, close, around;
    for (const Event &e : notes.spanning) spanning << noteText(e);
    for (const Event &e : notes.edgesNear) close << noteText(e);
    const sv_frame_t oneSecond = frameAt(1.0, rate);
    for (const Event &e : after.notes) {
        if (e.getFrame() < join + oneSecond &&
            e.getFrame() + e.getDuration() > join - oneSecond) {
            around << noteText(e);
        }
    }
    c.numbers.push_back({ tr("notes at the join"),
                          spanning.isEmpty() ? tr("none") :
                          spanning.join(", ") });
    c.numbers.push_back({ tr("nearest note edge"),
                          after.notes.empty() ? tr("no notes") :
                          signedMs(double(notes.nearestEdge) / rate) });
    c.numbers.push_back({ tr("notes within 1 s of the join"),
                          around.isEmpty() ? tr("none") : around.join(", ") });
    const bool notesNear = pitchNear || !around.isEmpty();
    if (!notesNear) {
        notJudged << tr("note: not judged, there was no note within 1 s of "
                        "the join, nor any pitch");
    } else if (!notes.pass) {
        QStringList why;
        if (notes.spanning.size() != 1) {
            why << tr("%1 notes hold the join, not one")
                .arg(notes.spanning.size());
        }
        if (!close.isEmpty()) {
            why << tr("a note begins or ends within %1 s of it: %2")
                .arg(TakeDiff::kNoteClearanceSeconds).arg(close.join(", "));
        }
        problems << tr("note: one note does not run through the join: %1")
            .arg(why.join(", and "));
    }

    // Nothing moved outside the two ranges together, beyond the margin
    const Coverage::Range both(frameAt(first.start, rate),
                               frameAt(second.end, rate));
    const TakeDiff::EventDiff pitchOutside =
        TakeDiff::eventsOutside(before.pitch, after.pitch, both, rate);
    const TakeDiff::EventDiff notesOutside =
        TakeDiff::eventsOutside(before.notes, after.notes, both, rate);
    const QString where = tr("outside %1")
        .arg(rangeText(double(pitchOutside.window.start) / rate,
                       double(pitchOutside.window.end) / rate));
    if (!pitchOutside.pass) {
        problems << tr("outside: the take's pitch outside the ranges changed");
    }
    if (!notesOutside.pass) {
        problems << tr("outside: the take's notes outside the ranges changed");
    }
    const bool pitchOutsideJudged =
        (countOutside(before.pitch, pitchOutside.window) > 0 ||
         !pitchOutside.pass);
    if (!pitchOutsideJudged) {
        notJudged << tr("outside: the take's pitch and notes not judged, it "
                        "had no pitch outside the ranges to compare");
    }
    c.numbers.push_back({ tr("pitch outside"),
                          eventsText(pitchOutside, before.pitch,
                                     tr("pitch events"), where, rate) });
    c.numbers.push_back({ tr("notes outside"),
                          eventsText(notesOutside, before.notes, tr("notes"),
                                     where, rate) });

    // What was not judged is said after the rest, in the order of the
    // parts, as the failures are
    const QString unjudged =
        notJudged.isEmpty() ? QString() : notJudged.join("; ") + ".";
    if (problems.isEmpty() && notJudged.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Two punch-ins meeting at %1 s, in the middle of a "
                       "held tone, left no step in the samples there, the "
                       "pitch track running through it, one note across it "
                       "and no note beginning or ending within %2 s of it, "
                       "and the take's pitch and notes beyond %3 s of the "
                       "two ranges as they were.")
            .arg(secondsText(first.end))
            .arg(TakeDiff::kNoteClearanceSeconds)
            .arg(TakeDiff::kEventMarginSeconds);
    } else if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Two punch-ins meeting at %1 s, in the middle of a "
                       "held tone, left no step in the samples there, and "
                       "passed every part that could be judged. %2")
            .arg(secondsText(first.end)).arg(unjudged);
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
        if (unjudged != "") c.message += " " + unjudged;
    }
    return c;
}

CheckResult
DevChecks::leadInCheck(QString reason) const
{
    CheckResult c;
    c.item = 12;
    c.name = "nothing_heard_or_changed_in_the_lead_in";

    const PunchInStage &stage = m_reRecord;
    const LatencyCheck::TakeSummary &s = stage.result.summary;
    if (!stage.done || s.punchIns.empty()) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // The re-recording's lead-in played over what an earlier punch-in
    // recorded before P.  Nothing of that may change: item 7's
    // comparisons, their part before P alone, so that a change there is
    // told apart from one after the range
    const sv_samplerate_t rate = stage.result.referenceRate;
    const LatencyCheck::PunchIn &p = s.punchIns[0].range;
    const Snapshot &before = stage.before;
    const Snapshot &after = stage.after;
    const sv_frame_t from = frameAt(p.start, rate);
    const sv_frame_t beyond = std::max
        (from + 1, sv_frame_t(std::max(before.audio.size(),
                                       after.audio.size())) +
         frameAt(1.0, rate));
    const Coverage::Range rest(from, beyond);
    QStringList problems;

    if (before.audioError != "" || after.audioError != "") {
        problems << (before.audioError != "" ? before.audioError :
                     after.audioError);
    } else {
        const TakeDiff::AudioDiff audio = TakeDiff::audioOutside
            (before.audio.data(), sv_frame_t(before.audio.size()),
             after.audio.data(), sv_frame_t(after.audio.size()), 1, rest);
        if (!audio.pass) {
            problems << tr("the take's audio before the punch-in changed");
        }
        c.numbers.push_back({ tr("audio before %1 s").arg(secondsText(p.start)),
                              audioText(audio, rate) });
    }

    const TakeDiff::EventDiff pitch =
        TakeDiff::eventsOutside(before.pitch, after.pitch, rest, rate);
    const TakeDiff::EventDiff notes =
        TakeDiff::eventsOutside(before.notes, after.notes, rest, rate);
    const QString where = tr("before %1 s")
        .arg(secondsText(double(pitch.window.start) / rate));
    if (!pitch.pass) problems << tr("the take's pitch before the punch-in "
                                    "changed");
    if (!notes.pass) problems << tr("the take's notes before the punch-in "
                                    "changed");
    // With no pitch there, as when a phone's speaker plays none of the
    // tones, there is nothing to compare: not judged, rather than failed
    QString pitchNotJudged;
    if (countOutside(before.pitch, pitch.window) == 0 && pitch.pass) {
        pitchNotJudged = tr("Its pitch and notes before the punch-in were "
                            "not judged: the take had no pitch there to "
                            "compare.");
    }
    c.numbers.push_back({ tr("pitch"), eventsText(pitch, before.pitch,
                                                  tr("pitch events"), where,
                                                  rate) });
    c.numbers.push_back({ tr("notes"), eventsText(notes, before.notes,
                                                  tr("notes"), where, rate) });

    // And while it played, Tony played the reference and nothing else:
    // item 4's looks at the output, those that lie wholly before P.  The
    // take's audio is under them now, and kept silent as in any take.
    // With no look in any gap, the take played out would not have shown:
    // that part is not judged, rather than failed
    const Watched *w = stage.watched.empty() ? nullptr : &stage.watched[0];
    const TakeLatency t = stage.result.takes.empty() ? TakeLatency() :
        stage.result.takes[0];
    QString notJudged;
    if (!w) {
        problems << tr("the punch-in was not watched");
    } else {
        const GapLooks g = gapLooks(m_layout, w->seen, t, rate, p.start);
        if (!g.placed) {
            problems << tr("the start gap was not measured, so what the "
                           "lead-in played could not be placed");
        } else {
            if (g.looks == 0) {
                notJudged = tr("What Tony played during the lead-in was "
                               "not judged: no look at the output lay "
                               "wholly in one of the reference's silent "
                               "gaps, where the take played out would "
                               "show (the longest wait between two looks "
                               "was %1, and a look reaches %2 either "
                               "side).")
                    .arg(unsignedMs(g.longestWait)).arg(unsignedMs(g.margin));
            }
            if (g.heard > 0.0) {
                problems << tr("during the lead-in Tony played something "
                               "where the reference is silent: %1 from %2 "
                               "to %3 s")
                    .arg(levelText(g.heard)).arg(g.heardFrom, 0, 'f', 3)
                    .arg(g.heardTo, 0, 'f', 3);
            }
            c.numbers.push_back
                ({ tr("output in the lead-in's silent gaps"),
                   tr("%1, over %2 looks").arg(levelText(g.loudestInGaps))
                   .arg(g.looks) });
            c.numbers.push_back
                ({ tr("longest wait between two looks in the lead-in"),
                   unsignedMs(g.longestWait) });
        }
    }

    // Either part may have nothing to judge: what was judged is said
    // first, then what was not
    QStringList unjudged;
    if (pitchNotJudged != "") unjudged << pitchNotJudged;
    if (notJudged != "") unjudged << notJudged;
    if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        if (pitchNotJudged == "" && notJudged == "") {
            c.message = tr("Before the punch-in the take's audio is the same "
                           "bit for bit, and its pitch and notes beyond %1 s "
                           "of it; and while the lead-in played over the "
                           "take, Tony played nothing where the reference is "
                           "silent.")
                .arg(TakeDiff::kEventMarginSeconds);
        } else if (pitchNotJudged == "") {
            c.message = tr("Before the punch-in the take's audio is the same "
                           "bit for bit, and its pitch and notes beyond %1 s "
                           "of it.").arg(TakeDiff::kEventMarginSeconds);
        } else if (notJudged == "") {
            c.message = tr("Before the punch-in the take's audio is the same "
                           "bit for bit; and while the lead-in played over "
                           "the take, Tony played nothing where the "
                           "reference is silent.");
        } else {
            c.message = tr("Before the punch-in the take's audio is the same "
                           "bit for bit.");
        }
        c.message += " " + unjudged.join(" ");
        c.message = c.message.trimmed();
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
        if (!unjudged.isEmpty()) c.message += " " + unjudged.join(" ");
    }
    return c;
}

CheckResult
DevChecks::nearStartCheck(QString reason) const
{
    CheckResult c;
    c.item = 13;
    c.name = "pre_roll_near_the_start";

    const PunchInStage &stage = m_nearStart;
    const LatencyCheck::TakeSummary &s = stage.result.summary;
    if (!stage.done || s.punchIns.empty()) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // Less song before the punch-in than the pre-roll asked for: the
    // lead-in is all there is of it, and no more
    const sv_samplerate_t rate = stage.result.referenceRate;
    const LatencyCheck::PunchIn &p = s.punchIns[0].range;
    const double room = std::min(kNearStartPreRollSeconds, p.start);
    QStringList problems;
    c.numbers.push_back({ tr("lead-in"),
                          tr("%1 s, of the %2 s asked for")
                          .arg(secondsText(double(stage.preRoll) / rate))
                          .arg(secondsText(kNearStartPreRollSeconds)) });
    if (stage.preRoll > frameAt(room, rate)) {
        problems << tr("the lead-in was %1 s, longer than the %2 s of song "
                       "before the punch-in")
            .arg(secondsText(double(stage.preRoll) / rate))
            .arg(secondsText(room));
    }

    const Watched *w = stage.watched.empty() ? nullptr : &stage.watched[0];
    const TakeLatency t = stage.result.takes.empty() ? TakeLatency() :
        stage.result.takes[0];
    if (!w) {
        problems << tr("the punch-in was not watched");
    } else {
        const TakeObserver::Observation &o = w->seen;

        // Playback from the start of the song, and the cursor never
        // before it.  The cursor is where playback started plus what has
        // been recorded, so it is read while recording only
        bool seen = false;
        sv_frame_t lowest = 0;
        for (const TakeObserver::Sample &sample : o.samples) {
            if (!sample.recording) continue;
            lowest = seen ? std::min(lowest, sample.playbackFrame) :
                sample.playbackFrame;
            seen = true;
        }
        c.numbers.push_back({ tr("playback from"),
                              tr("%1 s").arg(secondsText
                                             (double(o.playbackStart) /
                                              rate)) });
        c.numbers.push_back({ tr("cursor, lowest"),
                              seen ? tr("%1 s").arg(secondsText
                                                    (double(lowest) / rate))
                              : tr("not seen") });
        if (o.playbackStart != 0) {
            problems << tr("playback started at %1 s, not at the start of "
                           "the song")
                .arg(secondsText(double(o.playbackStart) / rate));
        }
        if (!seen) {
            problems << tr("the take was not seen recording");
        } else if (lowest < 0) {
            problems << tr("the cursor was at %1 s, before the start of the "
                           "song").arg(secondsText(double(lowest) / rate));
        }

        // The countdown as the status bar showed it: in whole seconds,
        // the lead-in and the round trip still to come (the singing that
        // answers the reference at P arrives a round trip later), down
        // to 1.  Before the start gap is measured the window counts with
        // its estimate, which may differ by a block or two: hence the
        // 50 ms
        vector<int> counted;
        for (const TakeObserver::Sample &sample : o.samples) {
            if (!sample.recording) continue;
            const int n = countdownOf(sample.status);
            if (n > 0 && (counted.empty() || counted.back() != n)) {
                counted.push_back(n);
            }
        }
        const double wait = room + (t.recordingRate > 0 ?
                                    double(t.roundTrip + t.startGap) /
                                    t.recordingRate : 0.0);
        const int most = int(std::ceil(wait + 0.05));
        QStringList words;
        for (int n : counted) words << QString::number(n);
        c.numbers.push_back
            ({ tr("countdown"),
               counted.empty() ? tr("none shown") :
               tr("%1; at most %2, for %3 s of lead-in and round trip")
               .arg(words.join(", ")).arg(most).arg(secondsText(wait)) });
        if (counted.empty()) {
            problems << tr("no countdown was shown");
        } else {
            const int highest =
                *std::max_element(counted.begin(), counted.end());
            if (highest > most) {
                problems << tr("the countdown began at %1, not %2: it "
                               "counted more lead-in than there is room for")
                    .arg(highest).arg(most);
            }
            if (counted.back() != 1) {
                problems << tr("the countdown ended at %1, not 1")
                    .arg(counted.back());
            }
        }
    }

    c.numbers.push_back({ tr("offsets"), offsetsOf(s, problems) });

    if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("With a pre-roll of %1 s asked for at %2 s, playback "
                       "ran from the start of the song and not before it, "
                       "the countdown counted only the lead-in there is "
                       "room for, and the take was placed within %3.")
            .arg(secondsText(kNearStartPreRollSeconds))
            .arg(secondsText(p.start)).arg(unsignedMs(kPlacementSeconds));
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
    }
    return c;
}

CheckResult
DevChecks::stopsItselfCheck(QString reason) const
{
    CheckResult c;
    c.item = 14;
    c.name = "record_into_selection_stops_by_itself";

    vector<const PunchInStage *> stages;
    for (const PunchInStage *stage : { &m_reRecord, &m_nearStart }) {
        if (stage->done && !stage->result.summary.punchIns.empty()) {
            stages.push_back(stage);
        }
    }
    if (stages.empty()) {
        c.verdict = CheckResult::Verdict::Skipped;
        c.message = reason;
        return c;
    }

    // The window looks this often whether a take into a selection has
    // all it needs
    const double poll = (m_window->m_takeTimer ?
                         m_window->m_takeTimer->interval() / 1000.0 : 0.0);
    auto seconds = [](double s) { return QString("%1 s").arg(s, 0, 'f', 3); };

    QStringList problems;
    for (const PunchInStage *stage : stages) {
        const LatencyCheck::TakeSummary &s = stage->result.summary;
        const sv_samplerate_t rate = stage->result.referenceRate;
        const LatencyCheck::PunchIn &p = s.punchIns[0].range;
        const sv_frame_t start = frameAt(p.start, rate);
        const sv_frame_t end = frameAt(p.end, rate);
        const QString range = rangeText(p.start, p.end);
        const Watched *w = stage->watched.empty() ? nullptr :
            &stage->watched[0];
        const TakeLatency t = stage->result.takes.empty() ? TakeLatency() :
            stage->result.takes[0];

        // How far past the selection's end the take recorded: its raw
        // recording against what reaches the end, the round trip and the
        // lead-in before the selection.  The take waits for a margin past
        // the end (TakeTiming::shouldStopAt()), and is then stopped by
        // the take timer's next look, with the block coming in as it
        // looks.  Not worked out with TakeTiming, whose margin is part of
        // what is checked.  In seconds: the raw recording, the round trip
        // and the start gap count frames of the recording, at the
        // device's rate; the lead-in and the selection count the
        // reference's
        if (!w) {
            problems << tr("the take at %1 was not watched").arg(range);
        } else if (stage->recorded < 0) {
            problems << tr("the take at %1: %2").arg(range)
                .arg(stage->recordedError);
        } else if (t.recordingRate <= 0 || rate <= 0) {
            problems << tr("the take at %1: its rate is not known")
                .arg(range);
        } else {
            const double needed =
                t.recordingSeconds(t.roundTrip + t.startGap) +
                double(stage->preRoll + (end - start)) / rate;
            const double past = t.recordingSeconds(stage->recorded) - needed;
            const double allowed = kStopMarginSeconds + poll + gapLooks
                (m_layout, w->seen, t, rate,
                 std::numeric_limits<double>::max()).margin;
            c.numbers.push_back
                ({ tr("stopped, %1").arg(range),
                   tr("%1 past the end of the selection, at most %2 allowed")
                   .arg(seconds(past)).arg(seconds(allowed)) });
            if (past < 0.0) {
                problems << tr("the take at %1 stopped %2 before the end of "
                               "its selection had been recorded")
                    .arg(range).arg(seconds(-past));
            } else if (past > allowed) {
                problems << tr("the take at %1 went on %2 past the end of its "
                               "selection, more than %3 s, a look of the "
                               "take timer and a block").arg(range)
                    .arg(seconds(past)).arg(kStopMarginSeconds);
            }
        }

        // The take's coverage as it was, and the selection: no more, and
        // no less
        Coverage expected = stage->before.coverage;
        expected.add(start, end);
        c.numbers.push_back({ tr("coverage after, %1").arg(range),
                              coverageText(stage->after.coverage, rate) });
        if (stage->after.coverage != expected) {
            problems << tr("after the take at %1 the take covers %2, not %3")
                .arg(range).arg(coverageText(stage->after.coverage, rate))
                .arg(coverageText(expected, rate));
        }

        // No question asked, and no dialog of any kind, from the take's
        // start to its analysis done
        if (w) {
            const TakeObserver::Sample *modal = nullptr;
            for (const TakeObserver::Sample &sample : w->seen.samples) {
                if (sample.modal) {
                    modal = &sample;
                    break;
                }
            }
            c.numbers.push_back
                ({ tr("dialogs, %1").arg(range),
                   modal ? tr("one up %1 into the take")
                   .arg(seconds(modal->ms / 1000.0)) :
                   tr("none, over %1 looks").arg(w->seen.samples.size()) });
            if (modal) {
                problems << tr("a dialog was up %1 into the take at %2")
                    .arg(seconds(modal->ms / 1000.0)).arg(range);
            }
        }
    }

    if (problems.isEmpty()) {
        c.verdict = CheckResult::Verdict::Pass;
        c.message = tr("Each take into a selection stopped by itself within "
                       "%1 s of the selection's end, a look of the take "
                       "timer and a block, added the selection to the take "
                       "and nothing else, and no dialog came up.")
            .arg(kStopMarginSeconds);
    } else {
        c.verdict = CheckResult::Verdict::Fail;
        c.message = problems.join("; ") + ".";
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

    // So that runs on two drivers, or at two latencies, can be told apart
    out << "Audio driver: "
        << AudioDriverMenus::driverName(m_devices.implementation) << "\n";
    const double latencyAsked = m_window->m_audioDriverMenus ?
        m_window->m_audioDriverMenus->appliedLatency() : 0.0;
    out << "Latency asked for: "
        << (latencyAsked > 0.0 ? unsignedMs(latencyAsked)
            : QString("none")) << "\n";

    // What the device says of itself, as the window has it now.  The
    // output latency counts frames at the rate the play source was told
    // the device runs at, else the recording's (MainWindow::roundTripAt())
    QStringList drivers;
    for (const std::string &name :
             breakfastquay::AudioFactory::getImplementationNames()) {
        drivers << QString::fromStdString(name);
    }
#ifdef Q_OS_ANDROID
    // Tony's own, which MainWindow::createAudioIO() opens: bqaudioio's
    // factory has none for Android
    drivers << "oboe";
#endif
    out << "Audio drivers built in: " << drivers.join(", ") << "\n";
    const vector<Run> all = runs();

    // A device that reports the route it opened, as OboeAudioIO does on
    // a phone, is none of those: which it is, and how its streams opened
    // for the first punch-in, on which the latencies below depend
    const AudioRoute::Route route =
        all.empty() ? AudioRoute::Route() : all.front().result->route;
    if (route.driver != "") {
        out << "Audio driver in use: " << route.driver << "\n";
        out << "Output streams: " << route.outputStreams << "\n";
        out << "Input streams: " << (route.inputStreams != "" ?
                                     route.inputStreams :
                                     QString("none open")) << "\n";
    }
    const sv_samplerate_t recordingRate =
        all.empty() ? 0 : all.front().result->recordingRate;
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
