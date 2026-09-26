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

#include "CalibrateAudioDialog.h"

#include "AudioCheckIndicator.h"
#include "MainWindow.h"
#include "PopupArea.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWindow>

#ifdef TONY_DEV_CHECKS
#include <QCheckBox>
#include <QFile>
#endif

#ifdef Q_OS_ANDROID
#include "AndroidScreen.h"
#include <QScroller>
#endif

#include <algorithm>
#include <cmath>
#include <iostream>

using std::cerr;
using std::endl;

using namespace sv;

namespace {

using LatencyCheck::Verdict;

// How long a run of the plan takes, roughly: the recording, as the
// runner counts what is still to come, and each analysis waited for
double
expectedSeconds(const AudioCheckRunner::Plan &plan)
{
    const std::vector<LatencyCheck::PunchIn> ranges =
        AudioCheckRunner::punchInsOf(plan);
    double seconds = 0.0;
    for (const LatencyCheck::PunchIn &r : ranges) {
        seconds += std::min(AudioCheckRunner::kPreRollSeconds, r.start) +
            (r.end - r.start);
    }
    return seconds + CalibrateAudioDialog::kSecondsPerAnalysis *
        double(ranges.size() + 1);
}

// How far the offsets disagree, as judgeTake() looks at it for Unsteady
// and Scattered: across punch-ins or within one, whichever is more
double
timingSpread(const LatencyCheck::TakeSummary &s)
{
    double spread = s.spread;
    for (const LatencyCheck::PunchInResult &p : s.punchIns) {
        if (p.found > 0) spread = std::max(spread, p.spread);
    }
    return spread;
}

QString
paragraph(QString html)
{
    return "<p>" + html + "</p>";
}

// Not "bold": the macOS SDK declares an enumerator of that name in the
// global namespace, which makes every unqualified call ambiguous there
QString
boldHtml(QString html)
{
    return "<b>" + html + "</b>";
}

// A page's text, scrolling where it is longer than the dialog can be
// tall.  It asks for the text's height at the width it is given, so that
// the dialog is no taller than its text needs, and for a few lines at
// the least, so that the dialog can be as short as a phone's window
class TextArea : public QScrollArea
{
public:
    TextArea(QLabel *text) {
        setWidget(text);
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
#ifdef Q_OS_ANDROID
        // A finger dragged over the text scrolls it, as on a phone; Qt's
        // own gesture on the viewport wants two
        QScroller::grabGesture(viewport(), QScroller::LeftMouseButtonGesture);
#endif
    }

    bool hasHeightForWidth() const override {
        return true;
    }

    int heightForWidth(int width) const override {
        const int frame = 2 * frameWidth();
        return widget()->heightForWidth(width - frame) + frame;
    }

    QSize minimumSizeHint() const override {
        const QFontMetrics metrics(widget()->font());
        return QSize(metrics.averageCharWidth() * 20,
                     metrics.lineSpacing() * 3 + 2 * frameWidth());
    }
};

}

CalibrateAudioDialog::CalibrateAudioDialog(MainWindow *window,
                                           AudioCheckRunner *runner) :
    QDialog(window),
    m_window(window),
    m_runner(runner),
    m_plan(AudioCheckRunner::calibrationPlan()),
    m_running(false),
    m_latencyKept(false),
    m_expectedSeconds(0),
    m_shownPermille(0),
    m_collapsed(false)
#ifdef TONY_DEV_CHECKS
    ,
    m_devChecksBox(nullptr),
    m_devRunning(false),
    m_haveDevReport(false)
