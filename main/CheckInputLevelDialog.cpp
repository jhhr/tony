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

#include "CheckInputLevelDialog.h"
#include "InputLevel.h"
#include "InputLevelFeed.h"
#include "InputLevelMeter.h"
#include "UserText.h"
#include "VoiceThreshold.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// A change in dB, whole
QString dbText(double db)
{
    return CheckInputLevelDialog::tr("%1 dB").arg(std::lround(std::fabs(db)));
}

// Under this the singing hardly reached the input at all
const double nothingHeardDb = -50.0;

// A gain within this of the target is right as it is
const double closeEnoughDb = 2.0;

// A threshold this near the loudest peak leaves soft singing under it:
// the voice's level is about 10 dB under its peaks, and soft singing
// 15 dB or more under the loudest
const double softSingingDb = 25.0;

}

CheckInputLevelDialog::CheckInputLevelDialog(InputLevelFeed *feed,
                                             QWidget *parent) :
    QDialog(parent),
    m_feed(feed),
    m_stage(Stage::Quiet),
    m_currentThreshold(VoiceThreshold::kOff),
    m_firstReadingMs(-1),
    m_loudest(0.f),
    m_clipped(false)
{
    setWindowTitle(tr("Check Input Level"));
    setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_label = new QLabel;
    m_label->setWordWrap(true);
    m_label->setTextFormat(Qt::RichText);
    m_label->setMinimumWidth(fontMetrics().averageCharWidth() * 56);
    layout->addWidget(m_label);

    // Large, and labelled
    m_meter = new InputLevelMeter(feed);
    m_meter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_meter->setFixedHeight(fontMetrics().height() * 4);
    m_meter->setLabelled(true);
    layout->addWidget(m_meter);

    QDialogButtonBox *buttons = new QDialogButtonBox;
    m_done = buttons->addButton(tr("Done"), QDialogButtonBox::AcceptRole);
    m_useThreshold = buttons->addButton(QString(),
                                        QDialogButtonBox::ActionRole);
    m_again = buttons->addButton(tr("Check Again"),
                                 QDialogButtonBox::ActionRole);
    m_close = buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    // Done ends the check, not the dialog
    connect(m_done, &QPushButton::clicked,
            this, &CheckInputLevelDialog::finish);
    connect(m_again, &QPushButton::clicked,
            this, [this]() { start(m_currentThreshold); });
    connect(m_useThreshold, &QPushButton::clicked, this, [this]() {
        const double t = suggestedThreshold();
        m_currentThreshold = t;
        emit thresholdChosen(t);
        m_useThreshold->hide();
        m_label->setText(resultText());
    });
    connect(m_close, &QPushButton::clicked, this, &QDialog::reject);

    if (m_feed) {
        connect(m_feed, &InputLevelFeed::levelRead,
                this, &CheckInputLevelDialog::levelRead);
    }
    connect(&m_timer, &QTimer::timeout, this, &CheckInputLevelDialog::tick);
}

CheckInputLevelDialog::~CheckInputLevelDialog()
{
}

void
CheckInputLevelDialog::start(double currentThreshold)
{
    m_currentThreshold = currentThreshold;
    m_quietPeaks.clear();
    m_loudest = 0.f;
    m_clipped = false;
    m_firstReadingMs = -1;
    m_clock.start();
    // As a take's start does: a light lit before is not this check's
    if (m_feed) m_feed->setClipped(false);
    showStage(Stage::Quiet);
    m_timer.start(100);
}

void
CheckInputLevelDialog::levelRead(float peak)
{
    if (!isVisible()) return;
    const std::int64_t ms = m_clock.elapsed();

    if (m_stage == Stage::NoInput) {
        // It came after all: from the silence again
        start(m_currentThreshold);
    }

    if (m_stage == Stage::Quiet) {
        if (m_firstReadingMs < 0) m_firstReadingMs = ms;
        m_quietPeaks.push_back(InputLevel::dbfs(peak));
        if (ms - m_firstReadingMs >= kQuietMs) showStage(Stage::Singing);
        return;
    }

    if (m_stage == Stage::Singing) {
        m_loudest = std::max(m_loudest, std::fabs(peak));
        if (std::fabs(peak) >= InputLevel::kFullScale) m_clipped = true;
    }
}

void
CheckInputLevelDialog::tick()
{
    if (m_stage == Stage::Quiet && m_firstReadingMs < 0 &&
        m_clock.elapsed() >= kNoInputMs) {
        showStage(Stage::NoInput);
    }
    if (m_stage == Stage::Result) m_timer.stop();
}

void
CheckInputLevelDialog::finish()
{
    if (m_stage != Stage::Singing) return;
    showStage(Stage::Result);
}

double
CheckInputLevelDialog::peakDbfs() const
{
    return InputLevel::dbfs(m_loudest);
}

double
CheckInputLevelDialog::noiseFloorDbfs() const
{
    return InputLevel::noiseFloor(m_quietPeaks);
}