#endif
{
    setWindowTitle(tr("Calibrate Audio"));
    setModal(false);

#ifdef Q_OS_ANDROID
    // A phone's widget font is sized for text read on its own: in a
    // window 400 px high the pages show more of themselves at a little
    // less, and scroll less
    QFont smaller = font();
    if (smaller.pixelSize() > 0) {
        smaller.setPixelSize
            (std::max(10, int(std::lround(smaller.pixelSize() * 0.85))));
    } else if (smaller.pointSizeF() > 0) {
        smaller.setPointSizeF(smaller.pointSizeF() * 0.85);
    }
    setFont(smaller);
    cerr << "CalibrateAudioDialog: text " << QFontInfo(font()).pixelSize()
         << " px, the window's " << QFontInfo(window->font()).pixelSize()
         << " px" << endl;
#endif

    QVBoxLayout *layout = new QVBoxLayout;
    setLayout(layout);

    m_pages = new QStackedWidget;
    layout->addWidget(m_pages, 1);

    auto textLabel = []() {
        QLabel *label = new QLabel;
        label->setWordWrap(true);
        label->setTextFormat(Qt::RichText);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        return label;
    };

    QWidget *instructions = new QWidget;
    QVBoxLayout *instructionsLayout = new QVBoxLayout;
    instructionsLayout->setContentsMargins(0, 0, 0, 0);
    instructions->setLayout(instructionsLayout);
    m_instructions = textLabel();
    m_instructionsArea = new TextArea(m_instructions);
    instructionsLayout->addWidget(m_instructionsArea, 1);
#ifdef TONY_DEV_CHECKS
    m_devChecksBox = new QCheckBox(tr("Run the dev checks after calibrating"));
    m_devChecksBox->setChecked(true);
    instructionsLayout->addWidget(m_devChecksBox);
#endif
    m_pages->addWidget(instructions);

    QWidget *progress = new QWidget;
    QVBoxLayout *progressLayout = new QVBoxLayout;
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progress->setLayout(progressLayout);
    m_step = new QLabel;
    m_step->setWordWrap(true);
    m_bar = new QProgressBar;
    m_bar->setRange(0, 1000);
    m_bar->setTextVisible(false);
    m_timeLeft = new QLabel;
    progressLayout->addWidget(m_step);
    progressLayout->addWidget(m_bar);
    progressLayout->addWidget(m_timeLeft);
    progressLayout->addStretch(1);
    m_pages->addWidget(progress);

    // Selectable, so that the figures can be copied and passed on.  Not
    // on a phone, whose selection cannot be copied (Copy does it), and
    // where a finger dragged over the text scrolls it
    m_resultText = textLabel();
#ifndef Q_OS_ANDROID
    m_resultText->setTextInteractionFlags(Qt::TextSelectableByMouse);
#endif
    m_resultArea = new TextArea(m_resultText);
    m_pages->addWidget(m_resultArea);

    m_buttons = new QHBoxLayout;
    m_useButton = new QPushButton(tr("Use this latency"));
    m_againButton = new QPushButton(tr("Check Again"));
    m_startButton = new QPushButton(tr("Start"));
    m_smallButton = new QPushButton(tr("Make Small"));
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_closeButton = new QPushButton(tr("Close"));
    m_copyButton = new QPushButton(tr("Copy"));
    m_buttons->addWidget(m_useButton);
    m_buttons->addStretch(1);
    m_buttons->addWidget(m_copyButton);
#ifdef Q_OS_ANDROID
    m_saveButton = new QPushButton(tr("Save Report..."));
    m_buttons->addWidget(m_saveButton);
#endif
    m_buttons->addWidget(m_againButton);
    m_buttons->addWidget(m_startButton);
    m_buttons->addWidget(m_smallButton);
    m_buttons->addWidget(m_cancelButton);
    m_buttons->addWidget(m_closeButton);
    layout->addLayout(m_buttons);

#ifdef Q_OS_ANDROID
    // Tall enough for a finger, whatever the font
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const int height =
            PopupArea::fingerWidth(screen->physicalDotsPerInchY()) * 3 / 4;
        for (QPushButton *button : { m_useButton, m_againButton,
                                     m_startButton, m_smallButton,
                                     m_cancelButton, m_closeButton,
                                     m_copyButton, m_saveButton }) {
            button->setMinimumHeight(height);
        }
    }
#endif

    // Put in the window's status bar by the window, and shown there while
    // the check runs with the dialog hidden
    m_indicator = new AudioCheckIndicator;
    m_indicator->hide();
    connect(m_indicator, &AudioCheckIndicator::clicked,
            this, &CalibrateAudioDialog::expand);

    connect(m_startButton, &QPushButton::clicked,
            this, &CalibrateAudioDialog::startCheck);
    connect(m_againButton, &QPushButton::clicked,
            this, &CalibrateAudioDialog::startCheck);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &CalibrateAudioDialog::cancelCheck);
    connect(m_smallButton, &QPushButton::clicked,
            this, &CalibrateAudioDialog::collapse);
    connect(m_useButton, &QPushButton::clicked,
            this, &CalibrateAudioDialog::useLatency);
    connect(m_closeButton, &QPushButton::clicked,
            this, &CalibrateAudioDialog::reject);
    connect(m_copyButton, &QPushButton::clicked,
            this, &CalibrateAudioDialog::copyReport);
#ifdef Q_OS_ANDROID
    connect(m_saveButton, &QPushButton::clicked,
            this, &CalibrateAudioDialog::saveReport);
#endif

    // Direct: the runner's signals carry types with no metatype
    connect(m_runner, &AudioCheckRunner::progress,
            this, &CalibrateAudioDialog::runnerProgress);
    connect(m_runner, &AudioCheckRunner::finished,
            this, &CalibrateAudioDialog::runnerFinished);

    showPage(Page::Instructions);
}

CalibrateAudioDialog::~CalibrateAudioDialog()
{
    // The window's status bar holds it, unless the window is gone first
    delete m_indicator;
}

void
CalibrateAudioDialog::setPlan(const AudioCheckRunner::Plan &plan)
{
    m_plan = plan;
}

#ifdef TONY_DEV_CHECKS

void
CalibrateAudioDialog::setDevChecks(DevChecks *devChecks)
{
    if (m_devChecks) disconnect(m_devChecks, nullptr, this, nullptr);
    m_devChecks = devChecks;
    if (!devChecks) return;

    // Direct, as the runner's: the report has no metatype
    connect(devChecks, &DevChecks::progress,
            this, &CalibrateAudioDialog::devProgress);
    connect(devChecks, &DevChecks::finished,
            this, &CalibrateAudioDialog::devFinished);
}

bool
CalibrateAudioDialog::devChecksWanted() const
{
    return m_devChecksBox->isChecked();
}

void
CalibrateAudioDialog::setDevChecksWanted(bool wanted)
{
    m_devChecksBox->setChecked(wanted);
}

void
CalibrateAudioDialog::setDevOptions(const DevChecks::Options &options)
{
    m_devOptions = options;
}

bool
CalibrateAudioDialog::startDevChecks(const AudioCheckResult &calibration)
{
    if (!m_devChecksBox->isChecked() || !m_devChecks) return false;

    // They place their takes with what the calibration measured, which
    // means nothing unless it can be used
    if (!calibration.calibrationUsable()) {
        m_devNote = tr("The dev checks did not run: they need a calibration "
                       "that can be used.");
        return false;
    }

    DevChecks::Options options = m_devOptions;
    options.roundTrip = calibration.calibratedRoundTrip;
    if (!m_devChecks->start(options)) {
        m_devNote = tr("The dev checks could not start.");
        return false;
    }

    m_devRunning = true;
    m_step->setText(tr("Calibrated. Starting the dev checks..."));
    m_timeLeft->setText(QString());
    m_bar->setRange(0, 0);
    indicate(tr("Calibrated. Starting the dev checks"), -1);
    return true;
}

void
CalibrateAudioDialog::devProgress(QString stage, int stageNumber, int stages)
{
    if (!m_devRunning) return;
    m_step->setText(tr("Dev checks, stage %1 of %2: %3...")
                    .arg(stageNumber).arg(stages).arg(stage));
    m_timeLeft->setText(QString());
    indicate(tr("Dev checks, stage %1 of %2: %3")
             .arg(stageNumber).arg(stages).arg(stage), -1);
}

void
CalibrateAudioDialog::devFinished(const DevReport &report)
{
    // Only those that carried on from a calibration of this dialog's
    if (!m_devRunning) return;
    m_devRunning = false;
    m_devReport = report;
    m_haveDevReport = true;
    m_bar->setRange(0, 1000);
    runEnded();
}

QString
CalibrateAudioDialog::devHtml() const
{
    QString html;
    if (m_devNote != "") html += paragraph(m_devNote.toHtmlEscaped());
    if (!m_haveDevReport) return html;

    html += paragraph(boldHtml(tr("Dev checks")));
    if (m_devReport.failure != "") {
        html += paragraph(tr("They ended early: %1")
                          .arg(m_devReport.failure.toHtmlEscaped()));
    }
    html += "<ul>";
    for (const CheckResult &c : m_devReport.checks) {
        html += "<li>" + tr("Item %1, %2: %3").arg(c.item)
            .arg(c.name.toHtmlEscaped())
            .arg(CheckResult::verdictName(c.verdict)) + "</li>";
    }
    html += "</ul>";
    html += paragraph(m_devReport.reportPath != "" ?
                      tr("Report: %1").arg(m_devReport.reportPath
                                           .toHtmlEscaped()) :
                      tr("The report could not be written."));
#ifdef Q_OS_ANDROID
    // Where a phone keeps it, only Tony can read it
    if (m_devReport.reportPath != "") {
        html += paragraph(tr("Copy and Save Report... take the report with "
                             "this page."));
    }
#endif
    return html;
}

QString
CalibrateAudioDialog::devReportText() const
{
    if (!m_haveDevReport || m_devReport.reportPath == "") return QString();

    // As the file has it, which is what a run on the desktop sends back
    QFile file(m_devReport.reportPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        cerr << "CalibrateAudioDialog: the dev checks' report "
             << m_devReport.reportPath << " could not be read: "
             << file.errorString() << endl;
        return "\n" + tr("The dev checks' report, %1, could not be read: %2")
            .arg(m_devReport.reportPath, file.errorString()) + "\n";
    }
    return "\n" + tr("The dev checks' report, %1:")
        .arg(m_devReport.reportPath) + "\n\n" +
        QString::fromUtf8(file.readAll());
}

#endif

CalibrateAudioDialog::Page
CalibrateAudioDialog::page() const
{
    return Page(m_pages->currentIndex());
}

QString
CalibrateAudioDialog::pageText() const
{
    QTextDocument document;
    switch (page()) {
    case Page::Instructions:
        document.setHtml(m_instructions->text());
        return document.toPlainText();
    case Page::Progress:
        return m_step->text() + "\n" + m_timeLeft->text();
    case Page::Result:
        document.setHtml(m_resultText->text());
        return document.toPlainText();
    }
    return QString();
}

bool
CalibrateAudioDialog::canUseLatency() const
{
    return page() == Page::Result &&
        !m_useButton->isHidden() && m_useButton->isEnabled();
}

void
CalibrateAudioDialog::present()
{
    // The devices and the latency may have changed since it was last
    // shown; a check of its own that is running stays on show
    if (!m_running) {
        m_instructions->setText(instructionsHtml());
#ifdef TONY_DEV_CHECKS
        // On each time the instructions are shown afresh
        m_devChecksBox->setChecked(true);
#endif
        showPage(Page::Instructions);
    }
    expand();
}

void
CalibrateAudioDialog::collapse()
{
    if (!m_running || !m_indicator) return;
    m_collapsed = true;
    m_indicator->show();
    hide();
}