double
CheckInputLevelDialog::suggestedThreshold() const
{
    return InputLevel::suggestedThreshold(noiseFloorDbfs());
}

QString
CheckInputLevelDialog::text() const
{
    QTextDocument doc;
    doc.setHtml(m_label->text());
    return doc.toPlainText();
}

void
CheckInputLevelDialog::showStage(Stage stage)
{
    m_stage = stage;
    m_done->setVisible(stage == Stage::Quiet || stage == Stage::Singing);
    m_done->setEnabled(stage == Stage::Singing);
    m_again->setVisible(stage == Stage::Result || stage == Stage::NoInput);
    m_useThreshold->hide();

    switch (stage) {
    case Stage::Quiet:
        m_label->setText
            (tr("<p><b>Stay quiet for two seconds</b>: the silence is being "
                "measured, as the noise floor.</p>"
                "<p>If you sing with the music on speakers, play it now, at "
                "the volume you sing with: it is then part of what the "
                "voice threshold has to stay over.</p>"));
        break;
    case Stage::Singing:
        m_label->setText
            (tr("<p><b>Now sing your loudest phrase</b>, as loud as you sing "
                "in a take, with the microphone where it is when you sing. "
                "Then press Done.</p>"));
        break;
    case Stage::NoInput:
        m_label->setText
            (tr("<p><b>The input has delivered nothing.</b> Check that the "
                "microphone is allowed, and that Playback &gt; Audio Input "
                "Device is the interface it is plugged into, then press "
                "Check Again.</p>"));
        break;
    case Stage::Result: {
        m_label->setText(resultText());
        const double suggested = suggestedThreshold();
        if (suggested != m_currentThreshold) {
            m_useThreshold->setText(tr("Set Voice Threshold to %1")
                                    .arg(VoiceThreshold::label(suggested)));
            m_useThreshold->show();
        }
        break;
    }
    }
}

QString
CheckInputLevelDialog::resultText() const
{
    const double peak = peakDbfs();
    const double noise = noiseFloorDbfs();
    const double suggested = suggestedThreshold();

    QString text;

    if (m_clipped) {
        text += tr("<p><b>Your loudest phrase clipped</b>: it reached full "
                   "scale, where how loud it really was can no longer be "
                   "read. Turn the input gain on your interface down by 10 dB "
                   "or more, and check again.</p>");
    } else if (peak < nothingHeardDb) {
        text += tr("<p><b>Hardly anything reached the input</b>: its loudest "
                   "peak was %1. Is the microphone plugged in, on the input "
                   "chosen under Playback &gt; Input Channel, and its gain "
                   "up?</p>").arg(UserText::dbfs(peak));
    } else {
        const double change = InputLevel::gainChange(peak);
        text += tr("<p>Your loudest peak: <b>%1</b>.</p>")
            .arg(UserText::dbfs(peak));
        if (std::fabs(change) <= closeEnoughDb) {
            text += tr("<p>The gain is right: your loudest peaks are near "
                       "%1.</p>")
                .arg(UserText::dbfs(InputLevel::kTargetPeakDbfs));
        } else if (change < 0.0) {
            text += tr("<p><b>Turn the input gain down by about %1</b>, to "
                       "put your loudest peaks near %2, which leaves room "
                       "for a take sung louder than this.</p>")
                .arg(dbText(change))
                .arg(UserText::dbfs(InputLevel::kTargetPeakDbfs));
        } else {
            text += tr("<p><b>Turn the input gain up by about %1</b>, to put "
                       "your loudest peaks near %2.</p>")
                .arg(dbText(change))
                .arg(UserText::dbfs(InputLevel::kTargetPeakDbfs));
        }
    }

    text += tr("<p>The silence before you sang, the noise floor: <b>%1</b>.")
        .arg(UserText::dbfs(noise));
    if (!m_clipped && peak >= nothingHeardDb && noise > -200.0) {
        text += tr(" That is %1 under your loudest peak.")
            .arg(dbText(peak - noise));
    }
    text += "</p>";

    if (!VoiceThreshold::isOn(suggested)) {
        text += tr("<p>Voice Threshold: <b>Off</b> will do: the live dots "
                   "leave a noise this quiet out by themselves.</p>");
    } else {
        text += tr("<p>Voice Threshold: <b>%1</b>, 5 dB or more over the "
                   "noise's peaks. Singing gets dots only where its level, "
                   "some 10 dB under its peaks, is over it.</p>")
            .arg(VoiceThreshold::label(suggested));
        if (!m_clipped && peak - suggested < softSingingDb) {
            text += tr("<p>That is close to your loudest singing: soft "
                       "singing may fall under it. Sing closer to the "
                       "microphone, or turn the music down.</p>");
        }
    }
    if (suggested == m_currentThreshold) {
        text += tr("<p>It is set to that now.</p>");
    }

    text += tr("<p><small>Only clipping in the interface's converter can be "
               "seen here. A microphone that distorts in its own "
               "electronics, before the converter, has to be "
               "heard.</small></p>");
    return text;
}