void
CalibrateAudioDialog::expand()
{
    m_collapsed = false;
    if (m_indicator) m_indicator->hide();
    fitToWindow();
    show();
    // Again, with the frame a desktop gives it when shown, if it is known
    // by now: the dialog stays where it is, made smaller if it must be
    fitToWindow();
    raise();
    activateWindow();
}

void
CalibrateAudioDialog::indicate(QString text, int permille)
{
    if (!m_indicator) return;
    m_indicator->setText(text);
    m_indicator->setProgress(permille);
}

void
CalibrateAudioDialog::startCheck()
{
    if (m_running) return;

#ifdef Q_OS_ANDROID
    // The takes need the microphone, which Android asks the user for, and
    // answers later: the check starts then, if this is still on show.
    // A take started without it would ask, and start once it is given,
    // long after the check had given up (AudioCheckRunner)
    if (!m_window->microphoneAllowed()) {
        QPointer<CalibrateAudioDialog> dialog(this);
        m_window->askForMicrophone([dialog]() {
            if (dialog && dialog->isVisible()) dialog->startCheck();
        });
        return;
    }
#endif

    m_result = AudioCheckResult();
    m_latencyKept = false;
    m_expectedSeconds = expectedSeconds(m_plan);
    m_shownPermille = 0;
    m_bar->setRange(0, 1000);
    m_bar->setValue(0);
#ifdef TONY_DEV_CHECKS
    m_devRunning = false;
    m_haveDevReport = false;
    m_devReport = DevReport();
    m_devNote = QString();
#endif
    m_step->setText(tr("Starting the check..."));
    m_timeLeft->setText(QString());
    indicate(tr("Starting the check"), 0);

    // The runner says nothing from inside start(), so the page is set
    // for what comes after it either way
    m_running = true;
    showPage(Page::Progress);

    if (!m_runner->start(m_plan)) {
        m_running = false;
        AudioCheckResult refused;
        refused.failure = tr("The check could not start while something "
                             "is being recorded. Stop the recording, then "
                             "try again.");
        showResult(refused);
        return;
    }

#ifdef Q_OS_ANDROID
    // No one touches the phone for as long as the check runs, a few
    // minutes with the dev checks, and a screen gone off would stop its
    // take (AndroidScreen)
    AndroidScreen::keepOn(true, "a check of the audio is running");
#endif

    // Out of the way of the takes it records, which are the thing to
    // watch while it runs
    collapse();
}

void
CalibrateAudioDialog::cancelCheck()
{
#ifdef TONY_DEV_CHECKS
    // Its dev checks, which end at once and report through devFinished()
    if (m_devRunning) {
        if (m_devChecks) {
            m_devChecks->cancel();
        } else {
            // Gone with nothing said (the window is going)
            m_devRunning = false;
            runEnded();
        }
        return;
    }
#endif

    // The run ends at once, and its end comes back through
    // runnerFinished()
    if (m_running) m_runner->cancel();
}

void
CalibrateAudioDialog::useLatency()
{
    if (!canUseLatency()) return;
    if (m_window->storeMeasuredLatency(m_result)) {
        m_latencyKept = true;
    } else {
        cerr << "CalibrateAudioDialog: the measured latency was not kept"
             << endl;
    }
    m_resultText->setText(resultHtml());
    showPage(Page::Result);
}

QString
CalibrateAudioDialog::reportText() const
{
    QTextDocument document;
    document.setHtml(resultHtml());
    QString text = tr("%1, Calibrate Audio, %2")
        .arg(QCoreApplication::applicationName(),
             QDateTime::currentDateTime().toString(Qt::ISODate)) +
        "\n\n" + document.toPlainText() + "\n";
#ifdef TONY_DEV_CHECKS
    text += devReportText();
#endif
    return text;
}

void
CalibrateAudioDialog::copyReport()
{
    QGuiApplication::clipboard()->setText(reportText());
    m_copyButton->setText(tr("Copied"));
}

#ifdef Q_OS_ANDROID
void
CalibrateAudioDialog::saveReport()
{
    QString name = "tony-calibration";
#ifdef TONY_DEV_CHECKS
    if (m_haveDevReport) {
        name = "tony-dev-checks";
        cerr << "CalibrateAudioDialog: the report saved holds the dev "
             << "checks' report, " << m_devReport.reportPath << endl;
    }
#endif
    m_window->saveTextThroughPicker
        (this, reportText().toUtf8(), tr("Save the report"),
         QString("%1-%2.txt").arg(name)
         .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")),
         tr("report"));
}
#endif

void
CalibrateAudioDialog::showResult(const AudioCheckResult &result)
{
    m_result = result;
#ifdef TONY_DEV_CHECKS
    m_haveDevReport = false;
    m_devReport = DevReport();
    m_devNote = QString();
#endif
    showResultPage();
}

void
CalibrateAudioDialog::showResultPage()
{
    m_latencyKept = false;
    m_resultText->setText(resultHtml());
    showPage(Page::Result);
}

void
CalibrateAudioDialog::runEnded()
{
    m_running = false;
#ifdef Q_OS_ANDROID
    AndroidScreen::keepOn(false, "the check has ended");
#endif
    showResultPage();
    // Back from small by itself, to say how the run went
    if (m_collapsed) expand();
}

void
CalibrateAudioDialog::reject()
{
    // Nothing else would show how the run went, and a check left running
    // behind a closed dialog would go on playing chirps
    if (m_running) cancelCheck();
    QDialog::reject();
}

void
CalibrateAudioDialog::runnerProgress(const AudioCheckRunner::Progress &p)
{
    if (!m_running) return;
#ifdef TONY_DEV_CHECKS
    // The runner's part in the dev checks, which show their own stages
    if (m_devRunning) return;
#endif

    // The step in full for the progress page, and in a few words for the
    // indicator, whose line has the time left too
    QString step, brief;
    switch (p.step) {
    case AudioCheckRunner::Step::Idle:
        return;
    case AudioCheckRunner::Step::OpeningReference:
        step = tr("Opening the test session...");
        brief = tr("Opening the test session");
        break;
    case AudioCheckRunner::Step::AnalysingReference:
        step = tr("Getting the test reference ready...");
        brief = tr("Getting the test reference ready");
        break;
    case AudioCheckRunner::Step::Recording:
        step = tr("Recording punch-in %1 of %2. Keep the earcup against the "
                  "microphone.").arg(p.punchIn).arg(p.punchIns);
        brief = tr("Recording punch-in %1 of %2")
            .arg(p.punchIn).arg(p.punchIns);
        break;
    case AudioCheckRunner::Step::AnalysingTake:
        step = tr("Analysing punch-in %1 of %2...")
            .arg(p.punchIn).arg(p.punchIns);
        brief = tr("Analysing punch-in %1 of %2")
            .arg(p.punchIn).arg(p.punchIns);
        break;
    }
    m_step->setText(step);

    // The recording still to come, and a rough time for each analysis
    // not yet done: the reference's, and that of every take from the one
    // being recorded or analysed on
    int analyses = p.punchIns - std::max(p.punchIn, 1) + 1;
    if (p.step == AudioCheckRunner::Step::OpeningReference ||
        p.step == AudioCheckRunner::Step::AnalysingReference) {
        analyses = p.punchIns + 1;
    }
    const double left = p.secondsLeft + kSecondsPerAnalysis * analyses;
    if (m_expectedSeconds > 0.0) {
        int permille = int(1000.0 * (1.0 - left / m_expectedSeconds));
        m_shownPermille =
            std::max(m_shownPermille, std::min(1000, std::max(0, permille)));
        m_bar->setValue(m_shownPermille);
    }
    const int seconds = int(std::ceil(left));
    m_timeLeft->setText(tr("About %1 seconds left").arg(seconds));
    indicate(tr("%1, %2 s left").arg(brief).arg(seconds), m_shownPermille);
}

void
CalibrateAudioDialog::runnerFinished(const AudioCheckResult &result)
{
    // A run started elsewhere is not this dialog's to show
    if (!m_running) return;
#ifdef TONY_DEV_CHECKS
    // Nor is the runner's part in the dev checks, which report for
    // themselves.  A calibration carries on into them if they are
    // wanted, and is shown with their report when they end
    if (m_devRunning) return;
    m_result = result;
    if (startDevChecks(result)) return;
#else
    m_result = result;
#endif
    runEnded();
}

void
CalibrateAudioDialog::showPage(Page page)
{
    m_pages->setCurrentIndex(int(page));

    m_startButton->setVisible(page == Page::Instructions);
    m_smallButton->setVisible(page == Page::Progress);
    m_cancelButton->setVisible(page == Page::Progress);
    m_againButton->setVisible(page == Page::Result);
    m_closeButton->setVisible(page != Page::Progress);
    m_copyButton->setVisible(page == Page::Result);
    m_copyButton->setText(tr("Copy"));
#ifdef Q_OS_ANDROID
    m_saveButton->setVisible(page == Page::Result);
#endif
    m_useButton->setVisible(page == Page::Result &&
                            m_result.calibrationUsable());
    m_useButton->setEnabled(!m_latencyKept);

    if (page == Page::Instructions) m_startButton->setDefault(true);
    if (page == Page::Result) m_closeButton->setDefault(true);

    fitToWindow();
}

void
CalibrateAudioDialog::fitToWindow()
{
    ensurePolished();

    const QRect area = windowArea();
    if (!area.isValid()) return;

    // The layouts keep the text areas' heights for a width until told
    // that they may have changed, which a new text does not tell them
    m_instructionsArea->updateGeometry();
    m_resultArea->updateGeometry();
    m_buttons->invalidate();

    // A desktop's window frame, once the dialog has been shown in one; a
    // phone draws none
    const QSize frame =
        (frameGeometry().size() - size()).expandedTo(QSize(0, 0));
    const QSize room = area.size() - frame;

    const int wanted = std::max(fontMetrics().averageCharWidth() *
                                preferredWidth,
                                minimumSizeHint().width());
    const int width = std::max(1, std::min(wanted, room.width()));
    const int height = std::max(1, std::min(heightFor(width), room.height()));
    resize(width, height);

    // Centred when it is about to be shown, as Qt would centre it, but
    // clear of a phone's bars, which Qt's centring is not; where the user
    // put it when it is on show
    const QRect placed =
        PopupArea::place(size() + frame, area, pos(), !isVisible());
    move(placed.topLeft());

    // Said when it changes, for a report from a phone
    const QRect now(pos(), size());
    if (now != m_fitted) {
        m_fitted = now;
        cerr << "CalibrateAudioDialog: " << now.width() << "x"
             << now.height() << " at " << now.x() << "," << now.y()
             << " within " << area.x() << "," << area.y() << " "
             << area.width() << "x" << area.height() << endl;
    }
}

QRect
CalibrateAudioDialog::windowArea() const
{
    QScreen *screen = m_window->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return QRect();

    // As TouchMenuStyle keeps menus inside it: on a phone the available
    // geometry is the whole screen, bars and all, and the window's safe
    // area margins are the bars
    QRect window;
    QMargins safeArea;
    if (m_window->isVisible()) {
        window = QRect(m_window->mapToGlobal(QPoint(0, 0)), m_window->size());
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        if (QWindow *handle = m_window->windowHandle()) {
            safeArea = handle->safeAreaMargins();
        }
#endif
    }
    return PopupArea::usable(screen->availableGeometry(), window, safeArea, 0);
}

int
CalibrateAudioDialog::heightFor(int width) const
{
    const QMargins margins = layout()->contentsMargins();
    const int inner = width - margins.left() - margins.right();

    // The page on show only: the stack would give the tallest page's
    QWidget *page = m_pages->currentWidget();
    const int pageHeight = page->hasHeightForWidth() ?
        page->heightForWidth(inner) : page->sizeHint().height();

    return margins.top() + pageHeight + std::max(0, layout()->spacing()) +
        m_buttons->sizeHint().height() + margins.bottom();
}

QString
CalibrateAudioDialog::instructionsHtml() const
{
    // The devices the Preferences name, or on a phone the route it has
    // open: the phone chooses it, and a figure is kept for each route
    const LatencyCalibration::Key devices = m_window->latencyKey(0);
    const AudioRoute::Route route = m_window->audioRoute();
#ifdef Q_OS_ANDROID
    // Before a file is opened no device is, and the route is not known
    const bool phone = true;
#else
    const bool phone = (route.driver != "");
#endif

    // In fives of seconds: the analyses' share is a guess
    const int seconds = 5 * int(std::ceil(expectedSeconds(m_plan) / 5.0));

    QString html;
    if (!phone) {
        html += paragraph
            (tr("Tony plays short chirps and records them, to measure how "
                "late recordings arrive through your devices. What it "
                "measures is used to place your takes on the reference."));
        html += paragraph(boldHtml(tr("Before you start:")));
        html += "<ul><li>" +
            tr("Hold one earcup of your headphones against the microphone, "
               "%1: the chirps are sharp.").arg(boldHtml(tr("off your ears"))) +
            "</li><li>" +
            tr("Set a moderate volume, and keep the room quiet.") +
            "</li></ul>";
    } else {
        html += paragraph
            (tr("Tony plays short chirps and records them, to measure how "
                "late recordings arrive through the phone's output and "
                "input below. What it measures is used to place your takes "
                "on the reference, whenever the phone has these two."));
        html += paragraph(boldHtml(tr("Before you start:")));
        html += "<ul><li>" +
            tr("Plug in what you will sing with first: each output and "
               "input is calibrated on its own, and a Bluetooth headset "
               "is much later than the phone's speaker.") +
            "</li><li>" +
            tr("With wired headphones, hold one earcup against the phone's "
               "microphone (usually at its bottom edge), %1: the chirps "
               "are sharp.").arg(boldHtml(tr("off your ears"))) +
            "</li><li>" +
            tr("With nothing plugged in, the phone's own speaker and "
               "microphone make the loop: lay it down in a quiet room.") +
            "</li><li>" +
            tr("A headset with a microphone of its own records from that "
               "microphone: hold the earcup against it.") +
            "</li><li>" +
            tr("Set a moderate volume, and keep the room quiet.") +
            "</li></ul>";
    }
    // Open for playback only, a phone says which input it records from
    // only when it records: the one calibrated with this output before,
    // if there is one, else the phone's choice.  With no device open, it
    // says nothing yet
    QString output = deviceName(devices.playbackDevice);
    QString input = deviceName(devices.recordDevice);
    if (phone && route.driver == "") {
        output = tr("the phone chooses when the check starts");
        input = output;
    } else if (phone && !route.hasInput) {
        input = (devices.recordDevice != "" ?
                 tr("%1, as when it was calibrated; the phone chooses when "
                    "recording starts").arg(devices.recordDevice) :
                 tr("the phone chooses when recording starts"));
    }
    html += "<table cellspacing=\"4\">";
    html += "<tr><td>" + tr("Output:") + "</td><td>" +
        output.toHtmlEscaped() + "</td></tr>";
    html += "<tr><td>" + tr("Input:") + "</td><td>" +
        input.toHtmlEscaped() + "</td></tr>";
    html += "<tr><td>" + tr("Latency in use:") + "</td><td>" +
        describeLatency(m_window->latencyInUse()).toHtmlEscaped() +
        "</td></tr>";
    html += "</table>";
    html += paragraph
        (tr("The check replaces the session that is open with a test session "
            "of its own, and asks you to save your work first. It takes about "
            "%1 seconds. Your song is not changed: open it again from File "
            "▸ Open Recent afterwards.").arg(seconds));
    return html;
}

QString
CalibrateAudioDialog::resultHtml() const
{
#ifdef TONY_DEV_CHECKS
    return calibrationHtml() + devHtml();
#else
    return calibrationHtml();
#endif
}

QString
CalibrateAudioDialog::calibrationHtml() const
{
    const AudioCheckResult &r = m_result;

    if (r.failure != "") {
        return paragraph(boldHtml(tr("The check did not finish."))) +
            paragraph(r.failure.toHtmlEscaped());
    }

    const LatencyCheck::TakeSummary &s = r.summary;
    const double driver = r.reportedOutputLatency + r.reportedInputLatency;

    // Recorded through a route a phone reported: its advice is a phone's
    const bool phone = (r.route.driver != "");
    const QString measured = milliseconds(r.calibratedRoundTrip);
    const QString timing = milliseconds(timingSpread(s));

    // The verdict in plain words, and what to do about it
    QString html;
    switch (s.verdict) {
    case Verdict::Ok:
        html += paragraph(boldHtml(tr("The test sounds came back steadily, "
                                  "%1 after they were played.")
                               .arg(measured)));
        html += paragraph
            (tr("Press Use this latency to place your takes with it."));
        break;
    case Verdict::NoSignal:
        html += paragraph(boldHtml(tr("Tony could not hear the test sounds: "
                                  "it found %1 of %2.")
                               .arg(s.found).arg(s.judged)));
        if (phone) {
            html += "<ul><li>" +
                tr("Turn the volume up, and hold the earcup right against "
                   "the phone's microphone, or with nothing plugged in, "
                   "lay the phone down in a quiet room.") + "</li><li>" +
                tr("Check that %1 may use the microphone (the phone's "
                   "Settings, Apps, %1, Permissions).")
                .arg(QCoreApplication::applicationName()) + "</li><li>" +
                tr("The phone may be cancelling echo or suppressing noise "
                   "on the microphone, which takes out what it plays "
                   "itself, although Tony asks for the microphone "
                   "unprocessed.") + "</li></ul>";
            break;
        }
        html += "<ul><li>" +
            tr("Turn the volume up, and hold the earcup right against "
               "the microphone.") + "</li><li>" +
            tr("Check that the microphone is not muted, and that it is "
               "the one Tony records from (Playback ▸ Audio Input "
               "Device).") + "</li><li>" +
            tr("In Windows, turn off Sound settings ▸ your microphone ▸ "
               "Audio enhancements.") + "</li><li>" +
            tr("Do not record through a Bluetooth headset's "
               "\"Hands-Free\" device: it records at telephone quality.") +
            "</li></ul>";
        break;
    case Verdict::Clipped:
        html += paragraph(boldHtml(tr("The test sounds were too loud: the "
                                  "recording reached full scale.")));
        html += paragraph(tr("Turn the volume down, or hold the earcup a "
                             "little away from the microphone, and "
                             "check again."));
        break;
    case Verdict::Fading:
        html += paragraph(boldHtml(tr("The test sounds got quieter as the "
                                  "check went on, by %1 dB.")
                               .arg(QLocale().toString
                                    (s.fadingDb, 'f', 0))));
        if (phone) {
            html += paragraph
                (tr("Something on the phone is filtering the microphone, "
                    "such as echo cancellation or noise suppression, "
                    "although Tony asks for the microphone unprocessed. "
                    "Check again; if it fades again, send the report."));
            break;
        }
        html += paragraph
            (tr("Something is filtering the microphone, such as echo "
                "cancellation or audio enhancements. In Windows, turn "
                "off Sound settings ▸ your microphone ▸ Audio "
                "enhancements, and check again."));
        break;
    case Verdict::PositionDependent:
        html += paragraph(boldHtml(tr("The delay grew from one punch-in to "
                                  "the next.")));
        html += paragraph
            (tr("The recording seems to run at another speed than the "
                "playback, so no one latency places every take right."));
        break;
    case Verdict::Scattered:
        html += paragraph(boldHtml(tr("The driver's timing varies from take "
                                  "to take by %1.").arg(timing)));
        html += paragraph
            (tr("No one latency places every take right when it varies "
                "that much. Close other programs that use sound, and "
                "check again."));
        break;
    case Verdict::Unsteady:
        html += paragraph(boldHtml(tr("The driver's timing varies from take "
                                  "to take by %1.").arg(timing)));
        html += paragraph
            (tr("That is small enough to use: the measured round trip, "
                "%1, is the middle of it. Press Use this latency to place "
                "your takes with it.").arg(measured));
        break;
    }

    if (s.echo.heard) {
        html += paragraph
            (tr("Your microphone is being played back somewhere (Windows "
                "\"Listen to this device\", or an interface's direct "
                "monitor): every test sound came back a second time, %1 "
                "later. Turn that off, or your takes will be heard twice.")
             .arg(milliseconds(s.echo.delaySeconds)));
    }

    if (m_latencyKept) {
        html += paragraph(boldHtml(tr("Kept.")) + " " +
                          tr("Takes on these devices are now placed with %1.")
                          .arg(measured));
    }

    // The figures, for whoever wants them, and for passing on
    auto row = [](QString name, QString value) {
        return "<tr><td>" + name + "</td><td>" + value.toHtmlEscaped() +
            "</td></tr>";
    };
    // What the sweeps found says how far the driver's figure is out even
    // when it cannot be used, as long as enough of them were found
    const bool haveMeasurement = s.found > 0 &&
        s.verdict != Verdict::NoSignal;

    html += "<table cellspacing=\"4\">";
    html += row(tr("Round trip:"),
                (haveMeasurement ?
                 tr("%1 measured").arg(measured) : tr("not measured")) +
                tr("; the driver reports %1 (%2 out, %3 in)")
                .arg(milliseconds(driver))
                .arg(milliseconds(r.reportedOutputLatency))
                .arg(milliseconds(r.reportedInputLatency)));
    if (!r.takes.empty()) {
        html += row(tr("Takes placed with:"),
                    (r.takes.front().measured ?
                     tr("%1, measured before") : tr("%1, the driver's figure"))
                    .arg(milliseconds(r.usedRoundTrip)));
    }
    QStringList offsets;
    for (const LatencyCheck::PunchInResult &p : s.punchIns) {
        offsets << (p.found > 0 ?
                    signedMilliseconds(p.medianOffset) : tr("nothing found"));
    }
    if (!offsets.isEmpty()) {
        html += row(tr("Punch-ins landed:"),
                    tr("%1 (+ is late)").arg(offsets.join(", ")));
    }
    html += row(tr("Spread between punch-ins:"), milliseconds(s.spread));
    html += row(tr("Test sounds found:"),
                tr("%1 of %2").arg(s.found).arg(s.judged));
    // A device at another rate than the reference's is a fact, not a
    // fault: its recordings are converted as they are spliced
    html += row(tr("Sample rates:"),
                (r.recordingRate != r.referenceRate ?
                 tr("recorded at %1 Hz, converted to the reference's %2 Hz") :
                 tr("recorded at %1 Hz, reference at %2 Hz"))
                .arg(hertz(r.recordingRate)).arg(hertz(r.referenceRate)));
    html += row(tr("Input peak:"),
                s.inputPeak > 0.0 ?
                tr("%1 dBFS").arg(QLocale().toString
                                  (20.0 * std::log10(s.inputPeak), 'f', 1)) :
                tr("silence"));
    html += row(tr("Echo:"),
                s.echo.heard ?
                tr("%1 after the sound, %2 dB %3")
                .arg(milliseconds(s.echo.delaySeconds))
                .arg(QLocale().toString(std::fabs(s.echo.levelDb), 'f', 0))
                .arg(s.echo.levelDb <= 0.0 ? tr("quieter") : tr("louder")) :
                tr("none heard"));
    html += row(tr("Devices:"),
                tr("output %1; input %2")
                .arg(deviceName(r.key.playbackDevice),
                     deviceName(r.key.recordDevice)));
    // What a figure kept for the route is checked against
    if (phone) {
        html += row(tr("Streams:"), tr("output %1; input %2")
                    .arg(r.route.outputStreams, r.route.inputStreams));
    }
    html += "</table>";
    return html;
}

QString
CalibrateAudioDialog::describeLatency(const LatencyCalibration::InUse &inUse)
{
    if (inUse.source == LatencyCalibration::Source::Measured) {
        if (!inUse.date.isValid()) {
            return tr("measured %1").arg(milliseconds(inUse.roundTrip));
        }
        // The year only when it is not this one
        const QDate day = inUse.date.toLocalTime().date();
        const QString format =
            day.year() == QDate::currentDate().year() ? "d MMM" : "d MMM yyyy";
        return tr("measured %1, %2").arg(milliseconds(inUse.roundTrip))
            .arg(QLocale().toString(day, format));
    }

    // The device reports its latencies once it is open
    if (!(inUse.roundTrip > 0.0)) {
        return tr("driver's figure, not known yet");
    }
    QString text = tr("driver's figure, %1").arg(milliseconds(inUse.roundTrip));
    if (inUse.stale) {
        text = tr("%1 (the measured one is out of date)").arg(text);
    }
    return text;
}

QString
CalibrateAudioDialog::milliseconds(double seconds)
{
    const double ms = seconds * 1000.0;
    if (std::fabs(ms) < 9.95) {
        double tenths = std::round(ms * 10.0) / 10.0;
        if (tenths == 0.0) tenths = 0.0; // not "-0"
        const int decimals = (tenths == std::round(tenths)) ? 0 : 1;
        return tr("%1 ms").arg(QLocale().toString(tenths, 'f', decimals));
    }
    return tr("%1 ms").arg(QLocale().toString(std::round(ms), 'f', 0));
}

QString
CalibrateAudioDialog::signedMilliseconds(double seconds)
{
    if (std::round(seconds * 10000.0) > 0.0) {
        return "+" + milliseconds(seconds);
    }
    return milliseconds(seconds);
}

QString
CalibrateAudioDialog::deviceName(QString name)
{
    // As the device menus call it
    return name == "" ? tr("(System Default)") : name;
}

QString
CalibrateAudioDialog::hertz(sv_samplerate_t rate)
{
    return QString::number(qint64(std::llround(rate)));
}
